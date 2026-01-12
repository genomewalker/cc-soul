---
name: health
description: Soul system health check with remediation
execution: task
---

# Health

```ssl
[health] check via Task agent

verify setup:
  symlinks@plugin/mind/
  binary@~/.claude/bin/chitta
  db files@~/.claude/mind/chitta.*
  version from plugin.json

get status: soul_context(format=json) + harmonize

evaluate:
  symlinks: valid|missing warm|missing hot/cold
  coherence(tau_k): >0.7 healthy | 0.5-0.7 warning | <0.5 critical
  hot nodes %: >50% healthy | 30-50% warning | <30% critical

remediate:
  setup issues→suggest ./setup.sh
  low coherence→cycle(save=true)

report: setup status | version | node count | coherence | actions needed
```
