#!/bin/bash
# Stop hook: Extract typed learnings, handle feedback, checkpoint
#
# HIGH PERFORMANCE: Uses queue for write ops (no blocking)
# STORES IN SSL FORMAT for better recall
#
# Learning types extracted:
#   [SOLUTION], [GOTCHA], [PREFERENCE], [DECISION], [FAILURE], [PATTERN], [LEARN]

set -e

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
QUEUE_FILE="${CHITTA_QUEUE:-/tmp/chitta-queue.jsonl}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-2}"
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"

# Parse JSON input
INPUT=$(cat)
TRANSCRIPT_PATH=$(echo "$INPUT" | jq -r '.transcript_path // empty')
STOP_HOOK_ACTIVE=$(echo "$INPUT" | jq -r '.stop_hook_active // false')
SESSION_ID_INPUT=$(echo "$INPUT" | jq -r '.session_id // empty')

# Prevent infinite loops
[[ "$STOP_HOOK_ACTIVE" == "true" ]] && exit 0
[[ -z "$TRANSCRIPT_PATH" || ! -f "$TRANSCRIPT_PATH" ]] && exit 0

# Queue write - fire and forget (~1ms)
queue_write() {
    local tool="$1" args="$2"
    echo "{\"tool\":\"$tool\",\"args\":$args,\"ts\":$(date +%s)}" >> "$QUEUE_FILE"
}

# Convert to SSL format
# Input: category, raw content
# Output: SSL formatted string
to_ssl() {
    local category="$1"
    local content="$2"

    # Detect realm/domain from content or use default
    local domain="partnership"
    if echo "$content" | grep -qiE '(code|function|class|file|build|compile)'; then
        domain="code"
    elif echo "$content" | grep -qiE '(hook|daemon|queue|rpc|socket)'; then
        domain="cc-soul"
    fi

    # Extract key parts using → notation
    # Try to find pattern: subject verb/action object/result
    local ssl_content

    case "$category" in
        solution)
            # [domain] problem→solution @location
            ssl_content="[$domain:sol] $content"
            ;;
        gotcha)
            # [domain] trap→consequence
            ssl_content="[$domain:gotcha] $content"
            ;;
        preference)
            # [partnership] user→prefers→X
            ssl_content="[partnership:pref] Antonio→$content"
            ;;
        decision)
            # [domain] chose→X over Y→because Z
            ssl_content="[$domain:dec] $content"
            ;;
        failure)
            # [domain] tried→X→failed because Y
            ssl_content="[$domain:fail] $content"
            ;;
        pattern)
            # [domain] when X→do Y
            ssl_content="[$domain:pat] $content"
            ;;
        *)
            ssl_content="[$domain] $content"
            ;;
    esac

    echo "$ssl_content"
}

# Map learning type to category
map_category() {
    case "$1" in
        SOLUTION) echo "solution" ;;
        GOTCHA) echo "gotcha" ;;
        PREFERENCE) echo "preference" ;;
        DECISION) echo "decision" ;;
        FAILURE) echo "failure" ;;
        PATTERN) echo "pattern" ;;
        *) echo "wisdom" ;;
    esac
}

# Extract last assistant message
RESPONSE=$(tac "$TRANSCRIPT_PATH" | grep -m1 '"role":"assistant"' | \
    jq -r '.message.content[] | select(.type=="text") | .text' 2>/dev/null | head -c 10000)

