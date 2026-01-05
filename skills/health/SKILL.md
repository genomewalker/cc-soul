---
name: health
description: Soul system health check with remediation. Use to verify setup or diagnose issues.
execution: task
---

# Health

Spawn a Task agent to check soul health. This saves context.

## Execute

```
Task(
  subagent_type="general-purpose",
  description="Soul health check",
  prompt="""
Check the soul system health using Bash and MCP tools.

## 1. Verify Setup

Run these checks with Bash:
```bash
# Check symlinks exist and point to valid targets
ls -la ${CLAUDE_PLUGIN_ROOT:-$(dirname $(dirname $0))}/mind/ 2>/dev/null || echo "ERROR: mind/ directory missing"

# Check binary exists
ls -la ${CLAUDE_PLUGIN_ROOT:-$(dirname $(dirname $0))}/bin/chitta_mcp 2>/dev/null || echo "ERROR: chitta_mcp binary missing"

# Check actual data location
ls -la ~/.claude/mind/chitta.* 2>/dev/null || echo "WARNING: No chitta database files at ~/.claude/mind/"

# Check version
cat ${CLAUDE_PLUGIN_ROOT:-$(dirname $(dirname $0))}/.claude-plugin/plugin.json 2>/dev/null | grep version
```

## 2. Get Soul Status

Call these MCP tools:
- mcp__plugin_cc-soul_cc-soul__soul_context(format="json") - Get coherence and node statistics
- mcp__plugin_cc-soul_cc-soul__harmonize() - Check voice agreement

## 3. Evaluate Health

| Metric | Healthy | Warning | Critical |
|--------|---------|---------|----------|
| Symlinks | Valid | Missing warm | Missing hot/cold |
| Binary | Present | - | Missing |
| Database files | Present | Empty | Missing |
| Coherence (tau_k) | > 0.7 | 0.5-0.7 | < 0.5 |
| Hot nodes % | > 50% | 30-50% | < 30% |

## 4. Remediate if Needed

If setup issues found:
- Suggest running: ./setup.sh

If coherence is low:
- mcp__plugin_cc-soul_cc-soul__cycle(save=true) - Run maintenance

## 5. Report

Return a concise health report:
- Setup status: OK / Issues found
- Version: X.Y.Z
- Node count and location
- Coherence scores
- Any issues or actions needed
"""
)
```

After the agent returns, present the health report.
