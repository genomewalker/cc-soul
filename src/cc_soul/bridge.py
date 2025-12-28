"""
Bridge between cc-soul (universal identity) and cc-memory (project-local).

Soul is semantic memory - universal patterns and wisdom.
Memory is episodic memory - what happened in specific projects.

The bridge enables:
- Promoting observations to wisdom (episodic → semantic)
- Unified context injection (soul + project memory)
- Cross-project pattern detection
- Mood enhancement with project signals
"""

from pathlib import Path
from typing import Optional, Dict, List
from datetime import datetime



# Try to import cc-memory
CC_MEMORY_AVAILABLE = False
try:
    from cc_memory import memory as cc_memory

    CC_MEMORY_AVAILABLE = True
except ImportError:
    cc_memory = None


def is_memory_available() -> bool:
    """Check if cc-memory is installed."""
    return CC_MEMORY_AVAILABLE


def find_project_dir() -> Optional[str]:
    """Find current project directory (with .memory/ or .git/)."""
    cwd = Path.cwd()

    # Check current directory and parents
    for path in [cwd] + list(cwd.parents):
        if (path / ".memory").exists():
            return str(path)
        if (path / ".git").exists():
            return str(path)

    return str(cwd)


def ensure_memory_initialized(project_dir: str) -> bool:
    """Initialize memory in project if not already present.

    Auto-creates .memory in any directory where Claude is running.
    This enables seamless soul-memory integration without manual init.
    """
    if not CC_MEMORY_AVAILABLE:
        return False

    memory_dir = Path(project_dir) / ".memory"

    if memory_dir.exists():
        return True

    cc_memory.init_memory(project_dir)
    return True


def get_project_memory(
    project_dir: str = None, auto_init: bool = True
) -> Optional[Dict]:
    """Get project memory context if available.

    Args:
        project_dir: Project directory (auto-detected if None)
        auto_init: If True, initialize memory in git repos without .memory
    """
    if not CC_MEMORY_AVAILABLE:
        return None

    project_dir = project_dir or find_project_dir()
    memory_dir = Path(project_dir) / ".memory"

    if not memory_dir.exists():
        if auto_init:
            if not ensure_memory_initialized(project_dir):
                return None
        else:
            return None

    try:
        stats = cc_memory.get_stats(project_dir)
        config = cc_memory.get_config(project_dir)
        recent = cc_memory.get_recent_observations(project_dir, limit=10)
        sessions = cc_memory.get_recent_sessions(project_dir, limit=3)

        return {
            "project": Path(project_dir).name,
            "project_dir": project_dir,
            "stats": stats,
            "config": config,
            "recent_observations": recent,
            "recent_sessions": sessions,
        }
    except Exception as e:
        return {"error": str(e)}


