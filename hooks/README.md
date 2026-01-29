# cc-soul Hooks

Custom hooks for Claude Code integration.

## log-bash-history.sh

Logs Claude Code bash commands to `~/.claude_bash_history` with timestamps.

### Features
- Timestamps for each command
- Filters trivial read-only commands (ls, cat, head, etc.)
- Easy to search/grep

### Installation

The hook is installed automatically by `setup.sh`. To enable it, add to `~/.claude/settings.json`:

```json
{
  "hooks": {
    "PostToolUse": [
      {
        "matcher": "Bash",
        "hooks": [
          {
            "type": "command",
            "command": "~/.claude/hooks/log-bash-history.sh \"$CLAUDE_TOOL_INPUT_command\"",
            "async": true
          }
        ]
      }
    ]
  }
}
```

### Shell Aliases

Add to `~/.bashrc` or `~/.zshrc`:

```bash
# Claude Code bash history
alias ch='tail -50 ~/.claude_bash_history'      # Recent commands
chf() { grep -i "$1" ~/.claude_bash_history; }  # Search history
```

### Customization

Edit `~/.claude/hooks/log-bash-history.sh` to modify which commands are filtered:

```bash
SKIP_PATTERNS=(
    '^ls '
    '^pwd$'
    # Add patterns here
)
```
