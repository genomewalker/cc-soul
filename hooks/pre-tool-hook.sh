#!/bin/bash
# PreToolUse hook: Inject code intel and memories before tool execution
#
# Features:
# - Thinking drift detection (only re-query if reasoning changed)
# - Code intel injection for file operations
# - Memory surfacing based on file/thinking context
# - SSL-formatted output: [conf%:type] content
#
# Input (JSON via stdin):
#   tool_name, tool_input, tool_use_id, session_id, transcript_path
#
# Output (JSON):
#   hookSpecificOutput.additionalContext - SSL-formatted context

set -e

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-3}"
STATE_FILE="${HOME}/.claude/soul-pretool-state.json"

# Parse JSON input
INPUT=$(cat)
TOOL_NAME=$(echo "$INPUT" | jq -r '.tool_name // empty')
TOOL_INPUT=$(echo "$INPUT" | jq -r '.tool_input // {}')
SESSION_ID=$(echo "$INPUT" | jq -r '.session_id // "unknown"')
TRANSCRIPT_PATH=$(echo "$INPUT" | jq -r '.transcript_path // empty')

# Only process on read-only tools (writes are time-sensitive)
case "$TOOL_NAME" in
    Read|Grep|Glob|Task|WebSearch|WebFetch) ;;
    *) exit 0 ;;
esac

# Check chitta CLI exists
[[ ! -x "$CHITTA_BIN" ]] && exit 0

json_escape() {
    echo -n "$1" | jq -Rs '.' | sed 's/^"//;s/"$//'
}

# Map category to short type
map_type() {
    case "$1" in
        solution|SOLUTION) echo "sol" ;;
        gotcha|GOTCHA) echo "gotcha" ;;
        preference|PREFERENCE) echo "pref" ;;
        decision|DECISION) echo "dec" ;;
        failure|FAILURE) echo "fail" ;;
        pattern|PATTERN) echo "pat" ;;
        insight|wisdom) echo "insight" ;;
        *) echo "mem" ;;
    esac
}

# Extract thinking from transcript (last assistant message)
THINKING=""
if [[ -n "$TRANSCRIPT_PATH" && -f "$TRANSCRIPT_PATH" ]]; then
    THINKING=$(tac "$TRANSCRIPT_PATH" 2>/dev/null | grep -m1 '"role":"assistant"' | \
        jq -r '.message.content[] | select(.type=="thinking") | .thinking // empty' 2>/dev/null | \
        tail -c 4000)

    # Fallback to text content if no thinking block
    if [[ -z "$THINKING" ]]; then
        THINKING=$(tac "$TRANSCRIPT_PATH" 2>/dev/null | grep -m1 '"role":"assistant"' | \
            jq -r '.message.content[] | select(.type=="text") | .text // empty' 2>/dev/null | \
            tail -c 4000)
    fi
fi

# Thinking drift detection - skip if unchanged
if [[ -n "$THINKING" ]]; then
    THINKING_HASH=$(echo "$THINKING" | md5sum | cut -d' ' -f1)

    if [[ -f "$STATE_FILE" ]]; then
        LAST_HASH=$(jq -r --arg sid "$SESSION_ID" '.[$sid].hash // empty' "$STATE_FILE" 2>/dev/null)

        if [[ "$THINKING_HASH" == "$LAST_HASH" ]]; then
            exit 0
        fi
    fi

    # Update state file
    mkdir -p "$(dirname "$STATE_FILE")"
    if [[ -f "$STATE_FILE" ]]; then
        jq --arg sid "$SESSION_ID" --arg hash "$THINKING_HASH" \
            '.[$sid] = {hash: $hash, ts: now}' "$STATE_FILE" > "${STATE_FILE}.tmp" 2>/dev/null && \
            mv "${STATE_FILE}.tmp" "$STATE_FILE"
    else
        echo "{\"$SESSION_ID\":{\"hash\":\"$THINKING_HASH\",\"ts\":$(date +%s)}}" > "$STATE_FILE"
    fi
