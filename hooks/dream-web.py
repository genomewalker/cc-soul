#!/usr/bin/env python3
"""dream-web.py — fetch gap memories and kick off a dream room per gap.

For each unresolved gap:
  1. Recall via chitta CLI (--exclude-tags compliance:auto is not yet a CLI
     flag; we post-filter by content prefix instead, mirroring the C++ fix).
  2. If the bridge MCP HTTP server is reachable: create a dream room with a
     local/gemma participant and run 1 round.
  3. Else: store a synthesized reflection note via chitta remember.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import uuid
from pathlib import Path


# ---------------------------------------------------------------------------
# Bridge discovery
# ---------------------------------------------------------------------------

def _read_bridge_port() -> tuple[int, str] | tuple[None, None]:
    ports_file = Path.home() / ".chitta-bridge" / "http.ports"
    if not ports_file.exists():
        return None, None
    data = {}
    for line in ports_file.read_text().splitlines():
        if "=" in line:
            k, _, v = line.partition("=")
            data[k.strip()] = v.strip()
    port = data.get("mcp")
    token = data.get("token")
    if not port or not token:
        return None, None
    return int(port), token


def _bridge_call(port: int, token: str, tool: str, arguments: dict) -> dict:
    """Send one MCP tools/call to the bridge StreamableHTTP endpoint."""
    import urllib.request

    payload = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {"name": tool, "arguments": arguments},
    }
    body = json.dumps(payload).encode()
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/mcp",
        data=body,
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {token}",
            "Accept": "application/json, text/event-stream",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            raw = resp.read().decode()
        # StreamableHTTP may return SSE lines or plain JSON
        # Extract the last JSON object from the response
        for line in reversed(raw.splitlines()):
            line = line.strip()
            if line.startswith("data:"):
                line = line[5:].strip()
            if line.startswith("{"):
                return json.loads(line)
        return {"raw": raw}
    except Exception as exc:
        return {"error": str(exc)}


# ---------------------------------------------------------------------------
# Gap recall
# ---------------------------------------------------------------------------

_COMPLIANCE_PREFIX = "[compliance:auto"


def _is_compliance(content: str) -> bool:
    return content.startswith(_COMPLIANCE_PREFIX)


def _clean_gap(content: str) -> str:
    if content.startswith("[gap] "):
        content = content[6:]
    return content[:200].strip()


def fetch_gaps(limit: int = 5) -> list[str]:
    """Return up to *limit* gap/question topics, excluding compliance memories."""
    chitta = os.environ.get("CHITTA_BIN", "chitta")
    cmd = [
        chitta, "recall",
        "--query", "gap curiosity unresolved open question",
        "--realm", "project:cc-soul",
        "--limit", str(limit * 3),  # over-fetch so we can filter
        "--json",
    ]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    if result.returncode != 0:
        print(f"[dream-web] recall failed: {result.stderr.strip()}", file=sys.stderr)
        return []

    try:
        hits = json.loads(result.stdout)
    except json.JSONDecodeError:
        # Fall back: parse line-by-line if non-JSON output
        lines = [l.strip() for l in result.stdout.splitlines() if l.strip()]
        return [l for l in lines if not _is_compliance(l)][:limit]

    gaps = []
    for hit in hits:
        content = hit.get("content", "") if isinstance(hit, dict) else str(hit)
        if not content or _is_compliance(content):
            continue
        gaps.append(_clean_gap(content))
        if len(gaps) >= limit:
            break
    return gaps


# ---------------------------------------------------------------------------
# Bridge dream room
# ---------------------------------------------------------------------------

def _room_id(topic: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", topic[:40].lower()).strip("-")
    return f"dream-{slug}-{uuid.uuid4().hex[:6]}"


def process_gap_via_bridge(port: int, token: str, topic: str) -> str:
    room_id = _room_id(topic)
    participants = json.dumps([
        {
            "name": "Dreamer",
            "backend": "local",
            "model": "gemma3:latest",
            "soul": {
                "system_prompt": (
                    "You are a reflective dreamer. Given an unresolved gap or question, "
                    "synthesize a concise insight, hypothesis, or next investigative step. "
                    "Be concrete and epistemic — note confidence, alternatives, and what "
                    "evidence would resolve the gap."
                ),
                "realm": "project:cc-soul",
            },
        }
    ])

    print(f"  [room_create] {room_id} — {topic[:60]}")
    create_result = _bridge_call(port, token, "room_create", {
        "room_id": room_id,
        "topic": topic,
        "participants": participants,
    })
    if "error" in create_result:
        return f"room_create error: {create_result['error']}"

    print(f"  [room_run]    {room_id}")
    run_result = _bridge_call(port, token, "room_run", {
        "room_id": room_id,
        "rounds": 1,
    })
    if "error" in run_result:
        return f"room_run error: {run_result['error']}"

    # Extract text from MCP result envelope
    try:
        content = run_result.get("result", {}).get("content", [])
        if isinstance(content, list) and content:
            return content[0].get("text", str(run_result))
    except Exception:
        pass
    return str(run_result)


# ---------------------------------------------------------------------------
# Fallback: store a reflection note directly
# ---------------------------------------------------------------------------

def process_gap_via_remember(topic: str) -> str:
    chitta = os.environ.get("CHITTA_BIN", "chitta")
    note = (
        f"[gap-reflection] Auto-dream reflection on: {topic}. "
        "This gap was surfaced by dream-web.py but the bridge was unavailable. "
        "Marked for follow-up in the next bridge-connected session."
    )
    cmd = [
        chitta, "remember",
        "--content", note,
        "--kind", "dream",
        "--realm", "project:cc-soul",
        "--tags", "dream,gap-reflection",
        "--confidence", "0.3",
    ]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
    if result.returncode == 0:
        return f"stored reflection note (bridge unavailable)"
    return f"remember failed: {result.stderr.strip()}"


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    print("[dream-web] fetching gap memories …")
    gaps = fetch_gaps(limit=5)
    if not gaps:
        print("[dream-web] no gaps found — nothing to dream about")
        return

    print(f"[dream-web] {len(gaps)} gap(s) found")
    port, token = _read_bridge_port()
    bridge_ok = port is not None
    if bridge_ok:
        print(f"[dream-web] bridge at port {port} — using room mode")
    else:
        print("[dream-web] bridge unavailable — using chitta remember fallback")

    for i, topic in enumerate(gaps, 1):
        print(f"\n[{i}/{len(gaps)}] {topic[:80]}")
        if bridge_ok:
            outcome = process_gap_via_bridge(port, token, topic)
        else:
            outcome = process_gap_via_remember(topic)
        # Truncate long outcomes for terminal readability
        if len(outcome) > 300:
            outcome = outcome[:300] + " …"
        print(f"  -> {outcome}")

    print("\n[dream-web] done")


if __name__ == "__main__":
    main()
