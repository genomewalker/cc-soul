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

# Fast O(1) check: is the daemon socket present?
# Returns 0 (true) if socket file exists, 1 (false) otherwise.
# Use this before any blocking chitta CLI calls to skip them instantly when daemon is down.
daemon_available() {
    [[ -S "$(get_socket_path)" ]]
}

# Get current session ID from environment or registry
get_session_id() {
    # First check environment
    if [[ -n "${CLAUDE_SESSION_ID:-}" ]]; then
        echo "$CLAUDE_SESSION_ID"
        return
    fi

    # Use CLI sql_query to lookup session by PID (no netcat)
    local claude_pid=${PPID:-$$}
    if [[ -n "$claude_pid" && "$claude_pid" != "0" ]]; then
        local result
        result=$(chitta sql_query --query "SELECT session_id FROM session_registry WHERE pid = $claude_pid AND status = 'active' LIMIT 1" --text-only 2>/dev/null)
        # Extract UUID from result (handles table format output)
        local session_id
        session_id=$(echo "$result" | grep -oE '[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}' | head -1)
        if [[ -n "$session_id" ]]; then
            echo "$session_id"
            return
        fi
    fi

    # Fallback to empty (caller should handle default)
    echo ""
}

# Get next turn index atomically (flock-protected increment)
get_next_turn() {
    local session_id="${1:-$(get_session_id)}"
    [[ -z "$session_id" ]] && echo 0 && return

    local turn_file="$HOME/.claude/mind/.turn_index_$session_id"
    local turn
    {
        flock -x 200
        turn=$(cat "$turn_file" 2>/dev/null || echo 0)
        echo $((turn + 1)) > "$turn_file"
    } 200>"$turn_file.lock"
    echo "$turn"
}

# Default queue file location (must match daemon's queue_path in simple_cli.cpp)
get_queue_file() {
    echo "${CHITTA_QUEUE:-/tmp/chitta-queue.jsonl}"
}

# Generate UUID for queue acknowledgments
# Uses uuidgen, /proc/sys/kernel/random/uuid, or fallback
generate_ack_id() {
    if command -v uuidgen >/dev/null 2>&1; then
        uuidgen
    elif [[ -f /proc/sys/kernel/random/uuid ]]; then
        cat /proc/sys/kernel/random/uuid
    else
        # Fallback: timestamp + random hex
        printf '%08x-%04x-%04x-%04x-%012x' \
            "$(date +%s)" \
            "$((RANDOM % 65536))" \
            "$((RANDOM % 65536))" \
            "$((RANDOM % 65536))" \
            "$((RANDOM % 281474976710656))"
    fi
}

# Queue write with acknowledgment ID
# Usage: queue_write <tool> <args_json>
# Writes: {"ack_id":"uuid","tool":"...","args":{...},"ts":...}
queue_write() {
    local tool="$1"
    local args="$2"
    local queue_file
    queue_file=$(get_queue_file)
    local ack_id
    ack_id=$(generate_ack_id)

    # Ensure directory exists
    mkdir -p "$(dirname "$queue_file")"

    echo "{\"ack_id\":\"$ack_id\",\"tool\":\"$tool\",\"args\":$args,\"ts\":$(date +%s)}" >> "$queue_file"
}
