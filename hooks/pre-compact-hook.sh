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
MAX_WAIT="${CC_SOUL_MAX_WAIT:-2}"

# Source shared library (provides queue_write with ack_id, get_queue_file, etc.)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/lib.sh"

# Parse JSON input
INPUT=$(cat)
TRIGGER=$(echo "$INPUT" | jq -r '.trigger // "auto"')
TRANSCRIPT_PATH=$(echo "$INPUT" | jq -r '.transcript_path // empty')
REAL_SESSION_ID=$(echo "$INPUT" | jq -r '.session_id // empty')

# Check chitta CLI exists
[[ ! -x "$CHITTA_BIN" ]] && exit 0

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

    # ═══════════════════════════════════════════════════════════════════════════
    # Extract and persist key contextual facts as memories (no LLM needed)
    # ═══════════════════════════════════════════════════════════════════════════

    # Extract operational knowledge patterns:
    # - "use X for/to/via Y" patterns
    # - "X is the working/correct host/server"
    # - "works via/through X"
    # - host names, SSH commands, rclone, proxy info
    FACTS_RAW=$(echo "$ASSISTANT_TEXT" | grep -iE \
        '(use [a-z]+ (for|to|via|through)|is the (working|correct|proper|right)|works? (via|through|by|with)|connect (to|through|via)|host[: ][a-z]|server[: ][a-z]|proxy|socks|ssh [^$]|rclone|important:|note:|remember:)' \
        2>/dev/null | grep -v '^\s*$' | head -10)

    # Also extract context around markers (lines near decisions/solutions)
    MARKER_CONTEXT=$(echo "$ASSISTANT_TEXT" | grep -B2 -A2 -E '^\[(DECISION|BLOCKER|SOLUTION|GOTCHA)\]' 2>/dev/null \
        | grep -v '^\[(DECISION|BLOCKER|SOLUTION|GOTCHA)\]' | grep -v '^--$' | head -10)

    # Combine and deduplicate
    ALL_FACTS=$(printf "%s\n%s" "$FACTS_RAW" "$MARKER_CONTEXT" | sort -u | grep -v '^\s*$' | head -15)

    if [[ -n "$ALL_FACTS" ]]; then
        # Store as wisdom memory with pre-compact provenance
        FACT_CONTENT=$(printf "[pre-compact:%s] Key context from session\n%s" "$REALM" "$ALL_FACTS")
        FACT_ARGS=$(jq -n \
            --arg category "wisdom" \
            --arg title "Pre-compact context: $SESSION_ID" \
            --arg content "$FACT_CONTENT" \
            --arg realm "$REALM" \
            '{category: $category, title: $title, content: $content, realm: $realm, confidence: 0.85}')
        queue_write "observe" "$FACT_ARGS"
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

# ═══════════════════════════════════════════════════════════════════════════
# Trigger immediate distillation via daemon
# ═══════════════════════════════════════════════════════════════════════════

# Use real session_id if available, otherwise derive from transcript path
DISTILL_SESSION_ID="$REAL_SESSION_ID"
if [[ -z "$DISTILL_SESSION_ID" && -n "$TRANSCRIPT_PATH" ]]; then
    DISTILL_SESSION_ID=$(basename "$TRANSCRIPT_PATH" .jsonl)
fi

if [[ -n "$DISTILL_SESSION_ID" && -n "$TRANSCRIPT_PATH" ]]; then
    # Ensure transcript is registered (in case session-start didn't fire)
    queue_write "transcript_register" "{\"session_id\":\"$DISTILL_SESSION_ID\",\"transcript_path\":$(echo "$TRANSCRIPT_PATH" | jq -Rs .),\"realm\":\"$REALM\"}"

    # Trigger immediate distillation before compaction loses context
    queue_write "distill_trigger" "{\"session_id\":\"$DISTILL_SESSION_ID\"}"

    echo "[distill] Triggered for $DISTILL_SESSION_ID" >&2
fi

# ═══════════════════════════════════════════════════════════════════════════
# COMPACT_CONTEXT: Memory-aware turn scoring before compaction
#
# Uses chitta's semantic memory to identify which conversation turns are
# already captured as memories (safe to drop) vs novel content (must keep).
# Stores a scored snapshot so the soul knows what was semantically important.
# ═══════════════════════════════════════════════════════════════════════════
if [[ -n "$TRANSCRIPT_PATH" && -f "$TRANSCRIPT_PATH" && -x "$CHITTA_BIN" ]]; then
    # Parse JSONL transcript → messages array (last 60 turns, skip empty content)
    MESSAGES_JSON=$(jq -sc '[
        .[] |
        select(.type == "user" or .type == "assistant") |
        {
            role: .type,
            content: (
                if .type == "assistant" then
                    ((.message.content // []) | map(select(.type == "text") | .text) | join(" "))
                else
                    (.message.content // "" | if type == "array" then (map(.text // "") | join(" ")) else tostring end)
                end
            )
        } |
        select((.content | length) > 0)
    ] | .[-60:]' "$TRANSCRIPT_PATH" 2>/dev/null || echo "[]")

    if [[ "$MESSAGES_JSON" != "[]" && "$MESSAGES_JSON" != "null" ]]; then
        # Build JSON-RPC request — use thin client mode (stdin) for complex nested params
        COMPACT_RPC=$(jq -n \
            --argjson messages "$MESSAGES_JSON" \
            --arg query "${SNAPSHOT:0:300}" \
            '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"compact_context","arguments":{
                "messages": $messages,
                "query": $query,
                "target_ratio": 0.4
            }}}')

        COMPACT_RESULT=$(printf '%s' "$COMPACT_RPC" | \
            timeout 15 "$CHITTA_BIN" 2>/dev/null || true)

        if [[ -n "$COMPACT_RESULT" ]]; then
            before=$(printf '%s' "$COMPACT_RESULT" | jq -r '.result.stats.before_tokens // 0' 2>/dev/null || echo 0)
            after=$(printf '%s' "$COMPACT_RESULT"  | jq -r '.result.stats.after_tokens // 0' 2>/dev/null || echo 0)
            dropped=$(printf '%s' "$COMPACT_RESULT" | jq -r '.result.stats.dropped_pct // 0' 2>/dev/null || echo 0)
            embedded=$(printf '%s' "$COMPACT_RESULT" | jq -r '.result.stats.embedding // false' 2>/dev/null || echo false)

            echo "[compact_context] ${before}→${after} tok | ${dropped}% memory-covered drops | embedding=${embedded}" >&2

            # Persist compaction metadata as an episode so the soul tracks what survived
            if [[ -n "$before" && "$before" != "0" ]]; then
                queue_write "observe" "$(jq -n \
                    --arg realm "$REALM" \
                    --arg sid "${REAL_SESSION_ID:-$SESSION_ID}" \
                    --arg before "$before" \
                    --arg after "$after" \
                    --arg dropped "$dropped" \
                    '{category: "episode",
                      content: ("[pre-compact:\($realm)] Memory-aware compaction: \($before)→\($after) tokens (\($dropped)% dropped as memory-covered). Session: \($sid)"),
                      realm: $realm,
                      confidence: 0.7}')"
            fi
        fi
    fi
fi

exit 0
