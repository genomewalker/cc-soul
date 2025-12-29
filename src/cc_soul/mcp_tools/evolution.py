# =============================================================================
# Evolution - Self-Improvement Through Reflection
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
