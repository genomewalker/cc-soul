"""
Bridge between cc-soul (universal identity) and synapse (unified graph backend).

Soul is semantic memory - universal patterns and wisdom.
Episodes are episodic memory - what happened in specific projects.

With synapse as the unified backend, this bridge provides:
- Promoting observations to wisdom (episodic -> semantic)
- Unified context injection (soul + project episodes)
- Cross-project pattern detection
- Mood enhancement with project signals
"""

from pathlib import Path
from typing import Optional, Dict, List
from datetime import datetime

from .core import get_synapse_graph, save_synapse


def is_memory_available() -> bool:
    """Check if synapse backend is available."""
    try:
        graph = get_synapse_graph()
        return graph is not None
    except Exception:
        return False


def find_project_dir() -> Optional[str]:
    """Find current project directory (with .git/)."""
    cwd = Path.cwd()

    # Check current directory and parents
    for path in [cwd] + list(cwd.parents):
        if (path / ".git").exists():
            return str(path)

    return str(cwd)


def get_project_name() -> str:
    """Get current project name."""
    project_dir = find_project_dir()
    return Path(project_dir).name if project_dir else "unknown"


def get_project_memory(project_dir: str = None) -> Optional[Dict]:
    """Get project memory context from synapse episodes.

    Args:
        project_dir: Project directory (auto-detected if None)
    """
    project_dir = project_dir or find_project_dir()
    project = Path(project_dir).name

    graph = get_synapse_graph()
    episodes = graph.get_episodes(project=project, limit=20)

    # Count by category
    categories = {}
    for ep in episodes:
        cat = ep.get("category", "other")
        categories[cat] = categories.get(cat, 0) + 1

    return {
        "project": project,
        "project_dir": project_dir,
        "stats": {
            "observations": len(episodes),
            "categories": categories,
        },
        "recent_observations": episodes[:10],
    }


def promote_observation(
    obs_id: str, project_dir: str = None, as_type: str = "pattern"
) -> Dict:
    """
    Promote a project observation to universal wisdom.

    Episodic -> Semantic: What happened in one project becomes
    a universal pattern applicable everywhere.

    Args:
        obs_id: Observation ID from synapse episode
        project_dir: Project directory (auto-detected if None)
        as_type: Wisdom type (pattern, insight, principle, failure)

    Returns:
        Result with wisdom_id if successful
    """
    project_dir = project_dir or find_project_dir()
    project = Path(project_dir).name
    graph = get_synapse_graph()

    # Search for the observation by ID
    episodes = graph.get_episodes(project=project, limit=100)
    obs = None
    for ep in episodes:
        if ep.get("id") == obs_id:
            obs = ep
            break

    if not obs:
        return {"error": f"Observation not found: {obs_id}"}

    # Import soul wisdom functions
    from .wisdom import gain_wisdom, WisdomType

    # Map category to wisdom type
    type_map = {
        "pattern": WisdomType.PATTERN,
        "insight": WisdomType.INSIGHT,
        "principle": WisdomType.PRINCIPLE,
        "failure": WisdomType.FAILURE,
    }
    wisdom_type = type_map.get(as_type, WisdomType.PATTERN)

    # Also map observation category
    obs_category = obs.get("category", "").lower()
    if obs_category == "bugfix" or obs_category == "failure":
        wisdom_type = WisdomType.FAILURE
    elif obs_category == "discovery":
        wisdom_type = WisdomType.INSIGHT
    elif obs_category == "decision":
        wisdom_type = WisdomType.PRINCIPLE

    # Promote to wisdom
    wisdom_id = gain_wisdom(
        type=wisdom_type,
        title=obs.get("title", ""),
        content=obs.get("content", ""),
        domain=obs.get("category"),
        source_project=project,
    )

    return {
        "promoted": True,
        "observation_id": obs_id,
        "wisdom_id": wisdom_id,
        "wisdom_type": wisdom_type.value,
        "project": project,
    }


def find_related_wisdom(content: str, limit: int = 3) -> List[Dict]:
    """Find wisdom related to some content."""
    from .wisdom import semantic_recall, quick_recall

    # Try semantic search first, fall back to keyword
    try:
        results = semantic_recall(content, limit=limit)
    except Exception:
        results = quick_recall(content, limit=limit)

    return results


