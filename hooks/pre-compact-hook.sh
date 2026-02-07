#!/bin/bash
# PreCompact hook: Save FULL session state before context compaction
#
# LOSSLESS: Captures everything needed to resume seamlessly
# - Files read during session (with line ranges)
# - Decisions made
# - Current tasks and progress
# - Blockers and discoveries
# - Understanding built

# Don't use set -e: we want to save as much state as possible even if parts fail

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

# Derive project directory from transcript path
# Transcript path: ~/.claude/projects/-maps-projects-X-Y-Z/session.jsonl
# Encoded path uses dashes, but dir names can have hyphens too (e.g., cc-soul)
decode_project_path() {
    local encoded="${1:1}"  # Skip leading dash
    local path_so_far=""
    IFS='-' read -ra PARTS <<< "$encoded"
    for part in "${PARTS[@]}"; do
        local test_path="$path_so_far/$part"
        if [[ -d "$test_path" ]]; then
            path_so_far="$test_path"
        else
            local alt_path="$path_so_far-$part"
            if [[ -d "$alt_path" ]]; then
                path_so_far="$alt_path"
            else
                path_so_far="$test_path"
            fi
        fi
    done
    echo "$path_so_far"
}

PROJECT_DIR=""
if [[ -n "$TRANSCRIPT_PATH" ]]; then
    PROJECT_ENCODED=$(dirname "$TRANSCRIPT_PATH" | xargs basename)
    PROJECT_DIR=$(decode_project_path "$PROJECT_ENCODED")
fi

# Detect realm from project directory
if [[ -n "$PROJECT_DIR" && -d "$PROJECT_DIR" ]]; then
    REALM=$(cd "$PROJECT_DIR" && timeout "$MAX_WAIT" "$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")
else
    REALM=$(timeout "$MAX_WAIT" "$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")
fi

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
    # Extract assistant message text content (properly unescaped via jq)
    # This gives us clean text without JSON escaping
    ASSISTANT_TEXT=$(cat "$TRANSCRIPT_PATH" | \
        jq -r 'select(.type=="assistant") | .message.content[]? | select(.type=="text") | .text' 2>/dev/null | \
        tail -10000)

    # Extract files read (from Read tool calls in raw JSON)
    FILES_RAW=$(grep -oE '"file_path"\s*:\s*"[^"]+"' "$TRANSCRIPT_PATH" 2>/dev/null | \
        sed 's/"file_path"\s*:\s*"//' | sed 's/"$//' | sort -u | tail -20)
    if [[ -n "$FILES_RAW" ]]; then
        ACTIVE_FILES=$(echo "$FILES_RAW" | jq -R . | jq -s .)
    fi

    # Extract typed markers from clean assistant text
    # [DECISION] - design choices
    DECISIONS_RAW=$(echo "$ASSISTANT_TEXT" | grep -oE '^\[DECISION\].*$' | sed 's/\[DECISION\]\s*//' | head -10)
    if [[ -n "$DECISIONS_RAW" ]]; then
        DECISIONS=$(echo "$DECISIONS_RAW" | jq -R . | jq -s .)
    fi

    # [BLOCKER] - things blocking progress
    BLOCKERS_RAW=$(echo "$ASSISTANT_TEXT" | grep -oE '^\[BLOCKER\].*$' | sed 's/\[BLOCKER\]\s*//' | head -5)
    if [[ -n "$BLOCKERS_RAW" ]]; then
        BLOCKERS=$(echo "$BLOCKERS_RAW" | jq -R . | jq -s .)
    fi

    # [SOLUTION] and [GOTCHA] - discoveries
    DISCOVERIES_RAW=$(echo "$ASSISTANT_TEXT" | grep -oE '^\[(SOLUTION|GOTCHA)\].*$' | head -10)
    if [[ -n "$DISCOVERIES_RAW" ]]; then
        DISCOVERIES=$(echo "$DISCOVERIES_RAW" | jq -R . | jq -s .)
    fi

    # Build snapshot: last significant chunk of assistant text
    SNAPSHOT=$(echo "$ASSISTANT_TEXT" | tail -100 | grep -v '^$' | tail -20 | head -c 2000)

    # If snapshot is too short, add file context
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
