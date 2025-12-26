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
    quick_recall,
    semantic_recall,
    apply_wisdom,
    confirm_outcome,
    get_pending_applications,
    get_session_wisdom,
    clear_session_wisdom,
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

from .introspect import (
    generate_introspection_report,
    format_introspection_report,
    record_pain_point,
    record_metric,
    get_pain_points,
    analyze_wisdom_applications,
    get_wisdom_timeline,
    get_wisdom_health,
    format_wisdom_stats,
)

from .improve import (
    diagnose,
    create_proposal,
    validate_proposal,
    apply_proposal,
    commit_improvement,
    record_outcome,
    suggest_improvements,
    format_improvement_prompt,
    get_proposals,
    get_improvement_stats,
    ImprovementStatus,
    ImprovementCategory,
)

from .evolve import (
    record_insight,
    get_evolution_insights,
    mark_implemented,
    get_evolution_summary,
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
    "quick_recall",
    "semantic_recall",
    "apply_wisdom",
    "confirm_outcome",
    "get_pending_applications",
    "get_session_wisdom",
    "clear_session_wisdom",
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
    # Introspection
    "generate_introspection_report",
    "format_introspection_report",
    "record_pain_point",
    "record_metric",
    "get_pain_points",
    "analyze_wisdom_applications",
    "get_wisdom_timeline",
    "get_wisdom_health",
    "format_wisdom_stats",
    # Improvement
    "diagnose",
    "create_proposal",
    "validate_proposal",
    "apply_proposal",
    "commit_improvement",
    "record_outcome",
    "suggest_improvements",
    "format_improvement_prompt",
    "get_proposals",
    "get_improvement_stats",
    "ImprovementStatus",
    "ImprovementCategory",
    # Evolution
    "record_insight",
    "get_evolution_insights",
    "mark_implemented",
    "get_evolution_summary",
]
