#!/bin/bash
# Subconscious daemon management
#
# Usage: subconscious.sh <start|stop|status>

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(dirname "$SCRIPT_DIR")"

CHITTA_CLI="$PLUGIN_DIR/bin/chitta_cli"
MIND_PATH="${HOME}/.claude/mind/chitta"
MODEL_PATH="$PLUGIN_DIR/chitta/models/model.onnx"
VOCAB_PATH="$PLUGIN_DIR/chitta/models/vocab.txt"
PID_FILE="${HOME}/.claude/mind/.subconscious.pid"
LOG_FILE="${HOME}/.claude/mind/.subconscious.log"
INTERVAL="${SUBCONSCIOUS_INTERVAL:-60}"

LOCK_FILE="/tmp/chitta-daemon.lock"
SOCKET_PATH="/tmp/chitta.sock"

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
    if pgrep -f "chitta_cli daemon" >/dev/null 2>&1; then
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
            pid=$(pgrep -f "chitta_cli daemon" | head -1)
        fi
        echo "[subconscious] Already running (pid=$pid)"
        return 0
    fi

    if [[ ! -x "$CHITTA_CLI" ]]; then
        echo "[subconscious] chitta_cli not found" >&2
        return 1
    fi

    # Acquire lock to prevent race with MCP clients
    exec 200>"$LOCK_FILE"
    if ! flock -n 200; then
        echo "[subconscious] Another process is starting daemon, waiting..."
        flock -w 5 200 || {
            echo "[subconscious] Lock timeout, checking if daemon started" >&2
            if is_running; then
                echo "[subconscious] Daemon was started by another process"
                return 0
            fi
            return 1
        }
    fi

    # Re-check after acquiring lock
    if is_running; then
        local pid
        if [[ -f "$PID_FILE" ]]; then
            pid=$(cat "$PID_FILE")
        else
            pid=$(pgrep -f "chitta_cli daemon" | head -1)
        fi
        echo "[subconscious] Already running (started by another process, pid=$pid)"
        exec 200>&-  # Release lock
        return 0
    fi

    # Start daemon in background with socket server for MCP clients
    nohup "$CHITTA_CLI" daemon \
        --socket \
        --path "$MIND_PATH" \
        --model "$MODEL_PATH" \
        --vocab "$VOCAB_PATH" \
        --interval "$INTERVAL" \
        --pid-file "$PID_FILE" \
        >> "$LOG_FILE" 2>&1 &

    # Wait for socket AND verify daemon responds (up to 10 seconds)
    # This ensures the daemon is fully ready for MCP clients
    local daemon_ready=false
    for i in {1..100}; do
        if [[ -S "$SOCKET_PATH" ]]; then
            # Socket exists, now verify daemon responds with heartbeat
            local response
            response=$(echo "stats" | timeout 1 nc -U "$SOCKET_PATH" 2>/dev/null || true)
            if [[ -n "$response" && "$response" == *"total"* ]]; then
                daemon_ready=true
                break
            fi
        fi
        sleep 0.1
    done

    # Release lock
    exec 200>&-

    if $daemon_ready && is_running; then
        local pid=$(cat "$PID_FILE" 2>/dev/null || pgrep -f "chitta_cli daemon" | head -1)
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
    other_pids=$(pgrep -f "chitta_cli daemon" 2>/dev/null || true)
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
            pid=$(pgrep -f "chitta_cli daemon" | head -1)
            echo "[subconscious] Running (pid=$pid, MCP-spawned)"
        fi
        # Show socket info
        if [[ -S "$SOCKET_PATH" ]]; then
            echo "[subconscious] Socket: $SOCKET_PATH"
        else
            echo "[subconscious] Socket: not found"
        fi
        return 0
    else
        echo "[subconscious] Not running"
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
