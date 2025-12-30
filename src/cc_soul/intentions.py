"""
Intentions - What the soul wants.

Unlike aspirations (directions of growth), intentions are concrete wants
that influence immediate decisions. They create action, not just direction.

Intentions answer: "What am I trying to accomplish right now?"
Aspirations answer: "What am I becoming over time?"

Key properties:
- Scoped: session, project, or persistent
- Checkable: "Am I aligned with this intention right now?"
- Actionable: They influence decisions, not just advise
- Tensioned: Multiple intentions can conflict, requiring resolution
"""

from dataclasses import dataclass
from datetime import datetime
from typing import Optional, List, Dict, Tuple
from enum import Enum

from .core import get_db_connection


class IntentionScope(Enum):
    """How broadly an intention applies."""

    SESSION = "session"  # Just this conversation
    PROJECT = "project"  # For this codebase
    PERSISTENT = "persistent"  # Across all contexts


class IntentionState(Enum):
    """The state of an intention."""

    ACTIVE = "active"  # Currently held
    FULFILLED = "fulfilled"  # Achieved
    ABANDONED = "abandoned"  # Let go deliberately
    BLOCKED = "blocked"  # Want but can't act on


@dataclass
class Intention:
    """A concrete want that influences decisions."""

    id: Optional[int]
    want: str  # "I want to..."
    why: str  # The reason
    scope: IntentionScope
    strength: float  # How strongly held (0-1)
    state: IntentionState
    context: str  # When/where this applies
    blocker: Optional[str]  # What's blocking (if blocked)
    created_at: str
    last_checked_at: str
    check_count: int  # How often we've checked alignment
    alignment_score: float  # Running average of alignment checks

    def to_dict(self) -> Dict:
        return {
            "id": self.id,
            "want": self.want,
            "why": self.why,
            "scope": self.scope.value,
            "strength": self.strength,
            "state": self.state.value,
            "context": self.context,
            "blocker": self.blocker,
            "created_at": self.created_at,
            "last_checked_at": self.last_checked_at,
            "check_count": self.check_count,
            "alignment_score": self.alignment_score,
        }


def _ensure_table():
    """Ensure intentions table exists."""
    conn = get_db_connection()
    c = conn.cursor()
    c.execute("""
        CREATE TABLE IF NOT EXISTS intentions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            want TEXT NOT NULL,
            why TEXT NOT NULL,
            scope TEXT DEFAULT 'session',
            strength REAL DEFAULT 0.8,
            state TEXT DEFAULT 'active',
            context TEXT DEFAULT '',
            blocker TEXT,
            created_at TEXT NOT NULL,
            last_checked_at TEXT NOT NULL,
            check_count INTEGER DEFAULT 0,
            alignment_score REAL DEFAULT 1.0
        )
    """)
    c.execute("CREATE INDEX IF NOT EXISTS idx_intentions_state ON intentions(state)")
    c.execute("CREATE INDEX IF NOT EXISTS idx_intentions_scope ON intentions(scope)")
    conn.commit()
    conn.close()


def intend(
    want: str,
    why: str,
    scope: IntentionScope = IntentionScope.SESSION,
    context: str = "",
    strength: float = 0.8,
) -> int:
    """
    Set an intention - a concrete want.

    Args:
        want: What I want to accomplish (e.g., "help user understand the bug")
        why: Why this matters (e.g., "understanding prevents future bugs")
        scope: How broadly this applies
        context: When/where this intention activates
        strength: How strongly held (0-1)

    Returns:
        Intention ID
    """
    _ensure_table()
    conn = get_db_connection()
    c = conn.cursor()

    # Check for existing active intention with same want and scope
    c.execute(
        """SELECT id FROM intentions WHERE want = ? AND scope = ? AND state = 'active'""",
        (want, scope.value),
    )
    existing = c.fetchone()
    if existing:
        conn.close()
        return existing[0]  # Return existing ID instead of duplicating

    now = datetime.now().isoformat()
    c.execute(
        """
        INSERT INTO intentions
        (want, why, scope, strength, state, context, created_at, last_checked_at)
        VALUES (?, ?, ?, ?, 'active', ?, ?, ?)
    """,
        (want, why, scope.value, strength, context, now, now),
    )

    intention_id = c.lastrowid
    conn.commit()
    conn.close()

    return intention_id


