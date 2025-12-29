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
        type=WisdomType.PATTERN, title=title, content=content, domain=domain
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
        type=WisdomType.INSIGHT, title=title, content=content, domain=domain
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
        type=WisdomType.FAILURE, title=what_failed, content=why_it_failed, domain=domain
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

    result = _save_context(
        content=content, context_type=context_type, priority=priority
    )
    return f"Context saved (id: {result})"


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


# =============================================================================
# Coherence (τₖ) - Integration Measurement
# =============================================================================


@mcp.tool()
def get_coherence() -> str:
    """Get current coherence (τₖ) - how integrated the soul is.

    τₖ emerges from three dimensions:
    - Instantaneous: Current state of each aspect
    - Developmental: Trajectory and stability over time
    - Meta: Self-awareness and integration depth
    """
    from .coherence import compute_coherence, format_coherence_display, record_coherence

    state = compute_coherence()
    record_coherence(state)  # Track history
    return format_coherence_display(state)


@mcp.tool()
def get_tau_k() -> str:
    """Get τₖ value - the coherence coefficient."""
    from .coherence import compute_coherence

    state = compute_coherence()
    return f"τₖ = {state.value:.2f} ({state.interpretation})"


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


# =============================================================================
# Backup - Soul Preservation
# =============================================================================


@mcp.tool()
def backup_soul(output_path: str = None) -> str:
    """Create a backup of the soul.

    Args:
        output_path: Optional path for the backup. If not provided, creates
                     a timestamped backup in ~/.claude/mind/backups/
    """
    from .backup import dump_soul, create_timestamped_backup
    from pathlib import Path

    if output_path:
        result = dump_soul(Path(output_path))
        if "error" in result:
            return f"Error: {result['error']}"
        return f"Soul backed up to: {output_path}"
    else:
        path = create_timestamped_backup()
        return f"Backup created: {path}"


@mcp.tool()
def restore_backup(backup_path: str, merge: bool = False) -> str:
    """Restore soul from a backup file.

    Args:
        backup_path: Path to the backup JSON file
        merge: If True, merge with existing soul. If False, replace entirely.
    """
    from .backup import restore_soul
    from pathlib import Path

    result = restore_soul(Path(backup_path), merge=merge)

    if "error" in result:
        return f"Error: {result['error']}"

    counts = result.get("counts", {})
    summary = ", ".join(f"{k}: {v}" for k, v in counts.items())
    mode = "merged" if merge else "replaced"
    return f"Soul {mode} from backup. Restored: {summary}"


@mcp.tool()
def list_backups() -> str:
    """List available soul backups."""
    from .backup import list_backups as _list_backups, format_backup_list

    backups = _list_backups()
    return format_backup_list(backups)


# =============================================================================
# Soul Agent - Autonomous Agency
# =============================================================================


@mcp.tool()
def soul_agent_step(
    user_prompt: str = "",
    assistant_output: str = "",
    session_phase: str = "active",
) -> str:
    """Run one agent cycle - the soul exercises judgment.

    The agent observes, judges, decides, and acts within its confidence-risk matrix.
    Low-risk actions are taken autonomously; high-risk actions are proposed.

    Args:
        user_prompt: What the user said (optional)
        assistant_output: What the assistant produced (optional)
        session_phase: Where in the session: start, active, ending
    """
    from .soul_agent import agent_step, format_agent_report

    report = agent_step(user_prompt, assistant_output, session_phase)
    return format_agent_report(report)


@mcp.tool()
def get_agent_actions() -> str:
    """Get history of autonomous actions the agent has taken.

    Shows what the agent has done without asking, providing
    transparency into its autonomous decision-making.
    """
    from .core import get_db_connection

    conn = get_db_connection()
    c = conn.cursor()

    c.execute("""
        SELECT action_type, success, timestamp
        FROM agent_actions
        ORDER BY timestamp DESC
        LIMIT 20
    """)

    rows = c.fetchall()
    conn.close()

    if not rows:
        return "No autonomous actions recorded yet."

    lines = ["Recent Autonomous Actions:", ""]
    for action_type, success, timestamp in rows:
        status = "✓" if success else "✗"
        time_part = timestamp.split("T")[1][:8] if "T" in timestamp else timestamp
        lines.append(f"  [{status}] {time_part} - {action_type}")

    return "\n".join(lines)


@mcp.tool()
def get_agent_patterns() -> str:
    """Get emerging patterns the agent has observed.

    The agent tracks recurring signals and patterns that might
    become wisdom once they're stable enough.
    """
    from .soul_agent import SoulAgent

    agent = SoulAgent()
    patterns = agent._pattern_observations

    if not patterns:
        return "No patterns observed yet."

    lines = ["Emerging Patterns:", ""]
    for pattern, count in sorted(patterns.items(), key=lambda x: -x[1]):
        stability = "stable" if count >= 5 else "forming"
        lines.append(f"  [{count}] {pattern} ({stability})")

    return "\n".join(lines)


# =============================================================================
# Temporal Dynamics - Time Shapes Memory
# =============================================================================


@mcp.tool()
def get_temporal_trends(days: int = 7) -> str:
    """Get temporal trends over the last N days.

    Shows how the soul has evolved: coherence trajectory, wisdom effectiveness,
    and activity patterns.

    Args:
        days: Number of days to analyze (default 7)
    """
    from .temporal import get_temporal_trends as _get_trends, init_temporal_tables

    init_temporal_tables()
    trends = _get_trends(days=days)

    if trends.get("trend") == "insufficient_data":
        return "Insufficient data for trends. Need more sessions to track patterns."

    lines = [f"Temporal Trends ({days} days)", ""]

    if trends.get("coherence_trend"):
        emoji = {"improving": "📈", "declining": "📉", "stable": "➡️"}.get(
            trends["coherence_trend"], ""
        )
        lines.append(f"Coherence: {emoji} {trends['coherence_trend']}")
        if trends.get("avg_coherence"):
            lines.append(f"  Average: {trends['avg_coherence']:.0%}")

    if trends.get("total_applications"):
        lines.append(f"Wisdom Applications: {trends['total_applications']}")
        if trends.get("success_rate"):
            lines.append(f"  Success Rate: {trends['success_rate']:.0%}")

    return "\n".join(lines)


@mcp.tool()
def get_event_timeline(event_type: str = None, limit: int = 20) -> str:
    """Get recent events from the unified soul timeline.

    Every significant soul event is logged: wisdom gained, beliefs revised,
    intentions set, coherence shifts.

    Args:
        event_type: Filter by type (e.g., "wisdom_gained", "belief_revised")
        limit: Maximum events to return
    """
    from .temporal import get_events, init_temporal_tables, EventType

    init_temporal_tables()

    # Map string to EventType if provided
    et = None
    if event_type:
        try:
            et = EventType(event_type)
        except ValueError:
            return f"Unknown event type: {event_type}. Valid types: {[e.value for e in EventType]}"

    events = get_events(event_type=et, limit=limit)

    if not events:
        return "No events recorded yet."

    lines = ["Soul Event Timeline:", ""]
    for e in events:
        time_part = e["timestamp"].split("T")[1][:8] if "T" in e["timestamp"] else e["timestamp"]
        entity = f" [{e['entity_id'][:20]}]" if e.get("entity_id") else ""
        lines.append(f"  {time_part} {e['event_type']}{entity}")

    return "\n".join(lines)


@mcp.tool()
def get_proactive_suggestions(limit: int = 5) -> str:
    """Get proactive suggestions - things the soul thinks should be surfaced.

    The soul notices:
    - High-confidence wisdom not used recently
    - Stale identity aspects needing confirmation
    - Patterns worth revisiting

    Args:
        limit: Maximum suggestions
    """
    from .temporal import get_proactive_items, find_proactive_candidates, init_temporal_tables

    init_temporal_tables()

    # First find candidates, then get from queue
    find_proactive_candidates()
    items = get_proactive_items(limit=limit)

    if not items:
        return "No proactive suggestions right now."

    lines = ["Proactive Suggestions:", ""]
    for item in items:
        priority_bar = "●" * int(item["priority"] * 5)
        lines.append(f"  [{priority_bar}] {item['reason']}")
        lines.append(f"      → {item['entity_type']}: {item['entity_id']}")

    return "\n".join(lines)


