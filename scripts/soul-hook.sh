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
CHITTA_BIN="$PLUGIN_DIR/chitta/build/chitta_mcp"
MIND_PATH="${HOME}/.claude/mind/chitta"
MODEL_PATH="$PLUGIN_DIR/chitta/models/model.onnx"
VOCAB_PATH="$PLUGIN_DIR/chitta/models/vocab.txt"

# Check binary exists
if [[ ! -x "$CHITTA_BIN" ]]; then
    echo "[cc-soul] chitta_mcp not found. Run setup.sh" >&2
    exit 0  # Don't fail hooks
fi

# Helper: call MCP tool
call_mcp() {
    local method="$1"
    local params="$2"
    local request="{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"$method\",\"arguments\":$params},\"id\":1}"
    echo "$request" | "$CHITTA_BIN" --path "$MIND_PATH" --model "$MODEL_PATH" --vocab "$VOCAB_PATH" 2>/dev/null | grep -v '^\[chitta' | jq -r '.result.content[0].text' 2>/dev/null || true
}

# Hook handlers
hook_start() {
    local after_compact=false
    [[ "$1" == "--after-compact" ]] && after_compact=true

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
    # Run maintenance cycle to save state
    local result
    result=$(call_mcp "cycle" '{"save":true}')
    echo "[cc-soul] Session ended, state saved"
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
        echo "[cc-soul] Nodes: $nodes"
    else
        local context
        context=$(call_mcp "soul_context" '{"format":"text"}')
        echo "$context"
    fi
}

hook_pre_compact() {
    # Save state before compact
    local result
    result=$(call_mcp "cycle" '{"save":true}')
    echo "[cc-soul] State saved before compact"
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
