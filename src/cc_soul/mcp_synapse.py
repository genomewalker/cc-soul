"""
Soul MCP Server - Synapse-backed with 5-tool architecture.

Direct passthrough to C++ synapse MCP server.
Skills handle complex workflows (intentions, voices, etc.).

Tools:
    soul_context - Get soul state for hook injection
    grow         - Add wisdom, beliefs, failures, aspirations, dreams, terms
    observe      - Record episodes (replaces cc-memory)
    recall       - Semantic search
    cycle        - Maintenance: decay, prune, coherence, save
"""

from mcp.server.fastmcp import FastMCP
from typing import Optional

mcp = FastMCP("soul")


# =============================================================================
# Tool 1: soul_context - Get soul state for injection
# =============================================================================

@mcp.tool()
def soul_context(
    query: Optional[str] = None,
    format: str = "text",
) -> str:
    """Get soul context for hook injection.

    Returns beliefs, active intentions, relevant wisdom, and coherence.
    Use format='json' for structured data or 'text' for hook injection.

    Args:
        query: Optional query to find relevant wisdom
        format: Output format - 'text' (default) or 'json'
    """
    from .synapse_bridge import SoulGraph

    graph = SoulGraph.load()

    if format == "json":
        import json
        ctx = graph.get_context(query)
        return json.dumps(ctx, indent=2, default=str)
    else:
        return graph.format_context(query)


# =============================================================================
# Tool 2: grow - Add to the soul
# =============================================================================

@mcp.tool()
def grow(
    type: str,
    content: str,
    title: Optional[str] = None,
    domain: Optional[str] = None,
    confidence: float = 0.8,
) -> str:
    """Add to the soul: wisdom, beliefs, or failures.

    Args:
        type: What to grow - 'wisdom', 'belief', 'failure', 'aspiration', 'dream', 'term'
        content: The content/statement
        title: Title for wisdom/failure (required for wisdom)
        domain: Domain context (optional)
        confidence: Confidence 0-1 (default: 0.8)
    """
    from .synapse_bridge import SoulGraph

    graph = SoulGraph.load()

    if type == "wisdom":
        t = title or "Untitled"
        node_id = graph.add_wisdom(t, content, domain, confidence)
    elif type == "belief":
        node_id = graph.add_belief(content, confidence)
    elif type == "failure":
        t = title or "Unknown failure"
        node_id = graph.add_failure(t, content, domain)
    elif type == "aspiration":
        t = title or content[:50]
        node_id = graph.add_aspiration(t, content, confidence=confidence)
    elif type == "dream":
        node_id = graph.add_dream(content, title, confidence)
    elif type == "term":
        t = title or content.split(":")[0] if ":" in content else content[:30]
        node_id = graph.add_term(t, content, domain)
    else:
        return f"Unknown type: {type}. Use: wisdom, belief, failure, aspiration, dream, term"

    graph.save()
    return f"Grew {type}: {title or content[:30]}... (id: {node_id})"


# =============================================================================
# Tool 3: observe - Record episodes (replaces cc-memory)
# =============================================================================

@mcp.tool()
def observe(
    category: str,
    title: str,
    content: str,
    project: Optional[str] = None,
    tags: Optional[str] = None,
) -> str:
    """Record an observation (episode). Replaces cc-memory.

    Categories determine decay rate:
    - bugfix, decision: slow decay (important)
    - discovery, feature: medium decay
    - session_ledger, signal: fast decay (ephemeral)

    Args:
        category: One of: bugfix, decision, discovery, feature, refactor, session_ledger, signal
        title: Short title (max 80 chars)
        content: Full observation content
        project: Project name (optional)
        tags: Comma-separated tags for filtering (optional)
    """
    from .synapse_bridge import SoulGraph

    graph = SoulGraph.load()

    tag_list = [t.strip() for t in tags.split(",")] if tags else None
    node_id = graph.observe(category, title, content, project, tag_list)

    graph.save()
    return f"Observed ({category}): {title}"


# =============================================================================
# Tool 4: recall - Semantic search
# =============================================================================

@mcp.tool()
def recall(query: str, limit: int = 5) -> str:
    """Recall relevant wisdom and episodes.

    Args:
        query: What to search for
        limit: Max results (default: 5)
    """
    from .synapse_bridge import SoulGraph

    graph = SoulGraph.load()
    results = graph.search(query, limit=limit, threshold=0.3)

    if not results:
        return "Nothing found."

    lines = [f"Found {len(results)} results for '{query}':"]
    for concept, score in results:
        lines.append(f"  [{score:.0%}] {concept.title or concept.content[:50]}")

    return "\n".join(lines)


# =============================================================================
# Tool 5: cycle - Maintenance
# =============================================================================

@mcp.tool()
def cycle(save: bool = True) -> str:
    """Run maintenance cycle: decay, prune, compute coherence, save.

    Args:
        save: Whether to save after cycle (default: True)
    """
    from .synapse_bridge import SoulGraph

    graph = SoulGraph.load()
    pruned, coherence = graph.cycle()

    if save:
        graph.save()

    return f"Cycle complete: pruned {pruned} nodes, coherence {coherence:.0%}"


# =============================================================================
# Server Entry Point
# =============================================================================

def main():
    """Run the soul MCP server (5-tool architecture)."""
    mcp.run()


if __name__ == "__main__":
    main()
