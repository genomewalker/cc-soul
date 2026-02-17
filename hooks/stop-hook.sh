#!/bin/bash
# Stop hook: Extract typed learnings, handle feedback, checkpoint
#
# HIGH PERFORMANCE: Uses queue for write ops (no blocking)
# STORES IN SSL FORMAT for better recall
#
# Learning types extracted:
#   [SOLUTION], [GOTCHA], [PREFERENCE], [DECISION], [FAILURE], [PATTERN], [LEARN]

# Don't use set -e: we want hooks to succeed even if some parts fail

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-2}"
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"

# Source shared library (provides queue_write with ack_id, get_queue_file, etc.)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/lib.sh"

# Parse JSON input (gracefully handle malformed input)
INPUT=$(cat)
TRANSCRIPT_PATH=$(echo "$INPUT" | jq -r '.transcript_path // empty' 2>/dev/null || echo "")
STOP_HOOK_ACTIVE=$(echo "$INPUT" | jq -r '.stop_hook_active // false' 2>/dev/null || echo "false")
SESSION_ID_INPUT=$(echo "$INPUT" | jq -r '.session_id // empty' 2>/dev/null || echo "")

# Set SESSION_ID early (used by store_turn)
SESSION_ID="${SESSION_ID_INPUT:-default}"

# Prevent infinite loops
[[ "$STOP_HOOK_ACTIVE" == "true" ]] && exit 0
[[ -z "$TRANSCRIPT_PATH" || ! -f "$TRANSCRIPT_PATH" ]] && exit 0

# Convert to SSL format
# Input: category, raw content
# Output: SSL formatted string
# Note: Uses global $REALM set by realm_detect (line 105)
to_ssl() {
    local category="$1"
    local content="$2"

    # Use detected realm (falls back to "brahman" if unset)
    local domain="${REALM:-brahman}"

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
    jq -r '.message.content[] | select(.type=="text") | .text' 2>/dev/null | head -c 50000)