fi

CONTEXT=""

# 1. File-based context (for Read tool)
FILE_PATH=$(echo "$TOOL_INPUT" | jq -r '.file_path // .path // empty')
if [[ -n "$FILE_PATH" && "$TOOL_NAME" == "Read" ]]; then
    FILENAME=$(basename "$FILE_PATH")

    # Code intel - symbols in this file
    symbols=$(timeout "$MAX_WAIT" "$CHITTA_BIN" find_symbol --name "$FILENAME" --kind file --limit 5 2>/dev/null || true)
    if [[ -n "$symbols" && "$symbols" != *"No symbols"* && "$symbols" != *"not indexed"* ]]; then
        symbol_list=$(echo "$symbols" | grep -oE '^[A-Za-z_][A-Za-z0-9_]*' | head -5 | tr '\n' ', ' | sed 's/,$//')
        [[ -n "$symbol_list" ]] && CONTEXT="[code:$FILENAME] $symbol_list"
    fi

    # File-specific memories
    memories=$(timeout "$MAX_WAIT" "$CHITTA_BIN" recall --query "$FILE_PATH" --limit 2 2>/dev/null || true)
    if [[ -n "$memories" && "$memories" != *"No memories"* ]]; then
        while read -r line; do
            [[ -z "$line" ]] && continue
            conf=$(echo "$line" | grep -oE '^\[[0-9]+%\]' | tr -d '[]%')
            cat=$(echo "$line" | sed 's/^\[[0-9]*%\] //' | grep -oE '^\[[^]]+\]' | tr -d '[]')
            content=$(echo "$line" | sed 's/^\[[0-9]*%\] \[[^]]*\] //' | sed 's/ ([a-f0-9-]*)$//' | cut -c1-80)
            type=$(map_type "$cat")
            [[ -n "$conf" && -n "$content" ]] && CONTEXT="$CONTEXT
[${conf}%:${type}] $content"
        done <<< "$(echo "$memories" | grep -E '^\[[0-9]+%\]' | head -2)"
    fi
fi

# 2. Thinking-based context (drift detection)
if [[ -n "$THINKING" ]]; then
    memories=$(timeout "$MAX_WAIT" "$CHITTA_BIN" recall --query "$THINKING" --limit 2 2>/dev/null || true)
    if [[ -n "$memories" && "$memories" != *"No memories"* ]]; then
        while read -r line; do
            [[ -z "$line" ]] && continue
            # Skip if already in context
            content_check=$(echo "$line" | sed 's/^\[[0-9]*%\] \[[^]]*\] //' | cut -c1-40)
            [[ "$CONTEXT" == *"$content_check"* ]] && continue

            conf=$(echo "$line" | grep -oE '^\[[0-9]+%\]' | tr -d '[]%')
            cat=$(echo "$line" | sed 's/^\[[0-9]*%\] //' | grep -oE '^\[[^]]+\]' | tr -d '[]')
            content=$(echo "$line" | sed 's/^\[[0-9]*%\] \[[^]]*\] //' | sed 's/ ([a-f0-9-]*)$//' | cut -c1-80)
            type=$(map_type "$cat")
            [[ -n "$conf" && -n "$content" ]] && CONTEXT="$CONTEXT
[drift:${conf}%:${type}] $content"
        done <<< "$(echo "$memories" | grep -E '^\[[0-9]+%\]' | head -2)"
    fi
fi

# Output context if we have any
if [[ -n "$CONTEXT" ]]; then
    CONTEXT=$(echo "$CONTEXT" | head -c 500)
    cat <<EOF
{
  "hookSpecificOutput": {
    "hookEventName": "PreToolUse",
    "additionalContext": "$(json_escape "$CONTEXT")"
  }
}
EOF
fi

exit 0
