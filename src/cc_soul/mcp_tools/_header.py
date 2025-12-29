# === HEADER ===
"""
Soul MCP Server - Native Claude Code integration.

Provides essential soul operations as MCP tools. Hooks handle context injection
at session start; this server handles writes and mid-conversation queries.

GENERATED FILE - DO NOT EDIT DIRECTLY
Edit files in mcp_tools/ and run: python -m cc_soul.mcp_tools._mcp_builder

Install and register:
    pip install cc-soul[mcp]
    claude mcp add soul -- soul-mcp
"""

from mcp.server.fastmcp import FastMCP

mcp = FastMCP("soul")
