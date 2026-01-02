"""
Core soul infrastructure - Synapse backend only.

All soul data stored in cc-synapse (Rust graph via PyO3).
No SQLite. No legacy. Clean.
"""

from pathlib import Path
from typing import Dict, Any

# Soul data lives at user level
SOUL_DIR = Path.home() / ".claude" / "mind"
SYNAPSE_PATH = SOUL_DIR / "soul.synapse"

# Singleton graph instance
_synapse_graph = None


def get_synapse_graph():
    """Get synapse graph (singleton)."""
    global _synapse_graph
    if _synapse_graph is not None:
        return _synapse_graph

    from .synapse_bridge import SoulGraph
    SOUL_DIR.mkdir(parents=True, exist_ok=True)
    _synapse_graph = SoulGraph.load(SYNAPSE_PATH)
    return _synapse_graph


def save_synapse():
    """Save synapse graph to disk."""
    graph = get_synapse_graph()
    graph.save()


def init_soul():
    """Initialize soul - ensures synapse graph exists."""
    get_synapse_graph()


def get_soul_context(query: str = None) -> Dict[str, Any]:
    """Get soul context from synapse."""
    graph = get_synapse_graph()
    return graph.get_context(query)


def summarize_soul() -> str:
    """Generate a human-readable summary of the soul."""
    graph = get_synapse_graph()
    return graph.format_context()
