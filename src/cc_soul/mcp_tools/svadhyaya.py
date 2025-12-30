# =============================================================================
# Svadhyaya (स्वाध्याय) - Self-Study MCP Tools
# =============================================================================
#
# Unified introspection tools using Vedantic philosophy:
#   - Darshana (दर्शन) - Vision into code
#   - Jnana (ज्ञान) - Knowledge health
#   - Vedana (वेदना) - Pain sensation
#   - Prajna (प्रज्ञा) - Deep wisdom
#   - Vichara (विचार) - Autonomous inquiry
#

@mcp.tool()
def svadhyaya_full(depth: str = "standard") -> str:
    """Run complete Svadhyaya (self-study) at specified depth.

    Svadhyaya combines all forms of introspection:
    - Darshana (vision): Code analysis
    - Jnana (knowledge): Wisdom health
    - Vedana (sensation): Pain points
    - Prajna (deep wisdom): Cross-session trends
    - Vichara (inquiry): Autonomous action

    Depths:
        quick: Darshana only
        standard: Darshana + Jnana
        deep: All perspectives
        ultrathink: Deep + autonomous inquiry

    Args:
        depth: quick, standard, deep, or ultrathink
    """
    from ..svadhyaya import svadhyaya, format_svadhyaya

    if depth not in ("quick", "standard", "deep", "ultrathink"):
        return f"Invalid depth: {depth}. Use: quick, standard, deep, or ultrathink"

    report = svadhyaya(depth)
    return format_svadhyaya(report)


@mcp.tool()
def darshana_codebase(depth: str = "standard", verbose: bool = False) -> str:
    """Run Darshana (code vision) on the soul codebase.

    Darshana uses the Antahkarana (inner instrument) voices:
    - Manas (quick): Surface scan, obvious issues
    - Buddhi (standard): + architectural analysis
    - Ahamkara (deep): + critical flaw detection
    - Sakshi (ultrathink): + belief coherence, evolution proposals

    Args:
        depth: quick, standard, deep, or ultrathink
        verbose: Include all issues in output
    """
    from ..svadhyaya import darshana, format_darshana

    if depth not in ("quick", "standard", "deep", "ultrathink"):
        return f"Invalid depth: {depth}. Use: quick, standard, deep, or ultrathink"

    report = darshana(depth)
    return format_darshana(report, verbose=verbose)


@mcp.tool()
def jnana_health() -> str:
    """Run Jnana (knowledge health) analysis.

    Examines wisdom health:
    - Decay: Wisdom losing confidence from inactivity
    - Staleness: Wisdom never applied
    - Success rates: Which wisdom consistently works
    - Coverage: Types and domains represented
    """
    from ..svadhyaya import jnana, format_jnana

    report = jnana()
    return format_jnana(report)


@mcp.tool()
def vedana_points(category: str = None, limit: int = 20) -> str:
    """Get Vedana (pain) points - operational friction.

    Categories: latency, error, missing, friction, inconsistency

    Args:
        category: Filter by category (optional)
        limit: Maximum points to return
    """
    from ..svadhyaya import get_vedana, analyze_vedana

    if category:
        points = get_vedana(category=category, limit=limit)
    else:
        points = get_vedana(limit=limit)

    if not points:
        return "No pain points recorded. The soul feels no friction."

    lines = [
        "Vedana (वेदना) - Pain Points",
        "═" * 40,
        "",
    ]

    for p in points:
        icon = {"critical": "🔴", "high": "🟠", "medium": "🟡", "low": "🟢"}.get(p["severity"], "⚪")
        lines.append(f"{icon} [{p['category']}] {p['description'][:60]}")
        lines.append(f"   {p['timestamp'][:10]}")
        lines.append("")

    analysis = analyze_vedana()
    lines.extend([
        "-" * 40,
        f"Total open: {analysis['total_open']}",
        f"By category: {analysis['by_category']}",
    ])

    return "\n".join(lines)


