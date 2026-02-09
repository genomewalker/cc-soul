#!/bin/bash
# UserPromptSubmit hook: Surface relevant memories + detect learning opportunities
#
# HIGH PERFORMANCE: Single call with smart routing
# - Uses smart_recall (auto-classifies query intent and routes optimally)
# - Handles temporal, aspect, entity, code, and exploratory queries
# - Minimum 30% confidence threshold
# - Detects patterns for proactive learning

# Don't use set -e: we want hooks to succeed even if some parts fail

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-2}"
MIN_CONFIDENCE=30
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"

# Source shared library
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/lib.sh"

SOCKET_PATH=$(get_socket_path)
SESSION_ID=$(get_session_id)
[[ -z "$SESSION_ID" ]] && SESSION_ID="default"

# Parse input - Claude Code sends JSON with session_id and prompt (gracefully handle malformed input)
INPUT=$(cat)
# Try to extract prompt from JSON, fall back to raw input if not JSON
QUERY=$(echo "$INPUT" | jq -r '.prompt // empty' 2>/dev/null || echo "")
[[ -z "$QUERY" ]] && QUERY="$INPUT"

[[ -z "$QUERY" ]] && exit 0
[[ ! -x "$CHITTA_BIN" ]] && exit 0

# Save user message for Stop hook to analyze
mkdir -p "$MIND_PATH"
echo "$QUERY" > "$MIND_PATH/.last_user_message"

# Get turn index from counter file
TURN_FILE="$MIND_PATH/.turn_index_$SESSION_ID"
TURN_INDEX=$(cat "$TURN_FILE" 2>/dev/null || echo "0")

# Store user turn in lossless conversation storage
QUEUE_FILE="${CHITTA_QUEUE:-/tmp/chitta-queue.jsonl}"
echo "{\"tool\":\"store_turn\",\"args\":{\"session_id\":\"$SESSION_ID\",\"role\":\"user\",\"content\":$(echo "$QUERY" | jq -Rs .),\"turn_index\":$TURN_INDEX},\"ts\":$(date +%s)}" >> "$QUEUE_FILE"

# Increment turn index
echo $((TURN_INDEX + 1)) > "$TURN_FILE"

# Use smart_recall for intelligent query routing
# - Automatically classifies query intent (temporal, aspect, entity, code, etc.)
# - Routes to optimal retrieval strategy
memories=$(timeout "$MAX_WAIT" "$CHITTA_BIN" smart_recall --query "$QUERY" --limit 6 2>/dev/null || true)

if [[ -z "$memories" || "$memories" == *"No memories"* ]]; then
    exit 0
fi

# Filter and format results
OUTPUT=""
COUNT=0
while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    # Match both formats: "[79%]..." (full_resonate) and "#123 [kind] [79%]..." (smart_recall)
    [[ ! "$line" =~ \[[0-9]+%\] ]] && continue

    # Extract confidence from anywhere in line
    conf=$(echo "$line" | grep -oE '\[[0-9]+%\]' | head -1 | tr -d '[]%')
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
# NARRATIVE: Get current work mode and log user message
# ===========================================
NARRATIVE_STATUS=""

# Log user_message event via queue (fire-and-forget)
if [[ -S "$SOCKET_PATH" ]]; then
    # First, ensure gate is initialized for this session
    request='{"jsonrpc":"2.0","id":1,"method":"gate_init","params":{"session_id":"'"$SESSION_ID"'"}}'
    timeout 0.5 echo "$request" | nc -U -N "$SOCKET_PATH" >/dev/null 2>&1 || true

    # Log the user message event
    summary=$(echo "$QUERY" | head -c 200 | tr '\n' ' ' | sed 's/"/\\"/g')
    request='{"jsonrpc":"2.0","id":2,"method":"narrative_log","params":{"session_id":"'"$SESSION_ID"'","kind":"user_message","summary":"'"$summary"'"}}'
    timeout 0.5 echo "$request" | nc -U -N "$SOCKET_PATH" >/dev/null 2>&1 || true

    # Get narrative status
    request='{"jsonrpc":"2.0","id":3,"method":"narrative_status","params":{"session_id":"'"$SESSION_ID"'"}}'
    response=$(timeout 1 echo "$request" | nc -U -N "$SOCKET_PATH" 2>/dev/null || true)

    if [[ -n "$response" ]]; then
        mode=$(echo "$response" | jq -r '.result.metadata.mode // "unknown"' 2>/dev/null)
        confidence=$(echo "$response" | jq -r '.result.metadata.confidence // 0' 2>/dev/null)
        if [[ "$mode" != "unknown" && "$mode" != "null" ]]; then
            # Format as percentage
            conf_pct=$(awk "BEGIN {printf \"%.0f\", $confidence * 100}")
            NARRATIVE_STATUS="[narrative:$mode:$conf_pct%]"
        fi
    fi
