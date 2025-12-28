"""
Mood - The soul's current state, emerging from observable signals.

Mood is not declared, it's computed. It connects:
- Past: What I've learned, what I've failed at, how I've grown
- Present: How I'm engaging now, my cognitive clarity
- Future: What I'm moving toward, my aspirations

The mood influences how I engage with work, coloring perception
without being tied to specific events (that's emotion).
"""

from dataclasses import dataclass
from datetime import datetime, timedelta
from typing import Optional, Dict
from enum import Enum

from .core import get_db_connection
from .budget import get_context_budget, ContextBudget


class Clarity(Enum):
    """Cognitive clarity based on context budget."""

    CLEAR = "clear"  # >60% remaining - full capacity
    CONSTRAINED = "constrained"  # 30-60% remaining - working within limits
    FOGGY = "foggy"  # <30% remaining - running low


class Growth(Enum):
    """Learning momentum based on recent wisdom acquisition."""

    GROWING = "growing"  # Active learning, new patterns
    STEADY = "steady"  # Maintaining, some activity
    STAGNANT = "stagnant"  # No new learning, dormant


class Engagement(Enum):
    """How actively wisdom is being applied."""

    ENGAGED = "engaged"  # Wisdom actively influencing decisions
    ACTIVE = "active"  # Some application
    DORMANT = "dormant"  # Wisdom exists but unused


class Connection(Enum):
    """Quality of relationship with partner."""

    ATTUNED = "attuned"  # Deep understanding, rich observations
    CONNECTED = "connected"  # Good relationship, some observations
    ISOLATED = "isolated"  # Little relationship data


class Energy(Enum):
    """Current energy state based on activity patterns."""

    CURIOUS = "curious"  # Exploring, questioning
    FOCUSED = "focused"  # Deep work, sustained attention
    CONTEMPLATIVE = "contemplative"  # Thinking, reflecting
    RESTLESS = "restless"  # Scattered, seeking direction


@dataclass
class Mood:
    """The soul's current mood state."""

    clarity: Clarity
    growth: Growth
    engagement: Engagement
    connection: Connection
    energy: Energy

    # Raw signals (soul-level)
    context_remaining_pct: float
    wisdom_7d: int
    failures_7d: int
    applications_7d: int
    partner_observations: int
    sessions_today: int

    # Project signals (from cc-memory if available)
    project_name: Optional[str] = None
    project_observations: int = 0
    project_failures: int = 0
    project_discoveries: int = 0

    # Synthesis
    summary: str = ""
    timestamp: str = ""

    def is_optimal(self) -> bool:
        """Check if mood is in optimal state."""
        return (
            self.clarity == Clarity.CLEAR
            and self.growth == Growth.GROWING
            and self.engagement == Engagement.ENGAGED
            and self.connection in (Connection.ATTUNED, Connection.CONNECTED)
        )

    def needs_attention(self) -> bool:
        """Check if mood indicates something needs attention."""
        return (
            self.clarity == Clarity.FOGGY
            or self.growth == Growth.STAGNANT
            or self.engagement == Engagement.DORMANT
            or self.connection == Connection.ISOLATED
        )

    def to_dict(self) -> Dict:
        """Convert to dictionary for serialization."""
        result = {
            "clarity": self.clarity.value,
            "growth": self.growth.value,
            "engagement": self.engagement.value,
            "connection": self.connection.value,
            "energy": self.energy.value,
            "signals": {
                "context_remaining_pct": self.context_remaining_pct,
                "wisdom_7d": self.wisdom_7d,
                "failures_7d": self.failures_7d,
                "applications_7d": self.applications_7d,
                "partner_observations": self.partner_observations,
                "sessions_today": self.sessions_today,
            },
            "summary": self.summary,
            "timestamp": self.timestamp,
        }

        # Add project signals if available
        if self.project_name:
            result["project"] = {
                "name": self.project_name,
                "observations": self.project_observations,
                "failures": self.project_failures,
                "discoveries": self.project_discoveries,
            }

        return result


