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
