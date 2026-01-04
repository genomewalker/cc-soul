"""
Synapse MCP Client: Python interface to synapse via MCP protocol.

Uses a subprocess-based MCP client to communicate with the C++ synapse server.
Avoids glibc version issues on RHEL8 and similar systems.

Usage:
    from synapse_mcp import Soul, SynapseGraph

    soul = Soul()
    soul.grow_wisdom("title", "content")
    results = soul.search("query")
"""

import json
import subprocess
import os
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Any
from dataclasses import dataclass


def _find_synapse_mcp() -> Path:
    """Find the synapse_mcp binary."""
    # Check common locations
    candidates = [
        Path(__file__).parent.parent / "build" / "synapse_mcp",
        Path.home() / ".local" / "bin" / "synapse_mcp",
        Path("/usr/local/bin/synapse_mcp"),
    ]

    # Check SYNAPSE_MCP_PATH env var
    env_path = os.environ.get("SYNAPSE_MCP_PATH")
    if env_path:
        candidates.insert(0, Path(env_path))

    for path in candidates:
        if path.exists() and os.access(path, os.X_OK):
            return path

    raise RuntimeError(
        "synapse_mcp not found. Set SYNAPSE_MCP_PATH or install to ~/.local/bin/"
    )


def _find_project_root() -> Optional[Path]:
    """Find project root by looking for .git or .synapse directory."""
    cwd = Path.cwd()
    for parent in [cwd] + list(cwd.parents):
        if (parent / ".git").exists() or (parent / ".synapse").exists():
            return parent
        # Stop at home directory
        if parent == Path.home():
            break
    return None


def get_project_mind_path(project_root: Optional[Path] = None) -> Optional[Path]:
    """Get project-specific mind path if in a project."""
    if project_root is None:
        project_root = _find_project_root()
    if project_root:
        return project_root / ".synapse" / "mind"
    return None


def get_global_mind_path() -> Path:
    """Get global mind path."""
    return Path.home() / ".claude" / "mind" / "synapse"


def _find_model_files() -> Tuple[Optional[Path], Optional[Path]]:
    """Find ONNX model and vocab files."""
    candidates = [
        Path(__file__).parent.parent / "models",
        Path.home() / ".local" / "share" / "synapse" / "models",
        Path("/usr/local/share/synapse/models"),
    ]

    env_path = os.environ.get("SYNAPSE_MODELS_PATH")
    if env_path:
        candidates.insert(0, Path(env_path))

    for path in candidates:
        model = path / "model.onnx"
        vocab = path / "vocab.txt"
        if model.exists() and vocab.exists():
            return model, vocab

    return None, None


@dataclass
class MCPResponse:
    """Response from MCP server."""
    id: int
    result: Optional[Dict] = None
    error: Optional[Dict] = None

    @property
    def success(self) -> bool:
        return self.error is None

    @property
    def content(self) -> str:
        if self.result and "content" in self.result:
            for item in self.result["content"]:
                if item.get("type") == "text":
                    return item.get("text", "")
        return ""


class MCPClient:
    """Low-level MCP client for synapse_mcp server."""

    def __init__(
        self,
        path: Optional[Path] = None,
        model_path: Optional[Path] = None,
        vocab_path: Optional[Path] = None,
    ):
        self._mcp_bin = _find_synapse_mcp()
        self._path = path or Path.home() / ".claude" / "mind" / "synapse"

        if model_path is None or vocab_path is None:
            model_path, vocab_path = _find_model_files()

        self._model_path = model_path
        self._vocab_path = vocab_path
        self._request_id = 0
        self._proc: Optional[subprocess.Popen] = None

    def _start_server(self) -> None:
        """Start the MCP server subprocess."""
        if self._proc is not None and self._proc.poll() is None:
            return  # Already running

        cmd = [str(self._mcp_bin), "--path", str(self._path)]
        if self._model_path and self._vocab_path:
            cmd.extend(["--model", str(self._model_path), "--vocab", str(self._vocab_path)])

        self._proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        # Send initialize
        self._send({"method": "initialize", "params": {}})

    def _send(self, request: Dict) -> MCPResponse:
        """Send a request and get response."""
        if self._proc is None or self._proc.poll() is not None:
            self._start_server()

        self._request_id += 1
        request["jsonrpc"] = "2.0"
        request["id"] = self._request_id

        self._proc.stdin.write(json.dumps(request) + "\n")
        self._proc.stdin.flush()

        line = self._proc.stdout.readline()
        if not line:
            raise RuntimeError("MCP server closed unexpectedly")

        response = json.loads(line)
        return MCPResponse(
            id=response.get("id", 0),
            result=response.get("result"),
            error=response.get("error"),
        )

    def call_tool(self, name: str, arguments: Dict = None) -> MCPResponse:
        """Call an MCP tool."""
        return self._send({
            "method": "tools/call",
            "params": {
                "name": name,
                "arguments": arguments or {},
            }
        })

    def close(self) -> None:
        """Shutdown the MCP server."""
        if self._proc is not None and self._proc.poll() is None:
            try:
                self._send({"method": "shutdown", "params": {}})
                self._proc.wait(timeout=5)
            except Exception:
                self._proc.kill()
            self._proc = None

    def __del__(self):
        self.close()