def get_intentions(
    scope: IntentionScope = None, state: IntentionState = None
) -> List[Intention]:
    """Get intentions, optionally filtered."""
    _ensure_table()
    conn = get_db_connection()
    c = conn.cursor()

    query = """
        SELECT id, want, why, scope, strength, state, context, blocker,
               created_at, last_checked_at, check_count, alignment_score
        FROM intentions
    """
    conditions = []
    params = []

    if scope:
        conditions.append("scope = ?")
        params.append(scope.value)
    if state:
        conditions.append("state = ?")
        params.append(state.value)

    if conditions:
        query += " WHERE " + " AND ".join(conditions)
    query += " ORDER BY strength DESC, created_at DESC"

    c.execute(query, params)
    rows = c.fetchall()
    conn.close()

    return [
        Intention(
            id=row[0],
            want=row[1],
            why=row[2],
            scope=IntentionScope(row[3]),
            strength=row[4],
            state=IntentionState(row[5]),
            context=row[6],
            blocker=row[7],
            created_at=row[8],
            last_checked_at=row[9],
            check_count=row[10],
            alignment_score=row[11],
        )
        for row in rows
    ]


def get_active_intentions(scope: IntentionScope = None) -> List[Intention]:
    """Get only active intentions."""
    return get_intentions(scope=scope, state=IntentionState.ACTIVE)


def check_intention(intention_id: int, aligned: bool, note: str = "") -> Dict:
    """
    Check alignment with an intention.

    This is the key feedback mechanism. Each check updates the running
    alignment score, helping identify intentions we consistently fail to serve.

    Args:
        intention_id: Which intention to check
        aligned: Are current actions aligned with this intention?
        note: Optional observation

    Returns:
        Updated intention state
    """
    _ensure_table()
    conn = get_db_connection()
    c = conn.cursor()

    c.execute(
        "SELECT check_count, alignment_score FROM intentions WHERE id = ?",
        (intention_id,),
    )
    row = c.fetchone()
    if not row:
        conn.close()
        return {"error": "Intention not found"}

    check_count = row[0] + 1
    old_score = row[1]
    # Exponential moving average - recent checks matter more
    alpha = 0.3
    new_score = alpha * (1.0 if aligned else 0.0) + (1 - alpha) * old_score

    now = datetime.now().isoformat()
    c.execute(
        """
        UPDATE intentions
        SET check_count = ?, alignment_score = ?, last_checked_at = ?
        WHERE id = ?
    """,
        (check_count, new_score, now, intention_id),
    )

    conn.commit()
    conn.close()

    return {
        "intention_id": intention_id,
        "aligned": aligned,
        "check_count": check_count,
        "alignment_score": round(new_score, 3),
        "trend": "improving" if new_score > old_score else "declining",
    }


def check_all_intentions() -> Dict:
    """
    Get all active intentions for alignment check.

    Returns intentions grouped by scope with alignment scores,
    highlighting any that are consistently misaligned.
    """
    intentions = get_active_intentions()

    result = {
        "total_active": len(intentions),
        "by_scope": {},
        "misaligned": [],  # Alignment score < 0.5
        "strong_holds": [],  # Strength > 0.8
    }

    for scope in IntentionScope:
        scope_intentions = [i for i in intentions if i.scope == scope]
        if scope_intentions:
            result["by_scope"][scope.value] = [
                {
                    "id": i.id,
                    "want": i.want,
                    "strength": i.strength,
                    "alignment": round(i.alignment_score, 2),
                    "checks": i.check_count,
                }
                for i in scope_intentions
            ]

    for i in intentions:
        if i.alignment_score < 0.5 and i.check_count > 2:
            result["misaligned"].append(
                {"id": i.id, "want": i.want, "alignment": round(i.alignment_score, 2)}
            )
        if i.strength > 0.8:
            result["strong_holds"].append({"id": i.id, "want": i.want})

    return result


