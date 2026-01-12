---
description: Gracefully stop the cc-soul daemon
---

# /cc-soul-shutdown

Gracefully stop the cc-soul daemon (subconscious process).

## What to do

1. Send shutdown command via `~/.claude/bin/chitta shutdown`
2. This saves state before shutting down
3. Confirm the daemon has stopped

## Notes

- Graceful shutdown preserves all pending state
- The daemon will restart automatically on next session
- Use `pkill -TERM chittad` as fallback if shutdown command fails
