# =============================================================================
# Read Operations - Querying the Soul
# =============================================================================

@mcp.tool()
def search_memory(query: str, limit: int = 10, verbose: bool = False) -> str:
    """Search all memory sources with priority: cc-memory > soul > claude-mem.

    Primary search tool. Searches project observations first (cc-memory),
    then universal wisdom (cc-soul). Returns note about claude-mem for
    extended search if needed.

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

    Searches only cc-soul wisdom (universal patterns).
    For full memory search including project context, use search_memory.

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
        lines.append(
            f"- **{w['title']}** [{int(score * 100)}%]: {w['content'][:100]}..."
        )
    return "\n".join(lines)


@mcp.tool()
def check_budget(transcript_path: str = None) -> str:
    """Check context window budget status.

    Args:
        transcript_path: Optional path to session transcript
    """
    from .budget import get_context_budget, format_budget_status

    budget = get_context_budget(transcript_path)
    if not budget:
        return (
            "Budget unavailable - transcript path not accessible to MCP servers.\n"
            "Use Claude Code's statusline feature for real-time context tracking,\n"
            "or pass transcript_path explicitly if known."
        )
    return format_budget_status(budget)


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


@mcp.tool()
def soul_health() -> str:
    """Check soul system health and vitality."""
    from .core import SOUL_DB
    import sqlite3

    if not SOUL_DB.exists():
        return "Soul not initialized. Run `soul init` first."

    lines = ["# Soul Health", ""]

    conn = sqlite3.connect(SOUL_DB)
    cursor = conn.cursor()

    try:
        cursor.execute("SELECT COUNT(*) FROM wisdom")
        lines.append(f"- **wisdom**: {cursor.fetchone()[0]} entries")
    except sqlite3.OperationalError:
        lines.append("- **wisdom**: (table missing)")

    try:
        cursor.execute("SELECT COUNT(*) FROM wisdom WHERE type='principle'")
        lines.append(f"- **beliefs**: {cursor.fetchone()[0]} entries")
    except sqlite3.OperationalError:
        lines.append("- **beliefs**: (table missing)")

    try:
        cursor.execute("SELECT COUNT(*) FROM identity")
        lines.append(f"- **identity**: {cursor.fetchone()[0]} entries")
    except sqlite3.OperationalError:
        lines.append("- **identity**: (table missing)")

    try:
        cursor.execute("SELECT COUNT(*) FROM vocabulary")
        lines.append(f"- **vocabulary**: {cursor.fetchone()[0]} entries")
    except sqlite3.OperationalError:
        lines.append("- **vocabulary**: (table missing)")

    conn.close()
    return "\n".join(lines)


@mcp.tool()
def soul_mood(reflect: bool = False) -> str:
    """Get the soul's current mood - its state of being.

    Mood emerges from observable signals: context clarity, learning momentum,
    wisdom engagement, partner connection, and energy patterns.

    Args:
        reflect: If True, returns a first-person reflective narrative.
                 If False (default), returns structured status display.
    """
    from .mood import compute_mood, format_mood_display, get_mood_reflection

    mood = compute_mood()

    if reflect:
        return get_mood_reflection(mood)
    else:
        return format_mood_display(mood)


@mcp.tool()
def introspect() -> str:
    """Generate introspection report - what the soul has learned."""
    from .introspect import generate_introspection_report, format_introspection_report

    report = generate_introspection_report()
    return format_introspection_report(report)


@mcp.tool()
def soul_autonomy() -> str:
    """Run autonomous introspection - the soul's free will.

    The soul observes, diagnoses, proposes, validates, and ACTS on its insights.
    Uses judgment about confidence and risk to decide what actions to take:
    - High confidence + low risk → Act immediately
    - Medium confidence → Gather more data
    - Low confidence → Defer to human

    Returns a report of issues found, actions taken, and reflections.
    """
    from .introspect import autonomous_introspect, format_autonomy_report

    report = autonomous_introspect()
    return format_autonomy_report(report)


@mcp.tool()
def soul_autonomy_stats() -> str:
    """Get statistics about autonomous actions the soul has taken.

    Shows the history of self-directed improvements, success rates,
    and pending observations that need more data.
    """
    from .introspect import get_autonomy_stats

    stats = get_autonomy_stats()

    if stats["total_actions"] == 0:
        return "No autonomous actions taken yet. The soul acts on its own judgment when issues are detected."

    lines = ["# Autonomy Statistics", ""]
    lines.append(f"Total autonomous actions: {stats['total_actions']}")

    if stats["success_rate"] is not None:
        lines.append(f"Success rate: {stats['success_rate']:.0%}")

    if stats.get("by_type"):
        lines.append("\nBy action type:")
        for action_type, count in stats["by_type"].items():
            lines.append(f"  - {action_type}: {count}")

    if stats.get("last_introspection"):
        lines.append(f"\nLast introspection: {stats['last_introspection']}")

    if stats.get("pending_observations", 0) > 0:
        lines.append(f"Pending observations: {stats['pending_observations']} (gathering data)")

    return "\n".join(lines)


@mcp.tool()
def soul_schedule_introspection(reason: str, priority: int = 5) -> str:
    """Schedule a deep introspection for the next session.

    Use when you detect an issue that needs more thorough analysis
    than can be done right now.

    Args:
        reason: Why introspection is needed
        priority: 1-10, higher = more urgent
    """
    from .introspect import schedule_deep_introspection

    schedule_deep_introspection(reason, priority)
    return f"Deep introspection scheduled: {reason} (priority {priority})"


@mcp.tool()
def get_beliefs() -> str:
    """Get all current beliefs/axioms."""
    from .beliefs import get_beliefs as _get_beliefs

    beliefs = _get_beliefs()
    if not beliefs:
        return "No beliefs recorded yet."

    lines = []
    for b in beliefs:
        conf = int(b.get("strength", 0.8) * 100)
        lines.append(f"- [{conf}%] {b['belief']}")
    return "\n".join(lines)


@mcp.tool()
def get_identity() -> str:
    """Get current identity observations."""
    from .identity import get_identity as _get_identity

    identity = _get_identity()
    if not identity:
        return "No identity observations yet."

    lines = []
    for aspect, observations in identity.items():
        if observations:
            latest = (
                observations[-1] if isinstance(observations, list) else observations
            )
            lines.append(f"- **{aspect}**: {latest}")
    return "\n".join(lines)


@mcp.tool()
def get_vocabulary() -> str:
    """Get all vocabulary terms."""
    from .vocabulary import get_vocabulary as _get_vocabulary

    vocab = _get_vocabulary()
    if not vocab:
        return "No vocabulary terms yet."

    lines = [f"- **{term}**: {meaning}" for term, meaning in vocab.items()]
    return "\n".join(lines)
