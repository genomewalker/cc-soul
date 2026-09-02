#!/bin/bash
# Structured span capture for agent learning
# Inspired by Microsoft's agent-lightning approach
#
# Captures tool uses with outcomes and reward signals
# Outputs JSONL spans to ~/.claude/mind/spans/

# Don't use set -e: we want hooks to succeed even if some parts fail

MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"
SPANS_DIR="$MIND_PATH/spans"

# Source shared library for queue_write with ack_id
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/lib.sh"

# Input: bounded Stop snapshot (preferred) or legacy transcript path, plus the
# last user message. The Stop hook always passes a snapshot so this script does
# not reopen the complete session transcript.
TRANSCRIPT_PATH="$1"
LAST_USER_MSG="$2"

[[ -z "$TRANSCRIPT_PATH" || ! -f "$TRANSCRIPT_PATH" ]] && exit 0

mkdir -p "$SPANS_DIR"

# Generate span file for this exchange
TIMESTAMP=$(date +%s)
IS_SNAPSHOT=false
if jq -e '.format == "cc-soul-stop-snapshot-v1"' "$TRANSCRIPT_PATH" >/dev/null 2>&1; then
    IS_SNAPSHOT=true
fi
if [[ "$IS_SNAPSHOT" == "true" ]]; then
    # One jq pass over the snapshot instead of three (session_id, tool_uses,
    # tool_results were each a separate parse of the same file). tostring
    # collapses the two array outputs to single lines so -r yields exactly
    # three lines regardless of pretty-printing.
    mapfile -t _SPAN_PARSED < <(jq -r '
        (.session_id // "unknown"),
        ([.tool_spans[]? | {id, tool, input}] | tostring),
        ([.tool_spans[]? | {id, output, is_error}] | tostring)
    ' "$TRANSCRIPT_PATH" 2>/dev/null)
    SESSION_ID="${_SPAN_PARSED[0]:-unknown}"
    TOOL_USES="${_SPAN_PARSED[1]:-[]}"
    TOOL_RESULTS="${_SPAN_PARSED[2]:-[]}"
else
    SESSION_ID=$(basename "$TRANSCRIPT_PATH" .json)
fi
SESSION_ID=$(printf '%s' "$SESSION_ID" | tr -c 'A-Za-z0-9._-' '_')
SPAN_FILE="$SPANS_DIR/${SESSION_ID}-${TIMESTAMP}.jsonl"

# Extract tool uses and results from transcript
# Format: array of {tool_use_id, tool_name, input} and {tool_use_id, output, is_error}

if [[ "$IS_SNAPSHOT" != "true" ]]; then
    TOOL_USES=$(jq -c '
      [.[] | select(.role == "assistant") | .message.content[]? |
       select(.type == "tool_use") |
       {id: .id, tool: .name, input: .input}]
    ' "$TRANSCRIPT_PATH" 2>/dev/null || echo "[]")

    TOOL_RESULTS=$(jq -c '
      [.[] | select(.role == "user") | .message.content[]? |
       select(.type == "tool_result") |
       {id: .tool_use_id, output: (.content // .text // "" | tostring | .[0:500]), is_error: (.is_error // false)}]
    ' "$TRANSCRIPT_PATH" 2>/dev/null || echo "[]")
fi

# Detect reward signal from user's next message
detect_reward() {
    local msg="$1"
    [[ -z "$msg" ]] && echo "0" && return

    local msg_lower=$(echo "$msg" | tr '[:upper:]' '[:lower:]')

    # Strong positive signals
    if echo "$msg_lower" | grep -qE '^(yes|perfect|great|thanks|exactly|good|nice|awesome|that works)'; then
        echo "1"
        return
    fi

    # Strong negative signals
    if echo "$msg_lower" | grep -qE '^(no[,. ]|wrong|that.s not|incorrect|actually|fix|error|bug|doesn.t work)'; then
        echo "-1"
        return
    fi

    # Neutral
    echo "0"
}

REWARD=$(detect_reward "$LAST_USER_MSG")

# Match tool uses with results and emit spans
echo "$TOOL_USES" | jq -c '.[]' 2>/dev/null | while read -r tool_use; do
    [[ -z "$tool_use" || "$tool_use" == "null" ]] && continue

    TOOL_ID=$(echo "$tool_use" | jq -r '.id')
    TOOL_NAME=$(echo "$tool_use" | jq -r '.tool')
    TOOL_INPUT=$(echo "$tool_use" | jq -c '.input')

    # Find matching result
    RESULT=$(echo "$TOOL_RESULTS" | jq -c --arg id "$TOOL_ID" '.[] | select(.id == $id)' 2>/dev/null)

    if [[ -n "$RESULT" && "$RESULT" != "null" ]]; then
        IS_ERROR=$(echo "$RESULT" | jq -r '.is_error')
        OUTPUT=$(echo "$RESULT" | jq -r '.output')

        # Determine success: not an error AND no negative reward
        SUCCESS="true"
        [[ "$IS_ERROR" == "true" ]] && SUCCESS="false"
        [[ "$REWARD" == "-1" ]] && SUCCESS="false"

        # Emit span
        SPAN=$(jq -nc \
            --arg ts "$TIMESTAMP" \
            --arg session "$SESSION_ID" \
            --arg tool "$TOOL_NAME" \
            --argjson input "$TOOL_INPUT" \
            --arg output "${OUTPUT:0:500}" \
            --arg success "$SUCCESS" \
            --arg reward "$REWARD" \
            --arg is_error "$IS_ERROR" \
            '{
                type: "tool_span",
                timestamp: ($ts | tonumber),
                session_id: $session,
                tool: $tool,
                input: $input,
                output: $output,
                success: ($success == "true"),
                reward: ($reward | tonumber),
                is_error: ($is_error == "true")
            }')

        echo "$SPAN" >> "$SPAN_FILE"

        # If this was a failure, learn from it
        if [[ "$SUCCESS" == "false" ]]; then
            # Queue a failure learning (uses lib.sh queue_write with ack_id)
            FAILURE_CONTENT="[tool:$TOOL_NAME] failed with: ${OUTPUT:0:100}"
            queue_write "observe" "{\"category\":\"failure\",\"content\":$(echo "$FAILURE_CONTENT" | jq -Rs .)}"
        fi

        # If strong positive reward, learn the successful pattern
        if [[ "$REWARD" == "1" && "$SUCCESS" == "true" ]]; then
            # Extract key from input for learning (uses lib.sh queue_write with ack_id)
            INPUT_SUMMARY=$(echo "$TOOL_INPUT" | jq -r 'to_entries | map("\(.key)=\(.value | tostring | .[0:30])") | join(", ")' 2>/dev/null | head -c 100)
            SUCCESS_CONTENT="[tool:$TOOL_NAME] success: $INPUT_SUMMARY"
            queue_write "observe" "{\"category\":\"solution\",\"content\":$(echo "$SUCCESS_CONTENT" | jq -Rs .)}"
        fi
    fi
done

# Count spans captured
SPAN_COUNT=0
[[ -f "$SPAN_FILE" ]] && SPAN_COUNT=$(wc -l < "$SPAN_FILE")

# Emit summary span for the exchange
if [[ $SPAN_COUNT -gt 0 ]]; then
    SUMMARY=$(jq -nc \
        --arg ts "$TIMESTAMP" \
        --arg session "$SESSION_ID" \
        --arg reward "$REWARD" \
        --arg tool_count "$SPAN_COUNT" \
        '{
            type: "exchange_span",
            timestamp: ($ts | tonumber),
            session_id: $session,
            tool_count: ($tool_count | tonumber),
            reward: ($reward | tonumber)
        }')
    echo "$SUMMARY" >> "$SPAN_FILE"

    echo "[spans] +$SPAN_COUNT tools (reward=$REWARD)" >&2
fi

exit 0
