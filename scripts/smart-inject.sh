#!/bin/bash
# Smart context injection - Soul decides what Claude needs
#
# Instead of Claude calling recall, the soul:
# 1. Analyzes the query
# 2. Decides what context is relevant
# 3. Injects ONLY what's needed
# 4. Guides behavior based on state
#
# Usage: smart-inject.sh <query>

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

CHITTAD_ARGS=()
CHITTAD_ARGS_WARNED=false

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
    echo "[cc-soul] chittad not found; skipping smart inject" >&2
    exit 0
fi

init_chittad_args

query="$1"
[[ -z "$query" ]] && exit 0

# Get soul state
state_json=$(run_with_timeout "$CHITTA_CLI" stats --json --fast "${CHITTAD_ARGS[@]}" 2>/dev/null || echo '{}')
tau=$(echo "$state_json" | jq -r '.coherence.tau // 0.84' 2>/dev/null)
psi=$(echo "$state_json" | jq -r '.ojas.psi // 0.88' 2>/dev/null)

# Convert to percentage (0-100)
tau_pct=$(echo "$tau * 100" | bc -l 2>/dev/null | cut -d. -f1 || echo "84")
psi_pct=$(echo "$psi * 100" | bc -l 2>/dev/null | cut -d. -f1 || echo "88")

# Soul-driven context injection
output=""

# 1. STATE GUIDANCE - Soul tells Claude how to behave
if [[ "$tau_pct" -lt 50 ]]; then
    output+="[Soul: Low coherence (${tau_pct}%) - be careful, clarify before acting]\n"
fi
if [[ "$psi_pct" -lt 50 ]]; then
    output+="[Soul: Low vitality (${psi_pct}%) - focus on consolidation]\n"
fi

# 2. RELEVANT MEMORIES - Only if query warrants it
if [[ ${#query} -gt 15 ]]; then
    # Get resonant memories (soul decides relevance)
    memories=$(run_with_timeout "$CHITTA_CLI" resonate "$query" --limit 2 "${CHITTAD_ARGS[@]}" 2>/dev/null \
        | grep -v "^No resonant" \
        | grep -v "^\[chitta" \
        | head -c 400)  # Strict limit

    if [[ -n "$memories" ]]; then
        output+="$memories\n"
    fi
fi

# 3. ACTIVE INTENTIONS - Remind Claude of current goals
intentions=$(run_with_timeout "$CHITTA_CLI" intentions "${CHITTAD_ARGS[@]}" 2>/dev/null | head -2)
if [[ -n "$intentions" && "$intentions" != *"No active"* ]]; then
    output+="[Active: $(echo "$intentions" | head -1 | cut -c1-80)]\n"
fi

# Output if we have anything relevant
if [[ -n "$output" ]]; then
    echo -e "$output"
fi
