#!/usr/bin/env python3
"""
chitta mind-viz server — real-time memory graph visualization backend.
Talks to chittad daemon via Unix socket (JSON-RPC tools/call protocol).
"""

import argparse
import glob
import json
import os
import re
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse

VERSION = "4.0.14"
CLAUDE_PROJECTS = os.path.expanduser("~/.claude/projects")
STATIC_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs", "mind-viz")


# ── Daemon socket ─────────────────────────────────────────────────────────────

def djb2_hash(s: str) -> int:
    h = 5381
    for c in s:
        h = ((h << 5) + h + ord(c)) & 0xFFFFFFFF
    return h


def get_socket_path() -> str:
    mind_path = os.path.join(os.path.expanduser("~"), ".claude", "mind")
    h = djb2_hash(mind_path)
    xdg = os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")
    return os.path.join(xdg, "chitta", f"chitta-{h}.sock")


class ChittaClient:
    def __init__(self):
        self.sock_path = get_socket_path()
        self._sock = None
        self._lock = threading.Lock()

    def _connect(self):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(10)
        s.connect(self.sock_path)
        return s

    def call(self, tool_name: str, arguments: dict = None) -> dict | None:
        req = json.dumps({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {"name": tool_name, "arguments": arguments or {}},
        }) + "\n"

        with self._lock:
            try:
                s = self._connect()
                s.sendall(req.encode())
                buf = b""
                while True:
                    chunk = s.recv(65536)
                    if not chunk:
                        break
                    buf += chunk
                    if b"\n" in buf:
                        break
                s.close()
                data = json.loads(buf.decode().strip())
                result = data.get("result", {})
                # structured field is our primary target
                if "structured" in result:
                    return result["structured"]
                # fall back to parsing text content
                content = result.get("content", [])
                if content and isinstance(content, list):
                    for item in content:
                        if isinstance(item, dict) and item.get("type") == "text":
                            try:
                                return json.loads(item["text"])
                            except (json.JSONDecodeError, KeyError):
                                return {"text": item.get("text", "")}
                return result
            except Exception:
                return None


_client = ChittaClient()

# Module-level caches
_graph_cache: dict[str, dict] = {}
_graph_cache_lock = threading.Lock()
_sse_lock = threading.Lock()
_sse_clients: list = []
_last_stats: dict = {"total": 0, "recent_recalls": 0}
_instance_colors = [
    "#89b4fa", "#a6e3a1", "#f9e2af", "#cba6f7",
    "#f38ba8", "#94e2d5", "#fab387", "#89dceb",
]


# ── Data fetchers ─────────────────────────────────────────────────────────────

def fetch_graph(limit: int = 200) -> dict:
    data = _client.call("list_memories_brief", {"limit": limit})
    if not data:
        return {"nodes": [], "edges": []}

    memories = data if isinstance(data, list) else data.get("memories", [])

    nodes = []
    with _graph_cache_lock:
        for m in memories:
            mid = str(m.get("id", ""))
            if not mid:
                continue
            content = m.get("content", m.get("text", m.get("body", "")))
            kind = m.get("memory_type", m.get("kind", "episodic"))
            node = {
                "id": mid,
                "label": content[:80],
                "content": content,
                "kind": kind,
                "strength": float(m.get("strength", m.get("resonance", 0.5))),
                "priority": int(m.get("priority", m.get("priority_tier", 1))),
                "ts_ms": int(m.get("ts_ms", m.get("created_at", 0))),
                "recall_count": int(m.get("recall_count", m.get("access_count", 0))),
                "tags": m.get("tags", []),
                "realm": m.get("realm", ""),
            }
            nodes.append(node)
            _graph_cache[mid] = node

    node_ids = {n["id"] for n in nodes}
    edges = []
    seen: set = set()

    # Fetch associations for a sample of nodes
    sample = nodes[:40]
    for node in sample:
        result = _client.call("query_triplets_temporal", {
            "subject": node["id"],
            "limit": 15,
        })
        if not result:
            continue
        items = result if isinstance(result, list) else result.get("triplets", [])
        for t in items:
            src = str(t.get("subject", node["id"]))
            tgt = str(t.get("object", ""))
            if not tgt or tgt not in node_ids:
                continue
            key = (min(src, tgt), max(src, tgt))
            if key in seen:
                continue
            seen.add(key)
            edges.append({
                "source": src,
                "target": tgt,
                "weight": float(t.get("weight", t.get("strength", 0.5))),
                "edge_type": t.get("predicate", t.get("relation", "associates")),
            })

    return {"nodes": nodes, "edges": edges}


