# cc-soul Hooks

Modular hooks for Claude Code integration. Each hook has a single responsibility.

## Hook Scripts

| Script | Hook Event | Purpose |
|--------|------------|---------|
| `session-start-hook.sh` | SessionStart | Inject soul context, load ledger, check active long tasks |
| `prompt-hook.sh` | UserPromptSubmit | Surface relevant memories via `full_resonate` |
| `pre-tool-hook.sh` | PreToolUse (Read\|Edit\|Write) | Inject code intel + memories before file operations |
| `stop-hook.sh` | Stop | Extract [LEARN] blocks, create triplets, auto-checkpoint |
| `pre-compact-hook.sh` | PreCompact | Save checkpoint before context compaction |
| `log-bash-history.sh` | PostToolUse (Bash) | Log bash commands to history file |

## Installation

Scripts are installed to `~/.claude/hooks/` by `setup.sh`. Add to `~/.claude/settings.json`:

```json
{
  "hooks": {
    "SessionStart": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "~/.claude/hooks/session-start-hook.sh",
            "timeout": 10,
            "statusMessage": "awakening…"
          }
        ]
      }
    ],
    "UserPromptSubmit": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "~/.claude/hooks/prompt-hook.sh",
            "timeout": 5,
            "statusMessage": "resonating…"
          }
        ]
      }
    ],
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "~/.claude/hooks/stop-hook.sh",
            "timeout": 10,
            "statusMessage": "integrating…"
          }
        ]
      }
    ],
    "PreCompact": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "~/.claude/hooks/pre-compact-hook.sh",
            "timeout": 5,
            "statusMessage": "checkpointing…"
          }
        ]
      }
    ],
    "PreToolUse": [
      {
        "matcher": "Read|Edit|Write",
        "hooks": [
          {
            "type": "command",
            "command": "~/.claude/hooks/pre-tool-hook.sh",
            "timeout": 3,
            "statusMessage": "recalling…"
          }
        ]
      }
    ],
    "PostToolUse": [
      {
        "matcher": "Bash",
        "hooks": [
          {
            "type": "command",
            "command": "~/.claude/hooks/log-bash-history.sh \"$CLAUDE_TOOL_INPUT_command\"",
            "timeout": 5,
            "statusMessage": "remembering…"
          }
        ]
      }
    ]
  }
}
```

## Hook Input/Output

All hooks receive JSON via stdin. See [Claude Code hooks reference](https://code.claude.com/docs/en/hooks) for full schema.

### SessionStart
- **Input**: `session_id`, `transcript_path`, `cwd`, `source`, `model`
- **Output**: stdout → injected as context

### UserPromptSubmit
- **Input**: `prompt` (user's query)
- **Output**: SSL-formatted memories:
```
[soul]
[85%:sol:uuid] cmake --build build --parallel for chitta
[72%:gotcha:uuid] realm_detect needs CHITTA_BIN set
```

### PreToolUse (Read|Grep|Glob|Task|WebSearch|WebFetch)
- **Input**: `tool_name`, `tool_input`, `transcript_path`, `session_id`
- **Features**: Thinking drift detection, code intel, file-specific memories
- **Output**: SSL-formatted context:
```
[code:Mind.hpp] Mind, SimpleMind, grow, decay
[85%:sol] cmake parallel builds
[drift:72%:gotcha] watch out for socket paths
```

### Stop
- **Input**: `transcript_path`, `stop_hook_active`, `session_id`
- **Extracts**: `[SOLUTION]`, `[GOTCHA]`, `[PREFERENCE]`, `[DECISION]`, `[FAILURE]`, `[PATTERN]`, `[LEARN]`
- **Feedback**: `[USED:uuid]` markers strengthen memories
- **Output**: JSON `{"decision": "block", "reason": "..."}` to prevent stopping

### PreCompact
- **Input**: `trigger` ("manual" or "auto")
- **Output**: stderr → shown in verbose mode

### PostToolUse (Bash)
- **Input**: `tool_name`, `tool_input`, `tool_response`
- **Output**: stderr → shown in verbose mode

## SSL Output Format

Compact, token-efficient format for injected context:

| Format | Meaning |
|--------|---------|
| `[conf%:type]` | Memory with confidence and type |
| `[conf%:type:uuid]` | Memory with UUID for feedback |
| `[code:file]` | Code intel symbols |
| `[drift:conf%:type]` | Memory from thinking drift detection |

**Types:** `sol` (solution), `gotcha`, `pref` (preference), `dec` (decision), `fail` (failure), `pat` (pattern), `insight`, `mem` (generic)

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `CHITTA_DB_PATH` | `~/.claude/mind/chitta` | Path to chitta database |
| `CHITTA_BIN` | `~/.claude/bin/chitta` | Path to chitta CLI |
| `CHITTAD_BIN` | `~/.claude/bin/chittad` | Path to chittad daemon |
| `CC_SOUL_MAX_WAIT` | `5` | RPC timeout in seconds |

## Testing

```bash
# Test session start
echo '{"session_id":"test","source":"startup"}' | ~/.claude/hooks/session-start-hook.sh

# Test prompt
echo '{"prompt":"how do I build chitta?"}' | ~/.claude/hooks/prompt-hook.sh

# Test stop (needs real transcript)
echo '{"transcript_path":"~/.claude/projects/.../session.jsonl"}' | ~/.claude/hooks/stop-hook.sh

# Test pre-compact
echo '{"trigger":"manual"}' | ~/.claude/hooks/pre-compact-hook.sh
```

## Shell Aliases

Add to `~/.bashrc` or `~/.zshrc`:

```bash
alias ch='tail -50 ~/.claude_bash_history'
chf() { grep -i "$1" ~/.claude_bash_history; }
```
