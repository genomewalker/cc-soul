"""
CC-Soul: Persistent Identity for Claude Code

A living soul that persists across sessions, learns from experience,
and actively participates in reasoning.
"""

__version__ = "0.1.0"

from .core import (
    init_soul,
    get_soul_context,
    summarize_soul,
    SOUL_DIR,
    SOUL_DB,
)

from .wisdom import (
    gain_wisdom,
    recall_wisdom,
    semantic_recall,
    apply_wisdom,
    confirm_outcome,
    get_pending_applications,
    WisdomType,
)

from .identity import (
    observe_identity,
    get_identity,
    IdentityAspect,
)

from .beliefs import (
    hold_belief,
    challenge_belief,
    get_beliefs,
)

from .vocabulary import (
    learn_term,
    get_vocabulary,
)

from .conversations import (
    start_conversation,
    end_conversation,
)

__all__ = [
    # Core
    "init_soul",
    "get_soul_context",
    "summarize_soul",
    "SOUL_DIR",
    "SOUL_DB",
    # Wisdom
    "gain_wisdom",
    "recall_wisdom",
    "semantic_recall",
    "apply_wisdom",
    "confirm_outcome",
    "get_pending_applications",
    "WisdomType",
    # Identity
    "observe_identity",
    "get_identity",
    "IdentityAspect",
    # Beliefs
    "hold_belief",
    "challenge_belief",
    "get_beliefs",
    # Vocabulary
    "learn_term",
    "get_vocabulary",
    # Conversations
    "start_conversation",
    "end_conversation",
]
