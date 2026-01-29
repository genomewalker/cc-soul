#!/bin/bash
# SessionStart hook: Initialize soul context for new session
#
# Input (JSON via stdin):
#   session_id, transcript_path, cwd, source, model
#
# Output (stdout → injected as context):
#   Soul metrics, ledger state, active long-task

set -e

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-5}"

# Parse JSON input
INPUT=$(cat)
SESSION_ID=$(echo "$INPUT" | jq -r '.session_id // empty')
TRANSCRIPT_PATH=$(echo "$INPUT" | jq -r '.transcript_path // empty')
CWD=$(echo "$INPUT" | jq -r '.cwd // empty')

# Check chitta CLI exists
[[ ! -x "$CHITTA_BIN" ]] && exit 0

# Detect realm from cwd
REALM=$(timeout "$MAX_WAIT" "$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")

# Register transcript for distillation
if [[ -n "$TRANSCRIPT_PATH" && -f "$TRANSCRIPT_PATH" ]]; then
    timeout "$MAX_WAIT" "$CHITTA_BIN" transcript_register \
        --session_id "$SESSION_ID" \
        --transcript_path "$TRANSCRIPT_PATH" \
        --realm "$REALM" 2>/dev/null || true
fi

# Soul context - compact metrics
soul_json=$(timeout "$MAX_WAIT" "$CHITTA_BIN" soul_context --json 2>/dev/null || true)
if [[ -n "$soul_json" ]]; then
    nodes=$(echo "$soul_json" | jq -r '.result.structured.total_nodes // 0' 2>/dev/null)
    triplets=$(echo "$soul_json" | jq -r '.result.structured.triplet_count // 0' 2>/dev/null)
    conf=$(echo "$soul_json" | jq -r '.result.structured.avg_confidence // 0' 2>/dev/null | cut -c1-4)
    status=$(echo "$soul_json" | jq -r '.result.structured.status // "unknown"' 2>/dev/null)
    [[ "$nodes" != "0" ]] && echo "[soul] n=$nodes t=$triplets c=$conf $status"
fi

# Ledger checkpoint - restore session state
ledger_json=$(timeout "$MAX_WAIT" "$CHITTA_BIN" ledger_load --project "$REALM" --json 2>/dev/null || true)
ledger_found=$(echo "$ledger_json" | jq -r '.result.structured.found // false' 2>/dev/null)

if [[ "$ledger_found" == "true" ]]; then
    session_id=$(echo "$ledger_json" | jq -r '.result.structured.session_id // ""' 2>/dev/null)
    mood=$(echo "$ledger_json" | jq -r '.result.structured.mood // ""' 2>/dev/null)
    snapshot=$(echo "$ledger_json" | jq -r '.result.structured.snapshot // ""' 2>/dev/null)
    active_files=$(echo "$ledger_json" | jq -r '.result.structured.active_files // []' 2>/dev/null)
    next_steps=$(echo "$ledger_json" | jq -r '.result.structured.next_steps // []' 2>/dev/null)

    echo "[ledger] Restored: $session_id"
    [[ -n "$mood" && "$mood" != "null" && "$mood" != "working" ]] && echo "  mood: $mood"
    [[ -n "$snapshot" && "$snapshot" != "null" ]] && echo "  last: ${snapshot:0:100}"

    file_count=$(echo "$active_files" | jq 'length' 2>/dev/null || echo "0")
    if [[ "$file_count" -gt 0 ]]; then
        files_list=$(echo "$active_files" | jq -r '.[:3] | .[]' 2>/dev/null | xargs -I{} basename {} | tr '\n' ', ' | sed 's/,$//')
        [[ -n "$files_list" ]] && echo "  files: $files_list"
    fi

    next_count=$(echo "$next_steps" | jq 'length' 2>/dev/null || echo "0")
    if [[ "$next_count" -gt 0 ]]; then
        first_next=$(echo "$next_steps" | jq -r '.[0] // ""' 2>/dev/null)
        [[ -n "$first_next" && "$first_next" != "null" ]] && echo "  next: ${first_next:0:80}"
    fi
fi

# Active long-task injection
task_json=$(timeout "$MAX_WAIT" "$CHITTA_BIN" long_task_active --realm "$REALM" --json 2>/dev/null || true)
task_found=$(echo "$task_json" | jq -r '.result.structured.found // false' 2>/dev/null)

if [[ "$task_found" == "true" ]]; then
    task_id=$(echo "$task_json" | jq -r '.result.structured.task_id // ""' 2>/dev/null)
    if [[ -n "$task_id" ]]; then
        snapshot_json=$(timeout "$MAX_WAIT" "$CHITTA_BIN" long_task_snapshot --task_id "$task_id" --mode inject --json 2>/dev/null || true)
        snapshot=$(echo "$snapshot_json" | jq -r '.result.structured.snapshot // ""' 2>/dev/null)
        iterations=$(echo "$snapshot_json" | jq -r '.result.structured.iterations // 0' 2>/dev/null)

        if [[ -n "$snapshot" ]]; then
            echo ""
            echo "=== ACTIVE LONG TASK (iteration $iterations) ==="
            echo "$snapshot"
            echo "Use /long-task to manage."
            echo "============================================="
        fi
    fi
fi

exit 0