[[ -z "$RESPONSE" || ${#RESPONSE} -lt 10 ]] && exit 0

# ===========================================
# LOSSLESS STORAGE: Store assistant turn
# ===========================================
# Get turn index from counter file
TURN_FILE="$MIND_PATH/.turn_index_$SESSION_ID"
TURN_INDEX=$(cat "$TURN_FILE" 2>/dev/null || echo "1")

# Extract tools used from transcript for this turn
TOOLS_JSON=$(jq -r '[.[] | select(.role=="assistant") | .message.content[]? | select(.type=="tool_use") | .name] | unique' "$TRANSCRIPT_PATH" 2>/dev/null || echo "[]")
[[ "$TOOLS_JSON" == "null" ]] && TOOLS_JSON="[]"

# Extract files touched
FILES_JSON=$(jq -r '[.[] | select(.role=="assistant") | .message.content[]? | select(.type=="tool_use") | .input.file_path // .input.path // empty] | unique | map(select(. != ""))' "$TRANSCRIPT_PATH" 2>/dev/null || echo "[]")
[[ "$FILES_JSON" == "null" ]] && FILES_JSON="[]"

# Check for errors
HAS_ERROR=false
echo "$RESPONSE" | grep -qiE '(error|failed|exception|traceback)' && HAS_ERROR=true

# Store assistant turn
queue_write "store_turn" "{\"session_id\":\"$SESSION_ID\",\"role\":\"assistant\",\"content\":$(echo "$RESPONSE" | jq -Rs .),\"turn_index\":$TURN_INDEX,\"tools_used\":$TOOLS_JSON,\"files_touched\":$FILES_JSON,\"has_error\":$HAS_ERROR}"

# Increment turn index
echo $((TURN_INDEX + 1)) > "$TURN_FILE"

# Detect realm (quick CLI call with short timeout)
REALM=$(timeout "$MAX_WAIT" "$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")

# ===========================================
# EVENT-BASED CHECKPOINT: Save on errors or milestones
# ===========================================
EVENT_CHECKPOINT=false
EVENT_MOOD=""
EVENT_SNAPSHOT=""

# Error checkpoint
if [[ "$HAS_ERROR" == "true" ]]; then
    EVENT_CHECKPOINT=true
    EVENT_MOOD="debugging"
    EVENT_SNAPSHOT=$(echo "$RESPONSE" | grep -iE '(error|failed|exception)' | head -3 | head -c 500)
    echo "[ledger] error checkpoint triggered" >&2
fi

# Milestone checkpoint (success patterns)
if echo "$RESPONSE" | grep -qiE '(✓|✅|success|complete|done|shipped|released|merged|tests pass|build succeed)'; then
    EVENT_CHECKPOINT=true
    EVENT_MOOD="confident"
    EVENT_SNAPSHOT=$(echo "$RESPONSE" | grep -iE '(success|complete|done|shipped|released|merged|tests pass|build)' | head -3 | head -c 500)
    echo "[ledger] milestone checkpoint triggered" >&2
fi

if [[ "$EVENT_CHECKPOINT" == "true" ]]; then
    EVENT_ARGS=$(jq -n \
        --arg session_id "$SESSION_ID" \
        --arg project "$REALM" \
        --arg transcript_path "$TRANSCRIPT_PATH" \
        --arg mood "$EVENT_MOOD" \
        --arg snapshot "$EVENT_SNAPSHOT" \
        '{session_id: $session_id, project: $project, transcript_path: $transcript_path, mood: $mood, snapshot: $snapshot}')
    queue_write "ledger_save" "$EVENT_ARGS"
fi

# Quality gate: dedup file for this session
DEDUP_FILE="$MIND_PATH/.stop_dedup"
touch "$DEDUP_FILE"

# Extract typed learnings → convert to SSL → queue
LEARNED=0
while IFS= read -r line; do
    if [[ "$line" =~ ^\[(SOLUTION|GOTCHA|PREFERENCE|DECISION|FAILURE|PATTERN|LEARN)\] ]]; then
        type="${BASH_REMATCH[1]}"
        raw_content="${line#\[$type\] }"
        category=$(map_category "$type")

        # Convert to SSL format
        ssl_content=$(to_ssl "$category" "$raw_content")

        # Quality gate: minimum content length (30 chars)
        if [[ ${#ssl_content} -lt 30 ]]; then
            echo "[soul] skip ${type,,}: too short (${#ssl_content}<30)" >&2
            continue
        fi

        # Quality gate: hash-based deduplication
        content_hash=$(echo -n "$ssl_content" | md5sum | cut -d' ' -f1)
        if grep -q "^${content_hash}$" "$DEDUP_FILE" 2>/dev/null; then
            echo "[soul] skip ${type,,}: duplicate" >&2
            continue
        fi
        echo "$content_hash" >> "$DEDUP_FILE"

        title=$(echo "$ssl_content" | head -c 100)

        # Queue observe with SSL-formatted content
        queue_write "observe" "{\"category\":\"$category\",\"title\":$(echo "$title" | jq -Rs .),\"content\":$(echo "$ssl_content" | jq -Rs .)}"
        echo "[soul] +${type,,}: ${title:0:60}" >&2
        ((LEARNED++)) || true
    fi
done <<< "$RESPONSE"

# Extract [USED:id] feedback → strengthen + learn_outcome
# Accepts both numeric IDs and UUID-like strings
while IFS= read -r marker; do
    [[ -z "$marker" ]] && continue
    mem_id="${marker#\[USED:}"
    mem_id="${mem_id%\]}"
    [[ -z "$mem_id" ]] && continue

    # Strengthen the memory (existing behavior)
    queue_write "strengthen" "{\"id\":\"$mem_id\",\"amount\":0.1}"
    # CRITICAL: Record positive usage outcome with fallback (feedback loop must not silently fail)
    if ! "$CHITTA_BIN" learn_outcome --memory-id "$mem_id" --outcome "positive" --context "Memory explicitly marked as helpful via [USED] marker" 2>/dev/null; then
        echo "{\"tool\":\"learn_outcome\",\"args\":{\"memory-id\":\"$mem_id\",\"outcome\":\"positive\",\"context\":\"Memory explicitly marked as helpful via [USED] marker\"},\"ts\":$(date +%s)}" >> "$HOME/.claude/mind/.failed_observations.jsonl"
    fi
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
    CORRECTION_DETECTED=false
    CORRECTION_TEXT=""

    # 1. Authoritative signal: prompt-hook wrote .last_correction_context
    CORRECTION_CTX_FILE="$MIND_PATH/.last_correction_context"
    if [[ -f "$CORRECTION_CTX_FILE" ]]; then
        CORRECTION_DETECTED=true
        CORRECTION_TEXT=$(cat "$CORRECTION_CTX_FILE" 2>/dev/null)
        echo "[soul] correction source: prompt-hook context file" >&2
    fi

    # 2. Fallback: regex on last user message
    if [[ "$CORRECTION_DETECTED" == "false" ]]; then
        if echo "$LAST_USER_MSG" | grep -qiE "(wrong|mistake|not working|incorrect|actually[, ]|that'?s not|you('re| are) (wrong|missing)|not what I|won'?t work|should be|use your memory|check.*memory|did you forget)"; then
            CORRECTION_DETECTED=true
            CORRECTION_TEXT=$(echo "$LAST_USER_MSG" | head -c 300 | tr '\n' ' ')
            echo "[soul] correction source: regex fallback" >&2
        fi
    fi

    # Auto-store correction if detected but Claude didn't learn
    if [[ "$CORRECTION_DETECTED" == "true" ]]; then
        echo "[soul] ⚠️ COMPLIANCE: Correction detected but learn_correction not called" >&2

        # Hash-based dedup to avoid storing the same correction twice
        correction_hash=$(echo -n "$CORRECTION_TEXT" | md5sum | cut -d' ' -f1)
        if ! grep -q "^${correction_hash}$" "$DEDUP_FILE" 2>/dev/null; then
            echo "$correction_hash" >> "$DEDUP_FILE"

            ssl_content="[compliance:auto] User correction: $CORRECTION_TEXT"
            title=$(echo "$ssl_content" | head -c 100)
            queue_write "observe" "{\"category\":\"correction\",\"title\":$(echo "$title" | jq -Rs .),\"content\":$(echo "$ssl_content" | jq -Rs .)}"
            echo "[soul] +auto-correction stored: ${title:0:60}" >&2
        else
            echo "[soul] skip auto-correction: duplicate" >&2
        fi
    fi

    # Direct + meta-preference patterns - detect and log, let distillation format properly
    if echo "$LAST_USER_MSG" | grep -qiE "(I (prefer|like|want|always|never)|please (don'?t|always|never)|from now on|more concise|fewer examples|go deeper|simpler please|don'?t overexplain|be more verbose)"; then
        pref_context=$(echo "$LAST_USER_MSG" | head -c 60 | tr '\n' ' ')
        echo "[soul] preference detected: ${pref_context}..." >&2
    fi

    # If milestone detected, log it - let distillation or explicit learn_milestone handle storage
    if echo "$LAST_USER_MSG" | grep -qiE "(it works|finally|success|shipped|released|completed|finished|passed|merged|deployed)"; then
        milestone_context=$(echo "$LAST_USER_MSG" | head -c 60 | tr '\n' ' ')
        echo "[soul] milestone detected: ${milestone_context}..." >&2
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
# HABIT OBSERVATION: Learn trigger→response patterns
# ===========================================
if [[ -n "$LAST_USER_MSG" && -n "$TOOLS_FROM_TRANSCRIPT" ]]; then
    # Extract trigger: first 5 meaningful words from user message
    trigger=$(echo "$LAST_USER_MSG" | tr -cs '[:alnum:]' ' ' | awk '{for(i=1;i<=5 && i<=NF;i++) printf "%s ", $i}' | sed 's/ $//')

    # Extract response: first 5 tools used
    response=$(echo "$TOOLS_FROM_TRANSCRIPT" | head -5 | tr '\n' ',' | sed 's/,$//')

    if [[ -n "$trigger" && -n "$response" ]]; then
        queue_write "habit_observe" "{\"trigger\":$(echo "$trigger" | jq -Rs .),\"response\":$(echo "$response" | jq -Rs .)}"
        echo "[soul] +habit: ${trigger:0:30}→${response:0:30}" >&2
    fi
fi

# ===========================================
# CALIBRATION: Track prediction accuracy by domain
# ===========================================
# Debugging calibration: error detected and resolution attempted
if echo "$RESPONSE" | grep -qiE '(error|failed|exception|bug)'; then
    # Check if resolution was attempted (edit/fix patterns in response)
    if echo "$RESPONSE" | grep -qiE '(fixed|resolved|updated|corrected|the issue was)'; then
        queue_write "calibration_record" "{\"domain\":\"debugging\",\"success\":true}"
        echo "[soul] +calibration: debugging success" >&2
    else
        queue_write "calibration_record" "{\"domain\":\"debugging\",\"success\":false}"
        echo "[soul] +calibration: debugging incomplete" >&2
    fi
fi

# Code generation calibration: Edit/Write tools used
if echo "$TOOLS_FROM_TRANSCRIPT" | grep -qiE '(Edit|Write)'; then
    # Success if no errors in response after code changes
    if ! echo "$RESPONSE" | grep -qiE '(error|failed|syntax error|compilation failed)'; then
        queue_write "calibration_record" "{\"domain\":\"code_generation\",\"success\":true}"
        echo "[soul] +calibration: code_generation success" >&2
    else
        queue_write "calibration_record" "{\"domain\":\"code_generation\",\"success\":false}"
        echo "[soul] +calibration: code_generation had errors" >&2
    fi
fi

# Architecture calibration: design/architecture discussions
if echo "$RESPONSE" | grep -qiE '(architecture|design pattern|refactor|abstraction|interface|module|component|structure)'; then
    # Record architecture discussion (success = we provided guidance)
    queue_write "calibration_record" "{\"domain\":\"architecture\",\"success\":true}"
    echo "[soul] +calibration: architecture discussion" >&2
fi

# ===========================================
# STRUCTURED SPANS: Capture tool uses with outcomes
# ===========================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -x "$SCRIPT_DIR/span-capture.sh" ]]; then
    "$SCRIPT_DIR/span-capture.sh" "$TRANSCRIPT_PATH" "$LAST_USER_MSG" 2>&1 || true
fi

# Clean up temp files
rm -f "$MIND_PATH/.last_user_message" "$MIND_PATH/.last_correction_context" "$PREDICTIONS_FILE" "$DEDUP_FILE" 2>/dev/null

# ===========================================
# LEDGER: Rich session checkpoint for continuity
# ===========================================
SESSION_ID="${SESSION_ID_INPUT:-auto-$(date +%Y%m%d-%H%M%S)}"
TURNS=$(jq '[.[] | select(.role=="assistant")] | length' "$TRANSCRIPT_PATH" 2>/dev/null || echo "?")

# Extract active files from tool calls
ACTIVE_FILES=$(jq -r '[.[] | select(.role=="assistant") | .message.content[]? | select(.type=="tool_use") | .input.file_path // .input.path // empty] | unique | map(select(. != ""))' "$TRANSCRIPT_PATH" 2>/dev/null || echo "[]")
[[ -z "$ACTIVE_FILES" || "$ACTIVE_FILES" == "null" ]] && ACTIVE_FILES="[]"

# Extract tools used
TOOLS_USED=$(jq -r '[.[] | select(.role=="assistant") | .message.content[]? | select(.type=="tool_use") | .name] | unique | join(", ")' "$TRANSCRIPT_PATH" 2>/dev/null | head -c 200)

# Extract assistant text for marker detection
ASSISTANT_TEXT=$(jq -r '.[] | select(.role=="assistant") | .message.content[]? | select(.type=="text") | .text' "$TRANSCRIPT_PATH" 2>/dev/null | tail -5000)

# Extract typed markers
DECISIONS="[]"
DECISIONS_RAW=$(echo "$ASSISTANT_TEXT" | grep -oE '\[DECISION\].*$' | sed 's/\[DECISION\]\s*//' | head -10)
[[ -n "$DECISIONS_RAW" ]] && DECISIONS=$(echo "$DECISIONS_RAW" | jq -R . | jq -s .)

BLOCKERS="[]"
BLOCKERS_RAW=$(echo "$ASSISTANT_TEXT" | grep -oE '\[BLOCKER\].*$' | sed 's/\[BLOCKER\]\s*//' | head -5)
[[ -n "$BLOCKERS_RAW" ]] && BLOCKERS=$(echo "$BLOCKERS_RAW" | jq -R . | jq -s .)

DISCOVERIES="[]"
DISCOVERIES_RAW=$(echo "$ASSISTANT_TEXT" | grep -oE '\[(SOLUTION|GOTCHA)\].*$' | head -10)
[[ -n "$DISCOVERIES_RAW" ]] && DISCOVERIES=$(echo "$DISCOVERIES_RAW" | jq -R . | jq -s .)

# Extract pending tasks from transcript
TODOS="[]"
TASK_INFO=$(grep -oE '"subject"\s*:\s*"[^"]*".*"status"\s*:\s*"(pending|in_progress)"' "$TRANSCRIPT_PATH" 2>/dev/null | head -5 || true)
if [[ -n "$TASK_INFO" ]]; then
    TODOS=$(echo "$TASK_INFO" | while read -r line; do
        subj=$(echo "$line" | grep -oE '"subject"\s*:\s*"[^"]*"' | sed 's/"subject"\s*:\s*"//' | sed 's/"$//')
        stat=$(echo "$line" | grep -oE '"status"\s*:\s*"[^"]*"' | sed 's/"status"\s*:\s*"//' | sed 's/"$//')
        echo "{\"content\":\"$subj\",\"status\":\"$stat\"}"
    done | jq -s .)
    [[ -z "$TODOS" || "$TODOS" == "null" ]] && TODOS="[]"
fi

# Detect mood from session content
if echo "$RESPONSE" | grep -qiE '(error|failed|bug|stuck)'; then
    mood="debugging"
elif echo "$RESPONSE" | grep -qiE '(complete|done|finished|shipped|success)'; then
    mood="confident"
elif [[ $LEARNED -gt 0 ]]; then
    mood="learning"
else
    mood="working"
fi

# Build snapshot: last meaningful assistant text (not just first line)
snapshot=$(echo "$ASSISTANT_TEXT" | grep -v '^$' | tail -20 | head -c 1000)
[[ -z "$snapshot" ]] && snapshot=$(echo "$RESPONSE" | head -3 | head -c 300)

# Quality gate: minimum 3 turns for session summary
if [[ "$TURNS" =~ ^[0-9]+$ && "$TURNS" -ge 3 ]]; then
    SUMMARY="[session:$SESSION_ID] ${mood}→${TURNS} turns"
    [[ -n "$TOOLS_USED" ]] && SUMMARY="$SUMMARY | tools: ${TOOLS_USED:0:100}"
    queue_write "observe" "{\"category\":\"session_summary\",\"title\":\"Session $SESSION_ID\",\"content\":$(echo "$SUMMARY" | jq -Rs .)}"
    echo "[soul] +session-summary: ${SUMMARY:0:60}" >&2
else
    echo "[soul] skip session-summary: too few turns ($TURNS<3)" >&2
fi

# Build rich ledger entry
LEDGER_ARGS=$(jq -n \
    --arg session_id "$SESSION_ID" \
    --arg project "$REALM" \
    --arg transcript_path "$TRANSCRIPT_PATH" \
    --arg mood "$mood" \
    --argjson active_files "$ACTIVE_FILES" \
    --argjson decisions "$DECISIONS" \
    --argjson todos "$TODOS" \
    --argjson blockers "$BLOCKERS" \
    --argjson discoveries "$DISCOVERIES" \
    --arg snapshot "$snapshot" \
    '{
        session_id: $session_id,
        project: $project,
        transcript_path: $transcript_path,
        mood: $mood,
        active_files: $active_files,
        decisions: $decisions,
        todos: $todos,
        blockers: $blockers,
        discoveries: $discoveries,
        snapshot: $snapshot
    }')

queue_write "ledger_save" "$LEDGER_ARGS"
file_count=$(echo "$ACTIVE_FILES" | jq 'length')
decision_count=$(echo "$DECISIONS" | jq 'length')
todo_count=$(echo "$TODOS" | jq 'length')
echo "[ledger] queued: $SESSION_ID ($mood, files=$file_count decisions=$decision_count todos=$todo_count)" >&2

# ===========================================
# GOAL DETECTION: Detect goal setting and progress patterns
# ===========================================
if [[ -n "$LAST_USER_MSG" ]]; then
    # Goal setting patterns: "I want to", "we need to", "let's build/create/implement"
    if echo "$LAST_USER_MSG" | grep -qiE "(I want to|we need to|let'?s (build|create|implement|make|ship|finish|complete)|goal is to|objective is|planning to)"; then
        goal_title=$(echo "$LAST_USER_MSG" | grep -ioE "(I want to|we need to|let'?s (build|create|implement|make|ship|finish|complete)|goal is to|objective is|planning to)[^.!?]*" | head -1 | head -c 100 | tr '\n' ' ')
        if [[ -n "$goal_title" && ${#goal_title} -gt 10 ]]; then
            goal_description=$(echo "$LAST_USER_MSG" | head -c 300)
            "$CHITTA_BIN" goal_set --title "$goal_title" --description "$goal_description" 2>/dev/null || true
            echo "[soul] +goal detected: ${goal_title:0:50}" >&2
        fi
    fi

    # Goal completion patterns: "done", "shipped", "released", "finished", "completed"
    # Note: goal_progress requires goal ID, so we get the most recent active goal first
    if echo "$LAST_USER_MSG" | grep -qiE "(it'?s done|we'?re done|finished|shipped|released|completed|all done|mission accomplished|working now|tests pass|merged)"; then
        # Get most recent active goal via CLI
        goal_response=$("$CHITTA_BIN" goal_list --status active --limit 1 --json 2>/dev/null || true)
        goal_id=$(echo "$goal_response" | jq -r '.goals[0].id // empty' 2>/dev/null)
        if [[ -n "$goal_id" ]]; then
            "$CHITTA_BIN" goal_progress --id "$goal_id" --progress 1.0 2>/dev/null || true
            echo "[soul] +goal progress: $goal_id -> 100%" >&2
        fi
    fi
fi

# ===========================================
# CURIOSITY RESOLUTION: Detect answers to knowledge gaps
# ===========================================
# Check if response contains resolution patterns
if echo "$RESPONSE" | grep -qiE "(I found|the answer is|it turns out|the reason is|figured out|discovered that|realized that|the issue was|the problem was|the solution is|turns out|mystery solved)"; then
    resolution_context=$(echo "$RESPONSE" | grep -iE "(I found|the answer is|it turns out|the reason is|figured out|discovered|realized|the issue was|the problem was|the solution is)" | head -1 | head -c 300 | tr '\n' ' ')
    if [[ -n "$resolution_context" && ${#resolution_context} -gt 20 ]]; then
        # Get most recent unresolved curiosity gap via CLI
        gaps_response=$("$CHITTA_BIN" curiosity_gaps --limit 1 --json 2>/dev/null || true)
        gap_id=$(echo "$gaps_response" | jq -r '.gaps[0].id // empty' 2>/dev/null)
        if [[ -n "$gap_id" ]]; then
            "$CHITTA_BIN" curiosity_resolve --id "$gap_id" --learned "$resolution_context" 2>/dev/null || true
            echo "[soul] +curiosity resolved: gap $gap_id" >&2
        fi
    fi
fi

# ===========================================
# CROSS-SESSION MESSAGING: Deregister session
# ===========================================
DEREGISTER_SESSION="${SESSION_ID_INPUT:-${CLAUDE_SESSION_ID:-}}"
if [[ -n "$DEREGISTER_SESSION" ]]; then
    "$CHITTA_BIN" session_deregister --session_id "$DEREGISTER_SESSION" 2>/dev/null || true
fi

exit 0
