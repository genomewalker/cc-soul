#!/bin/bash
# PostToolUse hook for Bash: Surface relevant memories on command failure
#
# When a command fails, searches for gotchas/corrections related to the command
# and injects them as context for debugging.

set -e

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
[[ ! -x "$CHITTA_BIN" ]] && exit 0

# Read stdin (PostToolUse gets tool result)
STDIN_DATA=$(cat)

# Extract exit code and command
exit_code=$(echo "$STDIN_DATA" | jq -r '.tool_result.exit_code // 0')
command=$(echo "$STDIN_DATA" | jq -r '.tool_input.command // empty')
output=$(echo "$STDIN_DATA" | jq -r '.tool_result.stdout // empty' | head -c 500)
stderr=$(echo "$STDIN_DATA" | jq -r '.tool_result.stderr // empty' | head -c 500)

# Only act on failures
[[ "$exit_code" == "0" ]] && exit 0
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
