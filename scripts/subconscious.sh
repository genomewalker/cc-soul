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

is_running() {
    if [[ -f "$PID_FILE" ]]; then
        local pid
        pid=$(cat "$PID_FILE")
        if kill -0 "$pid" 2>/dev/null; then
            return 0
        fi
        # Stale PID file
        rm -f "$PID_FILE"
    fi
    return 1
}

cmd_start() {
    if is_running; then
        local pid=$(cat "$PID_FILE")
        echo "[subconscious] Already running (pid=$pid)"
        return 0
    fi

    if [[ ! -x "$CHITTA_CLI" ]]; then
        echo "[subconscious] chitta_cli not found" >&2
        return 1
    fi

    # Start daemon in background
    nohup "$CHITTA_CLI" daemon \
        --path "$MIND_PATH" \
        --model "$MODEL_PATH" \
        --vocab "$VOCAB_PATH" \
        --interval "$INTERVAL" \
        --pid-file "$PID_FILE" \
        >> "$LOG_FILE" 2>&1 &

    # Wait briefly for PID file
    sleep 1

    if is_running; then
        local pid=$(cat "$PID_FILE")
        echo "[subconscious] Started (pid=$pid, interval=${INTERVAL}s)"
    else
        echo "[subconscious] Failed to start" >&2
        return 1
    fi
}

cmd_stop() {
    if ! is_running; then
        echo "[subconscious] Not running"
        return 0
    fi

    local pid=$(cat "$PID_FILE")
    echo "[subconscious] Stopping (pid=$pid)..."
    kill "$pid" 2>/dev/null || true

    # Wait for graceful shutdown
    for i in {1..10}; do
        if ! is_running; then
            echo "[subconscious] Stopped"
            return 0
        fi
        sleep 0.5
    done

    # Force kill
    kill -9 "$pid" 2>/dev/null || true
    rm -f "$PID_FILE"
    echo "[subconscious] Force stopped"
}

cmd_status() {
    if is_running; then
        local pid=$(cat "$PID_FILE")
        echo "[subconscious] Running (pid=$pid)"
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