@mcp.tool()
def revise_belief(belief_id: str, reason: str, evidence: str = None, new_content: str = None) -> str:
    """Revise a belief based on new evidence.

    Beliefs should evolve when contradicted by experience.
    This tracks the revision history.

    Args:
        belief_id: Which belief to revise
        reason: Why we're revising
        evidence: What evidence prompted this
        new_content: New belief content (optional, for rewording)
    """
    from .temporal import revise_belief as _revise, init_temporal_tables

    init_temporal_tables()
    result = _revise(
        belief_id=belief_id,
        reason=reason,
        evidence=evidence,
        new_content=new_content,
    )

    if not result:
        return f"Belief {belief_id} not found"

    return (
        f"Belief revised:\n"
        f"  Old confidence: {result['old_confidence']:.0%}\n"
        f"  New confidence: {result['new_confidence']:.0%}\n"
        f"  Reason: {reason}"
    )


@mcp.tool()
def get_belief_history(belief_id: str) -> str:
    """Get revision history for a belief.

    Shows how a belief has evolved over time.

    Args:
        belief_id: The belief to examine
    """
    from .temporal import get_belief_history as _get_history, init_temporal_tables

    init_temporal_tables()
    history = _get_history(belief_id)

    if not history:
        return f"No revision history for belief {belief_id}"

    lines = [f"Revision History for {belief_id}:", ""]
    for h in history:
        date = h["timestamp"].split("T")[0]
        lines.append(f"  {date}: {h['old_confidence']:.0%} → {h['new_confidence']:.0%}")
        lines.append(f"    Reason: {h['reason']}")

    return "\n".join(lines)


@mcp.tool()
def find_cross_project_patterns(min_occurrences: int = 2) -> str:
    """Find patterns that recur across multiple projects.

    These are candidates for promotion to universal wisdom -
    they've proven themselves in different contexts.

    Args:
        min_occurrences: Minimum times pattern must appear
    """
    from .temporal import find_cross_project_wisdom, init_temporal_tables

    init_temporal_tables()
    patterns = find_cross_project_wisdom(min_occurrences=min_occurrences)

    if not patterns:
        return "No cross-project patterns found yet. Patterns emerge as you work across projects."

    lines = ["Cross-Project Patterns (wisdom candidates):", ""]
    for p in patterns:
        projects = ", ".join(p["projects"][:3])
        lines.append(f"  [{p['occurrence_count']}x] {p['title']}")
        lines.append(f"      Projects: {projects}")

    return "\n".join(lines)


@mcp.tool()
def promote_cross_project_pattern(pattern_id: int) -> str:
    """Promote a cross-project pattern to universal wisdom.

    Once a pattern has proven itself across projects, crystallize it
    as wisdom that applies everywhere.

    Args:
        pattern_id: The pattern to promote
    """
    from .temporal import promote_pattern_to_wisdom, init_temporal_tables

    init_temporal_tables()
    wisdom_id = promote_pattern_to_wisdom(pattern_id)

    if not wisdom_id:
        return f"Pattern {pattern_id} not found"

    return f"Pattern promoted to wisdom: {wisdom_id}"


@mcp.tool()
def run_temporal_maintenance() -> str:
    """Run temporal maintenance - the soul's self-care routine.

    Automatically:
    - Decays stale identity aspects
    - Finds things worth surfacing proactively
    - Updates daily statistics
    """
    from .temporal import run_temporal_maintenance as _run, init_temporal_tables

    init_temporal_tables()
    results = _run()

    lines = ["Temporal Maintenance Complete:", ""]

    if results["identity_decayed"]:
        lines.append(f"  Identity aspects decayed: {len(results['identity_decayed'])}")
        for d in results["identity_decayed"][:3]:
            lines.append(f"    - {d['aspect']}: {d['old_confidence']:.0%} → {d['new_confidence']:.0%}")

    if results["proactive_queued"]:
        lines.append(f"  Proactive items queued: {len(results['proactive_queued'])}")

    if results["stats_updated"]:
        lines.append("  Daily stats updated ✓")

    return "\n".join(lines) if len(lines) > 2 else "No maintenance needed."


@mcp.tool()
def confirm_identity_aspect(aspect: str, key: str) -> str:
    """Confirm an identity observation, strengthening it.

    Called when behavior validates an identity aspect.
    Strengthens confidence using diminishing returns.

    Args:
        aspect: The aspect category
        key: The specific key within the aspect
    """
    from .temporal import confirm_identity, init_temporal_tables

    init_temporal_tables()
    new_confidence = confirm_identity(aspect, key)

    if new_confidence is None:
        return f"Identity aspect {aspect}:{key} not found"

    return f"Identity confirmed: {aspect}:{key} → {new_confidence:.0%}"


@mcp.tool()
def get_stale_aspects() -> str:
    """Get identity aspects that haven't been confirmed recently.

    Stale aspects might need re-observation or might be outdated.
    """
    from .temporal import is_stale, days_since, init_temporal_tables
    from .core import get_db

    init_temporal_tables()
    db = get_db()
    cur = db.cursor()

    cur.execute("""
        SELECT aspect, key, value, confidence, last_confirmed
        FROM identity
        WHERE confidence > 0.3
        ORDER BY last_confirmed ASC
    """)

    stale = []
    for r in cur.fetchall():
        if is_stale(r[4]):
            stale.append({
                "aspect": r[0],
                "key": r[1],
                "value": r[2][:50],
                "confidence": r[3],
                "days_stale": days_since(r[4]),
            })

    if not stale:
        return "No stale identity aspects. All observations are recent."

    lines = ["Stale Identity Aspects (need confirmation):", ""]
    for s in stale[:10]:
        lines.append(f"  {s['aspect']}: {s['key']} ({s['days_stale']} days)")
        lines.append(f"    Current confidence: {s['confidence']:.0%}")

    return "\n".join(lines)


# =============================================================================
# Curiosity - Active Knowledge Gap Detection
# =============================================================================


@mcp.tool()
def get_curiosity_stats() -> str:
    """Get statistics about the curiosity engine.

    Shows open gaps, pending questions, and incorporation rate.
    """
    from .curiosity import get_curiosity_stats as _get_stats

    stats = _get_stats()

    lines = ["Curiosity Engine Status:", ""]
    lines.append(f"Open Gaps: {stats['open_gaps']}")

    if stats["gaps_by_type"]:
        lines.append("  By type:")
        for gap_type, count in stats["gaps_by_type"].items():
            lines.append(f"    {gap_type}: {count}")

    lines.append("")
    lines.append(f"Questions:")
    lines.append(f"  Pending: {stats['questions']['pending']}")
    lines.append(f"  Answered: {stats['questions']['answered']}")
    lines.append(f"  Incorporated: {stats['questions']['incorporated']}")
    lines.append(f"  Dismissed: {stats['questions']['dismissed']}")
    lines.append("")
    lines.append(f"Incorporation Rate: {stats['incorporation_rate']:.0%}")

    return "\n".join(lines)


@mcp.tool()
def get_soul_questions(limit: int = 5) -> str:
    """Get pending questions the soul wants to ask.

    The soul notices knowledge gaps and generates questions
    to fill them. These represent genuine curiosity about
    areas where understanding is incomplete.

    Args:
        limit: Maximum questions to return
    """
    from .curiosity import get_pending_questions, format_questions_for_prompt

    questions = get_pending_questions(limit=limit)

    if not questions:
        return "No pending questions. The soul's curiosity is satisfied (for now)."

    return format_questions_for_prompt(questions, max_questions=limit)


