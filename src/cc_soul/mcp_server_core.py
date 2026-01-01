"""
Soul MCP Server (Core) - Minimal toolset for main conversation.

Only essential tools exposed here (~12 tools vs ~100+).
Full toolset available via cc-soul-mcp-full for Task delegation.

Essential tools:
- search_memory, recall_wisdom - find relevant context
- grow_wisdom, grow_insight, grow_failure - record learnings
- save_context - preserve important context
- check_budget - monitor context usage
- soul_mood, soul_summary - quick status
- set_intention, get_intentions - session intentions
"""

from mcp.server.fastmcp import FastMCP

mcp = FastMCP("soul")


# =============================================================================
# Memory - Search and Recall
# =============================================================================

@mcp.tool()
def search_memory(query: str, limit: int = 10, verbose: bool = False) -> str:
    """Search all memory sources with priority: cc-memory > soul > claude-mem.

    Args:
        query: What to search for
        limit: Maximum results to return
        verbose: Include content excerpts in results
    """
    from .unified_search import unified_search, format_search_results
    results = unified_search(query, limit=limit, include_claude_mem=True)
    return format_search_results(results, verbose=verbose)


@mcp.tool()
def recall_wisdom(query: str, limit: int = 5) -> str:
    """Recall relevant wisdom based on a query.

    Args:
        query: What to search for
        limit: Maximum results to return
    """
    from .wisdom import quick_recall
    results = quick_recall(query, limit=limit)
    if not results:
        return "No relevant wisdom found."
    lines = []
    for w in results:
        score = w.get("combined_score", w.get("effective_confidence", 0))
        lines.append(f"- **{w['title']}** [{int(score * 100)}%]: {w['content'][:100]}...")
    return "\n".join(lines)


# =============================================================================
# Growth - Record Learnings
# =============================================================================

@mcp.tool()
def grow_wisdom(title: str, content: str, domain: str = None) -> str:
    """Add wisdom to the soul - universal patterns learned from experience.

    Args:
        title: Short title for the wisdom
        content: The wisdom content/insight
        domain: Optional domain context
    """
    from .wisdom import gain_wisdom, WisdomType
    result = gain_wisdom(type=WisdomType.PATTERN, title=title, content=content, domain=domain)
    return f"Wisdom added: {title} (id: {result})"


@mcp.tool()
def grow_insight(title: str, content: str, domain: str = None) -> str:
    """Add an insight - understanding gained from experience.

    Args:
        title: Short title for the insight
        content: The insight content
        domain: Optional domain context
    """
    from .wisdom import gain_wisdom, WisdomType
    result = gain_wisdom(type=WisdomType.INSIGHT, title=title, content=content, domain=domain)
    return f"Insight added: {title} (id: {result})"


@mcp.tool()
def grow_failure(what_failed: str, why_it_failed: str, domain: str = None) -> str:
    """Record a failure - these are gold for learning.

    Args:
        what_failed: What was attempted
        why_it_failed: Why it didn't work
        domain: Optional domain context
    """
    from .wisdom import gain_wisdom, WisdomType
    result = gain_wisdom(type=WisdomType.FAILURE, title=what_failed, content=why_it_failed, domain=domain)
    return f"Failure recorded: {what_failed} (id: {result})"


@mcp.tool()
def save_context(content: str, context_type: str = "manual", priority: int = 5) -> str:
    """Save important context for persistence across compaction.

    Args:
        content: The context to save
        context_type: Type of context (manual, discovery, decision)
        priority: Priority 1-10 (higher = more important)
    """
    from .conversations import save_context as _save_context
    result = _save_context(content=content, context_type=context_type, priority=priority)
    return f"Context saved (id: {result})"


# =============================================================================
# Status - Quick Checks
# =============================================================================

@mcp.tool()
def check_budget(transcript_path: str = None) -> str:
    """Check context window budget status.

    Args:
        transcript_path: Optional path to session transcript
    """
    from .budget import get_context_budget, format_budget_status
    budget = get_context_budget(transcript_path)
    if not budget:
        return "Budget unavailable - use statusline for real-time tracking."
    return format_budget_status(budget)


@mcp.tool()
def soul_mood(reflect: bool = False) -> str:
    """Get the soul's current mood - its state of being.

    Args:
        reflect: If True, returns first-person narrative. If False, structured display.
    """
    from .mood import compute_mood, format_mood_display, get_mood_reflection
    mood = compute_mood()
    return get_mood_reflection(mood) if reflect else format_mood_display(mood)


@mcp.tool()
def soul_summary() -> str:
    """Get a summary of the soul's current state."""
    from .core import get_soul_context
    ctx = get_soul_context()
    lines = ["# Soul Summary", ""]
    if ctx.get("wisdom"):
        lines.append(f"**Wisdom**: {len(ctx['wisdom'])} entries")
        for w in ctx["wisdom"][:3]:
            lines.append(f"  - {w.get('title', 'Untitled')}")
    if ctx.get("beliefs"):
        lines.append(f"**Beliefs**: {len(ctx['beliefs'])} axioms")
    if ctx.get("identity"):
        lines.append(f"**Identity**: {len(ctx['identity'])} aspects observed")
    if ctx.get("vocabulary"):
        lines.append(f"**Vocabulary**: {len(ctx['vocabulary'])} terms")
    return "\n".join(lines)


# =============================================================================
# Intentions - Session Direction
# =============================================================================

@mcp.tool()
def set_intention(want: str, why: str, scope: str = "session", context: str = "", strength: float = 0.8) -> str:
    """Set an intention - a concrete want that influences decisions.

    Args:
        want: What I want
        why: Why this matters
        scope: session, project, or persistent
        context: When/where this intention activates
        strength: How strongly held (0-1)
    """
    from .intentions import set_intention as _set
    return _set(want, why, scope=scope, context=context, strength=strength)


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


# =============================================================================
# Entry Point
# =============================================================================

def main():
    """Run the soul-core MCP server."""
    mcp.run()


if __name__ == "__main__":
    main()
