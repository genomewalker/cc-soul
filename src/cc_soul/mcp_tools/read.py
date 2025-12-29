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
    """Check soul system health and vitality.

    Comprehensive health check covering:
    - Core: Database, wisdom, beliefs, identity
    - Infrastructure: Embeddings, LanceDB, Kuzu graph
    - Integration: cc-memory bridge, budget tracking
    - Agency: Active swarms, agent patterns
    """
    from .core import SOUL_DB, SOUL_DIR, get_db_connection
    import sqlite3

    lines = ["# Soul Health", ""]
    checks = []  # (category, name, status, detail)

    def check(category, name, fn):
        try:
            status, detail = fn()
            checks.append((category, name, status, detail))
        except Exception as e:
            checks.append((category, name, "FAIL", str(e)[:50]))

    # ═══════════════════════════════════════════════════════════════
    # CORE - Soul database and content
    # ═══════════════════════════════════════════════════════════════

    def check_database():
        if not SOUL_DB.exists():
            return "FAIL", "Database not found"
        conn = get_db_connection()
        cursor = conn.execute("SELECT COUNT(*) FROM wisdom")
        count = cursor.fetchone()[0]
        return "OK", f"{count} wisdom entries"

    def check_beliefs():
        conn = get_db_connection()
        cursor = conn.execute("SELECT COUNT(*) FROM wisdom WHERE type='principle'")
        count = cursor.fetchone()[0]
        return "OK" if count > 0 else "WARN", f"{count} beliefs"

    def check_identity():
        conn = get_db_connection()
        try:
            cursor = conn.execute("SELECT COUNT(*) FROM identity")
            count = cursor.fetchone()[0]
            return "OK" if count > 0 else "WARN", f"{count} aspects"
        except sqlite3.OperationalError:
            return "WARN", "table not created"

    check("core", "database", check_database)
    check("core", "beliefs", check_beliefs)
    check("core", "identity", check_identity)

    # ═══════════════════════════════════════════════════════════════
    # INFRASTRUCTURE - Embeddings, vector DB, graph DB
    # ═══════════════════════════════════════════════════════════════

    def check_embeddings():
        from .vectors import embed_text
        vec = embed_text("test")
        return "OK", f"dim={len(vec)}"

    def check_lancedb():
        import lancedb
        lance_dir = SOUL_DIR / "vectors" / "lancedb"
        lance_dir.mkdir(parents=True, exist_ok=True)
        db = lancedb.connect(str(lance_dir))
        tables = db.table_names()
        return "OK", f"{len(tables)} tables"

    def check_kuzu():
        try:
            import kuzu  # noqa: F401
            return "OK", "available"
        except ImportError:
            return "WARN", "not installed"

    check("infra", "embeddings", check_embeddings)
    check("infra", "lancedb", check_lancedb)
    check("infra", "kuzu", check_kuzu)

    # ═══════════════════════════════════════════════════════════════
    # INTEGRATION - cc-memory bridge, budget tracking
    # ═══════════════════════════════════════════════════════════════

    def check_cc_memory_bridge():
        from .bridge import is_memory_available
        if is_memory_available():
            from .bridge import find_project_dir
            project = find_project_dir()
            return "OK", f"connected ({project.name if project else 'global'})"
        return "WARN", "not available"

    def check_budget_tracking():
        from .budget import get_context_budget, get_all_session_budgets
        budget = get_context_budget()
        sessions = get_all_session_budgets()
        if budget:
            pct = int(budget.remaining_pct * 100)
            return "OK", f"{pct}% remaining, {len(sessions)} sessions tracked"
        return "WARN", f"transcript unavailable, {len(sessions)} sessions tracked"

    def check_hooks():
        from pathlib import Path
        import json
        settings_path = Path.home() / ".claude" / "settings.json"
        if not settings_path.exists():
            return "FAIL", "settings.json not found"
        with open(settings_path) as f:
            settings = json.load(f)
        hooks = settings.get("hooks", {})
        required = ["SessionStart", "SessionEnd", "UserPromptSubmit", "PreCompact"]
        installed = [h for h in required if h in hooks]
        if len(installed) == len(required):
            return "OK", f"{len(installed)}/{len(required)} hooks"
        return "WARN", f"{len(installed)}/{len(required)} hooks"

    check("integration", "cc-memory", check_cc_memory_bridge)
    check("integration", "budget", check_budget_tracking)
    check("integration", "hooks", check_hooks)

    # ═══════════════════════════════════════════════════════════════
    # AGENCY - Active swarms, agent patterns, proactivity
    # ═══════════════════════════════════════════════════════════════

    def check_active_swarms():
        from .convergence import list_active_swarms
        swarms = list_active_swarms()
        active = [s for s in swarms if s.get("status") != "completed"]
        return "OK", f"{len(active)} active, {len(swarms)} total"

    def check_agent_actions():
        conn = get_db_connection()
        try:
            cursor = conn.execute(
                "SELECT COUNT(*) FROM agent_actions WHERE timestamp > datetime('now', '-24 hours')"
            )
            count = cursor.fetchone()[0]
            return "OK", f"{count} actions (24h)"
        except sqlite3.OperationalError:
            return "OK", "no actions yet"

    def check_intentions():
        from .intentions import get_active_intentions
        intentions = get_active_intentions()
        return "OK" if intentions else "WARN", f"{len(intentions)} active"

    check("agency", "swarms", check_active_swarms)
    check("agency", "agent_actions", check_agent_actions)
    check("agency", "intentions", check_intentions)

    # ═══════════════════════════════════════════════════════════════
    # FORMAT OUTPUT
    # ═══════════════════════════════════════════════════════════════

    status_icons = {"OK": "✓", "WARN": "⚠", "FAIL": "✗"}
    current_category = None

    for category, name, status, detail in checks:
        if category != current_category:
            lines.append(f"\n## {category.upper()}")
            current_category = category
        icon = status_icons.get(status, "?")
        lines.append(f"  {icon} **{name}**: {detail}")

    # Overall status
    fails = sum(1 for _, _, s, _ in checks if s == "FAIL")
    warns = sum(1 for _, _, s, _ in checks if s == "WARN")

    lines.append("")
    if fails > 0:
        lines.append(f"**Status**: UNHEALTHY ({fails} failures, {warns} warnings)")
    elif warns > 0:
        lines.append(f"**Status**: DEGRADED ({warns} warnings)")
    else:
        lines.append("**Status**: HEALTHY")

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
