#!/bin/bash
# SessionStart hook: Initialize soul context for new session
#
# Input (JSON via stdin):
#   session_id, transcript_path, cwd, source, model
#
# Output (stdout → injected as context):
#   Soul metrics, ledger state, active long-task

set -e

MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind/chitta}"
CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
CHITTAD_BIN="${CHITTAD_BIN:-$HOME/.claude/bin/chittad}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-5}"

# Parse JSON input
INPUT=$(cat)
SESSION_ID=$(echo "$INPUT" | jq -r '.session_id // empty')
TRANSCRIPT_PATH=$(echo "$INPUT" | jq -r '.transcript_path // empty')
CWD=$(echo "$INPUT" | jq -r '.cwd // empty')
SOURCE=$(echo "$INPUT" | jq -r '.source // "startup"')

djb2_hash() {
    local str="$1" hash=5381 i c
    for ((i=0; i<${#str}; i++)); do
        c=$(printf '%d' "'${str:$i:1}")
        hash=$(( ((hash << 5) + hash) + c ))
        hash=$((hash & 0xFFFFFFFF))
    done
    echo "$hash"
}

SOCKET="/tmp/chitta-$(djb2_hash "$MIND_PATH").sock"

json_escape() {
    echo -n "$1" | jq -Rs '.' | sed 's/^"//;s/"$//'
}

rpc_call() {
    local tool="$1" args="$2"
    local request="{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"$tool\",\"arguments\":$args}}"
    [[ ! -S "$SOCKET" ]] && return 1
    echo "$request" | timeout "$MAX_WAIT" nc -U "$SOCKET" 2>/dev/null | head -1
}

extract_text() {
    echo "$1" | jq -r '.result.content[0].text // empty' 2>/dev/null
}

ensure_daemon() {
    [[ -S "$SOCKET" ]] && return 0
    [[ ! -x "$CHITTAD_BIN" ]] && return 1

    "$CHITTAD_BIN" daemon &
    disown

    local waited=0
    while [[ ! -S "$SOCKET" && $waited -lt 50 ]]; do
        sleep 0.1
        ((waited++))
    done
    [[ -S "$SOCKET" ]]
}

ensure_daemon || exit 0

# Detect realm from cwd
if [[ -x "$CHITTA_BIN" ]]; then
    REALM=$("$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")
else
    REALM="brahman"
fi

# Register transcript for distillation
if [[ -n "$TRANSCRIPT_PATH" && -f "$TRANSCRIPT_PATH" ]]; then
    rpc_call "transcript_register" "{\"session_id\":\"$(json_escape "$SESSION_ID")\",\"transcript_path\":\"$(json_escape "$TRANSCRIPT_PATH")\",\"realm\":\"$(json_escape "$REALM")\"}" >/dev/null 2>&1 || true
fi

# Soul context - compact metrics
response=$(rpc_call "soul_context" '{}')
structured=$(echo "$response" | jq -r '.result.structured // empty' 2>/dev/null)
if [[ -n "$structured" ]]; then
    nodes=$(echo "$structured" | jq -r '.total_nodes // 0')
    triplets=$(echo "$structured" | jq -r '.triplet_count // 0')
    conf=$(echo "$structured" | jq -r '.avg_confidence // 0' | cut -c1-4)
    status=$(echo "$structured" | jq -r '.status // "unknown"')
    echo "[soul] n=$nodes t=$triplets c=$conf $status"
fi

# Ledger checkpoint - restore session state
escaped_realm=$(json_escape "$REALM")
response=$(rpc_call "ledger_load" "{\"project\":\"$escaped_realm\"}")
ledger_json=$(echo "$response" | jq -r '.result.structured // empty' 2>/dev/null)
ledger_found=$(echo "$ledger_json" | jq -r '.found // false' 2>/dev/null)

if [[ "$ledger_found" == "true" ]]; then
    # Extract all ledger fields
    session_id=$(echo "$ledger_json" | jq -r '.session_id // ""' 2>/dev/null)
    mood=$(echo "$ledger_json" | jq -r '.mood // ""' 2>/dev/null)
    snapshot=$(echo "$ledger_json" | jq -r '.snapshot // ""' 2>/dev/null)
    active_files=$(echo "$ledger_json" | jq -r '.active_files // []' 2>/dev/null)
    decisions=$(echo "$ledger_json" | jq -r '.decisions // []' 2>/dev/null)
    next_steps=$(echo "$ledger_json" | jq -r '.next_steps // []' 2>/dev/null)

    echo "[ledger] Restored: $session_id"

    # Show mood if meaningful
    [[ -n "$mood" && "$mood" != "null" && "$mood" != "working" ]] && echo "  mood: $mood"

    # Show snapshot (what we were doing)
    if [[ -n "$snapshot" && "$snapshot" != "null" ]]; then
        echo "  last: ${snapshot:0:100}"
    fi

    # Show active files
    file_count=$(echo "$active_files" | jq 'length' 2>/dev/null || echo "0")
    if [[ "$file_count" -gt 0 ]]; then
        files_list=$(echo "$active_files" | jq -r '.[:3] | .[]' 2>/dev/null | xargs -I{} basename {} | tr '\n' ', ' | sed 's/,$//')
        [[ -n "$files_list" ]] && echo "  files: $files_list"
    fi

    # Show next steps
    next_count=$(echo "$next_steps" | jq 'length' 2>/dev/null || echo "0")
    if [[ "$next_count" -gt 0 ]]; then
        first_next=$(echo "$next_steps" | jq -r '.[0] // ""' 2>/dev/null)
        [[ -n "$first_next" && "$first_next" != "null" ]] && echo "  next: ${first_next:0:80}"
    fi

    # Show recent decisions
    dec_count=$(echo "$decisions" | jq 'length' 2>/dev/null || echo "0")
    if [[ "$dec_count" -gt 0 ]]; then
        first_dec=$(echo "$decisions" | jq -r '.[0] // ""' 2>/dev/null)
        [[ -n "$first_dec" && "$first_dec" != "null" ]] && echo "  decision: ${first_dec:0:60}"
    fi
fi

# Active long-task injection
response=$(rpc_call "long_task_active" "{\"realm\":\"$escaped_realm\"}")
task_found=$(echo "$response" | jq -r '.result.structured.found // false' 2>/dev/null)

if [[ "$task_found" == "true" ]]; then
    task_id=$(echo "$response" | jq -r '.result.structured.task_id // ""' 2>/dev/null)
    if [[ -n "$task_id" ]]; then
        snapshot_response=$(rpc_call "long_task_snapshot" "{\"task_id\":\"$(json_escape "$task_id")\",\"mode\":\"inject\"}")
        snapshot=$(echo "$snapshot_response" | jq -r '.result.structured.snapshot // ""' 2>/dev/null)
        iterations=$(echo "$snapshot_response" | jq -r '.result.structured.iterations // 0' 2>/dev/null)

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
