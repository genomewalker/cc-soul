#!/bin/bash
# UserPromptSubmit hook: Surface relevant memories + detect learning opportunities
#
# HIGH PERFORMANCE: Single call with filtering
# - Uses full_resonate (better than recall - has spreading activation)
# - Filters out code symbols for non-code queries
# - Minimum 30% confidence threshold
# - Detects patterns for proactive learning

set -e

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-2}"
MIN_CONFIDENCE=30
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"

# djb2 hash - must match C++ implementation in socket_server.hpp
djb2_hash() {
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

MIND_HASH=$(djb2_hash "$MIND_PATH")

# Parse input - Claude Code sends JSON with session_id and prompt
INPUT=$(cat)
# Try to extract prompt from JSON, fall back to raw input if not JSON
QUERY=$(echo "$INPUT" | jq -r '.prompt // empty' 2>/dev/null)
[[ -z "$QUERY" ]] && QUERY="$INPUT"

[[ -z "$QUERY" ]] && exit 0
[[ ! -x "$CHITTA_BIN" ]] && exit 0

# Save user message for Stop hook to analyze
mkdir -p "$MIND_PATH"
echo "$QUERY" > "$MIND_PATH/.last_user_message"

# Detect if query is about code (include symbols) or conversation (exclude symbols)
IS_CODE_QUERY=false
if echo "$QUERY" | grep -qiE '(function|class|method|implement|code|file|\.py|\.js|\.cpp|\.ts|error|bug|fix)'; then
    IS_CODE_QUERY=true
fi

# Use full_resonate for better semantic matching
# For non-code queries, use --partnership-only to exclude code intel kinds
if [[ "$IS_CODE_QUERY" == "false" ]]; then
    memories=$(timeout "$MAX_WAIT" "$CHITTA_BIN" full_resonate --query "$QUERY" --k 6 --partnership-only true 2>/dev/null || true)
else
    memories=$(timeout "$MAX_WAIT" "$CHITTA_BIN" full_resonate --query "$QUERY" --k 6 2>/dev/null || true)
fi

if [[ -z "$memories" || "$memories" == *"No memories"* ]]; then
    exit 0
fi

# Filter and format results
OUTPUT=""
COUNT=0
while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    [[ ! "$line" =~ ^\[[0-9]+%\] ]] && continue

    # Extract confidence
    conf=$(echo "$line" | grep -oE '^\[[0-9]+%\]' | tr -d '[]%')
    [[ -z "$conf" ]] && continue

    # Skip low confidence
    [[ "$conf" -lt "$MIN_CONFIDENCE" ]] && continue

    # Code symbol filtering is now done server-side via --partnership-only flag

    # Truncate and add
    OUTPUT="$OUTPUT${line:0:150}
"
    ((++COUNT))
    [[ $COUNT -ge 3 ]] && break
done <<< "$memories"

# ===========================================
# PATTERN DETECTION: Detect learning opportunities
# ===========================================
LEARNING_HINTS=""

# Detect CORRECTION patterns: user is correcting Claude
# Direct: "wrong", "mistake", "not working", "incorrect"
# Implicit: "actually", "should be", "not what I"
if echo "$QUERY" | grep -qiE "(wrong|mistake|not working|incorrect|actually[, ]|that'?s not|you('re| are) (wrong|missing)|I (said|meant|asked)|not what I|won'?t work|should be|not quite|use your memory|check.*memory|did you forget)"; then
    LEARNING_HINTS="[LEARN] ⚠️ CORRECTION - call learn_correction NOW with what was wrong and what's right"
fi

# Detect PREFERENCE patterns: user expressing preferences
# Direct preferences: "I prefer", "always", "never"
# Meta-preferences: "more concise", "fewer examples", "go deeper", "simpler please", "don't overexplain", "be more verbose"
if echo "$QUERY" | grep -qiE "(I (prefer|like|want|need|always|never|don'?t like)|please (don'?t|always|never)|stop doing|keep doing|from now on|in the future|more concise|fewer examples|go deeper|simpler please|don'?t overexplain|be more verbose)"; then
    LEARNING_HINTS="${LEARNING_HINTS:+$LEARNING_HINTS; }[LEARN] Preference detected → use learn_preference tool"
fi

# Detect FRUSTRATION/STATE patterns: user emotional state
# Strong frustration: "frustrated", "annoyed", "give up"
# Mild frustration: "tedious", "repetitive", "not sure", "overthinking"
if echo "$QUERY" | grep -qiE "(frustrated|annoyed|confused|stuck|lost|this is (hard|difficult|confusing)|I give up|help me understand|what am I missing|tedious|repetitive|not sure|overthinking)"; then
    LEARNING_HINTS="${LEARNING_HINTS:+$LEARNING_HINTS; }[LEARN] User state detected → use learn_approach if something helps"
fi

# Detect MILESTONE patterns: achievement
if echo "$QUERY" | grep -qiE "(it works|finally|success|done|shipped|released|completed|finished|passed|merged|deployed)"; then
    LEARNING_HINTS="${LEARNING_HINTS:+$LEARNING_HINTS; }[LEARN] Milestone detected → use learn_milestone tool"
fi

# ===========================================
# ANTICIPATION: Predict likely next actions
# ===========================================
ANTICIPATIONS=""
SOCKET_PATH="${CHITTA_SOCKET:-/tmp/chitta-${MIND_HASH}.sock}"
PREDICTIONS_FILE="$MIND_PATH/.last_predictions.json"

# Clear old predictions
rm -f "$PREDICTIONS_FILE" 2>/dev/null

# Call daemon via socket to get predictions with IDs
if [[ -S "$SOCKET_PATH" ]]; then
    # Build JSON-RPC request
    request='{"jsonrpc":"2.0","id":1,"method":"anticipation_predict","params":{"context":"'"$(echo "$QUERY" | sed 's/"/\\"/g' | tr '\n' ' ')"'","limit":3}}'

    # Call daemon (timeout 1s)
    response=$(timeout 1 echo "$request" | nc -U -N "$SOCKET_PATH" 2>/dev/null || true)

    if [[ -n "$response" ]]; then
        # Extract patterns array from JSON response
        patterns=$(echo "$response" | jq -r '.result.metadata.patterns // []' 2>/dev/null)

        if [[ "$patterns" != "[]" && -n "$patterns" ]]; then
            # Save full predictions to temp file for stop-hook
            echo "$patterns" > "$PREDICTIONS_FILE"

            # Extract action names for display (confidence > 40% based on frequency)
            actions=""
            while read -r pattern; do
                [[ -z "$pattern" ]] && continue

                freq=$(echo "$pattern" | jq -r '.frequency // 0' 2>/dev/null)
                success=$(echo "$pattern" | jq -r '.success_count // 0' 2>/dev/null)
                action=$(echo "$pattern" | jq -r '.action // ""' 2>/dev/null)

                # Simple confidence heuristic: success rate or frequency > 2
                if [[ "$freq" -gt 2 || "$success" -gt 0 ]] && [[ -n "$action" ]]; then
                    actions="${actions:+$actions, }$action"
                fi
            done <<< "$(echo "$patterns" | jq -c '.[]' 2>/dev/null)"

            if [[ -n "$actions" ]]; then
                ANTICIPATIONS="[anticipate] Predicted: $actions"
            fi
        fi
    fi
fi

# ===========================================
# OUTPUT - Learning hints FIRST (so Claude sees them immediately)
# ===========================================
# Learning hints at top - these are action items for Claude
if [[ -n "$LEARNING_HINTS" ]]; then
    echo "$LEARNING_HINTS"
fi

# Then memories
if [[ -n "$OUTPUT" && $COUNT -gt 0 ]]; then
    echo "[soul]"
    echo -n "$OUTPUT"
fi

# Anticipations last
if [[ -n "$ANTICIPATIONS" ]]; then
    echo "$ANTICIPATIONS"
fi

exit 0
