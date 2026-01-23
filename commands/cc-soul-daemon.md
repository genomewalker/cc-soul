---
description: Start, stop, or check the chittad daemon
---

# /cc-soul-daemon

```ssl
[cc-soul] daemon: manage chittad background process

actions:
  start: ${CLAUDE_PLUGIN_ROOT}/scripts/subconscious.sh start
  stop: ${CLAUDE_PLUGIN_ROOT}/scripts/subconscious.sh stop
  status: ${CLAUDE_PLUGIN_ROOT}/scripts/subconscious.sh status
  restart: stop→start

default: status (show if running, socket path, pid)
socket: /tmp/chitta-{hash}.sock
logs: ~/.claude/mind/.subconscious.log
```
