#!/bin/bash
# Subconscious daemon management
#
# Usage: subconscious.sh <start|stop|status>

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(dirname "$SCRIPT_DIR")"

# Binaries are installed to ~/.claude/bin/ by setup.sh
CHITTA_CLI="${HOME}/.claude/bin/chittad"
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind/chitta}"
MODEL_PATH="${HOME}/.claude/bin/model.onnx"
VOCAB_PATH="${HOME}/.claude/bin/vocab.txt"
PID_FILE="${HOME}/.claude/mind/.subconscious.pid"
LOG_FILE="${HOME}/.claude/mind/.subconscious.log"
INTERVAL="${SUBCONSCIOUS_INTERVAL:-60}"
TIMEOUT_CMD=()
TIMEOUT_WARNED=false
MAX_WAIT="${CC_SOUL_MAX_WAIT:-5}"

if [[ "$MAX_WAIT" != "0" ]] && command -v timeout >/dev/null 2>&1; then
    TIMEOUT_CMD=(timeout "$MAX_WAIT")
fi

run_with_timeout() {
    if [[ "$MAX_WAIT" != "0" && ${#TIMEOUT_CMD[@]} -eq 0 && "$TIMEOUT_WARNED" != "true" ]]; then
        echo "[cc-soul] timeout not available; running without limit" >&2
        TIMEOUT_WARNED=true
    fi

    if [[ ${#TIMEOUT_CMD[@]} -gt 0 ]]; then
        "${TIMEOUT_CMD[@]}" "$@"
    else
        "$@"
    fi
}

# djb2 hash - must match C++ implementation in socket_server.hpp
djb2_hash() {
    local str="$1"
    local hash=5381
    local i c
    for ((i=0; i<${#str}; i++)); do
        c=$(printf '%d' "'${str:$i:1}")
        hash=$(( ((hash << 5) + hash) + c ))
        hash=$((hash & 0xFFFFFFFF))  # Keep 32-bit
    done
    echo "$hash"
}

MIND_HASH=$(djb2_hash "$MIND_PATH")
LOCK_FILE="/tmp/chitta-${MIND_HASH}.lock"
SOCKET_PATH="/tmp/chitta-${MIND_HASH}.sock"

is_running() {
    # First check PID file
    if [[ -f "$PID_FILE" ]]; then
        local pid
        pid=$(cat "$PID_FILE")
        if kill -0 "$pid" 2>/dev/null; then
            return 0
        fi
        # Stale PID file
        rm -f "$PID_FILE"
    fi

    # Also check for any running daemon process (covers MCP-spawned daemons)
    if pgrep -f "chittad daemon" >/dev/null 2>&1; then
        return 0
    fi

    return 1
}

cmd_start() {
    if is_running; then
        local pid
        if [[ -f "$PID_FILE" ]]; then
            pid=$(cat "$PID_FILE")
        else
            pid=$(pgrep -f "chittad daemon" | head -1)
        fi
        echo "[subconscious] Already running (pid=$pid)"
        return 0
    fi

    if [[ ! -x "$CHITTA_CLI" ]]; then
        echo "[subconscious] chittad not found" >&2
        return 1
    fi

    # Acquire lock to prevent race with MCP clients
    exec 200>"$LOCK_FILE"
    if ! flock -n 200; then
        echo "[subconscious] Another process is starting daemon, waiting..."
        flock 200 || {
            echo "[subconscious] Failed to acquire lock" >&2
            return 1
        }
    fi

    # Re-check after acquiring lock
    if is_running; then
        local pid
        if [[ -f "$PID_FILE" ]]; then
            pid=$(cat "$PID_FILE")
        else
            pid=$(pgrep -f "chittad daemon" | head -1)
        fi
        echo "[subconscious] Already running (started by another process, pid=$pid)"
        exec 200>&-  # Release lock
        return 0
    fi

    # Detect supported daemon flags (avoid incompatible binaries)
    local daemon_help
    daemon_help=$(run_with_timeout "$CHITTA_CLI" daemon --help 2>&1 || true)
    if [[ -z "$daemon_help" ]]; then
        echo "[subconscious] Unable to read daemon help; proceeding cautiously" >&2
    fi

    local support_socket=false
    local support_interval=false
    local support_pid=false

    if echo "$daemon_help" | grep -q -- "--socket"; then
        support_socket=true
    fi
    if echo "$daemon_help" | grep -q -- "--interval"; then
        support_interval=true
    fi
    if echo "$daemon_help" | grep -q -- "--pid-file"; then
        support_pid=true
    fi

    if [[ -n "$daemon_help" && "$support_socket" != "true" ]]; then
        echo "[subconscious] Daemon does not support --socket; aborting startup" >&2
        exec 200>&-
        return 1
    fi

    local daemon_args=(daemon "--path" "$MIND_PATH" "--model" "$MODEL_PATH" "--vocab" "$VOCAB_PATH")
    if [[ "$support_socket" == "true" ]]; then
        daemon_args+=("--socket")
    fi
    if [[ "$support_interval" == "true" ]]; then
        daemon_args+=("--interval" "$INTERVAL")
    else
        echo "[subconscious] --interval not supported; using daemon default" >&2
    fi
    if [[ "$support_pid" == "true" ]]; then
        daemon_args+=("--pid-file" "$PID_FILE")
    else
        echo "[subconscious] --pid-file not supported; PID file not written" >&2
    fi

    # Start daemon in background with socket server for MCP clients
    nohup "$CHITTA_CLI" "${daemon_args[@]}" >> "$LOG_FILE" 2>&1 &

    # Wait for socket AND verify daemon responds
    # This ensures the daemon is fully ready for MCP clients
    local daemon_ready=false
    local wait_start
    wait_start=$(date +%s)
    while true; do
        if [[ -S "$SOCKET_PATH" ]]; then
            # Socket exists, now verify daemon responds with heartbeat
            local response
            response=$(echo "stats" | run_with_timeout nc -U "$SOCKET_PATH" 2>/dev/null || true)
            if [[ -n "$response" && "$response" == *"total"* ]]; then
                daemon_ready=true
                break
            fi
        fi

        if ! is_running; then
            echo "[subconscious] Failed to start (daemon exited). See $LOG_FILE" >&2
            break
        fi

        if [[ "$MAX_WAIT" != "0" ]]; then
            local now
            now=$(date +%s)
            if (( now - wait_start >= MAX_WAIT )); then
                echo "[subconscious] Startup timed out after ${MAX_WAIT}s (daemon may still be initializing)" >&2
                break
            fi
        fi

        sleep 0.1
    done

    # Release lock
    exec 200>&-

    if $daemon_ready && is_running; then
        local pid=$(cat "$PID_FILE" 2>/dev/null || pgrep -f "chittad daemon" | head -1)
        echo "[subconscious] Started (pid=$pid, socket=$SOCKET_PATH, heartbeat=ok)"
    else
        echo "[subconscious] Failed to start (daemon not responding)" >&2
        return 1
    fi
}

cmd_stop() {
    if ! is_running; then
        echo "[subconscious] Not running"
        return 0
    fi

    # Get PIDs from both PID file and process search
    local pids=""
    if [[ -f "$PID_FILE" ]]; then
        pids=$(cat "$PID_FILE")
    fi
    # Also find any MCP-spawned daemons
    local other_pids
    other_pids=$(pgrep -f "chittad daemon" 2>/dev/null || true)
    if [[ -n "$other_pids" ]]; then
        pids="$pids $other_pids"
    fi
    pids=$(echo "$pids" | tr ' ' '\n' | sort -u | tr '\n' ' ')

    echo "[subconscious] Stopping daemon(s): $pids"
    for pid in $pids; do
        kill "$pid" 2>/dev/null || true
    done

    # Wait for graceful shutdown
    for i in {1..10}; do
        if ! is_running; then
            echo "[subconscious] Stopped"
            rm -f "$PID_FILE"
            return 0
        fi
        sleep 0.5
    done

    # Force kill any remaining
    for pid in $pids; do
        kill -9 "$pid" 2>/dev/null || true
    done
    rm -f "$PID_FILE"
    echo "[subconscious] Force stopped"
}

cmd_status() {
    if is_running; then
        local pid
        if [[ -f "$PID_FILE" ]]; then
            pid=$(cat "$PID_FILE")
            echo "[subconscious] Running (pid=$pid, managed)"
        else
            pid=$(pgrep -f "chittad daemon" | head -1)
            echo "[subconscious] Running (pid=$pid, MCP-spawned)"
        fi
        # Show socket info
        if [[ -S "$SOCKET_PATH" ]]; then
            echo "[subconscious] Socket: $SOCKET_PATH"
        else
            echo "[subconscious] Socket: not found"
        fi
        echo "[subconscious] PID file: $PID_FILE"
        return 0
    else
        echo "[subconscious] Not running"
        echo "[subconscious] Socket: $SOCKET_PATH"
        echo "[subconscious] PID file: $PID_FILE"
        return 1
    fi
}

case "${1:-status}" in
    start)  cmd_start ;;
    stop)   cmd_stop ;;
    status) cmd_status ;;
    *)
        echo "Usage: subconscious.sh <start|stop|status>"
        exit 1
        ;;
esac
