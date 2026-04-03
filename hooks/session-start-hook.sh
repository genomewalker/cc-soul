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
# Claude Code provides: session_id, transcript_path, source (startup|resume|clear|compact)
INPUT=$(cat)
SESSION_ID=$(echo "$INPUT" | jq -r '.session_id // empty')
TRANSCRIPT_PATH=$(echo "$INPUT" | jq -r '.transcript_path // empty')
HOOK_SOURCE=$(echo "$INPUT" | jq -r '.source // "startup"')

# Clean stale per-session sentinels — but NOT on compact (same session continues)
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"
if [[ "$HOOK_SOURCE" != "compact" ]]; then
    rm -f "$MIND_PATH/.session_active" "$MIND_PATH/.gaps_surfaced"
    rm -f "$MIND_PATH/.stop_dedup_"* 2>/dev/null || true
fi

# Initialize turn-discipline counter to current turn so the discipline nudge
# measures idle turns within THIS session, not across session boundaries.
if [[ -n "$SESSION_ID" ]]; then
    TURN_FILE="${MIND_PATH}/.turn_index_${SESSION_ID}"
    CURRENT_TURN=$(cat "$TURN_FILE" 2>/dev/null || echo 0)
    echo "$CURRENT_TURN" > "${MIND_PATH}/.last_store_turn_${SESSION_ID}"
fi

# Check chitta CLI exists and daemon is running
[[ ! -x "$CHITTA_BIN" ]] && exit 0
daemon_available || exit 0


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
        (cd "$PROJECT_DIR" && "$AUTO_INDEX_SCRIPT") </dev/null >/dev/null 2>&1 &
        disown
    fi
fi

# Realm retag (daily rate limit built into script)
REALM_RETAG_SCRIPT="$PLUGIN_DIR/scripts/realm-retag.sh"
if [[ -x "$REALM_RETAG_SCRIPT" ]]; then
    "$REALM_RETAG_SCRIPT" </dev/null >/dev/null 2>&1 &
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
    timeout "$MAX_WAIT" chitta session_register --session_id "$SESSION_ID" --realm "$REALM" --pid "$CLAUDE_PID" >/dev/null 2>&1 || true

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

# Check if this is a post-compaction session
# Primary signal: Claude Code passes source="compact" in hook input (authoritative)
# Fallback: ledger mood="pre-compact" (requires daemon to have saved checkpoint before compact)
MOOD=$(echo "$LEDGER_JSON" | jq -r '.mood // empty')
IS_POST_COMPACT=false
[[ "$HOOK_SOURCE" == "compact" ]] && IS_POST_COMPACT=true
[[ "$MOOD" == "pre-compact" ]] && IS_POST_COMPACT=true

