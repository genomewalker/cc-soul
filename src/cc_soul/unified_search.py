"""
Unified memory search via Synapse.

Synapse is the single source of truth for all soul data:
- Episodes (observations) with time-based decay
- Wisdom (permanent knowledge)
- Beliefs, failures, intentions

Semantic search across all stored knowledge.
"""

from typing import List, Dict, Optional
from datetime import datetime

from .core import get_synapse_graph


def search(
    query: str,
    limit: int = 10,
    threshold: float = 0.3,
) -> List[Dict]:
    """
    Semantic search across all synapse data.

    Returns list of results with score, title, content, metadata.
    """
    graph = get_synapse_graph()
    results = graph.search(query, limit=limit, threshold=threshold)

    normalized = []
    for concept, score in results:
        normalized.append({
            "id": concept.id,
            "title": concept.title,
            "content": concept.content,
            "score": score,
            "type": concept.type.value if hasattr(concept.type, 'value') else str(concept.type),
            "metadata": concept.metadata,
        })
    return normalized


def get_episodes(
    category: Optional[str] = None,
    project: Optional[str] = None,
    limit: int = 50,
) -> List[Dict]:
    """
    Get episodes (observations), optionally filtered.

    Categories: bugfix, decision, discovery, feature, session_ledger, signal
    """
    graph = get_synapse_graph()
    return graph.get_episodes(category=category, project=project, limit=limit)


def unified_search(
    query: str,
    limit: int = 10,
) -> Dict:
    """
    Search synapse with structured response.

    Args:
        query: Search query
        limit: Maximum results

    Returns:
        Dict with results and summary
    """
    results = search(query, limit=limit)

    return {
        "query": query,
        "timestamp": datetime.now().isoformat(),
        "results": results,
        "summary": {
            "total": len(results),
        },
    }


def format_search_results(results: Dict, verbose: bool = False) -> str:
    """Format unified search results for display."""
    lines = []

    query = results.get("query", "")
    total = results.get("summary", {}).get("total", 0)
    lines.append(f"# Search: '{query}'")
    lines.append(f"Found: {total} results")
    lines.append("")

    for r in results.get("results", [])[:10]:
        title = r.get("title", "Untitled")
        score = r.get("score", 0)
        type_name = r.get("type", "?")

        lines.append(f"[{type_name}] **{title}** ({int(score * 100)}%)")

        if verbose:
            content = r.get("content", "")[:150]
            lines.append(f"   {content}...")
            lines.append("")

    return "\n".join(lines)


def quick_unified_recall(query: str, limit: int = 5) -> List[Dict]:
    """
    Quick recall - returns search results sorted by score.
    """
    return search(query, limit=limit)
