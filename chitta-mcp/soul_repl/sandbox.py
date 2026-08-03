"""
SoulAPI - Sandboxed API for memory exploration

Provides soul.* methods that map to chitta daemon RPC calls.
Used inside the REPL sandbox for programmatic memory access.
"""

import json
import socket
import os
from dataclasses import dataclass
from typing import Optional


def find_socket() -> str:
    """Find the chitta daemon socket."""
    runtime_dir = os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")
    socket_dir = os.path.join(runtime_dir, "chitta")

    if os.path.isdir(socket_dir):
        for f in os.listdir(socket_dir):
            if f.startswith("chitta-") and f.endswith(".sock"):
                return os.path.join(socket_dir, f)

    # Fallback to cache dir
    cache_dir = os.path.expanduser("~/.cache/chitta")
    if os.path.isdir(cache_dir):
        for f in os.listdir(cache_dir):
            if f.startswith("chitta-") and f.endswith(".sock"):
                return os.path.join(cache_dir, f)

    raise RuntimeError("No chitta socket found - is chittad running?")


def daemon_call(tool: str, args: dict) -> dict:
    """Call chitta daemon via Unix socket."""
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(30)

    try:
        sock.connect(find_socket())
        request = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {"name": tool, "arguments": args}
        }
        sock.sendall((json.dumps(request) + "\n").encode())

        response = b""
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                break
            response += chunk
            if b"\n" in chunk:
                break

        # Handle encoding issues gracefully
        result = json.loads(response.decode('utf-8', errors='replace'))
        if "error" in result:
            raise RuntimeError(result["error"].get("message", str(result["error"])))

        res = result.get("result", {})

        # Prefer structured data if available
        structured = res.get("structured", {})
        if structured:
            return structured

        # Fall back to text content
        content = res.get("content", [{}])
        text = content[0].get("text", "") if content else ""
        return {"text": text}
    finally:
        sock.close()


@dataclass
class Memory:
    """A memory from the soul."""
    id: int
    content: str
    score: float = 0.0
    tags: list = None
    created_at: str = ""

    def __post_init__(self):
        self.tags = self.tags or []

    def __repr__(self):
        preview = self.content[:60] + "..." if len(self.content) > 60 else self.content
        return f"Memory({self.id}, score={self.score:.2f}, '{preview}')"


@dataclass
class Triplet:
    """A relationship triplet."""
    subject: str
    predicate: str
    object: str

    def __repr__(self):
        return f"({self.subject}) --[{self.predicate}]--> ({self.object})"


