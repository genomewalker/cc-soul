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
