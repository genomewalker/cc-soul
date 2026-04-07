#!/bin/bash
# SessionStart hook for source=compact: Lightweight context restoration
#
# After compaction, Claude Code re-runs SessionStart hooks. The full session-start-hook.sh
# is expensive (topology queries, corrections, auto-index, realm-retag, behavioral probes).
# Post-compact we only need: ledger state + smart_context + session re-registration.

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-2}"
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/lib.sh"

# Parse JSON input
INPUT=$(cat)
SESSION_ID=$(echo "$INPUT" | jq -r '.session_id // empty')
TRANSCRIPT_PATH=$(echo "$INPUT" | jq -r '.transcript_path // empty')

[[ ! -x "$CHITTA_BIN" ]] && exit 0
daemon_available || exit 0

# Detect realm (fast, needed for ledger_load)
PROJECT_DIR=""
if [[ -n "$TRANSCRIPT_PATH" ]]; then
    # Reuse decode logic from session-start-hook.sh
    PROJECT_ENCODED=$(dirname "$TRANSCRIPT_PATH" | xargs basename)
    # Simple decode: try direct path reconstruction
    DECODED=$(echo "$PROJECT_ENCODED" | sed 's/^-/\//' | sed 's/-/\//g')
    [[ -d "$DECODED" ]] && PROJECT_DIR="$DECODED"
fi

if [[ -n "$PROJECT_DIR" && -d "$PROJECT_DIR" ]]; then
    REALM=$(cd "$PROJECT_DIR" && timeout "$MAX_WAIT" "$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")
else
    REALM=$(timeout "$MAX_WAIT" "$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")
fi

# Re-register session (PID survives compaction but session state needs refresh)
if [[ -n "$SESSION_ID" ]]; then
    CLAUDE_PID=${PPID:-$$}
    timeout "$MAX_WAIT" "$CHITTA_BIN" session_register --session_id "$SESSION_ID" --realm "$REALM" --pid "$CLAUDE_PID" >/dev/null 2>&1 || true
fi

# Re-register transcript
if [[ -n "$TRANSCRIPT_PATH" && -f "$TRANSCRIPT_PATH" ]]; then
    queue_write "transcript_register" "{\"session_id\":\"$SESSION_ID\",\"transcript_path\":$(echo "$TRANSCRIPT_PATH" | jq -Rs .),\"realm\":\"$REALM\"}"
fi

# Load ledger for state restoration
LEDGER_JSON=$(timeout "$MAX_WAIT" "$CHITTA_BIN" ledger_load --project "$REALM" --json 2>/dev/null || echo "{}")

# Build JSON output with additionalContext + watchPaths
CONTEXT_PARTS=()

