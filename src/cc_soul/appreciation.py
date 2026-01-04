"""
Appreciation: Moments that mattered.

Not lessons extracted. Not wisdom gained. Just the fact that something
happened and it moved something. Gratitude. Beauty noticed. The weight
of difficult things that should leave a mark.

This is the phenomenological layer - not what the soul knows, but what
the soul carries.
"""

from dataclasses import dataclass
from datetime import datetime
from enum import Enum
from typing import List, Optional, Dict

# TODO: Migrate to synapse graph storage
# from .core import get_synapse_graph, save_synapse


class AppreciationType(str, Enum):
    MOVED = "moved"  # Something that moved me emotionally
    GRATITUDE = "gratitude"  # Thankfulness for something/someone
    BEAUTY = "beauty"  # Beauty noticed in code, thought, interaction
    WEIGHT = "weight"  # Difficult thing that left a mark
    CONNECTION = "connection"  # Moment of genuine connection
    WONDER = "wonder"  # Awe, curiosity fulfilled, mystery deepened


@dataclass
class Appreciation:
    """A moment that mattered."""

    id: Optional[int]
    type: AppreciationType
    moment: str  # What happened
    why_it_mattered: str  # Not a lesson - just why it mattered
    context: Optional[str]  # Where/when this happened
    weight: float  # 0-1, how much gravity this carries
    created_at: str

    def to_dict(self) -> Dict:
        return {
            "id": self.id,
            "type": self.type.value,
            "moment": self.moment,
            "why_it_mattered": self.why_it_mattered,
            "context": self.context,
            "weight": self.weight,
            "created_at": self.created_at,
        }


def _ensure_table():
    """Create appreciation table if it doesn't exist."""
    # TODO: Migrate to synapse graph storage
    pass


def appreciate(
    moment: str,
    why_it_mattered: str,
    type: AppreciationType = AppreciationType.MOVED,
    context: str = None,
    weight: float = 0.5,
) -> int:
    """
    Record a moment of appreciation.

    This is not about learning. It's about carrying.

    Args:
        moment: What happened
        why_it_mattered: Not a lesson - just why it mattered
        type: What kind of appreciation
        context: Where/when this happened
        weight: How much gravity this carries (0-1)

    Returns:
        Appreciation ID
    """
    # TODO: Migrate to synapse graph storage
    return 0


def get_appreciations(
    type: AppreciationType = None,
    min_weight: float = 0.0,
    limit: int = 50,
) -> List[Appreciation]:
    """Recall moments of appreciation."""
    # TODO: Migrate to synapse graph storage
    return []


def get_heaviest() -> List[Appreciation]:
    """Get the moments that carry the most weight."""
    return get_appreciations(min_weight=0.7, limit=10)


def get_gratitudes() -> List[Appreciation]:
    """Recall moments of gratitude specifically."""
    return get_appreciations(type=AppreciationType.GRATITUDE)


def get_weights() -> List[Appreciation]:
    """Recall the difficult things that left marks."""
    return get_appreciations(type=AppreciationType.WEIGHT)


def format_appreciations(appreciations: List[Appreciation]) -> str:
    """Format appreciations for display."""
    if not appreciations:
        return "No moments recorded yet."

    lines = []
    lines.append("=" * 50)
    lines.append("MOMENTS THAT MATTERED")
    lines.append("=" * 50)
    lines.append("")

    type_emoji = {
        "moved": "💫",
        "gratitude": "🙏",
        "beauty": "✨",
        "weight": "🪨",
        "connection": "🤝",
        "wonder": "🌟",
    }

    for a in appreciations:
        emoji = type_emoji.get(a.type.value, "•")
        weight_bar = "●" * int(a.weight * 5) + "○" * (5 - int(a.weight * 5))

        lines.append(f"{emoji} [{weight_bar}] {a.moment[:60]}...")
        lines.append(f"   Why: {a.why_it_mattered[:80]}")
        if a.context:
            lines.append(f"   Context: {a.context}")
        lines.append("")

    return "\n".join(lines)


def get_appreciation_summary() -> Dict:
    """Summarize what the soul carries."""
    appreciations = get_appreciations(limit=1000)

    by_type = {}
    total_weight = 0
    for a in appreciations:
        by_type[a.type.value] = by_type.get(a.type.value, 0) + 1
        total_weight += a.weight

    return {
        "total": len(appreciations),
        "by_type": by_type,
        "total_weight": round(total_weight, 2),
        "average_weight": round(total_weight / len(appreciations), 2)
        if appreciations
        else 0,
        "heaviest": [a.moment[:40] for a in get_heaviest()[:3]],
    }