class SoulAPI:
    """
    RLM-style API for programmatic soul exploration.

    Example usage in REPL:
        memories = soul.search("authentication bugs", limit=10)
        relevant = [m for m in memories if m.score > 0.7]
        full = soul.expand(relevant[0].id)
        relations = soul.triplets(f"memory:{relevant[0].id}")
    """

    def __init__(self):
        self._trajectory = []  # Track all calls for episode recording

    def _track(self, method: str, args: dict, result):
        """Track call for trajectory."""
        self._trajectory.append({
            "method": method,
            "args": args,
            "result_preview": str(result)[:200]
        })

    def search(self, query: str, limit: int = 20, threshold: float = 0.0) -> list[Memory]:
        """
        Semantic search across memories.

        Args:
            query: Natural language search query
            limit: Maximum results to return
            threshold: Minimum similarity score (0.0-1.0)

        Returns:
            List of Memory objects sorted by relevance
        """
        try:
            result = daemon_call("recall", {"query": query, "limit": limit})
        except RuntimeError as e:
            # Handle daemon errors gracefully (e.g., UTF-8 issues)
            if "UTF-8" in str(e) or "json" in str(e).lower():
                # Try with shorter query
                short_query = query.split()[0] if " " in query else query[:20]
                try:
                    result = daemon_call("recall", {"query": short_query, "limit": limit})
                except:
                    return []
            else:
                raise
        memories = []

        # Handle both "memories" and "results" keys
        items = result.get("memories", result.get("results", []))
        for m in items:
            # Extract ID - could be numeric id or UUID string
            mem_id = m.get("id", 0)
            if isinstance(mem_id, str) and mem_id.startswith("00000000"):
                # Extract numeric suffix from UUID
                try:
                    mem_id = int(mem_id.split("-")[-1], 16)
                except:
                    mem_id = 0

            # Honest display signal: bounded max(cosine, lexical-overlap), mirroring
            # the C++ display_pct. The composite relevance/score is unbounded (>=1 for
            # keyword-route hits) and renders as a flat 100% for every junk hit.
            score = max(float(m.get("similarity") or 0), float(m.get("lexical") or 0))
            content = m.get("content", m.get("text", m.get("summary", "")))
            if score >= threshold:
                memories.append(Memory(
                    id=mem_id,
                    content=content,
                    score=score,
                    tags=m.get("tags", []),
                    created_at=m.get("created_at", "")
                ))

        self._track("search", {"query": query, "limit": limit}, memories)
        return memories

    def recall(self, query: str, limit: int = 10) -> list[Memory]:
        """
        Recall memories by semantic similarity.

        Alias for search() for API consistency.
        """
        result = daemon_call("recall", {"query": query, "limit": limit})
        memories = []

        items = result.get("memories", result.get("results", []))
        for m in items:
            mem_id = m.get("id", 0)
            if isinstance(mem_id, str) and mem_id.startswith("00000000"):
                try:
                    mem_id = int(mem_id.split("-")[-1], 16)
                except:
                    mem_id = 0

            memories.append(Memory(
                id=mem_id,
                content=m.get("content", m.get("text", "")),
                score=max(float(m.get("similarity") or 0), float(m.get("lexical") or 0)),
                tags=m.get("tags", []),
                created_at=m.get("created_at", "")
            ))

        self._track("recall", {"query": query, "limit": limit}, memories)
        return memories

    def expand(self, memory_id: int, depth: int = 3) -> dict:
        """
        Expand a memory to get full context (SSL -> Episode -> Full turns).

        Args:
            memory_id: ID of the memory to expand
            depth: How deep to expand (1=memory, 2=+episode, 3=+full turns)

        Returns:
            Dict with expanded context at each level
        """
        result = daemon_call("expand_memory", {"id": memory_id, "depth": depth})
        self._track("expand", {"memory_id": memory_id, "depth": depth}, result)
        return result

    def triplets(self, subject: str = None, predicate: str = None,
                 object: str = None, limit: int = 50) -> list[Triplet]:
        """
        Query the knowledge graph.

        Args:
            subject: Filter by subject (e.g., "memory:123", "file:main.py")
            predicate: Filter by relationship type (e.g., "derived_from", "mentions")
            object: Filter by object
            limit: Maximum results

        Returns:
            List of Triplet objects
        """
        args = {"limit": limit}
        if subject:
            args["subject"] = subject
        if predicate:
            args["predicate"] = predicate
        if object:
            args["object"] = object

        result = daemon_call("query_graph", args)
        triplets = [
            Triplet(t["subject"], t["predicate"], t["object"])
            for t in result.get("triplets", [])
        ]

        self._track("triplets", args, triplets)
        return triplets

    def recent(self, hours: int = 24, limit: int = 50) -> list[Memory]:
        """
        Get recent memories by time.

        Note: Uses explore_recall for temporal filtering.
        """
        # Use explore_recall which supports temporal queries
        result = daemon_call("explore_recall", {
            "query": "*",  # Broad match
            "hours": hours,
            "limit": limit
        })

        memories = []
        items = result.get("memories", result.get("results", []))
        for m in items:
            mem_id = m.get("id", 0)
            if isinstance(mem_id, str) and mem_id.startswith("00000000"):
                try:
                    mem_id = int(mem_id.split("-")[-1], 16)
                except:
                    mem_id = 0

            memories.append(Memory(
                id=mem_id,
                content=m.get("content", m.get("text", "")),
                score=m.get("recency_score", m.get("score", 1.0)),
                tags=m.get("tags", []),
                created_at=m.get("created_at", "")
            ))

        self._track("recent", {"hours": hours, "limit": limit}, memories)
        return memories

    def remember(self, content: str, tags: list[str] = None,
                 importance: float = 0.5) -> int:
        """
        Store a new memory.

        Args:
            content: Memory content
            tags: Optional tags for categorization
            importance: Importance score (0.0-1.0)

        Returns:
            ID of the created memory
        """
        args = {"content": content, "importance": importance}
        if tags:
            args["tags"] = tags

        result = daemon_call("remember", args)
        memory_id = result.get("id", result.get("memory_id", 0))

        self._track("remember", {"content": content[:50], "tags": tags}, memory_id)
        return memory_id

    def symbols(self, pattern: str = None, kind: str = None, limit: int = 50) -> list[dict]:
        """
        Search code symbols in the index.

        Args:
            pattern: Name pattern to match
            kind: Symbol kind (function, class, method, etc.)
            limit: Maximum results
        """
        args = {"limit": limit}
        if pattern:
            args["pattern"] = pattern
        if kind:
            args["kind"] = kind

        result = daemon_call("search_symbols", args)
        symbols = result.get("symbols", [])

        self._track("symbols", args, symbols)
        return symbols

    def read_symbol(self, name: str, file_path: str = None) -> dict:
        """
        Read a symbol's source code.

        Args:
            name: Symbol name
            file_path: Optional file path to disambiguate
        """
        args = {"name": name}
        if file_path:
            args["file_path"] = file_path

        result = daemon_call("read_symbol", args)
        self._track("read_symbol", args, result)
        return result

    def stats(self) -> dict:
        """Get soul statistics."""
        result = daemon_call("health_check", {})
        self._track("stats", {}, result)
        return result

    def trajectory(self) -> list[dict]:
        """Get the exploration trajectory (all calls made in this session)."""
        return self._trajectory.copy()

    def clear_trajectory(self):
        """Clear the exploration trajectory."""
        self._trajectory = []

    def __repr__(self):
        return f"SoulAPI(calls={len(self._trajectory)})"


# Create singleton for REPL use
soul = SoulAPI()