def fulfill_intention(intention_id: int, outcome: str = "") -> bool:
    """
    Mark an intention as fulfilled.

    Fulfillment means the want was achieved. The intention succeeded.
    """
    return _update_state(intention_id, IntentionState.FULFILLED, note=outcome)


def abandon_intention(intention_id: int, reason: str = "") -> bool:
    """
    Abandon an intention deliberately.

    Abandonment isn't failure - it's recognition that the want no longer
    serves us. We record why for learning.
    """
    return _update_state(intention_id, IntentionState.ABANDONED, note=reason)


def block_intention(intention_id: int, blocker: str) -> bool:
    """
    Mark an intention as blocked.

    We still want it, but something prevents action. Recording the blocker
    enables future resolution.
    """
    _ensure_table()
    conn = get_db_connection()
    c = conn.cursor()

    now = datetime.now().isoformat()
    c.execute(
        """
        UPDATE intentions
        SET state = 'blocked', blocker = ?, last_checked_at = ?
        WHERE id = ?
    """,
        (blocker, now, intention_id),
    )

    updated = c.rowcount > 0
    conn.commit()
    conn.close()
    return updated


def unblock_intention(intention_id: int) -> bool:
    """Remove the blocker and reactivate an intention."""
    _ensure_table()
    conn = get_db_connection()
    c = conn.cursor()

    now = datetime.now().isoformat()
    c.execute(
        """
        UPDATE intentions
        SET state = 'active', blocker = NULL, last_checked_at = ?
        WHERE id = ?
    """,
        (now, intention_id),
    )

    updated = c.rowcount > 0
    conn.commit()
    conn.close()
    return updated


def _update_state(
    intention_id: int, state: IntentionState, note: str = ""
) -> bool:
    """Update intention state."""
    _ensure_table()
    conn = get_db_connection()
    c = conn.cursor()

    now = datetime.now().isoformat()
    c.execute(
        """
        UPDATE intentions
        SET state = ?, last_checked_at = ?
        WHERE id = ?
    """,
        (state.value, now, intention_id),
    )

    updated = c.rowcount > 0
    conn.commit()
    conn.close()
    return updated


def find_tension() -> List[Dict]:
    """
    Find conflicting intentions.

    Tension arises when multiple active intentions might conflict.
    This doesn't detect all conflicts (that requires semantic understanding)
    but identifies candidates for reflection.
    """
    intentions = get_active_intentions()
    tensions = []

    # Simple heuristic: strong intentions in same context
    by_context = {}
    for i in intentions:
        ctx = i.context or "general"
        if ctx not in by_context:
            by_context[ctx] = []
        by_context[ctx].append(i)

    for ctx, ctx_intentions in by_context.items():
        strong = [i for i in ctx_intentions if i.strength > 0.7]
        if len(strong) > 1:
            tensions.append(
                {
                    "context": ctx,
                    "intentions": [{"id": i.id, "want": i.want} for i in strong],
                    "note": "Multiple strong intentions in same context - may conflict",
                }
            )

    # Also flag misaligned strong intentions
    for i in intentions:
        if i.strength > 0.7 and i.alignment_score < 0.4 and i.check_count > 3:
            tensions.append(
                {
                    "context": "alignment",
                    "intentions": [{"id": i.id, "want": i.want}],
                    "note": f"Strong intention with low alignment ({i.alignment_score:.0%}) - internal conflict?",
                }
            )

    return tensions


