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

# Parse input - Claude Code sends JSON with session_id and prompt (gracefully handle malformed input)
INPUT=$(cat)
# Try to extract session_id from JSON first (most reliable source)
SESSION_ID=$(echo "$INPUT" | jq -r '.session_id // empty' 2>/dev/null || echo "")
# Fall back to registry lookup if not in JSON
[[ -z "$SESSION_ID" ]] && SESSION_ID=$(get_session_id)
[[ -z "$SESSION_ID" ]] && SESSION_ID="default"

# Try to extract prompt from JSON, fall back to raw input if not JSON
QUERY=$(echo "$INPUT" | jq -r '.prompt // empty' 2>/dev/null || echo "")
[[ -z "$QUERY" ]] && QUERY="$INPUT"
TRANSCRIPT_PATH=$(echo "$INPUT" | jq -r '.transcript_path // empty' 2>/dev/null || echo "")

[[ -z "$QUERY" ]] && exit 0
[[ ! -x "$CHITTA_BIN" ]] && exit 0

# Save user message for Stop hook to analyze
mkdir -p "$MIND_PATH"
echo "$QUERY" > "$MIND_PATH/.last_user_message"

# Get turn index from counter file
TURN_FILE="$MIND_PATH/.turn_index_$SESSION_ID"
TURN_INDEX=$(cat "$TURN_FILE" 2>/dev/null || echo "0")

# Store user turn in lossless conversation storage (uses lib.sh queue_write with ack_id)
queue_write "store_turn" "{\"session_id\":\"$SESSION_ID\",\"role\":\"user\",\"content\":$(echo "$QUERY" | jq -Rs .),\"turn_index\":$TURN_INDEX}"

# Increment turn index
echo $((TURN_INDEX + 1)) > "$TURN_FILE"

# ===========================================
# TURN-BASED CHECKPOINT: Save ledger every N turns
# ===========================================
CHECKPOINT_INTERVAL="${CC_SOUL_CHECKPOINT_INTERVAL:-10}"
if [[ $((TURN_INDEX % CHECKPOINT_INTERVAL)) -eq 0 && $TURN_INDEX -gt 0 ]]; then
    # Detect realm for project
    REALM=$(timeout 1 "$CHITTA_BIN" realm_detect 2>/dev/null | grep -oP 'realm": "\K[^"]+' || echo "brahman")

    CHECKPOINT_ARGS=$(jq -n \
        --arg session_id "$SESSION_ID" \
        --arg project "$REALM" \
        --arg transcript_path "$TRANSCRIPT_PATH" \
        --arg mood "working" \
        --arg snapshot "Turn $TURN_INDEX checkpoint" \
        '{session_id: $session_id, project: $project, transcript_path: $transcript_path, mood: $mood, snapshot: $snapshot}')

    queue_write "ledger_save" "$CHECKPOINT_ARGS"
    echo "[ledger] checkpoint at turn $TURN_INDEX" >&2
fi

# ===========================================
# MEMORY RETRIEVAL: Choose strategy based on mode
# ===========================================
# RLM mode: Use soul_repl for dynamic exploration (more powerful but slower)
# Standard mode: Use smart_recall for fast retrieval
RLM_MODE="${CC_SOUL_RLM_MODE:-}"

