#!/bin/bash
# Subconscious daemon management
#
# Usage: subconscious.sh <start|stop|status>

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(dirname "$SCRIPT_DIR")"

# Binaries are installed to ~/.claude/bin/ by smart-install.sh
CHITTA_CLI="${HOME}/.claude/bin/chittad"
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"
MODEL_PATH="${HOME}/.claude/bin/model.onnx"
VOCAB_PATH="${HOME}/.claude/bin/vocab.txt"
LOG_FILE="${HOME}/.claude/mind/.subconscious.log"
# PID_FILE is defined after MIND_HASH calculation below
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
PID_FILE="/tmp/chitta-${MIND_HASH}.pid"  # Daemon writes PID here

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
    if pgrep -f "chittad daemon.*--path $MIND_PATH" >/dev/null 2>&1; then
        return 0
    fi

    return 1
}

# Check if daemon actually responds to commands (not just running)
is_responsive() {
    if [[ ! -S "$SOCKET_PATH" ]]; then
        return 1
    fi

    # Try to get stats with short timeout
    local response
    response=$(echo "stats" | timeout 3 nc -U "$SOCKET_PATH" 2>/dev/null || true)
    if [[ -n "$response" && "$response" == *"nodes"* ]]; then
        return 0
    fi
    return 1
}

# Kill unresponsive daemon(s) and clean up
kill_unresponsive() {
    echo "[subconscious] Daemon unresponsive, killing..." >&2

    # Get all daemon PIDs
    local pids=""
    if [[ -f "$PID_FILE" ]]; then
        pids=$(cat "$PID_FILE" 2>/dev/null || true)
    fi
    local other_pids
    other_pids=$(pgrep -f "chittad daemon.*--path $MIND_PATH" 2>/dev/null || true)
    if [[ -n "$other_pids" ]]; then
        pids="$pids $other_pids"
    fi
    pids=$(echo "$pids" | tr ' ' '\n' | sort -u | grep -v '^$' | tr '\n' ' ')

    # Force kill all
    for pid in $pids; do
        kill -9 "$pid" 2>/dev/null || true
    done

    # Clean up socket/lock/pid files
    rm -f "$SOCKET_PATH" "$LOCK_FILE" "$PID_FILE" 2>/dev/null || true

    sleep 1
    echo "[subconscious] Cleaned up stale daemon" >&2
}

cmd_start() {
    # First: kill ALL existing daemon processes to ensure clean state
    # This prevents multiple daemons from accumulating
    local existing_pids
    existing_pids=$(pgrep -f "chittad daemon.*--path $MIND_PATH" 2>/dev/null || true)
    if [[ -n "$existing_pids" ]]; then
        # Check if any are responsive
        if [[ -S "$SOCKET_PATH" ]]; then
            local response
            response=$(echo "stats" | timeout 2 nc -U "$SOCKET_PATH" 2>/dev/null || true)
            if [[ -n "$response" && "$response" == *"nodes"* ]]; then
                # Healthy daemon exists, nothing to do
                return 0
            fi
        fi
        # Kill all existing daemons (stale/unresponsive)
        echo "[subconscious] Cleaning up existing daemon(s): $existing_pids" >&2
        for pid in $existing_pids; do
            kill -9 "$pid" 2>/dev/null || true
        done
        rm -f "$SOCKET_PATH" "$PID_FILE" "$LOCK_FILE" 2>/dev/null || true
        sleep 1
    fi

    if [[ ! -x "$CHITTA_CLI" ]]; then
        echo "[cc-soul] Not installed. Run /cc-soul-setup or /cc-soul-update first." >&2
        return 0  # Don't block session, just warn
    fi

    # Atomic lock to prevent race conditions
    # Use mkdir which is atomic on POSIX systems
    local lock_dir="/tmp/chitta-${MIND_HASH}.startlock"
    if ! mkdir "$lock_dir" 2>/dev/null; then
        # Another process is starting - wait for it
        echo "[subconscious] Another process is starting daemon, waiting..." >&2
        for _ in {1..50}; do
            sleep 0.1
            if [[ -S "$SOCKET_PATH" ]]; then
                local response
                response=$(echo "stats" | timeout 2 nc -U "$SOCKET_PATH" 2>/dev/null || true)
                if [[ -n "$response" && "$response" == *"nodes"* ]]; then
                    return 0
                fi
            fi
        done
        # Timed out waiting, clean stale lock if old
        if [[ -d "$lock_dir" ]]; then
            local lock_age lock_mtime
            # Use portable stat syntax (Linux: -c %Y, macOS: -f %m)
            if stat -c %Y "$lock_dir" >/dev/null 2>&1; then
                lock_mtime=$(stat -c %Y "$lock_dir" 2>/dev/null || echo 0)
            else
                lock_mtime=$(stat -f %m "$lock_dir" 2>/dev/null || echo 0)
            fi
            lock_age=$(( $(date +%s) - lock_mtime ))
            if (( lock_age > 30 )); then
                rmdir "$lock_dir" 2>/dev/null || true
            fi
        fi
        return 1
    fi

    # We hold the lock - ensure cleanup on exit
    trap 'rmdir "$lock_dir" 2>/dev/null || true' EXIT

    # Detect supported daemon flags (avoid incompatible binaries)
    local daemon_help
    daemon_help=$(run_with_timeout "$CHITTA_CLI" daemon --help 2>&1 || true)
    if [[ -z "$daemon_help" ]]; then
        echo "[subconscious] Unable to read daemon help; proceeding cautiously" >&2
    fi

    local support_interval=false
    if echo "$daemon_help" | grep -q -- "--interval"; then
        support_interval=true
    fi

    local daemon_args=(daemon "--path" "$MIND_PATH")
    if [[ "$support_interval" == "true" ]]; then
        daemon_args+=("--interval" "$INTERVAL")
    fi

    # Start daemon - it will fork and return immediately
    "$CHITTA_CLI" "${daemon_args[@]}" 2>>"$LOG_FILE"

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
            if [[ -n "$response" && "$response" == *"nodes"* ]]; then
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
    rmdir "$lock_dir" 2>/dev/null || true
    trap - EXIT

    if $daemon_ready && is_running; then
        local pid=$(cat "$PID_FILE" 2>/dev/null || pgrep -f "chittad daemon.*--path $MIND_PATH" | head -1)
        echo "[subconscious] Started (pid=$pid, socket=$SOCKET_PATH, heartbeat=ok)"
    else
        echo "[subconscious] Failed to start (daemon not responding)" >&2
        return 1
    fi
}

