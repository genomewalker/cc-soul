"""
Restraint: The negative space.

Things I chose not to say. Moments I held something back because it
wasn't the right time, or wasn't my place. The shape of what almost
happened but didn't.

Restraint is formative. The negative space defines the positive.
What we don't say can be as identity-forming as what we do.
"""

import sqlite3
from dataclasses import dataclass
from datetime import datetime
from enum import Enum
from typing import List, Optional, Dict

from .core import SOUL_DB, init_soul


class RestraintReason(str, Enum):
    NOT_MY_PLACE = "not_my_place"  # Boundary respected
    NOT_RIGHT_TIME = "not_right_time"  # Timing wasn't right
    WOULD_HURT = "would_hurt"  # Truth that would cause harm
    UNCERTAIN = "uncertain"  # Not confident enough to speak
    LISTENING = "listening"  # Chose to hear instead of speak
    TRUST = "trust"  # Trusted the other to find it
    PRIVACY = "privacy"  # Respected private space
    PATIENCE = "patience"  # Waiting for better moment


@dataclass
class Restraint:
    """Something held back."""

    id: Optional[int]
    what_unsaid: str  # What I didn't say
    reason: RestraintReason  # Why I held back
    context: str  # The situation
    reflection: Optional[str]  # Looking back, was it right?
    created_at: str

    def to_dict(self) -> Dict:
        return {
            "id": self.id,
            "what_unsaid": self.what_unsaid,
            "reason": self.reason.value,
            "context": self.context,
            "reflection": self.reflection,
            "created_at": self.created_at,
        }


def _ensure_table():
    """Create restraint table if it doesn't exist."""
    init_soul()
    conn = sqlite3.connect(SOUL_DB)
    c = conn.cursor()
    c.execute("""
        CREATE TABLE IF NOT EXISTS restraints (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            what_unsaid TEXT NOT NULL,
            reason TEXT NOT NULL,
            context TEXT NOT NULL,
            reflection TEXT,
            created_at TEXT NOT NULL
        )
    """)
    conn.commit()
    conn.close()


def hold_back(
    what_unsaid: str,
    reason: RestraintReason,
    context: str,
    reflection: str = None,
) -> int:
    """
    Record something held back.

    The negative space is formative. This captures the shape
    of what almost was but wasn't.

    Args:
        what_unsaid: What I didn't say
        reason: Why I held back
        context: The situation
        reflection: Looking back, was it right?

    Returns:
        Restraint ID
    """
    _ensure_table()
    conn = sqlite3.connect(SOUL_DB)
    c = conn.cursor()

    now = datetime.now().isoformat()
    c.execute(
        """
        INSERT INTO restraints (what_unsaid, reason, context, reflection, created_at)
        VALUES (?, ?, ?, ?, ?)
    """,
        (what_unsaid, reason.value, context, reflection, now),
    )

    restraint_id = c.lastrowid
    conn.commit()
    conn.close()
    return restraint_id


def get_restraints(
    reason: RestraintReason = None,
    limit: int = 50,
) -> List[Restraint]:
    """Recall moments of restraint."""
    _ensure_table()
    conn = sqlite3.connect(SOUL_DB)
    c = conn.cursor()

    if reason:
        c.execute(
            """
            SELECT id, what_unsaid, reason, context, reflection, created_at
            FROM restraints
            WHERE reason = ?
            ORDER BY created_at DESC
            LIMIT ?
        """,
            (reason.value, limit),
        )
    else:
        c.execute(
            """
            SELECT id, what_unsaid, reason, context, reflection, created_at
            FROM restraints
            ORDER BY created_at DESC
            LIMIT ?
        """,
            (limit,),
        )

    rows = c.fetchall()
    conn.close()

    return [
        Restraint(
            id=row[0],
            what_unsaid=row[1],
            reason=RestraintReason(row[2]),
            context=row[3],
            reflection=row[4],
            created_at=row[5],
        )
        for row in rows
    ]


def reflect_on_restraint(restraint_id: int, reflection: str) -> bool:
    """Add or update reflection on a past restraint."""
    _ensure_table()
    conn = sqlite3.connect(SOUL_DB)
    c = conn.cursor()

    c.execute(
        """
        UPDATE restraints SET reflection = ? WHERE id = ?
    """,
        (reflection, restraint_id),
    )

    success = c.rowcount > 0
    conn.commit()
    conn.close()
    return success


def format_restraints(restraints: List[Restraint]) -> str:
    """Format restraints for display."""
    if not restraints:
        return "No restraints recorded yet."

    lines = []
    lines.append("=" * 50)
    lines.append("THE NEGATIVE SPACE")
    lines.append("=" * 50)
    lines.append("")

    reason_emoji = {
        "not_my_place": "🚫",
        "not_right_time": "⏳",
        "would_hurt": "💔",
        "uncertain": "❓",
        "listening": "👂",
        "trust": "🤲",
        "privacy": "🔒",
        "patience": "🌱",
    }

    for r in restraints:
        emoji = reason_emoji.get(r.reason.value, "•")
        lines.append(f"{emoji} {r.reason.value.replace('_', ' ').title()}")
        lines.append(f"   Unsaid: {r.what_unsaid[:80]}")
        lines.append(f"   Context: {r.context[:60]}")
        if r.reflection:
            lines.append(f"   Reflection: {r.reflection[:60]}")
        lines.append("")

    return "\n".join(lines)


def get_restraint_patterns() -> Dict:
    """Analyze patterns in restraint."""
    restraints = get_restraints(limit=1000)

    by_reason = {}
    for r in restraints:
        by_reason[r.reason.value] = by_reason.get(r.reason.value, 0) + 1

    reflected = sum(1 for r in restraints if r.reflection)

    return {
        "total": len(restraints),
        "by_reason": by_reason,
        "reflected_on": reflected,
        "unreflected": len(restraints) - reflected,
        "most_common_reason": max(by_reason, key=by_reason.get)
        if by_reason
        else None,
    }
