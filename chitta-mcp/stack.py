#!/usr/bin/env python3
"""Shared stack installer/status for cc-soul across Claude Code and Codex."""

from __future__ import annotations

import argparse
import importlib.metadata
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import install as mcp_install


def _repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def _shared_install_script() -> Path:
    return _repo_root() / "scripts" / "smart-install.sh"


def _claude_home() -> Path:
    return Path.home() / ".claude"


def _codex_home() -> Path:
    return Path(os.environ.get("CODEX_HOME", Path.home() / ".codex"))


def _run(cmd: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, check=check, capture_output=True, text=True)


def _command_output(cmd: list[str]) -> str:
    try:
        result = _run(cmd, check=False)
    except FileNotFoundError:
        return ""
    if result.returncode != 0:
        return ""
    return result.stdout.strip() or result.stderr.strip()


def _command_version(cmd: str) -> str:
    output = _command_output([cmd, "--version"])
    if not output:
        return ""
    return output.splitlines()[0].strip()


def _distribution_version(name: str) -> str:
    versions = [record["version"] for record in _distribution_records(name)]
    if not versions:
        return ""
    return max(versions, key=_version_key)


def _version_key(version: str) -> tuple:
    parts: list[int | str] = []
    for token in version.replace("-", ".").split("."):
        parts.append(int(token) if token.isdigit() else token)
    return tuple(parts)


def _status_line(label: str, value: str) -> str:
    return f"{label:<24} {value}"


def _resolve_command(*candidates: str) -> str:
    for candidate in candidates:
        if not candidate:
            continue
        resolved = shutil.which(candidate) if "/" not in candidate else candidate
        if resolved and Path(resolved).exists():
            return resolved
    return ""


def _json_file(path: Path) -> dict:
    if not path.is_file():
        return {}
    try:
        return json.loads(path.read_text())
    except json.JSONDecodeError:
        return {}


def _distribution_records(name: str) -> list[dict[str, str | Path]]:
    records: list[dict[str, str | Path]] = []
    for dist in importlib.metadata.distributions():
        dist_name = (dist.metadata.get("Name") or "").lower()
        if dist_name != name.lower():
            continue
        dist_path = getattr(dist, "_path", None)
        if dist_path is None:
            continue
        records.append({"version": dist.version, "path": Path(dist_path)})
    return records


def _systemd_daemon_state() -> str:
    if shutil.which("systemctl"):
        state = _command_output(["systemctl", "--user", "is-active", "chittad"])
        if state:
            return state
    if shutil.which("pgrep"):
        return "running" if _command_output(["pgrep", "-f", "chittad daemon"]) else "stopped"
    return "unknown"


def _bridge_install_cmd() -> list[str] | None:
    script = shutil.which("chitta-bridge-install")
    if script:
        return [script]

    sibling_repo = _repo_root().parent / "opencode-bridge" / "chitta_bridge" / "install.py"
    if sibling_repo.is_file():
        return [sys.executable, str(sibling_repo)]

    return None


def _install_shared_backend() -> bool:
    script = _shared_install_script()
    if not script.is_file():
        print(f"  shared backend: install script not found at {script}")
        return False
    try:
        subprocess.run(["bash", str(script)], check=True)
    except subprocess.CalledProcessError as exc:
        print(f"  shared backend: failed ({exc.returncode})")
        return False
    return True


def _install_codex_bridge() -> bool:
    cmd = _bridge_install_cmd()
    if not cmd:
        print("  Codex bridge: chitta-bridge-install not found; skipping")
        return False
    try:
        subprocess.run([*cmd, "codex"], check=True)
    except subprocess.CalledProcessError as exc:
        print(f"  Codex bridge: failed ({exc.returncode})")
        return False
    return True


def _uninstall_codex_bridge() -> bool:
    script = shutil.which("chitta-bridge-uninstall")
    if script:
        try:
            subprocess.run([script, "codex"], check=True)
        except subprocess.CalledProcessError as exc:
            print(f"  Codex bridge: uninstall failed ({exc.returncode})")
            return False
        return True
    return False


def install_stack(target: str, *, skip_bridge: bool) -> int:
    ok = True
    print("chitta-stack install:")

    if target in {"shared", "all"}:
        ok = _install_shared_backend() and ok

    if target in {"claude-code", "all"}:
        ok = mcp_install._install_claude_code() and ok

    if target in {"codex", "all"}:
        ok = mcp_install._install_codex() and ok
        if not skip_bridge:
            _install_codex_bridge()

    print("\ndone." if ok else "\ncompleted with warnings.")
    return 0 if ok else 1


def uninstall_stack(target: str, *, skip_bridge: bool) -> int:
    print("chitta-stack uninstall:")

    if target in {"claude-code", "all"}:
        mcp_install._uninstall_claude_code()

    if target in {"codex", "all"}:
        if not skip_bridge:
            _uninstall_codex_bridge()
        mcp_install._uninstall_codex()

    print("\ndone.")
    return 0


