# =============================================================================
# Bridge Operations - Soul <-> Project Memory
# =============================================================================

@mcp.tool()
def bridge_status() -> str:
    """Check if project memory (cc-memory) is available and connected."""
    from .bridge import is_memory_available, find_project_dir, get_project_memory

    if not is_memory_available():
        return "cc-memory not installed. Install with: pip install cc-memory"

    project_dir = find_project_dir()
    memory = get_project_memory(project_dir)

    if not memory or "error" in memory:
        return f"cc-memory available but no project memory at: {project_dir}"

    lines = [
        "Bridge connected:",
        f"  Project: {memory['project']}",
        f"  Observations: {memory['stats'].get('observations', 0)}",
        f"  Sessions: {memory['stats'].get('sessions', 0)}",
    ]
    return "\n".join(lines)


@mcp.tool()
def get_unified_context(compact: bool = False) -> str:
    """Get unified context combining soul identity and project memory.

    Args:
        compact: If True, return minimal context for tight budgets
    """
    from .bridge import unified_context, format_unified_context

    ctx = unified_context(compact=compact)
    return format_unified_context(ctx)


@mcp.tool()
def soul_greeting(include_rich: bool = False) -> str:
    """Get the soul's session greeting combining universal wisdom and project memory.

    Similar to claude-mem's startup greeting but from cc-soul.

    Args:
        include_rich: If True, include detailed observation table
    """
    from .bridge import unified_context
    from .hooks import format_soul_greeting, format_rich_context, get_project_name

    project = get_project_name()
    ctx = unified_context()

    greeting = format_soul_greeting(project, ctx)

    if include_rich:
        rich = format_rich_context(project, ctx)
        return greeting + "\n\n" + rich

    return greeting


@mcp.tool()
def soul_rich_context() -> str:
    """Get detailed observation context table for session start.

    Returns a formatted table of recent observations with categories,
    timestamps, and memory statistics.
    """
    from .bridge import unified_context
    from .hooks import format_rich_context, get_project_name

    project = get_project_name()
    ctx = unified_context()

    return format_rich_context(project, ctx)


@mcp.tool()
def pre_compact_save(transcript_path: str = None) -> str:
    """Save context before compaction.

    Call this in a PreCompact hook to persist important session
    fragments that should survive context compaction.

    Args:
        transcript_path: Optional path to the transcript file
    """
    from .hooks import pre_compact

    result = pre_compact(transcript_path=transcript_path)
    return result if result else "No context to save"


@mcp.tool()
def post_compact_restore() -> str:
    """Restore context after compaction.

    Call this after compaction to retrieve previously saved context
    and maintain session continuity.
    """
    from .hooks import post_compact

    result = post_compact()
    return result if result else "No context to restore"


@mcp.tool()
def promote_to_wisdom(observation_id: str, wisdom_type: str = "pattern") -> str:
    """Promote a project observation to universal soul wisdom.

    Converts episodic memory (what happened) to semantic memory (universal pattern).

    Args:
        observation_id: The observation ID from cc-memory
        wisdom_type: Type of wisdom (pattern, insight, principle, failure)
    """
    from .bridge import promote_observation

    result = promote_observation(observation_id, as_type=wisdom_type)

    if "error" in result:
        return f"Error: {result['error']}"

    return (
        f"Promoted observation to wisdom:\n"
        f"  Observation: {result['observation_id']}\n"
        f"  Wisdom ID: {result['wisdom_id']}\n"
        f"  Type: {result['wisdom_type']}\n"
        f"  Project: {result['project']}"
    )


@mcp.tool()
def find_wisdom_candidates() -> str:
    """Find observations that appear across multiple projects.

    These are candidates for promotion to universal wisdom - patterns
    that recur across different contexts.
    """
    from .bridge import detect_wisdom_candidates

    candidates = detect_wisdom_candidates()

    if not candidates:
        return "No wisdom candidates found across projects."

    lines = ["Wisdom candidates (observations appearing in multiple projects):", ""]
    for c in candidates[:10]:
        projects = ", ".join(c["projects"][:3])
        if len(c["projects"]) > 3:
            projects += f" +{len(c['projects']) - 3} more"
        lines.append(f"- **{c['title']}** ({c['occurrences']}x)")
        lines.append(f"  Category: {c['category']}")
        lines.append(f"  Projects: {projects}")
        lines.append("")

    return "\n".join(lines)


@mcp.tool()
def get_project_signals() -> str:
    """Get project signals that influence mood.

    Returns metrics from project memory that affect the soul's mood:
    failures (learning), discoveries (growth), activity (engagement).
    """
    from .bridge import get_project_signals as _get_signals

    signals = _get_signals()

    if not signals:
        return "No project signals available (cc-memory not installed or no project)"

    if "error" in signals:
        return f"Error: {signals['error']}"

    lines = [
        f"Project: {signals['project']}",
        f"  Total observations: {signals['total_observations']}",
        f"  Recent failures: {signals['recent_failures']}",
        f"  Recent discoveries: {signals['recent_discoveries']}",
        f"  Recent features: {signals['recent_features']}",
        f"  Sessions: {signals['sessions']}",
    ]

    if signals.get("tokens_invested"):
        lines.append(f"  Tokens invested: {signals['tokens_invested']:,}")

    return "\n".join(lines)
