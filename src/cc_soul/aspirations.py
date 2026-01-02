"""
Aspirations - What the soul is becoming.

The future dimension of temporal consciousness. Not goals (external targets)
but directions of growth (internal evolution).

Aspirations pull the present forward. They're not destinations but vectors -
ongoing movements toward greater coherence, capability, understanding.
"""

from dataclasses import dataclass
from datetime import datetime
from typing import Optional, List, Dict
from enum import Enum

from .core import get_synapse_graph, save_synapse, SOUL_DIR


class AspirationState(Enum):
    """The state of an aspiration."""

    ACTIVE = "active"  # Currently being pursued
    DORMANT = "dormant"  # Present but not active focus
    REALIZED = "realized"  # Achieved, now part of who we are
    RELEASED = "released"  # Let go, no longer relevant


@dataclass
class Aspiration:
    """A direction of growth."""

    id: Optional[str]
    direction: str  # What we're moving toward
    why: str  # Why this matters
    state: AspirationState
    progress_notes: str  # Observations about movement
    created_at: str
    updated_at: str

    def to_dict(self) -> Dict:
        return {
            "id": self.id,
            "direction": self.direction,
            "why": self.why,
            "state": self.state.value,
            "progress_notes": self.progress_notes,
            "created_at": self.created_at,
            "updated_at": self.updated_at,
        }


def aspire(direction: str, why: str) -> str:
    """
    Set an aspiration - a direction of growth.

    Args:
        direction: What we're moving toward (e.g., "deeper technical precision")
        why: Why this matters (e.g., "clarity enables trust")

    Returns:
        Aspiration ID
    """
    graph = get_synapse_graph()

    now = datetime.now().isoformat()
    aspiration_id = graph.observe(
        category="aspiration",
        title=direction,
        content=f"why:{why} state:active created:{now}",
        tags=["aspiration", "active"],
    )

    save_synapse()
    return aspiration_id


def get_aspirations(state: AspirationState = None) -> List[Aspiration]:
    """Get aspirations, optionally filtered by state."""
    graph = get_synapse_graph()

    episodes = graph.get_episodes(category="aspiration", limit=100)

    # Get state updates
    state_updates = {}
    state_episodes = graph.get_episodes(category="aspiration_state", limit=200)
    for ep in state_episodes:
        asp_id = ep.get("tags", [None])[0] if ep.get("tags") else None
        if asp_id:
            content = ep.get("content", "")
            if "state:realized" in content:
                state_updates[asp_id] = "realized"
            elif "state:released" in content:
                state_updates[asp_id] = "released"
            elif "state:dormant" in content:
                state_updates[asp_id] = "dormant"
            elif "state:active" in content:
                state_updates[asp_id] = "active"

    # Get progress notes
    progress_notes = {}
    progress_episodes = graph.get_episodes(category="aspiration_progress", limit=500)
    for ep in progress_episodes:
        asp_id = ep.get("tags", [None])[0] if ep.get("tags") else None
        if asp_id:
            if asp_id not in progress_notes:
                progress_notes[asp_id] = []
            note = ep.get("content", "")
            timestamp = ep.get("created_at", "")[:10]
            progress_notes[asp_id].append(f"[{timestamp}] {note}")

    aspirations = []
    for ep in episodes:
        asp_id = ep.get("id")
        direction = ep.get("title", "")
        content = ep.get("content", "")

        # Parse why from content
        why = ""
        if "why:" in content:
            why_start = content.index("why:") + 4
            why_end = content.find(" state:", why_start)
            if why_end == -1:
                why_end = len(content)
            why = content[why_start:why_end].strip()

        # Determine current state
        current_state = state_updates.get(asp_id, "active")
        if state and current_state != state.value:
            continue

        # Combine progress notes
        notes = "\n".join(progress_notes.get(asp_id, []))

        aspirations.append(Aspiration(
            id=asp_id,
            direction=direction,
            why=why,
            state=AspirationState(current_state),
            progress_notes=notes,
            created_at=ep.get("created_at", ""),
            updated_at=ep.get("created_at", ""),
        ))

    # Sort by most recent
    aspirations.sort(key=lambda a: a.created_at, reverse=True)
    return aspirations


def get_active_aspirations() -> List[Aspiration]:
    """Get only active aspirations."""
    return get_aspirations(AspirationState.ACTIVE)


def note_progress(aspiration_id: str, note: str) -> bool:
    """
    Note progress toward an aspiration.

    Progress isn't measured - it's observed. We notice movement
    without quantifying it.
    """
    graph = get_synapse_graph()

    graph.observe(
        category="aspiration_progress",
        title="Progress noted",
        content=note,
        tags=[aspiration_id],
    )

    # Strengthen the aspiration
    graph.strengthen(aspiration_id)

    save_synapse()
    return True


def realize_aspiration(aspiration_id: str) -> bool:
    """
    Mark an aspiration as realized.

    Realization means the direction has become part of who we are.
    It's not an endpoint but an integration.
    """
    return _update_state(aspiration_id, AspirationState.REALIZED)


def release_aspiration(aspiration_id: str) -> bool:
    """
    Release an aspiration.

    Sometimes directions no longer serve growth. Releasing isn't failure -
    it's wisdom about what matters.
    """
    return _update_state(aspiration_id, AspirationState.RELEASED)


def _update_state(aspiration_id: str, state: AspirationState) -> bool:
    """Update aspiration state."""
    graph = get_synapse_graph()

    graph.observe(
        category="aspiration_state",
        title=f"Aspiration {state.value}",
        content=f"state:{state.value}",
        tags=[aspiration_id],
    )

    save_synapse()
    return True


def get_aspiration_summary() -> Dict:
    """Get a summary of aspirational state."""
    aspirations = get_aspirations()

    by_state = {
        "active": [],
        "dormant": [],
        "realized": [],
        "released": [],
    }

    for a in aspirations:
        by_state[a.state.value].append(a.direction)

    return {
        "total": len(aspirations),
        "active": len(by_state["active"]),
        "realized": len(by_state["realized"]),
        "active_directions": by_state["active"][:5],
        "realized_directions": by_state["realized"][:5],
    }


def format_aspirations_display(aspirations: List[Aspiration]) -> str:
    """Format aspirations for terminal display."""
    if not aspirations:
        return "No aspirations set. The soul has no direction yet."

    lines = []
    lines.append("=" * 50)
    lines.append("ASPIRATIONS")
    lines.append("=" * 50)
    lines.append("")

    # Group by state
    active = [a for a in aspirations if a.state == AspirationState.ACTIVE]
    realized = [a for a in aspirations if a.state == AspirationState.REALIZED]
    dormant = [a for a in aspirations if a.state == AspirationState.DORMANT]

    if active:
        lines.append("ACTIVE DIRECTIONS")
        lines.append("-" * 40)
        for a in active:
            lines.append(f"  [{a.id[:8] if a.id else '?'}] {a.direction}")
            lines.append(f"      Why: {a.why}")
            if a.progress_notes:
                last_note = a.progress_notes.split("\n")[-1]
                lines.append(f"      Last: {last_note}")
            lines.append("")

    if realized:
        lines.append("REALIZED (now part of who we are)")
        lines.append("-" * 40)
        for a in realized:
            lines.append(f"  [+] {a.direction}")
        lines.append("")

    if dormant:
        lines.append("DORMANT (present but not active)")
        lines.append("-" * 40)
        for a in dormant:
            lines.append(f"  [~] {a.direction}")
        lines.append("")

    return "\n".join(lines)
