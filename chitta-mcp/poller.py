#!/usr/bin/env python3
"""
Background poller: checks status_check_cmd for active tasks and transitions them.
Called by shepherd-poll.sh on a schedule.
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from task_ledger import inbox_push, artifact_register
from provenance import snapshot, filesystem_diff

CHITTA_BIN = os.environ.get("CHITTA_BIN", str(Path.home() / ".claude/bin/chitta"))
MIND_PATH = os.environ.get("MIND_PATH", str(Path.home() / ".claude/mind"))

_SLURM_DONE = {"COMPLETED", "FAILED", "CANCELLED", "TIMEOUT", "NODE_FAIL", "OUT_OF_MEMORY"}
_SLURM_RUNNING = {"RUNNING", "PENDING", "COMPLETING", "CONFIGURING", "SUSPENDED"}


def _chitta(args: list[str]) -> object:
    try:
        r = subprocess.run(
            [CHITTA_BIN, *args], capture_output=True, text=True, timeout=10
        )
        if r.returncode == 0 and r.stdout.strip():
            return json.loads(r.stdout)
    except Exception:
        pass
    return None


def _run(cmd: str, timeout: int = 5) -> str:
    try:
        r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout)
        return (r.stdout + r.stderr).strip()
    except Exception as e:
        return f"ERROR: {e}"


def _sacct_state(job_id: str) -> str:
    try:
        r = subprocess.run(
            ["sacct", "-j", job_id, "--format=State", "--noheader", "-P"],
            capture_output=True, text=True, timeout=5,
        )
        lines = [l.strip().upper() for l in r.stdout.splitlines() if l.strip()]
        if not lines:
            return "COMPLETED"
        state = lines[0].split("|")[0]
        if any(x in state for x in ("FAILED", "CANCEL", "TIMEOUT", "OUT_OF_MEM")):
            return "FAILED"
        return "COMPLETED"
    except Exception:
        return "COMPLETED"


def _slurm_status(output: str, job_id: str) -> str:
    upper = output.upper().strip()
    if not upper or upper == "COMPLETED":
        return _sacct_state(job_id)
    for state in _SLURM_DONE:
        if state in upper:
            return "COMPLETED" if state == "COMPLETED" else "FAILED"
    for state in _SLURM_RUNNING:
        if state in upper:
            return "RUNNING"
    return "UNKNOWN"


def _artifact_kind(path: str) -> str:
    ext = Path(path).suffix.lower()
    if ext in (".png", ".svg", ".pdf", ".jpg", ".jpeg", ".html"):
        return "plot"
    if ext in (".tsv", ".csv", ".xlsx", ".parquet", ".feather"):
        return "table"
    if ext == ".log":
        return "log"
    if ext in (".h5", ".pkl", ".pt", ".pth", ".onnx", ".bin", ".model"):
        return "model"
    if ext in (".json", ".yaml", ".yml"):
        return "config"
    return "file"


def poll_task(task: dict) -> dict | None:
    """Return transition info or None if still running."""
    try:
        payload = json.loads(task.get("payload", "{}") or "{}")
    except Exception:
        payload = {}

    status_check = payload.get("status_check_cmd", "")
    if not status_check:
        return None

    job_id = payload.get("job_id", "")
    scheduler = payload.get("scheduler", "")
    task_id = task.get("id") or task.get("task_id", "")

    output = _run(status_check)

    if scheduler == "slurm" and job_id:
        state = _slurm_status(output, job_id)
        if state == "RUNNING":
            return None
        new_status = "completed" if state == "COMPLETED" else "failed"
    else:
        if "RUNNING" in output.upper():
            return None
        new_status = "completed" if "ERROR" not in output.upper() else "failed"

    return {
        "task_id": task_id,
        "new_status": new_status,
        "cwd": payload.get("cwd", ""),
        "payload": payload,
        "output": output,
    }


def poll_once() -> int:
    tasks_raw = _chitta(["query_tasks", "--status", "active", "--json"])
    if not tasks_raw:
        return 0
    tasks: list[dict] = (
        tasks_raw.get("tasks", []) if isinstance(tasks_raw, dict)
        else tasks_raw if isinstance(tasks_raw, list)
        else []
    )

    transitioned = 0
    for task in tasks:
        try:
            result = poll_task(task)
            if not result:
                continue

            task_id = result["task_id"]
            new_status = result["new_status"]
            cwd = result["cwd"]
            payload = result["payload"]
            output_tail = result["output"][-300:]

            # Transition in chitta
            if new_status == "completed":
                _chitta(["long_task_complete", "--task-id", task_id,
                         "--outcome", output_tail[:200]])
            else:
                _chitta(["long_task_event", "--task-id", task_id,
                         "--kind", "failed", "--payload", output_tail[:200]])

            # Register new artifacts via filesystem diff
            thread_id: str | None = payload.get("thread_id")
            if cwd and new_status == "completed":
                snap_path = Path(MIND_PATH) / f".fs_snapshot_{task_id}"
                if snap_path.exists():
                    before = json.loads(snap_path.read_text())
                    after = snapshot(cwd)
                    for f in filesystem_diff(before, after)[:20]:
                        try:
                            artifact_register(task_id, f, _artifact_kind(f),
                                              thread_id=thread_id)
                        except Exception:
                            pass
                    snap_path.unlink(missing_ok=True)

            # Inbox notification
            realm = payload.get("realm", "")
            goal = payload.get("goal", task_id)[:80]
            digest = f"{new_status.upper()}: {goal}"
            if output_tail:
                digest += f" — {output_tail[:120]}"

            inbox_push(
                task_id=task_id,
                event_type=new_status,
                digest=digest,
                target_realm=realm,
                thread_id=thread_id,
                payload={"output_tail": output_tail},
            )
            transitioned += 1

        except Exception as e:
            print(f"[poller] task error: {e}", file=sys.stderr)

    return transitioned


def _cli() -> None:
    p = argparse.ArgumentParser(prog="poller")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("poll_once")
    args = p.parse_args()
    if args.cmd == "poll_once":
        n = poll_once()
        print(json.dumps({"transitioned": n}))


if __name__ == "__main__":
    _cli()
