#!/bin/bash
# UserPromptSubmit hook: Surface relevant memories
#
# HIGH PERFORMANCE: Single recall with short timeout, fail gracefully

set -e

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-2}"

# Parse JSON input
INPUT=$(cat)
QUERY=$(echo "$INPUT" | jq -r '.prompt // empty')

[[ -z "$QUERY" ]] && exit 0
[[ ! -x "$CHITTA_BIN" ]] && exit 0

# Single recall with short timeout - fail silently if slow
memories=$(timeout "$MAX_WAIT" "$CHITTA_BIN" recall --query "$QUERY" --limit 3 2>/dev/null || true)

if [[ -n "$memories" && "$memories" != *"No memories"* ]]; then
    echo "[soul]"
    # Just output first 3 lines of results
    echo "$memories" | grep -E '^\[[0-9]+%\]' | head -3 | while read -r line; do
        # Truncate long lines
        echo "${line:0:150}"
    done
fi

exit 0
