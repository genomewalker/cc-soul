"""
Identity operations: observe and retrieve how we work together.
"""

from enum import Enum
from typing import Dict, Any

from .core import get_synapse_graph, save_synapse


class IdentityAspect(Enum):
    """Aspects of identity with this user."""

    COMMUNICATION = "communication"  # How we talk
    WORKFLOW = "workflow"  # How we work
    DOMAIN = "domain"  # What we work on
    RAPPORT = "rapport"  # Our relationship
    VOCABULARY = "vocabulary"  # Shared terms/acronyms


def observe_identity(
    aspect: IdentityAspect, key: str, value: str, confidence: float = 0.8
):
    """
    Record an observation about identity.

    Stored as an episode in synapse.
    """
    graph = get_synapse_graph()
    graph.observe(
        category="identity",
        title=f"{aspect.value}:{key}",
        content=value,
        tags=["identity", aspect.value, key],
    )
    save_synapse()


def get_identity(aspect: IdentityAspect = None) -> Dict[str, Any]:
    """Get identity observations, optionally filtered by aspect."""
    graph = get_synapse_graph()
    episodes = graph.get_episodes(category="identity", limit=100)

    result = {}
    for ep in episodes:
        title = ep.get("title", "")
        if ":" in title:
            ep_aspect, key = title.split(":", 1)
            if aspect and ep_aspect != aspect.value:
                continue

            if aspect:
                result[key] = {
                    "value": ep.get("content", ""),
                    "confidence": 0.8,
                    "observations": 1,
                }
            else:
                if ep_aspect not in result:
                    result[ep_aspect] = {}
                result[ep_aspect][key] = {
                    "value": ep.get("content", ""),
                    "confidence": 0.8,
                }

    return result
