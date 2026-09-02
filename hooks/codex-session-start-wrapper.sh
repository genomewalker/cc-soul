#!/bin/bash
# Codex-native SessionStart hook.
#
# IMPORTANT:
# - Does not delegate to Claude-oriented session-start-hook.sh
# - Performs Codex-specific side effects only
# - Always emits strict SessionStart JSON for Codex

# Headless bridge participant: it answers one prompt and exits, so there is no
# session to register and nothing to restore. This path costs ~10s warm, paid
# per participant per room turn, for state nobody reads.
if [[ -n "$CC_SOUL_HEADLESS" ]]; then
    cat >/dev/null
    printf '%s\n' '{}'
    exit 0
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/lib.sh"

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
INPUT="$(cat)"

SESSION_ID=$(echo "$INPUT" | jq -r '.session_id // empty' 2>/dev/null || echo "")
HOOK_SOURCE=$(echo "$INPUT" | jq -r '.source // "startup"' 2>/dev/null || echo "startup")
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"

# 1) Ensure subconscious/chittad bootstrap (Codex side effect)
if [[ -x "${SCRIPT_DIR}/subconscious.sh" ]]; then
    "${SCRIPT_DIR}/subconscious.sh" start >/dev/null 2>/dev/null || true
fi

# 2) Session-scoped cleanup for non-compact starts
if [[ "$HOOK_SOURCE" != "compact" ]]; then
    rm -f "$MIND_PATH/.session_active" "$MIND_PATH/.gaps_surfaced" 2>/dev/null || true
    rm -f "$MIND_PATH/.stop_dedup_"* 2>/dev/null || true
fi

# 3) Register the native Codex session and transcript in the shared multimodel
# registry. The adapter resolves a missing transcript_path by session ID and
# records the long-lived Codex ancestor PID, project, model, and client kind.
if [[ -n "$SESSION_ID" ]]; then
    printf '%s' "$INPUT" | registry_call 8 register --client codex
fi

# This hook injects no context. Codex's SessionStart output schema accepts an
# empty top-level object; using it avoids needless coupling to the nested
# hookSpecificOutput wire format.
printf '%s\n' '{}'
exit 0
