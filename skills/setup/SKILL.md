---
name: setup
description: Initial setup of cc-soul plugin. Downloads models, builds binaries, creates symlinks.
execution: inline
---

# Setup

Run this skill for initial plugin setup or to rebuild from scratch.

## Execute

1. **Run full setup:**

```bash
cd "${CLAUDE_PLUGIN_ROOT:-$(pwd)}" && ./setup.sh 2>&1
```

2. **Verify installation:**

```bash
echo "=== Binaries ===" && ls -1 "${CLAUDE_PLUGIN_ROOT:-$(pwd)}/bin/"
echo ""
echo "=== Symlinks ===" && ls -la "${CLAUDE_PLUGIN_ROOT:-$(pwd)}/mind/"
echo ""
echo "=== Database ===" && wc -c ~/.claude/mind/chitta.* 2>/dev/null
```

3. **Test soul:**

```bash
"${CLAUDE_PLUGIN_ROOT:-$(pwd)}/bin/chitta_cli" stats 2>&1 | grep -v "^\["
```

4. **Report result:**

```
Setup Complete:
- Binaries: [list built binaries]
- Database: [X] nodes
- Yantra: [ready/not ready]
```

If any step fails, explain what went wrong and suggest fixes.