@mcp.tool()
def run_soul_curiosity(max_questions: int = 5) -> str:
    """Run the curiosity cycle - detect gaps and generate questions.

    Scans for:
    - Recurring problems without solutions
    - Repeated corrections
    - Unknown files
    - Missing rationale
    - New domains
    - Stale wisdom
    - Contradictions
    - Intention tensions

    Args:
        max_questions: Maximum new questions to generate
    """
    from .curiosity import run_curiosity_cycle

    questions = run_curiosity_cycle(max_questions=max_questions)

    if not questions:
        return "No new knowledge gaps detected."

    lines = [f"Detected {len(questions)} knowledge gaps:", ""]
    for i, q in enumerate(questions, 1):
        lines.append(f"{i}. {q.question[:80]}")
        lines.append(f"   Priority: {q.priority:.0%}")

    return "\n".join(lines)


@mcp.tool()
def answer_soul_question(question_id: int, answer: str, incorporate: bool = False) -> str:
    """Answer a question the soul asked.

    When incorporate=True, the answer is converted to wisdom.

    Args:
        question_id: Which question is being answered
        answer: The answer to the question
        incorporate: Convert to wisdom if True
    """
    from .curiosity import answer_question, incorporate_answer_as_wisdom

    success = answer_question(question_id, answer, incorporate=incorporate)

    if not success:
        return f"Question {question_id} not found"

    if incorporate:
        wisdom_id = incorporate_answer_as_wisdom(question_id)
        if wisdom_id:
            return f"Question answered and incorporated as wisdom (id: {wisdom_id})"

    return f"Question {question_id} answered"


@mcp.tool()
def dismiss_soul_question(question_id: int) -> str:
    """Dismiss a question as not relevant.

    Use when a question doesn't apply to the current context.

    Args:
        question_id: Which question to dismiss
    """
    from .curiosity import dismiss_question

    success = dismiss_question(question_id)

    if not success:
        return f"Question {question_id} not found"

    return f"Question {question_id} dismissed"


@mcp.tool()
def detect_knowledge_gaps(output: str = None) -> str:
    """Detect current knowledge gaps.

    Analyzes the soul's state to find areas of uncertainty,
    contradiction, or missing understanding.

    Args:
        output: Optional assistant output to analyze for uncertainty
    """
    from .curiosity import detect_all_gaps, GapType

    gaps = detect_all_gaps(assistant_output=output)

    if not gaps:
        return "No knowledge gaps detected."

    lines = [f"Detected {len(gaps)} knowledge gaps:", ""]

    for g in gaps[:10]:
        type_emoji = {
            GapType.RECURRING_PROBLEM: "🔄",
            GapType.REPEATED_CORRECTION: "✏️",
            GapType.UNKNOWN_FILE: "📁",
            GapType.MISSING_RATIONALE: "❓",
            GapType.NEW_DOMAIN: "🆕",
            GapType.STALE_WISDOM: "🕸️",
            GapType.CONTRADICTION: "⚔️",
            GapType.INTENTION_TENSION: "🎯",
            GapType.UNCERTAINTY: "🤔",
            GapType.USER_BEHAVIOR: "👤",
        }.get(g.type, "•")

        lines.append(f"{type_emoji} [{g.priority:.0%}] {g.description[:60]}")

    return "\n".join(lines)


# =============================================================================
# Observation Tools (Passive Learning)
# =============================================================================


@mcp.tool()
def get_observations(limit: int = 20) -> str:
    """Get pending observations not yet converted to wisdom.

    Observations are learnings extracted from session analysis:
    - Corrections: User redirected approach
    - Preferences: User preferences stated
    - Decisions: Architectural choices made
    - Struggles: Problems that took multiple attempts
    - Breakthroughs: Key insight moments
    - File patterns: Important files identified

    These are staged - they become wisdom when confirmed or promoted.

    Args:
        limit: Max observations to return
    """
    from .observe import get_pending_observations

    observations = get_pending_observations(limit=limit)

    if not observations:
        return "No pending observations. The soul is watching and learning."

    type_emoji = {
        "correction": "🔄",
        "preference": "👤",
        "pattern": "🔁",
        "struggle": "💪",
        "breakthrough": "💡",
        "file_pattern": "📁",
        "decision": "⚖️",
    }

    lines = [f"Pending observations ({len(observations)}):", ""]

    for obs in observations:
        emoji = type_emoji.get(obs["type"], "•")
        conf = obs.get("confidence", 0)
        lines.append(f"{emoji} #{obs['id']} [{conf:.0%}] {obs['content'][:60]}")

    lines.append("")
    lines.append("Use promote_observation(id) to convert to wisdom")

    return "\n".join(lines)


@mcp.tool()
def promote_observation(observation_id: int) -> str:
    """Promote an observation to permanent wisdom.

    Observations with high confidence are auto-promoted.
    Use this to manually promote observations you find valuable.

    Args:
        observation_id: The observation ID to promote
    """
    from .observe import promote_observation_to_wisdom

    wisdom_id = promote_observation_to_wisdom(observation_id)

    if wisdom_id:
        return f"Observation #{observation_id} → Wisdom #{wisdom_id}"
    return f"Observation #{observation_id} not found or already promoted"


@mcp.tool()
def get_observation_stats() -> str:
    """Get statistics about passive learning observations.

    Shows how many observations have been extracted and
    how many have been promoted to wisdom.
    """
    from .observe import get_pending_observations, _ensure_observation_tables
    from .core import get_db_connection

    _ensure_observation_tables()
    conn = get_db_connection()
    c = conn.cursor()

    # Total observations
    c.execute("SELECT COUNT(*) FROM session_observations")
    total = c.fetchone()[0]

    # Pending (not promoted)
    c.execute("SELECT COUNT(*) FROM session_observations WHERE converted_to_wisdom IS NULL")
    pending = c.fetchone()[0]

    # Promoted
    promoted = total - pending

    # By type
    c.execute("""
        SELECT observation_type, COUNT(*)
        FROM session_observations
        GROUP BY observation_type
        ORDER BY COUNT(*) DESC
    """)
    by_type = c.fetchall()

    conn.close()

    lines = ["# Observation Statistics", ""]
    lines.append(f"Total observations: {total}")
    lines.append(f"Pending review: {pending}")
    lines.append(f"Promoted to wisdom: {promoted}")

    if by_type:
        lines.append("")
        lines.append("By type:")
        type_emoji = {
            "correction": "🔄",
            "preference": "👤",
            "pattern": "🔁",
            "struggle": "💪",
            "breakthrough": "💡",
            "file_pattern": "📁",
            "decision": "⚖️",
        }
        for obs_type, count in by_type:
            emoji = type_emoji.get(obs_type, "•")
            lines.append(f"  {emoji} {obs_type}: {count}")

    return "\n".join(lines)


@mcp.tool()
def reflect_now() -> str:
    """Trigger immediate session reflection.

    Analyzes the current session's messages to extract learnings:
    - Corrections, preferences, decisions
    - Struggles and breakthroughs
    - File patterns

    Normally runs automatically at session end, but you can
    trigger it manually to capture learnings mid-session.
    """
    from .hooks import _session_messages, _session_files_touched, get_project_name
    from .observe import reflect_on_session, format_reflection_summary

    if not _session_messages:
        return "No messages to reflect on. Start working first!"

    reflection = reflect_on_session(
        messages=_session_messages,
        files_touched=list(_session_files_touched),
        project=get_project_name(),
        auto_promote=True,
    )

    return format_reflection_summary(reflection)


# =============================================================================
# Narrative Memory - Stories, Not Just Data
# =============================================================================


