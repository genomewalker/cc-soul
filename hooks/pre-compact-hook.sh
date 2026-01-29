#!/bin/bash
# PreCompact hook: Save checkpoint before context compaction
#
# HIGH PERFORMANCE: Queue write only (no blocking)

set -e

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
QUEUE_FILE="${CHITTA_QUEUE:-/tmp/chitta-queue.jsonl}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-2}"

# Parse JSON input
INPUT=$(cat)
TRIGGER=$(echo "$INPUT" | jq -r '.trigger // "auto"')

# Check chitta CLI exists
[[ ! -x "$CHITTA_BIN" ]] && exit 0

# Queue write - fire and forget
queue_write() {
    local tool="$1" args="$2"
    echo "{\"tool\":\"$tool\",\"args\":$args,\"ts\":$(date +%s)}" >> "$QUEUE_FILE"
}

# Detect realm
REALM=$(timeout "$MAX_WAIT" "$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")

SESSION_ID="compact-$(date +%Y%m%d-%H%M%S)"
snapshot="Context compacted ($TRIGGER)"

# Queue ledger save (fire-and-forget)
queue_write "ledger_save" "{\"session_id\":\"$SESSION_ID\",\"project\":\"$REALM\",\"mood\":\"pre-compact\",\"snapshot\":$(echo "$snapshot" | jq -Rs .)}"

echo "[checkpoint] queued: $SESSION_ID ($TRIGGER)" >&2

exit 0
