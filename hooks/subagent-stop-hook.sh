#!/bin/bash
# SubagentStop hook: Capture team/agent learnings into chitta
#
# Fires when a subagent (teammate, research agent, etc.) finishes.
# Input JSON: { agent_id, agent_type, agent_transcript_path, last_assistant_message, session_id }
#
# Extracts key findings from the agent's last message and stores them
# as memories tagged with the agent type for cross-session recall.

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-2}"

[[ ! -x "$CHITTA_BIN" ]] && exit 0

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/lib.sh"

daemon_available || exit 0

# Parse JSON input
INPUT=$(cat)
AGENT_ID=$(echo "$INPUT" | jq -r '.agent_id // empty' 2>/dev/null)
AGENT_TYPE=$(echo "$INPUT" | jq -r '.agent_type // "general-purpose"' 2>/dev/null)
AGENT_TRANSCRIPT=$(echo "$INPUT" | jq -r '.agent_transcript_path // empty' 2>/dev/null)
LAST_MSG=$(echo "$INPUT" | jq -r '.last_assistant_message // empty' 2>/dev/null)
SESSION_ID=$(echo "$INPUT" | jq -r '.session_id // "default"' 2>/dev/null)

[[ -z "$AGENT_ID" ]] && exit 0

# Detect realm
REALM=$(timeout "$MAX_WAIT" "$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")

# Extract learnings from the agent's last message
if [[ -n "$LAST_MSG" && ${#LAST_MSG} -gt 50 ]]; then
    # Look for typed markers in agent output
    while IFS= read -r line; do
        if [[ "$line" =~ ^\[(SOLUTION|GOTCHA|DECISION|FAILURE|PATTERN)\] ]]; then
            type="${BASH_REMATCH[1]}"
            content="${line#\[$type\] }"
            ssl="[$REALM:$AGENT_TYPE] $content"
            queue_write "remember" "{\"content\":$(echo "$ssl" | jq -Rs .),\"kind\":\"${type,,}\",\"tags\":[\"agent\",\"$AGENT_TYPE\",\"$AGENT_ID\"]}"
            echo "[soul] +agent-${type,,} from $AGENT_TYPE: ${content:0:60}" >&2
        fi
    done <<< "$LAST_MSG"

    # Store a compact summary of what the agent accomplished
    SUMMARY=$(echo "$LAST_MSG" | head -c 300 | tr '\n' ' ')
    queue_write "remember" "{\"content\":$(echo "[$REALM:agent-result] $AGENT_TYPE ($AGENT_ID): $SUMMARY" | jq -Rs .),\"kind\":\"episode\",\"tags\":[\"agent-result\",\"$AGENT_TYPE\"]}"
    echo "[soul] +agent-result from $AGENT_TYPE ($AGENT_ID)" >&2
fi

# Register transcript for later distillation if available
if [[ -n "$AGENT_TRANSCRIPT" && -f "$AGENT_TRANSCRIPT" ]]; then
    queue_write "transcript_register" "{\"session_id\":\"${AGENT_ID}\",\"transcript_path\":$(echo "$AGENT_TRANSCRIPT" | jq -Rs .),\"realm\":\"$REALM\"}"
fi

exit 0