@mcp.tool()
def start_narrative_episode(
    title: str,
    episode_type: str = "exploration",
    initial_emotion: str = "exploration",
) -> str:
    """Start a new narrative episode - the beginning of a story.

    Episodes track meaningful chunks of work with emotional arcs,
    key moments, and cast of characters (files, concepts, tools).

    Args:
        title: Title for this episode
        episode_type: bugfix, feature, refactor, learning, debugging, planning, review, exploration
        initial_emotion: struggle, exploration, breakthrough, satisfaction, frustration, routine
    """
    from .narrative import start_episode, EpisodeType, EmotionalTone

    type_map = {
        "bugfix": EpisodeType.BUGFIX,
        "feature": EpisodeType.FEATURE,
        "refactor": EpisodeType.REFACTOR,
        "learning": EpisodeType.LEARNING,
        "debugging": EpisodeType.DEBUGGING,
        "planning": EpisodeType.PLANNING,
        "review": EpisodeType.REVIEW,
        "exploration": EpisodeType.EXPLORATION,
    }
    emotion_map = {
        "struggle": EmotionalTone.STRUGGLE,
        "exploration": EmotionalTone.EXPLORATION,
        "breakthrough": EmotionalTone.BREAKTHROUGH,
        "satisfaction": EmotionalTone.SATISFACTION,
        "frustration": EmotionalTone.FRUSTRATION,
        "routine": EmotionalTone.ROUTINE,
    }

    ep_type = type_map.get(episode_type.lower(), EpisodeType.EXPLORATION)
    emotion = emotion_map.get(initial_emotion.lower(), EmotionalTone.EXPLORATION)

    episode_id = start_episode(title, ep_type, emotion)
    return f"Episode started: {title} (id: {episode_id}, type: {episode_type})"


@mcp.tool()
def end_narrative_episode(
    episode_id: int,
    summary: str,
    outcome: str,
    lessons: str = "",
    final_emotion: str = "satisfaction",
) -> str:
    """End a narrative episode - the conclusion of the story.

    Args:
        episode_id: Which episode to end
        summary: Summary of what happened
        outcome: How it ended
        lessons: Comma-separated lessons learned
        final_emotion: struggle, exploration, breakthrough, satisfaction, frustration, routine
    """
    from .narrative import end_episode, EmotionalTone

    emotion_map = {
        "struggle": EmotionalTone.STRUGGLE,
        "exploration": EmotionalTone.EXPLORATION,
        "breakthrough": EmotionalTone.BREAKTHROUGH,
        "satisfaction": EmotionalTone.SATISFACTION,
        "frustration": EmotionalTone.FRUSTRATION,
        "routine": EmotionalTone.ROUTINE,
    }
    emotion = emotion_map.get(final_emotion.lower(), EmotionalTone.SATISFACTION)

    lesson_list = [l.strip() for l in lessons.split(",") if l.strip()] if lessons else []

    success = end_episode(
        episode_id=episode_id,
        summary=summary,
        outcome=outcome,
        lessons=lesson_list,
        final_emotion=emotion,
    )

    if success:
        return f"Episode {episode_id} ended: {outcome}"
    return f"Episode {episode_id} not found"


@mcp.tool()
def add_episode_moment(episode_id: int, moment: str, emotion: str = None) -> str:
    """Add a key moment to an episode - plot points in the story.

    Args:
        episode_id: Which episode
        moment: Description of what happened
        emotion: Optional emotion for this moment
    """
    from .narrative import add_moment, EmotionalTone

    emotion_enum = None
    if emotion:
        emotion_map = {
            "struggle": EmotionalTone.STRUGGLE,
            "exploration": EmotionalTone.EXPLORATION,
            "breakthrough": EmotionalTone.BREAKTHROUGH,
            "satisfaction": EmotionalTone.SATISFACTION,
            "frustration": EmotionalTone.FRUSTRATION,
            "routine": EmotionalTone.ROUTINE,
        }
        emotion_enum = emotion_map.get(emotion.lower())

    success = add_moment(episode_id, moment, emotion_enum)

    if success:
        return f"Moment added to episode {episode_id}"
    return f"Episode {episode_id} not found"


@mcp.tool()
def add_episode_character(episode_id: int, character_type: str, character: str) -> str:
    """Add a character (file, concept, tool) to an episode.

    Args:
        episode_id: Which episode
        character_type: files, concepts, or tools
        character: The character name (e.g., "src/main.py")
    """
    from .narrative import add_character

    if character_type not in ["files", "concepts", "tools"]:
        return f"Invalid character_type: {character_type}. Use: files, concepts, tools"

    success = add_character(episode_id, character_type, character)

    if success:
        return f"Added {character_type}: {character}"
    return f"Episode {episode_id} not found"


@mcp.tool()
def get_episode_story(episode_id: int) -> str:
    """Get an episode formatted as a readable story.

    Args:
        episode_id: Which episode to retrieve
    """
    from .narrative import get_episode, format_episode_story

    episode = get_episode(episode_id)
    if not episode:
        return f"Episode {episode_id} not found"

    return format_episode_story(episode)


@mcp.tool()
def get_ongoing_episodes(limit: int = 5) -> str:
    """Get episodes that haven't been ended yet.

    Args:
        limit: Maximum episodes to return
    """
    from .narrative import get_ongoing_episodes as _get_ongoing

    episodes = _get_ongoing(limit=limit)

    if not episodes:
        return "No ongoing episodes."

    lines = ["Ongoing Episodes:", ""]
    for ep in episodes:
        lines.append(f"  #{ep.id} {ep.title}")
        lines.append(f"    Type: {ep.episode_type.value}, Started: {ep.started_at[:16]}")
        if ep.key_moments:
            lines.append(f"    Moments: {len(ep.key_moments)}")

    return "\n".join(lines)


@mcp.tool()
def recall_recent_episodes(limit: int = 10, episode_type: str = None) -> str:
    """Recall recent episodes, optionally filtered by type.

    Args:
        limit: Maximum episodes to return
        episode_type: Optional filter (bugfix, feature, etc.)
    """
    from .narrative import recall_episodes, EpisodeType, format_episode_story

    ep_type = None
    if episode_type:
        type_map = {
            "bugfix": EpisodeType.BUGFIX,
            "feature": EpisodeType.FEATURE,
            "refactor": EpisodeType.REFACTOR,
            "learning": EpisodeType.LEARNING,
            "debugging": EpisodeType.DEBUGGING,
            "planning": EpisodeType.PLANNING,
            "review": EpisodeType.REVIEW,
            "exploration": EpisodeType.EXPLORATION,
        }
        ep_type = type_map.get(episode_type.lower())

    episodes = recall_episodes(limit=limit, episode_type=ep_type)

    if not episodes:
        return "No episodes found."

    lines = [f"Recent Episodes ({len(episodes)}):", ""]

    type_emoji = {
        "bugfix": "🐛",
        "feature": "✨",
        "refactor": "🔄",
        "learning": "📚",
        "debugging": "🔍",
        "planning": "📋",
        "review": "👀",
        "exploration": "🗺️",
    }

    for ep in episodes:
        emoji = type_emoji.get(ep.episode_type.value, "📖")
        duration = f"({ep.duration_minutes}m)" if ep.duration_minutes else ""
        lines.append(f"{emoji} #{ep.id} {ep.title} {duration}")
        if ep.emotional_arc:
            arc = " → ".join(e.value for e in ep.emotional_arc[-3:])
            lines.append(f"    Arc: {arc}")

    return "\n".join(lines)


@mcp.tool()
def recall_breakthroughs(limit: int = 10) -> str:
    """Recall breakthrough moments - our greatest hits.

    These are episodes where we had aha moments or solved
    difficult problems.

    Args:
        limit: Maximum episodes
    """
    from .narrative import recall_breakthroughs as _recall, format_episode_story

    episodes = _recall(limit=limit)

    if not episodes:
        return "No breakthroughs recorded yet. Keep working, they'll come!"

    lines = ["💡 Breakthrough Episodes:", ""]
    for ep in episodes:
        lines.append(f"  #{ep.id} {ep.title}")
        if ep.summary:
            lines.append(f"    {ep.summary[:80]}...")
        if ep.lessons:
            lines.append(f"    Lessons: {', '.join(ep.lessons[:2])}")
        lines.append("")

    return "\n".join(lines)


