#!/usr/bin/env python3
"""
Chitta MCP Server - Python bridge to chittad daemon.

Uses the official MCP SDK to expose chitta tools to Claude Code.
Tools are defined statically (like opencode-bridge) for proper discovery.
"""

import os
import sys
import json
import socket
import asyncio
import logging
from typing import Optional

# Suppress MCP SDK validation warnings (Claude Code sends incomplete initialize requests)
logging.getLogger("root").setLevel(logging.ERROR)
logging.getLogger("mcp").setLevel(logging.ERROR)

from mcp.server import Server, InitializationOptions
from mcp.server.stdio import stdio_server
from mcp.types import Tool, TextContent, ServerCapabilities, ToolsCapability


def djb2_hash(s: str) -> int:
    """DJB2 hash algorithm matching C++ implementation."""
    h = 5381
    for c in s:
        h = ((h << 5) + h + ord(c)) & 0xFFFFFFFF
    return h


def get_socket_path() -> str:
    """Get the daemon socket path."""
    home = os.environ.get("HOME", "")
    mind_path = os.path.join(home, ".claude", "mind", "chitta")
    hash_val = djb2_hash(mind_path)
    return f"/tmp/chitta-{hash_val}.sock"


class ChittaClient:
    """Client for communicating with chittad daemon."""

    def __init__(self, socket_path: str):
        self.socket_path = socket_path
        self.sock: Optional[socket.socket] = None

    def connect(self) -> bool:
        """Connect to daemon socket."""
        try:
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.connect(self.socket_path)
            return True
        except Exception:
            self.sock = None
            return False

    def request(self, data: str) -> Optional[str]:
        """Send JSON-RPC request and get response."""
        if not self.sock:
            return None
        try:
            self.sock.sendall((data + "\n").encode())
            response = b""
            while True:
                chunk = self.sock.recv(4096)
                if not chunk:
                    break
                response += chunk
                if b"\n" in response:
                    break
            return response.decode().strip()
        except Exception:
            return None

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None


# Global client and server
client: Optional[ChittaClient] = None
server = Server("chitta")


def ensure_daemon() -> bool:
    """Ensure daemon is running and connected."""
    global client

    socket_path = get_socket_path()

    if client and client.sock:
        return True

    client = ChittaClient(socket_path)
    if client.connect():
        return True

    # Try to start daemon
    chittad = os.path.join(os.environ.get("HOME", ""), ".claude", "bin", "chittad")
    if os.path.exists(chittad):
        os.system(f"{chittad} daemon >/dev/null 2>&1 &")
        for _ in range(50):
            import time
            time.sleep(0.1)
            if client.connect():
                return True

    return False


def daemon_call(tool_name: str, arguments: dict) -> str:
    """Call a tool on the daemon."""
    if not ensure_daemon():
        return "Error: Failed to connect to daemon"

    req = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {"name": tool_name, "arguments": arguments}
    }

    response = client.request(json.dumps(req))
    if not response:
        return "Error: No response from daemon"

    try:
        data = json.loads(response)
        result = data.get("result", {})
        content = result.get("content", [])

        if content and isinstance(content, list):
            texts = []
            for item in content:
                if isinstance(item, dict):
                    texts.append(item.get("text", str(item)))
                else:
                    texts.append(str(item))
            return "\n".join(texts)

        if "structured" in result:
            return json.dumps(result["structured"], indent=2)

        return json.dumps(result, indent=2)
    except Exception as e:
        return f"Error: {e}"


# Fetch tools from daemon at startup and cache them
def fetch_daemon_tools() -> list[dict]:
    """Fetch tool definitions from daemon."""
    if not ensure_daemon():
        return []

    req = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/list",
        "params": {}
    }

    response = client.request(json.dumps(req))
    if not response:
        return []

    try:
        data = json.loads(response)
        return data.get("result", {}).get("tools", [])
    except Exception:
        return []


# Cache tools at module load
CACHED_TOOLS: list[Tool] = []


def init_tools():
    """Initialize tools from daemon."""
    global CACHED_TOOLS
    daemon_tools = fetch_daemon_tools()

    CACHED_TOOLS = [
        Tool(
            name=t["name"],
            description=t.get("description", ""),
            inputSchema=t.get("inputSchema", {"type": "object", "properties": {}})
        )
        for t in daemon_tools
    ]


@server.list_tools()
async def list_tools():
    """Return cached tools from daemon."""
    if not CACHED_TOOLS:
        init_tools()
    return CACHED_TOOLS


@server.call_tool()
async def call_tool(name: str, arguments: dict):
    """Forward tool call to daemon using original tool name."""
    result = daemon_call(name, arguments)
    return [TextContent(type="text", text=result)]


def main():
    # Initialize tools eagerly at startup (before MCP SDK takes over)
    init_tools()

    async def run():
        init_options = InitializationOptions(
            server_name="chitta",
            server_version="0.1.0",
            capabilities=ServerCapabilities(tools=ToolsCapability())
        )
        async with stdio_server() as (read_stream, write_stream):
            await server.run(read_stream, write_stream, init_options)

    asyncio.run(run())


if __name__ == "__main__":
    main()
