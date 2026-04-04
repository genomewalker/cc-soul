#!/bin/bash
# Reclaim charge on durable progress
# Usage: charge-reclaim.sh <amount> [reason]
# Called by stop-hook on commits, passing tests, etc.

AMOUNT="${1:-0}"
REASON="${2:-manual}"
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

SESSION_ID="${CLAUDE_SESSION_ID:-unknown}"
CHARGEBOOK="${SCRIPT_DIR}/../chargebook.md"
CHARGE_FILE="${MIND_PATH}/.charge_${SESSION_ID}"

BUDGET=$(grep -E '^budget:' "$CHARGEBOOK" 2>/dev/null | awk '{print $2}' | head -1)
BUDGET=${BUDGET:-100}

[[ ! -f "$CHARGE_FILE" ]] && echo "$BUDGET" > "$CHARGE_FILE"

CURRENT=$(cat "$CHARGE_FILE" 2>/dev/null || echo "$BUDGET")
NEW_BALANCE=$((CURRENT + AMOUNT))
# Cap at budget
[[ "$NEW_BALANCE" -gt "$BUDGET" ]] && NEW_BALANCE="$BUDGET"
echo "$NEW_BALANCE" > "$CHARGE_FILE"

printf '[charge] Reclaimed +%d (%s) → %d/%d\n' "$AMOUNT" "$REASON" "$NEW_BALANCE" "$BUDGET"
