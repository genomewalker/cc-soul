# =============================================================================
# CONCEPT GRAPH
# =============================================================================

@mcp.tool()
def activate_concepts(prompt: str, limit: int = 10) -> str:
    """Activate concepts related to a prompt using spreading activation.

    The concept graph connects ideas. When you activate one concept,
    related concepts also activate - enabling cross-domain insights.

    Args:
        prompt: The triggering thought or problem statement
        limit: Maximum concepts to return
    """
    try:
        from .graph import activate_from_prompt, format_activation_result

        result = activate_from_prompt(prompt, limit=limit)
        return format_activation_result(result)

    except ImportError:
        return "Concept graph not available. Install: pip install kuzu"
    except Exception as e:
        return f"Activation failed: {e}"


@mcp.tool()
def link_concepts(source_id: str, target_id: str, relation: str = "related_to", weight: float = 1.0) -> str:
    """Create a link between two concepts in the graph.

    Building the concept graph strengthens cross-domain connections.
    Links accumulate over time, forming an associative knowledge web.

    Args:
        source_id: Source concept ID
        target_id: Target concept ID
        relation: Type of relationship (related_to, led_to, contradicts, evolved_from, reminded_by, used_with, requires)
        weight: Strength of the connection (0.0 to 1.0)
    """
    try:
        from .graph import link_concepts as _link, RelationType

        relation_type = RelationType(relation)
        success = _link(source_id, target_id, relation_type, weight=weight)

        if success:
            return f"Linked {source_id} --[{relation}]--> {target_id} (weight: {weight})"
        else:
            return "Failed to create link."

    except ValueError:
        valid = ["related_to", "led_to", "contradicts", "evolved_from", "reminded_by", "used_with", "requires"]
        return f"Invalid relation. Valid types: {', '.join(valid)}"
    except ImportError:
        return "Concept graph not available. Install: pip install kuzu"
    except Exception as e:
        return f"Linking failed: {e}"


@mcp.tool()
def sync_wisdom_to_graph() -> str:
    """Sync all wisdom entries to the concept graph.

    Creates concept nodes for all wisdom and auto-links related entries.
    Run this to initialize or rebuild the concept graph from wisdom.
    """
    try:
        from .graph import sync_wisdom_to_graph as _sync

        _sync()
        return "Successfully synced wisdom to concept graph."

    except ImportError:
        return "Concept graph not available. Install: pip install kuzu"
    except Exception as e:
        return f"Sync failed: {e}"


@mcp.tool()
def get_concept_graph_stats() -> str:
    """Get statistics about the concept graph.

    Shows the size and shape of the knowledge graph.
    """
    try:
        from .graph import get_graph_stats

        stats = get_graph_stats()

        lines = [
            "Concept Graph Statistics:",
            f"  Nodes: {stats.get('node_count', 0)}",
            f"  Edges: {stats.get('edge_count', 0)}",
        ]

        if stats.get("by_type"):
            lines.append("  By type:")
            for t, count in stats["by_type"].items():
                lines.append(f"    {t}: {count}")

        return "\n".join(lines)

    except ImportError:
        return "Concept graph not available. Install: pip install kuzu"
    except Exception as e:
        return f"Failed to get stats: {e}"
