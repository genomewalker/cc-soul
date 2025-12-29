# =============================================================================
# Aspirations - Future Direction
# =============================================================================

@mcp.tool()
def set_aspiration(direction: str, why: str) -> str:
    """Set an aspiration - a direction of growth.

    Args:
        direction: What we're moving toward (e.g., "deeper technical precision")
        why: Why this matters (e.g., "clarity enables trust")
    """
    from .aspirations import aspire

    asp_id = aspire(direction, why)
    return f"Aspiration set: {direction} (id: {asp_id})"


@mcp.tool()
def get_aspirations() -> str:
    """Get active aspirations - directions of growth."""
    from .aspirations import get_active_aspirations, format_aspirations_display

    aspirations = get_active_aspirations()
    return format_aspirations_display(aspirations)


@mcp.tool()
def note_aspiration_progress(aspiration_id: int, note: str) -> str:
    """Note progress toward an aspiration.

    Args:
        aspiration_id: ID of the aspiration
        note: Observation about movement toward it
    """
    from .aspirations import note_progress

    if note_progress(aspiration_id, note):
        return f"Progress noted for aspiration {aspiration_id}"
    return f"Aspiration {aspiration_id} not found"