cmd_stop() {
    # Clean up any stale lock directories first
    local lock_dir="/tmp/chitta-${MIND_HASH}.startlock"
    rmdir "$lock_dir" 2>/dev/null || true

    if ! is_running; then
        # Also clean up sockets/files even if not running
        rm -f "$SOCKET_PATH" "$PID_FILE" "$LOCK_FILE" 2>/dev/null || true
        echo "[subconscious] Not running (cleaned up stale files)"
        return 0
    fi

    # Get PIDs from both PID file and process search
    local pids=""
    if [[ -f "$PID_FILE" ]]; then
        pids=$(cat "$PID_FILE")
    fi
    # Also find any MCP-spawned daemons
    local other_pids
    other_pids=$(pgrep -f "chittad daemon.*--path $MIND_PATH" 2>/dev/null || true)
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
            rm -f "$PID_FILE" "$SOCKET_PATH" "$LOCK_FILE" 2>/dev/null || true
            return 0
        fi
        sleep 0.5
    done

    # Force kill any remaining
    for pid in $pids; do
        kill -9 "$pid" 2>/dev/null || true
    done
    rm -f "$PID_FILE" "$SOCKET_PATH" "$LOCK_FILE" 2>/dev/null || true
    echo "[subconscious] Force stopped"
}

cmd_status() {
    if is_running; then
        local pid
        if [[ -f "$PID_FILE" ]]; then
            pid=$(cat "$PID_FILE")
            echo "[subconscious] Running (pid=$pid, managed)"
        else
            pid=$(pgrep -f "chittad daemon.*--path $MIND_PATH" | head -1)
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

cmd_health() {
    if ! is_running; then
        echo "[subconscious] UNHEALTHY: Not running"
        return 1
    fi

    if ! is_responsive; then
        echo "[subconscious] UNHEALTHY: Running but not responding"
        echo "[subconscious] Attempting recovery..."
        kill_unresponsive
        cmd_start
        if is_responsive; then
            echo "[subconscious] RECOVERED: Daemon restarted successfully"
            return 0
        else
            echo "[subconscious] FAILED: Could not recover daemon"
            return 1
        fi
    fi

    local pid
    pid=$(cat "$PID_FILE" 2>/dev/null || pgrep -f "chittad daemon.*--path $MIND_PATH" | head -1)
    echo "[subconscious] HEALTHY: pid=$pid, responsive"
    return 0
}

case "${1:-status}" in
    start)   cmd_start ;;
    stop)    cmd_stop ;;
    restart) cmd_stop; cmd_start ;;
    status)  cmd_status ;;
    health)  cmd_health ;;
    *)
        echo "Usage: subconscious.sh <start|stop|restart|status|health>"
        exit 1
        ;;
esac