class Soul:
    """
    Soul interface via MCP subprocess.

    Path resolution:
    - If path is provided, use it directly
    - If use_project=True and in a project, use project-specific path
    - Otherwise use global path (~/.claude/mind/synapse)
    """

    def __init__(
        self,
        path: Optional[str] = None,
        use_project: bool = False,
        project_root: Optional[str] = None,
    ):
        if path:
            self._path = Path(path)
        elif use_project:
            proj_root = Path(project_root) if project_root else None
            proj_path = get_project_mind_path(proj_root)
            if proj_path:
                self._path = proj_path
                # Create directory if needed
                self._path.parent.mkdir(parents=True, exist_ok=True)
            else:
                self._path = get_global_mind_path()
        else:
            self._path = get_global_mind_path()

        self._client = MCPClient(path=self._path)
        self._project_root = _find_project_root() if use_project else None

    def save(self, path: Optional[str] = None) -> None:
        """Save to disk (triggers cycle with save=true)."""
        self._client.call_tool("cycle", {"save": True})

    # --- Grow operations ---

    def grow_wisdom(
        self,
        title: str,
        content: str,
        domain: Optional[str] = None,
        confidence: float = 0.8,
    ) -> str:
        """Add wisdom to the soul."""
        resp = self._client.call_tool("grow", {
            "type": "wisdom",
            "title": title,
            "content": content,
            "domain": domain,
            "confidence": confidence,
        })
        # Extract ID from response content
        if resp.success:
            # Parse "Grew wisdom: ... (id: xxx)"
            text = resp.content
            if "(id: " in text:
                return text.split("(id: ")[1].rstrip(")")
        return ""

    def hold_belief(self, statement: str, strength: float = 0.9) -> str:
        """Add an immutable belief."""
        resp = self._client.call_tool("grow", {
            "type": "belief",
            "content": statement,
            "confidence": strength,
        })
        if resp.success and "(id: " in resp.content:
            return resp.content.split("(id: ")[1].rstrip(")")
        return ""

    def record_failure(
        self,
        what_failed: str,
        why_it_failed: str,
        domain: Optional[str] = None,
    ) -> str:
        """Record a failure."""
        resp = self._client.call_tool("grow", {
            "type": "failure",
            "title": what_failed,
            "content": why_it_failed,
            "domain": domain,
        })
        if resp.success and "(id: " in resp.content:
            return resp.content.split("(id: ")[1].rstrip(")")
        return ""

    def aspire(
        self,
        direction: str,
        why: str,
        timeframe: Optional[str] = None,
        confidence: float = 0.7,
    ) -> str:
        """Add an aspiration."""
        content = direction
        if why:
            content += f" (because: {why})"
        if timeframe:
            content += f" [timeframe: {timeframe}]"

        resp = self._client.call_tool("grow", {
            "type": "aspiration",
            "content": content,
            "confidence": confidence,
        })
        if resp.success and "(id: " in resp.content:
            return resp.content.split("(id: ")[1].rstrip(")")
        return ""

    def dream(
        self,
        vision: str,
        inspiration: Optional[str] = None,
        confidence: float = 0.6,
    ) -> str:
        """Add a dream."""
        content = vision
        if inspiration:
            content += f" (inspired by: {inspiration})"

        resp = self._client.call_tool("grow", {
            "type": "dream",
            "content": content,
            "confidence": confidence,
        })
        if resp.success and "(id: " in resp.content:
            return resp.content.split("(id: ")[1].rstrip(")")
        return ""

    def learn_term(
        self,
        term: str,
        definition: str,
        domain: Optional[str] = None,
        examples: Optional[List[str]] = None,
    ) -> str:
        """Add a vocabulary term."""
        content = f"{term} = {definition}"
        if examples:
            content += f" (examples: {', '.join(examples)})"

        resp = self._client.call_tool("grow", {
            "type": "term",
            "title": term,
            "content": content,
            "domain": domain,
        })
        if resp.success and "(id: " in resp.content:
            return resp.content.split("(id: ")[1].rstrip(")")
        return ""

    # --- Observe ---

    def observe(
        self,
        category: str,
        title: str,
        content: str,
        project: Optional[str] = None,
        tags: Optional[List[str]] = None,
    ) -> str:
        """Record an observation (episode)."""
        resp = self._client.call_tool("observe", {
            "category": category,
            "title": title,
            "content": content,
            "project": project,
            "tags": ",".join(tags) if tags else None,
        })
        if resp.success and resp.result:
            return resp.result.get("id", "")
        return ""

    # --- Search ---

    def search(
        self,
        query: str,
        limit: int = 10,
        threshold: float = 0.3,
    ) -> List[Tuple[str, float, Dict]]:
        """Semantic search."""
        resp = self._client.call_tool("recall", {
            "query": query,
            "limit": limit,
            "threshold": threshold,
        })
        if resp.success:
            # Parse results from text content (MCP format)
            text = resp.content
            results = []
            for line in text.split('\n'):
                if line.startswith('[') and '%]' in line:
                    # Parse "[32.6%] Title: content..."
                    try:
                        pct_end = line.index('%]')
                        pct = float(line[1:pct_end]) / 100.0
                        rest = line[pct_end + 2:].strip()
                        # Split on first colon to get title vs content
                        if ': ' in rest:
                            title, content = rest.split(': ', 1)
                        else:
                            title = rest
                            content = rest
                        results.append((
                            "",  # ID not in text output
                            pct,
                            {"title": title, "content": content, "text": rest}
                        ))
                    except (ValueError, IndexError):
                        pass
            return results
        return []

    # --- Context ---

    def get_context(self, query: Optional[str] = None) -> Dict:
        """Get soul context."""
        resp = self._client.call_tool("soul_context", {
            "query": query,
            "format": "json",
        })
        if resp.success and resp.result:
            # Parse structured from content
            try:
                return json.loads(resp.content)
            except json.JSONDecodeError:
                return {}
        return {}

    def format_context(self, query: Optional[str] = None) -> str:
        """Format context as text."""
        resp = self._client.call_tool("soul_context", {
            "query": query,
            "format": "text",
        })
        return resp.content if resp.success else ""

    # --- Dynamics ---

    def cycle(self) -> Tuple[int, float]:
        """Run maintenance cycle."""
        resp = self._client.call_tool("cycle", {"save": True})
        if resp.success and resp.result:
            return (
                resp.result.get("triggers_fired", 0),
                resp.result.get("coherence", 0.0),
            )
        return (0, 0.0)

    def coherence(self) -> float:
        """Get current coherence."""
        resp = self._client.call_tool("soul_context", {"format": "json"})
        if resp.success:
            try:
                data = json.loads(resp.content)
                return data.get("coherence", {}).get("tau_k", 0.0)
            except json.JSONDecodeError:
                pass
        return 0.0

    def tick_dynamics(self) -> Dict:
        """Tick dynamics engine."""
        resp = self._client.call_tool("cycle", {"save": False})
        if resp.success and resp.result:
            return resp.result
        return {}

    def snapshot(self) -> int:
        """Create snapshot."""
        resp = self._client.call_tool("cycle", {"save": True})
        if resp.success and resp.result:
            return 1  # Simplified - just indicates success
        return 0

    def close(self) -> None:
        """Close the MCP connection."""
        self._client.close()

    def __del__(self):
        self.close()


# Alias for compatibility
SynapseGraph = Soul


# Module-level convenience for testing
def get_embedder():
    """Get embedder info (for compatibility)."""
    return {"type": "onnx", "model": "all-MiniLM-L6-v2"}
