#!/usr/bin/env python3
"""Deterministic, multimodel-safe transcript/thread resume selection."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).parent))
from session_registry import inventory as registry_inventory  # noqa: E402
from task_ledger import (  # noqa: E402
    lease_claim,
    session_bind,
    thread_create,
    thread_list,
)


def _canonical(path: str) -> str:
    if not path:
        return ""
    try:
        return str(Path(path).resolve())
    except OSError:
        return os.path.abspath(path)


def _git_root(path: str) -> str:
    if not path:
        return ""
    try:
        proc = subprocess.run(
            ["git", "-C", path, "rev-parse", "--show-toplevel"],
            capture_output=True, text=True, timeout=2,
        )
        return _canonical(proc.stdout.strip()) if proc.returncode == 0 else ""
    except (OSError, subprocess.SubprocessError):
        return ""


def _pid_alive(pid: Any) -> bool:
    # os.kill(pid, 0) alone is unsafe here: after a crash the OS can reuse the
    # PID for an unrelated process, making a dead session look live and locking
    # its transcript out of /resume forever. Require the process to look like
    # an agent frontend before trusting the PID.
    try:
        pid = int(pid)
        os.kill(pid, 0)
    except (OSError, TypeError, ValueError):
        return False
    try:
        cmdline = Path(f"/proc/{pid}/cmdline").read_bytes().replace(b"\0", b" ")
        return any(t in cmdline for t in (b"claude", b"codex", b"node"))
    except OSError:
        return True  # /proc unavailable: keep the old permissive behavior


def _entry_live(entry: dict[str, Any], now_ms: int,
                ttl_ms: int = 900_000) -> bool:
    if "is_live" in entry:
        return bool(entry["is_live"])
    if entry.get("status") not in (None, "", "active"):
        return False
    heartbeat = int(entry.get("last_heartbeat_ms") or entry.get("ts_ms") or 0)
    fresh = heartbeat > 1_000_000_000_000 and now_ms - heartbeat <= ttl_ms
    return fresh or _pid_alive(entry.get("pid"))


def _client_from_path(path: str) -> str:
    if "/.codex/sessions/" in path:
        return "codex"
    if "/.claude/projects/" in path:
        return "claude"
    return ""


def _mtime(path: str) -> float:
    try:
        return Path(path).stat().st_mtime
    except OSError:
        return 0.0


def _session_id_from_codex(path: Path, first: dict[str, Any]) -> str:
    payload = first.get("payload", {}) if isinstance(first, dict) else {}
    if first.get("type") == "session_meta" and payload.get("id"):
        return str(payload["id"])
    match = re.search(
        r"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})$",
        path.stem,
        re.IGNORECASE,
    )
    return match.group(1) if match else path.stem


def discover_project_transcripts(project_dir: str,
                                 limit: int = 100) -> list[dict[str, Any]]:
    """Recover historical Claude and Codex files missing old registrations.

    Discovery is project-exact and feeds the deterministic selector; it is
    never used as a global "newest transcript" fallback.
    """
    project = _canonical(project_dir)
    if not project:
        return []
    project_git = _git_root(project)
    home = Path.home()
    found: list[dict[str, Any]] = []

    encoded = "-" + project.lstrip("/").replace("/", "-")
    claude_dir = home / ".claude" / "projects" / encoded
    if claude_dir.is_dir():
        for path in claude_dir.glob("*.jsonl"):
            found.append({
                "session_id": path.stem,
                "client": "claude",
                "project_dir": project,
                "transcript_path": str(path.resolve()),
                "discovered": True,
            })

    codex_root = home / ".codex" / "sessions"
    if codex_root.is_dir():
        git_cache: dict[str, str] = {}
        paths = sorted(
            codex_root.rglob("*.jsonl"),
            key=lambda item: _mtime(str(item)),
            reverse=True,
        )[:max(limit * 4, 200)]
        for path in paths:
            try:
                with path.open(encoding="utf-8") as handle:
                    first = json.loads(handle.readline())
                payload = first.get("payload", {})
                cwd = _canonical(str(payload.get("cwd") or ""))
            except (OSError, json.JSONDecodeError, AttributeError):
                continue
            if cwd and cwd not in git_cache:
                git_cache[cwd] = _git_root(cwd)
            cwd_git = git_cache.get(cwd, "")
            if cwd != project and not (project_git and cwd_git == project_git):
                continue
            found.append({
                "session_id": _session_id_from_codex(path, first),
                "client": "codex",
                "model": str(payload.get("model") or ""),
                "project_dir": cwd,
                "transcript_path": str(path.resolve()),
                "discovered": True,
            })

    found.sort(key=lambda row: _mtime(str(row["transcript_path"])), reverse=True)
    return found[:limit]


def augment_inventory(data: dict[str, Any], project_dir: str) -> dict[str, Any]:
    merged = dict(data)
    transcripts = list(data.get("transcripts", []))
    known = {
        str(row.get("session_id") or row.get("transcript_id") or "")
        for row in transcripts
    }
    for row in discover_project_transcripts(project_dir):
        if row["session_id"] not in known:
            transcripts.append(row)
    merged["transcripts"] = transcripts
    return merged


def _ancestor_pids() -> set[int]:
    pids: set[int] = set()
    pid = os.getppid()
    for _ in range(32):
        if pid <= 1 or pid in pids:
            break
        pids.add(pid)
        try:
            stat = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
            pid = int(stat[stat.rfind(")") + 2:].split()[1])
        except (OSError, ValueError, IndexError):
            break
    return pids


def infer_current_session_id(data: dict[str, Any], project_dir: str,
                             client: str = "") -> str:
    """Identify the invoking frontend by registered ancestor PID if possible."""
    now_ms = int(time.time() * 1000)
    live = [row for row in data.get("sessions", []) if _entry_live(row, now_ms)]
    ancestors = _ancestor_pids()
    pid_matches = [
        row for row in live
        if int(row.get("pid") or -1) in ancestors
        and (not client or (row.get("client") or row.get("kind")) == client)
    ]
    if len(pid_matches) == 1:
        return str(pid_matches[0].get("session_id") or "")

    project = _canonical(project_dir)
    project_matches = [
        row for row in live
        if _canonical(str(row.get("project_dir") or "")) == project
        and (not client or (row.get("client") or row.get("kind")) == client)
    ]
    return str(project_matches[0].get("session_id") or "") \
        if len(project_matches) == 1 else ""


def _resolve_thread(value: str) -> str:
    if not value:
        return ""
    matches = [
        row for row in thread_list(status="active", limit=100)
        if str(row.get("thread_id", "")).startswith(value)
        or value.lower() in str(row.get("title", "")).lower()
    ]
    return str(matches[0]["thread_id"]) if len(matches) == 1 else value


def select_resume(data: dict[str, Any], project_dir: str,
                  current_session_id: str = "", requested_session_id: str = "",
                  requested_thread: str = "", client: str = "",
                  min_margin: int = 15) -> dict[str, Any]:
    """Return one selected inactive transcript, or an explicit ambiguity/lock."""
    now = time.time()
    now_ms = int(now * 1000)
    project = _canonical(project_dir)
    project_git = _git_root(project)

    daemon_sessions = {
        str(row.get("session_id")): dict(row)
        for row in data.get("sessions", []) if row.get("session_id")
    }
    ledger_sessions = {
        str(row.get("session_id")): dict(row)
        for row in data.get("thread_sessions", []) if row.get("session_id")
    }
    leases = {
        str(row.get("thread_id")): dict(row)
        for row in data.get("thread_leases", [])
        if row.get("thread_id") and float(row.get("expires_at") or 0) > now
    }
    registry_available = data.get("session_registry_available", True) is not False

    records: dict[str, dict[str, Any]] = {}
    for transcript in data.get("transcripts", []):
        sid = str(transcript.get("session_id") or transcript.get("transcript_id") or "")
        if sid:
            records.setdefault(sid, {}).update(transcript)
    for sid, row in ledger_sessions.items():
        records.setdefault(sid, {}).update({k: v for k, v in row.items() if v not in (None, "")})
    for sid, row in daemon_sessions.items():
        records.setdefault(sid, {}).update({k: v for k, v in row.items() if v not in (None, "")})

    live_sessions: list[dict[str, Any]] = []
    for sid, row in daemon_sessions.items():
        if not _entry_live(row, now_ms):
            continue
        ledger = ledger_sessions.get(sid, {})
        live_sessions.append({
            "session_id": sid,
            "client": row.get("client") or row.get("kind") or ledger.get("client") or "unknown",
            "model": row.get("model", ""),
            "project_dir": row.get("project_dir") or ledger.get("project_dir") or "",
            "thread_id": row.get("thread_id") or ledger.get("thread_id") or "",
            "transcript_path": row.get("transcript_path") or ledger.get("transcript_path") or "",
            "pid": row.get("pid"),
            "last_heartbeat_ms": row.get("last_heartbeat_ms") or row.get("ts_ms"),
        })

    requested_thread_id = _resolve_thread(requested_thread)
    candidates: list[dict[str, Any]] = []
    for sid, row in records.items():
        if sid == current_session_id:
            continue
        path = str(row.get("transcript_path") or row.get("path") or "")
        row_project = _canonical(str(row.get("project_dir") or ""))
        row_git = _git_root(row_project) if row_project else ""
        thread_id = str(row.get("thread_id") or "")
        is_live = sid in daemon_sessions and _entry_live(daemon_sessions[sid], now_ms)
        lease = leases.get(thread_id, {}) if thread_id else {}
        lease_owner = str(lease.get("session_id") or "")
        # An unexpired lease is authoritative even when it belongs to the
        # candidate's original session. Clean SessionEnd releases it; crashes
        # age out by TTL. This prevents takeover during a transient registry gap.
        locked = is_live or (lease_owner not in ("", current_session_id))

        score = 0
        reasons: list[str] = []
        if requested_session_id:
            if sid != requested_session_id:
                continue
            score += 200
            reasons.append("explicit-session")
        if requested_thread_id:
            if thread_id != requested_thread_id:
                continue
            score += 120
            reasons.append("explicit-thread")
        if project and row_project == project:
            score += 60
            reasons.append("same-directory")
        elif project_git and row_git == project_git:
            score += 50
            reasons.append("same-git-root")
        elif not requested_session_id and not requested_thread_id:
            continue
        if str(row.get("status") or "") in ("ended", "interrupted"):
            score += 10
            reasons.append(str(row.get("status")))
        age_hours = max(0.0, (now - (_mtime(path) or float(row.get("last_active_at") or 0))) / 3600)
        score += max(0, 20 - min(20, int(age_hours)))
        candidate = {
            "session_id": sid,
            "client": row.get("client") or row.get("kind") or _client_from_path(path) or "unknown",
            "model": row.get("model", ""),
            "project_dir": row_project,
            "transcript_path": path,
            "thread_id": thread_id,
            "status": row.get("status", ""),
            "live": is_live,
            "locked": locked,
            "lock_owner_session_id": sid if is_live else lease_owner,
            "score": score,
            "reasons": reasons,
            "mtime": _mtime(path),
        }
        candidates.append(candidate)

    candidates.sort(key=lambda row: (row["score"], row["mtime"]), reverse=True)
    unlocked = [row for row in candidates if not row["locked"]]
    selected: dict[str, Any] | None = None
    status = "none"
    if requested_session_id and candidates and candidates[0]["locked"]:
        status = "locked"
    elif unlocked:
        if len(unlocked) == 1 or unlocked[0]["score"] - unlocked[1]["score"] >= min_margin:
            selected = unlocked[0]
            status = "selected"
        else:
            status = "ambiguous"
    elif candidates:
        status = "locked"

    # A timeout is not evidence that no session is running. Keep the candidate
    # details for diagnosis, but fail closed until ownership can be checked.
    if not registry_available:
        selected = None
        status = "registry_unavailable"

    return {
        "status": status,
        "project_dir": project,
        "current_session_id": current_session_id,
        "session_registry_available": registry_available,
        "selected": selected,
        "candidates": candidates[:10],
        "live_sessions": sorted(
            live_sessions,
            key=lambda row: int(row.get("last_heartbeat_ms") or 0),
            reverse=True,
        ),
        "multimodel": sorted({str(row.get("client") or "unknown") for row in live_sessions}),
    }


def claim_selected(result: dict[str, Any], current_session_id: str,
                   client: str, project_dir: str, force: bool = False) -> dict[str, Any]:
    selected = result.get("selected") or {}
    thread_id = str(selected.get("thread_id") or "")
    if not current_session_id:
        return {"claimed": False, "reason": "missing_current_session_id"}
    if not thread_id:
        selected_session_id = str(selected.get("session_id") or "")
        thread_id = thread_create(
            title=f"resume {selected_session_id[:8] or 'session'}",
            realm=f"project:{Path(project_dir).name}" if project_dir else "",
        )
        selected["thread_id"] = thread_id
        if selected_session_id:
            prior_status = str(selected.get("status") or "ended")
            if prior_status not in ("ended", "interrupted", "completed"):
                prior_status = "ended"
            session_bind(
                selected_session_id, thread_id,
                client=str(selected.get("client") or ""),
                project_dir=str(selected.get("project_dir") or project_dir),
                transcript_path=str(selected.get("transcript_path") or ""),
                status=prior_status,
            )
    # Create/refresh the current session row for the lease FK, but let the
    # atomic claim update its thread only after ownership succeeds.
    session_bind(current_session_id, None, client=client,
                 project_dir=_canonical(project_dir))
    return lease_claim(thread_id, current_session_id, force=force)


def _cli() -> None:
    parser = argparse.ArgumentParser(prog="resume_selector")
    parser.add_argument("--project-dir", default=os.getcwd(), dest="project_dir")
    current_default = next((os.environ.get(name, "") for name in (
        "CLAUDE_SESSION_ID", "CODEX_SESSION_ID", "CODEX_THREAD_ID",
        "CODEX_CONVERSATION_ID",
    ) if os.environ.get(name)), "")
    parser.add_argument("--current-session-id", default=current_default,
                        dest="current_session_id")
    parser.add_argument("--session-id", default="", dest="requested_session_id")
    parser.add_argument("--thread", default="", dest="requested_thread")
    parser.add_argument("--client", choices=("", "claude", "codex"), default="")
    parser.add_argument("--min-margin", type=int, default=15, dest="min_margin")
    parser.add_argument("--claim", action="store_true")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--inventory-json", dest="inventory_json")
    args = parser.parse_args()

    if args.inventory_json:
        data = json.loads(Path(args.inventory_json).read_text(encoding="utf-8"))
    else:
        data = augment_inventory(registry_inventory(), args.project_dir)
    if not args.current_session_id:
        args.current_session_id = infer_current_session_id(
            data, args.project_dir, args.client,
        )
    result = select_resume(
        data, args.project_dir, args.current_session_id,
        args.requested_session_id, args.requested_thread,
        args.client, args.min_margin,
    )
    if args.claim:
        result["claim"] = claim_selected(
            result, args.current_session_id, args.client,
            args.project_dir, args.force,
        )
    print(json.dumps(result, default=str))


if __name__ == "__main__":
    _cli()
