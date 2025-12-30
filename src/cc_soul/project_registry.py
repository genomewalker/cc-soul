"""
Project Registry - Track all project memories for cross-project access.

The soul maintains a registry of all projects where cc-memory is active.
This enables:
- Cross-project search ("I've seen this pattern before...")
- Pattern detection across codebases
- Universal access to project-specific learnings
"""

import sqlite3
from pathlib import Path
from datetime import datetime
from typing import List, Dict, Optional

from .core import get_db_connection, SOUL_DIR

# cc-memory import
CC_MEMORY_AVAILABLE = False
try:
    from cc_memory import memory as cc_memory
    CC_MEMORY_AVAILABLE = True
except ImportError:
    cc_memory = None


def _ensure_registry_table():
    """Ensure project registry table exists in soul database."""
    conn = get_db_connection()
    c = conn.cursor()
    c.execute("""
        CREATE TABLE IF NOT EXISTS project_registry (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            path TEXT UNIQUE NOT NULL,
            last_accessed TEXT,
            observation_count INTEGER DEFAULT 0,
            session_count INTEGER DEFAULT 0,
            registered_at TEXT NOT NULL
        )
    """)
    c.execute("CREATE INDEX IF NOT EXISTS idx_project_path ON project_registry(path)")
    conn.commit()
    conn.close()


def register_project(project_path: str = None, name: str = None) -> Dict:
    """
    Register a project in the soul's project registry.

    Args:
        project_path: Path to project directory (auto-detected if None)
        name: Project name (defaults to directory name)

    Returns:
        Registration result
    """
    _ensure_registry_table()

    if project_path is None:
        project_path = _find_project_dir()

    project_path = str(Path(project_path).resolve())
    memory_dir = Path(project_path) / ".memory"

    if not memory_dir.exists():
        return {"error": f"No .memory directory found at {project_path}"}

    if name is None:
        name = Path(project_path).name

    # Get stats from project memory
    obs_count = 0
    session_count = 0
    if CC_MEMORY_AVAILABLE:
        try:
            stats = cc_memory.get_stats(project_path)
            obs_count = stats.get("observations", 0)
            session_count = stats.get("sessions", 0)
        except Exception:
            pass

    conn = get_db_connection()
    c = conn.cursor()
    now = datetime.now().isoformat()

    # Upsert
    c.execute("""
        INSERT INTO project_registry (name, path, last_accessed, observation_count, session_count, registered_at)
        VALUES (?, ?, ?, ?, ?, ?)
        ON CONFLICT(path) DO UPDATE SET
            name = excluded.name,
            last_accessed = excluded.last_accessed,
            observation_count = excluded.observation_count,
            session_count = excluded.session_count
    """, (name, project_path, now, obs_count, session_count, now))

    conn.commit()
    conn.close()

    return {
        "registered": True,
        "name": name,
        "path": project_path,
        "observations": obs_count,
        "sessions": session_count,
    }


def list_projects() -> List[Dict]:
    """List all registered projects."""
    _ensure_registry_table()
    conn = get_db_connection()
    c = conn.cursor()

    c.execute("""
        SELECT name, path, last_accessed, observation_count, session_count, registered_at
        FROM project_registry
        ORDER BY last_accessed DESC
    """)

    projects = []
    for row in c.fetchall():
        # Check if project still exists
        exists = Path(row[1]).exists() and (Path(row[1]) / ".memory").exists()
        projects.append({
            "name": row[0],
            "path": row[1],
            "last_accessed": row[2],
            "observations": row[3],
            "sessions": row[4],
            "registered_at": row[5],
            "exists": exists,
        })

    conn.close()
    return projects


def unregister_project(project_path: str) -> bool:
    """Remove a project from the registry."""
    _ensure_registry_table()
    project_path = str(Path(project_path).resolve())

    conn = get_db_connection()
    c = conn.cursor()
    c.execute("DELETE FROM project_registry WHERE path = ?", (project_path,))
    deleted = c.rowcount > 0
    conn.commit()
    conn.close()
    return deleted


