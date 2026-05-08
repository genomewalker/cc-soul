---
name: tasks
description: Browse and resume tasks, threads, and background jobs across sessions
execution: inline
---

Interactive task browser. Shows active threads, pending inbox items, and running jobs across all sessions.

## Behavior

1. Query the task ledger for active threads and pending inbox items for the current realm.
2. Present them using AskUserQuestion so the user can select what to focus on.
3. For the selected thread/task, load the resume capsule and surface the context.

## Implementation

Run this Python snippet to gather data:

```python
import subprocess, json, os

mcp_dir = os.path.expanduser("~/.claude/plugins/cache/genomewalker-cc-soul/cc-soul/$(cat ~/.claude/plugins/cache/genomewalker-cc-soul/cc-soul/.version 2>/dev/null || echo 'latest')/chitta-mcp")
# Fallback: find it relative to the skills dir
import pathlib
skill_file = pathlib.Path(__file__) if '__file__' in dir() else pathlib.Path('.')
```

Actually, use the Bash tool to gather the data, then use AskUserQuestion.

### Step 1 — gather data

```bash
_MCP_DIR="${CC_SOUL_PLUGIN_DIR:-$HOME/.claude/plugins/cache/genomewalker-cc-soul/cc-soul/$(cat $HOME/.claude/plugins/cache/genomewalker-cc-soul/.version 2>/dev/null || echo latest)}/chitta-mcp"
# fallback to sibling of hooks dir if plugin path doesn't work
[[ ! -d "$_MCP_DIR" ]] && _MCP_DIR="$(find $HOME/.claude/plugins/cache/genomewalker-cc-soul -name 'task_ledger.py' 2>/dev/null | head -1 | xargs dirname 2>/dev/null || echo '')"

# Active threads
python3 "$_MCP_DIR/task_ledger.py" thread_list --status active --limit 10

# Pending inbox
python3 "$_MCP_DIR/task_ledger.py" inbox_list --state pending --limit 20
```

### Step 2 — present with AskUserQuestion

Build options from threads + inbox items. Each option label should be:
- For threads: `[thread] <title> (last active: <relative time>)`
- For inbox completed: `✓ <digest[:80]>`
- For inbox failed: `✗ <digest[:80]>`

Include an option "Show all realms" and "Dismiss inbox" as needed.

### Step 3 — act on selection

**Thread selected**: Run resume capsule and display it:
```bash
python3 "$_MCP_DIR/resume_capsule.py" build --thread-id <thread_id>
```
Parse the `summary` field and present it as context. Then ask the user if they want to continue that thread or just view it.

**Inbox item selected**: Acknowledge it:
```bash
python3 "$_MCP_DIR/task_ledger.py" inbox_ack --item-id <item_id> --state acked
```
Then summarize what happened.

**"Dismiss all inbox"**: Ack all pending items for the realm.

## Notes

- The MCP dir can be found reliably as: the directory containing `task_ledger.py` under `$HOME/.claude/plugins/cache/genomewalker-cc-soul/`.
- If the task ledger DB doesn't exist yet (`~/.claude/task-ledger.db`), the ledger will be empty — that's fine, just say "No tracked tasks yet."
- Use `realm_detect` via chitta if REALM is not set: `chitta realm_detect`
