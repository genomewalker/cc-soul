"""
Belief operations - now a thin wrapper over wisdom with type='principle'.

Beliefs are guiding principles that shape reasoning. They are stored as
wisdom entries with type='principle', allowing unified storage and recall.

This module maintains backwards compatibility while using wisdom internally.
"""

from typing import List, Dict

from .wisdom import gain_wisdom, recall_wisdom, apply_wisdom, confirm_outcome, WisdomType


def hold_belief(belief: str, rationale: str = "", strength: float = 0.8) -> str:
    """
    Record a guiding principle or belief.

    Internally stores as wisdom with type='principle'.
    """
    content = rationale if rationale else belief
    return gain_wisdom(
        type=WisdomType.PRINCIPLE,
        title=belief,
        content=content,
        confidence=strength
    )


def challenge_belief(belief_id: str, confirmed: bool, context: str = ""):
    """
    Record when a belief is tested.

    Uses the wisdom feedback loop internally.
    """
    # Apply the wisdom (belief) first
    app_id = apply_wisdom(belief_id, context=context or "Belief challenged")
    # Then confirm outcome
    confirm_outcome(app_id, success=confirmed)


def get_beliefs(min_strength: float = 0.5) -> List[Dict]:
    """
    Get current beliefs above a strength threshold.

    Returns principles from wisdom table, formatted for backwards compatibility.
    """
    results = recall_wisdom(type=WisdomType.PRINCIPLE, limit=50)

    # Filter by strength and format for backwards compatibility
    beliefs = []
    for w in results:
        if w['effective_confidence'] >= min_strength:
            beliefs.append({
                'id': w['id'],
                'belief': w['title'],
                'rationale': w['content'],
                'strength': w['effective_confidence'],
                'confirmed': 0,  # Not tracked separately anymore
                'challenged': 0,
            })

    return beliefs
