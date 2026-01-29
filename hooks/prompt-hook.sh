#!/bin/bash
# UserPromptSubmit hook: Surface relevant memories
#
# HIGH PERFORMANCE: Single call with filtering
# - Uses full_resonate (better than recall - has spreading activation)
# - Filters out code symbols for non-code queries
# - Minimum 30% confidence threshold

set -e

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-2}"
MIN_CONFIDENCE=30

# Parse JSON input
INPUT=$(cat)
QUERY=$(echo "$INPUT" | jq -r '.prompt // empty')

[[ -z "$QUERY" ]] && exit 0
[[ ! -x "$CHITTA_BIN" ]] && exit 0

# Detect if query is about code (include symbols) or conversation (exclude symbols)
IS_CODE_QUERY=false
if echo "$QUERY" | grep -qiE '(function|class|method|implement|code|file|\.py|\.js|\.cpp|\.ts|error|bug|fix)'; then
    IS_CODE_QUERY=true
fi

# Use full_resonate for better semantic matching
memories=$(timeout "$MAX_WAIT" "$CHITTA_BIN" full_resonate --query "$QUERY" --k 6 2>/dev/null || true)

if [[ -z "$memories" || "$memories" == *"No memories"* ]]; then
    exit 0
fi

# Filter and format results
OUTPUT=""
COUNT=0
while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    [[ ! "$line" =~ ^\[[0-9]+%\] ]] && continue

    # Extract confidence
    conf=$(echo "$line" | grep -oE '^\[[0-9]+%\]' | tr -d '[]%')
    [[ -z "$conf" ]] && continue

    # Skip low confidence
    [[ "$conf" -lt "$MIN_CONFIDENCE" ]] && continue

    # Skip code symbols for non-code queries
    if [[ "$IS_CODE_QUERY" == "false" ]] && echo "$line" | grep -qE '\[symbol\]|\[code\]'; then
        continue
    fi

    # Truncate and add
    OUTPUT="$OUTPUT${line:0:150}
"
    ((COUNT++))
    [[ $COUNT -ge 3 ]] && break
done <<< "$memories"

# Only output if we have useful memories
if [[ -n "$OUTPUT" && $COUNT -gt 0 ]]; then
    echo "[soul]"
    echo -n "$OUTPUT"
fi

exit 0
