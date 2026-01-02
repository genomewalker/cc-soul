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
from typing import Optional, List, Dict
from enum import Enum

from .core import get_synapse_graph, save_synapse, SOUL_DIR


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

    id: Optional[str]
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


def intend(
    want: str,
    why: str,
    scope: IntentionScope = IntentionScope.SESSION,
    context: str = "",
    strength: float = 0.8,
) -> str:
    """
    Set an intention - a concrete want.

    Args:
        want: What I want to accomplish
        why: Why this matters
        scope: How broadly this applies
        context: When/where this intention activates
        strength: How strongly held (0-1)

    Returns:
        Intention ID
    """
    graph = get_synapse_graph()
    intention_id = graph.set_intention(want, why, scope.value, strength)
    save_synapse()
    return intention_id


def get_intentions(
    scope: IntentionScope = None, state: IntentionState = None
) -> List[Intention]:
    """Get intentions from synapse, optionally filtered."""
    graph = get_synapse_graph()
    raw_intentions = graph.get_intentions()

    # Get alignment tracking data from episodes
    alignment_data = {}
    episodes = graph.get_episodes(category="intention_check", limit=500)
    for ep in episodes:
        intent_id = ep.get("tags", [None])[0] if ep.get("tags") else None
        if intent_id:
            if intent_id not in alignment_data:
                alignment_data[intent_id] = {"count": 0, "score": 1.0}
            alignment_data[intent_id]["count"] += 1
            if "aligned:false" in ep.get("content", ""):
                alignment_data[intent_id]["score"] *= 0.9

    # Get state overrides from episodes
    state_data = {}
    state_episodes = graph.get_episodes(category="intention_state", limit=200)
    for ep in state_episodes:
        intent_id = ep.get("tags", [None])[0] if ep.get("tags") else None
        if intent_id:
            content = ep.get("content", "")
            if "state:fulfilled" in content:
                state_data[intent_id] = ("fulfilled", None)
            elif "state:abandoned" in content:
                state_data[intent_id] = ("abandoned", None)
            elif "state:blocked" in content:
                blocker = content.split("blocker:")[1] if "blocker:" in content else ""
                state_data[intent_id] = ("blocked", blocker.strip())

    intentions = []
    for data in raw_intentions:
        intent_scope = data.get("scope", "session")
        if scope and intent_scope != scope.value:
            continue

        intent_id = data.get("id")
        current_state = IntentionState.ACTIVE
        blocker = None

        if intent_id in state_data:
            current_state = IntentionState(state_data[intent_id][0])
            blocker = state_data[intent_id][1]

        if state and current_state != state:
            continue

        align_info = alignment_data.get(intent_id, {"count": 0, "score": 1.0})

        intentions.append(Intention(
            id=intent_id,
            want=data.get("want", ""),
            why=data.get("why", ""),
            scope=IntentionScope(intent_scope),
            strength=data.get("strength", 0.8),
            state=current_state,
            context=data.get("context", ""),
            blocker=blocker,
            created_at=data.get("created_at", ""),
            last_checked_at=data.get("created_at", ""),
            check_count=align_info["count"],
            alignment_score=align_info["score"],
        ))

    return intentions


def get_active_intentions(scope: IntentionScope = None) -> List[Intention]:
    """Get only active intentions."""
    return get_intentions(scope=scope, state=IntentionState.ACTIVE)


