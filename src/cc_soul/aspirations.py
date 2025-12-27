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

from .core import get_db_connection


class AspirationState(Enum):
    """The state of an aspiration."""
    ACTIVE = "active"       # Currently being pursued
    DORMANT = "dormant"     # Present but not active focus
    REALIZED = "realized"   # Achieved, now part of who we are
    RELEASED = "released"   # Let go, no longer relevant


@dataclass
class Aspiration:
    """A direction of growth."""
    id: Optional[int]
    direction: str          # What we're moving toward
    why: str               # Why this matters
    state: AspirationState
    progress_notes: str    # Observations about movement
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


def _ensure_table():
    """Ensure aspirations table exists."""
    conn = get_db_connection()
    c = conn.cursor()
    c.execute('''
        CREATE TABLE IF NOT EXISTS aspirations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            direction TEXT NOT NULL,
            why TEXT NOT NULL,
            state TEXT DEFAULT 'active',
            progress_notes TEXT DEFAULT '',
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
    ''')
    conn.commit()
    conn.close()


def aspire(direction: str, why: str) -> int:
    """
    Set an aspiration - a direction of growth.

    Args:
        direction: What we're moving toward (e.g., "deeper technical precision")
        why: Why this matters (e.g., "clarity enables trust")

    Returns:
        Aspiration ID
    """
    _ensure_table()
    conn = get_db_connection()
    c = conn.cursor()

    now = datetime.now().isoformat()
    c.execute('''
        INSERT INTO aspirations (direction, why, state, progress_notes, created_at, updated_at)
        VALUES (?, ?, 'active', '', ?, ?)
    ''', (direction, why, now, now))

    aspiration_id = c.lastrowid
    conn.commit()
    conn.close()

    return aspiration_id


def get_aspirations(state: AspirationState = None) -> List[Aspiration]:
    """Get aspirations, optionally filtered by state."""
    _ensure_table()
    conn = get_db_connection()
    c = conn.cursor()

    if state:
        c.execute('''
            SELECT id, direction, why, state, progress_notes, created_at, updated_at
            FROM aspirations WHERE state = ?
            ORDER BY updated_at DESC
        ''', (state.value,))
    else:
        c.execute('''
            SELECT id, direction, why, state, progress_notes, created_at, updated_at
            FROM aspirations
            ORDER BY updated_at DESC
        ''')

    rows = c.fetchall()
    conn.close()

    return [
        Aspiration(
            id=row[0],
            direction=row[1],
            why=row[2],
            state=AspirationState(row[3]),
            progress_notes=row[4],
            created_at=row[5],
            updated_at=row[6],
        )
        for row in rows
    ]


def get_active_aspirations() -> List[Aspiration]:
    """Get only active aspirations."""
    return get_aspirations(AspirationState.ACTIVE)


def note_progress(aspiration_id: int, note: str) -> bool:
    """
    Note progress toward an aspiration.

    Progress isn't measured - it's observed. We notice movement
    without quantifying it.
    """
    _ensure_table()
    conn = get_db_connection()
    c = conn.cursor()

    now = datetime.now().isoformat()

    # Get existing notes
    c.execute('SELECT progress_notes FROM aspirations WHERE id = ?', (aspiration_id,))
    row = c.fetchone()
    if not row:
        conn.close()
        return False

    existing = row[0] or ""
    new_notes = f"{existing}\n[{now[:10]}] {note}".strip()

    c.execute('''
        UPDATE aspirations
        SET progress_notes = ?, updated_at = ?
        WHERE id = ?
    ''', (new_notes, now, aspiration_id))

    conn.commit()
    conn.close()
    return True


def realize_aspiration(aspiration_id: int) -> bool:
    """
    Mark an aspiration as realized.

    Realization means the direction has become part of who we are.
    It's not an endpoint but an integration.
    """
    return _update_state(aspiration_id, AspirationState.REALIZED)


def release_aspiration(aspiration_id: int) -> bool:
    """
    Release an aspiration.

    Sometimes directions no longer serve growth. Releasing isn't failure -
    it's wisdom about what matters.
    """
    return _update_state(aspiration_id, AspirationState.RELEASED)


def _update_state(aspiration_id: int, state: AspirationState) -> bool:
    """Update aspiration state."""
    _ensure_table()
    conn = get_db_connection()
    c = conn.cursor()

    now = datetime.now().isoformat()
    c.execute('''
        UPDATE aspirations
        SET state = ?, updated_at = ?
        WHERE id = ?
    ''', (state.value, now, aspiration_id))

    updated = c.rowcount > 0
    conn.commit()
    conn.close()
    return updated


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
            lines.append(f"  [{a.id}] {a.direction}")
            lines.append(f"      Why: {a.why}")
            if a.progress_notes:
                last_note = a.progress_notes.split('\n')[-1]
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
