#!/bin/bash
# PreCompact hook: Save FULL session state before context compaction
#
# LOSSLESS: Captures everything needed to resume seamlessly
# - Files read during session (with line ranges)
# - Decisions made
# - Current tasks and progress
# - Blockers and discoveries
# - Understanding built

set -e

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
QUEUE_FILE="${CHITTA_QUEUE:-/tmp/chitta-queue.jsonl}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-2}"

# Parse JSON input
INPUT=$(cat)
TRIGGER=$(echo "$INPUT" | jq -r '.trigger // "auto"')
TRANSCRIPT_PATH=$(echo "$INPUT" | jq -r '.transcript_path // empty')

# Check chitta CLI exists
[[ ! -x "$CHITTA_BIN" ]] && exit 0

# Queue write - fire and forget
queue_write() {
    local tool="$1" args="$2"
    echo "{\"tool\":\"$tool\",\"args\":$args,\"ts\":$(date +%s)}" >> "$QUEUE_FILE"
}

# Detect realm
REALM=$(timeout "$MAX_WAIT" "$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")

SESSION_ID="compact-$(date +%Y%m%d-%H%M%S)"

# ═══════════════════════════════════════════════════════════════════════════
# Extract session state from transcript
# ═══════════════════════════════════════════════════════════════════════════

ACTIVE_FILES="[]"
DECISIONS="[]"
TODOS="[]"
BLOCKERS="[]"
DISCOVERIES="[]"
NEXT_STEPS="[]"
SNAPSHOT=""

if [[ -n "$TRANSCRIPT_PATH" && -f "$TRANSCRIPT_PATH" ]]; then
    # Extract files read (from Read tool calls)
    # Format: file_path:start-end or just file_path
    FILES_RAW=$(grep -oE '"file_path"\s*:\s*"[^"]+"' "$TRANSCRIPT_PATH" 2>/dev/null | \
        sed 's/"file_path"\s*:\s*"//' | sed 's/"$//' | sort -u | tail -20)
    if [[ -n "$FILES_RAW" ]]; then
        ACTIVE_FILES=$(echo "$FILES_RAW" | jq -R . | jq -s .)
    fi

    # Extract decisions from [DECISION] markers in assistant messages
    DECISIONS_RAW=$(grep -oE '\[DECISION\][^"]*' "$TRANSCRIPT_PATH" 2>/dev/null | \
        sed 's/\[DECISION\]\s*//' | head -10)
    if [[ -n "$DECISIONS_RAW" ]]; then
        DECISIONS=$(echo "$DECISIONS_RAW" | jq -R . | jq -s .)
    fi

    # Extract current task context from last few messages
    # Look for task descriptions, current work
    LAST_MESSAGES=$(tac "$TRANSCRIPT_PATH" | head -50)

    # Look for task markers or "currently working on" patterns
    CURRENT_WORK=$(echo "$LAST_MESSAGES" | grep -oE '(working on|implementing|fixing|adding|updating)[^.]*\.' | head -3)

    # Extract blockers from [BLOCKER] or error patterns
    BLOCKERS_RAW=$(grep -oE '\[BLOCKER\][^"]*' "$TRANSCRIPT_PATH" 2>/dev/null | \
        sed 's/\[BLOCKER\]\s*//' | head -5)
    if [[ -n "$BLOCKERS_RAW" ]]; then
        BLOCKERS=$(echo "$BLOCKERS_RAW" | jq -R . | jq -s .)
    fi

    # Extract discoveries from [SOLUTION] and [GOTCHA]
    DISCOVERIES_RAW=$(grep -oE '\[(SOLUTION|GOTCHA)\][^"]*' "$TRANSCRIPT_PATH" 2>/dev/null | \
        sed 's/\[(SOLUTION|GOTCHA)\]\s*//' | head -10)
    if [[ -n "$DISCOVERIES_RAW" ]]; then
        DISCOVERIES=$(echo "$DISCOVERIES_RAW" | jq -R . | jq -s .)
    fi

    # Build snapshot: last significant assistant message
    SNAPSHOT=$(tac "$TRANSCRIPT_PATH" | grep -m1 '"role":"assistant"' | \
        jq -r '.message.content[] | select(.type=="text") | .text' 2>/dev/null | \
        head -c 2000 || echo "")

    # If snapshot is too short, try to get more context
    if [[ ${#SNAPSHOT} -lt 100 ]]; then
        SNAPSHOT="Context compacted ($TRIGGER). Files: $(echo "$ACTIVE_FILES" | jq -r '.[:5] | join(", ")')"
    fi
fi

# ═══════════════════════════════════════════════════════════════════════════
# Check for active TaskList todos
# ═══════════════════════════════════════════════════════════════════════════

# Try to get pending tasks from the task system (if available in transcript)
TASK_INFO=$(grep -oE '"subject"\s*:\s*"[^"]*".*"status"\s*:\s*"(pending|in_progress)"' "$TRANSCRIPT_PATH" 2>/dev/null | head -5 || true)
if [[ -n "$TASK_INFO" ]]; then
    TODOS=$(echo "$TASK_INFO" | while read -r line; do
        subj=$(echo "$line" | grep -oE '"subject"\s*:\s*"[^"]*"' | sed 's/"subject"\s*:\s*"//' | sed 's/"$//')
        stat=$(echo "$line" | grep -oE '"status"\s*:\s*"[^"]*"' | sed 's/"status"\s*:\s*"//' | sed 's/"$//')
        echo "{\"content\":\"$subj\",\"status\":\"$stat\"}"
    done | jq -s .)
fi

# ═══════════════════════════════════════════════════════════════════════════
# Queue comprehensive ledger save
# ═══════════════════════════════════════════════════════════════════════════

# Build the full ledger entry
LEDGER_ARGS=$(jq -n \
    --arg session_id "$SESSION_ID" \
    --arg project "$REALM" \
    --arg mood "pre-compact" \
    --argjson active_files "$ACTIVE_FILES" \
    --argjson decisions "$DECISIONS" \
    --argjson todos "$TODOS" \
    --argjson blockers "$BLOCKERS" \
    --argjson discoveries "$DISCOVERIES" \
    --arg snapshot "$SNAPSHOT" \
    '{
        session_id: $session_id,
        project: $project,
        mood: $mood,
        active_files: $active_files,
        decisions: $decisions,
        todos: $todos,
        blockers: $blockers,
        discoveries: $discoveries,
        snapshot: $snapshot
    }')

queue_write "ledger_save" "$LEDGER_ARGS"

# Report what was captured
file_count=$(echo "$ACTIVE_FILES" | jq 'length')
decision_count=$(echo "$DECISIONS" | jq 'length')
todo_count=$(echo "$TODOS" | jq 'length')

echo "[checkpoint] $SESSION_ID: files=$file_count decisions=$decision_count todos=$todo_count" >&2

exit 0