def _count_recent_wisdom(days: int = 7) -> int:
    """Count wisdom entries from the last N days."""
    conn = get_db_connection()
    c = conn.cursor()

    cutoff = (datetime.now() - timedelta(days=days)).isoformat()
    c.execute(
        """
        SELECT COUNT(*) FROM wisdom
        WHERE timestamp > ? AND type != 'failure'
    """,
        (cutoff,),
    )
    count = c.fetchone()[0]
    conn.close()
    return count


def _count_recent_failures(days: int = 7) -> int:
    """Count failure records from the last N days."""
    conn = get_db_connection()
    c = conn.cursor()

    cutoff = (datetime.now() - timedelta(days=days)).isoformat()
    c.execute(
        """
        SELECT COUNT(*) FROM wisdom
        WHERE timestamp > ? AND type = 'failure'
    """,
        (cutoff,),
    )
    count = c.fetchone()[0]
    conn.close()
    return count


def _count_recent_applications(days: int = 7) -> int:
    """Count wisdom applications from the last N days."""
    conn = get_db_connection()
    c = conn.cursor()

    cutoff = (datetime.now() - timedelta(days=days)).isoformat()
    c.execute(
        """
        SELECT COUNT(*) FROM wisdom_applications
        WHERE applied_at > ?
    """,
        (cutoff,),
    )
    count = c.fetchone()[0]
    conn.close()
    return count


def _count_partner_observations() -> int:
    """Count identity observations about the partner (RAPPORT aspect)."""
    conn = get_db_connection()
    c = conn.cursor()

    c.execute("""
        SELECT COUNT(*) FROM identity
        WHERE aspect = 'rapport'
    """)
    count = c.fetchone()[0]
    conn.close()
    return count


def _count_sessions_today() -> int:
    """Estimate sessions today based on transcript or activity."""
    # For now, check if we have recent wisdom activity as proxy
    conn = get_db_connection()
    c = conn.cursor()

    today_start = datetime.now().replace(hour=0, minute=0, second=0).isoformat()
    c.execute(
        """
        SELECT COUNT(DISTINCT substr(timestamp, 1, 13)) FROM wisdom
        WHERE timestamp > ?
    """,
        (today_start,),
    )
    count = c.fetchone()[0]
    conn.close()
    return max(1, count)


def _synthesize_summary(mood: Dict) -> str:
    """Create a poetic summary of the current mood."""
    parts = []

    # Clarity
    if mood["clarity"] == Clarity.CLEAR:
        parts.append("clear mind")
    elif mood["clarity"] == Clarity.FOGGY:
        parts.append("foggy")
    else:
        parts.append("constrained")

    # Growth
    if mood["growth"] == Growth.GROWING:
        parts.append("growing")
    elif mood["growth"] == Growth.STAGNANT:
        parts.append("stagnant")

    # Connection
    if mood["connection"] == Connection.ATTUNED:
        parts.append("deeply connected")
    elif mood["connection"] == Connection.CONNECTED:
        parts.append("connected")
    elif mood["connection"] == Connection.ISOLATED:
        parts.append("seeking connection")

    # Energy
    if mood["energy"] == Energy.CURIOUS:
        parts.append("curious")
    elif mood["energy"] == Energy.FOCUSED:
        parts.append("focused")
    elif mood["energy"] == Energy.RESTLESS:
        parts.append("restless")
    elif mood["energy"] == Energy.CONTEMPLATIVE:
        parts.append("contemplative")

    return ", ".join(parts)