def refresh_project_stats():
    """Refresh observation/session counts for all registered projects."""
    if not CC_MEMORY_AVAILABLE:
        return {"error": "cc-memory not available"}

    projects = list_projects()
    updated = 0

    conn = get_db_connection()
    c = conn.cursor()

    for proj in projects:
        if not proj["exists"]:
            continue

        try:
            stats = cc_memory.get_stats(proj["path"])
            c.execute("""
                UPDATE project_registry
                SET observation_count = ?, session_count = ?
                WHERE path = ?
            """, (stats.get("observations", 0), stats.get("sessions", 0), proj["path"]))
            updated += 1
        except Exception:
            continue

    conn.commit()
    conn.close()
    return {"updated": updated, "total": len(projects)}


def search_all_projects(query: str, limit: int = 20) -> List[Dict]:
    """
    Search observations across ALL registered projects.

    This is federated search - queries each project's memory and combines results.

    Args:
        query: Search query
        limit: Max results per project

    Returns:
        Combined results from all projects
    """
    if not CC_MEMORY_AVAILABLE:
        return []

    projects = list_projects()
    all_results = []
    query_lower = query.lower()

    for proj in projects:
        if not proj["exists"]:
            continue

        try:
            # Get observations and filter by query
            observations = cc_memory.get_recent_observations(proj["path"], limit=100)

            for obs in observations:
                title = obs.get("title", "").lower()
                content = obs.get("content", "").lower()
                category = obs.get("category", "").lower()

                if query_lower in title or query_lower in content or query_lower in category:
                    obs["_project"] = proj["name"]
                    obs["_project_path"] = proj["path"]
                    all_results.append(obs)
        except Exception:
            continue

    # Sort by recency
    all_results.sort(key=lambda x: x.get("created_at", ""), reverse=True)

    return all_results[:limit]


def get_cross_project_stats() -> Dict:
    """Get aggregate statistics across all projects."""
    projects = list_projects()

    total_observations = sum(p["observations"] for p in projects if p["exists"])
    total_sessions = sum(p["sessions"] for p in projects if p["exists"])
    active_projects = sum(1 for p in projects if p["exists"])

    return {
        "registered_projects": len(projects),
        "active_projects": active_projects,
        "total_observations": total_observations,
        "total_sessions": total_sessions,
        "projects": [{"name": p["name"], "observations": p["observations"]}
                     for p in projects if p["exists"]][:10],
    }


def find_cross_project_patterns(min_occurrences: int = 2) -> List[Dict]:
    """
    Find patterns that appear across multiple projects.

    These are candidates for promotion to universal wisdom.
    """
    if not CC_MEMORY_AVAILABLE:
        return []

    projects = list_projects()
    all_observations = []

    for proj in projects:
        if not proj["exists"]:
            continue

        try:
            # Get recent observations from each project
            recent = cc_memory.get_recent_observations(proj["path"], limit=100)
            for obs in recent:
                obs["_project"] = proj["name"]
                obs["_project_path"] = proj["path"]
                all_observations.append(obs)
        except Exception:
            continue

    # Group by similar titles
    title_groups = {}
    for obs in all_observations:
        title_key = _normalize_title(obs.get("title", ""))
        if title_key not in title_groups:
            title_groups[title_key] = []
        title_groups[title_key].append(obs)

    # Find patterns appearing in multiple projects
    patterns = []
    for title_key, obs_list in title_groups.items():
        projects_with_pattern = set(o["_project"] for o in obs_list)

        if len(projects_with_pattern) >= min_occurrences:
            patterns.append({
                "title": obs_list[0].get("title", ""),
                "content": obs_list[0].get("content", ""),
                "category": obs_list[0].get("category", ""),
                "occurrences": len(obs_list),
                "projects": list(projects_with_pattern),
                "observation_ids": [o.get("id") for o in obs_list],
            })

    return sorted(patterns, key=lambda x: x["occurrences"], reverse=True)


def _normalize_title(title: str) -> str:
    """Normalize title for grouping similar observations."""
    return " ".join(sorted(title.lower().split()))


def _find_project_dir() -> str:
    """Find current project directory."""
    cwd = Path.cwd()

    for path in [cwd] + list(cwd.parents):
        if (path / ".memory").exists():
            return str(path)
        if (path / ".git").exists():
            return str(path)

    return str(cwd)


def auto_register_current_project() -> Optional[Dict]:
    """
    Auto-register the current project if it has .memory.

    Called at session start to keep registry updated.
    """
    project_dir = _find_project_dir()
    memory_dir = Path(project_dir) / ".memory"

    if memory_dir.exists():
        return register_project(project_dir)

    return None
