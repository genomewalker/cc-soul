#!/usr/bin/env python3
"""
Resume capsule builder: ~800-token context packet for /resume <thread>.
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from task_ledger import artifact_list, inbox_list, thread_get

CHITTA_BIN = os.environ.get("CHITTA_BIN", str(Path.home() / ".claude/bin/chitta"))


def _chitta(args: list[str]) -> object:
    try:
        r = subprocess.run([CHITTA_BIN, *args], capture_output=True, text=True, timeout=10)
        if r.returncode == 0 and r.stdout.strip():
            return json.loads(r.stdout)
    except (OSError, subprocess.SubprocessError, json.JSONDecodeError):
        # Daemon down or non-JSON reply. None is the documented "no data".
        pass
    return None


def _tasks_for_thread(thread_id: str) -> list[dict]:
    raw = _chitta(["query_tasks", "--json"])
    tasks: list[dict] = (
        raw.get("tasks", []) if isinstance(raw, dict) else raw if isinstance(raw, list) else []
    )
    result = []
    for t in tasks:
        try:
            payload = json.loads(t.get("payload", "{}") or "{}")
            if payload.get("thread_id") == thread_id:
                result.append(
                    {
                        "task_id": t.get("id") or t.get("task_id", ""),
                        "goal": payload.get("goal", ""),
                        "status": t.get("status", ""),
                        "cmd": payload.get("cmd", ""),
                        "cwd": payload.get("cwd", ""),
                        "git_branch": payload.get("git_branch", ""),
                        "conda_env": payload.get("conda_env", ""),
                        "blockers": payload.get("blockers", []),
                        "work_items": payload.get("work_items", []),
                        "completion_digest": payload.get("completion_digest", ""),
                        "job_id": payload.get("job_id", ""),
                        "iterations": payload.get("iterations", 0),
                        "params": payload.get("params", {}),
                        "inputs": payload.get("inputs", []),
                        "outputs": payload.get("outputs", []),
                    }
                )
        except (json.JSONDecodeError, AttributeError, TypeError):
            # Task row with a malformed payload: omit it from the capsule
            # rather than failing the whole resume.
            pass
    return result


def build(thread_id: str) -> dict:
    thread = thread_get(thread_id)
    if not thread:
        return {"error": f"thread not found: {thread_id}"}

    all_tasks = _tasks_for_thread(thread_id)
    active = [t for t in all_tasks if t["status"] in ("active", "running", "blocked")]
    completed = [t for t in all_tasks if t["status"] == "completed"][-3:]
    failed = [t for t in all_tasks if t["status"] == "failed"]

    artifacts = artifact_list(thread_id=thread_id)[-10:]
    inbox_items = [
        i
        for i in inbox_list(target_realm=thread.get("realm", ""), state="pending")
        if i.get("thread_id") == thread_id
    ]

    return {
        "thread_id": thread_id,
        "title": thread.get("title", ""),
        "realm": thread.get("realm", ""),
        "status": thread.get("status", ""),
        "last_active": thread.get("last_active_at"),
        "active_tasks": active,
        "completed": completed,
        "failed": failed,
        "artifacts": [
            {"path": a["path"], "kind": a["kind"], "size": a.get("size")} for a in artifacts
        ],
        "inbox": [{"event_type": i["event_type"], "digest": i["digest"]} for i in inbox_items],
        "summary": _render(thread, active, completed, failed, artifacts, inbox_items),
    }


def _render(
    thread: dict, active: list, completed: list, failed: list, artifacts: list, inbox: list
) -> str:
    lines: list[str] = []
    lines.append(f"Thread: {thread.get('title', thread.get('thread_id', ''))}")
    lines.append(f"Realm:  {thread.get('realm', '')}")
    lines.append("")

    if inbox:
        lines.append("━━━ inbox ━━━")
        for item in inbox[:4]:
            icon = "✓" if item["event_type"] == "completed" else "✗"
            lines.append(f"{icon} {item['digest'][:120]}")
        lines.append("")

    if active:
        lines.append("━━━ active ━━━")
        for t in active:
            lines.append(f"⟳  {t['goal'] or t['task_id']}")
            if t.get("job_id"):
                lines.append(f"   job: {t['job_id']}")
            if t.get("cmd"):
                lines.append(f"   cmd: {t['cmd'][:90]}")
            if t.get("git_branch"):
                lines.append(f"   git: {t['git_branch']}")
            if t.get("conda_env"):
                lines.append(f"   env: {t['conda_env']}")
            for b in t.get("blockers", [])[:2]:
                lines.append(f"   ⚠  {b}")
            if t.get("inputs"):
                lines.append(f"   inputs:  {', '.join(t['inputs'][:3])}")
            if t.get("outputs"):
                lines.append(f"   outputs: {', '.join(t['outputs'][:3])}")
        lines.append("")

    if completed:
        lines.append("━━━ completed ━━━")
        for t in completed:
            lines.append(f"✓  {t['goal'] or t['task_id']}")
            if t.get("completion_digest"):
                lines.append(f"   {t['completion_digest'][:100]}")
        lines.append("")

    if failed:
        lines.append("━━━ failed ━━━")
        for t in failed:
            lines.append(f"✗  {t['goal'] or t['task_id']}")
        lines.append("")

    if artifacts:
        lines.append("━━━ artifacts ━━━")
        for a in artifacts[-6:]:
            kb = f" ({a.get('size', 0) // 1024}KB)" if a.get("size") else ""
            lines.append(f"   {a['path']}{kb} [{a.get('kind', 'file')}]")
        lines.append("")

    if active and active[0].get("work_items"):
        lines.append("━━━ next ━━━")
        for item in active[0]["work_items"][:4]:
            lines.append(f"   → {item}")

    return "\n".join(lines)


def _cli() -> None:
    p = argparse.ArgumentParser(prog="resume_capsule")
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("build")
    s.add_argument("--thread-id", required=True, dest="thread_id")
    args = p.parse_args()
    if args.cmd == "build":
        print(json.dumps(build(args.thread_id), default=str))


if __name__ == "__main__":
    _cli()