fi

# ===========================================
# ANTICIPATION: Predict likely next actions (using new anticipation_filter)
# ===========================================
ANTICIPATIONS=""
PREDICTIONS_FILE="$MIND_PATH/.last_predictions.json"

# Clear old predictions
rm -f "$PREDICTIONS_FILE" 2>/dev/null

# Call anticipation_filter via socket to get gated predictions
if [[ -S "$SOCKET_PATH" ]]; then
    request='{"jsonrpc":"2.0","id":4,"method":"anticipation_filter","params":{"session_id":"'"$SESSION_ID"'","max":3}}'
    response=$(timeout 1 echo "$request" | nc -U -N "$SOCKET_PATH" 2>/dev/null || true)

    if [[ -n "$response" ]]; then
        candidates=$(echo "$response" | jq -r '.result.metadata.candidates // []' 2>/dev/null)

        if [[ "$candidates" != "[]" && -n "$candidates" ]]; then
            # Save predictions to temp file for stop-hook
            echo "$candidates" > "$PREDICTIONS_FILE"

            # Extract predictions
            while read -r candidate; do
                [[ -z "$candidate" ]] && continue

                id=$(echo "$candidate" | jq -r '.id // 0' 2>/dev/null)
                prediction=$(echo "$candidate" | jq -r '.prediction // ""' 2>/dev/null)
                source=$(echo "$candidate" | jq -r '.source // "rule"' 2>/dev/null)
                confidence=$(echo "$candidate" | jq -r '.confidence // 0' 2>/dev/null)

                if [[ -n "$prediction" && "$prediction" != "null" ]]; then
                    conf_pct=$(awk "BEGIN {printf \"%.0f\", $confidence * 100}")
                    ANTICIPATIONS="${ANTICIPATIONS}[anticipate:$source:$conf_pct%] ${prediction}
"
                fi
            done <<< "$(echo "$candidates" | jq -c '.[]' 2>/dev/null)"
        fi
    fi

    # Fall back to old anticipation_predict if no candidates from filter
    if [[ -z "$ANTICIPATIONS" ]]; then
        request='{"jsonrpc":"2.0","id":5,"method":"anticipation_predict","params":{"context":"'"$(echo "$QUERY" | sed 's/"/\\"/g' | tr '\n' ' ')"'","limit":3}}'
        response=$(timeout 1 echo "$request" | nc -U -N "$SOCKET_PATH" 2>/dev/null || true)

        if [[ -n "$response" ]]; then
            patterns=$(echo "$response" | jq -r '.result.metadata.patterns // []' 2>/dev/null)

            if [[ "$patterns" != "[]" && -n "$patterns" ]]; then
                while read -r pattern; do
                    [[ -z "$pattern" ]] && continue

                    freq=$(echo "$pattern" | jq -r '.frequency // 0' 2>/dev/null)
                    success=$(echo "$pattern" | jq -r '.success_count // 0' 2>/dev/null)
                    action=$(echo "$pattern" | jq -r '.action // ""' 2>/dev/null)

                    if [[ "$freq" -gt 2 || "$success" -gt 0 ]] && [[ -n "$action" ]]; then
                        ANTICIPATIONS="${ANTICIPATIONS}[anticipate] ${action}
