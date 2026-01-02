"""
Dreams - Visions that spark the soul's evolution.

Dreams are not aspirations (directions of growth). Dreams are wilder -
glimpses of possibility that may not yet be actionable. They live in
synapse graph as episodes because they emerge from specific contexts,
but they transcend those contexts to inspire universal growth.

Dreams spark:
- New aspirations (when a dream becomes a direction)
- Insights (when a dream illuminates understanding)
- Architecture evolution (when dreams reshape how the soul works)

The soul periodically harvests dreams, letting them influence
its trajectory toward new horizons.
"""

from dataclasses import dataclass
from datetime import datetime, timedelta
from typing import Optional, List, Dict
from pathlib import Path

from .core import get_synapse_graph, save_synapse


DREAM_CATEGORY = "dream"


@dataclass
class Dream:
    """A vision that sparks evolution."""

    id: str
    title: str
    content: str
    horizon: str  # What new territory this opens
    sparked_at: str
    project: str


def dream(title: str, content: str, horizon: str = "") -> Optional[str]:
    """
    Record a dream - a vision of possibility.

    Dreams are stored in synapse graph as episodes because they emerge
    from specific work, but they transcend that context.

    Args:
        title: Short name for the dream
        content: The vision itself
        horizon: What new territory this opens

    Returns:
        Dream ID if successful, None on failure
    """
    try:
        graph = get_synapse_graph()

        full_content = content
        if horizon:
            full_content += f"\n\nHorizon: {horizon}"

        tags = ["dream"]
        if horizon:
            tags.append("has_horizon")

        obs_id = graph.observe(
            category=DREAM_CATEGORY,
            title=title,
            content=full_content,
            tags=tags,
        )

        save_synapse()
        return obs_id
    except Exception:
        return None


def harvest_dreams(days: int = 30) -> List[Dream]:
    """
    Harvest dreams from synapse graph.

    Scans recent episodes for dreams that might inspire growth.
    """
    try:
        graph = get_synapse_graph()

        episodes = graph.get_episodes(category=DREAM_CATEGORY, limit=100)

        dreams = []
        cutoff = (datetime.now() - timedelta(days=days)).isoformat()

        for ep in episodes:
            timestamp = ep.get("timestamp", ep.get("created_at", ""))
            if timestamp and timestamp < cutoff:
                continue

            content = ep.get("content", "")
            horizon = ""
            if "\n\nHorizon: " in content:
                parts = content.split("\n\nHorizon: ")
                content = parts[0]
                horizon = parts[1] if len(parts) > 1 else ""

            project = ep.get("project", "")
            if not project:
                project = Path.cwd().name

            dreams.append(
                Dream(
                    id=ep.get("id", ""),
                    title=ep.get("title", ""),
                    content=content,
                    horizon=horizon,
                    sparked_at=timestamp,
                    project=project,
                )
            )

        return dreams
    except Exception:
        return []


def spark_aspiration_from_dream(dream: Dream) -> int:
    """
    When a dream becomes actionable, spark it into an aspiration.

    This is the transition from vision to direction.
    """
    from .aspirations import aspire

    why = dream.horizon if dream.horizon else f"Sparked by dream: {dream.title}"
    return aspire(direction=dream.title, why=why)


def spark_insight_from_dream(dream: Dream) -> int:
    """
    When a dream illuminates understanding, crystallize it as insight.
    """
    from .insights import crystallize_insight, InsightDepth

    return crystallize_insight(
        title=f"Dream: {dream.title}",
        content=dream.content,
        depth=InsightDepth.PATTERN,
        domain=dream.project,
        implications=dream.horizon,
    )


def find_resonant_dreams(query: str, limit: int = 5) -> List[Dream]:
    """
    Find dreams that resonate with a query.

    Used to let dreams influence current work.
    """
    dreams = harvest_dreams(days=90)

    query_words = set(query.lower().split())
    scored = []

    for d in dreams:
        dream_words = set(d.title.lower().split()) | set(d.content.lower().split())
        overlap = len(query_words & dream_words)
        if overlap > 0:
            scored.append((overlap, d))

    scored.sort(key=lambda x: x[0], reverse=True)
    return [d for _, d in scored[:limit]]


def get_dream_summary() -> Dict:
    """Get summary of dream archive."""
    dreams = harvest_dreams(days=365)

    projects = set(d.project for d in dreams)
    with_horizon = sum(1 for d in dreams if d.horizon)

    return {
        "total": len(dreams),
        "projects": list(projects),
        "with_horizon": with_horizon,
        "recent_titles": [d.title for d in dreams[:5]],
    }


def format_dreams_display(dreams: List[Dream]) -> str:
    """Format dreams for terminal display."""
    if not dreams:
        return "No dreams recorded. Dream to evolve."

    lines = []
    lines.append("=" * 50)
    lines.append("DREAMS")
    lines.append("=" * 50)
    lines.append("")
    lines.append("Visions that spark evolution")
    lines.append("")

    for d in dreams[:10]:
        lines.append(f"  [{d.id}] {d.title}")
        lines.append(f"      {d.content[:80]}...")
        if d.horizon:
            lines.append(f"      -> Horizon: {d.horizon}")
        lines.append(f"      Project: {d.project}")
        lines.append("")

    if len(dreams) > 10:
        lines.append(f"  ... and {len(dreams) - 10} more dreams")

    return "\n".join(lines)


def let_dreams_influence_aspirations() -> List[Dict]:
    """
    Scan dreams and suggest new aspirations.

    This is a periodic soul maintenance function - letting
    dreams shape future direction.
    """
    from .aspirations import get_active_aspirations

    dreams = harvest_dreams(days=90)
    active_aspirations = get_active_aspirations()
    active_directions = {a.direction.lower() for a in active_aspirations}

    suggestions = []
    for d in dreams:
        if d.title.lower() in active_directions:
            continue

        if d.horizon:
            suggestions.append(
                {
                    "dream_id": d.id,
                    "title": d.title,
                    "horizon": d.horizon,
                    "source": "dream",
                }
            )

    return suggestions[:5]