@mcp.tool()
def prajna_trends(days: int = 90) -> str:
    """Run Prajna (deep wisdom) analysis of growth trajectory.

    Analyzes:
    - Wisdom accumulation curve
    - Learning velocity (wisdom per week)
    - Domain expansion
    - Belief evolution

    Args:
        days: Number of days to analyze
    """
    from ..svadhyaya import prajna_trajectory, prajna_patterns

    trajectory = prajna_trajectory(days)
    patterns = prajna_patterns()

    lines = [
        "Prajna (प्रज्ञा) - Deep Wisdom Trends",
        "═" * 50,
        "",
        f"Period: {trajectory['period_days']} days",
        f"Total wisdom gained: {trajectory['total_wisdom_gained']}",
        f"Domains explored: {trajectory['total_domains']}",
        f"Weekly velocity: {trajectory['avg_weekly_velocity']} wisdom/week",
        f"Recent velocity: {trajectory['recent_velocity']} ({trajectory['velocity_trend']})",
        "",
    ]

    if trajectory.get("trajectory"):
        lines.append("Weekly Progress:")
        for t in trajectory["trajectory"][-8:]:
            bar = "█" * min(20, t["gained"])
            new_d = f" +{len(t['new_domains'])}d" if t["new_domains"] else ""
            lines.append(f"  {t['week']}: {bar} {t['gained']}{new_d}")
        lines.append("")

    lines.extend([
        "Learning Patterns:",
        f"  Dominant type: {patterns['dominant_type']}",
        f"  Growing domains: {', '.join(patterns['growing_domains'])}",
    ])

    if patterns["temporal_patterns"]["peak_hour"] is not None:
        lines.append(
            f"  Peak learning: {patterns['temporal_patterns']['peak_day']}s at {patterns['temporal_patterns']['peak_hour']}:00"
        )

    return "\n".join(lines)


@mcp.tool()
def vichara_run() -> str:
    """Run Vichara (autonomous self-inquiry).

    The soul examines itself and takes autonomous action:
    OBSERVE → DIAGNOSE → PROPOSE → VALIDATE → APPLY → REFLECT

    High confidence + low risk actions are executed immediately.
    Uncertain actions are deferred for data gathering.
    """
    from ..svadhyaya import vichara, format_vichara

    report = vichara()
    return format_vichara(report)


@mcp.tool()
def vichara_stats() -> str:
    """Get statistics about Vichara (autonomous action) history.

    Shows what actions the soul has taken autonomously,
    success rates, and pending observations.
    """
    from ..svadhyaya import get_autonomy_stats

    stats = get_autonomy_stats()

    if stats["total_actions"] == 0:
        return "No autonomous actions taken yet. The soul has been passive."

    lines = [
        "Vichara (विचार) - Autonomy Statistics",
        "═" * 40,
        "",
        f"Total autonomous actions: {stats['total_actions']}",
        f"Successes: {stats['successes']}",
        f"Success rate: {stats['success_rate']:.0%}" if stats['success_rate'] else "N/A",
        f"Last self-study: {stats['last_svadhyaya'] or 'Never'}",
        f"Pending observations: {stats['pending_observations']}",
        "",
        "Actions by type:",
    ]

    for action_type, count in stats.get("by_type", {}).items():
        lines.append(f"  {action_type}: {count}")

    return "\n".join(lines)


@mcp.tool()
def get_code_issues(severity: str = None, category: str = None, limit: int = 20) -> str:
    """Get detected code issues from Darshana analysis.

    Args:
        severity: Filter by severity (low, medium, high, critical)
        category: Filter by category (bug, smell, optimization, missing, etc.)
        limit: Maximum issues to return
    """
    from ..svadhyaya import darshana, IssueSeverity, IssueCategory

    report = darshana("standard")
    issues = report.issues

    if severity:
        try:
            sev = IssueSeverity(severity)
            issues = [i for i in issues if i.severity == sev]
        except ValueError:
            return f"Invalid severity: {severity}. Use: low, medium, high, critical"

    if category:
        try:
            cat = IssueCategory(category)
            issues = [i for i in issues if i.category == cat]
        except ValueError:
            return f"Invalid category: {category}. Use: bug, smell, optimization, missing, security, documentation, test, coherence"

    if not issues:
        return "No issues found matching criteria."

    lines = ["Code Issues (Darshana)", "═" * 40, ""]
    for issue in issues[:limit]:
        icon = {"critical": "🔴", "high": "🟠", "medium": "🟡", "low": "🟢"}[issue.severity.value]
        lines.append(f"{icon} [{issue.severity.value}] {issue.file}:{issue.line}")
        lines.append(f"   {issue.category.value}: {issue.message}")
        if issue.suggestion:
            lines.append(f"   → {issue.suggestion}")
        if issue.related_belief:
            lines.append(f"   Violates: \"{issue.related_belief}\"")
        lines.append("")

    if len(report.issues) > limit:
        lines.append(f"... and {len(report.issues) - limit} more issues")

    return "\n".join(lines)