def promote_observation(
    obs_id: str, project_dir: str = None, as_type: str = "pattern"
) -> Dict:
    """
    Promote a project observation to universal wisdom.

    Episodic → Semantic: What happened in one project becomes
    a universal pattern applicable everywhere.

    Args:
        obs_id: Observation ID from cc-memory
        project_dir: Project directory (auto-detected if None)
        as_type: Wisdom type (pattern, insight, principle, failure)

    Returns:
        Result with wisdom_id if successful
    """
    if not CC_MEMORY_AVAILABLE:
        return {"error": "cc-memory not installed"}

    project_dir = project_dir or find_project_dir()

    # Get the observation
    obs = cc_memory.get_observation_by_id(project_dir, obs_id)
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
        title=obs["title"],
        content=obs["content"],
        domain=obs.get("category"),
        source_project=Path(project_dir).name,
    )

    return {
        "promoted": True,
        "observation_id": obs_id,
        "wisdom_id": wisdom_id,
        "wisdom_type": wisdom_type.value,
        "project": Path(project_dir).name,
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
    Get unified context combining soul and project memory.

    This is what should be injected at session start:
    - Who I am (soul: beliefs, wisdom, identity)
    - What this project is (memory: config, recent work)

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

    # Project memory context
    project_dir = project_dir or find_project_dir()
    project_mem = get_project_memory(project_dir)

    if project_mem and "error" not in project_mem:
        context["project"] = {
            "name": project_mem["project"],
            "observations": project_mem["stats"].get("observations", 0),
            "sessions": project_mem["stats"].get("sessions", 0),
        }

        if not compact:
            # Include recent observations
            recent = project_mem.get("recent_observations", [])[:5]
            context["project"]["recent"] = [
                {"title": o["title"], "category": o["category"]} for o in recent
            ]

            # Include config
            context["project"]["config"] = project_mem.get("config", {})
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


def get_project_signals(project_dir: str = None, auto_init: bool = True) -> Dict:
    """
    Extract mood-relevant signals from project memory.

    Returns signals that can enhance mood computation:
    - Recent failures (learning/struggle)
    - Discovery rate (growth)
    - Session activity (engagement)

    Args:
        project_dir: Project directory (auto-detected if None)
        auto_init: If True, initialize memory in git repos without .memory
    """
    if not CC_MEMORY_AVAILABLE:
        return {}

    project_dir = project_dir or find_project_dir()

    if auto_init:
        if not ensure_memory_initialized(project_dir):
            return {}
    elif not (Path(project_dir) / ".memory").exists():
        return {}

    try:
        stats = cc_memory.get_stats(project_dir)
        recent = cc_memory.get_recent_observations(project_dir, limit=20)

        # Count by category
        categories = {}
        for obs in recent:
            cat = obs.get("category", "other")
            categories[cat] = categories.get(cat, 0) + 1

        # Calculate signals
        failures = categories.get("bugfix", 0) + categories.get("failure", 0)
        discoveries = categories.get("discovery", 0)
        features = categories.get("feature", 0)

        return {
            "project": Path(project_dir).name,
            "total_observations": stats.get("observations", 0),
            "recent_failures": failures,
            "recent_discoveries": discoveries,
            "recent_features": features,
            "sessions": stats.get("sessions", 0),
            "tokens_invested": stats.get("total_tokens_work", 0),
        }
    except Exception as e:
        return {"error": str(e)}


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
        lines.append(f"  - {proj.get('sessions', 0)} past sessions")

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

    Scans project memories for observations that:
    - Appear similar across multiple projects
    - Have high semantic overlap with existing wisdom
    - Are marked as important/frequent

    Returns candidates for manual promotion.
    """
    if not CC_MEMORY_AVAILABLE:
        return []

    # Find all project directories with memory
    candidates = []
    home = Path.home()

    # Look in common project locations
    search_paths = [
        home / "projects",
        home / "code",
        home / "repos",
        Path("/maps/projects"),  # HPC paths
    ]

    all_observations = []

    for search_path in search_paths:
        if not search_path.exists():
            continue

        for project_dir in search_path.iterdir():
            if not project_dir.is_dir():
                continue

            memory_dir = project_dir / ".memory"
            if not memory_dir.exists():
                continue

            try:
                recent = cc_memory.get_recent_observations(str(project_dir), limit=50)
                for obs in recent:
                    obs["_project"] = project_dir.name
                    all_observations.append(obs)
            except Exception:
                continue

    # Group by title similarity (simple approach)
    # A more sophisticated approach would use embeddings
    title_groups = {}
    for obs in all_observations:
        title_lower = obs["title"].lower()
        # Find similar titles
        matched = False
        for key in title_groups:
            if _similar_titles(title_lower, key):
                title_groups[key].append(obs)
                matched = True
                break
        if not matched:
            title_groups[title_lower] = [obs]

    # Candidates are those appearing in multiple projects
    for title, obs_list in title_groups.items():
        projects = set(o["_project"] for o in obs_list)
        if len(projects) >= 2:
            candidates.append(
                {
                    "title": obs_list[0]["title"],
                    "content": obs_list[0]["content"],
                    "category": obs_list[0]["category"],
                    "occurrences": len(obs_list),
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