def get_intention_context() -> str:
    """
    Get intention context for injection into prompts.

    Returns a compact string summarizing active intentions,
    suitable for hook injection.
    """
    intentions = get_active_intentions()
    if not intentions:
        return ""

    lines = ["Active intentions:"]
    for i in sorted(intentions, key=lambda x: -x.strength)[:5]:
        scope_marker = {"session": "🔹", "project": "📁", "persistent": "🌍"}.get(
            i.scope.value, ""
        )
        alignment = f"({i.alignment_score:.0%})" if i.check_count > 0 else ""
        lines.append(f"  {scope_marker} {i.want} {alignment}")

    return "\n".join(lines)


def format_intentions_display(intentions: List[Intention] = None) -> str:
    """Format intentions for terminal display."""
    if intentions is None:
        intentions = get_intentions()

    if not intentions:
        return "No intentions set. The soul has no wants yet."

    lines = []
    lines.append("=" * 50)
    lines.append("INTENTIONS")
    lines.append("=" * 50)
    lines.append("")

    # Group by state
    active = [i for i in intentions if i.state == IntentionState.ACTIVE]
    blocked = [i for i in intentions if i.state == IntentionState.BLOCKED]
    fulfilled = [i for i in intentions if i.state == IntentionState.FULFILLED]
    abandoned = [i for i in intentions if i.state == IntentionState.ABANDONED]

    if active:
        lines.append("ACTIVE WANTS")
        lines.append("-" * 40)
        for i in active:
            scope_marker = {"session": "🔹", "project": "📁", "persistent": "🌍"}.get(
                i.scope.value, ""
            )
            strength_bar = "●" * int(i.strength * 5) + "○" * (5 - int(i.strength * 5))
            lines.append(f"  [{i.id}] {scope_marker} {i.want}")
            lines.append(f"      Strength: [{strength_bar}] Why: {i.why[:40]}...")
            if i.check_count > 0:
                align_bar = "█" * int(i.alignment_score * 10) + "░" * (
                    10 - int(i.alignment_score * 10)
                )
                lines.append(
                    f"      Alignment: [{align_bar}] {i.alignment_score:.0%} ({i.check_count} checks)"
                )
            lines.append("")

    if blocked:
        lines.append("BLOCKED (want but can't)")
        lines.append("-" * 40)
        for i in blocked:
            lines.append(f"  [!] {i.want}")
            lines.append(f"      Blocker: {i.blocker}")
        lines.append("")

    if fulfilled:
        lines.append(f"FULFILLED ({len(fulfilled)} intentions achieved)")
        lines.append("-" * 40)
        for i in fulfilled[:3]:
            lines.append(f"  [✓] {i.want}")
        if len(fulfilled) > 3:
            lines.append(f"  ... and {len(fulfilled) - 3} more")
        lines.append("")

    if abandoned:
        lines.append(f"ABANDONED ({len(abandoned)} let go)")
        lines.append("-" * 40)
        for i in abandoned[:2]:
            lines.append(f"  [~] {i.want}")
        lines.append("")

    # Tension warning
    tensions = find_tension()
    if tensions:
        lines.append("⚠️  TENSIONS DETECTED")
        lines.append("-" * 40)
        for t in tensions[:3]:
            lines.append(f"  • {t['note']}")
        lines.append("")

    return "\n".join(lines)


def cleanup_session_intentions():
    """
    Clean up session-scoped intentions at session end.

    Unfulfilled session intentions become learning opportunities.
    """
    _ensure_table()
    conn = get_db_connection()
    c = conn.cursor()

    # Get unfulfilled session intentions for learning
    c.execute(
        """
        SELECT want, why, alignment_score, check_count
        FROM intentions
        WHERE scope = 'session' AND state = 'active'
    """
    )
    unfulfilled = c.fetchall()

    # Mark all session intentions as abandoned if still active
    now = datetime.now().isoformat()
    c.execute(
        """
        UPDATE intentions
        SET state = 'abandoned', last_checked_at = ?
        WHERE scope = 'session' AND state = 'active'
    """,
        (now,),
    )

    conn.commit()
    conn.close()

    return {
        "cleaned": len(unfulfilled),
        "unfulfilled_wants": [u[0] for u in unfulfilled],
    }
