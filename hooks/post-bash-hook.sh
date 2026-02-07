#!/bin/bash
# PostToolUse hook for Bash: Surface relevant memories on command failure
#
# When a command fails, searches for gotchas/corrections related to the command
# and injects them as context for debugging.

# Don't use set -e: we want hooks to succeed even if some parts fail

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MIND_PATH="${MIND_PATH:-$HOME/.claude/mind}"
LAST_CMD_FILE="$MIND_PATH/.last_bash_cmd"

[[ ! -x "$CHITTA_BIN" ]] && exit 0

# Read stdin (PostToolUse gets tool result)
STDIN_DATA=$(cat)

# Extract exit code and command
exit_code=$(echo "$STDIN_DATA" | jq -r '.tool_result.exit_code // 0')
command=$(echo "$STDIN_DATA" | jq -r '.tool_input.command // empty')
output=$(echo "$STDIN_DATA" | jq -r '.tool_result.stdout // empty' | head -c 500)
stderr=$(echo "$STDIN_DATA" | jq -r '.tool_result.stderr // empty' | head -c 500)

# Normalize command to first word (basename only)
normalize_cmd() {
    echo "$1" | awk '{print $1}' | sed 's|.*/||'
}

# Track command sequence for habit learning on success
if [[ "$exit_code" == "0" && -n "$command" ]]; then
    curr_cmd=$(normalize_cmd "$command")

    # Read previous command if exists
    if [[ -f "$LAST_CMD_FILE" ]]; then
        prev_cmd=$(cat "$LAST_CMD_FILE" 2>/dev/null)

        # Record habit if both commands present and different
        if [[ -n "$prev_cmd" && "$prev_cmd" != "$curr_cmd" ]]; then
            # Queue habit_observe async to not block
            (
                "$CHITTA_BIN" habit_observe \
                    --trigger "bash:$prev_cmd" \
                    --response "bash:$curr_cmd" 2>/dev/null || true
            ) &
        fi
    fi

    # Save current command for next time
    mkdir -p "$MIND_PATH" 2>/dev/null
    echo "$curr_cmd" > "$LAST_CMD_FILE"

    exit 0
fi

[[ -z "$command" ]] && exit 0

json_escape() {
    echo -n "$1" | jq -Rs '.' | sed 's/^"//;s/"$//'
}

# Build query from command and error
query="$command"
if [[ -n "$stderr" ]]; then
    # Extract key error terms
    error_terms=$(echo "$stderr" | grep -oiE '(error|failed|not found|permission denied|no such|cannot|invalid)' | head -3 | tr '\n' ' ')
    [[ -n "$error_terms" ]] && query="$query $error_terms"
fi

escaped_query=$(json_escape "$query")

# Search for gotchas and corrections
memories=$(timeout 2 "$CHITTA_BIN" recall --query "$escaped_query" --limit 2 --text-only 2>/dev/null | head -c 400 || true)

if [[ -n "$memories" && "$memories" != *"No memories"* ]]; then
    escaped_mem=$(json_escape "$memories")
    echo "{\"hookSpecificOutput\":{\"hookEventName\":\"PostToolUse\",\"additionalContext\":\"🔴 COMMAND FAILED (exit $exit_code) - Related memories:\\n$escaped_mem\"}}"
fi

exit 0
