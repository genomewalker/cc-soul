#!/bin/bash
# Log Claude Code bash commands to ~/.claude_bash_history
# Filters out trivial read-only commands to reduce noise
#
# Installation:
#   1. Copy to ~/.claude/hooks/log-bash-history.sh
#   2. chmod +x ~/.claude/hooks/log-bash-history.sh
#   3. Add to ~/.claude/settings.json:
#      "hooks": {
#        "PostToolUse": [{
#          "matcher": "Bash",
#          "hooks": [{
#            "type": "command",
#            "command": "~/.claude/hooks/log-bash-history.sh \"$CLAUDE_TOOL_INPUT_command\"",
#            "async": true
#          }]
#        }]
#      }
#   4. Add to ~/.bashrc:
#      alias ch='tail -50 ~/.claude_bash_history'
#      chf() { grep -i "$1" ~/.claude_bash_history; }

HISTORY_FILE="$HOME/.claude_bash_history"

# Codex PostToolUse sends JSON on stdin (no positional args). Older Claude
# wiring can pass the command as $1. Support both and always drain stdin to
# avoid upstream broken-pipe errors.
INPUT="$(cat)"
COMMAND="${1:-}"
if [[ -z "$COMMAND" && -n "$INPUT" ]]; then
    COMMAND="$(echo "$INPUT" | jq -r '.tool_input.command // empty' 2>/dev/null || true)"
fi

# Skip empty commands
[[ -z "$COMMAND" ]] && exit 0

# Skip trivial read-only commands (customize as needed)
SKIP_PATTERNS=(
    '^ls '
    '^ls$'
    '^pwd$'
    '^echo '
    '^cat '
    '^head '
    '^tail '
    '^wc '
    '^file '
    '^which '
    '^type '
    '^command -v'
    '^test '
    '^\['
)

for pattern in "${SKIP_PATTERNS[@]}"; do
    if [[ "$COMMAND" =~ $pattern ]]; then
        exit 0
    fi
done

# Log with timestamp
echo "# $(date '+%Y-%m-%d %H:%M:%S')" >> "$HISTORY_FILE"
echo "$COMMAND" >> "$HISTORY_FILE"

exit 0
