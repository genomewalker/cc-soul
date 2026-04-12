#!/bin/bash
# PreToolUse charge gate — deduct charge for expensive casts
# Usage: charge-check.sh <tool_name>
# Exits 2 (block) only when charge is deeply negative (< -20).
# Warns to stderr when charge is low (<= 20).

TOOL="${1:-unknown}"
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

INPUT=$(cat)
SESSION_ID=$(echo "$INPUT" | jq -r '.session_id // empty' 2>/dev/null)
[[ -z "$SESSION_ID" ]] && SESSION_ID="${CLAUDE_SESSION_ID:-unknown}"

CHARGEBOOK="${SCRIPT_DIR}/../chargebook.md"
CHARGE_FILE="${MIND_PATH}/.charge_${SESSION_ID}"

BUDGET=$(grep -E '^budget:' "$CHARGEBOOK" 2>/dev/null | awk '{print $2}' | head -1)
BUDGET=${BUDGET:-100}

# Lazy init: new session starts at full budget
[[ ! -f "$CHARGE_FILE" ]] && echo "$BUDGET" > "$CHARGE_FILE"

CURRENT=$(cat "$CHARGE_FILE" 2>/dev/null || echo "$BUDGET")
COST=$(grep -E "^cost_${TOOL}:" "$CHARGEBOOK" 2>/dev/null | awk '{print $2}' | head -1)
COST=${COST:-0}

[[ "$COST" -eq 0 ]] && exit 0

NEW_BALANCE=$((CURRENT - COST))
echo "$NEW_BALANCE" > "$CHARGE_FILE"

# Hard block: deeply overdrawn
if [[ "$NEW_BALANCE" -lt -20 ]]; then
    printf '[charge] DEPLETED (%d/%d). Reclaim via /checkpoint, commit, or tests passing.\n' \
        "$NEW_BALANCE" "$BUDGET" >&2
    exit 2
fi

# Advisory: low charge
if [[ "$NEW_BALANCE" -le 20 ]]; then
    printf '[charge] Low: %d/%d remaining (-%d for %s)\n' \
        "$NEW_BALANCE" "$BUDGET" "$COST" "$TOOL" >&2
fi

exit 0
