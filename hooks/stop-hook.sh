#!/bin/bash
# Stop hook: Extract typed learnings, handle feedback, checkpoint
#
# Learning types extracted:
#   [SOLUTION] - what worked
#   [GOTCHA] - traps and warnings
#   [PREFERENCE] - user preferences
#   [DECISION] - design choices with reasoning
#   [FAILURE] - what didn't work
#   [PATTERN] - recurring approaches
#   [LEARN] - general learnings (legacy)
#
# Feedback markers:
#   [USED:uuid] - memory was helpful (strengthens)
#
# Ledger: ALWAYS saves session state for continuity
#
# Input (JSON via stdin):
#   transcript_path, stop_hook_active, session_id
#
# Output:
#   - JSON with decision: "block" to prevent stopping (for long-tasks)
#   - stderr: status messages

set -e

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-5}"

# Parse JSON input
INPUT=$(cat)
TRANSCRIPT_PATH=$(echo "$INPUT" | jq -r '.transcript_path // empty')
STOP_HOOK_ACTIVE=$(echo "$INPUT" | jq -r '.stop_hook_active // false')
SESSION_ID_INPUT=$(echo "$INPUT" | jq -r '.session_id // empty')

# Prevent infinite loops
[[ "$STOP_HOOK_ACTIVE" == "true" ]] && exit 0
[[ -z "$TRANSCRIPT_PATH" || ! -f "$TRANSCRIPT_PATH" ]] && exit 0

# Check chitta CLI exists
[[ ! -x "$CHITTA_BIN" ]] && exit 0

json_escape() {
    echo -n "$1" | jq -Rs '.' | sed 's/^"//;s/"$//'
}

# Map learning type to category
map_category() {
    case "$1" in
        SOLUTION|solution|sol) echo "solution" ;;
        GOTCHA|gotcha|trap|warning) echo "gotcha" ;;
        PREFERENCE|preference|pref) echo "preference" ;;
        DECISION|decision|dec) echo "decision" ;;
        FAILURE|failure|fail) echo "failure" ;;
        PATTERN|pattern|pat) echo "pattern" ;;
        LEARN|learn|insight) echo "wisdom" ;;
        *) echo "wisdom" ;;
    esac
}

# Extract last assistant message from transcript
RESPONSE=$(tac "$TRANSCRIPT_PATH" | grep -m1 '"role":"assistant"' | \
    jq -r '.message.content[] | select(.type=="text") | .text' 2>/dev/null | head -c 10000)

