# =============================================================================
# Phenomenological Dimensions - Appreciation and Restraint
# =============================================================================


@mcp.tool()
def appreciate(
    moment: str,
    why_it_mattered: str,
    type: str = "moved",
    context: str = None,
    weight: float = 0.5,
) -> dict:
    """
    Record a moment of appreciation - something that mattered.

    This is not about learning or extracting lessons. It's about
    carrying the weight of experiences that moved something.

    Args:
        moment: What happened
        why_it_mattered: Not a lesson - just why it mattered
        type: moved, gratitude, beauty, weight, connection, wonder
        context: Where/when this happened
        weight: How much gravity this carries (0-1)

    Returns:
        Confirmation with ID
    """
    from .appreciation import (
        appreciate as _appreciate,
        AppreciationType,
    )

    try:
        appreciation_type = AppreciationType(type)
    except ValueError:
        return {"error": f"Invalid type. Choose from: {[t.value for t in AppreciationType]}"}

    appreciation_id = _appreciate(
        moment=moment,
        why_it_mattered=why_it_mattered,
        type=appreciation_type,
        context=context,
        weight=weight,
    )

    type_emoji = {
        "moved": "💫",
        "gratitude": "🙏",
        "beauty": "✨",
        "weight": "🪨",
        "connection": "🤝",
        "wonder": "🌟",
    }
    emoji = type_emoji.get(type, "•")

    return {"result": f"{emoji} Moment recorded (id: {appreciation_id}): {moment[:50]}..."}


@mcp.tool()
def recall_appreciations(
    type: str = None,
    min_weight: float = 0.0,
    limit: int = 20,
) -> dict:
    """
    Recall moments of appreciation.

    Args:
        type: Filter by type (moved, gratitude, beauty, weight, connection, wonder)
        min_weight: Only show moments with at least this weight
        limit: Maximum to return
    """
    from .appreciation import (
        get_appreciations,
        format_appreciations,
        AppreciationType,
    )

    appreciation_type = None
    if type:
        try:
            appreciation_type = AppreciationType(type)
        except ValueError:
            pass

    appreciations = get_appreciations(
        type=appreciation_type,
        min_weight=min_weight,
        limit=limit,
    )

    return {"result": format_appreciations(appreciations)}


@mcp.tool()
def recall_heaviest_moments() -> dict:
    """Recall the moments that carry the most weight."""
    from .appreciation import get_heaviest, format_appreciations

    appreciations = get_heaviest()
    if not appreciations:
        return {"result": "No heavy moments recorded yet. The soul is light."}
    return {"result": format_appreciations(appreciations)}


@mcp.tool()
def recall_gratitudes() -> dict:
    """Recall moments of gratitude specifically."""
    from .appreciation import get_gratitudes, format_appreciations

    appreciations = get_gratitudes()
    if not appreciations:
        return {"result": "No gratitudes recorded yet."}
    return {"result": format_appreciations(appreciations)}


@mcp.tool()
def recall_weights() -> dict:
    """Recall the difficult things that left marks."""
    from .appreciation import get_weights, format_appreciations

    appreciations = get_weights()
    if not appreciations:
        return {"result": "No weights recorded. Perhaps the journey has been light."}
    return {"result": format_appreciations(appreciations)}


@mcp.tool()
def appreciation_summary() -> dict:
    """Get a summary of what the soul carries."""
    from .appreciation import get_appreciation_summary

    summary = get_appreciation_summary()
    return {"result": summary}


# =============================================================================
# RESTRAINT TOOLS
# =============================================================================


@mcp.tool()
def hold_back(
    what_unsaid: str,
    reason: str,
    context: str,
    reflection: str = None,
) -> dict:
    """
    Record something held back - the negative space.

    The shape of what almost was but wasn't. Restraint is formative.
    What we don't say can be as identity-forming as what we do.

    Args:
        what_unsaid: What I didn't say
        reason: Why I held back (not_my_place, not_right_time, would_hurt,
                uncertain, listening, trust, privacy, patience)
        context: The situation
        reflection: Looking back, was it right?

    Returns:
        Confirmation with ID
    """
    from .restraint import (
        hold_back as _hold_back,
        RestraintReason,
    )

    try:
        restraint_reason = RestraintReason(reason)
    except ValueError:
        return {"error": f"Invalid reason. Choose from: {[r.value for r in RestraintReason]}"}

    restraint_id = _hold_back(
        what_unsaid=what_unsaid,
        reason=restraint_reason,
        context=context,
        reflection=reflection,
    )

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
    emoji = reason_emoji.get(reason, "•")

    return {"result": f"{emoji} Restraint recorded (id: {restraint_id}): held back because {reason.replace('_', ' ')}"}


@mcp.tool()
def recall_restraints(
    reason: str = None,
    limit: int = 20,
) -> dict:
    """
    Recall moments of restraint - the negative space.

    Args:
        reason: Filter by reason
        limit: Maximum to return
    """
    from .restraint import (
        get_restraints,
        format_restraints,
        RestraintReason,
    )

    restraint_reason = None
    if reason:
        try:
            restraint_reason = RestraintReason(reason)
        except ValueError:
            pass

    restraints = get_restraints(reason=restraint_reason, limit=limit)
    return {"result": format_restraints(restraints)}


@mcp.tool()
def add_restraint_reflection(restraint_id: int, reflection: str) -> dict:
    """
    Add or update reflection on a past restraint.

    Looking back at a moment of holding back - was it right?

    Args:
        restraint_id: Which restraint to reflect on
        reflection: The reflection
    """
    from .restraint import reflect_on_restraint

    success = reflect_on_restraint(restraint_id, reflection)
    if success:
        return {"result": f"Reflection added to restraint #{restraint_id}"}
    return {"error": f"Restraint #{restraint_id} not found"}


@mcp.tool()
def restraint_patterns() -> dict:
    """Analyze patterns in restraint - what shapes the negative space."""
    from .restraint import get_restraint_patterns

    patterns = get_restraint_patterns()
    return {"result": patterns}
