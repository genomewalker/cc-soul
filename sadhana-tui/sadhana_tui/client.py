"""ChittaClient - RPC client for communicating with chittad daemon."""

import json
import os
import socket
from typing import Any, Optional


def djb2_hash(s: str) -> int:
    """DJB2 hash algorithm matching C++ implementation."""
    h = 5381
    for c in s:
        h = ((h << 5) + h + ord(c)) & 0xFFFFFFFF
    return h


def get_socket_dir() -> str:
    """Get persistent socket directory (matches C++ daemon logic)."""
    xdg_runtime = os.environ.get("XDG_RUNTIME_DIR")
    if xdg_runtime and os.access(xdg_runtime, os.W_OK):
        socket_dir = os.path.join(xdg_runtime, "chitta")
        os.makedirs(socket_dir, mode=0o700, exist_ok=True)
        return socket_dir
    home = os.environ.get("HOME")
    if home:
        cache_dir = os.path.join(home, ".cache")
        os.makedirs(cache_dir, mode=0o755, exist_ok=True)
        socket_dir = os.path.join(cache_dir, "chitta")
        os.makedirs(socket_dir, mode=0o700, exist_ok=True)
        return socket_dir
    return "/tmp"


def get_socket_path() -> str:
    """Get the daemon socket path."""
    home = os.environ.get("HOME", "")
    mind_path = os.path.join(home, ".claude", "mind")
    hash_val = djb2_hash(mind_path)
    return os.path.join(get_socket_dir(), f"chitta-{hash_val}.sock")


