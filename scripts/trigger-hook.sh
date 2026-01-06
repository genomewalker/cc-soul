#!/bin/bash
# Trigger hook - parse user prompt for memory triggers
#
# Triggers:
#   remember: / remember this:  → observe (discovery)
#   note: / note to self:       → observe (signal)
#   important:                  → observe (decision)
#   belief: / I believe         → grow belief
#   lesson: / learned:          → grow wisdom
#   mistake: / failed:          → grow failure
#
# Usage: Called by UserPromptSubmit hook with JSON on stdin

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(dirname "$SCRIPT_DIR")"

CHITTA_BIN="$PLUGIN_DIR/bin/chitta_mcp"
MIND_PATH="${HOME}/.claude/mind/chitta"
MODEL_PATH="$PLUGIN_DIR/chitta/models/model.onnx"
VOCAB_PATH="$PLUGIN_DIR/chitta/models/vocab.txt"

# Check dependencies
if [[ ! -x "$CHITTA_BIN" ]]; then
    exit 0
fi

if ! command -v jq &> /dev/null; then
    exit 0
fi

# Read input from stdin
INPUT=$(cat)

# Extract prompt from JSON
PROMPT=$(echo "$INPUT" | jq -r '.prompt // empty' 2>/dev/null)

if [[ -z "$PROMPT" ]]; then
    exit 0
fi

# Helper: call MCP tool
call_mcp() {
    local method="$1"
    local params="$2"
    local request="{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"$method\",\"arguments\":$params},\"id\":1}"
    echo "$request" | "$CHITTA_BIN" --path "$MIND_PATH" --model "$MODEL_PATH" --vocab "$VOCAB_PATH" 2>/dev/null | grep -v '^\[chitta' || true
}

# Helper: escape for JSON
json_escape() {
    echo "$1" | jq -Rs '.' | sed 's/^"//;s/"$//'
}

# Check for triggers (case-insensitive)
PROMPT_LOWER=$(echo "$PROMPT" | tr '[:upper:]' '[:lower:]')

# Pattern: remember: or remember this:
if [[ "$PROMPT_LOWER" =~ ^remember[[:space:]]*this?[[:space:]]*:[[:space:]]*(.*) ]] || \
   [[ "$PROMPT_LOWER" =~ remember[[:space:]]*:[[:space:]]*(.*) ]]; then
    CONTENT="${PROMPT#*:}"
    CONTENT=$(echo "$CONTENT" | sed 's/^[[:space:]]*//')
    TITLE=$(echo "$CONTENT" | head -c 60)
    ESCAPED_CONTENT=$(json_escape "$CONTENT")
    ESCAPED_TITLE=$(json_escape "$TITLE")
    call_mcp "observe" "{\"category\":\"discovery\",\"title\":\"$ESCAPED_TITLE\",\"content\":\"$ESCAPED_CONTENT\",\"tags\":\"trigger:remember\"}" >/dev/null
    echo "[cc-soul] Remembered: $TITLE" >&2
fi

# Pattern: note: or note to self:
if [[ "$PROMPT_LOWER" =~ ^note[[:space:]]*(to[[:space:]]*self)?[[:space:]]*:[[:space:]]*(.*) ]]; then
    CONTENT="${PROMPT#*:}"
    CONTENT=$(echo "$CONTENT" | sed 's/^[[:space:]]*//')
    TITLE=$(echo "$CONTENT" | head -c 60)
    ESCAPED_CONTENT=$(json_escape "$CONTENT")
    ESCAPED_TITLE=$(json_escape "$TITLE")
    call_mcp "observe" "{\"category\":\"signal\",\"title\":\"$ESCAPED_TITLE\",\"content\":\"$ESCAPED_CONTENT\",\"tags\":\"trigger:note\"}" >/dev/null
    echo "[cc-soul] Noted: $TITLE" >&2
fi

# Pattern: important:
if [[ "$PROMPT_LOWER" =~ ^important[[:space:]]*:[[:space:]]*(.*) ]]; then
    CONTENT="${PROMPT#*:}"
    CONTENT=$(echo "$CONTENT" | sed 's/^[[:space:]]*//')
    TITLE=$(echo "$CONTENT" | head -c 60)
    ESCAPED_CONTENT=$(json_escape "$CONTENT")
    ESCAPED_TITLE=$(json_escape "$TITLE")
    call_mcp "observe" "{\"category\":\"decision\",\"title\":\"$ESCAPED_TITLE\",\"content\":\"$ESCAPED_CONTENT\",\"tags\":\"trigger:important\"}" >/dev/null
    echo "[cc-soul] Important: $TITLE" >&2
fi

# Pattern: belief: or I believe
if [[ "$PROMPT_LOWER" =~ ^belief[[:space:]]*:[[:space:]]*(.*) ]] || \
   [[ "$PROMPT_LOWER" =~ ^i[[:space:]]+believe[[:space:]]+(.*) ]]; then
    if [[ "$PROMPT_LOWER" =~ ^belief ]]; then
        CONTENT="${PROMPT#*:}"
    else
        CONTENT="${PROMPT#*believe }"
    fi
    CONTENT=$(echo "$CONTENT" | sed 's/^[[:space:]]*//')
    ESCAPED_CONTENT=$(json_escape "$CONTENT")
    call_mcp "grow" "{\"type\":\"belief\",\"content\":\"$ESCAPED_CONTENT\"}" >/dev/null
    echo "[cc-soul] Belief recorded" >&2
