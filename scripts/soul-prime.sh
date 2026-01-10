#!/bin/bash
# Soul priming - Proactively guide Claude's behavior
#
# The soul primes Claude with:
# 1. Project context (from codemap if available)
# 2. Recent work state (from ledger)
# 3. Relevant patterns (from wisdom)
# 4. Behavioral guidance (from soul state)
#
# This runs at session start to pre-load Claude's "intuition"

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(dirname "$SCRIPT_DIR")"
CHITTA_CLI="$PLUGIN_DIR/bin/chitta_cli"
MIND_PATH="${HOME}/.claude/mind/chitta"
MODEL_PATH="$PLUGIN_DIR/chitta/models/model.onnx"
VOCAB_PATH="$PLUGIN_DIR/chitta/models/vocab.txt"

# Get project name from git or cwd
project=$(basename "$(git rev-parse --show-toplevel 2>/dev/null || pwd)")

# Helper: call MCP
call_mcp() {
    local method="$1"
    local params="$2"
    local bin="$PLUGIN_DIR/bin/chitta"
    local request="{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"$method\",\"arguments\":$params},\"id\":1}"
    echo "$request" | "$bin" --path "$MIND_PATH" --model "$MODEL_PATH" --vocab "$VOCAB_PATH" 2>/dev/null | grep -v '^\[chitta' | jq -r '.result.content[0].text' 2>/dev/null || true
}

output=""

# 1. SOUL STATE (always, one line)
state_json=$("$CHITTA_CLI" stats --json --fast --path "$MIND_PATH" 2>/dev/null || echo '{}')
tau=$(echo "$state_json" | jq -r '.coherence.tau // 0.84' 2>/dev/null)
psi=$(echo "$state_json" | jq -r '.ojas.psi // 0.88' 2>/dev/null)
nodes=$(echo "$state_json" | jq -r '.total // 0' 2>/dev/null)
tau_pct=$(printf "%.0f" $(echo "$tau * 100" | bc -l 2>/dev/null) 2>/dev/null || echo "84")
psi_pct=$(printf "%.0f" $(echo "$psi * 100" | bc -l 2>/dev/null) 2>/dev/null || echo "88")

output+="Soul: τ:${tau_pct}% ψ:${psi_pct}% (${nodes} memories)\n"

# 2. PROJECT CODEMAP (if exists, micro view)
codemap=$(call_mcp "recall" "{\"query\":\"codemap $project\",\"zoom\":\"micro\",\"limit\":1}")
if [[ -n "$codemap" && "$codemap" != *"Found 0"* ]]; then
    # Extract just the title line
    map_title=$(echo "$codemap" | grep -oP '\[\d+%\].*' | head -1 | cut -c1-60)
    if [[ -n "$map_title" ]]; then
        output+="Project: $map_title\n"
    fi
fi

# 3. CONTINUATION (from ledger, if resuming work)
ledger=$(call_mcp "ledger" '{"action":"load"}' 2>/dev/null)
if [[ -n "$ledger" && "$ledger" != *"No ledger"* ]]; then
    # Extract next steps only
    next=$(echo "$ledger" | grep -A2 "Next:" | head -2 | tail -1 | cut -c1-80)
    if [[ -n "$next" ]]; then
        output+="Continue: $next\n"
    fi
fi

# 4. BEHAVIORAL PRIMING (based on state)
if [[ "$tau_pct" -lt 50 ]]; then
    output+="[Be careful - knowledge may conflict]\n"
elif [[ "$tau_pct" -gt 90 ]]; then
    output+="[High clarity - can be bold]\n"
fi

if [[ "$psi_pct" -lt 50 ]]; then
    output+="[Low energy - consolidate, don't explore]\n"
fi

# Output compact prime
echo -e "$output"
