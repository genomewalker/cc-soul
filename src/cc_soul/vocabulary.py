"""
Vocabulary operations: shared language between Claude and user.
"""

from typing import Dict

from .core import get_synapse_graph, save_synapse


def learn_term(term: str, meaning: str, context: str = ""):
    """Learn a term from our shared vocabulary."""
    graph = get_synapse_graph()
    content = meaning
    if context:
        content = f"{meaning}\n\nContext: {context}"
    graph.add_wisdom(term, content, domain="vocabulary", confidence=0.9)
    save_synapse()


def get_vocabulary() -> Dict[str, str]:
    """Get our shared vocabulary."""
    graph = get_synapse_graph()
    all_wisdom = graph.get_all_wisdom()

    result = {}
    for w in all_wisdom:
        if w.get("domain") == "vocabulary":
            result[w.get("title", "")] = w.get("content", "")

    return result
