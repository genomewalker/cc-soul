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

# Binaries installed to ~/.claude/bin/ by setup.sh
CHITTA_CLI="${HOME}/.claude/bin/chittad"
MIND_PATH="${HOME}/.claude/mind/chitta"
MODEL_PATH="${HOME}/.claude/bin/model.onnx"
VOCAB_PATH="${HOME}/.claude/bin/vocab.txt"
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

CHITTA_ARGS=()
CHITTAD_ARGS=()
CHITTAD_ARGS_WARNED=false

_djb2_hash() {
    local str="$1"
    local hash=5381
    local i c
    for ((i=0; i<${#str}; i++)); do
        c=$(printf '%d' "'${str:$i:1}")
        hash=$(( ((hash << 5) + hash) + c ))
        hash=$((hash & 0xFFFFFFFF))
    done
    echo "$hash"
}

init_chitta_args() {
    local help_output
    help_output=$(run_with_timeout "${HOME}/.claude/bin/chitta" --help 2>/dev/null || true)

    if [[ -z "$help_output" ]]; then
        return
    fi

    if echo "$help_output" | grep -q -- "--socket-path"; then
        local mind_hash
        mind_hash=$(_djb2_hash "$MIND_PATH")
        local socket_path="/tmp/chitta-${mind_hash}.sock"
        if [[ -S "$socket_path" ]]; then
            CHITTA_ARGS+=("--socket-path" "$socket_path")
        fi
    fi
}

init_chittad_args() {
    local help_output
    help_output=$(run_with_timeout "$CHITTA_CLI" --help 2>/dev/null || true)

    if [[ -z "$help_output" ]]; then
        return
    fi

    if echo "$help_output" | grep -q -- "--path"; then
        CHITTAD_ARGS+=("--path" "$MIND_PATH")
    fi

    if echo "$help_output" | grep -q -- "--model"; then
        if [[ -f "$MODEL_PATH" ]]; then
            CHITTAD_ARGS+=("--model" "$MODEL_PATH")
        elif [[ "$CHITTAD_ARGS_WARNED" != "true" ]]; then
            echo "[cc-soul] model.onnx missing; skipping --model" >&2
            CHITTAD_ARGS_WARNED=true
        fi
    fi

    if echo "$help_output" | grep -q -- "--vocab"; then
        if [[ -f "$VOCAB_PATH" ]]; then
            CHITTAD_ARGS+=("--vocab" "$VOCAB_PATH")
        elif [[ "$CHITTAD_ARGS_WARNED" != "true" ]]; then
            echo "[cc-soul] vocab.txt missing; skipping --vocab" >&2
            CHITTAD_ARGS_WARNED=true
        fi
    fi
}

if [[ ! -x "$CHITTA_CLI" ]]; then
    echo "[cc-soul] chittad not found; skipping soul prime" >&2
    exit 0
fi

if [[ ! -x "${HOME}/.claude/bin/chitta" ]]; then
    echo "[cc-soul] chitta not found; skipping soul prime" >&2
    exit 0
fi

init_chitta_args
init_chittad_args

# Get project name from git or cwd
project=$(basename "$(git rev-parse --show-toplevel 2>/dev/null || pwd)")

# Helper: call MCP
call_mcp() {
    local method="$1"
    local params="$2"
    local bin="${HOME}/.claude/bin/chitta"
    local request="{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"$method\",\"arguments\":$params},\"id\":1}"
    echo "$request" | run_with_timeout "$bin" "${CHITTA_ARGS[@]}" 2>/dev/null | grep -v '^\[chitta' | jq -r '.result.content[0].text' 2>/dev/null || true
}

output=""

# 1. SOUL STATE (always, one line)
state_json=$(run_with_timeout "$CHITTA_CLI" stats --json --fast "${CHITTAD_ARGS[@]}" 2>/dev/null || echo '{}')
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
