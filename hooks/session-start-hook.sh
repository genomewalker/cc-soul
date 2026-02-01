#!/bin/bash
# SessionStart hook: Initialize soul context with FULL state restoration
#
# LOSSLESS: Restores complete session state after compaction
# - Files that were being worked on
# - Decisions made
# - Tasks and progress
# - Blockers and discoveries

set -e

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
QUEUE_FILE="${CHITTA_QUEUE:-/tmp/chitta-queue.jsonl}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-2}"

# Parse JSON input
INPUT=$(cat)
SESSION_ID=$(echo "$INPUT" | jq -r '.session_id // empty')
TRANSCRIPT_PATH=$(echo "$INPUT" | jq -r '.transcript_path // empty')

# Check chitta CLI exists
[[ ! -x "$CHITTA_BIN" ]] && exit 0

# Queue write - fire and forget
queue_write() {
    local tool="$1" args="$2"
    echo "{\"tool\":\"$tool\",\"args\":$args,\"ts\":$(date +%s)}" >> "$QUEUE_FILE"
}

# Detect realm (quick, cached in CLI)
REALM=$(timeout "$MAX_WAIT" "$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")

# Queue transcript registration (fire-and-forget)
if [[ -n "$TRANSCRIPT_PATH" && -f "$TRANSCRIPT_PATH" ]]; then
    queue_write "transcript_register" "{\"session_id\":\"$SESSION_ID\",\"transcript_path\":$(echo "$TRANSCRIPT_PATH" | jq -Rs .),\"realm\":\"$REALM\"}"
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
fi

exit 0
