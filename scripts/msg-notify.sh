#!/usr/bin/env bash
# msg-notify.sh — Standalone cross-session message notification daemon.
#
# Polls chitta msg_inbox for the given session and appends new messages
# to a notification file read by the prompt hook on each turn.
#
# Zellij-independent: runs as a background process, no terminal multiplexer needed.
#
# Usage:
#   msg-notify.sh <session_id> [poll_interval_secs]
#
# Started by session-start hook, killed by stop hook.
# Messages are written to: ~/.claude/mind/.msg_notify/<session_id>
# Prompt hook reads and clears this file on each user prompt.

set -euo pipefail

SESSION_ID="${1:-}"
INTERVAL="${2:-5}"
CHITTA="${CHITTA_BIN:-${HOME}/.claude/bin/chitta}"
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"
NOTIFY_DIR="${MIND_PATH}/.msg_notify"
PID_DIR="${MIND_PATH}/.msg_notify_pids"

if [[ -z "$SESSION_ID" ]]; then
    echo "Usage: $0 <session_id> [poll_interval_secs]" >&2
    exit 1
fi

mkdir -p "$NOTIFY_DIR" "$PID_DIR"
NOTIFY_FILE="${NOTIFY_DIR}/${SESSION_ID}"
PID_FILE="${PID_DIR}/${SESSION_ID}.pid"

# Write PID so stop hook can find us
echo $$ > "$PID_FILE"

# Cleanup on exit
cleanup() {
    rm -f "$PID_FILE"
}
trap cleanup EXIT

# Track last seen event_id to avoid duplicate delivery
last_seen_id=0
if [[ -f "${NOTIFY_DIR}/${SESSION_ID}.last_id" ]]; then
    last_seen_id=$(cat "${NOTIFY_DIR}/${SESSION_ID}.last_id" 2>/dev/null || echo 0)
fi

while true; do
    response=$("$CHITTA" msg_inbox \
        --session_id "$SESSION_ID" \
        --limit 10 \
        --min_priority 1 \
        --json 2>/dev/null) || true

    if [[ -n "$response" ]]; then
        count=$(echo "$response" | jq -r '.count // 0' 2>/dev/null || echo 0)
        if [[ "$count" -gt 0 ]]; then
            # Extract messages newer than last_seen_id
            new_msgs=$(echo "$response" | jq -r --argjson last "$last_seen_id" '
                .messages[]
                | select((.memory_id // 0) > $last)
                | "[MSG:\(.sender_session_id // "unknown")] \(.content | .[0:200])"
            ' 2>/dev/null || true)

            if [[ -n "$new_msgs" ]]; then
                echo "$new_msgs" >> "$NOTIFY_FILE"
                # Update last seen id
                new_last=$(echo "$response" | jq -r '[.messages[].memory_id // 0] | max' 2>/dev/null || echo "$last_seen_id")
                echo "$new_last" > "${NOTIFY_DIR}/${SESSION_ID}.last_id"
                last_seen_id="$new_last"
            fi
        fi
    fi

    sleep "$INTERVAL"
done