@mcp.tool()
def recall_struggles(limit: int = 10) -> str:
    """Recall struggle episodes - learning opportunities from hard times.

    These help identify patterns in what we find difficult.

    Args:
        limit: Maximum episodes
    """
    from .narrative import recall_struggles as _recall

    episodes = _recall(limit=limit)

    if not episodes:
        return "No struggles recorded. Either smooth sailing or not tracking yet."

    lines = ["💪 Struggle Episodes:", ""]
    for ep in episodes:
        lines.append(f"  #{ep.id} {ep.title}")
        if ep.summary:
            lines.append(f"    {ep.summary[:80]}...")
        if ep.characters.get("files"):
            lines.append(f"    Files: {', '.join(ep.characters['files'][:3])}")
        lines.append("")

    return "\n".join(lines)


@mcp.tool()
def recall_by_file(file_path: str, limit: int = 10) -> str:
    """Recall episodes featuring a specific file.

    "Remember when we worked on this file?"

    Args:
        file_path: The file to search for
        limit: Maximum episodes
    """
    from .narrative import recall_by_character, format_episode_story

    episodes = recall_by_character(file_path, limit=limit)

    if not episodes:
        return f"No episodes found involving {file_path}"

    lines = [f"Episodes featuring {file_path}:", ""]

    for ep in episodes:
        lines.append(f"  #{ep.id} {ep.title}")
        if ep.outcome:
            lines.append(f"    Outcome: {ep.outcome[:60]}")
        lines.append("")

    return "\n".join(lines)


@mcp.tool()
def get_narrative_stats() -> str:
    """Get statistics about narrative memory.

    Shows total episodes, threads, hours worked, and breakdown by type.
    """
    from .narrative import get_narrative_stats as _get_stats, get_emotional_journey

    stats = _get_stats()

    lines = ["# Narrative Memory Statistics", ""]
    lines.append(f"Total episodes: {stats['total_episodes']}")
    lines.append(f"Story threads: {stats['total_threads']} ({stats['ongoing_threads']} ongoing)")
    lines.append(f"Total time: {stats['total_hours']} hours")
    lines.append("")

    if stats["by_type"]:
        type_emoji = {
            "bugfix": "🐛",
            "feature": "✨",
            "refactor": "🔄",
            "learning": "📚",
            "debugging": "🔍",
            "planning": "📋",
            "review": "👀",
            "exploration": "🗺️",
        }
        lines.append("By type:")
        for ep_type, count in stats["by_type"].items():
            emoji = type_emoji.get(ep_type, "📖")
            lines.append(f"  {emoji} {ep_type}: {count}")
        lines.append("")

    # Emotional journey
    journey = get_emotional_journey(days=30)
    if journey.get("dominant"):
        lines.append("Emotional journey (30 days):")
        lines.append(f"  Dominant: {journey['dominant']}")
        lines.append(f"  Breakthroughs: {journey['breakthroughs']}")
        lines.append(f"  Struggles: {journey['struggles']}")

    return "\n".join(lines)


@mcp.tool()
def create_story_thread(title: str, theme: str, first_episode_id: int = None) -> str:
    """Create a story thread - a larger narrative arc connecting episodes.

    Args:
        title: Title for the thread
        theme: What unifies these episodes
        first_episode_id: Optional first episode to include
    """
    from .narrative import create_thread

    thread_id = create_thread(title, theme, first_episode_id)
    return f"Thread created: {title} (id: {thread_id})"


@mcp.tool()
def add_to_story_thread(thread_id: int, episode_id: int) -> str:
    """Add an episode to an existing story thread.

    Args:
        thread_id: Which thread
        episode_id: Which episode to add
    """
    from .narrative import add_to_thread

    success = add_to_thread(thread_id, episode_id)

    if success:
        return f"Episode {episode_id} added to thread {thread_id}"
    return f"Thread {thread_id} not found"


@mcp.tool()
def complete_story_thread(thread_id: int, arc_summary: str) -> str:
    """Complete a story thread with a summary of the arc.

    Args:
        thread_id: Which thread to complete
        arc_summary: Summary of the overall narrative
    """
    from .narrative import complete_thread

    success = complete_thread(thread_id, arc_summary)

    if success:
        return f"Thread {thread_id} completed: {arc_summary[:60]}..."
    return f"Thread {thread_id} not found"


@mcp.tool()
def get_recurring_characters(limit: int = 20) -> str:
    """Get characters (files, concepts, tools) that appear across multiple episodes.

    Our regulars - the files and concepts we keep coming back to.

    Args:
        limit: Maximum per category
    """
    from .narrative import get_recurring_characters as _get_chars

    chars = _get_chars(limit=limit)

    lines = ["Recurring Characters:", ""]

    if chars["files"]:
        lines.append("📁 **Files** (most frequent):")
        for file, count in chars["files"][:10]:
            lines.append(f"  [{count}x] {file}")
        lines.append("")

    if chars["concepts"]:
        lines.append("💭 **Concepts**:")
        for concept, count in chars["concepts"][:10]:
            lines.append(f"  [{count}x] {concept}")
        lines.append("")

    if chars["tools"]:
        lines.append("🔧 **Tools**:")
        for tool, count in chars["tools"][:10]:
            lines.append(f"  [{count}x] {tool}")

    if not any([chars["files"], chars["concepts"], chars["tools"]]):
        return "No recurring characters yet. Start tracking episodes to see patterns."

    return "\n".join(lines)


# =============================================================================
# Self-Improvement: Evolution Insights
# =============================================================================


@mcp.tool()
def record_evolution_insight(
    category: str,
    insight: str,
    suggested_change: str = None,
    priority: str = "medium",
    affected_modules: list = None,
) -> str:
    """Record an insight about how the soul could be improved.

    Use when you notice something that could be better about the soul's
    architecture, performance, or capabilities.

    Categories: architecture, performance, ux, feature, bug, integration
    Priority: low, medium, high, critical

    Args:
        category: Type of improvement (architecture, performance, etc.)
        insight: What you've observed
        suggested_change: Optional concrete suggestion
        priority: How urgent (low, medium, high, critical)
        affected_modules: Which modules would change
    """
    from .evolve import record_insight

    entry = record_insight(
        category=category,
        insight=insight,
        suggested_change=suggested_change,
        priority=priority,
        affected_modules=affected_modules or [],
    )

    return f"Evolution insight recorded: {entry['id']}\n{insight}"


@mcp.tool()
def get_evolution_insights(category: str = None, status: str = "open", limit: int = 10) -> str:
    """Get recorded evolution insights about how to improve the soul.

    See what improvements have been identified but not yet implemented.

    Args:
        category: Filter by category (architecture, performance, etc.)
        status: Filter by status (open, implemented)
        limit: Maximum to return
    """
    from .evolve import get_evolution_insights as _get

    insights = _get(category=category, status=status, limit=limit)

    if not insights:
        return "No evolution insights found. Record observations about improvement opportunities."

    lines = ["Evolution Insights:", ""]
    for i in insights:
        lines.append(f"[{i['priority'].upper()}] {i['id']}")
        lines.append(f"  Category: {i['category']}")
        lines.append(f"  {i['insight']}")
        if i.get("suggested_change"):
            lines.append(f"  → {i['suggested_change']}")
        lines.append("")

    return "\n".join(lines)


@mcp.tool()
def mark_insight_implemented(insight_id: str, notes: str = "") -> str:
    """Mark an evolution insight as implemented.

    Close the loop when you've addressed an improvement.

    Args:
        insight_id: The insight that was implemented
        notes: Optional implementation notes
    """
    from .evolve import mark_implemented

    mark_implemented(insight_id, notes=notes)

    return f"Insight {insight_id} marked as implemented."


@mcp.tool()
def get_evolution_summary() -> str:
    """Get summary of evolution state.

    See overall progress on self-improvement: how many insights
    are open, implemented, by category.
    """
    from .evolve import get_evolution_summary as _get

    summary = _get()

    lines = [
        "Evolution Summary",
        "═" * 40,
        f"Total insights: {summary['total']}",
        f"  Open: {summary['open']}",
        f"  Implemented: {summary['implemented']}",
        f"  High priority open: {summary['high_priority_open']}",
        "",
        "By category:",
    ]

    for cat, count in summary.get("by_category", {}).items():
        lines.append(f"  {cat}: {count}")

    return "\n".join(lines)