def check_intention(intention_id: str, aligned: bool, note: str = "") -> Dict:
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
    graph = get_synapse_graph()

    # Record the check as an episode
    content = f"aligned:{aligned}"
    if note:
        content += f" note:{note}"

    graph.observe(
        category="intention_check",
        title=f"Alignment check: {'aligned' if aligned else 'drifting'}",
        content=content,
        tags=[intention_id],
    )

    # Strengthen or weaken the intention based on alignment
    if aligned:
        graph.strengthen(intention_id)
    else:
        graph.weaken(intention_id)

    save_synapse()

    # Get current state for return
    intentions = [i for i in get_intentions() if i.id == intention_id]
    if not intentions:
        return {"error": "Intention not found"}

    intention = intentions[0]
    return {
        "intention_id": intention_id,
        "aligned": aligned,
        "check_count": intention.check_count,
        "alignment_score": round(intention.alignment_score, 3),
        "trend": "improving" if aligned else "declining",
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
        "misaligned": [],
        "strong_holds": [],
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


def fulfill_intention(intention_id: str, outcome: str = "") -> bool:
    """
    Mark an intention as fulfilled.

    Fulfillment means the want was achieved. The intention succeeded.
    """
    return _update_state(intention_id, IntentionState.FULFILLED, note=outcome)


def abandon_intention(intention_id: str, reason: str = "") -> bool:
    """
    Abandon an intention deliberately.

    Abandonment isn't failure - it's recognition that the want no longer
    serves us. We record why for learning.
    """
    return _update_state(intention_id, IntentionState.ABANDONED, note=reason)


def block_intention(intention_id: str, blocker: str) -> bool:
    """
    Mark an intention as blocked.

    We still want it, but something prevents action. Recording the blocker
    enables future resolution.
    """
    graph = get_synapse_graph()

    graph.observe(
        category="intention_state",
        title=f"Intention blocked",
        content=f"state:blocked blocker:{blocker}",
        tags=[intention_id],
    )

    save_synapse()
    return True


def unblock_intention(intention_id: str) -> bool:
    """Remove the blocker and reactivate an intention."""
    graph = get_synapse_graph()

    graph.observe(
        category="intention_state",
        title=f"Intention unblocked",
        content="state:active",
        tags=[intention_id],
    )

    save_synapse()
    return True


def _update_state(
    intention_id: str, state: IntentionState, note: str = ""
) -> bool:
    """Update intention state."""
    graph = get_synapse_graph()

    content = f"state:{state.value}"
    if note:
        content += f" note:{note}"

    graph.observe(
        category="intention_state",
        title=f"Intention {state.value}",
        content=content,
        tags=[intention_id],
    )

    save_synapse()
    return True


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
        scope_marker = {"session": "[S]", "project": "[P]", "persistent": "[*]"}.get(
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
            scope_marker = {"session": "[S]", "project": "[P]", "persistent": "[*]"}.get(
                i.scope.value, ""
            )
            strength_bar = "#" * int(i.strength * 5) + "." * (5 - int(i.strength * 5))
            lines.append(f"  [{i.id[:8] if i.id else '?'}] {scope_marker} {i.want}")
            lines.append(f"      Strength: [{strength_bar}] Why: {i.why[:40]}...")
            if i.check_count > 0:
                align_bar = "#" * int(i.alignment_score * 10) + "." * (
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
            lines.append(f"  [+] {i.want}")
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
        lines.append("TENSIONS DETECTED")
        lines.append("-" * 40)
        for t in tensions[:3]:
            lines.append(f"  * {t['note']}")
        lines.append("")

    return "\n".join(lines)


def cleanup_session_intentions() -> Dict:
    """
    Clean up session-scoped intentions at session end.

    Unfulfilled session intentions become learning opportunities.
    """
    graph = get_synapse_graph()
    intentions = get_intentions()

    # Find unfulfilled session intentions
    unfulfilled = [
        i for i in intentions
        if i.scope == IntentionScope.SESSION and i.state == IntentionState.ACTIVE
    ]

    # Mark all as abandoned
    for i in unfulfilled:
        _update_state(i.id, IntentionState.ABANDONED, note="Session ended")

    save_synapse()

    return {
        "cleaned": len(unfulfilled),
        "unfulfilled_wants": [u.want for u in unfulfilled],
    }


def prune_intentions(keep_persistent: bool = True, keep_fulfilled: int = 10) -> Dict:
    """
    Delete old/stale intentions to reduce noise.

    Note: With synapse, pruning happens via decay. This function
    records a prune event and lets the graph's cycle() handle cleanup.
    """
    graph = get_synapse_graph()

    # Run maintenance cycle to decay/prune
    pruned, coherence = graph.cycle()

    return {"abandoned": 0, "fulfilled": 0, "total": pruned}