def print_status() -> int:
    claude_home = _claude_home()
    codex_home = _codex_home()
    claude_settings = _json_file(claude_home / "settings.json")
    codex_config = (codex_home / "config.toml").read_text() if (codex_home / "config.toml").is_file() else ""
    chitta_entry = (((claude_settings.get("mcpServers") or {}).get("chitta")) or {}).get("command", "")
    chitta_mcp_cmd = _resolve_command(
        "chitta-mcp",
        str(Path.home() / ".local" / "bin" / "chitta-mcp"),
        chitta_entry,
    )
    chitta_mcp_version = _distribution_version("chitta-mcp") or _command_version(chitta_mcp_cmd)

    print("Shared backend")
    print(_status_line("chitta", _command_version(str(claude_home / "bin" / "chitta")) or "missing"))
    print(_status_line("chittad", _command_version(str(claude_home / "bin" / "chittad")) or "missing"))
    print(_status_line("daemon", _systemd_daemon_state()))
    print(_status_line("mind path", str(claude_home / "mind")))
    print(_status_line("socket", str(claude_home / "mind" / "chittad.sock")))
    print(_status_line("chitta-mcp", chitta_mcp_version or "missing"))

    print("\nClaude Code adapter")
    print(_status_line("settings.json", "present" if (claude_home / "settings.json").is_file() else "missing"))
    print(_status_line("MCP server", chitta_entry or "not configured"))

    print("\nCodex adapter")
    print(
        _status_line(
            "cc-soul plugin",
            "installed" if (codex_home / "plugins" / "cache" / "local" / "cc-soul" / "local").is_dir() else "missing",
        )
    )
    print(
        _status_line(
            "cc-soul config",
            "enabled" if '[plugins."cc-soul@local"]' in codex_config else "not configured",
        )
    )
    print(
        _status_line(
            "bridge plugin",
            "installed"
            if (codex_home / "plugins" / "cache" / "local" / "chitta-bridge" / "local").is_dir()
            else "missing",
        )
    )
    print(
        _status_line(
            "bridge config",
            "present" if '[mcp_servers.chitta-bridge]' in codex_config else "not configured",
        )
    )
    print(_status_line("chitta-bridge", _distribution_version("chitta-bridge") or "missing"))
    return 0


def doctor_stack(*, fix: bool) -> int:
    managed = ("chitta-mcp", "chitta-bridge")
    issues = 0
    changes = 0

    print("chitta-stack doctor:")
    for name in managed:
        records = _distribution_records(name)
        if not records:
            print(f"  {name}: not installed")
            continue

        kept = max(records, key=lambda record: _version_key(str(record["version"])))
        duplicates = [record for record in records if record["path"] != kept["path"]]

        if not duplicates:
            print(f"  {name}: ok ({kept['version']})")
            continue

        issues += len(duplicates)
        print(f"  {name}: keeping {kept['version']} @ {kept['path']}")
        for duplicate in sorted(duplicates, key=lambda record: _version_key(str(record["version"]))):
            print(f"    stale {duplicate['version']} @ {duplicate['path']}")
            if fix:
                shutil.rmtree(Path(duplicate["path"]))
                changes += 1

    if fix:
        print(f"\nremoved {changes} stale dist-info directories." if changes else "\nno cleanup needed.")
    elif issues:
        print("\nrerun with: chitta-stack doctor --fix")
    else:
        print("\nno issues found.")

    return 0


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="chitta-stack",
        description="Manage one shared cc-soul backend with Claude Code and Codex adapters.",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    install_p = sub.add_parser("install", help="Install the shared backend and/or frontend adapters")
    install_p.add_argument(
        "target",
        nargs="?",
        default="all",
        choices=["shared", "claude-code", "codex", "all"],
        help="What to install (default: all)",
    )
    install_p.add_argument(
        "--skip-bridge",
        action="store_true",
        help="Skip the Codex chitta-bridge adapter when installing Codex",
    )

    uninstall_p = sub.add_parser("uninstall", help="Remove frontend adapters without touching the shared backend")
    uninstall_p.add_argument(
        "target",
        nargs="?",
        default="all",
        choices=["claude-code", "codex", "all"],
        help="Which frontend adapters to remove (default: all)",
    )
    uninstall_p.add_argument(
        "--skip-bridge",
        action="store_true",
        help="Skip chitta-bridge removal when uninstalling Codex",
    )

    sub.add_parser("status", help="Show shared-backend and frontend-adapter status")
    doctor_p = sub.add_parser("doctor", help="Inspect or clean stale Python package metadata for the shared stack")
    doctor_p.add_argument(
        "--fix",
        action="store_true",
        help="Remove stale dist-info directories for chitta-mcp and chitta-bridge",
    )
    return parser


def main() -> None:
    args = _parser().parse_args()
    if args.command == "install":
        raise SystemExit(install_stack(args.target, skip_bridge=args.skip_bridge))
    if args.command == "uninstall":
        raise SystemExit(uninstall_stack(args.target, skip_bridge=args.skip_bridge))
    if args.command == "doctor":
        raise SystemExit(doctor_stack(fix=args.fix))
    raise SystemExit(print_status())


if __name__ == "__main__":
    main()
