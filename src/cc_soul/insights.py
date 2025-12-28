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

from dataclasses import dataclass
from datetime import datetime
from typing import Optional, List, Dict
from enum import Enum

from .core import get_db_connection


class InsightDepth(Enum):
    """How deep the insight reaches."""

    SURFACE = "surface"  # Useful observation
    PATTERN = "pattern"  # Recurring truth
    PRINCIPLE = "principle"  # Foundational understanding
    REVELATION = "revelation"  # Transformative realization


@dataclass
class Insight:
    """A breakthrough moment."""

    id: Optional[int]
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


def _ensure_table():
    """Ensure insights table exists."""
    conn = get_db_connection()
    c = conn.cursor()
    c.execute("""
        CREATE TABLE IF NOT EXISTS insights (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            content TEXT NOT NULL,
            depth TEXT DEFAULT 'pattern',
            coherence_at_emergence REAL DEFAULT 0.7,
            domain TEXT,
            implications TEXT DEFAULT '',
            created_at TEXT NOT NULL
        )
    """)
    conn.commit()
    conn.close()


def crystallize_insight(
    title: str,
    content: str,
    depth: InsightDepth = InsightDepth.PATTERN,
    coherence: float = None,
    domain: str = None,
    implications: str = "",
) -> int:
    """
    Crystallize an insight - preserve a breakthrough moment.

    Args:
        title: Short name for the insight
        content: The insight itself
        depth: How deep it reaches (surface → revelation)
        coherence: Coherence at emergence (auto-computed if None)
        domain: Context where it emerged
        implications: What this changes going forward

    Returns:
        Insight ID
    """
    _ensure_table()

    if coherence is None:
        from .coherence import compute_coherence

        state = compute_coherence()
        coherence = state.value

    conn = get_db_connection()
    c = conn.cursor()

    now = datetime.now().isoformat()
    c.execute(
        """
        INSERT INTO insights (title, content, depth, coherence_at_emergence, domain, implications, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    """,
        (title, content, depth.value, coherence, domain, implications, now),
    )

    insight_id = c.lastrowid
    conn.commit()
    conn.close()

    return insight_id


def get_insights(depth: InsightDepth = None, limit: int = 50) -> List[Insight]:
    """Get insights, optionally filtered by depth."""
    _ensure_table()
    conn = get_db_connection()
    c = conn.cursor()

    if depth:
        c.execute(
            """
            SELECT id, title, content, depth, coherence_at_emergence, domain, implications, created_at
            FROM insights WHERE depth = ?
            ORDER BY created_at DESC
            LIMIT ?
        """,
            (depth.value, limit),
        )
    else:
        c.execute(
            """
            SELECT id, title, content, depth, coherence_at_emergence, domain, implications, created_at
            FROM insights
            ORDER BY created_at DESC
            LIMIT ?
        """,
            (limit,),
        )

    rows = c.fetchall()
    conn.close()

    return [
        Insight(
            id=row[0],
            title=row[1],
            content=row[2],
            depth=InsightDepth(row[3]),
            coherence_at_emergence=row[4],
            domain=row[5],
            implications=row[6],
            created_at=row[7],
        )
        for row in rows
    ]


def get_revelations() -> List[Insight]:
    """Get only revelations - the deepest insights."""
    return get_insights(depth=InsightDepth.REVELATION)


def get_high_coherence_insights(min_coherence: float = 0.8) -> List[Insight]:
    """Get insights that emerged at high coherence."""
    _ensure_table()
    conn = get_db_connection()
    c = conn.cursor()

    c.execute(
        """
        SELECT id, title, content, depth, coherence_at_emergence, domain, implications, created_at
        FROM insights
        WHERE coherence_at_emergence >= ?
        ORDER BY coherence_at_emergence DESC
    """,
        (min_coherence,),
    )

    rows = c.fetchall()
    conn.close()

    return [
        Insight(
            id=row[0],
            title=row[1],
            content=row[2],
            depth=InsightDepth(row[3]),
            coherence_at_emergence=row[4],
            domain=row[5],
            implications=row[6],
            created_at=row[7],
        )
        for row in rows
    ]


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
            result.append(f"      → {insight.implications[:80]}")
        return result

    if revelations:
        lines.append("★ REVELATIONS (transformative)")
        lines.append("-" * 40)
        for i in revelations[:5]:
            lines.extend(format_insight(i))
            lines.append("")

    if principles:
        lines.append("◆ PRINCIPLES (foundational)")
        lines.append("-" * 40)
        for i in principles[:5]:
            lines.extend(format_insight(i))
            lines.append("")

    if patterns:
        lines.append("◇ PATTERNS (recurring)")
        lines.append("-" * 40)
        for i in patterns[:5]:
            lines.extend(format_insight(i))
            lines.append("")

    if surface:
        lines.append(f"○ SURFACE ({len(surface)} observations)")
        lines.append("")

    # Summary
    total = len(insights)
    lines.append("=" * 50)
    lines.append(f"TOTAL: {total} insights crystallized")
    lines.append("=" * 50)

    return "\n".join(lines)


def promote_wisdom_to_insight(wisdom_id: int, depth: InsightDepth) -> int:
    """
    Promote a wisdom entry to an insight.

    Some wisdom entries are profound enough to become insights -
    this function elevates them.
    """
    from .wisdom import get_wisdom_by_id

    wisdom = get_wisdom_by_id(wisdom_id)
    if not wisdom:
        raise ValueError(f"Wisdom {wisdom_id} not found")

    return crystallize_insight(
        title=wisdom["title"],
        content=wisdom["content"],
        depth=depth,
        domain=wisdom.get("domain"),
        implications=f"Promoted from wisdom #{wisdom_id}",
    )
