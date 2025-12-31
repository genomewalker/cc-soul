"""
MCP tools for project registry - cross-project memory access.
"""


@mcp.tool()
def register_project(path: str = "", name: str = "") -> str:
    """Register a project in the soul's project registry.

    Enables the soul to access this project's memories from anywhere.

    Args:
        path: Path to project directory (auto-detected if empty)
        name: Project name (defaults to directory name)
    """
    from ..project_registry import register_project as _register

    result = _register(
        project_path=path if path else None,
        name=name if name else None
    )

    if "error" in result:
        return f"Error: {result['error']}"

    return (
        f"Registered: {result['name']}\n"
        f"Path: {result['path']}\n"
        f"Observations: {result['observations']}\n"
        f"Sessions: {result['sessions']}"
    )


@mcp.tool()
def list_registered_projects() -> str:
    """List all projects registered in the soul's project registry.

    Shows projects the soul can access for cross-project search.
    """
    from ..project_registry import list_projects

    projects = list_projects()

    if not projects:
        return "No projects registered. Use register_project() to add projects."

    lines = ["Registered Projects:", "=" * 50]

    for p in projects:
        status = "✓" if p["exists"] else "✗ (not found)"
        lines.append(f"\n{status} {p['name']}")
        lines.append(f"   Path: {p['path']}")
        lines.append(f"   Observations: {p['observations']} | Sessions: {p['sessions']}")
        lines.append(f"   Last accessed: {p['last_accessed'][:10] if p['last_accessed'] else 'never'}")

    return "\n".join(lines)


@mcp.tool()
def unregister_project(path: str) -> str:
    """Remove a project from the soul's registry.

    Args:
        path: Path to project directory to unregister
    """
    from ..project_registry import unregister_project as _unregister

    if _unregister(path):
        return f"Unregistered: {path}"
    return f"Project not found in registry: {path}"


@mcp.tool()
def search_all_projects(query: str, limit: int = 20) -> str:
    """Search observations across ALL registered projects.

    Federated search - finds relevant observations from any project
    the soul has access to.

    Args:
        query: Search query
        limit: Max results (default 20)
    """
    from ..project_registry import search_all_projects as _search

    results = _search(query, limit=limit)

    if not results:
        return f"No results found for '{query}' across registered projects."

    lines = [f"Search results for '{query}':", "=" * 50]

    for r in results[:limit]:
        project = r.get("_project", "unknown")
        title = r.get("title", "Untitled")
        category = r.get("category", "")
        content = r.get("content", "")[:100]

        lines.append(f"\n[{project}] {title}")
        lines.append(f"   Category: {category}")
        lines.append(f"   {content}...")

    lines.append(f"\n({len(results)} total results)")
    return "\n".join(lines)


@mcp.tool()
def cross_project_stats() -> str:
    """Get aggregate statistics across all registered projects.

    Shows total observations, sessions, and project breakdown.
    """
    from ..project_registry import get_cross_project_stats

    stats = get_cross_project_stats()

    lines = [
        "Cross-Project Statistics",
        "=" * 50,
        f"Registered projects: {stats['registered_projects']}",
        f"Active projects: {stats['active_projects']}",
        f"Total observations: {stats['total_observations']}",
        f"Total sessions: {stats['total_sessions']}",
    ]

    if stats["projects"]:
        lines.append("\nBy project:")
        for p in stats["projects"]:
            lines.append(f"  {p['name']}: {p['observations']} observations")

    return "\n".join(lines)


@mcp.tool()
def find_cross_project_patterns(min_occurrences: int = 2) -> str:
    """Find patterns that appear across multiple projects.

    These are candidates for promotion to universal wisdom -
    learnings that transcend individual codebases.

    Args:
        min_occurrences: Minimum times pattern must appear (default 2)
    """
    from ..project_registry import find_cross_project_patterns as _find

    patterns = _find(min_occurrences=min_occurrences)

    if not patterns:
        return "No cross-project patterns found yet. Keep learning!"

    lines = [
        "Cross-Project Patterns (wisdom candidates)",
        "=" * 50,
    ]

    for p in patterns[:10]:
        projects_str = ", ".join(p["projects"][:3])
        if len(p["projects"]) > 3:
            projects_str += f" +{len(p['projects']) - 3} more"

        lines.append(f"\n[{p['occurrences']}x] {p['title']}")
        lines.append(f"   Category: {p['category']}")
        lines.append(f"   Projects: {projects_str}")

    lines.append(f"\n({len(patterns)} patterns found)")
    lines.append("\nUse promote_to_wisdom() to convert patterns to universal wisdom.")

    return "\n".join(lines)


@mcp.tool()
def refresh_registry_stats() -> str:
    """Refresh observation/session counts for all registered projects.

    Updates the registry with current stats from each project.
    """
    from ..project_registry import refresh_project_stats

    result = refresh_project_stats()

    if "error" in result:
        return f"Error: {result['error']}"

    return f"Updated {result['updated']}/{result['total']} projects"