if [[ -n "$RLM_MODE" ]]; then
    # RLM-style exploration via Python soul_repl
    memories=$(timeout "$MAX_WAIT" python3 -c "
import sys
sys.path.insert(0, '${SCRIPT_DIR}/../chitta-mcp')
from server import handle_smart_context
result = handle_smart_context({'mode': 'rlm', 'task': '''$QUERY'''})
print(result)
" 2>/dev/null || true)
else
    # Standard: smart_recall for intelligent query routing
    # - Automatically classifies query intent (temporal, aspect, entity, code, etc.)
    # - Routes to optimal retrieval strategy
    memories=$(timeout "$MAX_WAIT" "$CHITTA_BIN" smart_recall --query "$QUERY" --limit 6 2>/dev/null || true)
fi

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
# SUS: Log memory exposures for utility scoring (fire-and-forget)
# ===========================================
(
    _sus_ids="["
    _sus_ranks="["
    _sus_scores="["
    _sus_first=true
    _sus_rank=0

    while IFS= read -r _sus_line; do
        [[ -z "$_sus_line" ]] && continue
        [[ ! "$_sus_line" =~ \[[0-9]+%\] ]] && continue

        # Extract memory ID: #NNN at start of line
        if [[ "$_sus_line" =~ ^#([0-9]+) ]]; then
            _sus_mid="${BASH_REMATCH[1]}"
        else
            continue
        fi

        # Extract confidence percentage
        _sus_pct=$(echo "$_sus_line" | grep -oE '\[[0-9]+%\]' | head -1 | tr -d '[]%')
        [[ -z "$_sus_pct" ]] && continue
        [[ "$_sus_pct" -lt "$MIN_CONFIDENCE" ]] && continue

        (( _sus_rank++ ))
        if [[ "$_sus_first" == "true" ]]; then
            _sus_first=false
        else
            _sus_ids+=","
            _sus_ranks+=","
            _sus_scores+=","
        fi
        _sus_ids+="$_sus_mid"
        _sus_ranks+="$_sus_rank"
        _sus_scores+="$(awk "BEGIN{printf \"%.2f\", $_sus_pct/100}")"

        [[ $_sus_rank -ge 3 ]] && break
    done <<< "$memories"

    _sus_ids+="]"
    _sus_ranks+="]"
    _sus_scores+="]"

    if [[ "$_sus_ids" != "[]" && -n "$SESSION_ID" ]]; then
        queue_write "log_exposure" "{\"session_id\":\"$SESSION_ID\",\"turn_id\":$TURN_INDEX,\"hook_type\":\"user_prompt\",\"memory_ids\":$_sus_ids,\"ranks\":$_sus_ranks,\"resonance_scores\":$_sus_scores}"
    fi
) 2>/dev/null || true

# ===========================================
# PATTERN DETECTION: Detect learning opportunities
# ===========================================
LEARNING_HINTS=""

# Detect CORRECTION patterns: user is correcting Claude
# Direct: "wrong", "mistake", "not working", "incorrect"
# Implicit: "actually", "should be", "not what I"
if echo "$QUERY" | grep -qiE "(wrong|mistake|not working|incorrect|actually[, ]|that'?s not|you('re| are) (wrong|missing)|I (said|meant|asked)|not what I|won'?t work|should be|not quite|use your memory|check.*memory|did you forget)"; then
    # Truncate to first 200 chars for context, escape for output
    correction_ctx=$(echo "$QUERY" | head -c 200 | tr '\n' ' ')
    LEARNING_HINTS="[LEARN] ⚠️ CORRECTION detected - call learn_correction NOW
  User said: \"${correction_ctx}\""
    # Signal for stop-hook: save correction context to temp file
    echo "$QUERY" > "$MIND_PATH/.last_correction_context"
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

# Log user_message event via CLI (fire-and-forget)
# First, ensure gate is initialized for this session
timeout 0.5 "$CHITTA_BIN" gate_init --session_id "$SESSION_ID" >/dev/null 2>&1 || true

# Log the user message event
summary=$(echo "$QUERY" | head -c 200 | tr '\n' ' ')
timeout 0.5 "$CHITTA_BIN" narrative_log --session_id "$SESSION_ID" --kind "user_message" --summary "$summary" >/dev/null 2>&1 || true

# Get narrative status
response=$(timeout 1 "$CHITTA_BIN" narrative_status --session_id "$SESSION_ID" --json 2>/dev/null || true)

if [[ -n "$response" ]]; then
    mode=$(echo "$response" | jq -r '.metadata.mode // "unknown"' 2>/dev/null)
    confidence=$(echo "$response" | jq -r '.metadata.confidence // 0' 2>/dev/null)
    if [[ "$mode" != "unknown" && "$mode" != "null" ]]; then
        # Format as percentage
        conf_pct=$(awk "BEGIN {printf \"%.0f\", $confidence * 100}")
        NARRATIVE_STATUS="[narrative:$mode:$conf_pct%]"
    fi
fi

# ===========================================
# ANTICIPATION: Predict likely next actions (using new anticipation_filter)
# ===========================================
ANTICIPATIONS=""
PREDICTIONS_FILE="$MIND_PATH/.last_predictions.json"

# Clear old predictions
rm -f "$PREDICTIONS_FILE" 2>/dev/null

# Call anticipation_filter via CLI to get gated predictions
response=$(timeout 1 "$CHITTA_BIN" anticipation_filter --session_id "$SESSION_ID" --max 3 --json 2>/dev/null || true)

if [[ -n "$response" ]]; then
    candidates=$(echo "$response" | jq -r '.metadata.candidates // []' 2>/dev/null)

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
    context=$(echo "$QUERY" | tr '\n' ' ')
    response=$(timeout 1 "$CHITTA_BIN" anticipation_predict --context "$context" --limit 3 --json 2>/dev/null || true)

    if [[ -n "$response" ]]; then
        patterns=$(echo "$response" | jq -r '.metadata.patterns // []' 2>/dev/null)

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

# ===========================================
# HABITS: Surface strong habits matching context
# ===========================================
HABITS_OUTPUT=""
# Build context from query for habit matching
context=$(echo "$QUERY" | head -c 100 | tr '\n' ' ')
response=$(timeout 1 "$CHITTA_BIN" habit_match --context "$context" --min_strength 0.7 --json 2>/dev/null || true)

if [[ -n "$response" ]]; then
    habits_array=$(echo "$response" | jq -c '.metadata.habits // []' 2>/dev/null)
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

# ===========================================
# GOALS: Surface active goals for context
# ===========================================
GOALS_OUTPUT=""
response=$(timeout 1 "$CHITTA_BIN" goal_list --status "active" --limit 3 --json 2>/dev/null || true)
if [[ -n "$response" ]]; then
    goals_array=$(echo "$response" | jq -c '.metadata.goals // []' 2>/dev/null)
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

# ===========================================
# CURIOSITY: Surface unresolved knowledge gaps (once per session)
# ===========================================
CURIOSITY_OUTPUT=""
if [[ ! -f "$MIND_PATH/.gaps_surfaced" ]]; then
    touch "$MIND_PATH/.gaps_surfaced"
    response=$(timeout 1 "$CHITTA_BIN" curiosity_gaps --limit 1 --json 2>/dev/null || true)
    if [[ -n "$response" ]]; then
        gaps_array=$(echo "$response" | jq -c '.metadata.gaps // []' 2>/dev/null)
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
# Use SESSION_ID from JSON input (already extracted above) - most reliable source
MSG_SESSION_ID="$SESSION_ID"
if [[ -n "$MSG_SESSION_ID" && "$MSG_SESSION_ID" != "default" ]]; then
    # Session register (upserts PID on each prompt - handles Claude restarts/resumes)
    # PPID is Claude's PID (hook runs as: Claude → bash → hook script)
    CLAUDE_PID=${PPID:-$$}
    timeout 0.3 "$CHITTA_BIN" session_register --session_id "$MSG_SESSION_ID" --realm "${REALM:-brahman}" --pid "$CLAUDE_PID" >/dev/null 2>&1 || true

    # Check for cross-session messages
    response=$(timeout 1 "$CHITTA_BIN" msg_inbox --session_id "$MSG_SESSION_ID" --limit 3 --min_priority 1 --auto_ack --json 2>/dev/null || true)
    if [[ -n "$response" ]]; then
        msg_count=$(echo "$response" | jq -r '.count // 0' 2>/dev/null)
        if [[ "$msg_count" -gt 0 ]]; then
            # Format messages based on priority:
            # priority 3 = [MSG:URGENT:realm], priority 2 = [MSG:important:realm], else = [msg:realm]
            CROSS_SESSION_MSGS=$(echo "$response" | jq -r '.messages[] |
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
