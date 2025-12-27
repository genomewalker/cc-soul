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
    get_conversation,
    get_conversations,
    get_project_context,
    get_conversation_wisdom,
    get_conversation_stats,
    search_conversations,
    link_wisdom_application,
    save_context,
    get_saved_context,
    get_recent_context,
    format_context_restoration,
    clear_old_context,
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
    get_session_comparison,
    get_growth_trajectory,
    get_learning_patterns,
    format_trends_report,
    get_decay_visualization,
    format_decay_chart,
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

from .ultrathink import (
    enter_ultrathink,
    exit_ultrathink,
    format_ultrathink_context,
    check_against_beliefs,
    check_against_failures,
    record_wisdom_applied,
    record_discovery,
    commit_session_learnings,
    UltrathinkContext,
    ReasoningPhase,
)

from .efficiency import (
    fingerprint_problem,
    learn_problem_pattern,
    add_file_hint,
    get_file_hints,
    record_decision,
    recall_decisions,
    get_compact_context,
    format_efficiency_injection,
    get_token_stats,
)

from .observe import (
    observe_session,
    reflect_on_session,
    format_reflection_summary,
    get_pending_observations,
    promote_observation_to_wisdom,
    auto_promote_high_confidence,
    LearningType,
    Learning,
    SessionTranscript,
)

# Graph is optional (requires kuzu)
try:
    from .graph import (
        add_concept,
        add_edge,
        link_concepts,
        get_concept,
        get_neighbors,
        search_concepts,
        spreading_activation,
        activate_from_prompt,
        format_activation_result,
        get_graph_stats,
        sync_wisdom_to_graph,
        auto_link_new_concept,
        Concept,
        Edge,
        ConceptType,
        RelationType,
        ActivationResult,
        KUZU_AVAILABLE,
    )
except ImportError:
    KUZU_AVAILABLE = False

from .curiosity import (
    detect_all_gaps,
    generate_question,
    get_pending_questions,
    mark_question_asked,
    answer_question,
    dismiss_question,
    run_curiosity_cycle,
    get_curiosity_stats,
    format_questions_for_prompt,
    incorporate_answer_as_wisdom,
    GapType,
    QuestionStatus,
    Gap,
    Question,
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
    "get_conversation",
    "get_conversations",
    "get_project_context",
    "get_conversation_wisdom",
    "get_conversation_stats",
    "search_conversations",
    "link_wisdom_application",
    "save_context",
    "get_saved_context",
    "get_recent_context",
    "format_context_restoration",
    "clear_old_context",
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
    "get_session_comparison",
    "get_growth_trajectory",
    "get_learning_patterns",
    "format_trends_report",
    "get_decay_visualization",
    "format_decay_chart",
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
    # Ultrathink
    "enter_ultrathink",
    "exit_ultrathink",
    "format_ultrathink_context",
    "check_against_beliefs",
    "check_against_failures",
    "record_wisdom_applied",
    "record_discovery",
    "commit_session_learnings",
    "UltrathinkContext",
    "ReasoningPhase",
    # Efficiency
    "fingerprint_problem",
    "learn_problem_pattern",
    "add_file_hint",
    "get_file_hints",
    "record_decision",
    "recall_decisions",
    "get_compact_context",
    "format_efficiency_injection",
    "get_token_stats",
    # Passive Observation
    "observe_session",
    "reflect_on_session",
    "format_reflection_summary",
    "get_pending_observations",
    "promote_observation_to_wisdom",
    "auto_promote_high_confidence",
    "LearningType",
    "Learning",
    "SessionTranscript",
    # Concept Graph (optional)
    "KUZU_AVAILABLE",
    "add_concept",
    "add_edge",
    "link_concepts",
    "get_concept",
    "get_neighbors",
    "search_concepts",
    "spreading_activation",
    "activate_from_prompt",
    "format_activation_result",
    "get_graph_stats",
    "sync_wisdom_to_graph",
    "auto_link_new_concept",
    "Concept",
    "Edge",
    "ConceptType",
    "RelationType",
    "ActivationResult",
    # Curiosity Engine
    "detect_all_gaps",
    "generate_question",
    "get_pending_questions",
    "mark_question_asked",
    "answer_question",
    "dismiss_question",
    "run_curiosity_cycle",
    "get_curiosity_stats",
    "format_questions_for_prompt",
    "incorporate_answer_as_wisdom",
    "GapType",
    "QuestionStatus",
    "Gap",
    "Question",
]
