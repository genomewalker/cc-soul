---
name: recap
description: Token-savvy session continuation. Rebuilds working context from transcript + soul memories in ~1500 tokens instead of replaying full history. Use when starting a new session to continue previous work.
execution: direct
---

# Recap: Token-Savvy Session Continuation

Reconstructs working context from a previous session's transcript and soul memories,
fitting the result into ~1500 tokens. This replaces `claude --resume` which replays
the entire conversation history (often 300k+ tokens with cache invalidation issues).

## Process

### Step 1: Identify the session

If a session_id is provided, use it. Otherwise auto-detect:

```bash
# Transcript files live at ~/.claude/projects/<encoded-cwd>/<session-id>.jsonl
# Find the most recent one that ISN'T the current session
PROJECT_DIR=$(ls -dt ~/.claude/projects/*/  | head -1)  # or derive from CWD
ls -t "$PROJECT_DIR"*.jsonl | head -5
```

Pick the **second most recent** file (the first is the current session being written to).
Extract the session_id from the filename (strip path and .jsonl extension).

If the user says "last N" or "pick", show the 5 most recent with sizes:
```bash
ls -lht ~/.claude/projects/<encoded-cwd>/*.jsonl | head -5
```

### Step 2: Extract key signals from transcript

Read **user messages only** (these carry intent). Read in two passes: first half and last half,
since the last portion is usually where the active work is:

```tool
mcp__chitta__read_transcript({
  session_id: "<id>",
  role_filter: "user",
  max_chars_per_turn: 150,
  limit: 30
})
```

Then read the tail:

```tool
mcp__chitta__read_transcript({
  session_id: "<id>",
  role_filter: "user",
  max_chars_per_turn: 150,
  start_turn: -30,
  limit: 30
})
```

**When reading user messages, IGNORE these noise patterns:**
- `<local-command-*>` blocks (model switches, login, plugin commands)
- `<command-name>` blocks (slash commands)
- `<task-notification>` blocks (agent completions)
- `<system-reminder>` blocks (hook output)
- `[Request interrupted by user]`
- Repeated identical messages (user often retypes when switching models)
- `<local-command-stdout>` blocks

**Focus ONLY on lines that express intent:** actual questions, requests, corrections,
confirmations. These are typically short, informal, often with typos.

Read **last 15 assistant messages** for decisions:

```tool
mcp__chitta__read_transcript({
  session_id: "<id>",
  role_filter: "assistant",
  max_chars_per_turn: 200,
  start_turn: -20,
  limit: 15
})
```

### Step 3: Get soul context for the session

Query soul for memories tagged with the session or relevant work:

```tool
mcp__chitta__smart_context({
  task: "<summarize what user was working on from step 2>",
  mode: "fast",
  limit: 300
})
```

### Step 4: Get current environment state

```bash
git status --short
git log --oneline -5
ps aux | grep -E "cresearch|chittad" | grep -v grep | awk '{print $11}'
```

### Step 5: Synthesize the recap block

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
- **Running**: <processes>

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

Compare: `claude --resume` typically sends 100k-300k tokens of raw history.

## Arguments

- No args: resume from most recent *previous* session in current project (auto-detect)
- `<session_id>`: resume from specific session
- `last N`: show last N sessions to pick from

## Auto-Detection Logic

The transcript directory for the current project is:
```
~/.claude/projects/<encoded-cwd>/
```

Where `<encoded-cwd>` is the working directory with `/` replaced by `-` and leading `-`.
For example: `/maps/projects/fernandezguerra/apps/repos/cc-soul` →
`-maps-projects-fernandezguerra-apps-repos-cc-soul`

To find the previous session:
```bash
# List transcripts by recency, skip the current (being written to = largest/newest)
ls -t ~/.claude/projects/-maps-projects-.../*.jsonl | sed -n '2p'
```

The session_id is the filename without `.jsonl`.

## Example

```
/recap
→ Reads transcript from last session
→ Extracts: "User was implementing multi-round hypotheses in chitta-research hotr.
   Key fix: labeled 'outer loop with break to avoid Noop. Deployed binary was stale.
   Pending: verify hotr generates round-2 hypotheses with web refs."
→ ~1200 tokens instead of ~250k
```
