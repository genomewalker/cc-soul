#!/bin/bash
# Soul hook handler - socket-first, daemon-aware
#
# Usage: soul-hook.sh <hook-type> [options]
#   hook-type: start, end, prompt, pre-compact
#
# Architecture:
#   - Uses daemon socket directly for fast queries (no process spawn)
#   - Falls back to chitta_mcp thin client if socket unavailable
#   - Never uses direct mode (slow model loading)

set -e

# Resolve plugin directory (where this script lives)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(dirname "$SCRIPT_DIR")"

# Paths
CHITTA_BIN="$PLUGIN_DIR/bin/chitta_mcp"
SESSION_FILE="${HOME}/.claude/mind/.session_state"
LEAN_MODE="${CC_SOUL_LEAN:-false}"  # Set CC_SOUL_LEAN=true for minimal context

# Check jq exists (required for JSON parsing)
if ! command -v jq &> /dev/null; then
    echo "[cc-soul] jq not found. Install jq for full functionality." >&2
    exit 0
fi

# Find versioned daemon socket
find_socket() {
    # Look for versioned sockets first (e.g., /tmp/chitta-2.32.0.sock)
    local sock
    sock=$(ls -t /tmp/chitta-*.sock 2>/dev/null | head -1)
    if [[ -S "$sock" ]]; then
        echo "$sock"
        return 0
    fi
    # Fall back to legacy socket
    if [[ -S "/tmp/chitta.sock" ]]; then
        echo "/tmp/chitta.sock"
        return 0
    fi
    return 1
}

# Helper: query daemon socket directly (fast path)
socket_query() {
    local query="$1"
    local socket
    socket=$(find_socket) || return 1

    # Use timeout and netcat for socket communication
    echo "$query" | timeout 5 nc -U "$socket" 2>/dev/null | head -1
}

# Helper: call MCP tool via socket or thin client
call_mcp() {
    local method="$1"
    local params="$2"
    local request="{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"$method\",\"arguments\":$params},\"id\":1}"

    # Try socket first (fast)
    local response
    response=$(socket_query "$request" 2>/dev/null)
    if [[ -n "$response" ]]; then
        echo "$response" | jq -r '.result.content[0].text' 2>/dev/null || true
        return 0
    fi

    # Fall back to thin client (spawns process but uses daemon)
    if [[ -x "$CHITTA_BIN" ]]; then
        echo "$request" | "$CHITTA_BIN" 2>/dev/null | grep -v '^\[chitta' | jq -r '.result.content[0].text' 2>/dev/null || true
    fi
}

# Helper: get stats directly from daemon (fastest path)
get_stats() {
    socket_query "stats" 2>/dev/null || echo "{}"
}

# Helper: escape JSON string
json_escape() {
    jq -n --arg s "$1" '$s'
}

