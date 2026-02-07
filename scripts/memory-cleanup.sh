#!/bin/bash
# memory-cleanup.sh: One-time cleanup of existing noise in the soul
#
# Purpose: Remove low-quality memories that accumulated before quality gates were added.
# Run once after deploying the new quality gates, then periodically as needed.

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"

[[ ! -x "$CHITTA_BIN" ]] && echo "Error: chitta not found at $CHITTA_BIN" >&2 && exit 1

echo "=== Soul Memory Cleanup ===" >&2

# 1. Aggressive hygiene pass
echo "[1/3] Running aggressive hygiene pass..." >&2
$CHITTA_BIN hygiene_run --prune-threshold 0.15 --min-age-days 3 2>&1 || {
    echo "Warning: hygiene_run failed, continuing..." >&2
}

# 2. Restore code intel confidence
echo "[2/3] Restoring code intel confidence..." >&2
$CHITTA_BIN restore_code_intel_confidence --target 0.8 2>&1 || {
    echo "Warning: restore_code_intel_confidence failed, continuing..." >&2
}

# 3. Batch forget trivial session summaries
echo "[3/3] Removing trivial session summaries..." >&2
count=0
while IFS= read -r id; do
    [[ -z "$id" || "$id" == "id" ]] && continue
    $CHITTA_BIN forget --id "$id" 2>/dev/null && ((count++)) || true
done < <($CHITTA_BIN sql_query --query "SELECT id FROM memory WHERE content LIKE '%working→% turns%' AND confidence < 0.3" 2>/dev/null | tail -n +2)

echo "" >&2
echo "=== Cleanup Complete ===" >&2
echo "Removed $count trivial session summaries." >&2
echo "Run 'chitta hygiene_stats' to verify memory health." >&2
