#!/usr/bin/env bash
# configure-mcp.sh — Add or remove chitta MCP server entry in ~/.claude/settings.json
# Uses the installed chitta-mcp entrypoint (version-independent).

set -euo pipefail

SETTINGS="${HOME}/.claude/settings.json"
REMOVE=false

for arg in "$@"; do
    [[ "$arg" == "--remove" ]] && REMOVE=true
done

if ! command -v jq &>/dev/null; then
    echo "[cc-soul] jq required but not found" >&2
    exit 1
fi

[[ ! -f "$SETTINGS" ]] && echo '{}' > "$SETTINGS"

# Resolve chitta-mcp command — prefer installed entrypoint over server.py
CHITTA_MCP_CMD=""
for candidate in \
    "$(command -v chitta-mcp 2>/dev/null || true)" \
    "${HOME}/.local/bin/chitta-mcp" \
    "/maps/projects/fernandezguerra/apps/opt/conda/envs/bioinfo/bin/chitta-mcp"; do
    if [[ -n "$candidate" && -x "$candidate" ]]; then
        CHITTA_MCP_CMD="$candidate"
        break
    fi
done

if [[ -z "$CHITTA_MCP_CMD" && "$REMOVE" == "false" ]]; then
    echo "[cc-soul] chitta-mcp not found — run: pip install -e <plugin>/chitta-mcp" >&2
    exit 1
fi

current=$(cat "$SETTINGS")

if [[ "$REMOVE" == "true" ]]; then
    updated=$(echo "$current" | jq 'del(.mcpServers.chitta)')
    updated=$(echo "$updated" | jq '.permissions.allow //= [] | .permissions.allow |= map(select(test("mcp__chitta") | not))')
    echo "$updated" | jq '.' > "$SETTINGS"
    echo "[cc-soul] Removed chitta MCP server from settings.json"
    exit 0
fi

# Add/update mcpServers.chitta with entrypoint command
updated=$(echo "$current" | jq \
    --arg cmd "$CHITTA_MCP_CMD" \
    '.mcpServers //= {} | .mcpServers.chitta = {"type": "stdio", "command": $cmd, "args": []}')

# Add mcp__chitta__* permission if missing
if ! echo "$updated" | jq -e '.permissions.allow | index("mcp__chitta__*")' &>/dev/null; then
    updated=$(echo "$updated" | jq '.permissions //= {} | .permissions.allow //= [] | .permissions.allow += ["mcp__chitta__*"]')
fi

echo "$updated" | jq '.' > "$SETTINGS"
echo "[cc-soul] Configured chitta MCP server: $CHITTA_MCP_CMD"
echo "[cc-soul] Restart Claude Code to activate"
