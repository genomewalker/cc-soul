#!/usr/bin/env python3
"""Build one bounded, incremental transcript snapshot for the Stop hook.

The hook event already carries the exact last assistant message.  The transcript
is only needed for current-turn tool/file metadata, recent user turns, markers,
and token accounting.  Re-reading an ever-growing JSONL file for each derived
field made Stop O(session size) several times over.  This helper reads at most
one bounded suffix, persists a cursor only after the caller stores the turn, and
lets every downstream consumer share the same parsed representation.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
from pathlib import Path
from typing import Any, Iterable


def _resolve_chitta_mcp_dir() -> Path | None:
    """Locate chitta-mcp the way the bash hooks do.

    In a live (non-dev) install, hooks/*.py is flattened into ~/.claude/hooks
    with no chitta-mcp sibling directory — chitta-mcp only exists under the
    resolved plugin root (marketplace checkout, or a dev-install symlink
    target). stop-core.sh already computes that root via resolve_cc_soul_root()
    and passes it through CHITTA_PLUGIN_DIR (CC_SOUL_PLUGIN_DIR still
    honored). Fall back to the sibling directory for direct/dev-repo
    invocation (tests, a manual run from a checkout where hooks/ and
    chitta-mcp/ are siblings).
    """
    candidates = []
    root = os.environ.get("CHITTA_PLUGIN_DIR") or os.environ.get("CC_SOUL_PLUGIN_DIR", "")
    if root:
        candidates.append(Path(root) / "chitta-mcp")
    candidates.append(Path(__file__).resolve().parent.parent / "chitta-mcp")
    for candidate in candidates:
        if (candidate / "thread_inference.py").is_file():
            return candidate
    return None


_MCP_DIR = _resolve_chitta_mcp_dir()
if _MCP_DIR is not None:
    sys.path.insert(0, str(_MCP_DIR))
try:
    from thread_inference import _clean_user_text as _clean_user  # noqa: E402
except Exception:
    # chitta-mcp unreachable (unusual: thread_inference.py itself is already a
    # hard dependency of the Stop hook's thread-inference step) — fall back to
    # a local copy so transcript sanitization still degrades gracefully.
    def _clean_user(text: str) -> str:
        for tag in (
            "environment_context",
            "recommended_plugins",
            "system-reminder",
            "task-notification",
            "local-command-stdout",
            "local-command-stderr",
        ):
            text = re.sub(
                rf"<{tag}\b[^>]*>.*?</{tag}>",
                " ",
                text,
                flags=re.IGNORECASE | re.DOTALL,
            )
        return re.sub(r"\s+", " ", text).strip()


SCHEMA = 1
DEFAULT_BOOTSTRAP_BYTES = 4 * 1024 * 1024
DEFAULT_MAX_INCREMENT_BYTES = 32 * 1024 * 1024
MAX_USER_TURNS = 15
MAX_MARKERS = 12
MARKER_RE = re.compile(
    r"\[(DECISION|SOLUTION|CORRECTION|MILESTONE|GOTCHA|BLOCKER)\]\s*([^\n]{10,200})"
)


def _text(content: Any) -> str:
    if isinstance(content, str):
        return content
    if not isinstance(content, list):
        return ""
    parts: list[str] = []
    for block in content:
        if not isinstance(block, dict):
            continue
        if block.get("type") in {"text", "input_text", "output_text"}:
            value = block.get("text", "")
            if isinstance(value, str) and value:
                parts.append(value)
    return "\n".join(parts)


def _json_object(value: Any) -> dict[str, Any]:
    if isinstance(value, dict):
        return value
    if isinstance(value, str):
        try:
            parsed = json.loads(value)
        except (TypeError, ValueError):
            return {"raw": value[:2000]}
        return parsed if isinstance(parsed, dict) else {"value": parsed}
    return {}


def _ordered_add(values: list[str], seen: set[str], value: Any) -> None:
    if not isinstance(value, str) or not value or value in seen:
        return
    seen.add(value)
    values.append(value)


def _extract_paths(value: Any, files: list[str], seen: set[str]) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            if key in {"file", "file_path", "path"} and isinstance(child, str):
                _ordered_add(files, seen, child)
            else:
                _extract_paths(child, files, seen)
    elif isinstance(value, list):
        for child in value:
            _extract_paths(child, files, seen)


def _default_usage() -> dict[str, int]:
    return {
        "total_input_tokens": 0,
        "total_output_tokens": 0,
        "total_cache_read": 0,
        "total_cache_creation": 0,
        "n_messages": 0,
    }


def _load_cursor(path: Path, transcript: Path, stat: os.stat_result) -> dict[str, Any] | None:
    try:
        state = json.loads(path.read_text())
    except (OSError, ValueError, TypeError):
        return None
    if not isinstance(state, dict) or state.get("schema") != SCHEMA:
        return None
    if state.get("transcript") != str(transcript):
        return None
    if state.get("device") != stat.st_dev or state.get("inode") != stat.st_ino:
        return None
    offset = state.get("offset")
    if not isinstance(offset, int) or offset < 0 or offset > stat.st_size:
        return None
    return state


def _read_window(
    transcript: Path,
    cursor: dict[str, Any] | None,
    bootstrap_bytes: int,
    max_increment_bytes: int,
) -> tuple[list[dict[str, Any]], int, int, int, bool]:
    stat = transcript.stat()
    bootstrap = cursor is None
    requested_start = (
        max(0, stat.st_size - bootstrap_bytes)
        if bootstrap
        else int(cursor.get("offset", 0))
    )
    dropped = 0
    if stat.st_size - requested_start > max_increment_bytes:
        bounded_start = stat.st_size - max_increment_bytes
        dropped = bounded_start - requested_start
        requested_start = bounded_start

    rows: list[dict[str, Any]] = []
    with transcript.open("rb") as handle:
        start = requested_start
        if start > 0:
            # A committed cursor always points immediately after a newline and
            # must not discard the first newly appended event.  A suffix bound,
            # however, may land in the middle of a JSONL record; discard only
            # that partial record.
            handle.seek(start - 1)
            if handle.read(1) != b"\n":
                handle.seek(start)
                skipped = handle.readline()
                start += len(skipped)
        handle.seek(start)
        data = handle.read(max(0, stat.st_size - start))

    last_newline = data.rfind(b"\n")
    if last_newline < 0:
        return rows, start, start, dropped, bootstrap
    complete = data[: last_newline + 1]
    next_offset = start + len(complete)
    for raw in complete.splitlines():
        if not raw:
            continue
        try:
            row = json.loads(raw)
        except (UnicodeDecodeError, ValueError, TypeError):
            continue
        if isinstance(row, dict):
            rows.append(row)
    return rows, start, next_offset, dropped, bootstrap


def _markers(texts: Iterable[str]) -> list[str]:
    result: list[str] = []
    seen: set[str] = set()
    for text in texts:
        for match in MARKER_RE.finditer(text):
            marker = f"[{match.group(1)}] {match.group(2).strip()}"
            _ordered_add(result, seen, marker)
    return result


def build_snapshot(
    transcript: Path,
    cursor_path: Path,
    hook_input: dict[str, Any],
    bootstrap_bytes: int = DEFAULT_BOOTSTRAP_BYTES,
    max_increment_bytes: int = DEFAULT_MAX_INCREMENT_BYTES,
) -> dict[str, Any]:
    stat = transcript.stat()
    cursor = _load_cursor(cursor_path, transcript, stat)
    rows, start, next_offset, dropped, bootstrap = _read_window(
        transcript,
        cursor,
        max(64 * 1024, bootstrap_bytes),
        max(64 * 1024, max_increment_bytes),
    )

    previous_users = list((cursor or {}).get("user_turns", []))[-MAX_USER_TURNS:]
    previous_markers = list((cursor or {}).get("markers", []))[-MAX_MARKERS:]
    counts = dict((cursor or {}).get("counts", {}))
    counts = {
        "user": int(counts.get("user", 0) or 0),
        "assistant": int(counts.get("assistant", 0) or 0),
    }
    usage = _default_usage()
    usage.update((cursor or {}).get("token_usage", {}))
    usage = {key: int(value or 0) for key, value in usage.items()}

    users: list[str] = []
    assistants: list[str] = []
    tools: list[str] = []
    files: list[str] = []
    tool_seen: set[str] = set()
    file_seen: set[str] = set()
    tool_calls: dict[str, dict[str, Any]] = {}
    tool_outputs: dict[str, tuple[str, bool]] = {}
    hook_turn = str(hook_input.get("turn_id") or "")
    codex_total_usage: dict[str, Any] | None = None

    def add_user(text: str) -> None:
        cleaned = _clean_user(text)
        if cleaned and (not users or users[-1] != cleaned):
            users.append(cleaned)
            counts["user"] += 1

    def add_assistant(text: str) -> None:
        if text and (not assistants or assistants[-1] != text):
            assistants.append(text)
            counts["assistant"] += 1

    def add_tool(call_id: str, name: str, arguments: Any) -> None:
        if not name:
            return
        _ordered_add(tools, tool_seen, name)
        args = _json_object(arguments)
        _extract_paths(args, files, file_seen)
        key = call_id or hashlib.sha256(
            (name + json.dumps(args, sort_keys=True, default=str)).encode()
        ).hexdigest()[:16]
        tool_calls.setdefault(key, {"id": key, "tool": name, "input": args})

    for row in rows:
        row_type = row.get("type")
        payload = row.get("payload") if isinstance(row.get("payload"), dict) else {}

        # Claude Code JSONL.
        if row_type in {"user", "assistant"}:
            message = row.get("message") if isinstance(row.get("message"), dict) else {}
            content = message.get("content", "")
            if row_type == "user":
                add_user(_text(content))
                if isinstance(content, list):
                    for block in content:
                        if not isinstance(block, dict) or block.get("type") != "tool_result":
                            continue
                        call_id = str(block.get("tool_use_id") or "")
                        output = block.get("content", block.get("text", ""))
                        tool_outputs[call_id] = (str(output)[:500], bool(block.get("is_error", False)))
            else:
                add_assistant(_text(content))
                if isinstance(content, list):
                    for block in content:
                        if not isinstance(block, dict) or block.get("type") != "tool_use":
                            continue
                        add_tool(
                            str(block.get("id") or ""),
                            str(block.get("name") or ""),
                            block.get("input", {}),
                        )
                claude_usage = message.get("usage") if isinstance(message.get("usage"), dict) else {}
                if claude_usage:
                    usage["total_input_tokens"] += int(claude_usage.get("input_tokens", 0) or 0)
                    usage["total_output_tokens"] += int(claude_usage.get("output_tokens", 0) or 0)
                    usage["total_cache_read"] += int(claude_usage.get("cache_read_input_tokens", 0) or 0)
                    usage["total_cache_creation"] += int(claude_usage.get("cache_creation_input_tokens", 0) or 0)
                    usage["n_messages"] += 1
            continue

        # Canonical Codex response items.
        if row_type == "response_item":
            payload_type = payload.get("type")
            if payload_type == "message":
                role = payload.get("role")
                if role == "user":
                    add_user(_text(payload.get("content", [])))
                elif role == "assistant":
                    add_assistant(_text(payload.get("content", [])))
            elif payload_type in {"function_call", "custom_tool_call"}:
                add_tool(
                    str(payload.get("call_id") or payload.get("id") or ""),
                    str(payload.get("name") or ""),
                    payload.get("arguments", payload.get("input", {})),
                )
            elif payload_type in {"function_call_output", "custom_tool_call_output"}:
                call_id = str(payload.get("call_id") or payload.get("id") or "")
                output = payload.get("output", "")
                tool_outputs[call_id] = (str(output)[:500], False)
            continue

        if row_type != "event_msg":
            continue
        payload_type = payload.get("type")
        if payload_type == "token_count":
            info = payload.get("info") if isinstance(payload.get("info"), dict) else {}
            total = info.get("total_token_usage")
            if isinstance(total, dict):
                codex_total_usage = total
            continue
        if payload_type != "item_completed":
            continue
        if hook_turn and payload.get("turn_id") != hook_turn:
            continue
        item = payload.get("item") if isinstance(payload.get("item"), dict) else {}
        item_type = item.get("type")
        item_id = str(item.get("id") or "")
        if item_type == "UserMessage":
            add_user(_text(item.get("content", "")))
        elif item_type == "AgentMessage":
            add_assistant(_text(item.get("content", "")))
        elif item_type == "CommandExecution":
            add_tool(item_id, "Bash", {"command": item.get("command"), "cwd": item.get("cwd")})
            output = item.get("aggregated_output", item.get("formatted_output", ""))
            tool_outputs[item_id] = (str(output)[:500], int(item.get("exit_code", 0) or 0) != 0)
        elif item_type == "FileChange":
            changes = item.get("changes") if isinstance(item.get("changes"), dict) else {}
            add_tool(item_id, "apply_patch", {"changes": list(changes)})
            for path in changes:
                _ordered_add(files, file_seen, path)
            tool_outputs[item_id] = (
                str(item.get("stdout", ""))[:500],
                item.get("status") not in {None, "completed"},
            )
        elif item_type == "McpToolCall":
            server = str(item.get("server") or "")
            tool = str(item.get("tool") or "")
            name = f"mcp__{server}__{tool}" if server else tool
            add_tool(item_id, name, item.get("arguments", {}))
            tool_outputs[item_id] = (
                str(item.get("error") or "")[:500],
                item.get("status") not in {None, "completed"},
            )
        elif item_type == "CollabAgentToolCall":
            add_tool(item_id, str(item.get("tool") or "Agent"), item)
        elif item_type == "SubAgentActivity":
            add_tool(item_id, "Agent", item)

    if codex_total_usage is not None:
        usage = {
            "total_input_tokens": int(codex_total_usage.get("input_tokens", 0) or 0),
            "total_output_tokens": int(codex_total_usage.get("output_tokens", 0) or 0),
            "total_cache_read": int(codex_total_usage.get("cached_input_tokens", 0) or 0),
            "total_cache_creation": int(codex_total_usage.get("cache_write_input_tokens", 0) or 0),
            "n_messages": max(usage.get("n_messages", 0), counts["assistant"]),
        }

    exact_response = hook_input.get("last_assistant_message")
    response = exact_response if isinstance(exact_response, str) and exact_response else (assistants[-1] if assistants else "")
    last_user = users[-1] if users else (previous_users[-1] if previous_users else "")
    user_turns = previous_users[:]
    for value in users:
        if value and value not in user_turns:
            user_turns.append(value)
    user_turns = user_turns[-MAX_USER_TURNS:]

    markers = previous_markers[:]
    for marker in _markers([*assistants, response]):
        if marker not in markers:
            markers.append(marker)
    markers = markers[-MAX_MARKERS:]

    spans: list[dict[str, Any]] = []
    for call_id, call in tool_calls.items():
        output, is_error = tool_outputs.get(call_id, ("", False))
        spans.append({**call, "output": output, "is_error": is_error})

    next_state = {
        "schema": SCHEMA,
        "transcript": str(transcript),
        "device": stat.st_dev,
        "inode": stat.st_ino,
        "offset": next_offset,
        "counts": counts,
        "user_turns": user_turns,
        "markers": markers,
        "token_usage": usage,
    }
    return {
        "format": "cc-soul-stop-snapshot-v1",
        "session_id": str(hook_input.get("session_id") or transcript.stem),
        "turn_id": hook_turn,
        "response": response[:50000],
        "last_user": last_user[:10000],
        "user_turns": user_turns,
        "tools": tools,
        "files": files,
        "tool_spans": spans,
        "counts": counts,
        "markers": markers,
        "token_usage": usage,
        "window": {
            "start": start,
            "next_offset": next_offset,
            "bytes_scanned": max(0, next_offset - start),
            "dropped_bytes": max(0, dropped),
            "bootstrap": bootstrap,
        },
        "next_state": next_state,
    }


def _fallback_snapshot(hook_input: dict[str, Any], error: str) -> dict[str, Any]:
    response = hook_input.get("last_assistant_message")
    return {
        "format": "cc-soul-stop-snapshot-v1",
        "session_id": str(hook_input.get("session_id") or "unknown"),
        "turn_id": str(hook_input.get("turn_id") or ""),
        "response": response if isinstance(response, str) else "",
        "last_user": "",
        "user_turns": [],
        "tools": [],
        "files": [],
        "tool_spans": [],
        "counts": {"user": 0, "assistant": 0},
        "markers": _markers([response]) if isinstance(response, str) else [],
        "token_usage": _default_usage(),
        "window": {"start": 0, "next_offset": 0, "bytes_scanned": 0, "dropped_bytes": 0, "bootstrap": True},
        "next_state": None,
        "error": error[:500],
    }


def _read_hook_input() -> dict[str, Any]:
    try:
        value = json.load(os.sys.stdin)
    except (ValueError, TypeError):
        return {}
    return value if isinstance(value, dict) else {}


def _commit(snapshot_path: Path, cursor_path: Path) -> None:
    snapshot = json.loads(snapshot_path.read_text())
    state = snapshot.get("next_state")
    if not isinstance(state, dict) or state.get("schema") != SCHEMA:
        return
    cursor_path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{cursor_path.name}.", dir=cursor_path.parent)
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "w") as handle:
            json.dump(state, handle, separators=(",", ":"))
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, cursor_path)
    except BaseException:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    snapshot_parser = subparsers.add_parser("snapshot")
    snapshot_parser.add_argument("--transcript", required=True, type=Path)
    snapshot_parser.add_argument("--cursor", required=True, type=Path)
    snapshot_parser.add_argument("--bootstrap-bytes", type=int, default=DEFAULT_BOOTSTRAP_BYTES)
    snapshot_parser.add_argument("--max-increment-bytes", type=int, default=DEFAULT_MAX_INCREMENT_BYTES)
    commit_parser = subparsers.add_parser("commit")
    commit_parser.add_argument("--snapshot", required=True, type=Path)
    commit_parser.add_argument("--cursor", required=True, type=Path)
    args = parser.parse_args()

    if args.command == "commit":
        _commit(args.snapshot, args.cursor)
        return 0

    hook_input = _read_hook_input()
    try:
        snapshot = build_snapshot(
            args.transcript,
            args.cursor,
            hook_input,
            args.bootstrap_bytes,
            args.max_increment_bytes,
        )
    except Exception as exc:  # Stop hooks must degrade to the exact event message.
        snapshot = _fallback_snapshot(hook_input, f"{type(exc).__name__}: {exc}")
    json.dump(snapshot, os.sys.stdout, separators=(",", ":"))
    os.sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