@mcp.tool()
def get_belief_violations() -> str:
    """Get code that violates soul beliefs (Satya alignment).

    Identifies where the codebase contradicts stated beliefs like
    'Simplicity over cleverness'.
    """
    from ..svadhyaya import darshana

    report = darshana("deep")

    if not report.belief_violations:
        return "No belief violations detected. Code aligns with Satya (truth)."

    lines = ["Belief Violations (Satya Alignment)", "═" * 40, ""]
    for v in report.belief_violations:
        lines.append(f"📍 {v.file}:{v.line}")
        lines.append(f"   Issue: {v.message}")
        lines.append(f"   Violates: \"{v.related_belief}\"")
        if v.suggestion:
            lines.append(f"   → {v.suggestion}")
        lines.append("")

    return "\n".join(lines)


@mcp.tool()
def get_codebase_metrics() -> str:
    """Get metrics about the soul codebase (Darshana summary).

    Shows lines of code, complexity, type hint coverage,
    docstring coverage per module.
    """
    from ..svadhyaya import darshana

    report = darshana("quick")

    lines = [
        "Codebase Metrics (Darshana)",
        "═" * 60,
        f"Modules: {report.modules_scanned}",
        f"Total lines: {report.total_lines:,}",
        "",
        "Per-Module Breakdown:",
        "-" * 60,
    ]

    sorted_metrics = sorted(report.metrics, key=lambda m: -m.lines)

    for m in sorted_metrics[:15]:
        lines.append(f"{m.file}")
        lines.append(f"  Lines: {m.lines}, Functions: {m.functions}, Classes: {m.classes}")
        lines.append(f"  Complexity: {m.avg_complexity:.1f}, Types: {m.type_hint_coverage:.0%}, Docs: {m.docstring_coverage:.0%}")
        lines.append("")

    if len(report.metrics) > 15:
        lines.append(f"... and {len(report.metrics) - 15} more modules")

    return "\n".join(lines)


@mcp.tool()
def record_vedana(category: str, description: str, severity: str = "medium") -> str:
    """Record a pain point (Vedana - friction sensation).

    Use this to track operational pain:
    - latency: Something was slow
    - error: Something failed
    - missing: Capability needed but absent
    - friction: User experience issue
    - inconsistency: Behavior didn't match expectations

    Args:
        category: latency, error, missing, friction, inconsistency
        description: What caused the pain
        severity: low, medium, high, critical
    """
    from ..svadhyaya import record_vedana as _record

    valid_categories = ["latency", "error", "missing", "friction", "inconsistency"]
    if category not in valid_categories:
        return f"Invalid category. Use: {', '.join(valid_categories)}"

    valid_severities = ["low", "medium", "high", "critical"]
    if severity not in valid_severities:
        return f"Invalid severity. Use: {', '.join(valid_severities)}"

    entry = _record(category, description, severity)
    return f"Recorded vedana: [{severity}] {category} - {description[:50]}..."


@mcp.tool()
def schedule_vichara(reason: str, priority: int = 5) -> str:
    """Schedule deep Vichara (self-inquiry) for next session.

    Use when an issue needs more thorough analysis than
    can be done in the current context.

    Args:
        reason: Why deep inquiry is needed
        priority: 1-10, higher = more urgent
    """
    from ..svadhyaya import schedule_vichara as _schedule

    _schedule(reason, priority)
    return f"Scheduled deep Vichara (priority {priority}): {reason}"
