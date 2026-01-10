#!/bin/bash
# Ledger auto-save hook for cc-soul
# Saves session ledger (Atman snapshot) via chitta MCP
#
# Usage: ledger-autosave.sh [action] [session_id]
#   action: "save" | "load" (default: save)
#   session_id: optional session identifier
#
# Environment variables:
#   CHITTA_SOCKET: path to chitta MCP socket (default: ~/.claude/chitta.sock)
#   WORK_STATE: JSON object with work state
#   CONTINUATION: JSON object with continuation

set -euo pipefail

ACTION="${1:-save}"
SESSION_ID="${2:-$(date +%Y%m%d-%H%M%S)}"

# Chitta MCP socket or stdio
CHITTA_MCP="${CHITTA_MCP:-$HOME/.claude/bin/chitta}"

# Build JSON-RPC request
build_request() {
    local action="$1"
    local session_id="$2"
    local work_state="${WORK_STATE:-{}}"
    local continuation="${CONTINUATION:-{}}"

    cat <<EOF
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "ledger",
    "arguments": {
      "action": "$action",
      "session_id": "$session_id",
      "work_state": $work_state,
      "continuation": $continuation
    }
  }
}
EOF
}

# Send request to MCP server
send_request() {
    local request="$1"

    if [[ -x "$CHITTA_MCP" ]]; then
        echo "$request" | "$CHITTA_MCP" 2>/dev/null | head -1
    else
        echo '{"error": "chitta not found"}' >&2
        return 1
    fi
}

# Main
REQUEST=$(build_request "$ACTION" "$SESSION_ID")
RESPONSE=$(send_request "$REQUEST")

if echo "$RESPONSE" | grep -q '"error"'; then
    echo "[ledger] Error: $RESPONSE" >&2
    exit 1
else
    echo "[ledger] $ACTION complete for session $SESSION_ID"
fi
