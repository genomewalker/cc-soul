---
name: health
description: Diagnoses and remediates Soul system component health by verifying symlinks, binaries, database files, and version metadata; retrieving soul_context and hygiene_stats; and evaluating coherence (tau_k) and hot-node thresholds. Produces a structured status report and applies automated fixes. Use when the user asks about Soul system health, chitta diagnostics, coherence scores, hot/cold node status, symlink or binary issues, setup failures, system degradation, or requests a health check or remediation on the Soul/chitta stack.
execution: task
---

# Health

```ssl
[health] check via Task agent

verify setup:
```
```bash
ls -la plugin/mind/          # symlinks@plugin/mind/
ls -la ~/.claude/bin/chitta  # binary@~/.claude/bin/chitta
ls ~/.claude/mind/chitta.*   # db files@~/.claude/mind/chitta.*
grep -i version plugin.json  # version from plugin.json
```
```ssl

get status:
  soul_context              → mcp__soul__soul_context()
  hygiene_stats             → mcp__soul__hygiene_stats()

evaluate:
  symlinks:
    all present & valid     → healthy
    warm symlinks missing   → warning
    hot/cold symlinks missing → critical

  coherence (tau_k):
    >0.7                    → healthy
    0.5–0.7                 → warning  (consider cycle)
    <0.5                    → critical (cycle required)

  hot nodes %:
    >50%                    → healthy
    30–50%                  → warning
    <30%                    → critical

remediate:
  setup issues (missing binary/symlinks/db)
    → suggest: bash scripts/smart-install.sh
  low coherence (tau_k < 0.7)
    → run: cycle(save=true)  [MCP tool call or function invocation; not a CLI command]

report format:
  ┌─ Soul System Health ──────────────────────┐
  │ Setup:      OK | WARN | CRITICAL          │
  │ Version:    <value from plugin.json>       │
  │ Node count: <total from hygiene_stats>     │
  │ Hot nodes:  <% hot> (healthy|warn|crit)   │
  │ Coherence:  tau_k=<value> (healthy|warn|crit) │
  │ Actions:    <none | list of steps needed> │
  └───────────────────────────────────────────┘

error handling:
  binary not found    → CRITICAL; halt further checks; suggest smart-install.sh
  db files missing    → CRITICAL; coherence/node checks skipped; suggest smart-install.sh
  soul_context error  → WARN; report partial data; retry once before flagging
  hygiene_stats error → WARN; report partial data; retry once before flagging
  cycle(save=true) fails → escalate; do not retry automatically
```
