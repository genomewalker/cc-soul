---
description: Check if cc-soul daemon is running
---

# /cc-soul-status

Check the status of the cc-soul daemon (subconscious process).

## What to do

1. Check if the daemon is running using `~/.claude/bin/chittad status`
2. Report the daemon PID, uptime, and socket path if running
3. If not running, indicate that and suggest starting it

## Notes

- The daemon runs background processing (decay, synthesis, Hebbian learning)
- Socket is typically at `/tmp/chitta-$USER.sock`
- PID file is at `~/.claude/mind/.subconscious.pid`
