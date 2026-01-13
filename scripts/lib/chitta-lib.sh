#!/bin/bash
# Chitta shared library - common functions for hooks
# Sourced by other scripts; no main logic

# Resolve plugin directory from library location
CHITTA_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHITTA_PLUGIN_DIR="$(dirname "$CHITTA_LIB_DIR")"
CHITTA_PLUGIN_DIR="$(dirname "$CHITTA_PLUGIN_DIR")"

# Prefer stable global symlink, fall back to plugin bin
CHITTA_BIN="${HOME}/.claude/bin/chitta"
[[ ! -x "$CHITTA_BIN" ]] && CHITTA_BIN="$CHITTA_PLUGIN_DIR/bin/chitta"

# Mind path for socket derivation
CHITTA_MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind/chitta}"

# djb2 hash - must match C++ implementation in socket_server.hpp
_djb2_hash() {
    local str="$1"
    local hash=5381
    local i c
    for ((i=0; i<${#str}; i++)); do
        c=$(printf '%d' "'${str:$i:1}")
        hash=$(( ((hash << 5) + hash) + c ))
        hash=$((hash & 0xFFFFFFFF))
    done
    echo "$hash"
}

# Find daemon socket (mind-scoped path)
find_socket() {
    local hash=$(_djb2_hash "$CHITTA_MIND_PATH")
    local socket="/tmp/chitta-${hash}.sock"
    if [[ -S "$socket" ]]; then
        echo "$socket"
        return 0
    fi
    return 1
}

# Query daemon socket directly (fast path)
socket_query() {
    local query="$1"
    local socket
    socket=$(find_socket) || return 1
    echo "$query" | timeout 5 nc -U "$socket" 2>/dev/null | head -1
}

# Call MCP tool via socket or thin client
call_mcp() {
    local method="$1"
    local params="$2"
    local request="{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"$method\",\"arguments\":$params},\"id\":1}"

    # Try socket first (fast)
    local response
    response=$(socket_query "$request" 2>/dev/null)
    if [[ -n "$response" ]]; then
        echo "$response" | jq -r '.result.content[0].text' 2>/dev/null || true
        return 0
    fi

    # Fall back to thin client
    if [[ -x "$CHITTA_BIN" ]]; then
        echo "$request" | "$CHITTA_BIN" 2>/dev/null | grep -v '^\[chitta' | jq -r '.result.content[0].text' 2>/dev/null || true
    fi
}

# Escape string for JSON
json_escape() {
    jq -n --arg s "$1" '$s'
}
