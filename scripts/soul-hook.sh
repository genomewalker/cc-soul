#!/bin/bash
# Soul hook handler - self-contained, no Python dependency
#
# Usage: soul-hook.sh <hook-type> [options]
#   hook-type: start, end, prompt, pre-compact
#
# Requires: chitta_mcp binary built, ONNX models downloaded

set -e

# Resolve plugin directory (where this script lives)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(dirname "$SCRIPT_DIR")"

# Paths
CHITTA_BIN="$PLUGIN_DIR/bin/chitta_mcp"
MIND_PATH="${HOME}/.claude/mind/chitta"
MODEL_PATH="$PLUGIN_DIR/chitta/models/model.onnx"
VOCAB_PATH="$PLUGIN_DIR/chitta/models/vocab.txt"
SESSION_FILE="${HOME}/.claude/mind/.session_state"

# Check binary exists
if [[ ! -x "$CHITTA_BIN" ]]; then
    echo "[cc-soul] chitta_mcp not found. Run setup.sh" >&2
    exit 0  # Don't fail hooks
fi

# Check jq exists (required for JSON parsing)
if ! command -v jq &> /dev/null; then
    echo "[cc-soul] jq not found. Install jq for full functionality." >&2
    exit 0
fi

# Helper: call MCP tool
call_mcp() {
    local method="$1"
    local params="$2"
    local request="{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"$method\",\"arguments\":$params},\"id\":1}"
    echo "$request" | "$CHITTA_BIN" --path "$MIND_PATH" --model "$MODEL_PATH" --vocab "$VOCAB_PATH" 2>/dev/null | grep -v '^\[chitta' | jq -r '.result.content[0].text' 2>/dev/null || true
}

# Helper: escape JSON string (using jq, already verified above)
json_escape() {
    jq -n --arg s "$1" '$s'
}

# Hook handlers
hook_start() {
    local trigger="${1:-startup}"

    # If starting after clear, the previous session's ledger should already be saved
    # by pre-clear or session-end. Just log the trigger.
    if [[ "$trigger" == "clear" ]]; then
        echo "[cc-soul] Session started (after /clear)"
    fi

    # Record session start state
    local start_time=$(date +%s)
    local stats
    stats=$(call_mcp "soul_context" '{"format":"json"}')
    local start_nodes
    start_nodes=$(echo "$stats" | jq -r '.statistics.total_nodes' 2>/dev/null || echo "0")

    # Save session state
    echo "{\"start_time\":$start_time,\"start_nodes\":$start_nodes}" > "$SESSION_FILE"

    # Get soul context for injection
    local context
    context=$(call_mcp "soul_context" '{"format":"text"}')

    if [[ -n "$context" && "$context" != "null" ]]; then
        echo "[cc-soul] Session started"
        echo "$context"
    else
        echo "[cc-soul] Session started (empty soul)"
    fi
}

