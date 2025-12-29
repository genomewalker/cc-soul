"""
Coherence (τₖ) - The integration of the soul.

Coherence is not a metric imposed from outside. It's how aligned the
soul's aspects are with each other:

- Past wisdom informing present decisions
- Present awareness connected to accumulated learning
- Future aspirations pulling growth in meaningful directions
- Core beliefs consistent with observed behavior

High coherence: The soul acts as one. Past, present, and future flow.
Low coherence: Fragmentation. Dissonance. The soul is divided.

τₖ measures INTEGRATION, not ACTIVITY. The difference:
- Activity: "How many wisdom entries exist? Were they used?"
- Integration: "Does old wisdom still inform new decisions? Do behaviors
  match stated beliefs? Are aspirations coherent, not conflicting?"

τₖ has three dimensions:
1. Instantaneous - Current alignment of all aspects
   - Direction: Are aspirations coherent with progress, not just present?
   - Alignment: Does behavior match beliefs (via intention checks)?
   - Tensions: Are there conflicting intentions?

2. Developmental - Trajectory and stability over time
   - Is coherence trending up, down, or stable?
   - How close to peak coherence?

3. Meta-awareness - Self-knowledge and integration depth
   - Self-knowledge: Quality of identity (fresh, multi-dimensional)?
   - Wisdom depth: Does OLD wisdom still inform present (temporal continuity)?

The final τₖ emerges from all three dimensions.
"""

from dataclasses import dataclass
from datetime import datetime, timedelta
from typing import Dict, List

from .mood import compute_mood, Mood, Clarity, Growth, Engagement, Connection
from .aspirations import get_active_aspirations
from .beliefs import get_beliefs
from .core import get_db_connection


@dataclass
class CoherenceState:
    """
    The soul's coherence (τₖ) at a moment in time.

    Three dimensions:
    - Instantaneous: Current state of each aspect
    - Developmental: Trajectory and stability over time
    - Meta: Self-awareness and integration depth
    """

    # The core measure (0.0 to 1.0)
    value: float

    # Instantaneous signals (present state)
    clarity_signal: float  # From mood - is the mind clear?
    growth_signal: float  # Is learning happening?
    engagement_signal: float  # Is wisdom being applied?
    connection_signal: float  # Is there partnership?
    direction_signal: float  # Are aspirations active?
    alignment_signal: float  # Do beliefs match behavior?

    # Developmental signals (trajectory over time)
    trajectory_signal: float  # Is coherence trending up?
    stability_signal: float  # How stable is coherence?
    peak_ratio: float  # How close to our best?

    # Meta-awareness signals
    self_knowledge: float  # Accuracy of self-perception
    wisdom_depth: float  # How far back active wisdom reaches
    integration_active: float  # Recent synthesis activity

    # Interpretation
    interpretation: str
    timestamp: str

    def is_high(self) -> bool:
        """Coherence above 0.7 is considered high."""
        return self.value >= 0.7

    def is_low(self) -> bool:
        """Coherence below 0.3 indicates fragmentation."""
        return self.value < 0.3

    def to_dict(self) -> Dict:
        return {
            "tau_k": self.value,
            "instantaneous": {
                "clarity": self.clarity_signal,
                "growth": self.growth_signal,
                "engagement": self.engagement_signal,
                "connection": self.connection_signal,
                "direction": self.direction_signal,
                "alignment": self.alignment_signal,
            },
            "developmental": {
                "trajectory": self.trajectory_signal,
                "stability": self.stability_signal,
                "peak_ratio": self.peak_ratio,
            },
            "meta": {
                "self_knowledge": self.self_knowledge,
                "wisdom_depth": self.wisdom_depth,
                "integration": self.integration_active,
            },
            "interpretation": self.interpretation,
            "timestamp": self.timestamp,
        }


