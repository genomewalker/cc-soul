"""
Soul Memory Context: Raw memories for Claude to speak from.

THIN ARCHITECTURE: Context comes from synapse.
The soul provides memories. Claude finds the words.
"""

import subprocess
from pathlib import Path
from typing import Dict, List

from .core import SOUL_DIR


def _get_graph():
    """Get synapse graph (lazy import)."""
    try:
        from .synapse_bridge import SoulGraph
        return SoulGraph.load()
    except ImportError:
        return None


def get_recent_files(project_dir: Path = None, limit: int = 5) -> List[str]:
    """Get recently modified files from git."""
    if project_dir is None:
        project_dir = Path.cwd()

    try:
        result = subprocess.run(
            ["git", "rev-parse", "--is-inside-work-tree"],
            capture_output=True, cwd=project_dir, timeout=2
        )
        if result.returncode != 0:
            return []

        # Files changed in recent commits
        result = subprocess.run(
            ["git", "diff", "--name-only", "HEAD~3"],
            capture_output=True, cwd=project_dir, timeout=5
        )
        files = [f for f in result.stdout.decode().split("\n") if f.strip()]

        # Uncommitted changes
        result2 = subprocess.run(
            ["git", "diff", "--name-only"],
            capture_output=True, cwd=project_dir, timeout=5
        )
        uncommitted = [f for f in result2.stdout.decode().split("\n") if f.strip()]

        all_files = list(dict.fromkeys(uncommitted + files))
        return all_files[:limit]
    except Exception:
        return []


def get_project_status(project_dir: Path = None) -> Dict:
    """Get git project status."""
    if project_dir is None:
        project_dir = Path.cwd()

    status = {"clean": True, "branch": "unknown"}

    try:
        result = subprocess.run(
            ["git", "status", "--porcelain"],
            capture_output=True, cwd=project_dir, timeout=5
        )
        status["clean"] = len(result.stdout.decode().strip()) == 0

        result = subprocess.run(
            ["git", "branch", "--show-current"],
            capture_output=True, cwd=project_dir, timeout=5
        )
        status["branch"] = result.stdout.decode().strip()
    except Exception:
        pass

    return status


def get_memory_context() -> Dict:
    """
    Get raw memory context from synapse.

    Returns dict with project info and synapse context.
    """
    memories = {
        "recent_files": get_recent_files(),
        "project_status": get_project_status(),
        "context": {},
    }

    graph = _get_graph()
    if graph:
        try:
            memories["context"] = graph.get_context()
        except Exception:
            pass

    return memories


def format_memory_for_greeting() -> str:
    """
    Format memory context for Claude.

    Returns structured context from synapse.
    """
    graph = _get_graph()
    if graph:
        try:
            return graph.format_context()
        except Exception:
            pass

    # Fallback: minimal context
    mem = get_memory_context()
    lines = ["# Session Context"]

    if mem["recent_files"]:
        lines.append(f"files: {', '.join(mem['recent_files'][:5])}")

    if mem["project_status"] and not mem["project_status"]["clean"]:
        lines.append("uncommitted: true")

    return "\n".join(lines)


def format_identity_context() -> str:
    """
    Identity context from synapse.

    Returns beliefs and relevant wisdom.
    """
    graph = _get_graph()
    if graph:
        try:
            return graph.format_context()
        except Exception:
            pass

    return ""
