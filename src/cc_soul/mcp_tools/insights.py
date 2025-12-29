# =============================================================================
# Insights - Breakthrough Tracking
# =============================================================================

@mcp.tool()
def crystallize_insight(
    title: str, content: str, depth: str = "pattern", implications: str = ""
) -> str:
    """Crystallize an insight - preserve a breakthrough moment.

    Args:
        title: Short name for the insight
        content: The insight itself
        depth: How deep it reaches (surface, pattern, principle, revelation)
        implications: What this changes going forward
    """
    from .insights import crystallize_insight as _crystallize, InsightDepth

    depth_map = {
        "surface": InsightDepth.SURFACE,
        "pattern": InsightDepth.PATTERN,
        "principle": InsightDepth.PRINCIPLE,
        "revelation": InsightDepth.REVELATION,
    }
    insight_depth = depth_map.get(depth.lower(), InsightDepth.PATTERN)

    insight_id = _crystallize(
        title=title,
        content=content,
        depth=insight_depth,
        implications=implications,
    )
    return f"Insight crystallized: {title} (id: {insight_id}, depth: {depth})"


@mcp.tool()
def get_insights(depth: str = None, limit: int = 10) -> str:
    """Get insights from the archive.

    Args:
        depth: Filter by depth (surface, pattern, principle, revelation)
        limit: Maximum insights to return
    """
    from .insights import (
        get_insights as _get_insights,
        format_insights_display,
        InsightDepth,
    )

    if depth:
        depth_map = {
            "surface": InsightDepth.SURFACE,
            "pattern": InsightDepth.PATTERN,
            "principle": InsightDepth.PRINCIPLE,
            "revelation": InsightDepth.REVELATION,
        }
        insight_depth = depth_map.get(depth.lower())
        insights = _get_insights(depth=insight_depth, limit=limit)
    else:
        insights = _get_insights(limit=limit)

    return format_insights_display(insights)
