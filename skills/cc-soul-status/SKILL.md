---
name: cc-soul-status
description: Check if cc-soul daemon is running
execution: inline
---

# cc-soul-status

Check the status of the chittad daemon.

## Usage

```bash
${CLAUDE_PLUGIN_ROOT}/scripts/subconscious.sh status
```

Reports:
- Whether daemon is running
- Process ID
- Socket path (/tmp/chitta-{hash}.sock)
- PID file location
