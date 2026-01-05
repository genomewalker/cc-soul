---
name: update
description: Run after git pull to rebuild and verify cc-soul setup. Creates symlinks, rebuilds binary if needed, tests connectivity.
execution: inline
---

# Update

Run this after updating the plugin (git pull) to ensure everything is set up correctly.

## Execute

1. **Run setup script:**

```bash
cd "${CLAUDE_PLUGIN_ROOT:-/maps/projects/fernandezguerra/apps/repos/cc-soul}" && ./setup.sh --quick
```

2. **Verify symlinks:**

```bash
echo "=== Symlinks ===" && ls -la "${CLAUDE_PLUGIN_ROOT:-/maps/projects/fernandezguerra/apps/repos/cc-soul}/mind/"
```

3. **Verify database:**

```bash
echo "=== Database ===" && ls -la ~/.claude/mind/chitta.* 2>/dev/null && echo "Size: $(du -h ~/.claude/mind/chitta.hot 2>/dev/null | cut -f1)"
```

4. **Test MCP connectivity:**

Call `mcp__plugin_cc-soul_cc-soul__soul_context` with format="json" to verify the MCP server can connect and read the database.

5. **Report result:**

```
Update Complete:
- Version: X.Y.Z
- Symlinks: OK
- Database: X nodes
- MCP: Connected
```

If any step fails, explain what went wrong and how to fix it.
