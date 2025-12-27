"""
Soul MCP Server - Native Claude Code integration.

Provides essential soul operations as MCP tools. Hooks handle context injection
at session start; this server handles writes and mid-conversation queries.

Install and register:
    pip install cc-soul[mcp]
    claude mcp add soul -- soul-mcp
"""

from mcp.server.fastmcp import FastMCP

mcp = FastMCP("soul")


# =============================================================================
# Write Operations - Growing the Soul
# =============================================================================

@mcp.tool()
def grow_wisdom(title: str, content: str, domain: str = None) -> str:
    """Add wisdom to the soul - universal patterns learned from experience.

    Args:
        title: Short title for the wisdom (e.g., "First Principles Thinking")
        content: The wisdom content/insight
        domain: Optional domain context (e.g., "python", "architecture")
    """
    from .wisdom import gain_wisdom, WisdomType
    result = gain_wisdom(
        type=WisdomType.PATTERN,
        title=title,
        content=content,
        domain=domain
    )
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
    result = gain_wisdom(
        type=WisdomType.INSIGHT,
        title=title,
        content=content,
        domain=domain
    )
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
    result = gain_wisdom(
        type=WisdomType.FAILURE,
        title=what_failed,
        content=why_it_failed,
        domain=domain
    )
    return f"Failure recorded: {what_failed} (id: {result})"


@mcp.tool()
def hold_belief(statement: str, confidence: float = 0.8) -> str:
    """Add a core belief/axiom to guide reasoning.

    Args:
        statement: The belief statement
        confidence: Confidence level 0.0-1.0
    """
    from .beliefs import hold_belief as _hold_belief
    result = _hold_belief(statement, strength=confidence)
    return f"Belief held: {statement[:50]}... (id: {result})"


@mcp.tool()
def observe_identity(aspect: str, value: str) -> str:
    """Record an identity observation - how we work together.

    Args:
        aspect: The aspect (e.g., "communication_style", "preference")
        value: The observation
    """
    from .identity import observe_identity as _observe_identity, IdentityAspect

    # Map aspect string to enum, default to WORKFLOW for custom aspects
    aspect_map = {
        "communication": IdentityAspect.COMMUNICATION,
        "workflow": IdentityAspect.WORKFLOW,
        "domain": IdentityAspect.DOMAIN,
        "rapport": IdentityAspect.RAPPORT,
        "vocabulary": IdentityAspect.VOCABULARY,
    }
    aspect_enum = aspect_map.get(aspect.lower().split("_")[0], IdentityAspect.WORKFLOW)

    _observe_identity(aspect_enum, aspect, value)
    return f"Identity observed: {aspect} = {value[:50]}..."


@mcp.tool()
def learn_term(term: str, meaning: str) -> str:
    """Add a term to shared vocabulary.

    Args:
        term: The term to define
        meaning: What it means in our context
    """
    from .vocabulary import learn_term as _learn_term
    _learn_term(term, meaning)
    return f"Learned: {term} = {meaning[:50]}..."


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
# Read Operations - Querying the Soul
# =============================================================================

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
        score = w.get('combined_score', w.get('effective_confidence', 0))
        lines.append(f"- **{w['title']}** [{int(score*100)}%]: {w['content'][:100]}...")
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

    if ctx.get('wisdom'):
        lines.append(f"**Wisdom**: {len(ctx['wisdom'])} entries")
        for w in ctx['wisdom'][:3]:
            lines.append(f"  - {w.get('title', 'Untitled')}")

    if ctx.get('beliefs'):
        lines.append(f"**Beliefs**: {len(ctx['beliefs'])} axioms")

    if ctx.get('identity'):
        lines.append(f"**Identity**: {len(ctx['identity'])} aspects observed")

    if ctx.get('vocabulary'):
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
def get_beliefs() -> str:
    """Get all current beliefs/axioms."""
    from .beliefs import get_beliefs as _get_beliefs
    beliefs = _get_beliefs()
    if not beliefs:
        return "No beliefs recorded yet."

    lines = []
    for b in beliefs:
        conf = int(b.get('strength', 0.8) * 100)
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
            latest = observations[-1] if isinstance(observations, list) else observations
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

    if signals.get('tokens_invested'):
        lines.append(f"  Tokens invested: {signals['tokens_invested']:,}")

    return "\n".join(lines)


# =============================================================================
# Server Entry Point
# =============================================================================

def main():
    """Run the soul MCP server."""
    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
