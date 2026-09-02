#!/bin/bash
# Outcome ledger: fail-open JSONL event log. Hooks append `injected` (which
# memories rode into a turn) and `bash_outcome`/`session_end` events; the
# offline joiner (chitta-mcp/outcome_ledger.py) correlates them into per-memory
# success/failure credit. Never blocks or fails the calling hook.
#
# Usage: source outcome-ledger.sh; ledger_append '{"event":"injected","ids":[1]}' "$SESSION_ID"
# Adds ts (epoch ms) and session_id to the given JSON object before appending.

# ledger_append <event-json> [session_id]
ledger_append() {
    local event_json="$1"
    local session_id="${2:-${SESSION_ID:-unknown}}"
    {
        [[ -z "$event_json" ]] && return 0
        local mind="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"
        mkdir -p "$mind"
        local ts
        ts=$(date +%s%3N)
        printf '%s' "$event_json" | jq -c --argjson ts "$ts" --arg sid "$session_id" \
            '. + {ts: $ts, session_id: $sid}' >> "${mind}/outcome_ledger.jsonl"
    } 2>/dev/null
    return 0
}