def fetch_memory_detail(memory_id: str) -> dict:
    with _graph_cache_lock:
        node = _graph_cache.get(memory_id, {}).copy()

    result = _client.call("query_triplets_temporal", {
        "subject": memory_id,
        "limit": 30,
    })
    associations = []
    if result:
        items = result if isinstance(result, list) else result.get("triplets", [])
        for t in items:
            tgt = str(t.get("object", ""))
            if not tgt:
                continue
            with _graph_cache_lock:
                related = _graph_cache.get(tgt, {})
            associations.append({
                "id": tgt,
                "label": related.get("label", tgt[:40]),
                "kind": related.get("kind", ""),
                "relation": t.get("predicate", t.get("relation", "associates")),
                "weight": float(t.get("weight", t.get("strength", 0.5))),
            })
        associations.sort(key=lambda x: -x["weight"])

    node["associations"] = associations[:20]
    return node


def fetch_stats() -> dict:
    result = _client.call("health_check", {})
    if not result:
        return _last_stats.copy()

    # health_check returns various fields; normalise
    total = int(result.get("total_memories", result.get("memories", result.get("total", 0))))
    recalls = int(result.get("recent_recalls", result.get("recalls_last_hour", 0)))
    triplets = int(result.get("triplets", result.get("total_triplets", 0)))
    edges = int(result.get("assoc_edges", result.get("edges", 0)))
    return {"total": total, "recent_recalls": recalls, "triplets": triplets, "edges": edges}


def fetch_coactivation(top: int = 40) -> dict:
    data = _client.call("list_memories_brief", {"limit": top})
    if not data:
        return {"ids": [], "labels": [], "matrix": []}

    memories = data if isinstance(data, list) else data.get("memories", [])
    memories = sorted(memories, key=lambda m: -int(m.get("recall_count", m.get("access_count", 0))))[:top]

    ids = [str(m.get("id", "")) for m in memories]
    labels = [m.get("content", m.get("text", ""))[:30] for m in memories]
    n = len(ids)
    matrix = [[0.0] * n for _ in range(n)]
    for i in range(n):
        matrix[i][i] = 1.0

    id_index = {mid: i for i, mid in enumerate(ids)}

    for i, mid in enumerate(ids):
        result = _client.call("query_triplets_temporal", {"subject": mid, "limit": 30})
        if not result:
            continue
        items = result if isinstance(result, list) else result.get("triplets", [])
        for t in items:
            tgt = str(t.get("object", ""))
            if tgt in id_index:
                j = id_index[tgt]
                w = float(t.get("weight", t.get("strength", 0.3)))
                matrix[i][j] = max(matrix[i][j], w)
                matrix[j][i] = max(matrix[j][i], w)

    return {"ids": ids, "labels": labels, "matrix": matrix}


def fetch_instances() -> dict:
    instances = []
    now_ms = int(time.time() * 1000)
    cutoff_ms = now_ms - 30 * 60 * 1000

    if os.path.isdir(CLAUDE_PROJECTS):
        for jl_file in glob.glob(os.path.join(CLAUDE_PROJECTS, "*", "*.jsonl")):
            try:
                mtime_ms = int(os.path.getmtime(jl_file) * 1000)
                if mtime_ms < cutoff_ms:
                    continue
                session_id = os.path.splitext(os.path.basename(jl_file))[0]
                proj_name = os.path.basename(os.path.dirname(jl_file)).replace("-", "/").lstrip("/")[:40]
                age_secs = (now_ms - mtime_ms) // 1000
                instances.append({
                    "id": session_id,
                    "short_id": session_id[-8:],
                    "project": proj_name,
                    "last_active_ms": mtime_ms,
                    "age_secs": int(age_secs),
                    "status": "active" if age_secs < 120 else "idle",
                })
            except OSError:
                continue

    instances.sort(key=lambda x: -x["last_active_ms"])
    for inst in instances:
        h = sum(ord(c) for c in inst["id"])
        inst["color"] = _instance_colors[h % len(_instance_colors)]

    return {"instances": instances[:10]}


# ── SSE broadcaster ───────────────────────────────────────────────────────────

