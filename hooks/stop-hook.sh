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

# Extract [USED:uuid] feedback → queue strengthen
while IFS= read -r marker; do
    [[ -z "$marker" ]] && continue
    uuid="${marker#\[USED:}"
    uuid="${uuid%\]}"
    [[ -z "$uuid" || ${#uuid} -lt 30 ]] && continue

    queue_write "strengthen" "{\"id\":\"$uuid\",\"amount\":0.1}"
    echo "[soul] ↑ ${uuid:0:8}..." >&2
done <<< "$(echo "$RESPONSE" | grep -oE '\[USED:[a-f0-9-]+\]')"

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
    # Hard + soft correction patterns
    if echo "$LAST_USER_MSG" | grep -qiE "(no,|no\.|actually|that'?s (wrong|not|incorrect)|you('re| are) wrong|wrong approach|that won'?t work|let me rephrase|I meant|should be|not quite|close but)"; then
        # Extract what was wrong from user message
        correction_context=$(echo "$LAST_USER_MSG" | head -c 150 | tr '\n' ' ')
        # Extract what Claude should do instead from response (first line)
        better_approach=$(echo "$RESPONSE" | grep -v '^$' | head -1 | head -c 150)

        # Format as SSL correction (matches learn_correction format)
        content="[correction] WRONG: $correction_context
CORRECT: $better_approach"

        # Queue the learning (async, no blocking)
        queue_write "observe" "{\"category\":\"correction\",\"title\":\"Auto-correction\",\"content\":$(echo "$content" | jq -Rs .)}"
        echo "[soul] +auto-correction: ${correction_context:0:60}" >&2
        ((LEARNED++)) || true
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
# ANTICIPATION SUCCESS: Learn from correct predictions
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
            while read -r pattern; do
                [[ -z "$pattern" ]] && continue

                pattern_id=$(echo "$pattern" | jq -r '.id // 0' 2>/dev/null)
                action=$(echo "$pattern" | jq -r '.action // ""' 2>/dev/null)

                [[ "$pattern_id" -eq 0 || -z "$action" ]] && continue

                # Check if action matches any tool used (fuzzy match: contains tool name or vice versa)
                match_found=false
                while IFS= read -r tool; do
                    [[ -z "$tool" ]] && continue

                    # Match if:
                    # - tool name appears in action (e.g., "Edit" in "use Edit tool")
                    # - action appears in tool name
                    # - case-insensitive substring match
                    action_lower=$(echo "$action" | tr '[:upper:]' '[:lower:]')
                    tool_lower=$(echo "$tool" | tr '[:upper:]' '[:lower:]')

                    if [[ "$action_lower" == *"$tool_lower"* || "$tool_lower" == *"$action_lower"* ]]; then
                        match_found=true
                        break
                    fi
                done <<< "$TOOLS_USED"

                if [[ "$match_found" == "true" ]]; then
                    # Queue anticipation_success call
                    queue_write "anticipation_success" "{\"id\":$pattern_id}"
                    echo "[soul] ✓ prediction #${pattern_id}: ${action:0:40}" >&2
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

queue_write "ledger_save" "{\"session_id\":\"$SESSION_ID\",\"project\":\"$REALM\",\"mood\":\"$mood\",\"snapshot\":$(echo "$snapshot" | jq -Rs .)}"
echo "[ledger] queued: $SESSION_ID ($mood, +$LEARNED)" >&2

exit 0