# Hook handlers
hook_start() {
    local trigger="${1:-startup}"

    # Record session start state
    local start_time=$(date +%s)
    local stats
    stats=$(call_mcp "soul_context" '{"format":"json"}')
    local start_nodes
    start_nodes=$(echo "$stats" | jq -r '.statistics.total_nodes' 2>/dev/null || echo "0")

    # Save session state for later delta calculation
    echo "{\"start_time\":$start_time,\"start_nodes\":$start_nodes}" > "$SESSION_FILE"

    # Try to load previous session's ledger for continuity
    local ledger_narrative
    ledger_narrative=$(call_mcp "ledger" '{"action":"load"}' 2>/dev/null)

    # Get soul context
    local context
    context=$(call_mcp "soul_context" '{"format":"text"}')

    if [[ "$LEAN_MODE" == "true" ]]; then
        # Ultra-lean: just τ and ψ in one line
        local tau psi nodes
        tau=$(echo "$context" | grep -oP 'τ.*?:\s*\K\d+' | head -1 || echo "?")
        psi=$(echo "$context" | grep -oP 'ψ.*?:\s*\K\d+' | head -1 || echo "?")
        nodes=$(echo "$context" | grep -oP 'Nodes:\s*\K\d+' | head -1 || echo "?")
        echo "[cc-soul] τ:${tau}% ψ:${psi}% nodes:${nodes}"
    else
        echo "[cc-soul] Session started"

        # Show ledger narrative if available (for resume)
        if [[ -n "$ledger_narrative" && "$ledger_narrative" != "null" && "$ledger_narrative" != *"No ledger found"* ]]; then
            echo ""
            echo "$ledger_narrative"
        fi

        # Show soul context
        if [[ -n "$context" && "$context" != "null" ]]; then
            echo ""
            echo "$context"
        fi
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
    # Let chitta auto-populate work_state with intentions and recent observations
    local duration_min=$((duration / 60))
    local session_id="session-$(date +%Y%m%d-%H%M%S)"
    local ledger_params
    ledger_params=$(cat <<EOF
{
  "action": "save",
  "session_id": "$session_id",
  "continuation": {
    "reason": "session_end",
    "session_duration_min": $duration_min,
    "node_delta": $node_delta
  }
}
EOF
)
    # soul_state and work_state are auto-populated by chitta with rich data
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
    local resonate=false
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --lean) lean=true; shift ;;
            --resonate) resonate=true; shift ;;
            *) shift ;;
        esac
    done

    # Read user message from stdin (hooks receive JSON)
    local input
    input=$(cat)
    local user_message
    user_message=$(echo "$input" | jq -r '.message // .prompt // .content // empty' 2>/dev/null | head -c 500)

    # Context-aware auto-ledger: save when context drops below 15%
    local context_remaining
    context_remaining=$(echo "$input" | jq -r '.context_window.remaining_percent // 100' 2>/dev/null || echo "100")
    local auto_save_marker="${HOME}/.claude/mind/.autosave_done"

    if [[ "$context_remaining" -lt 15 && ! -f "$auto_save_marker" ]]; then
        # Auto-save ledger before context gets too low
        local session_id="autosave-$(date +%Y%m%d-%H%M%S)"
        call_mcp "ledger" "{\"action\":\"save\",\"session_id\":\"$session_id\",\"continuation\":{\"reason\":\"low_context\",\"critical\":[\"Context at ${context_remaining}% - auto-saved\"]}}" >/dev/null 2>&1 || true
        touch "$auto_save_marker"
        echo "[cc-soul] Auto-saved ledger (context at ${context_remaining}%)"
    elif [[ "$context_remaining" -gt 50 && -f "$auto_save_marker" ]]; then
        # Reset marker when context is high again (e.g., after compact/new session)
        rm -f "$auto_save_marker"
    fi

    # Quick stats output
    if $lean && ! $resonate; then
        local stats
        stats=$(call_mcp "soul_context" '{"format":"json"}')
        local nodes
        nodes=$(echo "$stats" | jq -r '.statistics.total_nodes' 2>/dev/null || echo "0")
        local hot
        hot=$(echo "$stats" | jq -r '.statistics.hot_nodes' 2>/dev/null || echo "0")
        echo "[cc-soul] Nodes: $nodes ($hot hot)"
        return
    fi

    # Full resonance mode - inject relevant memories via daemon socket
    if $resonate && [[ -n "$user_message" && ${#user_message} -gt 10 ]]; then
        local limit=3
        [[ "$LEAN_MODE" == "true" ]] && limit=2

        # Escape query for JSON
        local query_escaped
        query_escaped=$(echo "$user_message" | jq -Rs '.' | sed 's/^"//;s/"$//')

        # Call full_resonate via daemon with exclude_tags filter
        local raw_output
        raw_output=$(call_mcp "full_resonate" "{\"query\":\"$query_escaped\",\"k\":$limit,\"exclude_tags\":[\"auto:cmd\",\"auto:file\",\"auto:edit\"]}")

        if [[ -n "$raw_output" && "$raw_output" != "null" ]]; then
            # Clean up output for display
            local resonance_output
            resonance_output=$(echo "$raw_output" \
                | grep -v "^Full resonance for:" \
                | head -c 500)

            if [[ -n "$resonance_output" ]]; then
                echo "$resonance_output"
                # Track token savings
                local chars_injected=${#resonance_output}
                "$SCRIPT_DIR/token-savings.sh" add-transparent "$chars_injected" 2>/dev/null || true
            fi
        fi
    fi
}

hook_pre_compact() {
    # Save state before compact - this is important!
    # Let chitta auto-populate soul_state and work_state
    local session_id="pre-compact-$(date +%Y%m%d-%H%M%S)"
    local ledger_params
    ledger_params=$(cat <<EOF
{
  "action": "save",
  "session_id": "$session_id",
  "continuation": {
    "reason": "context_compaction",
    "critical": ["Context was compacted - some details may be summarized"]
  }
}
EOF
)
    # soul_state and work_state are auto-populated by chitta
    call_mcp "ledger" "$ledger_params" >/dev/null 2>&1 || true

    # Record that a compact is happening
    call_mcp "observe" '{"category":"signal","title":"Pre-compact checkpoint","content":"Context about to be compacted. Ledger saved with full work state."}' >/dev/null 2>&1 || true
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