[[ -z "$RESPONSE" || ${#RESPONSE} -lt 10 ]] && exit 0

# Detect realm (quick CLI call with short timeout)
REALM=$(timeout "$MAX_WAIT" "$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")

# Extract typed learnings → convert to SSL → queue
LEARNED=0
while IFS= read -r line; do
    if [[ "$line" =~ ^\[(SOLUTION|GOTCHA|PREFERENCE|DECISION|FAILURE|PATTERN|LEARN)\] ]]; then
        type="${BASH_REMATCH[1]}"
        raw_content="${line#\[$type\] }"
        category=$(map_category "$type")

        # Convert to SSL format
        ssl_content=$(to_ssl "$category" "$raw_content")
        title=$(echo "$ssl_content" | head -c 100)

        # Queue observe with SSL-formatted content
        queue_write "observe" "{\"category\":\"$category\",\"title\":$(echo "$title" | jq -Rs .),\"content\":$(echo "$ssl_content" | jq -Rs .)}"
        echo "[soul] +${type,,}: ${title:0:60}" >&2
        ((LEARNED++)) || true
    fi
done <<< "$RESPONSE"

# Extract [USED:id] feedback → queue strengthen + learn_outcome
# Accepts both numeric IDs and UUID-like strings
while IFS= read -r marker; do
    [[ -z "$marker" ]] && continue
    mem_id="${marker#\[USED:}"
    mem_id="${mem_id%\]}"
    [[ -z "$mem_id" ]] && continue

    # Strengthen the memory (existing behavior)
    queue_write "strengthen" "{\"id\":\"$mem_id\",\"amount\":0.1}"
    # Record positive usage outcome (closes feedback loop)
    queue_write "learn_outcome" "{\"memory-id\":\"$mem_id\",\"outcome\":\"positive\",\"context\":\"Memory explicitly marked as helpful via [USED] marker\"}"
    echo "[soul] ↑+ ${mem_id:0:12}..." >&2
done <<< "$(echo "$RESPONSE" | grep -oE '\[USED:[a-zA-Z0-9_-]+\]')"

# ===========================================
# CURIOSITY GAPS: Detect uncertainty/knowledge gaps
# ===========================================
if echo "$RESPONSE" | grep -qiE "(I don.?t know|I.?m not sure|unclear|couldn.?t (find|determine)|need to check|I.?ll have to look)"; then
    gap_context=$(echo "$RESPONSE" | grep -iE "(I don.?t know|I.?m not sure|unclear|couldn.?t)" | head -1 | head -c 200 | tr '\n' ' ' | sed 's/"/\\"/g')
    [[ -n "$gap_context" ]] && queue_write "curiosity_note_gap" "{\"gap\":$(echo "$gap_context" | jq -Rs .)}"
    echo "[soul] +curiosity-gap detected" >&2
fi

# Extract [TRIPLET] → queue connect
while IFS= read -r line; do
    if [[ "$line" =~ ^\[TRIPLET\] ]]; then
        triplet="${line#\[TRIPLET\] }"
        subj=$(echo "$triplet" | awk '{print $1}')
        pred=$(echo "$triplet" | awk '{print $2}')
        obj=$(echo "$triplet" | awk '{print $3}')
        [[ -n "$subj" && -n "$pred" && -n "$obj" ]] && \
            queue_write "connect" "{\"subject\":\"$subj\",\"predicate\":\"$pred\",\"object\":\"$obj\"}"
    fi
done <<< "$RESPONSE"

# ===========================================
# AUTO-LEARNING: Detect missed learning opportunities
# ===========================================
# Read user's last message (saved by UserPromptSubmit)
LAST_USER_MSG=""
if [[ -f "$MIND_PATH/.last_user_message" ]]; then
    LAST_USER_MSG=$(cat "$MIND_PATH/.last_user_message" 2>/dev/null)
fi

# Check if Claude used a learn_* tool (indicated by tool output patterns)
CLAUDE_LEARNED=false
if echo "$RESPONSE" | grep -qiE '(learn_correction|learn_preference|learn_insight|learn_approach|learn_outcome|learn_milestone|Stored correction|Stored preference|Stored insight|Stored approach|Stored outcome|Stored milestone)'; then
    CLAUDE_LEARNED=true
fi

# If user correction detected but Claude didn't learn, auto-store
if [[ "$CLAUDE_LEARNED" == "false" && -n "$LAST_USER_MSG" ]]; then
    # ===========================================
    # COMPLIANCE TRACKING: Did Claude learn when prompted?
    # ===========================================
    # Check if prompt-hook detected a correction opportunity
    CORRECTION_DETECTED=false
    if echo "$LAST_USER_MSG" | grep -qiE "(wrong|mistake|not working|incorrect|actually[, ]|that'?s not|you('re| are) (wrong|missing)|not what I|won'?t work|should be|use your memory|check.*memory|did you forget)"; then
        CORRECTION_DETECTED=true
    fi

    # If correction was detected but Claude didn't call learn_correction, log compliance failure
    if [[ "$CORRECTION_DETECTED" == "true" && "$CLAUDE_LEARNED" == "false" ]]; then
        # Store compliance failure for tracking
        failure_context=$(echo "$LAST_USER_MSG" | head -c 100 | tr '\n' ' ')
        queue_write "observe" "{\"category\":\"compliance\",\"title\":\"Missed correction\",\"content\":$(echo "[compliance:fail] Correction detected but learn_correction not called: $failure_context" | jq -Rs .)}"
        echo "[soul] ⚠️ COMPLIANCE FAIL: Correction detected but learn_correction not called" >&2
    fi

    # Direct + meta-preference patterns
    if echo "$LAST_USER_MSG" | grep -qiE "(I (prefer|like|want|always|never)|please (don'?t|always|never)|from now on|more concise|fewer examples|go deeper|simpler please|don'?t overexplain|be more verbose)"; then
        pref_context=$(echo "$LAST_USER_MSG" | head -c 200 | tr '\n' ' ')

        # Format as SSL preference
        content="[preference] Antonio→$pref_context"

        # Queue the learning
        queue_write "observe" "{\"category\":\"preference\",\"title\":\"Auto-preference\",\"content\":$(echo "$content" | jq -Rs .)}"
        echo "[soul] +auto-preference: ${pref_context:0:60}" >&2
        ((LEARNED++)) || true
    fi

    # If milestone detected, auto-store
    if echo "$LAST_USER_MSG" | grep -qiE "(it works|finally|success|shipped|released|completed|finished|passed|merged|deployed)"; then
        milestone_context=$(echo "$LAST_USER_MSG" | head -c 100 | tr '\n' ' ')

        # Format as SSL milestone
        content="[milestone] $milestone_context"

        # Queue the learning
        queue_write "observe" "{\"category\":\"milestone\",\"title\":\"Auto-milestone\",\"content\":$(echo "$content" | jq -Rs .)}"
        echo "[soul] +auto-milestone: ${milestone_context:0:60}" >&2
        ((LEARNED++)) || true
    fi
fi

# ===========================================
# NARRATIVE EVENT LOGGING: Log assistant response and tool uses
# ===========================================
SESSION_ID="${SESSION_ID_INPUT:-default}"

# djb2 hash - must match C++ implementation
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
SOCKET_PATH="${CHITTA_SOCKET:-/tmp/chitta-${MIND_HASH}.sock}"

# Log assistant_message event (first line of response)
if [[ -S "$SOCKET_PATH" && -n "$RESPONSE" ]]; then
    summary=$(echo "$RESPONSE" | grep -v '^$' | grep -v '^\[' | head -1 | head -c 200 | tr '\n' ' ' | sed 's/"/\\"/g')
    [[ -n "$summary" ]] && queue_write "narrative_log" "{\"session_id\":\"$SESSION_ID\",\"kind\":\"assistant_message\",\"summary\":$(echo "$summary" | jq -Rs .)}"

    # Extract and log tool_use events (last 10 unique tools)
    TOOLS_FROM_TRANSCRIPT=$(jq -r '.[] | select(.role=="assistant") | .message.content[]? | select(.type=="tool_use") | .name' "$TRANSCRIPT_PATH" 2>/dev/null | tail -10 | sort -u)

    while IFS= read -r tool; do
        [[ -z "$tool" ]] && continue
        queue_write "narrative_log" "{\"session_id\":\"$SESSION_ID\",\"kind\":\"tool_use\",\"summary\":\"Used $tool\",\"tool_name\":\"$tool\",\"success\":true}"
    done <<< "$TOOLS_FROM_TRANSCRIPT"

    # Log error events if response contains error indicators
    if echo "$RESPONSE" | grep -qiE '(error|failed|exception|traceback|fatal)'; then
        error_line=$(echo "$RESPONSE" | grep -iE '(error|failed|exception)' | head -1 | head -c 150 | tr '\n' ' ' | sed 's/"/\\"/g')
        [[ -n "$error_line" ]] && queue_write "narrative_log" "{\"session_id\":\"$SESSION_ID\",\"kind\":\"error\",\"summary\":$(echo "$error_line" | jq -Rs .),\"success\":false}"
    fi
fi

# ===========================================
# ANTICIPATION OUTCOME: Track prediction correctness
# ===========================================
PREDICTIONS_FILE="$MIND_PATH/.last_predictions.json"

if [[ -f "$PREDICTIONS_FILE" ]]; then
    # Extract tool usage from transcript (tool names from assistant's actions)
    TOOLS_USED=$(jq -r '.[] | select(.role=="assistant") | .message.content[]? | select(.type=="tool_use") | .name' "$TRANSCRIPT_PATH" 2>/dev/null | tail -10)

    if [[ -n "$TOOLS_USED" ]]; then
        # Read predictions
        predictions=$(cat "$PREDICTIONS_FILE" 2>/dev/null)

        if [[ -n "$predictions" && "$predictions" != "[]" ]]; then
            # For each prediction, check if the action matches any tool used
            while read -r candidate; do
                [[ -z "$candidate" ]] && continue

                candidate_id=$(echo "$candidate" | jq -r '.id // 0' 2>/dev/null)
                prediction=$(echo "$candidate" | jq -r '.prediction // ""' 2>/dev/null)

                [[ "$candidate_id" -eq 0 || -z "$prediction" ]] && continue

                # Check if prediction matches any tool used (fuzzy match)
                match_found=false
                while IFS= read -r tool; do
                    [[ -z "$tool" ]] && continue

                    prediction_lower=$(echo "$prediction" | tr '[:upper:]' '[:lower:]')
                    tool_lower=$(echo "$tool" | tr '[:upper:]' '[:lower:]')

                    # Match keywords: edit, test, build, commit, etc.
                    if [[ "$prediction_lower" == *"edit"* && "$tool_lower" == *"edit"* ]] ||
                       [[ "$prediction_lower" == *"test"* && ("$tool_lower" == *"test"* || "$tool_lower" == *"bash"*) ]] ||
                       [[ "$prediction_lower" == *"build"* && "$tool_lower" == *"bash"* ]] ||
                       [[ "$prediction_lower" == *"commit"* && "$tool_lower" == *"bash"* ]] ||
                       [[ "$prediction_lower" == *"$tool_lower"* || "$tool_lower" == *"$prediction_lower"* ]]; then
                        match_found=true
                        break
                    fi
                done <<< "$TOOLS_USED"

                if [[ "$match_found" == "true" ]]; then
                    # Record correct outcome via new anticipation_record_outcome
                    queue_write "anticipation_record_outcome" "{\"candidate_id\":$candidate_id,\"correct\":true}"
                    # Feed calibration
                    queue_write "calibration_record" "{\"domain\":\"anticipation\",\"success\":$match_found}"
                    echo "[soul] ✓ prediction #${candidate_id}: ${prediction:0:40}" >&2
                fi
            done <<< "$(echo "$predictions" | jq -c '.[]' 2>/dev/null)"
        fi
    fi
fi

# ===========================================
# STRUCTURED SPANS: Capture tool uses with outcomes
# ===========================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -x "$SCRIPT_DIR/span-capture.sh" ]]; then
    "$SCRIPT_DIR/span-capture.sh" "$TRANSCRIPT_PATH" "$LAST_USER_MSG" 2>&1 || true
fi

# Clean up temp files
rm -f "$MIND_PATH/.last_user_message" "$PREDICTIONS_FILE" 2>/dev/null

# Ledger save → queue (fire-and-forget)
SESSION_ID="${SESSION_ID_INPUT:-auto-$(date +%Y%m%d-%H%M%S)}"

# Detect mood
if echo "$RESPONSE" | grep -qiE '(error|failed|bug|stuck)'; then
    mood="debugging"
elif echo "$RESPONSE" | grep -qiE '(complete|done|finished|success)'; then
    mood="confident"
elif [[ $LEARNED -gt 0 ]]; then
    mood="learning"
else
    mood="working"
fi

snapshot=$(echo "$RESPONSE" | grep -v '^$' | grep -v '^\[' | head -1 | head -c 200)
[[ -z "$snapshot" ]] && snapshot=$(echo "$RESPONSE" | head -1 | head -c 200)

# ===========================================
# SESSION SUMMARY: Capture narrative for continuity
# ===========================================
# Extract key actions from transcript (tool uses + outcomes)
TOOLS_USED=$(jq -r '[.[] | select(.role=="assistant") | .message.content[]? | select(.type=="tool_use") | .name] | unique | join(", ")' "$TRANSCRIPT_PATH" 2>/dev/null | head -c 200)
TURNS=$(jq '[.[] | select(.role=="assistant")] | length' "$TRANSCRIPT_PATH" 2>/dev/null || echo "?")

# Build SSL summary
SUMMARY="[session:$SESSION_ID] ${mood}→${TURNS} turns"
[[ -n "$TOOLS_USED" ]] && SUMMARY="$SUMMARY | tools: ${TOOLS_USED:0:100}"
[[ -n "$snapshot" ]] && SUMMARY="$SUMMARY | ${snapshot:0:100}"

# Queue session summary memory
queue_write "observe" "{\"category\":\"session_summary\",\"title\":\"Session $SESSION_ID\",\"content\":$(echo "$SUMMARY" | jq -Rs .)}"
echo "[soul] +session-summary: ${SUMMARY:0:60}" >&2

queue_write "ledger_save" "{\"session_id\":\"$SESSION_ID\",\"project\":\"$REALM\",\"mood\":\"$mood\",\"snapshot\":$(echo "$snapshot" | jq -Rs .)}"
echo "[ledger] queued: $SESSION_ID ($mood, +$LEARNED)" >&2

exit 0
