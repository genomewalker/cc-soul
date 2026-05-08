---
name: resume
description: Resume a thread by loading its ~800-token context capsule
execution: inline
---

Load the resume capsule for a thread and surface it as context for continuing work.

Usage: `/resume` (shows picker) or `/resume <thread_id_prefix>`

## Implementation

### If a thread_id or prefix was given as argument

```bash
_MCP_DIR=$(find $HOME/.claude/plugins/cache/genomewalker-cc-soul -name 'task_ledger.py' 2>/dev/null | head -1 | xargs dirname 2>/dev/null)

# Find matching thread
python3 "$_MCP_DIR/task_ledger.py" thread_list --status active --limit 20
```

Match the argument against thread_id prefix or title (case-insensitive substring). If multiple match, present them via AskUserQuestion.

### If no argument — show picker

List active threads:
```bash
python3 "$_MCP_DIR/task_ledger.py" thread_list --status active --limit 10
```

Present with AskUserQuestion. Each option: `<title> [<thread_id[:8]>] — <realm>`

### Load the capsule

```bash
python3 "$_MCP_DIR/resume_capsule.py" build --thread-id <thread_id>
```

The result JSON has a `summary` field — display it verbatim. It contains:
- Active tasks with job IDs, git branches, conda envs, blockers
- Last 3 completed tasks with digests
- Failed tasks
- Recent artifacts with sizes
- Next work items

### After displaying

Ask: "Continue from here?" If yes, set the current thread context:
```bash
echo "<thread_id>" > "$HOME/.claude/mind/.current_thread_id"
```

And note the realm, git branch, conda env, and open files from the capsule so you can orient yourself immediately.

## Error cases

- Thread not found: say so, offer to run `/tasks` instead.
- Capsule build fails: fall back to raw thread_list output.
- DB missing: "No task history yet — tasks are tracked automatically once you run analyses."
