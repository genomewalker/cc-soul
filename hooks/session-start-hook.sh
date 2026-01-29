#!/bin/bash
# SessionStart hook: Initialize soul context
#
# HIGH PERFORMANCE: Queue writes, minimal reads with short timeout

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

# Minimal soul context (single quick read)
soul_output=$(timeout "$MAX_WAIT" "$CHITTA_BIN" soul_context 2>/dev/null || true)
if [[ -n "$soul_output" ]]; then
    # Parse from text output: "Total nodes: X"
    nodes=$(echo "$soul_output" | grep -oE 'Total nodes: [0-9]+' | grep -oE '[0-9]+' || echo "0")
    triplets=$(echo "$soul_output" | grep -oE 'Triplets: [0-9]+' | grep -oE '[0-9]+' || echo "0")
    [[ "$nodes" != "0" ]] && echo "[soul] n=$nodes t=$triplets"
fi

# Quick ledger check (single read)
ledger_output=$(timeout "$MAX_WAIT" "$CHITTA_BIN" ledger_load --project "$REALM" 2>/dev/null || true)
if echo "$ledger_output" | grep -q "Session:"; then
    session=$(echo "$ledger_output" | grep -oE 'Session: [^ ]+' | cut -d' ' -f2)
    mood=$(echo "$ledger_output" | grep -oE 'Mood: [^ ]+' | cut -d' ' -f2)
    [[ -n "$session" ]] && echo "[ledger] $session ($mood)"
fi

exit 0
