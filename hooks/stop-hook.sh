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

MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind/chitta}"
CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
CHITTAD_BIN="${CHITTAD_BIN:-$HOME/.claude/bin/chittad}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-5}"

# Parse JSON input
INPUT=$(cat)
TRANSCRIPT_PATH=$(echo "$INPUT" | jq -r '.transcript_path // empty')
STOP_HOOK_ACTIVE=$(echo "$INPUT" | jq -r '.stop_hook_active // false')
SESSION_ID_INPUT=$(echo "$INPUT" | jq -r '.session_id // empty')

# Prevent infinite loops
[[ "$STOP_HOOK_ACTIVE" == "true" ]] && exit 0
[[ -z "$TRANSCRIPT_PATH" || ! -f "$TRANSCRIPT_PATH" ]] && exit 0

djb2_hash() {
    local str="$1" hash=5381 i c
    for ((i=0; i<${#str}; i++)); do
        c=$(printf '%d' "'${str:$i:1}")
        hash=$(( ((hash << 5) + hash) + c ))
        hash=$((hash & 0xFFFFFFFF))
    done
    echo "$hash"
}

SOCKET="/tmp/chitta-$(djb2_hash "$MIND_PATH").sock"

json_escape() {
    echo -n "$1" | jq -Rs '.' | sed 's/^"//;s/"$//'
}

rpc_call() {
    local tool="$1" args="$2"
    local request="{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"$tool\",\"arguments\":$args}}"
    [[ ! -S "$SOCKET" ]] && return 1
    echo "$request" | timeout "$MAX_WAIT" nc -U "$SOCKET" 2>/dev/null | head -1
}

ensure_daemon() {
    [[ -S "$SOCKET" ]] && return 0
    [[ ! -x "$CHITTAD_BIN" ]] && return 1

    "$CHITTAD_BIN" daemon &
    disown

    local waited=0
    while [[ ! -S "$SOCKET" && $waited -lt 50 ]]; do
        sleep 0.1
        ((waited++))
    done
    [[ -S "$SOCKET" ]]
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

# Ledger save with retry and fallback
save_ledger() {
    local session_id="$1"
    local realm="$2"
    local mood="$3"
    local files_json="$4"
    local decisions_json="$5"
    local next_steps_json="$6"
    local snapshot="$7"
    local learned="$8"

    # Try CLI first (more reliable)
    if [[ -x "$CHITTA_BIN" ]]; then
        if "$CHITTA_BIN" ledger_save \
            --session_id "$session_id" \
            --project "$realm" \
            --mood "$mood" \
            --active_files "$files_json" \
            --decisions "$decisions_json" \
            --next_steps "$next_steps_json" \
            --snapshot "$snapshot" 2>/dev/null; then
            echo "[ledger] saved: $session_id ($mood, +$learned)" >&2
            return 0
        fi
    fi

    # Fallback to RPC
    if [[ -S "$SOCKET" ]]; then
        local args="{\"session_id\":\"$(json_escape "$session_id")\",\"project\":\"$(json_escape "$realm")\",\"mood\":\"$mood\",\"snapshot\":\"$(json_escape "$snapshot")\"}"
        if rpc_call "ledger_save" "$args" >/dev/null 2>&1; then
            echo "[ledger] saved via RPC: $session_id" >&2
            return 0
        fi
    fi

    echo "[ledger] FAILED to save: $session_id" >&2
    return 1
}

ensure_daemon || exit 0

# Extract last assistant message from transcript
RESPONSE=$(tac "$TRANSCRIPT_PATH" | grep -m1 '"role":"assistant"' | \
    jq -r '.message.content[] | select(.type=="text") | .text' 2>/dev/null | head -c 10000)

[[ -z "$RESPONSE" || ${#RESPONSE} -lt 10 ]] && exit 0

# Detect realm
if [[ -x "$CHITTA_BIN" ]]; then
    REALM=$("$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")
else
    REALM="brahman"
fi
escaped_realm=$(json_escape "$REALM")

# Handle active long-task
response=$(rpc_call "long_task_active" "{\"realm\":\"$escaped_realm\"}")
task_found=$(echo "$response" | jq -r '.result.structured.found // false' 2>/dev/null)

if [[ "$task_found" == "true" ]]; then
    task_id=$(echo "$response" | jq -r '.result.structured.task_id // ""' 2>/dev/null)

    if [[ -n "$task_id" ]]; then
        escaped_task=$(json_escape "$task_id")

        summary=$(echo "$RESPONSE" | head -c 500 | tr '\n' ' ')
        rpc_call "long_task_event" "{\"task_id\":\"$escaped_task\",\"kind\":\"checkpoint\",\"payload\":\"$(json_escape "$summary")\"}" >/dev/null 2>&1 || true
        rpc_call "long_task_update" "{\"task_id\":\"$escaped_task\"}" >/dev/null 2>&1 || true

        if echo "$RESPONSE" | grep -qiE '\[TASK[_-]?COMPLETE\]|\[DONE\]|<promise>COMPLETE</promise>'; then
            rpc_call "long_task_complete" "{\"task_id\":\"$escaped_task\",\"outcome\":\"Completed\"}" >/dev/null 2>&1 || true
            echo "[long-task] Completed: $task_id" >&2
        else
            eval_response=$(rpc_call "long_task_evaluate" "{\"task_id\":\"$escaped_task\"}")
            decision=$(echo "$eval_response" | jq -r '.result.structured.decision // "continue"' 2>/dev/null)

            if [[ "$decision" == "blocked" ]]; then
                next_prompt=$(echo "$eval_response" | jq -r '.result.structured.next_prompt // ""' 2>/dev/null)
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

        rpc_call "observe" "{\"category\":\"$category\",\"title\":\"$(json_escape "$title")\",\"content\":\"$(json_escape "$content")\"}" >/dev/null 2>&1 || true
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

    rpc_call "strengthen" "{\"id\":\"$uuid\",\"amount\":0.1}" >/dev/null 2>&1 || true
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
            rpc_call "connect" "{\"subject\":\"$subj\",\"predicate\":\"$pred\",\"object\":\"$obj\"}" >/dev/null 2>&1 || true
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

# Save ledger (with retry and fallback)
save_ledger "$SESSION_ID" "$REALM" "$mood" "$files_json" "$decisions_json" "$next_steps_json" "$snapshot" "$LEARNED"

exit 0