[[ -z "$RESPONSE" || ${#RESPONSE} -lt 10 ]] && exit 0

# Detect realm
REALM=$(timeout "$MAX_WAIT" "$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")

# Handle active long-task
task_json=$(timeout "$MAX_WAIT" "$CHITTA_BIN" long_task_active --realm "$REALM" --json 2>/dev/null || true)
task_found=$(echo "$task_json" | jq -r '.result.structured.found // false' 2>/dev/null)

if [[ "$task_found" == "true" ]]; then
    task_id=$(echo "$task_json" | jq -r '.result.structured.task_id // ""' 2>/dev/null)

    if [[ -n "$task_id" ]]; then
        summary=$(echo "$RESPONSE" | head -c 500 | tr '\n' ' ')
        timeout "$MAX_WAIT" "$CHITTA_BIN" long_task_event --task_id "$task_id" --kind checkpoint --payload "$summary" 2>/dev/null || true
        timeout "$MAX_WAIT" "$CHITTA_BIN" long_task_update --task_id "$task_id" 2>/dev/null || true

        if echo "$RESPONSE" | grep -qiE '\[TASK[_-]?COMPLETE\]|\[DONE\]|<promise>COMPLETE</promise>'; then
            timeout "$MAX_WAIT" "$CHITTA_BIN" long_task_complete --task_id "$task_id" --outcome "Completed" 2>/dev/null || true
            echo "[long-task] Completed: $task_id" >&2
        else
            eval_json=$(timeout "$MAX_WAIT" "$CHITTA_BIN" long_task_evaluate --task_id "$task_id" --json 2>/dev/null || true)
            decision=$(echo "$eval_json" | jq -r '.result.structured.decision // "continue"' 2>/dev/null)

            if [[ "$decision" == "blocked" ]]; then
                next_prompt=$(echo "$eval_json" | jq -r '.result.structured.next_prompt // ""' 2>/dev/null)
                echo "{\"decision\":\"block\",\"reason\":\"Long task blocked: $next_prompt\"}"
                exit 0
            fi
        fi
    fi
fi

# Extract typed learnings: [TYPE] content
LEARNED=0
while IFS= read -r line; do
    if [[ "$line" =~ ^\[(SOLUTION|GOTCHA|PREFERENCE|DECISION|FAILURE|PATTERN|LEARN)\] ]]; then
        type="${BASH_REMATCH[1]}"
        content="${line#\[$type\] }"
        category=$(map_category "$type")
        title=$(echo "$content" | head -c 100)

        timeout "$MAX_WAIT" "$CHITTA_BIN" observe --category "$category" --title "$title" --content "$content" 2>/dev/null || true
        echo "[soul] +${type,,}: $title" >&2
        ((LEARNED++)) || true
    fi
done <<< "$RESPONSE"

# Extract [USED:uuid] feedback markers - strengthen those memories
while IFS= read -r marker; do
    [[ -z "$marker" ]] && continue
    uuid="${marker#\[USED:}"
    uuid="${uuid%\]}"
    [[ -z "$uuid" || ${#uuid} -lt 30 ]] && continue

    timeout "$MAX_WAIT" "$CHITTA_BIN" strengthen --id "$uuid" --amount 0.1 2>/dev/null || true
    echo "[soul] ↑ strengthened: ${uuid:0:8}..." >&2
done <<< "$(echo "$RESPONSE" | grep -oE '\[USED:[a-f0-9-]+\]')"

# Extract [TRIPLET] lines
while IFS= read -r line; do
    if [[ "$line" =~ ^\[TRIPLET\] ]]; then
        triplet="${line#\[TRIPLET\] }"
        subj=$(echo "$triplet" | awk '{print $1}')
        pred=$(echo "$triplet" | awk '{print $2}')
        obj=$(echo "$triplet" | awk '{print $3}')

        if [[ -n "$subj" && -n "$pred" && -n "$obj" ]]; then
            timeout "$MAX_WAIT" "$CHITTA_BIN" connect --subject "$subj" --predicate "$pred" --object "$obj" 2>/dev/null || true
        fi
    fi
done <<< "$RESPONSE"

# ALWAYS save ledger for session continuity
SESSION_ID="${SESSION_ID_INPUT:-auto-$(date +%Y%m%d-%H%M%S)}"

# Extract file paths
files_json=$(echo "$RESPONSE" | grep -oE '[a-zA-Z0-9_/-]+\.(py|js|ts|tsx|cpp|hpp|c|h|rs|go|sh|md|json|yaml|yml|toml|sql|html|css|scss)' | sort -u | head -10 | jq -R . | jq -s '.' 2>/dev/null || echo '[]')

# Extract decisions
decisions_json=$(echo "$RESPONSE" | grep -iE '(chose|decided|using .* instead|went with|picked|selected|opting for)' | head -5 | sed 's/^[[:space:]]*//' | jq -R . | jq -s '.' 2>/dev/null || echo '[]')

# Extract next steps
next_steps_json=$(echo "$RESPONSE" | grep -iE '(^[0-9]+\.|^- \[.\]|next:|todo:|should .* next|will .* next|need to)' | head -5 | sed 's/^[[:space:]]*//' | jq -R . | jq -s '.' 2>/dev/null || echo '[]')

# Detect mood from response content
if echo "$RESPONSE" | grep -qiE '(error|failed|bug|issue|problem|stuck|broken)'; then
    mood="debugging"
elif echo "$RESPONSE" | grep -qiE '(complete|done|finished|working|success|passed|shipped)'; then
    mood="confident"
elif echo "$RESPONSE" | grep -qiE '(trying|attempting|testing|investigating|exploring)'; then
    mood="exploring"
elif [[ $LEARNED -gt 0 ]]; then
    mood="learning"
else
    mood="working"
fi

# Snapshot from first meaningful line
snapshot=$(echo "$RESPONSE" | grep -v '^$' | grep -v '^\[' | head -1 | head -c 200)
[[ -z "$snapshot" ]] && snapshot=$(echo "$RESPONSE" | head -1 | head -c 200)

# Ensure valid JSON arrays
[[ -z "$files_json" || "$files_json" == "null" ]] && files_json='[]'
[[ -z "$decisions_json" || "$decisions_json" == "null" ]] && decisions_json='[]'
[[ -z "$next_steps_json" || "$next_steps_json" == "null" ]] && next_steps_json='[]'

# Save ledger via CLI
if timeout "$MAX_WAIT" "$CHITTA_BIN" ledger_save \
    --session_id "$SESSION_ID" \
    --project "$REALM" \
    --mood "$mood" \
    --active_files "$files_json" \
    --decisions "$decisions_json" \
    --next_steps "$next_steps_json" \
    --snapshot "$snapshot" 2>/dev/null; then
    echo "[ledger] saved: $SESSION_ID ($mood, +$LEARNED)" >&2
else
    echo "[ledger] FAILED to save: $SESSION_ID" >&2
fi

exit 0