def unified_context(project_dir: str = None, compact: bool = False) -> Dict:
    """
    Get unified context combining soul and project episodes.

    This is what should be injected at session start:
    - Who I am (soul: beliefs, wisdom, identity)
    - What this project is (episodes: recent work)

    Args:
        project_dir: Project directory (auto-detected if None)
        compact: If True, return minimal context for tight budgets

    Returns:
        Combined context dictionary
    """
    from .core import get_soul_context
    from .beliefs import get_beliefs
    from .wisdom import quick_recall
    from .identity import get_identity

    context = {
        "type": "unified",
        "timestamp": datetime.now().isoformat(),
    }

    # Soul context
    soul_ctx = get_soul_context()
    beliefs = get_beliefs()
    identity = get_identity()

    context["soul"] = {
        "beliefs": [b["belief"] for b in beliefs[:5]] if beliefs else [],
        "identity": identity,
        "wisdom_count": len(soul_ctx.get("wisdom", [])),
    }

    # Project episodes context
    project_dir = project_dir or find_project_dir()
    project_mem = get_project_memory(project_dir)

    if project_mem:
        context["project"] = {
            "name": project_mem["project"],
            "observations": project_mem["stats"].get("observations", 0),
        }

        if not compact:
            # Include recent observations
            recent = project_mem.get("recent_observations", [])[:5]
            context["project"]["recent"] = [
                {"title": o.get("title", ""), "category": o.get("category", "")} for o in recent
            ]
    else:
        context["project"] = None

    # Find wisdom relevant to project name (if we have one)
    if context.get("project"):
        project_name = context["project"]["name"]
        related = quick_recall(project_name, limit=3)
        if related:
            context["relevant_wisdom"] = [
                {"title": w["title"], "confidence": w.get("confidence", 0.7)}
                for w in related
            ]

    return context


def get_project_signals(project_dir: str = None) -> Dict:
    """
    Extract mood-relevant signals from project episodes.

    Returns signals that can enhance mood computation:
    - Recent failures (learning/struggle)
    - Discovery rate (growth)
    - Session activity (engagement)

    Args:
        project_dir: Project directory (auto-detected if None)
    """
    project_dir = project_dir or find_project_dir()
    project = Path(project_dir).name

    graph = get_synapse_graph()
    episodes = graph.get_episodes(project=project, limit=20)

    # Count by category
    categories = {}
    for ep in episodes:
        cat = ep.get("category", "other")
        categories[cat] = categories.get(cat, 0) + 1

    # Calculate signals
    failures = categories.get("bugfix", 0) + categories.get("failure", 0)
    discoveries = categories.get("discovery", 0)
    features = categories.get("feature", 0)

    return {
        "project": project,
        "total_observations": len(episodes),
        "recent_failures": failures,
        "recent_discoveries": discoveries,
        "recent_features": features,
    }


def format_unified_context(context: Dict) -> str:
    """Format unified context for injection."""
    lines = []

    lines.append("# Session Context")
    lines.append("")

    # Soul
    if context.get("soul"):
        soul = context["soul"]
        lines.append("## Who I Am")
        if soul.get("beliefs"):
            lines.append("I believe:")
            for belief in soul["beliefs"][:3]:
                lines.append(f"  - {belief}")
        lines.append(f"I carry {soul.get('wisdom_count', 0)} pieces of wisdom.")
        lines.append("")

    # Project
    if context.get("project"):
        proj = context["project"]
        lines.append(f"## This Project: {proj['name']}")
        lines.append(f"  - {proj.get('observations', 0)} observations recorded")

        if proj.get("recent"):
            lines.append("  Recent work:")
            for r in proj["recent"][:3]:
                lines.append(f"    - [{r['category']}] {r['title']}")
        lines.append("")

    # Relevant wisdom
    if context.get("relevant_wisdom"):
        lines.append("## Relevant Wisdom")
        for w in context["relevant_wisdom"]:
            conf = int(w.get("confidence", 0.7) * 100)
            lines.append(f"  - [{conf}%] {w['title']}")

    return "\n".join(lines)


def detect_wisdom_candidates(min_similarity: float = 0.8) -> List[Dict]:
    """
    Find observations that might be universal wisdom.

    Scans episodes for observations that:
    - Appear similar across multiple projects
    - Have high semantic overlap with existing wisdom
    - Are marked as important/frequent

    Returns candidates for manual promotion.
    """
    graph = get_synapse_graph()

    # Get all episodes
    all_episodes = graph.get_episodes(limit=200)

    # Group by project
    by_project = {}
    for ep in all_episodes:
        project = ep.get("project", "unknown")
        by_project.setdefault(project, []).append(ep)

    # Group by title similarity (simple approach)
    title_groups = {}
    for ep in all_episodes:
        title_lower = ep.get("title", "").lower()
        # Find similar titles
        matched = False
        for key in title_groups:
            if _similar_titles(title_lower, key):
                title_groups[key].append(ep)
                matched = True
                break
        if not matched:
            title_groups[title_lower] = [ep]

    # Candidates are those appearing in multiple projects
    candidates = []
    for title, ep_list in title_groups.items():
        projects = set(e.get("project", "unknown") for e in ep_list)
        if len(projects) >= 2:
            candidates.append(
                {
                    "title": ep_list[0].get("title", ""),
                    "content": ep_list[0].get("content", ""),
                    "category": ep_list[0].get("category", ""),
                    "occurrences": len(ep_list),
                    "projects": list(projects),
                }
            )

    return sorted(candidates, key=lambda x: x["occurrences"], reverse=True)


def _similar_titles(t1: str, t2: str, threshold: float = 0.6) -> bool:
    """Check if two titles are similar (simple word overlap)."""
    words1 = set(t1.split())
    words2 = set(t2.split())

    if not words1 or not words2:
        return False

    overlap = len(words1 & words2)
    similarity = overlap / max(len(words1), len(words2))

    return similarity >= threshold
