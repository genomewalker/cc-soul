"""
Project Registry - Track all project memories for cross-project access.

The soul maintains a registry of all projects with synapse episodes.
This enables:
- Cross-project search ("I've seen this pattern before...")
- Pattern detection across codebases
- Universal access to project-specific learnings
"""

from pathlib import Path
from datetime import datetime
from typing import List, Dict, Optional

from .core import get_synapse_graph, save_synapse, SOUL_DIR


def register_project(project_path: str = None, name: str = None) -> Dict:
    """
    Register a project in the soul's project registry.

    Args:
        project_path: Path to project directory (auto-detected if None)
        name: Project name (defaults to directory name)

    Returns:
        Registration result
    """
    if project_path is None:
        project_path = _find_project_dir()

    project_path = str(Path(project_path).resolve())
    memory_dir = Path(project_path) / ".memory"

    if not memory_dir.exists():
        return {"error": f"No .memory directory found at {project_path}"}

    if name is None:
        name = Path(project_path).name

    graph = get_synapse_graph()

    # Count episodes for this project
    obs_count = 0
    session_count = 0
    try:
        episodes = graph.get_episodes(project=project_path, limit=1000)
        obs_count = len(episodes)
        session_episodes = graph.get_episodes(category="session_ledger", project=project_path, limit=100)
        session_count = len(session_episodes)
    except Exception:
        pass
    now = datetime.now().isoformat()

    content = (
        f"path: {project_path}\n"
        f"observations: {obs_count}\n"
        f"sessions: {session_count}\n"
        f"registered_at: {now}"
    )

    graph.observe(
        category="project_registry",
        title=name,
        content=content,
        project=project_path,
        tags=["registry", "project"],
    )
    save_synapse()

    return {
        "registered": True,
        "name": name,
        "path": project_path,
        "observations": obs_count,
        "sessions": session_count,
    }


def list_projects() -> List[Dict]:
    """List all registered projects."""
    graph = get_synapse_graph()
    episodes = graph.get_episodes(category="project_registry", limit=100)

    projects = []
    seen_paths = set()

    for ep in episodes:
        content = ep.get("content", "")
        lines = content.split("\n")

        path = ""
        obs = 0
        sessions = 0
        registered_at = ""

        for line in lines:
            if line.startswith("path: "):
                path = line[6:].strip()
            elif line.startswith("observations: "):
                try:
                    obs = int(line[14:].strip())
                except ValueError:
                    pass
            elif line.startswith("sessions: "):
                try:
                    sessions = int(line[10:].strip())
                except ValueError:
                    pass
            elif line.startswith("registered_at: "):
                registered_at = line[15:].strip()

        if path and path not in seen_paths:
            seen_paths.add(path)
            exists = Path(path).exists() and (Path(path) / ".memory").exists()
            projects.append({
                "name": ep.get("title", Path(path).name),
                "path": path,
                "last_accessed": ep.get("created_at", registered_at),
                "observations": obs,
                "sessions": sessions,
                "registered_at": registered_at,
                "exists": exists,
            })

    projects.sort(key=lambda x: x.get("last_accessed", ""), reverse=True)
    return projects


def unregister_project(project_path: str) -> bool:
    """Remove a project from the registry."""
    project_path = str(Path(project_path).resolve())
    projects_before = list_projects()

    graph = get_synapse_graph()
    graph.observe(
        category="project_unregistered",
        title=f"unregistered: {Path(project_path).name}",
        content=f"path: {project_path}\nunregistered_at: {datetime.now().isoformat()}",
        project=project_path,
        tags=["registry", "unregistered"],
    )
    save_synapse()

    return any(p["path"] == project_path for p in projects_before)


def refresh_project_stats():
    """Refresh observation/session counts for all registered projects."""
    projects = list_projects()
    updated = 0

    for proj in projects:
        if not proj["exists"]:
            continue

        try:
            register_project(proj["path"], proj["name"])
            updated += 1
        except Exception:
            continue

    return {"updated": updated, "total": len(projects)}


def search_all_projects(query: str, limit: int = 20) -> List[Dict]:
    """
    Search observations across ALL registered projects.

    This is federated search - queries each project's episodes and combines results.

    Args:
        query: Search query
        limit: Max results per project

    Returns:
        Combined results from all projects
    """
    projects = list_projects()
    all_results = []
    query_lower = query.lower()
    graph = get_synapse_graph()

    for proj in projects:
        if not proj["exists"]:
            continue

        try:
            episodes = graph.get_episodes(project=proj["path"], limit=100)

            for ep in episodes:
                title = ep.get("title", "").lower()
                content = ep.get("content", "").lower()
                category = ep.get("category", "").lower()

                if query_lower in title or query_lower in content or query_lower in category:
                    ep["_project"] = proj["name"]
                    ep["_project_path"] = proj["path"]
                    all_results.append(ep)
        except Exception:
            continue

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
    projects = list_projects()
    all_episodes = []
    graph = get_synapse_graph()

    for proj in projects:
        if not proj["exists"]:
            continue

        try:
            episodes = graph.get_episodes(project=proj["path"], limit=100)
            for ep in episodes:
                ep["_project"] = proj["name"]
                ep["_project_path"] = proj["path"]
                all_episodes.append(ep)
        except Exception:
            continue

    title_groups = {}
    for ep in all_episodes:
        title_key = _normalize_title(ep.get("title", ""))
        if title_key not in title_groups:
            title_groups[title_key] = []
        title_groups[title_key].append(ep)

    patterns = []
    for title_key, ep_list in title_groups.items():
        projects_with_pattern = set(e["_project"] for e in ep_list)

        if len(projects_with_pattern) >= min_occurrences:
            patterns.append({
                "title": ep_list[0].get("title", ""),
                "content": ep_list[0].get("content", ""),
                "category": ep_list[0].get("category", ""),
                "occurrences": len(ep_list),
                "projects": list(projects_with_pattern),
                "observation_ids": [e.get("id") for e in ep_list],
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
