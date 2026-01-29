#!/bin/bash
# UserPromptSubmit hook: Surface relevant memories for user query
#
# Input (JSON via stdin):
#   prompt - the user's query
#
# Output (stdout → injected as context):
#   SSL-formatted memories: [conf%:type] content

set -e

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-5}"

# Parse JSON input
INPUT=$(cat)
QUERY=$(echo "$INPUT" | jq -r '.prompt // empty')

[[ -z "$QUERY" ]] && exit 0

# Map category to short type
map_type() {
    case "$1" in
        solution|SOLUTION|WORKING_SOLUTION) echo "sol" ;;
        gotcha|GOTCHA|trap) echo "gotcha" ;;
        preference|PREFERENCE|pref) echo "pref" ;;
        decision|DECISION|dec) echo "dec" ;;
        failure|FAILURE|fail) echo "fail" ;;
        pattern|PATTERN|pat) echo "pat" ;;
        insight|wisdom) echo "insight" ;;
        correction) echo "fix" ;;
        *) echo "mem" ;;
    esac
}

# Check chitta CLI exists
[[ ! -x "$CHITTA_BIN" ]] && exit 0

# Detect realm for scoped recall
REALM=$("$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")

# Surface relevant memories using CLI (starts daemon if needed)
memories=$(timeout "$MAX_WAIT" "$CHITTA_BIN" full_resonate \
    --query "$QUERY" \
    --k 4 \
    --realm "$REALM" 2>/dev/null || true)

if [[ -n "$memories" && "$memories" != *"No memories"* ]]; then
    echo "[soul]"

    # Parse and format each memory line
    # Input format: [85%] [category] content (uuid)
    echo "$memories" | grep -E '^\[[0-9]+%\]' | head -4 | while read -r line; do
        # Extract confidence
        conf=$(echo "$line" | grep -oE '^\[[0-9]+%\]' | tr -d '[]%')
        # Extract category
        cat=$(echo "$line" | sed 's/^\[[0-9]*%\] //' | grep -oE '^\[[^]]+\]' | tr -d '[]')
        # Extract content (remove conf, cat, and trailing uuid)
        content=$(echo "$line" | sed 's/^\[[0-9]*%\] \[[^]]*\] //' | sed 's/ ([a-f0-9-]*)$//' | cut -c1-120)
        # Extract uuid if present
        uuid=$(echo "$line" | grep -oE '\([a-f0-9-]+\)$' | tr -d '()')

        type=$(map_type "$cat")

        if [[ -n "$uuid" ]]; then
            echo "[${conf}%:${type}:${uuid}] $content"
        else
            echo "[${conf}%:${type}] $content"
        fi
    done
fi

exit 0
