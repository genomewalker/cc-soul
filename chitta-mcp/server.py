#!/usr/bin/env python3
"""
Chitta MCP Server - Python bridge to chittad daemon.

Uses the official MCP SDK to expose chitta tools to Claude Code.
Tools are defined statically (like opencode-bridge) for proper discovery.

Includes composite tools for token-efficient code intelligence:
- read_symbol: Read just a symbol's code, not entire file
- read_function: Convenience wrapper for read_symbol
- symbol_callers: Find all callers via triplet queries
- smart_context: Task-aware context assembly
"""

import os
import json
import socket
import asyncio
import logging
import re
from typing import Optional, Dict, Any, List

# Suppress MCP SDK validation warnings (Claude Code sends incomplete initialize requests)
logging.getLogger("root").setLevel(logging.ERROR)
logging.getLogger("mcp").setLevel(logging.ERROR)

from mcp.server import Server, InitializationOptions
from mcp.server.stdio import stdio_server
from mcp.types import TextContent, ServerCapabilities, ToolsCapability

from tools_static import TOOLS, COMPOSITE_TOOLS


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
server = Server("chitta-mcp")


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


def daemon_call(tool_name: str, arguments: dict, structured: bool = False) -> str:
    """Call a tool on the daemon.

    Args:
        tool_name: Name of the tool to call
        arguments: Tool arguments
        structured: If True, return structured JSON data instead of text
    """
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

        # Return structured data if requested
        if structured and "structured" in result:
            return json.dumps(result["structured"])

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


@server.list_tools()
async def list_tools():
    """Return static tools (imported from tools_static.py)."""
    return TOOLS + COMPOSITE_TOOLS


# Composite tool handlers for token-efficient code intelligence
def read_file_lines(file_path: str, start: int, end: int) -> str:
    """Read specific line range from a file."""
    try:
        with open(file_path, 'r', encoding='utf-8', errors='replace') as f:
            lines = f.readlines()

        # Clamp to valid range
        start = max(1, start)
        end = min(len(lines), end)

        return ''.join(lines[start-1:end])
    except Exception as e:
        return f"Error reading file: {e}"


def handle_read_symbol(arguments: dict) -> str:
    """
    Read just a symbol's code, not entire file.

    1. find_symbol(name, kind) → get file + line range
    2. Read only line_start - 3 to line_end + 1
    3. Return: [symbol @ file:start-end] + code

    Token savings: ~10x vs full file read
    """
    name = arguments.get("name", "")
    kind = arguments.get("kind")
    project = arguments.get("project")
    context_lines = arguments.get("context", 3)  # Lines before/after

    if not name:
        return "Error: name parameter required"

    # Call find_symbol on daemon with structured=True to get line numbers
    find_args = {"name": name}
    if kind:
        find_args["kind"] = kind

    response = daemon_call("find_symbol", find_args, structured=True)

    # Parse structured response: {"count": N, "symbols": [...]}
    try:
        data = json.loads(response)

        symbols = data.get("symbols", [])
        if not symbols:
            return f"No symbol found: {name}"

        # Use first matching symbol
        symbol = symbols[0]
        file_path = symbol.get("file", "")
        line_start = symbol.get("line_start", 1)
        line_end = symbol.get("line_end", line_start + 50)
        symbol_kind = symbol.get("kind", kind or "symbol")
        symbol_name = symbol.get("name", name)

    except (json.JSONDecodeError, KeyError, TypeError) as e:
        return f"Error parsing symbol data: {e}\nResponse: {response[:200]}"

    if not file_path or not os.path.exists(file_path):
        return f"Symbol found but file not accessible: {response}"

    # Read with context
    start = max(1, line_start - context_lines)
    end = line_end + 1

    code = read_file_lines(file_path, start, end)

    # Calculate token estimate (rough: ~4 chars per token)
    tokens = len(code) // 4

    header = f"[{symbol_kind} {symbol_name} @ {file_path}:{line_start}-{line_end}] (~{tokens} tokens)"
    return f"{header}\n{code}"


def handle_read_function(arguments: dict) -> str:
    """Convenience wrapper for read_symbol with kind=function."""
    arguments["kind"] = arguments.get("kind", "function")
    return handle_read_symbol(arguments)


def handle_symbol_callers(arguments: dict) -> str:
    """
    Find all callers of a symbol without grep.
    Query triplets where object = symbol_name and predicate = calls.
    """
    name = arguments.get("name", "")
    limit = arguments.get("limit", 20)

    if not name:
        return "Error: name parameter required"

    # Query triplets where this symbol is called
    response = daemon_call("query", {"object": name, "predicate": "calls"})

    try:
        data = json.loads(response)
        triplets = data if isinstance(data, list) else data.get("triplets", [])

        if not triplets:
            return f"No callers found for: {name}"

        # Format results
        results = []
        for t in triplets[:limit]:
            caller = t.get("subject", "unknown")
            results.append(f"  {caller} → {name}")

        return f"Callers of {name} ({len(results)} found):\n" + "\n".join(results)

    except json.JSONDecodeError:
        return response  # Return raw response


