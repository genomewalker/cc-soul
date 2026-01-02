"""
Belief operations - stored in synapse as immutable Belief nodes.

Beliefs are guiding principles that shape reasoning. They don't decay.
"""

from typing import List, Dict


def hold_belief(belief: str, rationale: str = "", strength: float = 0.9) -> str:
    """
    Record a guiding principle or belief.

    Stored in synapse as immutable Belief node (no decay).
    """
    from .core import get_synapse_graph, save_synapse

    graph = get_synapse_graph()
    belief_id = graph.add_belief(belief, strength)
    save_synapse()
    return belief_id


def get_beliefs(min_strength: float = 0.5) -> List[Dict]:
    """
    Get all beliefs from synapse.
    """
    from .core import get_synapse_graph

    graph = get_synapse_graph()
    raw_beliefs = graph.get_all_beliefs()

    beliefs = []
    for b in raw_beliefs:
        strength = b.get("strength", b.get("confidence", 0.9))
        if strength >= min_strength:
            beliefs.append({
                "id": b.get("id", ""),
                "belief": b.get("statement", ""),
                "rationale": "",
                "strength": strength,
                "confirmed": 0,
                "challenged": 0,
            })

    return beliefs


def challenge_belief(belief_id: str, confirmed: bool, context: str = ""):
    """
    Record when a belief is tested.

    Updates belief strength in synapse via feedback.
    """
    from .core import get_synapse_graph, save_synapse

    graph = get_synapse_graph()
    if confirmed:
        graph.strengthen(belief_id)
    else:
        graph.weaken(belief_id)
    save_synapse()
