#!/bin/bash
# Configure chitta MCP server in Claude Code settings
#
# Usage: configure-mcp.sh [--remove]
#
# This is a one-time setup to enable direct MCP tool access to chitta.
# Run manually after installation if you want mcp__chitta__* tools available.

set -e

SETTINGS_FILE="${HOME}/.claude/settings.json"
CHITTA_BIN="${HOME}/.claude/bin/chitta"

remove_mcp() {
    if [[ ! -f "$SETTINGS_FILE" ]]; then
        echo "[configure-mcp] No settings file"
        return 0
    fi

    if ! command -v jq &>/dev/null; then
        echo "[configure-mcp] jq required for removal" >&2
        return 1
    fi

    local current
    current=$(cat "$SETTINGS_FILE")

    if echo "$current" | jq -e '.mcpServers.chitta' &>/dev/null; then
        local updated
        updated=$(echo "$current" | jq 'del(.mcpServers.chitta)')
        echo "$updated" | jq '.' > "$SETTINGS_FILE"
        echo "[configure-mcp] Removed chitta MCP server"
    else
        echo "[configure-mcp] chitta MCP server not configured"
    fi
}

add_mcp() {
    mkdir -p "${HOME}/.claude"

    if [[ ! -f "$SETTINGS_FILE" ]]; then
        echo '{}' > "$SETTINGS_FILE"
    fi

    if ! command -v jq &>/dev/null; then
        echo "[configure-mcp] jq not found, skipping" >&2
        return 0
    fi

    local current
    current=$(cat "$SETTINGS_FILE")

    # Add mcp__chitta__* permission if missing
    if ! echo "$current" | jq -e '.permissions.allow' &>/dev/null; then
        current=$(echo "$current" | jq '.permissions = {"allow": []}')
    fi

    if ! echo "$current" | jq -e '.permissions.allow | index("mcp__chitta__*")' &>/dev/null; then
        current=$(echo "$current" | jq '.permissions.allow += ["mcp__chitta__*"]')
        echo "[configure-mcp] Added mcp__chitta__* permission"
    fi

    # Add MCP server config
    if ! echo "$current" | jq -e '.mcpServers.chitta' &>/dev/null; then
        current=$(echo "$current" | jq --arg cmd "$CHITTA_BIN" '.mcpServers.chitta = {"command": $cmd, "args": ["mcp"]}')
        echo "[configure-mcp] Added chitta MCP server"
    else
        echo "[configure-mcp] chitta MCP server already configured"
    fi

    echo "$current" | jq '.' > "$SETTINGS_FILE"
    echo "[configure-mcp] Done. Restart Claude Code to use mcp__chitta__* tools."
}

case "${1:-}" in
    --remove|-r)
        remove_mcp
        ;;
    *)
        add_mcp
        ;;
esac
