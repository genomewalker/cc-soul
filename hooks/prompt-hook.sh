#!/bin/bash
# UserPromptSubmit hook: Surface relevant memories for user query
#
# Input (JSON via stdin):
#   prompt - the user's query
#
# Output (stdout → injected as context):
#   SSL-formatted memories: [conf%:type] content

set -e

MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind/chitta}"
CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
CHITTAD_BIN="${CHITTAD_BIN:-$HOME/.claude/bin/chittad}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-5}"

# Parse JSON input
INPUT=$(cat)
QUERY=$(echo "$INPUT" | jq -r '.prompt // empty')

[[ -z "$QUERY" ]] && exit 0

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

# Map category to short type
map_type() {
    case "$1" in
        solution|SOLUTION|WORKING_SOLUTION) echo "sol" ;;
        gotcha|GOTCHA|trap) echo "gotcha" ;;
        preference|PREFERENCE|pref) echo "pref" ;;
        decision|DECISION|dec) echo "dec" ;;
        failure|FAILURE|fail) echo "fail" ;;
        pattern|PATTERN|pat) echo "pat" ;;
        insight|wisdom) echo "insight" ;;
        correction) echo "fix" ;;
        *) echo "mem" ;;
    esac
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

# Detect realm for scoped recall
if [[ -x "$CHITTA_BIN" ]]; then
    REALM=$("$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")
else
    REALM="brahman"
fi

# Surface relevant memories
response=$(rpc_call "full_resonate" "{\"query\":\"$(json_escape "$QUERY")\",\"k\":4,\"realm\":\"$(json_escape "$REALM")\",\"include_global\":true}")
memories=$(extract_text "$response")

if [[ -n "$memories" && "$memories" != *"No memories"* ]]; then
    echo "[soul]"

    # Parse and format each memory line
    # Input format: [85%] [category] content (uuid)
    echo "$memories" | grep -E '^\[[0-9]+%\]' | head -4 | while read -r line; do
        # Extract confidence
        conf=$(echo "$line" | grep -oE '^\[[0-9]+%\]' | tr -d '[]%')
        # Extract category
        cat=$(echo "$line" | sed 's/^\[[0-9]*%\] //' | grep -oE '^\[[^]]+\]' | tr -d '[]')
        # Extract content (remove conf, cat, and trailing uuid)
        content=$(echo "$line" | sed 's/^\[[0-9]*%\] \[[^]]*\] //' | sed 's/ ([a-f0-9-]*)$//' | cut -c1-120)
        # Extract uuid if present
        uuid=$(echo "$line" | grep -oE '\([a-f0-9-]+\)$' | tr -d '()')

        type=$(map_type "$cat")

        if [[ -n "$uuid" ]]; then
            echo "[${conf}%:${type}:${uuid}] $content"
        else
            echo "[${conf}%:${type}] $content"
        fi
    done
fi

exit 0
