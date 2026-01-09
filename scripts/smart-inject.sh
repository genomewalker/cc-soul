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
CHITTA_CLI="$PLUGIN_DIR/bin/chitta_cli"
MIND_PATH="${HOME}/.claude/mind/chitta"
MODEL_PATH="$PLUGIN_DIR/chitta/models/model.onnx"
VOCAB_PATH="$PLUGIN_DIR/chitta/models/vocab.txt"

query="$1"
[[ -z "$query" ]] && exit 0

# Get soul state
state_json=$("$CHITTA_CLI" stats --json --fast --path "$MIND_PATH" 2>/dev/null || echo '{}')
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
    memories=$("$CHITTA_CLI" resonate "$query" --path "$MIND_PATH" --model "$MODEL_PATH" --vocab "$VOCAB_PATH" --limit 2 2>/dev/null \
        | grep -v "^No resonant" \
        | grep -v "^\[chitta" \
        | head -c 400)  # Strict limit

    if [[ -n "$memories" ]]; then
        output+="$memories\n"
    fi
fi

# 3. ACTIVE INTENTIONS - Remind Claude of current goals
intentions=$("$CHITTA_CLI" intentions --path "$MIND_PATH" 2>/dev/null | head -2)
if [[ -n "$intentions" && "$intentions" != *"No active"* ]]; then
    output+="[Active: $(echo "$intentions" | head -1 | cut -c1-80)]\n"
fi

# Output if we have anything relevant
if [[ -n "$output" ]]; then
    echo -e "$output"
fi