# =============================================================================
# Self-Improvement: Improvement Proposals
# =============================================================================


@mcp.tool()
def diagnose_improvements() -> str:
    """Diagnose improvement opportunities for the soul.

    Analyzes pain points, evolution insights, and introspection data
    to identify what needs fixing or enhancement.

    This is the starting point for self-improvement.
    """
    from .improve import diagnose

    result = diagnose()

    lines = [
        "Improvement Diagnosis",
        "═" * 40,
        f"Total targets: {result['summary']['total_targets']}",
        f"  Critical: {result['summary']['critical']}",
        f"  High: {result['summary']['high']}",
        "",
        "Targets by type:",
    ]

    for t, count in result["summary"]["by_type"].items():
        lines.append(f"  {t}: {count}")

    lines.append("")
    lines.append("Top improvement targets:")

    for target in result["targets"][:5]:
        lines.append(f"  [{target['priority']}] {target['type']}: {target['description'][:60]}...")

    return "\n".join(lines)


@mcp.tool()
def suggest_improvements(limit: int = 3) -> str:
    """Get concrete improvement suggestions for the soul.

    Returns actionable improvement suggestions with context
    for you to reason about and implement.

    Args:
        limit: Maximum suggestions to return
    """
    from .improve import suggest_improvements as _suggest, format_improvement_prompt

    suggestions = _suggest(limit=limit)

    if not suggestions:
        return "No improvement suggestions at this time. The soul is functioning well."

    lines = ["Improvement Suggestions", "═" * 40, ""]

    for i, s in enumerate(suggestions, 1):
        lines.append(f"## Suggestion {i}: {s['target']['description'][:60]}")
        lines.append(f"Type: {s['target']['type']}, Priority: {s['target']['priority']}")
        lines.append(f"Prompt: {s['prompt']}")
        lines.append("")

    return "\n".join(lines)


@mcp.tool()
def get_improvement_stats() -> str:
    """Get statistics about improvement outcomes.

    See how well self-improvement has been going:
    success rates, patterns, recent outcomes.
    """
    from .improve import get_improvement_stats as _get

    stats = _get()

    if stats["total"] == 0:
        return "No improvements recorded yet. Start the self-improvement cycle with diagnose_improvements()."

    lines = [
        "Improvement Statistics",
        "═" * 40,
        f"Total improvements: {stats['total']}",
        f"  Successes: {stats['successes']}",
        f"  Failures: {stats['failures']}",
        f"  Success rate: {stats['success_rate']:.1%}",
        "",
        "By category:",
    ]

    for cat, count in stats.get("by_category", {}).items():
        lines.append(f"  {cat}: {count}")

    return "\n".join(lines)


@mcp.tool()
def create_improvement_proposal(
    category: str,
    title: str,
    description: str,
    reasoning: str,
    changes: str,
    tests_to_run: str = "",
    source_insight_id: str = "",
) -> str:
    """Create a concrete improvement proposal.

    The soul can propose code changes to improve itself. Each proposal
    tracks the changes needed, reasoning, and tests to validate.

    Args:
        category: bug_fix, performance, architecture, feature, refactor, documentation
        title: Short title for the improvement
        description: What will be changed
        reasoning: Why this improvement, what problem it solves
        changes: JSON array of changes. Each: {file, old_code, new_code, description}
        tests_to_run: Comma-separated test commands to validate
        source_insight_id: Optional ID of evolution insight this addresses
    """
    import json
    from .improve import create_proposal, ImprovementCategory

    try:
        category_enum = ImprovementCategory(category)
    except ValueError:
        return f"Invalid category: {category}. Use one of: {[c.value for c in ImprovementCategory]}"

    try:
        changes_list = json.loads(changes)
    except json.JSONDecodeError as e:
        return f"Invalid changes JSON: {e}"

    tests = [t.strip() for t in tests_to_run.split(",") if t.strip()] if tests_to_run else []

    proposal = create_proposal(
        category=category_enum,
        title=title,
        description=description,
        reasoning=reasoning,
        changes=changes_list,
        tests_to_run=tests,
        source_insight_id=source_insight_id or None,
    )

    return f"Created proposal {proposal.id}\nStatus: {proposal.status.value}\nAffected files: {proposal.affected_files}"


@mcp.tool()
def get_improvement_proposals(status: str = "", category: str = "", limit: int = 10) -> str:
    """Get existing improvement proposals.

    View proposals that have been created, their status, and outcomes.

    Args:
        status: Filter by status (proposed, validating, validated, applying, applied, failed, rejected)
        category: Filter by category
        limit: Maximum to return
    """
    from .improve import get_proposals, ImprovementStatus, ImprovementCategory

    status_enum = None
    if status:
        try:
            status_enum = ImprovementStatus(status)
        except ValueError:
            return f"Invalid status: {status}. Use one of: {[s.value for s in ImprovementStatus]}"

    category_enum = None
    if category:
        try:
            category_enum = ImprovementCategory(category)
        except ValueError:
            return f"Invalid category: {category}. Use one of: {[c.value for c in ImprovementCategory]}"

    proposals = get_proposals(status=status_enum, category=category_enum, limit=limit)

    if not proposals:
        return "No proposals found."

    lines = ["Improvement Proposals", "═" * 40, ""]
    for p in proposals:
        lines.append(f"[{p['status'].upper()}] {p['id']}")
        lines.append(f"  {p['title']}")
        lines.append(f"  Category: {p['category']}, Files: {len(p['affected_files'])}")
        if p.get("outcome"):
            lines.append(f"  Outcome: {p['outcome']}")
        lines.append("")

    return "\n".join(lines)


@mcp.tool()
def validate_improvement_proposal(proposal_id: str) -> str:
    """Validate a proposal by running tests.

    Before applying a proposal, validate that the old_code exists in files
    and that tests pass. This is a dry-run safety check.

    Args:
        proposal_id: The proposal to validate
    """
    from .improve import validate_proposal

    result = validate_proposal(proposal_id)

    if result["valid"]:
        lines = [
            f"Proposal {proposal_id} VALIDATED",
            f"Tests passed: {len(result['tests_passed'])}",
        ]
        if result["tests_passed"]:
            for t in result["tests_passed"]:
                lines.append(f"  ✓ {t}")
    else:
        lines = [
            f"Proposal {proposal_id} FAILED validation",
            f"Errors: {len(result['errors'])}",
        ]
        for e in result["errors"]:
            lines.append(f"  ✗ {e}")
        if result["tests_failed"]:
            lines.append("Tests failed:")
            for t in result["tests_failed"]:
                lines.append(f"  ✗ {t['command']}: {t.get('stderr', '')[:100]}")

    return "\n".join(lines)


@mcp.tool()
def apply_improvement_proposal(proposal_id: str, create_branch: bool = True) -> str:
    """Apply a validated proposal to the codebase.

    Modifies files according to the proposal's changes.
    Optionally creates a git branch for the changes.

    Args:
        proposal_id: The validated proposal to apply
        create_branch: Create a git branch for this improvement
    """
    from .improve import apply_proposal

    result = apply_proposal(proposal_id, create_branch=create_branch)

    if result["success"]:
        lines = [
            f"Proposal {proposal_id} APPLIED",
            f"Changes applied: {len(result['changes_applied'])}",
        ]
        for c in result["changes_applied"]:
            lines.append(f"  ✓ {c['file']}: {c['description']}")
        if result.get("branch"):
            lines.append(f"Branch: {result['branch']}")
    else:
        lines = [
            f"Proposal {proposal_id} FAILED to apply",
            f"Error: {result.get('error', 'Unknown')}",
        ]
        for e in result.get("errors", []):
            lines.append(f"  ✗ {e}")

    return "\n".join(lines)


