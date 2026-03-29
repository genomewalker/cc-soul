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

# Get socket directory — matches C++ get_socket_dir() priority:
# $XDG_RUNTIME_DIR/chitta > ~/.cache/chitta > /tmp
get_socket_dir() {
    if [[ -n "${XDG_RUNTIME_DIR:-}" && -w "$XDG_RUNTIME_DIR" ]]; then
        echo "${XDG_RUNTIME_DIR}/chitta"
    elif [[ -n "${HOME:-}" ]]; then
        echo "${HOME}/.cache/chitta"
    else
        echo "/tmp"
    fi
}

# Check if the chitta daemon is reachable via its Unix socket.
# Used as a gate in hooks: daemon_available || exit 0
daemon_available() {
    local socket
    socket=$(get_socket_path 2>/dev/null)
    [[ -n "$socket" && -S "$socket" ]]
}

# Compute socket path from mind path — matches C++ socket_path_for_mind()
get_socket_path() {
    local mind_path="${CHITTA_DB_PATH:-${CHITTA_MIND:-$HOME/.claude/mind}}"
    local hash=$(djb2_hash "$mind_path")
    echo "$(get_socket_dir)/chitta-${hash}.sock"
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

# emit_event — structured soul event with provenance.
# Usage: emit_event <dedup_file> <category> <source> <content> <confidence> <evidence> <realm>
#   category:   solution|gotcha|preference|decision|failure|pattern|correction|curiosity_gap
#   source:     hook_regex|hook_compliance|distillation|mcp_tool
#   content:    raw learning text (SSL-formatted by caller or this function)
#   confidence: 0.5 (provisional/hook) or 0.85 (distillation) or 1.0 (explicit)
#   evidence:   what triggered this (e.g. "regex match on [SOLUTION]")
emit_event() {
    local dedup_file="$1"
    local category="$2"
    local source="$3"
    local content="$4"
    local confidence="${5:-0.7}"
    local evidence="${6:-}"
    local realm="${7:-brahman}"

    # Quality gate: minimum length
    if [[ ${#content} -lt 30 ]]; then return; fi

    # Quality gate: dedup
    local content_hash
    content_hash=$(echo -n "$content" | md5sum | cut -d' ' -f1)
    if [[ -n "$dedup_file" ]] && grep -q "^${content_hash}$" "$dedup_file" 2>/dev/null; then
        return
    fi
    [[ -n "$dedup_file" ]] && echo "$content_hash" >> "$dedup_file"

    local title
    title=$(echo "$content" | head -c 100)

    queue_write "observe" "{\"category\":\"$category\",\"title\":$(echo "$title" | jq -Rs .),\"content\":$(echo "$content" | jq -Rs .),\"confidence\":$confidence,\"source\":$(echo "$source" | jq -Rs .),\"evidence\":$(echo "$evidence" | jq -Rs .),\"realm\":$(echo "$realm" | jq -Rs .)}"
}

# Guard: fail if CHITTA_SANDBOX=1 and running in main worktree
worktree_guard() {
    if [[ "${CHITTA_SANDBOX:-0}" != "1" ]]; then
        return 0
    fi
    local main_wt
    main_wt="$(git rev-parse --show-toplevel 2>/dev/null)" || return 0
    local cwd="$PWD"
    # Allow if inside a named worktree directory
    if [[ "$cwd" != "$main_wt"* ]] || [[ "$cwd" == */worktrees/* ]]; then
        return 0
    fi
    echo "ERROR: CHITTA_SANDBOX=1 but operating in main worktree. Use EnterWorktree first." >&2
    return 1
}
