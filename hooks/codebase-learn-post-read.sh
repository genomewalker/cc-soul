#!/bin/bash
# One-shot PostToolUse hook for /codebase-learn skill (once: true)
#
# After codebase-learn runs, this fires once on the next Read tool use.
# It indexes the file's directory to capture any new symbols discovered
# during the learning session.

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
MAX_WAIT="${CC_SOUL_MAX_WAIT:-2}"

[[ ! -x "$CHITTA_BIN" ]] && exit 0

# Parse JSON input from PostToolUse
INPUT=$(cat)
FILE_PATH=$(echo "$INPUT" | jq -r '.tool_input.file_path // empty' 2>/dev/null)

[[ -z "$FILE_PATH" ]] && exit 0
[[ ! -f "$FILE_PATH" ]] && exit 0

# Source shared library for queue_write
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/lib.sh"

# Index the file's directory (fire-and-forget)
DIR_PATH=$(dirname "$FILE_PATH")
REALM=$(timeout "$MAX_WAIT" "$CHITTA_BIN" realm_detect 2>/dev/null || echo "brahman")

queue_write "learn_codebase" "{\"path\":\"$DIR_PATH\",\"project\":\"$REALM\"}"

# Emit as JSON PostToolUse additionalContext
cat <<EOF
{
  "hookSpecificOutput": {
    "hookEventName": "PostToolUse",
    "additionalContext": "[codebase-learn] Indexed $(basename "$DIR_PATH") after skill run"
  }
}
EOF

exit 0
