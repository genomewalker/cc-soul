#!/bin/bash
# Configure chitta MCP server in Claude Code settings
#
# Usage: configure-mcp.sh [--remove]
#
# This installs the Python MCP server and registers it with Claude Code.
# The Python server uses the official MCP SDK and bridges to the chittad daemon.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
CHITTA_MCP_DIR="$REPO_DIR/chitta-mcp"
SETTINGS_FILE="${HOME}/.claude/settings.json"

# Detect Python/pip
find_pip() {
    if command -v uv &>/dev/null; then
        echo "uv pip"
    elif command -v pip3 &>/dev/null; then
        echo "pip3"
    elif command -v pip &>/dev/null; then
        echo "pip"
    else
        echo ""
    fi
}

remove_mcp() {
    echo "[configure-mcp] Removing chitta MCP server..."

    # Try claude mcp remove first
    if command -v claude &>/dev/null; then
        claude mcp remove chitta 2>/dev/null || true
        echo "[configure-mcp] Removed from Claude Code"
    fi

    # Also clean up settings.json manually
    if [[ -f "$SETTINGS_FILE" ]] && command -v jq &>/dev/null; then
        local current
        current=$(cat "$SETTINGS_FILE")
        if echo "$current" | jq -e '.mcpServers.chitta' &>/dev/null; then
            local updated
            updated=$(echo "$current" | jq 'del(.mcpServers.chitta)')
            echo "$updated" | jq '.' > "$SETTINGS_FILE"
        fi
    fi

    # Uninstall Python package
    local PIP=$(find_pip)
    if [[ -n "$PIP" ]]; then
        $PIP uninstall chitta-mcp -y 2>/dev/null || true
        echo "[configure-mcp] Uninstalled Python package"
    fi

    echo "[configure-mcp] Done"
}

add_mcp() {
    echo "[configure-mcp] Installing chitta MCP server..."

    # Check for Python package directory
    if [[ ! -d "$CHITTA_MCP_DIR" ]]; then
        echo "[configure-mcp] Error: chitta-mcp directory not found at $CHITTA_MCP_DIR" >&2
        return 1
    fi

    # Find pip
    local PIP=$(find_pip)
    if [[ -z "$PIP" ]]; then
        echo "[configure-mcp] Error: pip not found. Install Python 3.10+ first." >&2
        return 1
    fi

    # Install Python package
    echo "[configure-mcp] Installing chitta-mcp Python package..."
    $PIP install -q "$CHITTA_MCP_DIR"

    # Verify installation
    if ! command -v chitta-mcp &>/dev/null; then
        echo "[configure-mcp] Warning: chitta-mcp not on PATH after install"
        echo "[configure-mcp] You may need to add ~/.local/bin to PATH"
    fi

    # Register with Claude Code using claude mcp add
    if command -v claude &>/dev/null; then
        echo "[configure-mcp] Registering with Claude Code..."
        claude mcp add --transport stdio --scope user chitta -- chitta-mcp 2>/dev/null || {
            # May already exist
            if claude mcp list 2>/dev/null | grep -q chitta; then
                echo "[configure-mcp] chitta-mcp already registered"
            else
                echo "[configure-mcp] Warning: Failed to register with claude mcp add"
                echo "[configure-mcp] Falling back to settings.json..."
                add_mcp_settings
            fi
        }
    else
        echo "[configure-mcp] Claude CLI not found, configuring settings.json directly..."
        add_mcp_settings
    fi

    # Add permission
    add_permission

    echo "[configure-mcp] Done. Restart Claude Code to use mcp__chitta__* tools."
}

add_mcp_settings() {
    # Fallback: add directly to settings.json
    mkdir -p "${HOME}/.claude"

    if [[ ! -f "$SETTINGS_FILE" ]]; then
        echo '{}' > "$SETTINGS_FILE"
    fi

    if ! command -v jq &>/dev/null; then
        echo "[configure-mcp] jq not found, cannot modify settings.json" >&2
        return 1
    fi

    local current
    current=$(cat "$SETTINGS_FILE")

    # Find chitta-mcp path
    local CHITTA_MCP_PATH
    CHITTA_MCP_PATH=$(command -v chitta-mcp 2>/dev/null || echo "chitta-mcp")

    if ! echo "$current" | jq -e '.mcpServers.chitta' &>/dev/null; then
        current=$(echo "$current" | jq --arg cmd "$CHITTA_MCP_PATH" '.mcpServers.chitta = {"command": $cmd, "args": []}')
        echo "$current" | jq '.' > "$SETTINGS_FILE"
        echo "[configure-mcp] Added chitta to settings.json"
    else
        echo "[configure-mcp] chitta already in settings.json"
    fi
}

add_permission() {
    if [[ ! -f "$SETTINGS_FILE" ]]; then
        return 0
    fi

    if ! command -v jq &>/dev/null; then
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
        echo "$current" | jq '.' > "$SETTINGS_FILE"
        echo "[configure-mcp] Added mcp__chitta__* permission"
    fi
}

case "${1:-}" in
    --remove|-r)
        remove_mcp
        ;;
    *)
        add_mcp
        ;;
esac