def handle_symbol_callees(arguments: dict) -> str:
    """
    Find all symbols that this symbol calls.
    Query triplets where subject = symbol_name and predicate = calls.
    """
    name = arguments.get("name", "")
    limit = arguments.get("limit", 20)

    if not name:
        return "Error: name parameter required"

    # Query triplets where this symbol calls others
    response = daemon_call("query", {"subject": name, "predicate": "calls"})

    try:
        data = json.loads(response)
        triplets = data if isinstance(data, list) else data.get("triplets", [])

        if not triplets:
            return f"No callees found for: {name}"

        # Format results
        results = []
        for t in triplets[:limit]:
            callee = t.get("object", "unknown")
            results.append(f"  {name} → {callee}")

        return f"{name} calls ({len(results)} found):\n" + "\n".join(results)

    except json.JSONDecodeError:
        return response  # Return raw response


def handle_smart_context(arguments: dict) -> str:
    """
    Build minimal context for a task.

    1. search_symbols(task) for code matches
    2. full_resonate(task) for memories
    3. Compress to fit token limit
    """
    task = arguments.get("task", "")
    token_limit = arguments.get("limit", 500)
    include_memories = arguments.get("memories", True)
    include_code = arguments.get("code", True)

    if not task:
        return "Error: task parameter required"

    results = []
    tokens_used = 0
    char_limit = token_limit * 4  # Rough: 4 chars per token

    # Get relevant code symbols
    if include_code:
        code_response = daemon_call("search_symbols", {"query": task, "limit": 5})
        try:
            code_data = json.loads(code_response)
            symbols = code_data if isinstance(code_data, list) else code_data.get("symbols", [])

            if symbols:
                code_section = ["## Code Context"]
                for sym in symbols[:3]:  # Top 3 most relevant
                    name = sym.get("name", "?")
                    kind = sym.get("kind", "?")
                    file_path = sym.get("file", "?")
                    line = sym.get("line_start", "?")
                    score = sym.get("score", 0)
                    code_section.append(f"- {kind} `{name}` @ {file_path}:{line} (score: {score:.2f})")

                section_text = "\n".join(code_section)
                if tokens_used + len(section_text)//4 < token_limit:
                    results.append(section_text)
                    tokens_used += len(section_text) // 4

        except json.JSONDecodeError:
            pass

    # Get relevant memories
    if include_memories:
        mem_response = daemon_call("full_resonate", {"query": task, "k": 5})

        # Extract just the memory content, not the full formatted output
        if mem_response and "No memories" not in mem_response:
            mem_section = ["## Memory Context"]

            # Parse memory format: [score%] [type] content
            for line in mem_response.split("\n"):
                if line.startswith("[") and "%" in line:
                    # Extract content after type tag
                    match = re.match(r'\[\d+%\]\s*\[[^\]]+\]\s*(.+)', line)
                    if match:
                        content = match.group(1)[:100]  # Truncate
                        if tokens_used + len(content)//4 < token_limit:
                            mem_section.append(f"- {content}")
                            tokens_used += len(content) // 4

            if len(mem_section) > 1:
                results.append("\n".join(mem_section))

    if not results:
        return f"No relevant context found for: {task}"

    header = f"## Smart Context (~{tokens_used} tokens)\nTask: {task}\n"
    return header + "\n\n".join(results)


# Map composite tool names to handlers
COMPOSITE_HANDLERS = {
    "read_symbol": handle_read_symbol,
    "read_function": handle_read_function,
    "symbol_callers": handle_symbol_callers,
    "symbol_callees": handle_symbol_callees,
    "smart_context": handle_smart_context,
}


@server.call_tool()
async def call_tool(name: str, arguments: dict):
    """Handle tool calls - composite tools handled locally, others forwarded to daemon."""

    # Check if this is a composite tool
    if name in COMPOSITE_HANDLERS:
        result = COMPOSITE_HANDLERS[name](arguments)
    else:
        # Forward to daemon
        result = daemon_call(name, arguments)

    return [TextContent(type="text", text=result)]


def main():
    async def run():
        init_options = InitializationOptions(
            server_name="chitta-mcp",
            server_version="0.1.0",
            capabilities=ServerCapabilities(tools=ToolsCapability())
        )
        async with stdio_server() as (read_stream, write_stream):
            await server.run(read_stream, write_stream, init_options)

    asyncio.run(run())


if __name__ == "__main__":
    main()
