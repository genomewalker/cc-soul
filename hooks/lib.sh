#!/bin/bash
# Shared library for cc-soul hooks
#
# Common functions used across session-start, prompt, and stop hooks.

# DJB2 hash function (matches C++ implementation in socket_server.hpp)
djb2_hash() {
    local str="$1"
    local h=5381
    local i c
    for ((i=0; i<${#str}; i++)); do
        c=$(printf '%d' "'${str:$i:1}")
        h=$(( ((h << 5) + h + c) & 0xFFFFFFFF ))
    done
    echo "$h"
}

# Compute socket path from mind path
get_socket_path() {
    local mind_path="${CHITTA_DB_PATH:-${CHITTA_MIND:-$HOME/.claude/mind}}"
    local hash=$(djb2_hash "$mind_path")
    echo "/tmp/chitta-${hash}.sock"
}

# Get current session ID from environment
get_session_id() {
    echo "${CLAUDE_SESSION_ID:-}"
}
