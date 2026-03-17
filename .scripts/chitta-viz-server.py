#!/usr/bin/env python3
"""
chitta mind-viz server — real-time memory graph visualization backend.
"""

import argparse
import glob
import json
import os
import re
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse

CHITTA = os.path.expanduser("~/.claude/bin/chitta")
VERSION = "4.0.14"
MIND_DIR = os.path.expanduser("~/.claude/mind")
CLAUDE_PROJECTS = os.path.expanduser("~/.claude/projects")

# Module-level caches
_graph_cache = {}          # id -> full node dict
_graph_cache_lock = threading.Lock()
_sse_lock = threading.Lock()
_sse_clients = []
_last_stats = {"total": 0, "recent_recalls": 0, "triplets": 0}
_instance_colors = [
    "#89b4fa", "#a6e3a1", "#f9e2af", "#cba6f7",
    "#f38ba8", "#94e2d5", "#fab387", "#89dceb",
]


def find_socket():
    matches = glob.glob("/run/user/*/chitta/*.sock")
    return matches[0] if matches else None


def run_chitta(*args):
    sock = find_socket()
    cmd = [CHITTA]
    if sock:
        cmd += ["--socket-path", sock]
    cmd += list(args)
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        if result.returncode != 0:
            return None
        return json.loads(result.stdout)
    except (subprocess.TimeoutExpired, subprocess.SubprocessError, json.JSONDecodeError, ValueError):
        return None


def fetch_graph(limit=200):
    data = run_chitta("list_memories_brief", "--limit", str(limit))
    if not data:
        return {"nodes": [], "edges": []}

    memories = data if isinstance(data, list) else data.get("memories", [])

    nodes = []
    with _graph_cache_lock:
        for m in memories:
            mid = str(m.get("id", ""))
            if not mid:
                continue
            content = m.get("content", m.get("text", ""))
            kind = m.get("memory_type", m.get("kind", "episodic"))
            node = {
                "id": mid,
                "label": content[:80],
                "content": content,
                "kind": kind,
                "strength": float(m.get("strength", m.get("resonance", 0.5))),
                "priority": int(m.get("priority", m.get("priority_tier", 1))),
                "ts_ms": int(m.get("ts_ms", m.get("created_at", 0))),
                "recall_count": int(m.get("recall_count", 0)),
                "tags": m.get("tags", []),
                "realm": m.get("realm", ""),
            }
            nodes.append(node)
            _graph_cache[mid] = node

    node_ids = {n["id"] for n in nodes}
    edges = []
    seen = set()

    for node in nodes[:50]:
        triplets = run_chitta("query_triplets", "--subject", node["id"], "--limit", "20")
        if not triplets:
            continue
        items = triplets if isinstance(triplets, list) else triplets.get("triplets", [])
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


def fetch_memory_detail(memory_id):
    """Full memory detail for inspector panel."""
    with _graph_cache_lock:
        node = _graph_cache.get(memory_id, {}).copy()

    # Fetch associations
    triplets = run_chitta("query_triplets", "--subject", memory_id, "--limit", "30")
    associations = []
    if triplets:
        items = triplets if isinstance(triplets, list) else triplets.get("triplets", [])
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


def fetch_stats():
    data = run_chitta("stats")
    if not data:
        return _last_stats.copy()
    return {
        "total": int(data.get("total_memories", data.get("total", 0))),
        "recent_recalls": int(data.get("recent_recalls", data.get("recalls_last_hour", 0))),
        "triplets": int(data.get("triplets", data.get("total_triplets", 0))),
        "edges": int(data.get("assoc_edges", data.get("edges", 0))),
    }


def fetch_coactivation(top=40):
    data = run_chitta("list_memories_brief", "--limit", str(top), "--sort", "recall_count")
    if not data:
        return {"ids": [], "labels": [], "matrix": []}

    memories = data if isinstance(data, list) else data.get("memories", [])
    memories = memories[:top]

    ids = [str(m.get("id", "")) for m in memories]
    labels = [m.get("content", m.get("text", ""))[:30] for m in memories]
    n = len(ids)

    matrix = [[0.0] * n for _ in range(n)]
    for i in range(n):
        matrix[i][i] = 1.0

    id_index = {mid: i for i, mid in enumerate(ids)}

    for i, mid in enumerate(ids):
        triplets = run_chitta("query_triplets", "--subject", mid, "--limit", "30")
        if not triplets:
            continue
        items = triplets if isinstance(triplets, list) else triplets.get("triplets", [])
        for t in items:
            tgt = str(t.get("object", ""))
            if tgt in id_index:
                j = id_index[tgt]
                w = float(t.get("weight", t.get("strength", 0.3)))
                matrix[i][j] = max(matrix[i][j], w)
                matrix[j][i] = max(matrix[j][i], w)

    return {"ids": ids, "labels": labels, "matrix": matrix}


