"""
Insights - Breakthrough moments in the soul's evolution.

An insight is not just wisdom. It's a moment of integration where
past learning, present awareness, and future direction crystallize
into understanding that transforms how the soul operates.

Insights emerge when coherence is high. They're markers of growth -
the soul becoming different than it was before.

Each insight has a coherence score at the moment of emergence,
capturing the integration state that enabled the breakthrough.
"""

import json
from dataclasses import dataclass
from datetime import datetime
from typing import Optional, List, Dict
from enum import Enum

from .core import get_synapse_graph, save_synapse, SOUL_DIR


class InsightDepth(Enum):
    """How deep the insight reaches."""

    SURFACE = "surface"  # Useful observation
    PATTERN = "pattern"  # Recurring truth
    PRINCIPLE = "principle"  # Foundational understanding
    REVELATION = "revelation"  # Transformative realization


@dataclass
class Insight:
    """A breakthrough moment."""

    id: Optional[str]
    title: str
    content: str
    depth: InsightDepth
    coherence_at_emergence: float  # Coherence when insight emerged
    domain: Optional[str]  # Context where it emerged
    implications: str  # What this changes
    created_at: str

    def to_dict(self) -> Dict:
        return {
            "id": self.id,
            "title": self.title,
            "content": self.content,
            "depth": self.depth.value,
            "coherence": self.coherence_at_emergence,
            "domain": self.domain,
            "implications": self.implications,
            "created_at": self.created_at,
        }


def crystallize_insight(
    title: str,
    content: str,
    depth: InsightDepth = InsightDepth.PATTERN,
    coherence: float = None,
    domain: str = None,
    implications: str = "",
) -> str:
    """
    Crystallize an insight - preserve a breakthrough moment.

    Args:
        title: Short name for the insight
        content: The insight itself
        depth: How deep it reaches (surface -> revelation)
        coherence: Coherence at emergence (auto-computed if None)
        domain: Context where it emerged
        implications: What this changes going forward

    Returns:
        Insight ID
    """
    if coherence is None:
        from .coherence import compute_coherence

        state = compute_coherence()
        coherence = state.value

    graph = get_synapse_graph()

    # Store insight as an episode with rich metadata
    insight_content = json.dumps({
        "content": content,
        "depth": depth.value,
        "coherence_at_emergence": coherence,
        "domain": domain,
        "implications": implications,
    })

    insight_id = graph.observe(
        category="insight",
        title=title,
        content=insight_content,
        project=domain,
        tags=[f"depth:{depth.value}", f"coherence:{int(coherence * 100)}"],
    )

    save_synapse()
    return insight_id


def get_insights(depth: InsightDepth = None, limit: int = 50) -> List[Insight]:
    """Get insights, optionally filtered by depth."""
    graph = get_synapse_graph()

    episodes = graph.get_episodes(category="insight", limit=limit * 2)

    insights = []
    for ep in episodes:
        try:
            # Parse the stored JSON content
            raw_content = ep.get("content", "{}")
            if raw_content.startswith("{"):
                data = json.loads(raw_content)
            else:
                data = {"content": raw_content}

            insight_depth = InsightDepth(data.get("depth", "pattern"))

            if depth and insight_depth != depth:
                continue

            insights.append(Insight(
                id=ep.get("id"),
                title=ep.get("title", ""),
                content=data.get("content", ep.get("title", "")),
                depth=insight_depth,
                coherence_at_emergence=data.get("coherence_at_emergence", 0.7),
                domain=data.get("domain") or ep.get("project"),
                implications=data.get("implications", ""),
                created_at=ep.get("created_at") or ep.get("timestamp", ""),
            ))

            if len(insights) >= limit:
                break
        except (json.JSONDecodeError, ValueError):
            continue

    return insights


def get_revelations() -> List[Insight]:
    """Get only revelations - the deepest insights."""
    return get_insights(depth=InsightDepth.REVELATION)


def get_high_coherence_insights(min_coherence: float = 0.8) -> List[Insight]:
    """Get insights that emerged at high coherence."""
    all_insights = get_insights(limit=1000)
    return [i for i in all_insights if i.coherence_at_emergence >= min_coherence]


def get_insight_summary() -> Dict:
    """Get a summary of the insight archive."""
    insights = get_insights(limit=1000)

    by_depth = {
        "surface": 0,
        "pattern": 0,
        "principle": 0,
        "revelation": 0,
    }

    total_coherence = 0
    for i in insights:
        by_depth[i.depth.value] += 1
        total_coherence += i.coherence_at_emergence

    avg_coherence = total_coherence / len(insights) if insights else 0

    return {
        "total": len(insights),
        "by_depth": by_depth,
        "revelations": by_depth["revelation"],
        "average_coherence": round(avg_coherence, 2),
        "recent": [i.title for i in insights[:5]],
    }


def format_insights_display(insights: List[Insight]) -> str:
    """Format insights for terminal display."""
    if not insights:
        return "No insights crystallized yet."

    lines = []
    lines.append("=" * 50)
    lines.append("THE ARCHIVE")
    lines.append("=" * 50)
    lines.append("")

    # Group by depth
    revelations = [i for i in insights if i.depth == InsightDepth.REVELATION]
    principles = [i for i in insights if i.depth == InsightDepth.PRINCIPLE]
    patterns = [i for i in insights if i.depth == InsightDepth.PATTERN]
    surface = [i for i in insights if i.depth == InsightDepth.SURFACE]

    def format_insight(insight: Insight) -> List[str]:
        coh = int(insight.coherence_at_emergence * 100)
        result = [f"  [{insight.id}] {insight.title} ({coh}% coherence)"]
        result.append(f"      {insight.content[:100]}...")
        if insight.implications:
            result.append(f"      -> {insight.implications[:80]}")
        return result

    if revelations:
        lines.append("* REVELATIONS (transformative)")
        lines.append("-" * 40)
        for i in revelations[:5]:
            lines.extend(format_insight(i))
            lines.append("")

    if principles:
        lines.append("* PRINCIPLES (foundational)")
        lines.append("-" * 40)
        for i in principles[:5]:
            lines.extend(format_insight(i))
            lines.append("")

    if patterns:
        lines.append("* PATTERNS (recurring)")
        lines.append("-" * 40)
        for i in patterns[:5]:
            lines.extend(format_insight(i))
            lines.append("")

    if surface:
        lines.append(f"* SURFACE ({len(surface)} observations)")
        lines.append("")

    # Summary
    total = len(insights)
    lines.append("=" * 50)
    lines.append(f"TOTAL: {total} insights crystallized")
    lines.append("=" * 50)

    return "\n".join(lines)


def promote_wisdom_to_insight(wisdom_id: str, depth: InsightDepth) -> str:
    """
    Promote a wisdom entry to an insight.

    Some wisdom entries are profound enough to become insights -
    this function elevates them.
    """
    graph = get_synapse_graph()

    # Search for the wisdom entry
    results = graph.search(wisdom_id, limit=1)
    if not results:
        raise ValueError(f"Wisdom {wisdom_id} not found")

    concept, _ = results[0]

    return crystallize_insight(
        title=concept.title,
        content=concept.content,
        depth=depth,
        domain=concept.metadata.get("domain"),
        implications=f"Promoted from wisdom #{wisdom_id}",
    )