# Ledger state restoration
if [[ -n "$LEDGER_JSON" && "$LEDGER_JSON" != "{}" ]]; then
    RESTORE_TEXT="[session-restored]"

    ACTIVE_FILES=$(echo "$LEDGER_JSON" | jq -r '.active_files // [] | .[]' 2>/dev/null)
    if [[ -n "$ACTIVE_FILES" ]]; then
        RESTORE_TEXT="${RESTORE_TEXT}\nFiles: $(echo "$ACTIVE_FILES" | tr '\n' ', ' | sed 's/, $//')"
    fi

    SNAPSHOT=$(echo "$LEDGER_JSON" | jq -r '.snapshot // empty')
    if [[ -n "$SNAPSHOT" && ${#SNAPSHOT} -gt 20 ]]; then
        RESTORE_TEXT="${RESTORE_TEXT}\nContext: ${SNAPSHOT:0:300}"
    fi

    DECISIONS=$(echo "$LEDGER_JSON" | jq -r '.decisions // [] | .[:3] | .[]' 2>/dev/null)
    if [[ -n "$DECISIONS" ]]; then
        RESTORE_TEXT="${RESTORE_TEXT}\nDecisions: $(echo "$DECISIONS" | tr '\n' '; ' | sed 's/; $//')"
    fi

    TODOS=$(echo "$LEDGER_JSON" | jq -r '.todos // [] | .[] | "[\(.status)] \(.content)"' 2>/dev/null)
    if [[ -n "$TODOS" ]]; then
        RESTORE_TEXT="${RESTORE_TEXT}\nTasks: $(echo "$TODOS" | head -3 | tr '\n' '; ' | sed 's/; $//')"
    fi

    BLOCKERS=$(echo "$LEDGER_JSON" | jq -r '.blockers // [] | .[:2] | .[]' 2>/dev/null)
    if [[ -n "$BLOCKERS" ]]; then
        RESTORE_TEXT="${RESTORE_TEXT}\nBlockers: $(echo "$BLOCKERS" | tr '\n' '; ')"
    fi

    DISCOVERIES=$(echo "$LEDGER_JSON" | jq -r '.discoveries // [] | .[:5] | .[]' 2>/dev/null)
    if [[ -n "$DISCOVERIES" ]]; then
        RESTORE_TEXT="${RESTORE_TEXT}\nDiscoveries: $(echo "$DISCOVERIES" | tr '\n' '; ' | sed 's/; $//')"
    fi

    RESTORE_TEXT="${RESTORE_TEXT}\n[/session-restored]"
    CONTEXT_PARTS+=("$RESTORE_TEXT")
fi

# Smart context for current work (fast mode)
SMART_CTX=$(timeout "$MAX_WAIT" "$CHITTA_BIN" smart_context --task "session continuation after compaction" --mode fast --limit 200 2>/dev/null || true)
if [[ -n "$SMART_CTX" && "$SMART_CTX" != *"No memories"* ]]; then
    CONTEXT_PARTS+=("[soul-compact]\n${SMART_CTX:0:400}\n[/soul-compact]")
fi

# Subagent budget carry-forward: warn if count is high post-compact
if [[ -n "$SESSION_ID" ]]; then
    AGENT_COUNT_FILE="$MIND_PATH/.subagent_count_${SESSION_ID}"
    AGENT_COUNT=$(cat "$AGENT_COUNT_FILE" 2>/dev/null || echo 0)
    if [[ $AGENT_COUNT -gt 15 ]]; then
        CONTEXT_PARTS+=("[token-budget] ${AGENT_COUNT} subagents spawned pre-compact. Consider batching remaining work or starting fresh with /recap.")
    fi
fi

# Emit as JSON hookSpecificOutput for SessionStart
if [[ ${#CONTEXT_PARTS[@]} -gt 0 ]]; then
    FULL_CONTEXT=""
    for part in "${CONTEXT_PARTS[@]}"; do
        FULL_CONTEXT="${FULL_CONTEXT}${part}\n"
    done

    # Build watchPaths for key project files
    WATCH_PATHS="[]"
    if [[ -n "$PROJECT_DIR" && -d "$PROJECT_DIR" ]]; then
        PATHS_ARRAY="["
        FIRST=true
        for f in Snakefile Nextfile pyproject.toml Cargo.toml CMakeLists.txt package.json go.mod; do
            if [[ -f "$PROJECT_DIR/$f" ]]; then
                $FIRST && FIRST=false || PATHS_ARRAY+=","
                PATHS_ARRAY+="\"$PROJECT_DIR/$f\""
            fi
        done
        PATHS_ARRAY+="]"
        WATCH_PATHS="$PATHS_ARRAY"
    fi

    # Emit JSON (stdout starts with { → Claude Code parses as hookSpecificOutput)
    printf '%s' "$(jq -n \
        --arg ctx "$(printf '%b' "$FULL_CONTEXT")" \
        --argjson wp "$WATCH_PATHS" \
        '{
            hookSpecificOutput: {
                hookEventName: "SessionStart",
                additionalContext: $ctx,
                watchPaths: $wp
            }
        }')"
else
    # No context to restore — emit minimal JSON
    printf '%s' '{"hookSpecificOutput":{"hookEventName":"SessionStart"}}'
fi

exit 0
