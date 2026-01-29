#!/bin/bash
# PreCompact hook: Save full checkpoint before context compaction
#
# CRITICAL: This is the last chance to save state before context is lost
#
# Input (JSON via stdin):
#   trigger - "manual" or "auto"
#   transcript_path - path to current transcript
#
# Output (stdout → shown in verbose mode):
#   Checkpoint confirmation

set -e

MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind/chitta}"
CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
CHITTAD_BIN="${CHITTAD_BIN:-$HOME/.claude/bin/chittad}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-5}"

# Parse JSON input
INPUT=$(cat)
TRIGGER=$(echo "$INPUT" | jq -r '.trigger // "auto"')
TRANSCRIPT_PATH=$(echo "$INPUT" | jq -r '.transcript_path // empty')

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

ensure_daemon || {
    echo "[pre-compact] WARNING: daemon not running, checkpoint may fail" >&2
}

# Detect realm
if [[ -x "$CHITTA_BIN" ]]; then
    REALM=$("$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")
else
    REALM="brahman"
fi

SESSION_ID="compact-$(date +%Y%m%d-%H%M%S)"

# Try to extract context from transcript for richer checkpoint
files_json='[]'
decisions_json='[]'
next_steps_json='["Review previous work after compaction","Continue from checkpoint"]'
snapshot="Context compacted ($TRIGGER)"

if [[ -n "$TRANSCRIPT_PATH" && -f "$TRANSCRIPT_PATH" ]]; then
    # Extract files mentioned in recent messages
    files_json=$(tail -50 "$TRANSCRIPT_PATH" 2>/dev/null | \
        grep -oE '[a-zA-Z0-9_/-]+\.(py|js|ts|tsx|cpp|hpp|c|h|rs|go|sh|md|json|yaml|yml)' | \
        sort -u | head -10 | jq -R . | jq -s '.' 2>/dev/null || echo '[]')

    # Try to get last assistant summary
    last_response=$(tac "$TRANSCRIPT_PATH" 2>/dev/null | grep -m1 '"role":"assistant"' | \
        jq -r '.message.content[] | select(.type=="text") | .text' 2>/dev/null | head -c 500)

    if [[ -n "$last_response" ]]; then
        snapshot=$(echo "$last_response" | grep -v '^$' | head -1 | head -c 200)
        [[ -z "$snapshot" ]] && snapshot="Context compacted ($TRIGGER)"

        # Extract any next steps mentioned
        next_from_response=$(echo "$last_response" | grep -iE '(next:|todo:|should .* next)' | head -3 | jq -R . | jq -s '.' 2>/dev/null)
        [[ -n "$next_from_response" && "$next_from_response" != "[]" ]] && next_steps_json="$next_from_response"
    fi
fi

[[ -z "$files_json" || "$files_json" == "null" ]] && files_json='[]'
[[ -z "$next_steps_json" || "$next_steps_json" == "null" ]] && next_steps_json='["Review previous work"]'

# Save with retry
SAVED=false

# Try CLI first
if [[ -x "$CHITTA_BIN" ]]; then
    if "$CHITTA_BIN" ledger_save \
        --session_id "$SESSION_ID" \
        --project "$REALM" \
        --mood "pre-compact" \
        --active_files "$files_json" \
        --decisions "$decisions_json" \
        --next_steps "$next_steps_json" \
        --snapshot "$snapshot" 2>/dev/null; then
        SAVED=true
    fi
fi

# Fallback to RPC
if [[ "$SAVED" != "true" && -S "$SOCKET" ]]; then
    request="{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"ledger_save\",\"arguments\":{\"session_id\":\"$SESSION_ID\",\"project\":\"$(json_escape "$REALM")\",\"mood\":\"pre-compact\",\"snapshot\":\"$(json_escape "$snapshot")\"}}}"
    if echo "$request" | timeout "$MAX_WAIT" nc -U "$SOCKET" 2>/dev/null | grep -q '"result"'; then
        SAVED=true
    fi
fi

if [[ "$SAVED" == "true" ]]; then
    echo "[checkpoint] $REALM: $SESSION_ID ($TRIGGER)" >&2
else
    echo "[checkpoint] FAILED - state may be lost!" >&2
fi

exit 0