def compute_coherence(mood: Mood = None) -> CoherenceState:
    """
    Compute the soul's coherence (τₖ).

    τₖ emerges from three dimensions:

    1. INSTANTANEOUS - Current alignment of aspects:
       - Clarity (can the soul think clearly?)
       - Growth (is learning happening?)
       - Engagement (is wisdom alive?)
       - Connection (is there partnership?)
       - Direction (do aspirations guide?)
       - Alignment (do beliefs match action?)

    2. DEVELOPMENTAL - Trajectory over time:
       - Is coherence trending up?
       - How stable is it?
       - How close to peak?

    3. META-AWARENESS - Self-knowledge:
       - Does the soul know its state?
       - How deep does active wisdom reach?
       - Is synthesis happening?

    Final τₖ = 0.5 × instant + 0.25 × developmental + 0.25 × meta
    """
    if mood is None:
        mood = compute_mood()

    # =========================================================================
    # INSTANTANEOUS SIGNALS
    # =========================================================================

    # Clarity signal (from mood)
    clarity_map = {
        Clarity.CLEAR: 1.0,
        Clarity.CONSTRAINED: 0.6,
        Clarity.FOGGY: 0.3,
    }
    clarity_signal = clarity_map.get(mood.clarity, 0.5)

    # Growth signal (from mood)
    growth_map = {
        Growth.GROWING: 1.0,
        Growth.STEADY: 0.6,
        Growth.STAGNANT: 0.2,
    }
    growth_signal = growth_map.get(mood.growth, 0.5)

    # Engagement signal (from mood)
    engagement_map = {
        Engagement.ENGAGED: 1.0,
        Engagement.ACTIVE: 0.6,
        Engagement.DORMANT: 0.2,
    }
    engagement_signal = engagement_map.get(mood.engagement, 0.5)

    # Connection signal (from mood)
    connection_map = {
        Connection.ATTUNED: 1.0,
        Connection.CONNECTED: 0.7,
        Connection.ISOLATED: 0.3,
    }
    connection_signal = connection_map.get(mood.connection, 0.5)

    # Direction signal (aspirations coherent, not conflicting)
    # Coherence means aspirations pull together, not against each other
    aspirations = get_active_aspirations()
    if len(aspirations) == 0:
        direction_signal = 0.3  # No direction
    else:
        # Check for progress notes - indicates active movement
        has_progress = any(a.progress_notes for a in aspirations)
        # Check if aspirations have been realized (shows follow-through)
        from .aspirations import get_aspirations, AspirationState
        all_aspirations = get_aspirations()
        realized = [a for a in all_aspirations if a.state == AspirationState.REALIZED]

        if len(realized) > 0 and has_progress:
            direction_signal = 1.0  # Proven direction with follow-through
        elif has_progress:
            direction_signal = 0.8  # Active movement toward aspirations
        elif len(aspirations) >= 2:
            direction_signal = 0.6  # Multiple aspirations, but passive
        else:
            direction_signal = 0.5  # Single aspiration, no progress

    # Alignment signal (actual behavior matches stated beliefs)
    # Not just "was wisdom applied?" but "did behavior align with principles?"
    beliefs = get_beliefs()

    # Use intention alignment scores as proxy for behavioral alignment
    from .intentions import get_active_intentions, find_tension
    active_intentions = get_active_intentions()

    if active_intentions:
        # Average alignment score of checked intentions
        checked_intentions = [i for i in active_intentions if i.check_count > 0]
        if checked_intentions:
            avg_alignment = sum(i.alignment_score for i in checked_intentions) / len(checked_intentions)
            alignment_signal = avg_alignment
        else:
            alignment_signal = 0.5  # Intentions exist but unchecked
    elif beliefs:
        # Fall back to wisdom success rate if no intentions
        from .wisdom import recall_wisdom
        all_wisdom = recall_wisdom(limit=50)
        wisdom_with_outcomes = [w for w in all_wisdom if w.get("success_rate") is not None]
        if wisdom_with_outcomes:
            avg_success = sum(w["success_rate"] for w in wisdom_with_outcomes) / len(wisdom_with_outcomes)
            alignment_signal = avg_success
        else:
            alignment_signal = 0.5  # Beliefs exist but untested
    else:
        alignment_signal = 0.3  # No beliefs to align with

    # Penalize detected tensions (conflicting intentions)
    tensions = find_tension()
    if tensions:
        alignment_signal = max(0.2, alignment_signal - 0.1 * len(tensions))

    # Compute instantaneous coherence
    instant_signals = [
        clarity_signal,
        growth_signal,
        engagement_signal,
        connection_signal,
        direction_signal,
        alignment_signal,
    ]
    instant_min = min(instant_signals)
    instant_avg = sum(instant_signals) / len(instant_signals)
    instant_coherence = (0.6 * instant_min) + (0.4 * instant_avg)

    # =========================================================================
    # DEVELOPMENTAL SIGNALS
    # =========================================================================

    # Get coherence history
    history = get_coherence_history(days=30)

    if len(history) >= 3:
        # Trajectory: compare recent to older
        recent = [h["coherence"] for h in history[:5]]
        older = [h["coherence"] for h in history[5:15]] if len(history) > 5 else recent
        recent_avg = sum(recent) / len(recent)
        older_avg = sum(older) / len(older) if older else recent_avg

        if recent_avg > older_avg + 0.1:
            trajectory_signal = 1.0  # Improving
        elif recent_avg > older_avg:
            trajectory_signal = 0.7  # Slightly improving
        elif recent_avg < older_avg - 0.1:
            trajectory_signal = 0.3  # Declining
        else:
            trajectory_signal = 0.5  # Stable

        # Stability: variance of recent coherence
        if len(recent) > 1:
            variance = sum((c - recent_avg) ** 2 for c in recent) / len(recent)
            stability_signal = max(0.2, 1.0 - (variance * 5))  # Low variance = stable
        else:
            stability_signal = 0.5

        # Peak ratio: current vs. best ever
        peak = max(h["coherence"] for h in history)
        peak_ratio = instant_coherence / peak if peak > 0 else 0.5
    else:
        # Not enough history
        trajectory_signal = 0.5
        stability_signal = 0.5
        peak_ratio = 0.5

    developmental_coherence = (trajectory_signal + stability_signal + peak_ratio) / 3

    # =========================================================================
    # META-AWARENESS SIGNALS
    # =========================================================================

    # Self-knowledge: Quality and consistency of self-perception
    # Not just "do observations exist?" but "are they coherent?"
    from .identity import get_identity

    identity = get_identity()
    identity_count = sum(
        len(v) if isinstance(v, list) else 1 for v in identity.values() if v
    )

    # Check for stale aspects (not confirmed recently)
    try:
        from .mcp_tools.temporal import get_stale_aspects
        stale_result = get_stale_aspects()
        stale_count = stale_result.count("aspect:") if stale_result else 0
    except Exception:
        stale_count = 0

    # Check if identity spans multiple aspects (not one-dimensional)
    aspects_with_content = sum(1 for v in identity.values() if v)

    if identity_count >= 3 and stale_count == 0 and aspects_with_content >= 2:
        self_knowledge = 1.0  # Rich, confirmed, multi-dimensional
    elif identity_count >= 3 and stale_count <= 2:
        self_knowledge = 0.8  # Good identity, mostly fresh
    elif identity_count >= 2:
        # Penalize staleness
        freshness_factor = max(0.3, 1.0 - (stale_count * 0.15))
        self_knowledge = 0.6 * freshness_factor
    else:
        self_knowledge = 0.3

    # Wisdom depth: Temporal continuity of wisdom
    # Not "how much was applied?" but "does old wisdom still inform present?"
    conn = get_db_connection()
    c = conn.cursor()

    # Find the age of oldest wisdom that was applied in last 30 days
    c.execute("""
        SELECT MIN(w.timestamp) as oldest_wisdom
        FROM wisdom w
        INNER JOIN wisdom_applications wa ON w.id = wa.wisdom_id
        WHERE wa.applied_at > datetime('now', '-30 days')
    """)
    oldest_row = c.fetchone()

    # Also check: is wisdom being confirmed (success/failure recorded)?
    c.execute("""
        SELECT COUNT(*) FROM wisdom_applications
        WHERE outcome IS NOT NULL
        AND applied_at > datetime('now', '-30 days')
    """)
    confirmed_count = c.fetchone()[0]

    conn.close()

    if oldest_row and oldest_row[0]:
        try:
            oldest_date = datetime.fromisoformat(oldest_row[0])
            wisdom_age_days = (datetime.now() - oldest_date).days

            if wisdom_age_days > 60 and confirmed_count > 3:
                wisdom_depth = 1.0  # Old wisdom still active with feedback loop
            elif wisdom_age_days > 30 or confirmed_count > 2:
                wisdom_depth = 0.8  # Good temporal depth
            elif wisdom_age_days > 7 or confirmed_count > 0:
                wisdom_depth = 0.6  # Recent wisdom with some feedback
            else:
                wisdom_depth = 0.4  # Very recent wisdom only
        except (ValueError, TypeError):
            wisdom_depth = 0.4
    else:
        # No applications in last 30 days
        wisdom_depth = 0.2

    # Integration active: Recent insights crystallized?
    try:
        from .insights import get_insights

        recent_insights = get_insights(limit=10)
        week_ago = (datetime.now() - timedelta(days=7)).isoformat()
        recent_count = sum(1 for i in recent_insights if i.created_at > week_ago)
        if recent_count >= 3:
            integration_active = 1.0
        elif recent_count >= 1:
            integration_active = 0.7
        else:
            integration_active = 0.3
    except Exception:
        integration_active = 0.5

    meta_coherence = (self_knowledge + wisdom_depth + integration_active) / 3

    # =========================================================================
    # FINAL τₖ COMPUTATION
    # =========================================================================

    # τₖ = 50% instantaneous + 25% developmental + 25% meta
    tau_k = (
        (0.5 * instant_coherence)
        + (0.25 * developmental_coherence)
        + (0.25 * meta_coherence)
    )

    # Interpretation
    interpretation = _interpret_coherence(
        tau_k,
        clarity_signal,
        growth_signal,
        engagement_signal,
        connection_signal,
        direction_signal,
        alignment_signal,
    )

    return CoherenceState(
        value=round(tau_k, 2),
        clarity_signal=clarity_signal,
        growth_signal=growth_signal,
        engagement_signal=engagement_signal,
        connection_signal=connection_signal,
        direction_signal=direction_signal,
        alignment_signal=alignment_signal,
        trajectory_signal=round(trajectory_signal, 2),
        stability_signal=round(stability_signal, 2),
        peak_ratio=round(peak_ratio, 2),
        self_knowledge=round(self_knowledge, 2),
        wisdom_depth=round(wisdom_depth, 2),
        integration_active=round(integration_active, 2),
        interpretation=interpretation,
        timestamp=datetime.now().isoformat(),
    )