def fetch_instances():
    """Detect active Claude Code sessions by scanning transcript files."""
    instances = []
    now_ms = int(time.time() * 1000)
    cutoff_ms = now_ms - 30 * 60 * 1000  # 30 minutes

    if os.path.isdir(CLAUDE_PROJECTS):
        for proj_dir in glob.glob(os.path.join(CLAUDE_PROJECTS, "*")):
            for jl_file in glob.glob(os.path.join(proj_dir, "*.jsonl")):
                try:
                    mtime_ms = int(os.path.getmtime(jl_file) * 1000)
                    if mtime_ms < cutoff_ms:
                        continue
                    session_id = os.path.splitext(os.path.basename(jl_file))[0]
                    proj_name = os.path.basename(proj_dir).replace("-", "/").lstrip("/")[:40]
                    age_secs = (now_ms - mtime_ms) // 1000
                    status = "active" if age_secs < 120 else "idle"
                    instances.append({
                        "id": session_id,
                        "short_id": session_id[-8:],
                        "project": proj_name,
                        "last_active_ms": mtime_ms,
                        "age_secs": age_secs,
                        "status": status,
                    })
                except OSError:
                    continue

    instances.sort(key=lambda x: -x["last_active_ms"])
    # Assign stable color indices by short_id hash
    for inst in instances:
        h = sum(ord(c) for c in inst["id"])
        inst["color"] = _instance_colors[h % len(_instance_colors)]

    return {"instances": instances[:10]}


# SSE broadcaster
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
                recent = run_chitta("list_memories_brief", "--limit", "15", "--sort", "recent")
                if recent:
                    memories = recent if isinstance(recent, list) else recent.get("memories", [])
                    ids = [str(m.get("id", "")) for m in memories]
                    # Try to attribute to an instance (last-modified session)
                    instance_id = None
                    if os.path.isdir(CLAUDE_PROJECTS):
                        newest = max(
                            (f for f in glob.glob(os.path.join(CLAUDE_PROJECTS, "*", "*.jsonl"))),
                            key=os.path.getmtime, default=None
                        )
                        if newest:
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

            # Every 30 ticks (~60s), broadcast instance list
            if tick % 30 == 0:
                inst_data = fetch_instances()
                events.append(("instances", inst_data))

            _broadcast(events)

        except Exception:
            pass


def _broadcast(events):
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


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def send_cors_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def send_json(self, data, status=200):
        body = json.dumps(data).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_cors_headers()
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_cors_headers()
        self.end_headers()

    def do_GET(self):
        parsed = urlparse(self.path)
        qs = parse_qs(parsed.query)
        path = parsed.path

        if path == "/health":
            self.send_json({"status": "ok", "version": VERSION})

        elif path == "/graph":
            limit = int(qs.get("limit", ["200"])[0])
            self.send_json(fetch_graph(limit))

        elif path == "/coactivation":
            top = int(qs.get("top", ["40"])[0])
            self.send_json(fetch_coactivation(top))

        elif path == "/instances":
            self.send_json(fetch_instances())

        elif re.match(r"^/memory/(\d+)$", path):
            m = re.match(r"^/memory/(\d+)$", path)
            memory_id = m.group(1)
            detail = fetch_memory_detail(memory_id)
            if not detail:
                self.send_json({"error": "not found"}, 404)
            else:
                self.send_json(detail)

        elif path == "/events":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.send_cors_headers()
            self.end_headers()

            # Send initial data burst
            try:
                stats = fetch_stats()
                self.wfile.write(
                    f"event: stats\ndata: {json.dumps(stats)}\n\n".encode()
                )
                instances = fetch_instances()
                self.wfile.write(
                    f"event: instances\ndata: {json.dumps(instances)}\n\n".encode()
                )
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
            self.send_response(404)
            self.send_cors_headers()
            self.end_headers()


def main():
    parser = argparse.ArgumentParser(description="chitta mind-viz server")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()

    # Pre-warm graph cache in background
    threading.Thread(target=lambda: fetch_graph(300), daemon=True).start()

    broadcaster = threading.Thread(target=sse_broadcaster, daemon=True)
    broadcaster.start()

    server = HTTPServer(("", args.port), Handler)
    print(f"chitta mind-viz running at http://localhost:{args.port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nshutting down")
        server.server_close()


if __name__ == "__main__":
    main()