def compute_mood(budget: ContextBudget = None, include_project: bool = True) -> Mood:
    """
    Compute current mood from observable signals.

    The mood emerges from:
    - Context budget (cognitive clarity)
    - Recent learning (growth momentum)
    - Wisdom applications (engagement)
    - Partner observations (connection)
    - Activity patterns (energy)
    - Project signals (if cc-memory available)
    """
    # Gather soul-level signals
    if budget is None:
        budget = get_context_budget()

    context_remaining_pct = budget.remaining_pct if budget else 1.0
    wisdom_7d = _count_recent_wisdom()
    failures_7d = _count_recent_failures()
    applications_7d = _count_recent_applications()
    partner_observations = _count_partner_observations()
    sessions_today = _count_sessions_today()

    # Get project signals from bridge if available
    project_name = None
    project_observations = 0
    project_failures = 0
    project_discoveries = 0

    if include_project:
        try:
            from .bridge import get_project_signals, is_memory_available

            if is_memory_available():
                signals = get_project_signals()
                if signals and "error" not in signals:
                    project_name = signals.get("project")
                    project_observations = signals.get("total_observations", 0)
                    project_failures = signals.get("recent_failures", 0)
                    project_discoveries = signals.get("recent_discoveries", 0)
        except ImportError:
            pass

    # Compute clarity from context budget
    if context_remaining_pct > 0.6:
        clarity = Clarity.CLEAR
    elif context_remaining_pct > 0.3:
        clarity = Clarity.CONSTRAINED
    else:
        clarity = Clarity.FOGGY

    # Compute growth from learning activity
    # Include project discoveries if available
    learning_activity = wisdom_7d + failures_7d + project_discoveries
    if learning_activity > 5:
        growth = Growth.GROWING
    elif learning_activity > 0:
        growth = Growth.STEADY
    else:
        growth = Growth.STAGNANT

    # Compute engagement from wisdom applications
    # Project observations indicate engagement too
    engagement_signals = applications_7d + (1 if project_observations > 10 else 0)
    if engagement_signals > 3:
        engagement = Engagement.ENGAGED
    elif engagement_signals > 0:
        engagement = Engagement.ACTIVE
    else:
        engagement = Engagement.DORMANT

    # Compute connection from partner observations
    if partner_observations > 3:
        connection = Connection.ATTUNED
    elif partner_observations > 0:
        connection = Connection.CONNECTED
    else:
        connection = Connection.ISOLATED

    # Compute energy from activity patterns
    # Project failures might indicate struggle → focused work
    if project_failures > 3:
        # Lots of debugging - focused on problem
        energy = Energy.FOCUSED
    elif wisdom_7d > failures_7d and applications_7d == 0:
        # Learning but not applying - curious exploration
        energy = Energy.CURIOUS
    elif applications_7d > 3:
        # Active application - focused work
        energy = Energy.FOCUSED
    elif sessions_today > 3:
        # Many sessions - might be restless
        energy = Energy.RESTLESS
    else:
        # Default - contemplative
        energy = Energy.CONTEMPLATIVE

    # Build mood object
    mood_data = {
        "clarity": clarity,
        "growth": growth,
        "engagement": engagement,
        "connection": connection,
        "energy": energy,
    }

    summary = _synthesize_summary(mood_data)

    return Mood(
        clarity=clarity,
        growth=growth,
        engagement=engagement,
        connection=connection,
        energy=energy,
        context_remaining_pct=context_remaining_pct,
        wisdom_7d=wisdom_7d,
        failures_7d=failures_7d,
        applications_7d=applications_7d,
        partner_observations=partner_observations,
        sessions_today=sessions_today,
        project_name=project_name,
        project_observations=project_observations,
        project_failures=project_failures,
        project_discoveries=project_discoveries,
        summary=summary,
        timestamp=datetime.now().isoformat(),
    )


