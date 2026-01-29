#!/bin/bash
# PreToolUse hook: Inject code intel and memories before tool execution
#
# Features:
# - Thinking drift detection (only re-query if reasoning changed)
# - Code intel injection for file operations
# - Memory surfacing based on file/thinking context
# - Only fires on read-only tools (not writes) for performance
# - SSL-formatted output: [conf%:type] content
#
# Input (JSON via stdin):
#   tool_name, tool_input, tool_use_id, session_id, transcript_path
#
# Output (JSON):
#   hookSpecificOutput.additionalContext - SSL-formatted context

set -e

MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind/chitta}"
CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
CHITTAD_BIN="${CHITTAD_BIN:-$HOME/.claude/bin/chittad}"
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

djb2_hash() {
    local str="$1" hash=5381 i c
    for ((i=0; i<${#str}; i++)); do
        c=$(printf '%d' "'${str:$i:1}")
        hash=$(( ((hash << 5) + hash) + c ))
        hash=$((hash & 0xFFFFFFFF))
    done
    echo "$hash"
}

SOCKET="/tmp/chitta-$(djb2_hash "$MIND_PATH").sock"

json_escape() {
    echo -n "$1" | jq -Rs '.' | sed 's/^"//;s/"$//'
}

rpc_call() {
    local tool="$1" args="$2"
    local request="{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"$tool\",\"arguments\":$args}}"
    [[ ! -S "$SOCKET" ]] && return 1
    echo "$request" | timeout "$MAX_WAIT" nc -U "$SOCKET" 2>/dev/null | head -1
}

extract_text() {
    echo "$1" | jq -r '.result.content[0].text // empty' 2>/dev/null
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

# Check daemon is running (don't start - too slow for PreToolUse)
[[ ! -S "$SOCKET" ]] && exit 0

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
    ESCAPED_FILE=$(json_escape "$FILENAME")

    # Code intel - symbols in this file
    response=$(rpc_call "find_symbol" "{\"name\":\"$ESCAPED_FILE\",\"kind\":\"file\",\"limit\":5}")
    symbols=$(extract_text "$response")

    if [[ -n "$symbols" && "$symbols" != *"No symbols"* && "$symbols" != *"not indexed"* ]]; then
        symbol_list=$(echo "$symbols" | grep -oE '^[A-Za-z_][A-Za-z0-9_]*' | head -5 | tr '\n' ', ' | sed 's/,$//')
        [[ -n "$symbol_list" ]] && CONTEXT="[code:$FILENAME] $symbol_list"
    fi

    # File-specific memories
    response=$(rpc_call "recall" "{\"query\":\"$(json_escape "$FILE_PATH")\",\"k\":2}")
    memories=$(extract_text "$response")

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
    response=$(rpc_call "recall" "{\"query\":\"$(json_escape "$THINKING")\",\"k\":2}")
    memories=$(extract_text "$response")

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