hook_end() {
    # Calculate session duration and node delta
    local end_time=$(date +%s)
    local duration=0
    local start_nodes=0
    local node_delta=0

    if [[ -f "$SESSION_FILE" ]]; then
        local start_time
        start_time=$(jq -r '.start_time' "$SESSION_FILE" 2>/dev/null || echo "$end_time")
        start_nodes=$(jq -r '.start_nodes' "$SESSION_FILE" 2>/dev/null || echo "0")
        duration=$((end_time - start_time))
        rm -f "$SESSION_FILE"
    fi

    # Get current state
    local stats
    stats=$(call_mcp "soul_context" '{"format":"json"}')
    local end_nodes
    end_nodes=$(echo "$stats" | jq -r '.statistics.total_nodes' 2>/dev/null || echo "0")
    local coherence
    coherence=$(echo "$stats" | jq -r '.coherence.tau_k' 2>/dev/null || echo "0.5")

    node_delta=$((end_nodes - start_nodes))

    # Save session ledger (Atman snapshot) before ending
    local duration_min=$((duration / 60))
    local session_id="session-$(date +%Y%m%d-%H%M%S)"
    local ledger_params
    ledger_params=$(cat <<EOF
{
  "action": "save",
  "session_id": "$session_id",
  "soul_state": {
    "coherence": $coherence,
    "nodes": $end_nodes,
    "duration_minutes": $duration_min
  },
  "continuation": {
    "reason": "session_end",
    "node_delta": $node_delta
  }
}
EOF
)
    call_mcp "ledger" "$ledger_params" >/dev/null 2>&1 || true

    # Record session observation (only if session was meaningful - >1 minute)
    if [[ $duration -gt 60 ]]; then
        local title="Session completed (${duration_min}m)"
        local content="Duration: ${duration_min} minutes. Nodes: ${start_nodes} → ${end_nodes} (${node_delta:+$node_delta}). Coherence: ${coherence}."

        # Escape for JSON
        local title_escaped=$(json_escape "$title")
        local content_escaped=$(json_escape "$content")

        call_mcp "observe" "{\"category\":\"session_ledger\",\"title\":$title_escaped,\"content\":$content_escaped}" >/dev/null 2>&1 || true
    fi

    # Run maintenance cycle to save state
    call_mcp "cycle" '{"save":true}' >/dev/null 2>&1 || true

    echo "[cc-soul] Session ended (${duration}s, ${node_delta:+$node_delta} nodes, ledger saved)"
}

hook_prompt() {
    local lean=false
    [[ "$1" == "--lean" ]] && lean=true

    # Quick recall for relevant wisdom based on recent context
    # In lean mode, we just check coherence
    if $lean; then
        local stats
        stats=$(call_mcp "soul_context" '{"format":"json"}')
        local nodes
        nodes=$(echo "$stats" | jq -r '.statistics.total_nodes' 2>/dev/null || echo "0")
        local hot
        hot=$(echo "$stats" | jq -r '.statistics.hot_nodes' 2>/dev/null || echo "0")
        echo "[cc-soul] Nodes: $nodes ($hot hot)"
    else
        local context
        context=$(call_mcp "soul_context" '{"format":"text"}')
        echo "$context"
    fi
}

hook_pre_compact() {
    # Save state before compact - this is important!
    # Get current state for ledger
    local stats
    stats=$(call_mcp "soul_context" '{"format":"json"}')
    local nodes
    nodes=$(echo "$stats" | jq -r '.statistics.total_nodes' 2>/dev/null || echo "0")
    local coherence
    coherence=$(echo "$stats" | jq -r '.coherence.tau_k' 2>/dev/null || echo "0.5")

    # Save session ledger (Atman snapshot) before compaction
    local session_id="pre-compact-$(date +%Y%m%d-%H%M%S)"
    local ledger_params
    ledger_params=$(cat <<EOF
{
  "action": "save",
  "session_id": "$session_id",
  "soul_state": {
    "coherence": $coherence,
    "nodes": $nodes
  },
  "continuation": {
    "reason": "context_compaction",
    "critical": ["Context was compacted - some details may be summarized"]
  }
}
EOF
)
    call_mcp "ledger" "$ledger_params" >/dev/null 2>&1 || true

    # Record that a compact is happening
    call_mcp "observe" '{"category":"signal","title":"Pre-compact checkpoint","content":"Context about to be compacted. Ledger and soul state saved."}' >/dev/null 2>&1 || true
    call_mcp "cycle" '{"save":true}' >/dev/null 2>&1 || true
    echo "[cc-soul] Ledger and state saved before compact"
}

# Main dispatch
case "${1:-help}" in
    start)
        shift
        hook_start "$@"
        ;;
    end)
        hook_end
        ;;
    prompt)
        shift
        hook_prompt "$@"
        ;;
    pre-compact)
        hook_pre_compact
        ;;
    *)
        echo "Usage: soul-hook.sh <start|end|prompt|pre-compact> [options]"
        exit 1
        ;;
esac
