#!/bin/bash
# Analyze captured spans to extract tool usage patterns
#
# Usage: analyze-spans.sh [--since HOURS] [--tool TOOL_NAME]
#
# Outputs:
#   - Tool success rates
#   - Common failure patterns
#   - High-reward tool uses (good examples)

set -e

MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"
SPANS_DIR="$MIND_PATH/spans"

SINCE_HOURS=24
TOOL_FILTER=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --since) SINCE_HOURS="$2"; shift 2 ;;
        --tool) TOOL_FILTER="$2"; shift 2 ;;
        *) shift ;;
    esac
done

[[ ! -d "$SPANS_DIR" ]] && echo "No spans directory" && exit 0

# Calculate cutoff timestamp
CUTOFF=$(($(date +%s) - SINCE_HOURS * 3600))

# Collect all spans from recent files
ALL_SPANS=$(mktemp)
trap 'rm -f "$ALL_SPANS"' EXIT

for f in "$SPANS_DIR"/*.jsonl; do
    [[ ! -f "$f" ]] && continue
    # Check file modification time
    FILE_TIME=$(stat -c %Y "$f" 2>/dev/null || stat -f %m "$f" 2>/dev/null)
    [[ $FILE_TIME -lt $CUTOFF ]] && continue
    cat "$f" >> "$ALL_SPANS"
done

SPAN_COUNT=$(wc -l < "$ALL_SPANS")
[[ $SPAN_COUNT -eq 0 ]] && echo "No spans in last ${SINCE_HOURS}h" && exit 0

echo "=== Span Analysis (last ${SINCE_HOURS}h) ==="
echo "Total spans: $SPAN_COUNT"
echo ""

# Tool success rates
echo "=== Tool Success Rates ==="
jq -r 'select(.type == "tool_span") | "\(.tool)\t\(.success)"' "$ALL_SPANS" 2>/dev/null | \
    awk -F'\t' '
    {
        tools[$1]++
        if ($2 == "true") success[$1]++
    }
    END {
        for (t in tools) {
            rate = (success[t] ? success[t] : 0) / tools[t] * 100
            printf "%-20s %3d/%3d (%5.1f%%)\n", t, (success[t] ? success[t] : 0), tools[t], rate
        }
    }' | sort -t'(' -k2 -rn

echo ""

# Reward distribution
echo "=== Reward Distribution ==="
jq -r 'select(.type == "tool_span") | .reward' "$ALL_SPANS" 2>/dev/null | \
    sort | uniq -c | awk '{
        if ($2 == "1") label = "positive"
        else if ($2 == "-1") label = "negative"
        else label = "neutral"
        printf "  %s: %d\n", label, $1
    }'

echo ""

# Common failures (tools with negative reward)
echo "=== Recent Failures ==="
jq -r 'select(.type == "tool_span" and .reward == -1) | "[\(.tool)] \(.output[0:80])"' "$ALL_SPANS" 2>/dev/null | \
    head -5

echo ""

# High-reward successes (good examples to learn from)
echo "=== Successful Patterns (reward=1) ==="
jq -r 'select(.type == "tool_span" and .reward == 1 and .success == true) |
    "[\(.tool)] \(.input | to_entries | map("\(.key)=\(.value | tostring | .[0:20])") | join(", ") | .[0:60])"' \
    "$ALL_SPANS" 2>/dev/null | head -5

echo ""

# If tool filter specified, show details
if [[ -n "$TOOL_FILTER" ]]; then
    echo "=== Details for tool: $TOOL_FILTER ==="
    jq -r --arg tool "$TOOL_FILTER" '
        select(.type == "tool_span" and .tool == $tool) |
        "[\(if .success then "OK" else "FAIL" end)] reward=\(.reward) \(.input | tostring | .[0:60])"
    ' "$ALL_SPANS" 2>/dev/null | head -10
fi

echo ""
echo "Span files: $(ls "$SPANS_DIR"/*.jsonl 2>/dev/null | wc -l)"
