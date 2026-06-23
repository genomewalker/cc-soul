#!/bin/bash
# run-ledger.sh — session-end hook (G7).
# Emits a [run-ledger] signal memory recording how the current cognitive
# process performed this session. Consumed by future G8/G10/G11 scripts to
# decide whether enough data exists to train (see ledger_has_domain_entries).
#
# Stdin: Claude Code Stop-hook JSON ({session_id, transcript_path, ...}).
# Output signal:
#   [run-ledger] session:<ID> realm:<REALM> task_type:<T> genome_id:<G> \
#     n_wisdoms:<N> g0_delta:<D> ts:<ISO8601> status:complete

set -euo pipefail

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"

INPUT="$(cat 2>/dev/null || true)"

SESSION_ID="${CLAUDE_SESSION_ID:-}"
if [[ -z "$SESSION_ID" ]]; then
    SESSION_ID="$(jq -r '.session_id // empty' <<<"$INPUT" 2>/dev/null || true)"
fi
if [[ -z "$SESSION_ID" ]]; then
    SESSION_ID="$(cat /proc/sys/kernel/random/uuid 2>/dev/null || true)"
fi
[[ -z "$SESSION_ID" ]] && SESSION_ID="unknown-$$"

TRANSCRIPT_PATH="$(jq -r '.transcript_path // empty' <<<"$INPUT" 2>/dev/null || true)"

GENOME_ID="${CHITTA_GENOME_ID:-none}"
[[ -z "$GENOME_ID" ]] && GENOME_ID="none"

# REALM: most common realm tag across the last 5 recall-result realms.
REALM="$(
    "$CHITTA_BIN" recall --query "session realm" --limit 5 --json 2>/dev/null \
        | jq -r '[.results[]?.realm // empty | select(. != "")] | .[0:5] | .[]' 2>/dev/null \
        | sort | uniq -c | sort -rn | head -1 | awk '{print $2}'
)"
[[ -z "$REALM" ]] && REALM="unknown"

# TASK_TYPE: classify from transcript hints.
classify_task_type() {
    local tp="$1"
    [[ -z "$tp" || ! -f "$tp" ]] && { echo "unknown"; return; }
    local coding research analysis distill
    coding=$(grep -ciE '"name":"(Edit|Write|symbol_patch|file_patch)"|cargo|cmake|pytest|compile' "$tp" 2>/dev/null || echo 0)
    research=$(grep -ciE '"name":"(WebSearch|WebFetch|web_search|lit_search|paper_fetch)"|arxiv|biorxiv|literature' "$tp" 2>/dev/null || echo 0)
    analysis=$(grep -ciE 'analy[sz]|dataframe|plot|statistic|correlation|benchmark' "$tp" 2>/dev/null || echo 0)
    distill=$(grep -ciE 'distill|synthesi[sz]|\[wisdom\]|dream-sweep' "$tp" 2>/dev/null || echo 0)
    local best="unknown" max=0
    for pair in "coding:$coding" "research:$research" "analysis:$analysis" "distillation:$distill"; do
        local name="${pair%%:*}" cnt="${pair##*:}"
        if (( cnt > max )); then max=$cnt; best=$name; fi
    done
    echo "$best"
}
TASK_TYPE="$(classify_task_type "$TRANSCRIPT_PATH")"

# n_wisdoms: wisdom memories stored this session.
N_WISDOMS="$(
    "$CHITTA_BIN" recall --type wisdom --limit 100 --json 2>/dev/null \
        | jq '[.results[]?] | length' 2>/dev/null || echo 0
)"
[[ -z "$N_WISDOMS" ]] && N_WISDOMS=0

# g0_delta: placeholder until G11/GEPA populates it.
G0_DELTA="0.0"

TS="$(date -Iseconds)"

CONTENT="[run-ledger] session:${SESSION_ID} realm:${REALM} task_type:${TASK_TYPE} genome_id:${GENOME_ID} n_wisdoms:${N_WISDOMS} g0_delta:${G0_DELTA} ts:${TS} status:complete"

"$CHITTA_BIN" remember \
    --content "$CONTENT" \
    --type signal \
    --tags "run-ledger,provenance" \
    --realm brahman \
    --visibility 1 >/dev/null 2>&1 || true

# Guard for G8/G10/G11: true when ≥5 [run-ledger] signals exist (enough to train).
ledger_has_domain_entries() {
    "$CHITTA_BIN" recall --query "[run-ledger]" --type signal --limit 10 --json 2>/dev/null \
        | jq '[.results[] | select(.text | startswith("[run-ledger]"))] | length >= 5'
}

# When invoked directly with `has-entries`, expose the guard for callers.
if [[ "${1:-}" == "has-entries" ]]; then
    ledger_has_domain_entries
fi

exit 0