def sse_broadcaster():
    global _last_stats
    tick = 0
    while True:
        time.sleep(2)
        tick += 1
        try:
            stats = fetch_stats()
            events = []

            prev_recalls = _last_stats.get("recent_recalls", 0)
            curr_recalls = stats.get("recent_recalls", 0)

            if curr_recalls != prev_recalls:
                recent = _client.call("list_memories_brief", {"limit": 15})
                if recent:
                    mems = recent if isinstance(recent, list) else recent.get("memories", [])
                    ids = [str(m.get("id", "")) for m in mems]
                    instance_id = None
                    if os.path.isdir(CLAUDE_PROJECTS):
                        all_jl = glob.glob(os.path.join(CLAUDE_PROJECTS, "*", "*.jsonl"))
                        if all_jl:
                            newest = max(all_jl, key=os.path.getmtime)
                            instance_id = os.path.splitext(os.path.basename(newest))[0][-8:]
                    events.append(("recall", {
                        "memory_ids": ids,
                        "passes": 1,
                        "ts_ms": int(time.time() * 1000),
                        "instance_id": instance_id,
                    }))

            events.append(("stats", {
                "total": stats["total"],
                "recent_recalls": stats["recent_recalls"],
                "triplets": stats.get("triplets", 0),
                "edges": stats.get("edges", 0),
            }))
            _last_stats = stats

            if tick % 30 == 0:
                events.append(("instances", fetch_instances()))

            _broadcast(events)
        except Exception:
            pass


def _broadcast(events: list):
    with _sse_lock:
        dead = []
        for client in _sse_clients:
            try:
                for ev_type, ev_data in events:
                    payload = json.dumps(ev_data)
                    client.wfile.write(f"event: {ev_type}\ndata: {payload}\n\n".encode())
                client.wfile.flush()
            except (BrokenPipeError, OSError):
                dead.append(client)
        for d in dead:
            _sse_clients.remove(d)


# ── HTTP handler ──────────────────────────────────────────────────────────────

class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def json(self, data, status=200):
        body = json.dumps(data).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.cors()
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(204)
        self.cors()
        self.end_headers()

    def do_GET(self):
        parsed = urlparse(self.path)
        qs = parse_qs(parsed.query)
        path = parsed.path

        if path in ("/", "/index.html"):
            html_path = os.path.join(STATIC_DIR, "index.html")
            try:
                with open(html_path, "rb") as f:
                    body = f.read()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.cors()
                self.end_headers()
                self.wfile.write(body)
            except OSError:
                self.send_response(404); self.end_headers()

        elif path == "/health":
            ok = _client.call("health_check", {}) is not None
            self.json({"status": "ok" if ok else "daemon_unreachable", "version": VERSION})

        elif path == "/graph":
            limit = int(qs.get("limit", ["200"])[0])
            self.json(fetch_graph(limit))

        elif path == "/coactivation":
            top = int(qs.get("top", ["40"])[0])
            self.json(fetch_coactivation(top))

        elif path == "/instances":
            self.json(fetch_instances())

        elif re.match(r"^/memory/(\d+)$", path):
            memory_id = re.match(r"^/memory/(\d+)$", path).group(1)
            detail = fetch_memory_detail(memory_id)
            self.json(detail if detail else {"error": "not found"}, 200 if detail else 404)

        elif path == "/events":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.cors()
            self.end_headers()
            try:
                stats = fetch_stats()
                self.wfile.write(f"event: stats\ndata: {json.dumps(stats)}\n\n".encode())
                inst = fetch_instances()
                self.wfile.write(f"event: instances\ndata: {json.dumps(inst)}\n\n".encode())
                self.wfile.flush()
            except OSError:
                return

            with _sse_lock:
                _sse_clients.append(self)
            try:
                while True:
                    time.sleep(30)
            except (BrokenPipeError, OSError):
                pass
            finally:
                with _sse_lock:
                    if self in _sse_clients:
                        _sse_clients.remove(self)

        else:
            self.send_response(404); self.cors(); self.end_headers()


def main():
    parser = argparse.ArgumentParser(description="chitta mind-viz server")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()

    # Pre-warm graph cache
    threading.Thread(target=lambda: fetch_graph(300), daemon=True).start()

    threading.Thread(target=sse_broadcaster, daemon=True).start()

    server = HTTPServer(("", args.port), Handler)
    print(f"chitta mind-viz running at http://localhost:{args.port}")
    print(f"  SSH tunnel: ssh -L {args.port}:localhost:{args.port} <user>@<host>")
    print(f"  Then open:  http://localhost:{args.port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nshutting down")
        server.server_close()


if __name__ == "__main__":
    main()