class ChittaClient:
    """Client for communicating with chittad daemon via Unix socket."""

    def __init__(self, socket_path: Optional[str] = None):
        self.socket_path = socket_path or get_socket_path()
        self.sock: Optional[socket.socket] = None
        self._request_id = 0

    def connect(self) -> bool:
        """Connect to daemon socket."""
        try:
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.settimeout(5.0)
            self.sock.connect(self.socket_path)
            return True
        except Exception:
            self.sock = None
            return False

    def close(self):
        """Close socket connection."""
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None

    def is_connected(self) -> bool:
        """Check if socket is connected."""
        return self.sock is not None

    def reconnect(self) -> bool:
        """Reconnect to daemon."""
        self.close()
        return self.connect()

    def _send_request(self, data: str) -> Optional[str]:
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
            self.sock = None
            return None

    def call(self, tool: str, args: Optional[dict] = None) -> dict:
        """Call a tool on the daemon.

        Returns the structured result dict, or {"error": ...} on failure.
        Daemon uses one request per connection, so we reconnect each time.
        """
        # Always reconnect - daemon closes connection after each response
        self.close()
        if not self.connect():
            return {"error": "Not connected to daemon"}

        self._request_id += 1
        req = {
            "jsonrpc": "2.0",
            "id": self._request_id,
            "method": "tools/call",
            "params": {"name": tool, "arguments": args or {}}
        }

        response = self._send_request(json.dumps(req))

        # Retry once on connection failure
        if not response:
            self.close()
            if self.connect():
                response = self._send_request(json.dumps(req))

        if not response:
            return {"error": "No response from daemon"}

        try:
            data = json.loads(response)
            if "error" in data:
                return {"error": data["error"].get("message", str(data["error"]))}
            result = data.get("result", {})
            if result is None:
                return {"error": "Null result from daemon"}
            # Check for tool-level error (isError flag in MCP response)
            if result.get("isError"):
                # Extract error text from content array
                content = result.get("content", [])
                if content and isinstance(content, list):
                    text = content[0].get("text", "Unknown error") if content else "Unknown error"
                    return {"error": text}
                return {"error": "Tool execution failed"}
            # Return structured data if present, otherwise the full result
            structured = result.get("structured")
            return structured if structured is not None else result
        except json.JSONDecodeError as e:
            return {"error": f"Invalid JSON: {e}"}

    def sadhana_list(self) -> list[dict[str, Any]]:
        """Get list of all sadhanas."""
        result = self.call("sadhana_list")
        if "error" in result:
            return []
        return result.get("sadhanas", [])

    def sadhana_status(self, sadhana_id: int, history_limit: int = 20) -> dict:
        """Get status and history for a specific sadhana."""
        return self.call("sadhana_status", {"id": sadhana_id, "history_limit": history_limit})

    def sadhana_start(
        self,
        goal: str,
        brain: str = "claude",
        model: str = "haiku",
        interval: int = 300
    ) -> dict:
        """Start a new sadhana."""
        return self.call("sadhana_start", {
            "goal": goal,
            "brain_provider": brain,
            "brain_model": model,
            "interval_seconds": interval
        })

    def sadhana_stop(self, sadhana_id: int) -> dict:
        """Stop a sadhana."""
        return self.call("sadhana_stop", {"id": sadhana_id})

    def sadhana_pause(self, sadhana_id: int) -> dict:
        """Pause a sadhana."""
        return self.call("sadhana_pause", {"id": sadhana_id})

    def sadhana_resume(self, sadhana_id: int) -> dict:
        """Resume a paused sadhana."""
        return self.call("sadhana_resume", {"id": sadhana_id})

    def sadhana_set_model(self, sadhana_id: int, model: str) -> dict:
        """Change the model for a sadhana."""
        return self.call("sadhana_set_model", {"id": sadhana_id, "model": model})

    def watch_events(self, sadhana_id: int = 0):
        """Generator: yields event JSON strings pushed from daemon in real-time.

        First yielded line is the ack JSON (contains "jsonrpc" key).
        Subsequent lines are pushed event JSON objects (no "jsonrpc" key).
        Yields None on 30s timeout (heartbeat — allows caller to check liveness).
        Returns (StopIteration) when the connection closes.

        Usage:
            for line in client.watch_events(sadhana_id=0):
                if line is None: continue  # timeout
                data = json.loads(line)
                if "jsonrpc" in data: continue  # skip ack
                handle(data)  # pushed event
        """
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(30.0)
        try:
            sock.connect(self.socket_path)
        except Exception:
            return

        self._request_id += 1
        req = {
            "jsonrpc": "2.0",
            "id": self._request_id,
            "method": "tools/call",
            "params": {"name": "sadhana_watch", "arguments": {"id": sadhana_id}}
        }
        try:
            sock.sendall((json.dumps(req) + "\n").encode())
        except Exception:
            sock.close()
            return

        buf = b""
        try:
            while True:
                try:
                    chunk = sock.recv(4096)
                    if not chunk:
                        break
                    buf += chunk
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        line_str = line.decode().strip()
                        if line_str:
                            yield line_str
                except socket.timeout:
                    yield None  # Heartbeat — lets caller check if it should exit
                except Exception:
                    break
        finally:
            try:
                sock.close()
            except Exception:
                pass

    def health_check(self) -> dict:
        """Check daemon health."""
        return self.call("health_check")

    def soul_repl(self, code: str, reset: bool = False) -> dict:
        """Execute RLM-style code in soul_repl sandbox.

        Example:
            result = client.soul_repl('''
                memories = soul.search("goal context", limit=5)
                relevant = [m for m in memories if m.score > 0.5]
                soul.expand(relevant[0].id) if relevant else {}
            ''')

        Returns dict with 'output', 'error', 'trajectory'.
        """
        return self.call("soul_repl", {"code": code, "reset": reset})

    def smart_context_rlm(self, task: str) -> dict:
        """Get RLM-style smart context for a task.

        Uses soul_repl internally for dynamic exploration.
        """
        return self.call("smart_context", {"task": task, "mode": "rlm"})

    def explore_for_goal(self, goal: str, limit: int = 10) -> list[dict]:
        """RLM exploration tailored for sadhana sense phase.

        Searches memories relevant to the goal and returns structured context.
        """
        code = f'''
results = []
memories = soul.search("{goal[:200]}", limit={limit})
for m in memories[:5]:
    if m.score > 0.3:
        results.append({{"id": m.id, "score": m.score, "content": m.content[:200]}})
results
'''
        result = self.soul_repl(code)
        if "error" in result:
            return []
        # Parse the result output
        try:
            # The result is in the output, parse it
            output = result.get("output", "")
            if "[" in output:
                import ast
                return ast.literal_eval(output.strip().split("\n")[-1])
        except Exception:
            pass
        return []
