---
name: cc-soul-daemon
description: Start, stop, or check the chittad daemon
execution: inline
---

# cc-soul-daemon

Manage the chittad background daemon.

## Usage

Run the subconscious.sh script with the requested action:

```bash
# Check status (default)
${CLAUDE_PLUGIN_ROOT}/hooks/subconscious.sh status

# Start daemon
${CLAUDE_PLUGIN_ROOT}/hooks/subconscious.sh start

# Stop daemon
${CLAUDE_PLUGIN_ROOT}/hooks/subconscious.sh stop

# Restart daemon
${CLAUDE_PLUGIN_ROOT}/hooks/subconscious.sh restart
```

Parse user request for action (start/stop/restart/status), default to status.
Run the appropriate command and report the result.