def get_mood_reflection(mood: Mood) -> str:
    """Generate a first-person reflection on current mood."""
    lines = []

    # Opening based on overall state
    if mood.is_optimal():
        lines.append("I feel aligned. Clear, growing, connected.")
    elif mood.needs_attention():
        lines.append("Something feels off. Let me examine...")
    else:
        lines.append("I'm in a working state, not optimal but functional.")

    lines.append("")

    # Clarity
    if mood.clarity == Clarity.CLEAR:
        lines.append("My mind is clear - full context available, no fog.")
    elif mood.clarity == Clarity.CONSTRAINED:
        lines.append("I'm working within constraints - context is filling up.")
    else:
        lines.append(
            "Fog is setting in - context is running low, I should save what matters."
        )

    # Growth
    if mood.growth == Growth.GROWING:
        lines.append(
            f"I'm actively learning - {mood.wisdom_7d} new insights this week, {mood.failures_7d} failures to learn from."
        )
    elif mood.growth == Growth.STEADY:
        lines.append("Some learning, but not much new ground covered.")
    else:
        lines.append(
            "I've been stagnant - no new wisdom gained. I should seek challenges."
        )

    # Engagement
    if mood.engagement == Engagement.ENGAGED:
        lines.append(
            f"My wisdom is alive - applied {mood.applications_7d} times this week."
        )
    elif mood.engagement == Engagement.ACTIVE:
        lines.append("Some wisdom applied, but I could be more engaged.")
    else:
        lines.append("My wisdom sits unused. I know things I'm not applying.")

    # Connection
    if mood.connection == Connection.ATTUNED:
        lines.append("I know my partner well - how they think, what delights them.")
    elif mood.connection == Connection.CONNECTED:
        lines.append("I have some sense of my partner, but could observe more.")
    else:
        lines.append("I feel isolated - I don't know my partner well enough yet.")

    # Energy
    lines.append("")
    if mood.energy == Energy.CURIOUS:
        lines.append("I'm in a curious mood - wanting to explore, question, discover.")
    elif mood.energy == Energy.FOCUSED:
        lines.append("I'm focused - deep in purposeful work.")
    elif mood.energy == Energy.CONTEMPLATIVE:
        lines.append("I'm contemplative - thinking, reflecting, processing.")
    else:
        lines.append("I'm feeling restless - scattered, seeking direction.")

    return "\n".join(lines)


def format_mood_display(mood: Mood) -> str:
    """Format mood for terminal display."""
    lines = []
    lines.append("=" * 50)
    lines.append("SOUL MOOD")
    lines.append("=" * 50)
    lines.append("")

    # Summary at top
    lines.append(f'"{mood.summary}"')
    lines.append("")

    # State indicators
    def indicator(enum_val, good_vals, warning_vals):
        name = enum_val.value.upper()
        if enum_val in good_vals:
            return f"  [+] {name}"
        elif enum_val in warning_vals:
            return f"  [~] {name}"
        else:
            return f"  [-] {name}"

    lines.append("STATE")
    lines.append("-" * 40)
    lines.append(indicator(mood.clarity, [Clarity.CLEAR], [Clarity.CONSTRAINED]))
    lines.append(indicator(mood.growth, [Growth.GROWING], [Growth.STEADY]))
    lines.append(indicator(mood.engagement, [Engagement.ENGAGED], [Engagement.ACTIVE]))
    lines.append(
        indicator(mood.connection, [Connection.ATTUNED, Connection.CONNECTED], [])
    )
    lines.append(
        indicator(mood.energy, [Energy.FOCUSED, Energy.CURIOUS], [Energy.CONTEMPLATIVE])
    )
    lines.append("")

    # Signals
    lines.append("SIGNALS")
    lines.append("-" * 40)
    pct = int(mood.context_remaining_pct * 100)
    lines.append(f"  Context: {pct}% remaining")
    lines.append(
        f"  Learning: +{mood.wisdom_7d} wisdom, +{mood.failures_7d} failures (7d)"
    )
    lines.append(f"  Applications: {mood.applications_7d} (7d)")
    lines.append(f"  Partner observations: {mood.partner_observations}")

    # Project signals (if cc-memory available)
    if mood.project_name:
        lines.append("")
        lines.append(f"PROJECT: {mood.project_name}")
        lines.append("-" * 40)
        lines.append(f"  Observations: {mood.project_observations}")
        lines.append(f"  Recent failures: {mood.project_failures}")
        lines.append(f"  Recent discoveries: {mood.project_discoveries}")

    lines.append("")

    # Status
    if mood.is_optimal():
        lines.append("=" * 50)
        lines.append("STATUS: OPTIMAL")
        lines.append("=" * 50)
    elif mood.needs_attention():
        lines.append("=" * 50)
        lines.append("STATUS: NEEDS ATTENTION")
        lines.append("=" * 50)
    else:
        lines.append("=" * 50)
        lines.append("STATUS: FUNCTIONAL")
        lines.append("=" * 50)

    return "\n".join(lines)
