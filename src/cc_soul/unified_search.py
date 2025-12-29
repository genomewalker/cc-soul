"""
Unified memory search with priority ordering.

Search order:
1. cc-memory (project-local observations) - PRIMARY
2. cc-soul wisdom (universal patterns) - SECONDARY
3. claude-mem (if available) - FALLBACK

This ensures project context takes precedence over universal wisdom,
which in turn takes precedence over general semantic search.
"""

from typing import List, Dict, Optional
from datetime import datetime


def search_cc_memory(query: str, limit: int = 5) -> List[Dict]:
    """Search cc-memory (project-local observations)."""
    try:
        from .bridge import is_memory_available, find_project_dir

        if not is_memory_available():
            return []

        from cc_memory import memory as cc_memory

        project_dir = find_project_dir()

        # Use cc-memory's recall (semantic search)
        results = cc_memory.recall(project_dir, query, limit=limit)

        # Normalize format
        normalized = []
        for r in results:
            normalized.append({
                "source": "cc-memory",
                "id": r.get("id"),
                "title": r.get("title", ""),
                "content": r.get("content", ""),
                "category": r.get("category", "observation"),
                "score": r.get("score", r.get("_distance", 0.5)),
                "timestamp": r.get("timestamp"),
            })
        return normalized
    except Exception:
        return []


def search_soul_wisdom(query: str, limit: int = 5) -> List[Dict]:
    """Search cc-soul wisdom (universal patterns)."""
    try:
        from .wisdom import quick_recall

        results = quick_recall(query, limit=limit)

        # Normalize format
        normalized = []
        for w in results:
            normalized.append({
                "source": "cc-soul",
                "id": w.get("id"),
                "title": w.get("title", ""),
                "content": w.get("content", ""),
                "category": w.get("type", "wisdom"),
                "score": w.get("combined_score", w.get("effective_confidence", 0.5)),
                "confidence": w.get("confidence", 0.7),
            })
        return normalized
    except Exception:
        return []


def search_claude_mem(query: str, limit: int = 5) -> List[Dict]:
    """
    Search claude-mem (third-party semantic memory).

    This requires the claude-mem MCP plugin to be installed and running.
    We can't call MCP tools directly from Python, so this returns
    instructions for the caller to invoke the MCP tool.
    """
    # claude-mem is accessed via MCP, not direct Python import
    # Return a marker indicating the caller should use MCP
    return [{
        "source": "claude-mem",
        "type": "mcp_reference",
        "tool": "mcp__plugin_claude-mem_mem-search__search",
        "query": query,
        "limit": limit,
        "note": "Use MCP tool mcp__plugin_claude-mem_mem-search__search for extended search",
    }]


def unified_search(
    query: str,
    limit: int = 10,
    include_claude_mem: bool = True,
    cc_memory_weight: float = 1.2,
    soul_weight: float = 1.0,
) -> Dict:
    """
    Search all memory sources with priority ordering.

    Args:
        query: Search query
        limit: Maximum total results
        include_claude_mem: Whether to include claude-mem reference
        cc_memory_weight: Score multiplier for cc-memory results (>1 = higher priority)
        soul_weight: Score multiplier for soul wisdom

    Returns:
        Dict with results grouped by source and combined list
    """
    results = {
        "query": query,
        "timestamp": datetime.now().isoformat(),
        "by_source": {},
        "combined": [],
    }

    # 1. Search cc-memory first (project context)
    cc_mem_results = search_cc_memory(query, limit=limit)
    if cc_mem_results:
        # Apply priority weight
        for r in cc_mem_results:
            r["weighted_score"] = r.get("score", 0.5) * cc_memory_weight
        results["by_source"]["cc-memory"] = cc_mem_results
        results["combined"].extend(cc_mem_results)

    # 2. Search soul wisdom (universal patterns)
    soul_results = search_soul_wisdom(query, limit=limit)
    if soul_results:
        for r in soul_results:
            r["weighted_score"] = r.get("score", 0.5) * soul_weight
        results["by_source"]["cc-soul"] = soul_results
        results["combined"].extend(soul_results)

    # 3. Add claude-mem reference if requested
    if include_claude_mem:
        claude_mem_ref = search_claude_mem(query, limit=limit)
        results["by_source"]["claude-mem"] = claude_mem_ref
        # Don't add to combined - it's a reference, not actual results

    # Sort combined by weighted score
    results["combined"].sort(key=lambda x: x.get("weighted_score", 0), reverse=True)
    results["combined"] = results["combined"][:limit]

    # Summary
    results["summary"] = {
        "cc_memory_count": len(cc_mem_results),
        "soul_count": len(soul_results),
        "claude_mem_available": include_claude_mem,
        "total": len(results["combined"]),
    }

    return results


def format_search_results(results: Dict, verbose: bool = False) -> str:
    """Format unified search results for display."""
    lines = []

    summary = results.get("summary", {})
    query = results.get("query", "")
    lines.append(f"# Memory Search: '{query}'")
    lines.append(f"Found: {summary.get('cc_memory_count', 0)} project + {summary.get('soul_count', 0)} wisdom")
    lines.append("")

    # Combined results
    for r in results.get("combined", [])[:10]:
        source = r.get("source", "?")
        title = r.get("title", "Untitled")
        score = r.get("weighted_score", r.get("score", 0))
        category = r.get("category", "")

        source_icon = "📂" if source == "cc-memory" else "🧠"
        lines.append(f"{source_icon} **{title}** [{category}] ({int(score * 100)}%)")

        if verbose:
            content = r.get("content", "")[:150]
            lines.append(f"   {content}...")
            lines.append("")

    # claude-mem search instruction
    if results.get("summary", {}).get("claude_mem_available"):
        lines.append("")
        lines.append("---")
        lines.append("**Next step:** Search claude-mem for extended results:")
        lines.append(f'Call `mcp__plugin_claude-mem_mem-search__search` with query="{query}"')

    return "\n".join(lines)


def quick_unified_recall(query: str, limit: int = 5) -> List[Dict]:
    """
    Quick recall with automatic priority ordering.

    Returns combined results from cc-memory and soul, sorted by weighted score.
    """
    results = unified_search(query, limit=limit, include_claude_mem=False)
    return results.get("combined", [])
