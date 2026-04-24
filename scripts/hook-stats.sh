#!/bin/bash
# Summarize code-intel hook shadow log.
# Usage: ./scripts/hook-stats.sh [/path/to/.hook_shadow.jsonl]
set -e

LOG="${1:-${CHITTA_DB_PATH:-$HOME/.claude/mind}/.hook_shadow.jsonl}"
if [[ ! -f "$LOG" ]]; then
    echo "No shadow log at $LOG"
    exit 1
fi

N=$(wc -l < "$LOG")
FIRST=$(head -1 "$LOG" | jq -r .ts)
LAST=$(tail -1 "$LOG" | jq -r .ts)
AGE_DAYS=$(( ( $(date +%s) - $(date -d "$FIRST" +%s 2>/dev/null || echo 0) ) / 86400 ))

echo "=== Shadow log: $LOG ==="
echo "Entries: $N  |  First: $FIRST  |  Last: $LAST  |  Age: ${AGE_DAYS}d"
echo

if [[ "$N" -ge 100 && "$AGE_DAYS" -ge 3 ]]; then
    echo "Auto-enforce: ACTIVE (≥100 entries, ≥3 days)"
else
    need_n=$((100 - N)); [[ "$need_n" -lt 0 ]] && need_n=0
    need_d=$((3 - AGE_DAYS)); [[ "$need_d" -lt 0 ]] && need_d=0
    echo "Auto-enforce: waiting — need $need_n more entries, $need_d more day(s)"
fi
echo

echo "--- Decisions ---"
jq -s 'group_by(.decision) | map({decision: .[0].decision, n: length}) | sort_by(-.n) | .[] | "\(.n)\t\(.decision)"' -r "$LOG"
echo
echo "--- Reasons ---"
jq -s 'group_by(.reason) | map({reason: .[0].reason, n: length}) | sort_by(-.n) | .[] | "\(.n)\t\(.reason)"' -r "$LOG"
echo
echo "--- By tool ---"
jq -s 'group_by(.tool) | map({tool: .[0].tool, n: length}) | .[] | "\(.n)\t\(.tool)"' -r "$LOG"
echo
echo "--- Indexed vs non-indexed ---"
jq -s 'map({idx: (if .indexed==1 then "indexed" else "non-indexed" end)}) | group_by(.idx) | map({k: .[0].idx, n: length}) | .[] | "\(.n)\t\(.k)"' -r "$LOG"
echo
echo "--- Enforced actions (if any) ---"
jq -s 'map(select(.enforced==1)) | group_by(.decision) | map({d: .[0].decision, n: length}) | .[] | "\(.n)\t\(.d)"' -r "$LOG"
