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

# Get current session ID from environment or registry
get_session_id() {
    # First check environment
    if [[ -n "${CLAUDE_SESSION_ID:-}" ]]; then
        echo "$CLAUDE_SESSION_ID"
        return
    fi

    # Try to find session by PID in registry using SQL query
    local socket_path=$(get_socket_path)
    local claude_pid=${PPID:-$$}

    if [[ -S "$socket_path" && -n "$claude_pid" && "$claude_pid" != "0" ]]; then
        local request='{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"sql_query","arguments":{"query":"SELECT session_id FROM session_registry WHERE pid = '"$claude_pid"' AND status = '\''active'\'' LIMIT 1"}}}'
        local response=$(echo "$request" | timeout 1 nc -U "$socket_path" 2>/dev/null)

        # Extract session_id from response (format: | session_id | \n| uuid |)
        local session_id=$(echo "$response" | grep -oE '[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}' | head -1)

        if [[ -n "$session_id" ]]; then
            echo "$session_id"
            return
        fi
    fi

    # Fallback to empty (caller should handle default)
    echo ""
}
