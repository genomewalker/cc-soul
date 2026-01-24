#!/usr/bin/env python3
"""Install/uninstall chitta-mcp MCP server with Claude Code."""

import subprocess
import sys


def install():
    """Register chitta-mcp as an MCP server with Claude Code."""
    try:
        result = subprocess.run(
            ["claude", "mcp", "add", "--transport", "stdio", "--scope", "user",
             "chitta", "--", "chitta-mcp"],
            capture_output=True,
            text=True
        )
        if result.returncode == 0:
            print("chitta-mcp registered with Claude Code")
            print(result.stdout)
        else:
            if "already exists" in result.stderr.lower():
                print("chitta-mcp already registered")
            else:
                print(f"Failed to register: {result.stderr}")
                sys.exit(1)
    except FileNotFoundError:
        print("Claude Code CLI not found. Install from: https://claude.ai/download")
        sys.exit(1)


def uninstall():
    """Remove chitta-mcp MCP server from Claude Code."""
    try:
        result = subprocess.run(
            ["claude", "mcp", "remove", "chitta"],
            capture_output=True,
            text=True
        )
        if result.returncode == 0:
            print("chitta-mcp removed from Claude Code")
            print(result.stdout)
        else:
            if "not found" in result.stderr.lower():
                print("chitta-mcp not registered")
            else:
                print(f"Failed to remove: {result.stderr}")
                sys.exit(1)
    except FileNotFoundError:
        print("Claude Code CLI not found")
        sys.exit(1)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "uninstall":
        uninstall()
    else:
        install()
