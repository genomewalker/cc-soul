---
name: health
description: Soul system health check with remediation. Use to verify setup or diagnose issues.
---

# Soul Health Check

You are checking if the soul system is working. Use the tools directly, don't shell out.

## Quick Check Process

### 1. Memory System

```
mcp__cc-memory__mem-stats()    # Should return observation counts
mcp__cc-memory__mem-recent(limit=3)  # Should return recent items
```

Healthy: Returns data without errors
Unhealthy: Connection errors, empty when shouldn't be

### 2. Soul/Wisdom System

```
Bash: ls ~/.claude/mind/wisdom/  # Wisdom files exist?
Bash: ls ~/.claude/mind/beliefs.json  # Beliefs file exists?
```

Healthy: Files exist with content
Unhealthy: Missing files, empty directories

### 3. Skills

```
Bash: ls ~/.claude/skills/  # List installed skills
```

Healthy: Expected skills present with SKILL.md files
Unhealthy: Missing skills, malformed skill files

### 4. Hooks

```
Bash: cat ~/.claude/settings.json | grep -A5 hooks
```

Healthy: Hooks registered for session events
Unhealthy: Missing hook configuration

### 5. Coherence

```
Bash: python -m cc_soul.cli coherence 2>/dev/null
```

Healthy: Returns percentage (even if low)
Unhealthy: Errors, crashes

## Status Summary

After checking, report:

| Component | Status | Issue (if any) |
|-----------|--------|----------------|
| Memory | HEALTHY/UNHEALTHY | ... |
| Wisdom | HEALTHY/UNHEALTHY | ... |
| Skills | HEALTHY/UNHEALTHY | ... |
| Hooks | HEALTHY/UNHEALTHY | ... |
| Coherence | HEALTHY/UNHEALTHY | ... |

## Common Fixes

If unhealthy, suggest specific remediation:

- **Memory errors**: Check MCP server is running, reinstall with `cc-soul install-mcps`
- **Missing wisdom**: Initialize with `cc-soul init`
- **Skills missing**: Reinstall with `cc-soul install-skills --force`
- **Hooks broken**: Reinstall with `cc-soul install-hooks --force`
- **Coherence crashes**: Check database, may need `cc-soul init --force`

## When to Use

- Session startup shows errors
- Features not working as expected
- After upgrading cc-soul
- Before starting important work
- When something feels off

## Depth Levels

- **Quick**: Just check memory and skills respond
- **Standard**: All five components
- **Deep**: Also verify hook behavior, run test suite

## Remember

Health check is diagnostic, not therapeutic. If something is broken, use `/improve` to fix it.
