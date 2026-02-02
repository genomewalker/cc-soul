#!/bin/bash
# PreToolUse hook: Inject corrections before command execution
#
# Only handles Bash commands to avoid performance impact on Read/Grep/Glob.
# Detects R/python/git patterns and surfaces relevant corrections.

set -e

MATCHER="${1:-}"
[[ -z "$MATCHER" ]] && exit 0

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
[[ ! -x "$CHITTA_BIN" ]] && exit 0

# Read stdin
STDIN_DATA=$(cat)

json_escape() {
    local text="$1"
    echo -n "$text" | jq -Rs '.' | sed 's/^"//;s/"$//'
}

case "$MATCHER" in
    Bash)
        command=$(echo "$STDIN_DATA" | jq -r '.tool_input.command // empty')
        [[ -z "$command" ]] && exit 0

        query=""
        # Detect R commands - high priority correction
        if echo "$command" | grep -qiE '(Rscript|R --vanilla|R -e|module.*load.*R)'; then
            query="R conda environment activation correction"
        # Detect Python commands
        elif echo "$command" | grep -qiE '(python3? |pip |conda activate)'; then
            query="python conda environment activation"
        fi

        if [[ -n "$query" ]]; then
            escaped_query=$(json_escape "$query")
            # Search for corrections
            memories=$(timeout 2 "$CHITTA_BIN" recall --query "$escaped_query" --tag "correction" --limit 1 --text-only 2>/dev/null | head -c 300)

            if [[ -n "$memories" && "$memories" != *"No memories"* ]]; then
                escaped_mem=$(json_escape "$memories")
                echo "{\"hookSpecificOutput\":{\"hookEventName\":\"PreToolUse\",\"additionalContext\":\"[CORRECTION] $escaped_mem\"}}"
            fi
        fi
        ;;
    *)
        # Other matchers disabled for performance
        exit 0
        ;;
esac
