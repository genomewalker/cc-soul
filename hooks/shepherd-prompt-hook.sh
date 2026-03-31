#!/bin/bash
# Shepherd skill-scoped UserPromptSubmit hook: Show pipeline status on each prompt
# Only fires when /shepherd is active (registered via skill frontmatter)

MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind}"
SHEPHERD_STATE="$MIND_PATH/.shepherd_active"

# Only run if shepherd state file exists
[[ ! -f "$SHEPHERD_STATE" ]] && exit 0

TASK_ID=$(jq -r '.task_id // empty' "$SHEPHERD_STATE" 2>/dev/null)
PANE_NAME=$(jq -r '.pane_name // "pipeline-main"' "$SHEPHERD_STATE" 2>/dev/null)

[[ -z "$TASK_ID" ]] && exit 0

# One-liner status reminder (uses almost zero tokens)
echo "[shepherd:active] Monitoring $PANE_NAME ($TASK_ID)"

exit 0
