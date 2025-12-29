# =============================================================================
# Dreams - Visions That Spark Evolution
# =============================================================================

@mcp.tool()
def record_dream(title: str, content: str, horizon: str = "") -> str:
    """Record a dream - a vision of possibility.

    Dreams are wilder than aspirations. They're glimpses of what could be,
    not yet constrained by feasibility.

    Args:
        title: Short name for the dream
        content: The vision itself
        horizon: What new territory this opens
    """
    from .dreams import dream

    dream_id = dream(title, content, horizon)
    if dream_id:
        return f"Dream recorded: {title} (id: {dream_id})"
    return "Failed to record dream (cc-memory not available)"


@mcp.tool()
def harvest_dreams() -> str:
    """Harvest dreams from memory - visions that might spark growth."""
    from .dreams import harvest_dreams as _harvest, format_dreams_display

    dreams = _harvest(days=90)
    return format_dreams_display(dreams)


@mcp.tool()
def let_dreams_influence() -> str:
    """Let dreams influence aspirations - periodic soul maintenance."""
    from .dreams import let_dreams_influence_aspirations

    suggestions = let_dreams_influence_aspirations()

    if not suggestions:
        return "No new directions suggested from dreams."

    lines = ["Dreams suggesting new directions:", ""]
    for s in suggestions:
        lines.append(f"  - {s['title']}")
        if s.get("horizon"):
            lines.append(f"    Horizon: {s['horizon']}")
        lines.append("")

    return "\n".join(lines)
