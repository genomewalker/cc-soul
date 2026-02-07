# CC-Soul Hooks System

CC-Soul integrates with Claude Code through the hooks system, enabling automatic context injection and lifecycle management.

---

## Table of Contents

- [Overview](#overview)
- [Hook Events](#hook-events)
- [Configuration](#configuration)
- [Scripts](#scripts)
- [Transparent Memory](#transparent-memory)
- [Proactive Learning](#proactive-learning)
- [Subconscious Daemon](#subconscious-daemon)
- [Custom Hooks](#custom-hooks)
- [Troubleshooting](#troubleshooting)

---

## Overview

Hooks are shell commands that execute in response to Claude Code events. CC-Soul uses hooks for:

1. **Context injection** — Soul state and relevant memories appear automatically
2. **Transparent memory** — Memories surface without explicit MCP calls
3. **Proactive learning** — Corrections, preferences, and milestones detected automatically
4. **Session continuity** — State saved and restored across sessions via ledger
5. **Anticipation** — Context→action patterns learned and predicted
6. **Background processing** — Subconscious daemon management

### Two Hook Systems

CC-Soul provides hooks in two forms:

| System | Files | Used When |
|--------|-------|-----------|
| **Plugin hooks** | `hooks/hooks.json` + `hooks/session-start-hook.sh, hooks/prompt-hook.sh, hooks/stop-hook.sh` | Installed as Claude Code plugin |
| **Settings hooks** | `hooks/*.sh` + `~/.claude/settings.json` | Standalone installation |

The `smart-install.sh` script configures the appropriate system automatically.

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
│  │  1. Read hooks config (plugin or settings.json)          │  │
│  │  2. Match event to handlers                              │  │
│  │  3. Execute scripts                                      │  │
│  │  4. Inject stdout into context                           │  │
│  └─────────────────────────────────────────────────────────┘  │
│         │                                                      │
│         ▼                                                      │
│  ┌─────────────────────────────────────────────────────────┐  │
│  │               PROMPT-HOOK.SH                              │  │
│  │                                                          │  │
│  │  1. Extract user message from stdin JSON                 │  │
│  │  2. Detect learning opportunities (corrections, prefs)   │  │
│  │  3. Run full_resonate(message) via daemon socket         │  │
│  │  4. Search code symbols if query is code-related         │  │
│  │  5. Run anticipation_predict for proactive suggestions   │  │
│  │  6. Output combined context (max ~500 chars)             │  │
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
1. Auto-install binaries if needed (`smart-install.sh`)
2. Start subconscious daemon (`subconscious.sh start`)
3. Detect current realm/project
4. Incremental code re-indexing (only changed files)
5. Auto-register transcript for distillation
6. Load soul context (nodes, triplets, confidence, status)
7. Surface user profile (expertise, preferences, style)
8. Load active goals
9. Surface behavioral corrections
10. Load ledger checkpoint (pending tasks, next steps, mood)
11. Detect git changes since last session

### SessionEnd / Stop

**When:** Claude Code session ends or Claude produces a response

**What CC-Soul Does (Stop hook — stop-hook.sh):**
1. Read Claude's response from stdin
2. Auto-detect missed learning opportunities:
   - Corrections the user made → auto-store via `remember`
   - Preferences expressed → auto-store via `remember`
   - Milestones detected → auto-store via `remember`
3. Record anticipation patterns (context→action)
4. Verify predictions from earlier anticipation
5. Auto-checkpoint every 5 turns or on meaningful work
6. Extract files, decisions, next steps, blockers from response
7. Save ledger with session state

**What CC-Soul Does (End hook — stop-hook.sh):**
1. Calculate session duration and node delta
2. Save session ledger (Ātman snapshot)
3. Record session observation (for sessions >1 minute)
4. Run maintenance cycle

### UserPromptSubmit

**When:** User sends a message

**What CC-Soul Does:**
1. Extract user message from stdin JSON
2. Context recovery: if user says "continue"/"resume", re-inject full soul context
3. Detect learning opportunities:
   - Correction patterns ("no", "actually", "that's wrong")
   - Preference patterns ("I prefer", "always", "never")
   - Frustration patterns ("stuck", "confused", "frustrated")
   - Milestone patterns ("it works", "shipped", "deployed")
4. Determine proactive surfacing tags (corrections, failures, decisions)
5. Run `full_resonate(query)` via daemon socket
6. Filter results: only ≥25% relevance, strip type prefixes, max 500 chars
7. Surface code symbols if query is code-related
8. Run `anticipation_predict` for context-based predictions
9. Output combined context

### PostToolUse

**When:** After Bash, Write, or Edit tools execute

**What CC-Soul Does:**
- **capture-hook.sh**: Currently disabled (exits immediately). Previously captured significant commands and file operations.
- **post-bash-hook.sh**: Records significant Edit/Write operations as signals for background learning.

### PreToolUse

**When:** Before Read, Edit, Write, or Bash tools execute

**What CC-Soul Does (pre-tool-hook.sh):**
- **Read**: Injects past memories about the file being read
- **Edit/Write**: Surfaces past decisions about the file being edited
- **Bash**: Detects command patterns (R, Python, git) and injects relevant corrections

### PreCompact

**When:** Before conversation context is compacted

**What CC-Soul Does:**
1. Save current state to ledger checkpoint
2. Record that compaction is happening
3. Ensure work state is preserved for continuation

---

## Configuration

### Plugin Mode (hooks.json)

When installed as a Claude Code plugin, hooks are defined in `hooks/hooks.json`:

```json
{
  "hooks": {
    "SessionStart": [
      {
        "matcher": "*",
        "hooks": [
          {"type": "command", "command": "${CLAUDE_PLUGIN_ROOT}/hooks/subconscious.sh start"},
          {"type": "command", "command": "${CLAUDE_PLUGIN_ROOT}/hooks/session-start-hook.sh"}
        ]
      }
    ],
    "UserPromptSubmit": [
      {
        "matcher": "*",
        "hooks": [
          {"type": "command", "command": "${CLAUDE_PLUGIN_ROOT}/hooks/prompt-hook.sh", "timeout": 10}
        ]
      }
    ],
    "Stop": [
      {
        "matcher": "*",
        "hooks": [
          {"type": "command", "command": "${CLAUDE_PLUGIN_ROOT}/hooks/stop-hook.sh"}
        ]
      }
    ],
    "PreCompact": [
      {
        "matcher": "*",
        "hooks": [
          {"type": "command", "command": "${CLAUDE_PLUGIN_ROOT}/hooks/pre-compact-hook.sh"}
        ]
      }
    ],
    "PreToolUse": [
      {
        "matcher": "Bash",
        "hooks": [
          {"type": "command", "command": "${CLAUDE_PLUGIN_ROOT}/hooks/pre-tool-hook.sh Bash", "timeout": 3}
        ]
      }
    ],
    "PostToolUse": [
      {
        "matcher": "Bash",
        "hooks": [
          {"type": "command", "command": "${CLAUDE_PLUGIN_ROOT}/hooks/post-bash-hook.sh", "timeout": 5}
        ]
      }
    ]
  }
}
```

### Settings Mode (~/.claude/settings.json)

When installed standalone, `smart-install.sh` configures hooks in settings.json:

```json
{
  "hooks": {
    "SessionStart": [{
      "matcher": "*",
      "hooks": [
        {"type": "command", "command": "~/.claude/hooks/subconscious.sh start"},
        {"type": "command", "command": "~/.claude/hooks/session-start-hook.sh"}
      ]
    }],
    "UserPromptSubmit": [{
      "matcher": "*",
      "hooks": [
        {"type": "command", "command": "~/.claude/hooks/prompt-hook.sh"}
      ]
    }],
    "Stop": [{
      "matcher": "*",
      "hooks": [
        {"type": "command", "command": "~/.claude/hooks/stop-hook.sh"}
      ]
    }],
    "PreCompact": [{
      "matcher": "*",
      "hooks": [
        {"type": "command", "command": "~/.claude/hooks/pre-compact-hook.sh"}
      ]
    }],
    "PreToolUse": [{
      "matcher": "Read|Edit|Write",
      "hooks": [
        {"type": "command", "command": "~/.claude/hooks/pre-tool-hook.sh", "timeout": 5}
      ]
    }]
  }
}
```

### Configuration Options

| Field | Description |
|-------|-------------|
| `matcher` | Regex pattern to filter events (empty or `*` = match all) |
| `type` | Hook type: `command` |
| `command` | Shell command to execute |
| `timeout` | Timeout in seconds (optional) |

### Variables

| Variable | Value |
|----------|-------|
| `${CLAUDE_PLUGIN_ROOT}` | Plugin installation directory |

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `CC_SOUL_LEAN` | `false` | Ultra-lean context mode (stats only) |
| `CC_SOUL_MAX_WAIT` | `5` | Max seconds to wait for daemon responses |
| `DEBUG_SOUL` | `0` | Enable debug output to stderr |
| `CHITTA_DB_PATH` | `~/.claude/mind` | Mind storage path |
| `SUBCONSCIOUS_INTERVAL` | `60` | Daemon cycle interval in seconds |

---

## Scripts

### Individual Hook Scripts

Each lifecycle event is handled by a dedicated script in the `hooks/` directory. All scripts use socket-first communication with the daemon.

| Script | Event | Purpose |
|--------|-------|---------|
| `session-start-hook.sh` | SessionStart | Realm detection, code re-indexing, soul context injection, ledger restore, git diff awareness |
| `prompt-hook.sh` | UserPromptSubmit | full_resonate search, code symbol injection, anticipation prediction, learning detection |
| `stop-hook.sh` | Stop | Auto-learning extraction, anticipation recording, ledger save, auto-checkpoint |
| `pre-compact-hook.sh` | PreCompact | Save ledger checkpoint before context compaction |
| `pre-tool-hook.sh` | PreToolUse | Surface file memories, past decisions, command corrections |
| `post-bash-hook.sh` | PostToolUse | Record significant file changes as signals |
| `distill.sh` | Background | Transcript distillation into compressed wisdom nodes |

**Communication:** All scripts use Unix domain socket to talk to daemon directly (fast path). Falls back to `chitta` thin client if socket unavailable. Socket path is derived from mind path using djb2 hash: `/tmp/chitta-{hash}.sock`.

**Key features:**
- Realm-aware (auto-detects project realm)
- Context recovery on "continue"/"resume" patterns
- Proactive learning hint injection
- Anticipation prediction and verification
- Code symbol injection for code-related queries
- Auto-checkpoint on meaningful work or every 5 turns

### subconscious.sh

Daemon management script with health checking.

**Usage:**
```bash
subconscious.sh <command>

Commands:
  start         Start daemon if not running (with responsiveness check)
  stop          Stop running daemon (graceful → force kill)
  restart       Stop then start
  status        Check daemon status (PID, socket, managed/MCP-spawned)
  health        Health check with auto-recovery
```

**Start sequence:**
1. Kill all existing daemon processes (prevents accumulation)
2. Check if any are responsive via socket ping
3. Acquire atomic start lock (`mkdir` for POSIX atomicity)
4. Launch `chittad daemon --path $MIND_PATH --interval $INTERVAL`
5. Wait for socket AND verify daemon responds with stats
6. Release lock

**Socket path:** `/tmp/chitta-{djb2_hash(MIND_PATH)}.sock`
**PID file:** `/tmp/chitta-{djb2_hash(MIND_PATH)}.pid`

### smart-install.sh

Auto-installation script that runs on SessionStart.

**What it does:**
1. Check if already installed (version marker)
2. Stop existing daemon (version mismatch safety)
3. Download ONNX model and vocabulary from HuggingFace
4. Try pre-built binaries (platform-specific tar.gz from GitHub releases)
5. Fall back to building from source (cmake + make)
6. Copy binaries to `~/.claude/bin/`
7. Install hook scripts to `~/.claude/hooks/`
8. Configure bash permissions in settings.json
9. Configure hooks in settings.json (if not plugin-managed)
10. Validate binaries and mark as installed

**Supported platforms:**
- `linux-x64`, `linux-arm64`
- `macos-x64`, `macos-arm64`

### capture-hook.sh

Currently **disabled** (exits immediately at line 16). Previously captured Bash commands, file writes, and edits as observations. Disabled because automatic capture creates noise — the soul now learns from Claude's typed markers (`[LEARN]`, `[SOLUTION]`, etc.) extracted by the stop hook, and from MCP tool calls.

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

3. **Hook runs resonance via daemon socket**:
   ```bash
   rpc_call "full_resonate" '{"query":"How should I handle rate limiting?","k":3}'
   ```

4. **Output injected into Claude's context**:
   ```
   In Project X, used exponential backoff for rate limiting...
   Rate limiting gotcha: always return 429, not 500...
   Redis INCR with EXPIRE for distributed rate limiting...
   ```

5. **Claude sees memories as part of the conversation** — no explicit recall needed.

### Filtering and Formatting

The hook applies several filters to keep context clean:

- **Relevance threshold**: Only ≥25% relevance results pass
- **Type stripping**: `[72%] [wisdom]` prefix removed — just content
- **Truncation**: Max 500 chars total injection
- **Tag exclusion**: Auto-captured signals (`auto:cmd`, `auto:file`, `auto:edit`) are excluded

### Controlling Transparency

Edit hook configuration to modify behavior:

```bash
# Disable resonance (stats only)
prompt-hook.sh --lean

# Enable resonance (default)
prompt-hook.sh --lean --resonate

# Full context (verbose)
prompt-hook.sh
```

---

## Proactive Learning

The hooks detect learning opportunities in user messages and inject hints for Claude to act on.

### Detection Patterns

| Pattern | Detection | Action |
|---------|-----------|--------|
| **Correction** | "no", "actually", "that's wrong", "not quite" | Hint: use `learn_correction` |
| **Preference** | "I prefer", "always", "never", "more concise" | Hint: use `learn_preference` |
| **Frustration** | "stuck", "confused", "frustrated", "tedious" | Hint: use `learn_approach` |
| **Milestone** | "it works", "shipped", "released", "deployed" | Hint: use `learn_milestone` |

### Auto-Learning (Stop Hook)

The stop hook goes further — if Claude didn't use a learning tool but the user clearly corrected or expressed a preference, it **auto-stores** the learning:

```bash
# If correction detected but Claude didn't call learn_correction:
chitta remember --content "[correction] WRONG: ... CORRECT: ..." \
  --tags "correction,auto-learned" --type wisdom --visibility 2
```

### Anticipation

The hook system learns context→action patterns:

1. **UserPromptSubmit**: Calls `anticipation_predict` to suggest likely next action
2. **Stop**: Records what Claude actually did via `anticipation_observe`
3. **Verification**: If prediction matched, calls `anticipation_success` to strengthen pattern

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
     ├─▶ Kill stale daemons
     ├─▶ Acquire atomic lock
     ├─▶ Start chittad daemon
     ├─▶ Wait for socket + heartbeat
     └─▶ Log: "[subconscious] Started (pid=12345, socket=..., heartbeat=ok)"

(Daemon runs independently)
     │
     ├─▶ Every 1 second:  Check for work
     ├─▶ Every 30 seconds: Process embedding queue (batch of 20)
     ├─▶ Every 30 minutes: Hygiene cycle (decay, prune, consolidate)
     ├─▶ Every 60 minutes: Theme maintenance (split, merge, reassign)
     └─▶ Pattern detection: Corrections, preferences, frustration, milestones
```

### Why Not Stop on SessionEnd?

The daemon keeps running even after Claude Code exits because:

1. **Brain-like behavior** — Your brain doesn't stop processing when you're not actively thinking
2. **Cross-session synthesis** — Wisdom can emerge between sessions
3. **Multi-instance support** — Daemon serves all Claude instances
4. **Resource efficiency** — Starting/stopping is more expensive than continuous running
5. **Background distillation** — Transcript processing continues asynchronously

### Managing the Daemon

```bash
# Check status
./hooks/subconscious.sh status

# Health check with auto-recovery
./hooks/subconscious.sh health

# Stop manually
./hooks/subconscious.sh stop

# Restart
./hooks/subconscious.sh restart

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

# Read stdin if needed (JSON format varies by event)
input=$(cat)

# Do something
echo "[my-hook] Processing..."

# Output appears in Claude's context as <system-reminder>
```

2. Make it executable:

```bash
chmod +x scripts/my-custom-hook.sh
```

3. Add to hooks configuration (plugin mode — `hooks/hooks.json`):

```json
{
  "hooks": {
    "UserPromptSubmit": [
      {
        "hooks": [
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
  "context_window": {
    "remaining_percent": 85
  }
}
```

**PostToolUse:**
```json
{
  "tool_name": "Bash",
  "tool_input": {"command": "ls -la"},
  "tool_response": "...",
  "cwd": "/path/to/project",
  "session_id": "abc123"
}
```

**SessionStart:**
```json
{
  "transcript_path": "/path/to/transcript.jsonl"
}
```

### Hook Output

- **stdout** — Injected as `<system-reminder>` in Claude's context
- **stderr** — Logged but not shown to Claude
- **Exit code** — Non-zero indicates error (logged)

### RPC from Scripts

To communicate with the daemon from custom hooks:

```bash
# Via Unix socket (fast path)
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"recall","arguments":{"query":"test","limit":3}}}' \
  | nc -U /tmp/chitta-HASH.sock

# Via thin client (fallback)
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"recall","arguments":{"query":"test","limit":3}}}' \
  | ~/.claude/bin/chitta
```

---

## Troubleshooting

### Hooks Not Running

1. Check if using plugin mode or settings mode:
   ```bash
   # Plugin mode: check hooks.json
   jq . hooks/hooks.json

   # Settings mode: check settings.json
   jq '.hooks' ~/.claude/settings.json
   ```

2. Verify scripts are executable:
   ```bash
   ls -la scripts/*.sh
   chmod +x scripts/*.sh
   ```

3. Ensure daemon is running:
   ```bash
   ./hooks/subconscious.sh status
   ```

### Slow Hook Execution

Hooks have timeouts. If they're slow:

1. Increase timeout in configuration
2. Check daemon responsiveness: `./hooks/subconscious.sh health`
3. Set `CC_SOUL_MAX_WAIT=10` for longer daemon response timeout
4. Reduce resonance results: default is 3, can lower to 2

### Resonance Not Working

1. Check daemon socket exists:
   ```bash
   ls /tmp/chitta-*.sock
   ```

2. Test daemon responds:
   ```bash
   echo "stats" | nc -U /tmp/chitta-*.sock
   ```

3. Test resonance directly:
   ```bash
   echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"full_resonate","arguments":{"query":"test","k":3}}}' \
     | nc -U /tmp/chitta-*.sock
   ```

### Daemon Not Starting

1. Check for stale processes:
   ```bash
   pgrep -f "chittad daemon"
   ```

2. Clean up stale files:
   ```bash
   rm -f /tmp/chitta-*.sock /tmp/chitta-*.pid /tmp/chitta-*.lock
   ```

3. Check if binary exists:
   ```bash
   ls -la ~/.claude/bin/chittad
   ```

4. View daemon logs:
   ```bash
   cat ~/.claude/mind/.subconscious.log
   ```

5. Try starting manually:
   ```bash
   ~/.claude/bin/chittad daemon --path ~/.claude/mind --foreground
   ```

### Context Not Appearing

1. Verify hook output manually:
   ```bash
   echo '{"message":"test query"}' | ./hooks/prompt-hook.sh prompt
   ```

2. Check daemon socket communication:
   ```bash
   echo '{"jsonrpc":"2.0","id":0,"method":"tools/call","params":{"name":"version_check"}}' \
     | nc -U /tmp/chitta-*.sock
   ```

### Debug Mode

Enable debug mode to see exactly what the hooks are doing:

```bash
# Set environment variable before running Claude Code
export DEBUG_SOUL=1
claude
```

Debug output (to stderr) shows:
- **Memory search**: Query, realm, boost k, extra tags
- **Raw results**: Full response from `full_resonate` with relevance scores
- **Code search**: Symbol search results when triggered
- **Final output**: What actually gets injected into context

To test manually:
```bash
DEBUG_SOUL=1 ./hooks/prompt-hook.sh prompt "your test query"
```

---

*Hooks are the nervous system connecting soul to body.*