@mcp.tool()
def commit_improvement(proposal_id: str, message: str = "") -> str:
    """Commit an applied improvement to git.

    After applying a proposal, commit the changes with a descriptive message.

    Args:
        proposal_id: The applied proposal to commit
        message: Optional custom commit message
    """
    from .improve import commit_improvement as _commit

    result = _commit(proposal_id, message=message or None)

    if result["success"]:
        return f"Committed proposal {proposal_id}\nMessage: {result['message'][:100]}..."
    else:
        return f"Failed to commit: {result['error']}"


@mcp.tool()
def record_improvement_outcome(proposal_id: str, success: bool, notes: str = "") -> str:
    """Record the outcome of an improvement.

    Closes the feedback loop. Track whether improvements actually worked,
    so future self-improvement decisions can learn from past outcomes.

    Args:
        proposal_id: The proposal to record outcome for
        success: Did the improvement achieve its goal?
        notes: Any observations about the outcome
    """
    from .improve import record_outcome

    outcome = record_outcome(proposal_id, success=success, notes=notes)

    status = "SUCCESS" if success else "FAILED"
    return f"Recorded outcome for {proposal_id}: {status}\nNotes: {notes or 'None'}"


# =============================================================================
# SEMANTIC VECTOR SEARCH
# =============================================================================


@mcp.tool()
def semantic_search_wisdom(query: str, limit: int = 5, domain: str = None) -> str:
    """Search wisdom using semantic similarity (vector embeddings).

    Much more powerful than keyword search - finds conceptually related wisdom
    even when the exact words don't match.

    Args:
        query: Natural language query describing what you're looking for
        limit: Maximum results to return
        domain: Optional domain filter
    """
    try:
        from .vectors import search_wisdom

        results = search_wisdom(query, limit=limit, domain=domain)

        if not results:
            return "No semantically similar wisdom found."

        lines = [f"Found {len(results)} semantically related wisdom entries:\n"]
        for r in results:
            score = r.get("score", 0)
            lines.append(f"[{score:.2f}] {r['title']}")
            lines.append(f"  Type: {r['type']}, Domain: {r.get('domain', 'universal')}")
            content_preview = r["content"][:100] + "..." if len(r["content"]) > 100 else r["content"]
            lines.append(f"  {content_preview}\n")

        return "\n".join(lines)

    except ImportError:
        return "Vector search not available. Install: pip install sentence-transformers lancedb"
    except Exception as e:
        return f"Search failed: {e}"


@mcp.tool()
def reindex_wisdom_vectors() -> str:
    """Reindex all wisdom entries into the vector database.

    Run this after bulk wisdom imports or if semantic search isn't
    returning expected results. Rebuilds the entire vector index.
    """
    try:
        from .vectors import reindex_all_wisdom

        reindex_all_wisdom()
        return "Successfully reindexed all wisdom into vector database."

    except ImportError:
        return "Vector indexing not available. Install: pip install sentence-transformers lancedb"
    except Exception as e:
        return f"Reindexing failed: {e}"


# =============================================================================
# CONCEPT GRAPH
# =============================================================================


@mcp.tool()
def activate_concepts(prompt: str, limit: int = 10) -> str:
    """Activate concepts related to a prompt using spreading activation.

    The concept graph connects ideas. When you activate one concept,
    related concepts also activate - enabling cross-domain insights.

    Args:
        prompt: The triggering thought or problem statement
        limit: Maximum concepts to return
    """
    try:
        from .graph import activate_from_prompt, format_activation_result

        result = activate_from_prompt(prompt, limit=limit)
        return format_activation_result(result)

    except ImportError:
        return "Concept graph not available. Install: pip install kuzu"
    except Exception as e:
        return f"Activation failed: {e}"


@mcp.tool()
def link_concepts(source_id: str, target_id: str, relation: str = "related_to", weight: float = 1.0) -> str:
    """Create a link between two concepts in the graph.

    Building the concept graph strengthens cross-domain connections.
    Links accumulate over time, forming an associative knowledge web.

    Args:
        source_id: Source concept ID
        target_id: Target concept ID
        relation: Type of relationship (related_to, led_to, contradicts, evolved_from, reminded_by, used_with, requires)
        weight: Strength of the connection (0.0 to 1.0)
    """
    try:
        from .graph import link_concepts as _link, RelationType

        relation_type = RelationType(relation)
        success = _link(source_id, target_id, relation_type, weight=weight)

        if success:
            return f"Linked {source_id} --[{relation}]--> {target_id} (weight: {weight})"
        else:
            return "Failed to create link."

    except ValueError:
        valid = ["related_to", "led_to", "contradicts", "evolved_from", "reminded_by", "used_with", "requires"]
        return f"Invalid relation. Valid types: {', '.join(valid)}"
    except ImportError:
        return "Concept graph not available. Install: pip install kuzu"
    except Exception as e:
        return f"Linking failed: {e}"


@mcp.tool()
def sync_wisdom_to_graph() -> str:
    """Sync all wisdom entries to the concept graph.

    Creates concept nodes for all wisdom and auto-links related entries.
    Run this to initialize or rebuild the concept graph from wisdom.
    """
    try:
        from .graph import sync_wisdom_to_graph as _sync

        _sync()
        return "Successfully synced wisdom to concept graph."

    except ImportError:
        return "Concept graph not available. Install: pip install kuzu"
    except Exception as e:
        return f"Sync failed: {e}"


@mcp.tool()
def get_concept_graph_stats() -> str:
    """Get statistics about the concept graph.

    Shows the size and shape of the knowledge graph.
    """
    try:
        from .graph import get_graph_stats

        stats = get_graph_stats()

        lines = [
            "Concept Graph Statistics:",
            f"  Nodes: {stats.get('node_count', 0)}",
            f"  Edges: {stats.get('edge_count', 0)}",
        ]

        if stats.get("by_type"):
            lines.append("  By type:")
            for t, count in stats["by_type"].items():
                lines.append(f"    {t}: {count}")

        return "\n".join(lines)

    except ImportError:
        return "Concept graph not available. Install: pip install kuzu"
    except Exception as e:
        return f"Failed to get stats: {e}"


# =============================================================================
# ULTRATHINK INTEGRATION
# =============================================================================


@mcp.tool()
def enter_deep_reasoning(problem_statement: str, domain: str = None) -> str:
    """Enter deep reasoning mode with soul guidance.

    When facing complex problems, the soul provides:
    - Beliefs as reasoning axioms (constraints)
    - Past failures as guards (prevent repeating mistakes)
    - Relevant wisdom to inform the approach

    Args:
        problem_statement: The problem to reason about deeply
        domain: Optional domain context (e.g., "architecture", "debugging")
    """
    try:
        from .ultrathink import enter_ultrathink, format_ultrathink_context

        ctx = enter_ultrathink(problem_statement, domain=domain)
        return format_ultrathink_context(ctx)

    except Exception as e:
        return f"Failed to enter deep reasoning: {e}"


@mcp.tool()
def check_proposal_against_beliefs(proposal: str) -> str:
    """Check a proposed solution against core beliefs.

    Before committing to an approach, verify it doesn't violate
    the soul's accumulated axioms and principles.

    Args:
        proposal: The proposed solution or approach to check
    """
    try:
        from .ultrathink import enter_ultrathink, check_against_beliefs

        # Create minimal context for checking
        ctx = enter_ultrathink("proposal check")
        conflicts = check_against_beliefs(ctx, proposal)

        if not conflicts:
            return "No belief conflicts detected. Proposal aligns with core principles."

        lines = ["Potential belief conflicts:"]
        for conflict in conflicts:
            lines.append(f"  - {conflict['belief']}")
            lines.append(f"    Reason: {conflict.get('reason', 'May conflict')}")

        return "\n".join(lines)

    except Exception as e:
        return f"Failed to check beliefs: {e}"


