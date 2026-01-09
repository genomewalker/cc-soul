# CC-Soul Hooks System

CC-Soul integrates with Claude Code through the hooks system, enabling automatic context injection and lifecycle management.

---

## Table of Contents

- [Overview](#overview)
- [Hook Events](#hook-events)
- [Configuration](#configuration)
- [Scripts](#scripts)
- [Transparent Memory](#transparent-memory)
- [Subconscious Daemon](#subconscious-daemon)
- [Custom Hooks](#custom-hooks)
- [Troubleshooting](#troubleshooting)

---

## Overview

Hooks are shell commands that execute in response to Claude Code events. CC-Soul uses hooks for:

1. **Context injection** — Soul state appears automatically
2. **Transparent memory** — Relevant memories surface without explicit commands
3. **Session continuity** — State saved and restored across sessions
4. **Background processing** — Subconscious daemon management

### How It Works

```
┌───────────────────────────────────────────────────────────────┐
│                      CLAUDE CODE                               │
│                                                                │
│  Event: UserPromptSubmit                                      │
│         │                                                      │
│         ▼                                                      │
│  ┌─────────────────────────────────────────────────────────┐  │
│  │                     HOOKS SYSTEM                         │  │
│  │                                                          │  │
│  │  1. Read hooks.json                                      │  │
│  │  2. Match event to handlers                              │  │
│  │  3. Execute commands                                     │  │
│  │  4. Inject output into context                           │  │
│  │                                                          │  │
│  └─────────────────────────────────────────────────────────┘  │
│         │                                                      │
│         ▼                                                      │
│  ┌─────────────────────────────────────────────────────────┐  │
│  │                   CC-SOUL SCRIPTS                        │  │
│  │                                                          │  │
│  │  soul-hook.sh prompt --lean --resonate                   │  │
│  │         │                                                │  │
│  │         ▼                                                │  │
│  │  1. Extract user message from stdin                      │  │
│  │  2. Run full_resonate(message)                           │  │
│  │  3. Output relevant memories                             │  │
│  │                                                          │  │
│  └─────────────────────────────────────────────────────────┘  │
│         │                                                      │
│         ▼                                                      │
│  Output injected as <system-reminder>                         │
│                                                                │
└───────────────────────────────────────────────────────────────┘
```

---

## Hook Events

CC-Soul responds to these Claude Code events:

### SessionStart

**When:** Claude Code starts or resumes a session

**Triggers:** `startup`, `resume`, `clear`, `compact`

**What CC-Soul Does:**
1. Auto-install binaries if needed
2. Load previous session ledger
3. Inject soul context
4. Start subconscious daemon

### SessionEnd

**When:** Claude Code session ends

**What CC-Soul Does:**
1. Save session ledger (Atman snapshot)
2. Record session statistics
3. Run maintenance cycle

### UserPromptSubmit

**When:** User sends a message

**What CC-Soul Does:**
1. Extract user message from stdin
2. Run `full_resonate(message)` to find relevant memories
3. Inject resonant memories as context
4. Show node statistics

### PostToolUse

**When:** After Bash, Write, or Edit tools execute

**What CC-Soul Does:**
1. Capture tool use for passive learning
2. Record significant operations as observations

### PreCompact

**When:** Before conversation context is compacted

**What CC-Soul Does:**
1. Save current state to ledger
2. Record that compaction is happening
3. Ensure nothing is lost

---

## Configuration

Hooks are defined in `hooks/hooks.json`:

```json
{
  "hooks": {
    "SessionStart": [
      {
        "matcher": "startup|resume|clear|compact",
        "hooks": [
          {
            "type": "command",
            "command": "${CLAUDE_PLUGIN_ROOT}/scripts/smart-install.sh",
            "timeout": 300
          },
          {
            "type": "command",
            "command": "${CLAUDE_PLUGIN_ROOT}/scripts/soul-hook.sh start"
          },
          {
            "type": "command",
            "command": "${CLAUDE_PLUGIN_ROOT}/scripts/subconscious.sh start",
            "timeout": 10
          }
        ]
      }
    ],
    "SessionEnd": [
      {
        "matcher": "",
        "hooks": [
          {
            "type": "command",
            "command": "${CLAUDE_PLUGIN_ROOT}/scripts/soul-hook.sh end"
          }
        ]
      }
    ],
    "UserPromptSubmit": [
      {
        "matcher": "",
        "hooks": [
          {
            "type": "command",
            "command": "${CLAUDE_PLUGIN_ROOT}/scripts/soul-hook.sh prompt --lean --resonate",
            "timeout": 15
          },
          {
            "type": "command",
            "command": "${CLAUDE_PLUGIN_ROOT}/scripts/trigger-hook.sh",
            "timeout": 10
          }
        ]
      }
    ],
    "PostToolUse": [
      {
        "matcher": "Bash|Write|Edit",
        "hooks": [
          {
            "type": "command",
            "command": "${CLAUDE_PLUGIN_ROOT}/scripts/capture-hook.sh",
            "timeout": 10
          }
        ]
      }
    ],
    "PreCompact": [
      {
        "matcher": "",
        "hooks": [
          {
            "type": "command",
            "command": "${CLAUDE_PLUGIN_ROOT}/scripts/soul-hook.sh pre-compact"
          }
        ]
      }
    ]
  }
}
```

### Configuration Options

| Field | Description |
|-------|-------------|
| `matcher` | Regex pattern to filter events (empty = match all) |
| `type` | Hook type: `command` |
| `command` | Shell command to execute |
| `timeout` | Timeout in seconds (optional) |

### Variables

| Variable | Value |
|----------|-------|
| `${CLAUDE_PLUGIN_ROOT}` | Plugin installation directory |

---

## Scripts

### soul-hook.sh

Main hook handler for soul operations.

**Usage:**
```bash
soul-hook.sh <command> [options]

Commands:
  start         Session start (load context, ledger)
  end           Session end (save ledger, stats)
  prompt        User prompt (inject context)
  pre-compact   Before compaction (save state)

Options:
  --lean        Minimal output (stats only)
  --resonate    Run full_resonate on user message
```

**How prompt --lean --resonate works:**

```bash
# 1. Read user message from stdin (JSON)
input=$(cat)
user_message=$(echo "$input" | jq -r '.message // .prompt // .content')

# 2. Quick stats
echo "[cc-soul] Nodes: $nodes ($hot hot)"

# 3. If message is substantial, run resonance
if [[ ${#user_message} -gt 10 ]]; then
    memories=$("$CHITTA_CLI" resonate "$user_message" --limit 3)
    if [[ -n "$memories" ]]; then
        echo ""
        echo "Resonant memories for this query:"
        echo "$memories"
    fi
fi
```

### subconscious.sh

Daemon management script.

**Usage:**
```bash
subconscious.sh <command>

Commands:
  start         Start daemon if not running
  stop          Stop running daemon
  status        Check daemon status
```

**Environment:**
| Variable | Description | Default |
|----------|-------------|---------|
| `SUBCONSCIOUS_INTERVAL` | Cycle interval in seconds | `60` |

### smart-install.sh

Auto-installation script.

**What it does:**
1. Detects current installed version
2. Compares with repository version
3. Downloads pre-built binaries or builds from source
4. Updates models if needed

### capture-hook.sh

Passive learning from tool use.

**What it captures:**
- Bash commands (significant ones)
- File edits
- File writes

### trigger-hook.sh

Process trigger patterns in user messages.

---

## Transparent Memory

The key innovation is **transparent memory** — memories that surface automatically without explicit tool calls.

### How It Works

1. **User sends message**: "How should I handle rate limiting?"

2. **Hook receives JSON on stdin**:
   ```json
   {
     "message": "How should I handle rate limiting?"
   }
   ```

3. **soul-hook.sh extracts message and runs resonance**:
   ```bash
   chitta_cli resonate "How should I handle rate limiting?" --limit 3
   ```

4. **Output injected into Claude's context**:
   ```
   Resonant memories for this query:
   [65%] In Project X, used exponential backoff for rate limiting...
   [52%] Rate limiting gotcha: always return 429, not 500...
   [48%] Redis INCR with EXPIRE for distributed rate limiting...
   ```

5. **Claude sees memories as part of the conversation**

### Benefits

- **No explicit recall needed** — memories just appear
- **Context-aware** — only relevant memories surface
- **Learning** — full_resonate applies Hebbian learning
- **Efficient** — 3 results, limited text

### Controlling Transparency

Edit `hooks/hooks.json` to modify behavior:

```json
// Disable resonance (stats only)
"command": "${CLAUDE_PLUGIN_ROOT}/scripts/soul-hook.sh prompt --lean"

// Enable resonance
"command": "${CLAUDE_PLUGIN_ROOT}/scripts/soul-hook.sh prompt --lean --resonate"

// Full context (verbose)
"command": "${CLAUDE_PLUGIN_ROOT}/scripts/soul-hook.sh prompt"
```

---

## Subconscious Daemon

The subconscious daemon runs background processing without consuming main context tokens.

### Lifecycle

```
SessionStart
     │
     ▼
subconscious.sh start
     │
     ├─▶ Check if already running (PID file)
     │
     ├─▶ Start chitta_cli daemon in background
     │
     └─▶ Log: "[subconscious] Started (pid=12345)"

(Daemon runs independently)
     │
     ├─▶ Every 60 seconds:
     │       ├── Apply decay
     │       ├── Synthesize wisdom
     │       ├── Apply feedback
     │       ├── Run attractor dynamics
     │       └── Save state
     │
     └─▶ Continues until killed or system shutdown
```

### Why Not Stop on SessionEnd?

The daemon keeps running even after Claude Code exits because:

1. **Brain-like behavior** — Your brain doesn't stop processing when you're not actively thinking
2. **Cross-session synthesis** — Wisdom can emerge between sessions
3. **Multi-instance support** — Daemon serves all Claude instances
4. **Resource efficiency** — Starting/stopping is more expensive than continuous running

### Managing the Daemon

```bash
# Check status
./scripts/subconscious.sh status

# Stop manually
./scripts/subconscious.sh stop

# View logs
tail -f ~/.claude/mind/.subconscious.log
```

---

## Custom Hooks

You can add your own hooks to extend CC-Soul.

### Adding a Custom Hook

1. Create your script in `scripts/`:

```bash
#!/bin/bash
# scripts/my-custom-hook.sh

# Read stdin if needed
input=$(cat)

# Do something
echo "[my-hook] Processing..."

# Output appears in Claude's context
```

2. Make it executable:

```bash
chmod +x scripts/my-custom-hook.sh
```

3. Add to `hooks/hooks.json`:

```json
{
  "hooks": {
    "UserPromptSubmit": [
      {
        "hooks": [
          // ... existing hooks ...
          {
            "type": "command",
            "command": "${CLAUDE_PLUGIN_ROOT}/scripts/my-custom-hook.sh",
            "timeout": 5
          }
        ]
      }
    ]
  }
}
```

### Hook Input

Hooks receive JSON on stdin. Format varies by event:

**UserPromptSubmit:**
```json
{
  "message": "User's message text",
  "timestamp": 1704812345678
}
```

**PostToolUse:**
```json
{
  "tool": "Bash",
  "input": {"command": "ls -la"},
  "output": "...",
  "success": true
}
```

### Hook Output

- **stdout** — Injected as `<system-reminder>` in Claude's context
- **stderr** — Logged but not shown to Claude
- **Exit code** — Non-zero indicates error (logged)

---

## Troubleshooting

### Hooks Not Running

1. Check hooks.json syntax:
   ```bash
   jq . hooks/hooks.json
   ```

2. Verify scripts are executable:
   ```bash
   ls -la scripts/*.sh
   chmod +x scripts/*.sh
   ```

3. Check Claude Code plugin loading:
   ```bash
   claude --plugin-dir ./cc-soul
   ```

### Slow Hook Execution

Hooks have timeouts. If they're slow:

1. Increase timeout in hooks.json
2. Use `--fast` flag for CLI calls
3. Reduce number of results (--limit)

### Resonance Not Working

1. Check if Yantra is ready:
   ```bash
   chitta_cli stats | grep Yantra
   ```

2. Verify model files:
   ```bash
   ls -la chitta/models/
   ```

3. Test CLI directly:
   ```bash
   chitta_cli resonate "test query"
   ```

### Daemon Not Starting

1. Check for existing process:
   ```bash
   ps aux | grep chitta_cli
   ```

2. Check PID file:
   ```bash
   cat ~/.claude/mind/.subconscious.pid
   ```

3. View logs:
   ```bash
   cat ~/.claude/mind/.subconscious.log
   ```

### Context Not Appearing

1. Verify hook output manually:
   ```bash
   echo '{"message":"test"}' | ./scripts/soul-hook.sh prompt --lean --resonate
   ```

2. Check hook configuration in hooks.json

3. Look for errors in Claude Code output

---

*Hooks are the nervous system connecting soul to body.*