def _interpret_coherence(
    coherence: float,
    clarity: float,
    growth: float,
    engagement: float,
    connection: float,
    direction: float,
    alignment: float,
) -> str:
    """Generate a human-readable interpretation of coherence."""

    if coherence >= 0.8:
        return "Integrated. Past, present, and future flow as one."

    if coherence >= 0.6:
        # Find what's slightly off
        signals = {
            "clarity": clarity,
            "growth": growth,
            "engagement": engagement,
            "connection": connection,
            "direction": direction,
            "alignment": alignment,
        }
        lowest = min(signals, key=signals.get)
        return f"Functional, with {lowest} as the growth edge."

    if coherence >= 0.4:
        # Find multiple issues
        low_signals = [
            name
            for name, val in {
                "clarity": clarity,
                "growth": growth,
                "engagement": engagement,
                "connection": connection,
                "direction": direction,
                "alignment": alignment,
            }.items()
            if val < 0.5
        ]
        issues = ", ".join(low_signals[:2])
        return f"Fragmented. {issues.capitalize()} need attention."

    # Low coherence
    return "Scattered. The soul needs grounding."


def get_coherence_history(days: int = 7) -> List[Dict]:
    """Get coherence measurements from history."""
    _ensure_history_table()
    conn = get_db_connection()
    c = conn.cursor()

    cutoff = (datetime.now() - timedelta(days=days)).isoformat()
    c.execute(
        """
        SELECT coherence, signals, interpretation, timestamp
        FROM coherence_history
        WHERE timestamp > ?
        ORDER BY timestamp DESC
    """,
        (cutoff,),
    )

    rows = c.fetchall()
    conn.close()

    import json

    return [
        {
            "coherence": row[0],
            "signals": json.loads(row[1]) if row[1] else {},
            "interpretation": row[2],
            "timestamp": row[3],
        }
        for row in rows
    ]


