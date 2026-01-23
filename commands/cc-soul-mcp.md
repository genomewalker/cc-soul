---
description: Configure chitta MCP server for direct tool access (mcp__chitta__*)
---

# /cc-soul-mcp

```ssl
[cc-soul] mcp: enable direct tool access via MCP

check: ~/.claude/settings.json exists
add: mcpServers.chitta = {command: ~/.claude/bin/chitta, args: ["mcp"]}
add: permissions.allow += "mcp__chitta__*"

run: ${CLAUDE_PLUGIN_ROOT}/scripts/configure-mcp.sh
result: restart session→mcp__chitta__* tools available

--remove flag: remove chitta from mcpServers
```
