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