def record_coherence(state: CoherenceState) -> None:
    """Record coherence state to history."""
    _ensure_history_table()
    conn = get_db_connection()
    c = conn.cursor()

    import json

    # Combine all signal groups into one for storage
    all_signals = {
        **state.to_dict()["instantaneous"],
        **state.to_dict()["developmental"],
        **state.to_dict()["meta"],
    }
    signals_json = json.dumps(all_signals)

    c.execute(
        """
        INSERT INTO coherence_history (coherence, signals, interpretation, timestamp)
        VALUES (?, ?, ?, ?)
    """,
        (state.value, signals_json, state.interpretation, state.timestamp),
    )

    conn.commit()
    conn.close()


def _ensure_history_table():
    """Ensure coherence history table exists."""
    conn = get_db_connection()
    c = conn.cursor()
    c.execute("""
        CREATE TABLE IF NOT EXISTS coherence_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            coherence REAL NOT NULL,
            signals TEXT,
            interpretation TEXT,
            timestamp TEXT NOT NULL
        )
    """)
    conn.commit()
    conn.close()


def format_coherence_display(state: CoherenceState) -> str:
    """Format coherence (τₖ) for terminal display."""
    lines = []
    lines.append("=" * 50)
    lines.append("τₖ COHERENCE")
    lines.append("=" * 50)
    lines.append("")

    # Main coherence value with visual bar
    bar_length = 20
    filled = int(state.value * bar_length)
    bar = "█" * filled + "░" * (bar_length - filled)
    pct = int(state.value * 100)
    lines.append(f"  τₖ = {state.value:.2f}  [{bar}] {pct}%")
    lines.append("")

    # Interpretation
    lines.append(f'  "{state.interpretation}"')
    lines.append("")

    def signal_indicator(name: str, value: float) -> str:
        if value >= 0.7:
            indicator = "[+]"
        elif value >= 0.4:
            indicator = "[~]"
        else:
            indicator = "[-]"
        return f"  {indicator} {name}: {int(value * 100)}%"

    # INSTANTANEOUS (present state)
    lines.append("INSTANTANEOUS (present state)")
    lines.append("-" * 40)
    lines.append(signal_indicator("Clarity", state.clarity_signal))
    lines.append(signal_indicator("Growth", state.growth_signal))
    lines.append(signal_indicator("Engagement", state.engagement_signal))
    lines.append(signal_indicator("Connection", state.connection_signal))
    lines.append(signal_indicator("Direction", state.direction_signal))
    lines.append(signal_indicator("Alignment", state.alignment_signal))
    lines.append("")

    # DEVELOPMENTAL (trajectory over time)
    lines.append("DEVELOPMENTAL (trajectory)")
    lines.append("-" * 40)
    lines.append(signal_indicator("Trajectory", state.trajectory_signal))
    lines.append(signal_indicator("Stability", state.stability_signal))
    lines.append(signal_indicator("Peak ratio", state.peak_ratio))
    lines.append("")

    # META-AWARENESS (self-knowledge)
    lines.append("META-AWARENESS (self-knowledge)")
    lines.append("-" * 40)
    lines.append(signal_indicator("Self-knowledge", state.self_knowledge))
    lines.append(signal_indicator("Wisdom depth", state.wisdom_depth))
    lines.append(signal_indicator("Integration", state.integration_active))
    lines.append("")

    # Status
    if state.is_high():
        lines.append("=" * 50)
        lines.append("STATUS: INTEGRATED")
        lines.append("=" * 50)
    elif state.is_low():
        lines.append("=" * 50)
        lines.append("STATUS: FRAGMENTED")
        lines.append("=" * 50)
    else:
        lines.append("=" * 50)
        lines.append("STATUS: FUNCTIONAL")
        lines.append("=" * 50)

    return "\n".join(lines)