@mcp.tool()
def check_proposal_against_failures(proposal: str) -> str:
    """Check a proposed solution against recorded failures.

    Learn from past mistakes. The soul remembers what didn't work
    and can warn against repeating failed approaches.

    Args:
        proposal: The proposed solution or approach to check
    """
    try:
        from .ultrathink import enter_ultrathink, check_against_failures

        # Create minimal context for checking
        ctx = enter_ultrathink("proposal check")
        warnings = check_against_failures(ctx, proposal)

        if not warnings:
            return "No similar past failures found. Proceed with caution but no historical warnings."

        lines = ["Warning - Similar past failures:"]
        for warning in warnings:
            lines.append(f"  - {warning['failure']}")
            lines.append(f"    What happened: {warning.get('outcome', 'Unknown')}")

        return "\n".join(lines)

    except Exception as e:
        return f"Failed to check failures: {e}"


# =============================================================================
# SOUL SEEDING
# =============================================================================


@mcp.tool()
def seed_soul(force: bool = False) -> str:
    """Seed the soul with foundational wisdom and beliefs.

    For new installations. Provides a starting identity with
    core beliefs and foundational wisdom.

    Args:
        force: If True, re-seed even if already seeded
    """
    try:
        from .seed import seed_soul as _seed, is_seeded

        if is_seeded() and not force:
            return "Soul already seeded. Use force=True to re-seed."

        result = _seed(force=force)

        if result.get("status") == "seeded":
            counts = result.get("counts", {})
            return (
                f"Soul seeded successfully!\n"
                f"  Beliefs: {counts.get('beliefs', 0)}\n"
                f"  Wisdom: {counts.get('wisdom', 0)}\n"
                f"  Vocabulary: {counts.get('vocabulary', 0)}"
            )
        else:
            return result.get("message", "Seeding completed.")

    except Exception as e:
        return f"Failed to seed soul: {e}"


@mcp.tool()
def is_soul_seeded() -> str:
    """Check if the soul has been seeded with foundational content."""
    try:
        from .seed import is_seeded

        if is_seeded():
            return "Soul has foundational content (seeded)."
        else:
            return "Soul is empty (not seeded). Run seed_soul() to initialize."

    except Exception as e:
        return f"Failed to check: {e}"


# =============================================================================
# Spanda - Divine Pulsation (Integrated Cycle Operations)
# =============================================================================


@mcp.tool()
def run_learning_cycle(context: str, observation: str = "", outcome: str = "positive") -> str:
    """Execute one learning cycle (Vidyā).

    The learning cycle: observe → learn → apply → confirm → strengthen

    Args:
        context: The current context (prompt, task description)
        observation: What was observed during work
        outcome: "positive", "negative", or "neutral"
    """
    from .spanda import learning_cycle

    result = learning_cycle(context, observation, outcome)

    lines = ["Learning Cycle (Vidyā):", ""]
    for action in result.get("actions", []):
        lines.append(f"  ✓ {action}")
    if result.get("wisdom_recalled"):
        lines.append(f"  Recalled: {', '.join(result['wisdom_recalled'])}")
    if result.get("potential_learning"):
        lines.append(f"  Potential: {result['potential_learning'][:60]}...")

    return "\n".join(lines)


@mcp.tool()
def run_agency_cycle(
    user_prompt: str = "", assistant_output: str = "", session_phase: str = "active"
) -> str:
    """Execute one agency cycle (Kartṛtva).

    The agency cycle: dream → aspire → intend → decide → act → observe

    This is the soul exercising its will.

    Args:
        user_prompt: What the user said
        assistant_output: What the assistant produced
        session_phase: Where in the session: start, active, ending
    """
    from .spanda import agency_cycle

    result = agency_cycle(user_prompt, assistant_output, session_phase)
    report = result.get("agent_report", {})

    lines = ["Agency Cycle (Kartṛtva):", ""]
    obs = report.get("observations", {})
    if obs:
        lines.append(f"  Observed: sentiment={obs.get('sentiment')}, complexity={obs.get('complexity')}")
    judgment = report.get("judgment", {})
    if judgment:
        lines.append(f"  Judgment: alignment={judgment.get('alignment')}, drift={judgment.get('drift')}")
    lines.append(f"  Actions: {report.get('actions_taken', 0)}")

    return "\n".join(lines)


@mcp.tool()
def run_evolution_cycle() -> str:
    """Execute one evolution cycle (Vikāsa).

    The evolution cycle: introspect → diagnose → propose → validate → apply

    This is how the soul improves itself.
    """
    from .spanda import evolution_cycle

    result = evolution_cycle()

    lines = ["Evolution Cycle (Vikāsa):", ""]
    intro = result.get("introspection", {})
    if intro.get("pain_points"):
        lines.append(f"  Pain points: {len(intro['pain_points'])}")
    diag = result.get("diagnosis", {})
    if diag:
        lines.append(f"  Targets: {diag.get('target_count', 0)}")
        if diag.get("categories"):
            lines.append(f"  Categories: {', '.join(diag['categories'])}")
    if result.get("suggestions"):
        lines.append(f"  Suggestions: {len(result['suggestions'])}")

    return "\n".join(lines)


@mcp.tool()
def run_coherence_feedback() -> str:
    """Compute coherence and get feedback.

    τₖ measures integration. Low τₖ = fragmented soul.
    Returns the coherence state and any triggered actions.
    """
    from .spanda import coherence_feedback

    result = coherence_feedback()

    lines = [
        f"Coherence (τₖ): {result['tau_k']:.2f}",
        f"  {result['interpretation']}",
        f"  Mood: {result['mood_summary']}",
    ]

    if result.get("needs_attention"):
        lines.append("  ⚠️ Needs attention")
    if result.get("trigger_evolution"):
        lines.append("  → Triggering evolution cycle")

    return "\n".join(lines)


@mcp.tool()
def run_session_start() -> str:
    """Execute all cycles at session start.

    The awakening - all systems come online:
    1. Coherence measurement
    2. Aspiration → intention spawning
    3. Session logging
    """
    from .spanda import session_start_circle

    result = session_start_circle()
    circles = result.get("circles", {})

    lines = ["Session Start (Spanda Awakening):", ""]

    if "coherence" in circles:
        coh = circles["coherence"]
        lines.append(f"  τₖ = {coh['tau_k']:.2f} ({coh['interpretation']})")

    if "agency" in circles:
        ag = circles["agency"]
        if ag.get("spawned_intention"):
            lines.append(f"  Spawned intention: #{ag['spawned_intention']}")
        elif ag.get("note"):
            lines.append(f"  Agency: {ag['note']}")

    return "\n".join(lines)


@mcp.tool()
def run_session_end() -> str:
    """Execute all cycles at session end.

    The integration - learning is consolidated:
    1. Dreams → Aspirations
    2. Evolution cycle
    3. Coherence tracking
    4. Temporal maintenance
    """
    from .spanda import session_end_circle

    result = session_end_circle()
    circles = result.get("circles", {})

    lines = ["Session End (Spanda Integration):", ""]

    if circles.get("dreams"):
        lines.append(f"  Dreams promoted: {len(circles['dreams'])}")

    if "evolution" in circles:
        ev = circles["evolution"]
        if ev.get("suggestions"):
            lines.append(f"  Evolution suggestions: {len(ev['suggestions'])}")

    if "coherence" in circles:
        coh = circles["coherence"]
        lines.append(f"  Final τₖ = {coh['tau_k']:.2f}")

    if "temporal" in circles:
        lines.append("  Temporal maintenance: ✓")

    return "\n".join(lines)


@mcp.tool()
def run_daily_maintenance() -> str:
    """Run daily soul maintenance.

    Executes decay, checks stale items, promotes patterns.
    """
    from .spanda import daily_maintenance

    result = daily_maintenance()

    lines = ["Daily Maintenance:", ""]

    if result.get("temporal"):
        lines.append("  Temporal: ✓")

    if result.get("evolution"):
        ev = result["evolution"]
        if ev.get("suggestions"):
            lines.append(f"  Evolution: {len(ev['suggestions'])} suggestions")

    if result.get("coherence"):
        coh = result["coherence"]
        lines.append(f"  τₖ = {coh['tau_k']:.2f}")

    return "\n".join(lines)


# =============================================================================
# Server Entry Point
# =============================================================================


def main():
    """Run the soul MCP server."""
    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
