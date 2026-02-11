#!/bin/bash
# SessionStart hook: Initialize soul context with FULL state restoration
#
# LOSSLESS: Restores complete session state after compaction
# - Files that were being worked on
# - Decisions made
# - Tasks and progress
# - Blockers and discoveries

# Don't use set -e: we want to continue even if some parts fail
# This is critical for post-compaction sessions where some data may be missing

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-2}"

# Source shared library (provides queue_write with ack_id, get_queue_file, etc.)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/lib.sh"

SOCKET_PATH=$(get_socket_path)

# Parse JSON input
INPUT=$(cat)
SESSION_ID=$(echo "$INPUT" | jq -r '.session_id // empty')
TRANSCRIPT_PATH=$(echo "$INPUT" | jq -r '.transcript_path // empty')

# Check chitta CLI exists
[[ ! -x "$CHITTA_BIN" ]] && exit 0

# ===========================================
# AUTO-UPGRADE: Check version mismatch
# ===========================================
PLUGIN_ROOT="${CLAUDE_PLUGIN_ROOT:-$(dirname "$SCRIPT_DIR")}"
PLUGIN_VERSION=$(jq -r '.version // empty' "$PLUGIN_ROOT/.claude-plugin/plugin.json" 2>/dev/null)
INSTALLED_VERSION=$("$CHITTA_BIN" --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')

if [[ -n "$PLUGIN_VERSION" && -n "$INSTALLED_VERSION" && "$PLUGIN_VERSION" != "$INSTALLED_VERSION" ]]; then
    echo "[soul] Version mismatch: plugin=$PLUGIN_VERSION installed=$INSTALLED_VERSION" >&2
    if [[ -x "$PLUGIN_ROOT/scripts/smart-install.sh" ]]; then
        echo "[soul] Auto-upgrading in background..." >&2
        nohup "$PLUGIN_ROOT/scripts/smart-install.sh" > /tmp/cc-soul-upgrade.log 2>&1 &
    fi
fi

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

# ═══════════════════════════════════════════════════════════════════════════
# Background maintenance: auto-index and realm-retag
# ═══════════════════════════════════════════════════════════════════════════

# Determine plugin directory (for script paths)
PLUGIN_DIR="${CC_SOUL_PLUGIN_DIR:-$(dirname "$(dirname "$(realpath "${BASH_SOURCE[0]}")")")}"

# Auto-index codebase (10-minute rate limit built into script)
if [[ -n "$PROJECT_DIR" && -d "$PROJECT_DIR" ]]; then
    AUTO_INDEX_SCRIPT="$PLUGIN_DIR/scripts/auto-index.sh"
    if [[ -x "$AUTO_INDEX_SCRIPT" ]]; then
        (cd "$PROJECT_DIR" && "$AUTO_INDEX_SCRIPT") &
        disown
    fi
fi

# Realm retag (daily rate limit built into script)
REALM_RETAG_SCRIPT="$PLUGIN_DIR/scripts/realm-retag.sh"
if [[ -x "$REALM_RETAG_SCRIPT" ]]; then
    "$REALM_RETAG_SCRIPT" &
    disown
fi

# Queue transcript registration (fire-and-forget)
if [[ -n "$TRANSCRIPT_PATH" && -f "$TRANSCRIPT_PATH" ]]; then
    queue_write "transcript_register" "{\"session_id\":\"$SESSION_ID\",\"transcript_path\":$(echo "$TRANSCRIPT_PATH" | jq -Rs .),\"realm\":\"$REALM\"}"

    # Trigger distillation of any pending un-distilled transcripts (handles post-compaction case)
    queue_write "distill_trigger" "{\"session_id\":\"$SESSION_ID\"}"
fi

# Archive orphaned distillation staging files (one-time cleanup)
STAGING_DIR="$HOME/.claude/mind/.distill_staging"
if [[ -d "$STAGING_DIR" ]] && ls "$STAGING_DIR"/*.json >/dev/null 2>&1; then
    mkdir -p "$STAGING_DIR/archive"
    mv "$STAGING_DIR"/*.json "$STAGING_DIR/archive/" 2>/dev/null || true
fi

# Register session in cross-session messaging registry
# Use CLI instead of netcat (netcat to socket doesn't work reliably)
# PPID = Claude's PID (parent of this hook script)
if [[ -n "$SESSION_ID" ]]; then
    CLAUDE_PID=${PPID:-$$}
    chitta session_register --session_id "$SESSION_ID" --realm "$REALM" --pid "$CLAUDE_PID" >/dev/null 2>&1 || true

    # Export session environment variables for other processes
    SESSION_ENV_FILE="$HOME/.claude/mind/.session_env_$$"
    mkdir -p "$(dirname "$SESSION_ENV_FILE")"
    cat > "$SESSION_ENV_FILE" << EOF
export CLAUDE_SESSION_ID="$SESSION_ID"
export CLAUDE_TRANSCRIPT_PATH="$TRANSCRIPT_PATH"
export CLAUDE_REALM="$REALM"
export CLAUDE_PID="$CLAUDE_PID"
EOF
    chmod 600 "$SESSION_ENV_FILE"
fi

# Retry failed observations from previous sessions
FAILED_OBS_FILE="$HOME/.claude/mind/.failed_observations.jsonl"
if [[ -f "$FAILED_OBS_FILE" && -s "$FAILED_OBS_FILE" ]]; then
    TEMP_FAILED=$(mktemp)
    while IFS= read -r line; do
        [[ -z "$line" ]] && continue
        category=$(echo "$line" | jq -r '.category // "general"')
        content=$(echo "$line" | jq -r '.content // empty')
        if [[ -n "$content" ]]; then
            if timeout "$MAX_WAIT" "$CHITTA_BIN" observe --category "$category" --content "$content" >/dev/null 2>&1; then
                : # Success, don't add to temp file
            else
                echo "$line" >> "$TEMP_FAILED"
            fi
        fi
    done < "$FAILED_OBS_FILE"
    # Replace original with remaining failures (or remove if empty)
    if [[ -s "$TEMP_FAILED" ]]; then
        mv "$TEMP_FAILED" "$FAILED_OBS_FILE"
    else
        rm -f "$FAILED_OBS_FILE" "$TEMP_FAILED"
    fi
fi

# ═══════════════════════════════════════════════════════════════════════════
# Load and inject session state
# ═══════════════════════════════════════════════════════════════════════════

# Get full ledger entry (not just summary)
# ledger_load returns the most recent entry for the project
LEDGER_JSON=$(timeout "$MAX_WAIT" "$CHITTA_BIN" ledger_load --project "$REALM" --json 2>/dev/null || echo "{}")

# Check if this is a post-compaction session (mood = pre-compact)
MOOD=$(echo "$LEDGER_JSON" | jq -r '.mood // empty')

if [[ "$MOOD" == "pre-compact" ]]; then
    # This is a continuation after compaction - inject full state
    echo ""
    echo "[session-restored]"

    # Active files
    ACTIVE_FILES=$(echo "$LEDGER_JSON" | jq -r '.active_files // [] | .[]' 2>/dev/null)
    if [[ -n "$ACTIVE_FILES" ]]; then
        echo "Files in context:"
        echo "$ACTIVE_FILES" | while read -r f; do
            [[ -n "$f" ]] && echo "  - $f"
        done
    fi

    # Decisions made
    DECISIONS=$(echo "$LEDGER_JSON" | jq -r '.decisions // [] | .[]' 2>/dev/null)
    if [[ -n "$DECISIONS" ]]; then
        echo ""
        echo "Decisions made:"
        echo "$DECISIONS" | while read -r d; do
            [[ -n "$d" ]] && echo "  - $d"
        done
    fi

    # Pending tasks
    TODOS=$(echo "$LEDGER_JSON" | jq -r '.todos // [] | .[] | "[\(.status)] \(.content)"' 2>/dev/null)
    if [[ -n "$TODOS" ]]; then
        echo ""
        echo "Tasks:"
        echo "$TODOS" | while read -r t; do
            [[ -n "$t" ]] && echo "  $t"
        done
    fi

    # Blockers
    BLOCKERS=$(echo "$LEDGER_JSON" | jq -r '.blockers // [] | .[]' 2>/dev/null)
    if [[ -n "$BLOCKERS" ]]; then
        echo ""
        echo "Blockers:"
        echo "$BLOCKERS" | while read -r b; do
            [[ -n "$b" ]] && echo "  ! $b"
        done
    fi

    # Discoveries
    DISCOVERIES=$(echo "$LEDGER_JSON" | jq -r '.discoveries // [] | .[]' 2>/dev/null)
    if [[ -n "$DISCOVERIES" ]]; then
        echo ""
        echo "Discoveries:"
        echo "$DISCOVERIES" | while read -r d; do
            [[ -n "$d" ]] && echo "  * $d"
        done
    fi

    # Snapshot (what we were doing)
    SNAPSHOT=$(echo "$LEDGER_JSON" | jq -r '.snapshot // empty')
    if [[ -n "$SNAPSHOT" && ${#SNAPSHOT} -gt 20 ]]; then
        echo ""
        echo "Last context:"
        echo "$SNAPSHOT" | head -c 500
        echo ""
    fi

    echo "[/session-restored]"
    echo ""
else
    # Normal session start - just show minimal info
    soul_output=$(timeout "$MAX_WAIT" "$CHITTA_BIN" soul_context 2>/dev/null || true)
    if [[ -n "$soul_output" ]]; then
        nodes=$(echo "$soul_output" | grep -oE 'Nodes: [0-9]+' | grep -oE '[0-9]+' || echo "0")
        triplets=$(echo "$soul_output" | grep -oE 'Triplets: [0-9]+' | grep -oE '[0-9]+' || echo "0")
        [[ "$nodes" != "0" ]] && echo "[soul] n=$nodes t=$triplets"
    fi

    # Quick ledger check
    if [[ -n "$LEDGER_JSON" && "$LEDGER_JSON" != "{}" ]]; then
        session=$(echo "$LEDGER_JSON" | jq -r '.session_id // empty')
        mood=$(echo "$LEDGER_JSON" | jq -r '.mood // empty')
        [[ -n "$session" ]] && echo "[ledger] $session ($mood)"
    fi

    # ===========================================
    # CORRECTIONS RECAP: Surface recent high-priority corrections
    # ===========================================
    corrections=$(timeout "$MAX_WAIT" "$CHITTA_BIN" recall --query "correction" --tag "correction" --limit 3 --text-only 2>/dev/null | head -c 600 || true)
    if [[ -n "$corrections" && "$corrections" != *"No memories"* ]]; then
        echo ""
        echo "[recent-corrections]"
        echo "$corrections" | head -5
        echo "[/recent-corrections]"
    fi

    # Check for compliance failures (missed learning opportunities)
    compliance=$(timeout "$MAX_WAIT" "$CHITTA_BIN" recall --query "compliance fail" --tag "compliance" --limit 2 --text-only 2>/dev/null | head -c 300 || true)
    if [[ -n "$compliance" && "$compliance" != *"No memories"* ]]; then
        echo ""
        echo "⚠️ [compliance-issues] Recent missed corrections - be more proactive!"
    fi

    # ===========================================
    # MEMORY.MD MERGE: Import Claude Code auto-memory into chitta
    # ===========================================
    SANITIZED_PATH=$(echo "$PROJECT_DIR" | sed 's|/|-|g')
    MEMORY_FILE="$HOME/.claude/projects/$SANITIZED_PATH/memory/MEMORY.md"

    if [[ -f "$MEMORY_FILE" ]]; then
        MEMORY_CONTENT=$(cat "$MEMORY_FILE" 2>/dev/null || true)

        # Skip if empty or only contains chitta auto-synced content
        if [[ -n "$MEMORY_CONTENT" && "$MEMORY_CONTENT" != *"Chitta Soul Memories (auto-synced)"* ]] || \
           [[ "$MEMORY_CONTENT" == *"## Project Notes"* ]]; then

            # Extract user-added content (after "## Project Notes" or before "Chitta Soul")
            USER_CONTENT=""
            if [[ "$MEMORY_CONTENT" == *"## Project Notes"* ]]; then
                USER_CONTENT=$(echo "$MEMORY_CONTENT" | sed -n '/## Project Notes/,/---/p' | grep -v "^##" | grep -v "^---" | head -20)
            elif [[ "$MEMORY_CONTENT" != *"Chitta Soul"* ]]; then
                # No chitta section, entire file is user content
                USER_CONTENT=$(echo "$MEMORY_CONTENT" | grep -v "^#" | head -20)
            fi

            if [[ -n "$USER_CONTENT" && ${#USER_CONTENT} -gt 10 ]]; then
                # Import into chitta (fire-and-forget via queue)
                PROJECT_NAME=$(basename "$PROJECT_DIR")
                IMPORT_SSL="[memory:$PROJECT_NAME] Claude Code MEMORY.md import\n$USER_CONTENT"
                queue_write "remember" "{\"content\":$(echo -e "$IMPORT_SSL" | jq -Rs .),\"tags\":[\"memory-import\",\"$PROJECT_NAME\"]}"
                echo "[soul] imported MEMORY.md content" >&2
            fi
        fi
    fi
fi

exit 0
