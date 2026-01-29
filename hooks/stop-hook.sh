#!/bin/bash
# Stop hook: Extract typed learnings, handle feedback, checkpoint
#
# HIGH PERFORMANCE: Uses queue for write ops (no blocking)
#
# Learning types extracted:
#   [SOLUTION], [GOTCHA], [PREFERENCE], [DECISION], [FAILURE], [PATTERN], [LEARN]
#
# Feedback markers:
#   [USED:uuid] - memory was helpful (strengthens)

set -e

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
QUEUE_FILE="${CHITTA_QUEUE:-/tmp/chitta-queue.jsonl}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-2}"

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

# Extract typed learnings → queue (no waiting)
LEARNED=0
while IFS= read -r line; do
    if [[ "$line" =~ ^\[(SOLUTION|GOTCHA|PREFERENCE|DECISION|FAILURE|PATTERN|LEARN)\] ]]; then
        type="${BASH_REMATCH[1]}"
        content="${line#\[$type\] }"
        category=$(map_category "$type")
        title=$(echo "$content" | head -c 100)

        # Queue observe (fire-and-forget)
        queue_write "observe" "{\"category\":\"$category\",\"title\":$(echo "$title" | jq -Rs .),\"content\":$(echo "$content" | jq -Rs .)}"
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
