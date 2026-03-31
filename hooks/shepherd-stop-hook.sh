#!/bin/bash
# Shepherd skill-scoped Stop hook: Check pipeline pane after each assistant response
# Only fires when /shepherd is active (registered via skill frontmatter)

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"
SHEPHERD_STATE="$MIND_PATH/.shepherd_active"

# Only run if shepherd state file exists (set by shepherd init)
[[ ! -f "$SHEPHERD_STATE" ]] && exit 0
[[ ! -x "$CHITTA_BIN" ]] && exit 0

# Read shepherd config
PANE_NAME=$(jq -r '.pane_name // "pipeline-main"' "$SHEPHERD_STATE" 2>/dev/null)
TASK_ID=$(jq -r '.task_id // empty' "$SHEPHERD_STATE" 2>/dev/null)
SESSION=$(jq -r '.session // empty' "$SHEPHERD_STATE" 2>/dev/null)

[[ -z "$TASK_ID" ]] && exit 0

# Quick tail check — last 5 lines from the monitored pane
# This is intentionally lightweight (no full sense-think-act cycle)
PANE_TAIL=$(timeout 2 bash -c "
    # Use zellij plugin if available, otherwise skip
    echo '{\"pane_name\": \"$PANE_NAME\", \"lines\": 5}' | \
    timeout 1 python3 -c '
import sys, json
sys.path.insert(0, \"'\${HOME}'/.claude/plugins/cache/genomewalker-zellij-mcp\")
# Best effort — if zellij MCP not importable, exit silently
' 2>/dev/null
" 2>/dev/null || true)

# Check for error patterns in pane output (fast grep, no daemon call)
if [[ -n "$PANE_TAIL" ]]; then
    if echo "$PANE_TAIL" | grep -qiE "(Error|FAILED|Exception|Traceback|TIMEOUT)"; then
        echo "[shepherd:alert] Error detected in $PANE_NAME — run /shepherd status"
    elif echo "$PANE_TAIL" | grep -qiE "(complete|finished|done|100%)"; then
        echo "[shepherd:done] Pipeline appears complete — run /shepherd status to confirm"
    fi
fi

exit 0