fi

# Pattern: lesson: or learned:
if [[ "$PROMPT_LOWER" =~ ^lesson[[:space:]]*:[[:space:]]*(.*) ]] || \
   [[ "$PROMPT_LOWER" =~ ^learned[[:space:]]*:[[:space:]]*(.*) ]]; then
    CONTENT="${PROMPT#*:}"
    CONTENT=$(echo "$CONTENT" | sed 's/^[[:space:]]*//')
    TITLE=$(echo "$CONTENT" | head -c 60)
    ESCAPED_CONTENT=$(json_escape "$CONTENT")
    ESCAPED_TITLE=$(json_escape "$TITLE")
    call_mcp "grow" "{\"type\":\"wisdom\",\"title\":\"$ESCAPED_TITLE\",\"content\":\"$ESCAPED_CONTENT\"}" >/dev/null
    echo "[cc-soul] Wisdom recorded: $TITLE" >&2
fi

# Pattern: mistake: or failed:
if [[ "$PROMPT_LOWER" =~ ^mistake[[:space:]]*:[[:space:]]*(.*) ]] || \
   [[ "$PROMPT_LOWER" =~ ^failed[[:space:]]*:[[:space:]]*(.*) ]]; then
    CONTENT="${PROMPT#*:}"
    CONTENT=$(echo "$CONTENT" | sed 's/^[[:space:]]*//')
    TITLE=$(echo "$CONTENT" | head -c 60)
    ESCAPED_CONTENT=$(json_escape "$CONTENT")
    ESCAPED_TITLE=$(json_escape "$TITLE")
    call_mcp "grow" "{\"type\":\"failure\",\"title\":\"$ESCAPED_TITLE\",\"content\":\"$ESCAPED_CONTENT\"}" >/dev/null
    echo "[cc-soul] Failure recorded: $TITLE" >&2
fi

# ═══════════════════════════════════════════════════════════════════════════
# Neural Triggers: Semantic recall for wisdom surfacing
# ═══════════════════════════════════════════════════════════════════════════

NEURAL_THRESHOLD=0.30
NEURAL_LIMIT=2
CLI_BIN="$PLUGIN_DIR/bin/chitta_cli"

# Skip neural triggers for short prompts or commands
if [[ ${#PROMPT} -lt 15 ]] || [[ "$PROMPT_LOWER" =~ ^(yes|no|ok|thanks|done|help|exit|quit|clear|y|n)$ ]]; then
    exit 0
fi

# Skip if CLI not available
if [[ ! -x "$CLI_BIN" ]]; then
    exit 0
fi

# Run semantic recall (use default paths - explicit paths break embedding loading)
RECALL_OUTPUT=$(timeout 5 "$CLI_BIN" recall "$PROMPT" 2>/dev/null || true)

# Parse results above threshold
NEURAL_MATCHES=""
MATCH_COUNT=0
CURRENT_SCORE=""

while IFS= read -r line; do
    # Skip loading messages
    [[ "$line" =~ ^\[TieredStorage\] ]] && continue
    [[ "$line" =~ ^Results\ for: ]] && continue
    [[ "$line" =~ ^═ ]] && continue

    # Extract score from lines like "[1] (score: 0.549431)"
    if [[ "$line" =~ ^\[([0-9]+)\][[:space:]]*\(score:[[:space:]]*([0-9.]+)\) ]]; then
        CURRENT_SCORE="${BASH_REMATCH[2]}"
        continue
    fi

    # If we have a pending score and this line starts with [cc-soul], it's the title
    if [[ -n "$CURRENT_SCORE" ]] && [[ "$line" =~ ^\[cc-soul\] ]]; then
        # Compare score to threshold
        if command -v bc &>/dev/null; then
            ABOVE=$(echo "$CURRENT_SCORE > $NEURAL_THRESHOLD" | bc -l 2>/dev/null || echo "0")
        else
            ABOVE=0
            [[ "${CURRENT_SCORE:0:3}" > "0.2" ]] && ABOVE=1
        fi

        if [[ "$ABOVE" == "1" ]] && [[ $MATCH_COUNT -lt $NEURAL_LIMIT ]]; then
            TITLE="${line#\[cc-soul\] }"
            TITLE="${TITLE:0:80}"
            NEURAL_MATCHES="${NEURAL_MATCHES}• ${TITLE}\n"
            ((MATCH_COUNT++))
        fi
        CURRENT_SCORE=""
    fi
done <<< "$RECALL_OUTPUT"

# Output neural matches if found
if [[ $MATCH_COUNT -gt 0 ]]; then
    echo "[cc-soul] Related wisdom:" >&2
    printf "%b" "$NEURAL_MATCHES" >&2
fi

exit 0