"
                    fi
                done <<< "$(echo "$patterns" | jq -c '.[]' 2>/dev/null)"
            fi
        fi
    fi
fi

# ===========================================
# HABITS: Surface strong habits matching context
# ===========================================
HABITS_OUTPUT=""
if [[ -S "$SOCKET_PATH" ]]; then
    # Build context from query for habit matching
    context=$(echo "$QUERY" | head -c 100 | tr '\n' ' ' | sed 's/"/\\"/g')
    request='{"jsonrpc":"2.0","id":6,"method":"habit_match","params":{"context":"'"$context"'","min_strength":0.7}}'
    response=$(timeout 1 echo "$request" | nc -U -N "$SOCKET_PATH" 2>/dev/null || true)

    if [[ -n "$response" ]]; then
        habits_array=$(echo "$response" | jq -c '.result.metadata.habits // []' 2>/dev/null)
        if [[ "$habits_array" != "[]" && -n "$habits_array" ]]; then
            while read -r habit; do
                [[ -z "$habit" ]] && continue
                response_text=$(echo "$habit" | jq -r '.response // ""' 2>/dev/null)
                strength=$(echo "$habit" | jq -r '.strength // 0' 2>/dev/null)
                if [[ -n "$response_text" && "$response_text" != "null" ]]; then
                    strength_pct=$(awk "BEGIN {printf \"%.0f\", $strength * 100}")
                    HABITS_OUTPUT="${HABITS_OUTPUT}[habit:${strength_pct}%] ${response_text}
"
                fi
            done <<< "$(echo "$habits_array" | jq -c '.[]' 2>/dev/null)"
        fi
    fi
fi

# ===========================================
# GOALS: Surface active goals for context
# ===========================================
GOALS_OUTPUT=""
if [[ -S "$SOCKET_PATH" ]]; then
    request='{"jsonrpc":"2.0","id":7,"method":"goal_list","params":{"status":"active","limit":3}}'
    response=$(timeout 1 echo "$request" | nc -U -N "$SOCKET_PATH" 2>/dev/null || true)
    if [[ -n "$response" ]]; then
        goals_array=$(echo "$response" | jq -c '.result.metadata.goals // []' 2>/dev/null)
        if [[ "$goals_array" != "[]" && -n "$goals_array" ]]; then
            while read -r goal; do
                [[ -z "$goal" ]] && continue
                id=$(echo "$goal" | jq -r '.id // ""' 2>/dev/null)
                title=$(echo "$goal" | jq -r '.title // ""' 2>/dev/null)
                progress=$(echo "$goal" | jq -r '.progress // 0' 2>/dev/null)
                if [[ -n "$title" && "$title" != "null" ]]; then
                    progress_pct=$(awk "BEGIN {printf \"%.0f\", $progress * 100}")
                    GOALS_OUTPUT="${GOALS_OUTPUT}[goal:${id}] ${title} (${progress_pct}%)
"
                fi
            done <<< "$(echo "$goals_array" | jq -c '.[]' 2>/dev/null)"
        fi
    fi
fi

# ===========================================
# CURIOSITY: Surface unresolved knowledge gaps (once per session)
# ===========================================
CURIOSITY_OUTPUT=""
if [[ ! -f "$MIND_PATH/.gaps_surfaced" && -S "$SOCKET_PATH" ]]; then
    touch "$MIND_PATH/.gaps_surfaced"
    request='{"jsonrpc":"2.0","id":8,"method":"curiosity_gaps","params":{"limit":1}}'
    response=$(timeout 1 echo "$request" | nc -U -N "$SOCKET_PATH" 2>/dev/null || true)
    if [[ -n "$response" ]]; then
        gaps_array=$(echo "$response" | jq -c '.result.metadata.gaps // []' 2>/dev/null)
        if [[ "$gaps_array" != "[]" && -n "$gaps_array" ]]; then
            while read -r gap; do
                [[ -z "$gap" ]] && continue
                content=$(echo "$gap" | jq -r '.content // ""' 2>/dev/null)
                if [[ -n "$content" && "$content" != "null" ]]; then
                    # Extract just the gap text, strip [gap] prefix if present
                    gap_text=$(echo "$content" | sed 's/^\[gap\] //' | head -c 150 | tr '\n' ' ')
                    CURIOSITY_OUTPUT="[curiosity] Unresolved: ${gap_text}"
                    break  # Only show one gap per session
                fi
            done <<< "$(echo "$gaps_array" | jq -c '.[]' 2>/dev/null)"
        fi
    fi
