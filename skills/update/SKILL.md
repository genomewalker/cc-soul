---
name: update
description: Run after git pull to rebuild and verify cc-soul setup. Rebuilds binaries, updates symlinks, tests connectivity.
execution: inline
---

# Update

Run this after updating the plugin (git pull) to rebuild and verify setup.

## Execute

1. **Pull latest and smart-install:**

```bash
cd "${CLAUDE_PLUGIN_ROOT:-$(pwd)}" && git pull 2>&1 | tail -5 && ./scripts/smart-install.sh 2>&1
```

2. **Verify installation:**

```bash
PLUGIN="${CLAUDE_PLUGIN_ROOT:-$(pwd)}"
echo "=== Version ===" && grep '"version"' "$PLUGIN/.claude-plugin/plugin.json"
echo ""
echo "=== Binaries ===" && ls -1 "$PLUGIN/bin/"
echo ""
echo "=== Symlinks ===" && ls -la "$PLUGIN/mind/" 2>/dev/null
echo ""
echo "=== Database ===" && wc -c ~/.claude/mind/chitta.* 2>/dev/null
```

3. **Test soul:**

```bash
"${CLAUDE_PLUGIN_ROOT:-$(pwd)}/bin/chitta_cli" stats 2>&1 | grep -v "^\["
```

4. **Report result:**

```
Update Complete:
- Version: [version from plugin.json]
- Binaries: [count] built
- Database: [X] nodes
- Yantra: [ready/not ready]
```

If any step fails, explain what went wrong and suggest fixes.
