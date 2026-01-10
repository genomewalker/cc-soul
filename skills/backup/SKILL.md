---
name: backup
description: Preserve soul state across time. Use for session-end backups, before major changes, or weekly maintenance.
execution: task
---

# Backup

Spawn a Task agent to backup the soul. This saves context.

## Execute

```
Task(
  subagent_type="general-purpose",
  description="Soul backup",
  prompt="""
Backup the soul state.

## 1. Get Current State

Run this Bash command:
```bash
chitta_mcp soul_context
```

Note the node count and coherence.

## 2. Run Backup

Run this Bash command:
```bash
chitta_mcp cycle --save true
```

This writes the soul graph to ~/.claude/mind/chitta/

## 3. Verify

After cycle completes, report:
- Nodes backed up (total count)
- Coherence at backup time
- Any pruning that occurred during cycle

## 4. Respond

Return a brief confirmation (3-5 lines):
- Backup completed
- Node count
- Coherence
- Storage location

Keep it simple.
"""
)
```

After the agent returns, confirm the backup to the user.

## Storage

Soul state is saved to: `~/.claude/mind/chitta/`

For additional backup, you can copy this directory:
```bash
cp -r ~/.claude/mind/chitta ~/.claude/mind/chitta-backup-$(date +%Y%m%d)
```
