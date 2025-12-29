# =============================================================================
# Intentions - Concrete Wants That Influence Decisions
# =============================================================================

@mcp.tool()
def set_intention(
    want: str,
    why: str,
    scope: str = "session",
    context: str = "",
    strength: float = 0.8,
) -> str:
    """Set an intention - a concrete want that influences decisions.

    Unlike aspirations (directions of growth), intentions are specific wants
    that should influence immediate action.

    Args:
        want: What I want (e.g., "help user understand the bug")
        why: Why this matters (e.g., "understanding prevents future bugs")
        scope: How broadly this applies (session, project, persistent)
        context: When/where this intention activates
        strength: How strongly held (0-1)
    """
    from .intentions import intend, IntentionScope

    scope_map = {
        "session": IntentionScope.SESSION,
        "project": IntentionScope.PROJECT,
        "persistent": IntentionScope.PERSISTENT,
    }
    intention_scope = scope_map.get(scope.lower(), IntentionScope.SESSION)

    intention_id = intend(
        want=want, why=why, scope=intention_scope, context=context, strength=strength
    )
    return f"Intention set: {want} (id: {intention_id}, scope: {scope})"


@mcp.tool()
def get_intentions(scope: str = None, active_only: bool = True) -> str:
    """Get current intentions.

    Args:
        scope: Filter by scope (session, project, persistent)
        active_only: If True, only show active intentions
    """
    from .intentions import (
        get_intentions as _get_intentions,
        get_active_intentions,
        format_intentions_display,
        IntentionScope,
        IntentionState,
    )

    scope_filter = None
    if scope:
        scope_map = {
            "session": IntentionScope.SESSION,
            "project": IntentionScope.PROJECT,
            "persistent": IntentionScope.PERSISTENT,
        }
        scope_filter = scope_map.get(scope.lower())

    if active_only:
        intentions = get_active_intentions(scope=scope_filter)
    else:
        intentions = _get_intentions(scope=scope_filter)

    return format_intentions_display(intentions)


@mcp.tool()
def check_intention(intention_id: int, aligned: bool, note: str = "") -> str:
    """Check alignment with an intention.

    This is the key feedback mechanism. Each check updates the running
    alignment score, helping identify intentions we consistently fail to serve.

    Args:
        intention_id: Which intention to check
        aligned: Are current actions aligned with this intention?
        note: Optional observation
    """
    from .intentions import check_intention as _check

    result = _check(intention_id, aligned, note)

    if "error" in result:
        return f"Error: {result['error']}"

    trend = "↑" if result["trend"] == "improving" else "↓"
    return (
        f"Intention {intention_id} {'aligned' if aligned else 'misaligned'}\n"
        f"  Alignment: {result['alignment_score']:.0%} {trend}\n"
        f"  Checks: {result['check_count']}"
    )


@mcp.tool()
def check_all_intentions() -> str:
    """Check alignment status of all active intentions.

    Returns intentions grouped by scope with alignment scores,
    highlighting any that are consistently misaligned.
    """
    from .intentions import check_all_intentions as _check_all

    result = _check_all()

    lines = [f"Active intentions: {result['total_active']}", ""]

    for scope, intentions in result["by_scope"].items():
        scope_icon = {"session": "🔹", "project": "📁", "persistent": "🌍"}.get(
            scope, ""
        )
        lines.append(f"{scope_icon} {scope.upper()}")
        for i in intentions:
            align_bar = "█" * int(i["alignment"] * 5) + "░" * (
                5 - int(i["alignment"] * 5)
            )
            lines.append(f"  [{i['id']}] {i['want'][:40]}...")
            lines.append(f"      [{align_bar}] {i['alignment']:.0%}")
        lines.append("")

    if result["misaligned"]:
        lines.append("⚠️  MISALIGNED (need attention)")
        for m in result["misaligned"]:
            lines.append(f"  [{m['id']}] {m['want']} ({m['alignment']:.0%})")
        lines.append("")

    return "\n".join(lines)


@mcp.tool()
def fulfill_intention(intention_id: int, outcome: str = "") -> str:
    """Mark an intention as fulfilled.

    Args:
        intention_id: Which intention was achieved
        outcome: Optional description of the outcome
    """
    from .intentions import fulfill_intention as _fulfill

    if _fulfill(intention_id, outcome):
        return f"Intention {intention_id} fulfilled! ✓"
    return f"Intention {intention_id} not found"


@mcp.tool()
def abandon_intention(intention_id: int, reason: str = "") -> str:
    """Abandon an intention deliberately.

    Abandonment isn't failure - it's recognition that the want no longer serves.

    Args:
        intention_id: Which intention to release
        reason: Why we're letting go
    """
    from .intentions import abandon_intention as _abandon

    if _abandon(intention_id, reason):
        return f"Intention {intention_id} abandoned"
    return f"Intention {intention_id} not found"


@mcp.tool()
def block_intention(intention_id: int, blocker: str) -> str:
    """Mark an intention as blocked.

    We still want it, but something prevents action.

    Args:
        intention_id: Which intention is blocked
        blocker: What's preventing action
    """
    from .intentions import block_intention as _block

    if _block(intention_id, blocker):
        return f"Intention {intention_id} blocked by: {blocker}"
    return f"Intention {intention_id} not found"


@mcp.tool()
def unblock_intention(intention_id: int) -> str:
    """Remove the blocker and reactivate an intention.

    Args:
        intention_id: Which intention to unblock
    """
    from .intentions import unblock_intention as _unblock

    if _unblock(intention_id):
        return f"Intention {intention_id} unblocked and reactivated"
    return f"Intention {intention_id} not found"


@mcp.tool()
def find_intention_tension() -> str:
    """Find conflicting intentions.

    Tension arises when multiple active intentions might conflict.
    This surfaces candidates for reflection and resolution.
    """
    from .intentions import find_tension

    tensions = find_tension()

    if not tensions:
        return "No tensions detected among active intentions."

    lines = ["Intention tensions detected:", ""]
    for t in tensions:
        lines.append(f"• {t['note']}")
        for i in t["intentions"]:
            lines.append(f"  [{i['id']}] {i['want']}")
        lines.append("")

    return "\n".join(lines)


@mcp.tool()
def get_intention_context() -> str:
    """Get active intentions formatted for context injection.

    Returns a compact summary suitable for hook injection.
    """
    from .intentions import get_intention_context as _get_context

    ctx = _get_context()
    return ctx if ctx else "No active intentions."
