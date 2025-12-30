# =============================================================================
# Self-Introspection - The Soul Examines Its Own Code
# =============================================================================

@mcp.tool()
def introspect_codebase(depth: str = "standard", verbose: bool = False) -> str:
    """Run soul self-introspection on the codebase.

    The soul examines its own code using multiple perspectives:
    - Manas (quick): Surface scan for obvious issues
    - Buddhi (standard): + architectural analysis
    - Ahamkara (deep): + critical flaw detection
    - Sakshi (ultrathink): + belief coherence, evolution proposals

    Args:
        depth: quick, standard, deep, or ultrathink
        verbose: Include all issues in output
    """
    from ..self_introspection import run_introspection, format_report

    if depth not in ("quick", "standard", "deep", "ultrathink"):
        return f"Invalid depth: {depth}. Use: quick, standard, deep, or ultrathink"

    report = run_introspection(depth=depth)
    return format_report(report, verbose=verbose)


@mcp.tool()
def get_code_issues(severity: str = None, category: str = None, limit: int = 20) -> str:
    """Get detected code issues from last introspection.

    Args:
        severity: Filter by severity (low, medium, high, critical)
        category: Filter by category (bug, smell, optimization, missing, etc.)
        limit: Maximum issues to return
    """
    from ..self_introspection import run_introspection, IssueSeverity, IssueCategory

    report = run_introspection(depth="standard")

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

    lines = ["Code Issues", "═" * 40, ""]
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
    """Get code that violates soul beliefs.

    Identifies where the codebase contradicts stated beliefs like
    'Simplicity over cleverness'.
    """
    from ..self_introspection import run_introspection

    report = run_introspection(depth="deep")

    if not report.belief_violations:
        return "No belief violations detected. Code aligns with soul principles."

    lines = ["Belief Violations", "═" * 40, ""]
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
    """Get metrics about the soul codebase.

    Shows lines of code, complexity, type hint coverage,
    docstring coverage per module.
    """
    from ..self_introspection import run_introspection

    report = run_introspection(depth="quick")

    lines = [
        "Codebase Metrics",
        "═" * 60,
        f"Modules: {report.modules_scanned}",
        f"Total lines: {report.total_lines:,}",
        "",
        "Per-Module Breakdown:",
        "-" * 60,
    ]

    # Sort by lines descending
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
def get_optimization_opportunities() -> str:
    """Get opportunities to optimize the codebase.

    Identifies high-complexity functions, large files, and
    other candidates for refactoring.
    """
    from ..self_introspection import run_introspection

    report = run_introspection(depth="deep")

    if not report.optimization_opportunities:
        return "No optimization opportunities identified."

    lines = ["Optimization Opportunities", "═" * 40, ""]
    for opt in report.optimization_opportunities:
        lines.append(f"• {opt}")
        lines.append("")

    return "\n".join(lines)


@mcp.tool()
def ultrathink_introspect() -> str:
    """Run the deepest introspection - ultrathink level.

    Performs comprehensive analysis across all perspectives:
    1. Surface scan (Manas)
    2. Architecture analysis (Buddhi)
    3. Critical flaw detection (Ahamkara)
    4. Missing feature identification (Vikalpa)
    5. Essential truth synthesis (Sakshi)

    Generates evolution proposals for self-improvement.
    """
    from ..self_introspection import run_introspection, format_report

    report = run_introspection(depth="ultrathink")

    lines = [format_report(report, verbose=False)]

    if report.evolution_proposals:
        lines.append("")
        lines.append("GENERATED EVOLUTION PROPOSALS")
        lines.append("=" * 40)
        for prop in report.evolution_proposals:
            lines.append(f"📋 {prop['title']}")
            lines.append(f"   Category: {prop['category']}")
            lines.append(f"   Priority: {prop['priority']}")
            lines.append(f"   Files: {', '.join(prop.get('files', []))}")
            lines.append(f"   {prop['description'][:200]}...")
            lines.append("")

    return "\n".join(lines)