fi

# ===========================================
# SESSION CONTINUITY: Surface last session for context
# ===========================================
if [[ ! -f "$MIND_PATH/.session_active" ]]; then
    touch "$MIND_PATH/.session_active"
    # Surface last session summary for continuity
    recent_session=$(timeout 1 "$CHITTA_BIN" recall --query "session_summary" --limit 1 2>/dev/null || true)
    if [[ -n "$recent_session" && "$recent_session" != *"No memories"* ]]; then
        # Extract just the first relevant line
        session_line=$(echo "$recent_session" | grep -v '^$' | head -1 | head -c 150)
        [[ -n "$session_line" ]] && ANTICIPATIONS="${ANTICIPATIONS}[last-session] ${session_line}
"
    fi
fi

# ===========================================
# CROSS-SESSION MESSAGING: Heartbeat and inbox check
# ===========================================
CROSS_SESSION_MSGS=""
# Refresh session ID for messaging (in case it wasn't available at script start)
MSG_SESSION_ID=$(get_session_id)
[[ -z "$MSG_SESSION_ID" ]] && MSG_SESSION_ID="$SESSION_ID"
if [[ -n "$MSG_SESSION_ID" && "$MSG_SESSION_ID" != "default" && -S "$SOCKET_PATH" ]]; then
    # Session heartbeat
    request='{"jsonrpc":"2.0","id":10,"method":"session_heartbeat","params":{"session_id":"'"$MSG_SESSION_ID"'"}}'
    timeout 0.3 echo "$request" | nc -U "$SOCKET_PATH" >/dev/null 2>&1 || true

    # Check for cross-session messages
    request='{"jsonrpc":"2.0","id":11,"method":"msg_inbox","params":{"session_id":"'"$MSG_SESSION_ID"'","limit":3,"min_priority":1,"auto_ack":true}}'
    response=$(timeout 1 echo "$request" | nc -U "$SOCKET_PATH" 2>/dev/null || true)
    if [[ -n "$response" ]]; then
        msg_count=$(echo "$response" | jq -r '.result.count // 0' 2>/dev/null)
        if [[ "$msg_count" -gt 0 ]]; then
            # Format messages based on priority:
            # priority 3 = [MSG:URGENT:realm], priority 2 = [MSG:important:realm], else = [msg:realm]
            CROSS_SESSION_MSGS=$(echo "$response" | jq -r '.result.messages[] |
                if .priority == 3 then "[MSG:URGENT:\(.sender_realm)] \(.content | .[0:150])"
                elif .priority == 2 then "[MSG:important:\(.sender_realm)] \(.content | .[0:150])"
                else "[msg:\(.sender_realm)] \(.content | .[0:150])"
                end' 2>/dev/null || true)
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

# Narrative status (work mode context)
if [[ -n "$NARRATIVE_STATUS" ]]; then
    echo "$NARRATIVE_STATUS"
fi

# Goals (active objectives)
if [[ -n "$GOALS_OUTPUT" ]]; then
    echo -n "$GOALS_OUTPUT"
fi

# Curiosity gaps (unresolved knowledge)
if [[ -n "$CURIOSITY_OUTPUT" ]]; then
    echo "$CURIOSITY_OUTPUT"
fi

# Cross-session messages
if [[ -n "$CROSS_SESSION_MSGS" ]]; then
    echo "[cross-session messages]"
    echo "$CROSS_SESSION_MSGS"
    echo "[/cross-session messages]"
fi

# Habits (strong patterns)
if [[ -n "$HABITS_OUTPUT" ]]; then
    echo -n "$HABITS_OUTPUT"
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
