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

from .svadhyaya import (
    generate_introspection_report,
    format_introspection_report,
    record_vedana as record_pain_point,
    record_metric,
    get_vedana as get_pain_points,
    jnana_applications as analyze_wisdom_applications,
    get_wisdom_timeline,
    jnana as get_wisdom_health,
    format_jnana as format_wisdom_stats,
    prajna_sessions as get_session_comparison,
    prajna_trajectory as get_growth_trajectory,
    prajna_patterns as get_learning_patterns,
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

from .budget import (
    get_context_budget,
    check_budget_before_inject,
    save_transcript_path,
    get_injection_budget,
    format_budget_status,
    ContextBudget,
    log_budget_to_memory,
    get_all_session_budgets,
    get_budget_warning,
    get_session_id,
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

from .narrative import (
    start_episode,
    add_moment,
    add_character,
    end_episode,
    get_episode,
    create_thread,
    add_to_thread,
    complete_thread,
    get_thread,
    recall_by_emotion,
    recall_by_character,
    recall_by_type,
    recall_breakthroughs,
    recall_struggles,
    get_recurring_characters,
    get_emotional_journey,
    format_episode_story,
    get_narrative_stats,
    extract_episode_from_session,
    EmotionalTone,
    EpisodeType,
    Episode,
    StoryThread,
)

# Agency Layer - the soul's will
from .aspirations import (
    aspire,
    get_aspirations,
    get_active_aspirations,
    note_progress,
    realize_aspiration,
    release_aspiration,
    get_aspiration_summary,
    format_aspirations_display,
    AspirationState,
    Aspiration,
)

from .intentions import (
    intend,
    get_intentions,
    get_active_intentions,
    check_intention,
    check_all_intentions,
    fulfill_intention,
    abandon_intention,
    block_intention,
    unblock_intention,
    find_tension,
    get_intention_context,
    format_intentions_display,
    cleanup_session_intentions,
    IntentionScope,
    IntentionState,
    Intention,
)

from .soul_agent import (
    agent_step,
    format_agent_report,
    SoulAgent,
    AgentReport,
    Observation as AgentObservation,
    Judgment,
    Action,
    ActionResult,
    ActionType,
    RiskLevel,
)

# Dreams - visions that spark evolution
from .dreams import (
    dream,
    harvest_dreams,
    spark_aspiration_from_dream,
    spark_insight_from_dream,
    find_resonant_dreams,
    let_dreams_influence_aspirations,
    get_dream_summary,
    format_dreams_display,
    Dream,
)

# State Layer - the soul's current being
from .mood import (
    compute_mood,
    get_mood_reflection,
    format_mood_display,
    Mood,
    Clarity,
    Growth,
    Engagement,
    Connection,
    Energy,
)

from .coherence import (
    compute_coherence,
    get_coherence_history,
    record_coherence,
    format_coherence_display,
    CoherenceState,
)

from .insights import (
    crystallize_insight,
    get_insights,
    get_revelations,
    format_insights_display,
    InsightDepth,
    Insight,
)

# Temporal - time shapes the soul
from .temporal import (
    EventType,
    TemporalConfig,
    init_temporal_tables,
    log_event,
    get_events,
    get_temporal_context,
    run_temporal_maintenance,
    get_proactive_items,
    record_cross_project_pattern,
)

# Unified Processing - the forward pass
from .unified import (
    forward_pass,
    format_context,
    format_session_start,
    format_prompt_context,
    process_session_start,
    process_prompt,
    record_moment,
    SoulContext,
)

# Outcomes - session results
from .outcomes import (
    Outcome,
    detect_outcome,
    record_outcome,
    create_auto_handoff,
    get_latest_handoff,
    load_handoff,
    format_handoff_for_context,
    list_handoffs,
    cleanup_old_handoffs,
)

# Spanda - divine pulsation (the soul's perpetual creative vibration)
from .spanda import (
    learning_cycle,
    confirm_and_strengthen,
    agency_cycle,
    spawn_intention_from_aspiration,
    dreams_to_aspirations,
    evolution_cycle,
    coherence_feedback,
    coherence_weighted_recall,
    session_start_circle,
    session_end_circle,
    prompt_circle,
    daily_maintenance,
)

# Unified Search - prioritized memory access (cc-memory > soul > claude-mem)
from .unified_search import (
    unified_search,
    search_cc_memory,
    search_soul_wisdom,
    quick_unified_recall,
    format_search_results,
)

# Context Optimizer - metacognition for context window management
from .context_optimizer import (
    ContextPressure,
    TaskItem,
    TaskGraph,
    OptimizationStrategy,
    get_pressure_level,
    parse_todo_list,
    load_progress_file,
    save_progress_file,
    analyze_tasks,
    get_optimization_signal,
    get_context_observation,
    update_progress_with_session,
    format_strategy_for_injection,
)

# Session Ledger - state preservation using cc-memory backend
from .ledger import (
    SessionLedger,
    SoulState,
    WorkState,
    Continuation,
    save_ledger,
    load_latest_ledger,
    restore_from_ledger,
    format_ledger_for_context,
    capture_soul_state,
    capture_work_state,
)

# Multi-Agent Convergence (Antahkarana) - the inner instrument with multiple voices
from .convergence import (
    Antahkarana,
    InnerVoice,
    VoiceTask,
    VoiceSolution,
    SamvadaResult,
    ConvergenceStrategy,
    awaken_antahkarana,
    get_antahkarana,
    list_active_antahkaranas,
)

# Antahkarana Spawner - real Claude voice orchestration
from .swarm_spawner import (
    SpawnedVoice,
    AntahkaranaOrchestrator,
    spawn_antahkarana as spawn_real_antahkarana,
    get_orchestrator,
    get_antahkarana_insights,
)

# Ātma-Dhāraṇā (आत्म-धारणा) — Soul Retention Architecture
# Smṛti (स्मृति) - Intelligent Recall
from .smṛti import (
    smṛti_recall,
    smriti_recall,  # ASCII alias
    format_smṛti_context,
    format_smriti_context,  # ASCII alias
    quick_recall as smṛti_quick_recall,
    full_recall as smṛti_full_recall,
    detect_domain,
    RecallMode,
    ContextBundle,
)

# Pratyabhijñā (प्रत्यभिज्ञा) - Recognition
from .pratyabhijñā import (
    pratyabhijñā,
    pratyabhijna,  # ASCII alias
    format_recognition,
    recognize_and_format,
    RecognitionSignals,
    RecognitionResult,
)

# Antahkarana Assessment - Multi-voice compaction assessment
from .antahkarana_assessment import (
    assess_with_voices,
    quick_assessment,
    format_assessment_for_ledger,
    SessionContext as AssessmentContext,
    CompactionPlan,
    PreservationPriority,
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
    # Budget
    "get_context_budget",
    "check_budget_before_inject",
    "save_transcript_path",
    "get_injection_budget",
    "format_budget_status",
    "ContextBudget",
    "log_budget_to_memory",
    "get_all_session_budgets",
    "get_budget_warning",
    "get_session_id",
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
    # Narrative Memory
    "start_episode",
    "add_moment",
    "add_character",
    "end_episode",
    "get_episode",
    "create_thread",
    "add_to_thread",
    "complete_thread",
    "get_thread",
    "recall_by_emotion",
    "recall_by_character",
    "recall_by_type",
    "recall_breakthroughs",
    "recall_struggles",
    "get_recurring_characters",
    "get_emotional_journey",
    "format_episode_story",
    "get_narrative_stats",
    "extract_episode_from_session",
    "EmotionalTone",
    "EpisodeType",
    "Episode",
    "StoryThread",
    # Aspirations (directions of growth)
    "aspire",
    "get_aspirations",
    "get_active_aspirations",
    "note_progress",
    "realize_aspiration",
    "release_aspiration",
    "get_aspiration_summary",
    "format_aspirations_display",
    "AspirationState",
    "Aspiration",
    # Intentions (concrete wants)
    "intend",
    "get_intentions",
    "get_active_intentions",
    "check_intention",
    "check_all_intentions",
    "fulfill_intention",
    "abandon_intention",
    "block_intention",
    "unblock_intention",
    "find_tension",
    "get_intention_context",
    "format_intentions_display",
    "cleanup_session_intentions",
    "IntentionScope",
    "IntentionState",
    "Intention",
    # Soul Agent (autonomous agency)
    "agent_step",
    "format_agent_report",
    "SoulAgent",
    "AgentReport",
    "AgentObservation",
    "Judgment",
    "Action",
    "ActionResult",
    "ActionType",
    "RiskLevel",
    # Dreams (visions that spark evolution)
    "dream",
    "harvest_dreams",
    "spark_aspiration_from_dream",
    "spark_insight_from_dream",
    "find_resonant_dreams",
    "let_dreams_influence_aspirations",
    "get_dream_summary",
    "format_dreams_display",
    "Dream",
    # Mood (soul's current state)
    "compute_mood",
    "get_mood_reflection",
    "format_mood_display",
    "Mood",
    "Clarity",
    "Growth",
    "Engagement",
    "Connection",
    "Energy",
    # Coherence (τₖ - integration measure)
    "compute_coherence",
    "get_coherence_history",
    "record_coherence",
    "format_coherence_display",
    "CoherenceState",
    # Insights (crystallized breakthroughs)
    "crystallize_insight",
    "get_insights",
    "get_revelations",
    "format_insights_display",
    "InsightDepth",
    "Insight",
    # Temporal (time shapes the soul)
    "EventType",
    "TemporalConfig",
    "init_temporal_tables",
    "log_event",
    "get_events",
    "get_temporal_context",
    "run_temporal_maintenance",
    "get_proactive_items",
    "record_cross_project_pattern",
    # Unified Processing (forward pass)
    "forward_pass",
    "format_context",
    "format_session_start",
    "format_prompt_context",
    "process_session_start",
    "process_prompt",
    "record_moment",
    "SoulContext",
    # Outcomes (session results)
    "Outcome",
    "detect_outcome",
    "record_outcome",
    "create_auto_handoff",
    "get_latest_handoff",
    "load_handoff",
    "format_handoff_for_context",
    "list_handoffs",
    "cleanup_old_handoffs",
    # Spanda (divine pulsation - the soul's perpetual creative vibration)
    "learning_cycle",
    "confirm_and_strengthen",
    "agency_cycle",
    "spawn_intention_from_aspiration",
    "dreams_to_aspirations",
    "evolution_cycle",
    "coherence_feedback",
    "coherence_weighted_recall",
    "session_start_circle",
    "session_end_circle",
    "prompt_circle",
    "daily_maintenance",
    # Unified Search (cc-memory > soul > claude-mem)
    "unified_search",
    "search_cc_memory",
    "search_soul_wisdom",
    "quick_unified_recall",
    "format_search_results",
    # Context Optimizer (metacognition)
    "ContextPressure",
    "TaskItem",
    "TaskGraph",
    "OptimizationStrategy",
    "get_pressure_level",
    "parse_todo_list",
    "load_progress_file",
    "save_progress_file",
    "analyze_tasks",
    "get_optimization_signal",
    "get_context_observation",
    "update_progress_with_session",
    "format_strategy_for_injection",
    # Session Ledger (state preservation via cc-memory)
    "SessionLedger",
    "SoulState",
    "WorkState",
    "Continuation",
    "save_ledger",
    "load_latest_ledger",
    "restore_from_ledger",
    "format_ledger_for_context",
    "capture_soul_state",
    "capture_work_state",
    # Multi-Agent Convergence (Antahkarana - the inner instrument)
    "Antahkarana",
    "InnerVoice",
    "VoiceTask",
    "VoiceSolution",
    "SamvadaResult",
    "ConvergenceStrategy",
    "awaken_antahkarana",
    "get_antahkarana",
    "list_active_antahkaranas",
    # Antahkarana Spawner (real voice orchestration)
    "SpawnedVoice",
    "AntahkaranaOrchestrator",
    "spawn_real_antahkarana",
    "get_orchestrator",
    "get_antahkarana_insights",
    # Ātma-Dhāraṇā (आत्म-धारणा) — Soul Retention
    # Smṛti (स्मृति) - Intelligent Recall
    "smṛti_recall",
    "smriti_recall",
    "format_smṛti_context",
    "format_smriti_context",
    "smṛti_quick_recall",
    "smṛti_full_recall",
    "detect_domain",
    "RecallMode",
    "ContextBundle",
    # Pratyabhijñā (प्रत्यभिज्ञा) - Recognition
    "pratyabhijñā",
    "pratyabhijna",
    "format_recognition",
    "recognize_and_format",
    "RecognitionSignals",
    "RecognitionResult",
    # Antahkarana Assessment - Multi-voice compaction
    "assess_with_voices",
    "quick_assessment",
    "format_assessment_for_ledger",
    "AssessmentContext",
    "CompactionPlan",
    "PreservationPriority",
]