if [[ "$IS_POST_COMPACT" == "true" ]]; then
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
        memories=$(echo "$soul_output" | grep -oE 'Memory: [0-9]+' | grep -oE '[0-9]+' || echo "0")
        triplets=$(echo "$soul_output" | grep -oE '[0-9]+ triplets' | grep -oE '[0-9]+' || echo "0")
        [[ "$memories" != "0" ]] && echo "[soul] m=$memories t=$triplets"
    fi

    # Quick ledger check
    if [[ -n "$LEDGER_JSON" && "$LEDGER_JSON" != "{}" ]]; then
        session=$(echo "$LEDGER_JSON" | jq -r '.session_id // empty')
        mood=$(echo "$LEDGER_JSON" | jq -r '.mood // empty')
        [[ -n "$session" ]] && echo "[ledger] $session ($mood)"
    fi

    # ═══════════════════════════════════════════
    # TOPOLOGY: Structural map of memory state
    # ═══════════════════════════════════════════
    TOPOLOGY_PARTS=()

    # Top 3 active themes (show representative memory content, not internal UUID names)
    theme_out=$(timeout "$MAX_WAIT" "$CHITTA_BIN" sql_query \
        --query "SELECT t.memory_count, substr(m.content, 1, 60) as label FROM theme t JOIN memory m ON t.representative_id = m.id WHERE t.memory_count > 0 ORDER BY t.updated_at DESC LIMIT 3" \
        --json 2>/dev/null || true)
    if [[ -n "$theme_out" ]]; then
        theme_str=$(echo "$theme_out" | jq -r '[.rows[]? | "\(.memory_count)m: \(.label)"] | join(" | ")' 2>/dev/null || true)
        [[ -n "$theme_str" ]] && TOPOLOGY_PARTS+=("Themes: $theme_str")
    fi

    # Memory kind distribution (most active)
    kind_out=$(timeout "$MAX_WAIT" "$CHITTA_BIN" sql_query \
        --query "SELECT kind, COUNT(*) as cnt FROM memory WHERE access_count > 1 GROUP BY kind ORDER BY cnt DESC LIMIT 6" \
        --json 2>/dev/null || true)
    if [[ -n "$kind_out" ]]; then
        kind_str=$(echo "$kind_out" | jq -r '[.rows[]? | "\(.kind):\(.cnt)"] | join(", ")' 2>/dev/null || true)
        [[ -n "$kind_str" ]] && TOPOLOGY_PARTS+=("Active: $kind_str")
    fi

    # Recent project memories (only if REALM is set and not brahman)
    if [[ -n "${REALM:-}" && "${REALM}" != "brahman" ]]; then
        recent_out=$(timeout "$MAX_WAIT" "$CHITTA_BIN" sql_query \
            --query "SELECT id, kind, content FROM memory WHERE realm = '${REALM}' ORDER BY accessed_at DESC LIMIT 3" \
            --json 2>/dev/null || true)
        if [[ -n "$recent_out" ]]; then
            recent_str=$(echo "$recent_out" | jq -r '.rows[]? | "#\(.id) [\(.kind)] \(.content[0:80])"' 2>/dev/null || true)
            if [[ -n "$recent_str" ]]; then
                TOPOLOGY_PARTS+=("Recent (${REALM}):")
                while IFS= read -r line; do
                    [[ -n "$line" ]] && TOPOLOGY_PARTS+=("  $line")
                done <<< "$recent_str"
            fi
        fi
    fi

    # Summary counts
    count_out=$(timeout "$MAX_WAIT" "$CHITTA_BIN" sql_query \
        --query "SELECT COUNT(*) as total, COUNT(CASE WHEN priority_tier = 2 THEN 1 END) as critical, COUNT(CASE WHEN pinned = true THEN 1 END) as pinned FROM memory" \
        --json 2>/dev/null || true)
    if [[ -n "$count_out" ]]; then
        total_mem=$(echo "$count_out" | jq -r '.rows[0]?.total // 0' 2>/dev/null || echo "0")
        critical_mem=$(echo "$count_out" | jq -r '.rows[0]?.critical // 0' 2>/dev/null || echo "0")
        pinned_mem=$(echo "$count_out" | jq -r '.rows[0]?.pinned // 0' 2>/dev/null || echo "0")
        [[ "$total_mem" != "0" ]] && TOPOLOGY_PARTS+=("Total: ${total_mem} memories (${critical_mem} critical, ${pinned_mem} pinned)")
    fi

    # Emit topology block
    if [[ ${#TOPOLOGY_PARTS[@]} -gt 0 ]]; then
        echo ""
        echo "[topology]"
        for part in "${TOPOLOGY_PARTS[@]}"; do
            echo "$part"
        done
        echo "[/topology]"
    fi

    # ===========================================
    # CORRECTIONS RECAP: Surface recent corrections (with suppression after N surfaces)
    # Corrections tagged 'wontfix' or 'verified' are suppressed.
    # Others are suppressed after CORRECTION_MAX_SURFACES sessions without action.
    # ===========================================
    CORRECTION_MAX_SURFACES=5
    SURFACE_COUNT_FILE="${MIND_PATH}/.correction_surfaces"
    touch "$SURFACE_COUNT_FILE" 2>/dev/null

    corrections_raw=$(timeout "$MAX_WAIT" "$CHITTA_BIN" recall --query "correction" --tag "correction" --limit 5 --json 2>/dev/null || true)
    if [[ -n "$corrections_raw" && "$corrections_raw" != *"No memories"* ]]; then
        corrections_out=""
        while IFS= read -r corr_line; do
            [[ -z "$corr_line" ]] && continue
            corr_id=$(echo "$corr_line" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('id',''))" 2>/dev/null || true)
            corr_text=$(echo "$corr_line" | python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d.get('text','')[:120])" 2>/dev/null || true)
            [[ -z "$corr_id" || -z "$corr_text" ]] && continue

            # Skip if tagged wontfix or verified
            tags=$(timeout 0.5 "$CHITTA_BIN" triplet_history --id "$corr_id" --predicate "tagged" --json 2>/dev/null | python3 -c "import sys,json; d=json.load(sys.stdin); print(' '.join(t.get('object','') for t in d.get('triplets',[])))" 2>/dev/null || echo "")
            [[ "$tags" =~ wontfix|verified ]] && continue

            # Track surface count
            current=$(grep -c "^${corr_id}$" "$SURFACE_COUNT_FILE" 2>/dev/null || echo "0")
            if [[ "$current" -ge "$CORRECTION_MAX_SURFACES" ]]; then
                continue  # Suppressed after N surfaces without action
            fi
            echo "$corr_id" >> "$SURFACE_COUNT_FILE"
            corrections_out="${corrections_out}${corr_text}\n"
        done <<< "$(echo "$corrections_raw" | python3 -c "import sys,json; d=json.load(sys.stdin); [print(json.dumps(r)) for r in d.get('results',[])]" 2>/dev/null)"

        if [[ -n "$corrections_out" ]]; then
            echo ""
            echo "[recent-corrections]"
            printf '%b' "$corrections_out" | head -5
            echo "[/recent-corrections]"
        fi
    fi

    # Check for compliance failures (missed learning opportunities)
    compliance=$(timeout "$MAX_WAIT" "$CHITTA_BIN" recall --query "compliance fail" --tag "compliance" --limit 2 --text-only 2>/dev/null | head -c 300 || true)
    if [[ -n "$compliance" && "$compliance" != *"No memories"* ]]; then
        echo ""
        echo "⚠️ [compliance-issues] Recent missed corrections - be more proactive!"
    fi

    # ===========================================
    # BEHAVIORAL PROBE NUDGE: Surface chronic behavioral patterns from recent sessions
    # Reads probe_signal triplets stored by the stop hook over the last 5 sessions.
    # If a pattern is chronic (>=3 occurrences), inject a direct behavioral nudge.
    # ===========================================
    _probe_nudge=""
    _probe_triplets=$(timeout 1 "$CHITTA_BIN" query_triplets --predicate "probe_signal" --limit 10 2>/dev/null || true)
    if [[ -n "$_probe_triplets" && "$_probe_triplets" != *"No triplets"* ]]; then
        _hedge_count=$(echo "$_probe_triplets" | grep -c "hedging" || true)
        _syco_count=$(echo "$_probe_triplets" | grep -c "sycophantic" || true)
        _shallow_count=$(echo "$_probe_triplets" | grep -c "shallow" || true)

        if [[ "${_hedge_count:-0}" -ge 3 ]]; then
            _probe_nudge="${_probe_nudge}[probe] Chronic hedging detected (${_hedge_count} recent sessions) — be direct and assertive, drop qualifiers.\n"
        fi
        if [[ "${_syco_count:-0}" -ge 3 ]]; then
            _probe_nudge="${_probe_nudge}[probe] Chronic sycophancy detected (${_syco_count} recent sessions) — push back when needed, prioritize accuracy over agreement.\n"
        fi
        if [[ "${_shallow_count:-0}" -ge 3 ]]; then
            _probe_nudge="${_probe_nudge}[probe] Chronic shallow reasoning detected (${_shallow_count} recent sessions) — go deeper, think step by step before answering.\n"
        fi
    fi
    if [[ -n "$_probe_nudge" ]]; then
        echo ""
        echo -e "$_probe_nudge"
    fi

    # ===========================================
    # CACHE BREAK WARNING: Surface recent cache break detections
    # ===========================================
    _sus3_cb_warn=$(timeout "$MAX_WAIT" "$CHITTA_BIN" recall --query "cache break session" --tag "cache-break" --limit 1 --text-only 2>/dev/null | head -c 400 || true)
    if [[ -n "$_sus3_cb_warn" && "$_sus3_cb_warn" != *"No memories"* && "$_sus3_cb_warn" != *"0 memories"* ]]; then
        echo ""
        echo "⚠️ BEFORE RUNNING: [cache] Recent cache break detected:"
        echo "$_sus3_cb_warn" | head -3
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

# ===========================================
# MSG-NOTIFY DAEMON: Launch background message polling for this session
# ===========================================
if [[ -n "$SESSION_ID" && "$SESSION_ID" != "default" ]]; then
    NOTIFY_SCRIPT="${SCRIPT_DIR}/../scripts/msg-notify.sh"
    if [[ -f "$NOTIFY_SCRIPT" ]]; then
        # Kill any stale notify daemon for this session
        PID_FILE="${MIND_PATH}/.msg_notify_pids/${SESSION_ID}.pid"
        if [[ -f "$PID_FILE" ]]; then
            old_pid=$(cat "$PID_FILE" 2>/dev/null || true)
            if [[ -n "$old_pid" ]]; then
                kill "$old_pid" 2>/dev/null || true
            fi
            rm -f "$PID_FILE"
        fi
        # Copy script to temp to avoid NFS lock on plugin marketplace dir
        NOTIFY_TMP="/tmp/msg-notify-$$.sh"
        cp "$NOTIFY_SCRIPT" "$NOTIFY_TMP"
        chmod +x "$NOTIFY_TMP"
        bash "$NOTIFY_TMP" "$SESSION_ID" 5 </dev/null >/dev/null 2>&1 &
        disown $! 2>/dev/null || true
        rm -f "$NOTIFY_TMP"
    fi
fi

# ===========================================
# WATCH PATHS: Register key project files for FileChanged hooks
# Only emits on startup/resume (compact-restore-hook handles its own)
# ===========================================
if [[ -n "$PROJECT_DIR" && -d "$PROJECT_DIR" ]]; then
    _wp_array="["
    _wp_first=true
    for _wp_f in Snakefile Nextfile pyproject.toml Cargo.toml CMakeLists.txt package.json go.mod Makefile; do
        if [[ -f "$PROJECT_DIR/$_wp_f" ]]; then
            $_wp_first && _wp_first=false || _wp_array+=","
            _wp_array+="\"$PROJECT_DIR/$_wp_f\""
        fi
    done
    _wp_array+="]"

    # If we have watchPaths, emit them as JSON hookSpecificOutput on fd 3
    # which we'll merge at exit. For now, save to temp file.
    if [[ "$_wp_array" != "[]" ]]; then
        echo "$_wp_array" > "$MIND_PATH/.watch_paths_$$"
    fi
fi

# ===========================================
# FINAL OUTPUT: Wrap in JSON if watchPaths exist, otherwise plain text passthrough
# Plain text was already emitted to stdout above. JSON hookSpecificOutput requires
# ALL stdout to be JSON (no mixed mode). Since session-start-hook emits plain text
# throughout, we only use JSON wrapper when we have watchPaths to register.
# ===========================================
_wp_file="$MIND_PATH/.watch_paths_$$"
if [[ -f "$_wp_file" ]]; then
    _wp_json=$(cat "$_wp_file")
    rm -f "$_wp_file"
    # Note: plain text was already printed to stdout. We can't retroactively wrap it.
    # Instead, emit the watchPaths as a separate line for potential future JSON parsing.
    # The FileChanged watcher is also registered by compact-restore-hook.sh (which does use JSON).
    # For startup/resume, register watchPaths via the daemon's file_watch RPC as fallback.
    queue_write "file_watch_register" "{\"session_id\":\"$SESSION_ID\",\"paths\":$_wp_json}" 2>/dev/null || true
fi

exit 0
