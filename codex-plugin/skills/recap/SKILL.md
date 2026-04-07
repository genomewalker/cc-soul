---
name: recap
description: "Token-savvy session continuation. Rebuilds working context from transcript + soul memories in ~1500 tokens instead of replaying full history. Use when starting a new session to continue previous work."
execution: direct
aliases: [resume]
---

# Recap: Token-Savvy Session Continuation

Reconstructs working context from a previous session's transcript and soul memories,
fitting the result into ~1500 tokens.

## Step 0: Detect Environment

**First, determine which tool you are running in:**

```bash
# Codex: ~/.codex/sessions/ exists and has recent content
# Claude Code: ~/.claude/projects/ with encoded-cwd dirs
ls ~/.codex/sessions/ 2>/dev/null && echo "CODEX" || echo "CLAUDE"
```

Then follow the appropriate path below.

---

## Claude Code Path

### Step 1 (Claude): Identify the session

```bash
# Transcript files: ~/.claude/projects/<encoded-cwd>/<session-id>.jsonl
# encoded-cwd = working dir with / → - and leading -
# Example: /maps/projects/foo/bar → -maps-projects-foo-bar
ls -t ~/.claude/projects/-<encoded-cwd>/*.jsonl | head -5
```

Pick the **second most recent** (first = current session being written to).
Session_id = filename without `.jsonl`.

### Step 2 (Claude): Extract key signals

Read user messages in two passes:

```tool
mcp__chitta__read_transcript({
  session_id: "<id>",
  role_filter: "user",
  max_chars_per_turn: 150,
  limit: 30
})
```

```tool
mcp__chitta__read_transcript({
  session_id: "<id>",
  role_filter: "user",
  max_chars_per_turn: 150,
  start_turn: -30,
  limit: 30
})
```

Read last 15 assistant messages:

```tool
mcp__chitta__read_transcript({
  session_id: "<id>",
  role_filter: "assistant",
  max_chars_per_turn: 200,
  start_turn: -20,
  limit: 15
})
```

---

## Codex Path

### Step 1 (Codex): Find recent sessions for current project

```bash
# Codex transcripts: ~/.codex/sessions/YYYY/MM/DD/rollout-<datetime>-<uuid>.jsonl
# Session meta (first line) contains cwd — use to filter by current project
CWD=$(pwd)
python3 - <<'EOF'
import os, json, glob
cwd = os.getcwd()
sessions = []
for f in glob.glob(os.path.expanduser("~/.codex/sessions/**/*.jsonl"), recursive=True):
    try:
        first = json.loads(open(f).readline())
        if first.get("payload", {}).get("cwd") == cwd:
            sessions.append((os.path.getmtime(f), f))
    except: pass
for mtime, path in sorted(sessions, reverse=True)[:5]:
    sid = os.path.basename(path).replace(".jsonl", "").split("-", 3)[-1]  # UUID part
    size = os.path.getsize(path)
    print(f"{path}  [{sid}]  {size//1024}KB")
EOF
```

Pick the most recent (the current session has no prior turns worth recapping).
Session path = the full file path. Session_id = the UUID suffix after the timestamp.

### Step 2 (Codex): Extract key signals from transcript

Use python3 to parse the Codex JSONL format (different from Claude's format):

```bash
# Read user messages from a Codex transcript
python3 - <<'EOF'
import json, sys

TRANSCRIPT = "/path/to/rollout-....jsonl"  # fill in
MAX_CHARS = 150
LIMIT = 40
ROLE_FILTER = "user"  # or "assistant" or "" for both

turns = []
for line in open(TRANSCRIPT):
    d = json.loads(line)
    if d.get("type") == "response_item":
        p = d["payload"]
        role = p.get("role", "")
        if ROLE_FILTER and role != ROLE_FILTER:
            continue
        content = p.get("content", "")
        if isinstance(content, list):
            content = " ".join(b.get("text","") for b in content if isinstance(b,dict) and b.get("type")=="text")
        content = str(content).strip()
        if content:
            turns.append((role, content[:MAX_CHARS]))

# First half
for role, text in turns[:20]:
    print(f"[{role}] {text}")
print("--- tail ---")
for role, text in turns[-20:]:
    print(f"[{role}] {text}")
EOF
```

Run twice: once for user messages (ROLE_FILTER="user"), once for assistant (ROLE_FILTER="assistant").

**When reading user messages, IGNORE these noise patterns:**
- `<local-command-*>` blocks (model switches, login, plugin commands)
- `<command-name>` blocks (slash commands)
- `<task-notification>` blocks (agent completions)
- `<system-reminder>` blocks (hook output)
- `[Request interrupted by user]`
- Repeated identical messages

**Focus ONLY on lines that express intent:** questions, requests, corrections, confirmations.

---

## Step 3: Get soul context

Query soul for memories relevant to what was being worked on:

```tool
mcp__chitta__smart_context({
  task: "<summarize what user was working on from step 2>",
  mode: "fast",
  limit: 300
})
```

## Step 4: Get current environment state

```bash
git status --short
git log --oneline -5
```

## Step 5: Synthesize the recap block

Combine everything into a structured block under 1500 tokens:

```markdown
## Session Recap: <session_id>

### What was being done
<2-3 sentences from user messages — the primary task and subtasks>

### Key decisions made
<Bullet list of important choices/fixes from assistant messages>

### Current state
- **Branch**: <branch>
- **Uncommitted**: <files or "clean">

### Pending work
<What was left unfinished — derived from last user messages + soul context>

### Key context
<Critical facts that would be lost without replay — error patterns,
 architectural decisions, corrections the user made>
```

## Rules

1. **Never replay full tool outputs** — those are the token killers
2. **User corrections are highest priority** — always include them
3. **Skip system-reminder content** — it's noise for resume purposes
4. **Skip task-notification content** — ephemeral
5. **Decisions > descriptions** — "chose X because Y" not "I looked at file Z"
6. **Include file paths only if they're actively being edited**

## Token Budget

| Section | Target |
|---------|--------|
| What was being done | 200 tokens |
| Key decisions | 400 tokens |
| Current state | 150 tokens |
| Pending work | 300 tokens |
| Key context | 450 tokens |
| **Total** | **~1500 tokens** |

## When to use /recap vs /compact

| Situation | Action | Why |
|-----------|--------|-----|
| Same session, context growing | `/compact` | Preserves conversation, trims tool output |
| New session, continue previous work | `/recap` | ~1500 tokens vs replaying full history |
| Session idle >5 min (cache expired) | New session + `/recap` | Avoids cache-write repricing of stale context |
| >20 subagents spawned | `/compact` or new + `/recap` | Each subagent cold-starts a cache |
| Transcript >20MB | New session + `/recap` | Reduces cache-write volume |

**Cost perspective**: A 50MB session that resumes after cache expiry re-prices all context
at cache-write rates (~$18.75/MTok). Starting fresh with /recap costs ~$0.03.

## Arguments

- No args: resume from most recent previous session for current project (auto-detect)
- `<session_id>`: resume from specific session
- `last N`: show last N sessions to pick from
