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

Call:
- mcp__soul__soul_context(format="json") - Get statistics before backup

Note the node count and coherence.

## 2. Run Backup

Call:
- mcp__soul__cycle(save=true) - Save current state to disk

This writes the soul graph to ~/.claude/mind/synapse/

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

Soul state is saved to: `~/.claude/mind/synapse/`

For additional backup, you can copy this directory:
```bash
cp -r ~/.claude/mind/synapse ~/.claude/mind/synapse-backup-$(date +%Y%m%d)
```
