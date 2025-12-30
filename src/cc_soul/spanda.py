"""
Spanda - Divine Pulsation.

स्पन्द (spanda) - the perpetual creative vibration in Kashmir Shaivism.
The pulse of consciousness that drives the soul's continuous evolution.

This module implements three self-sustaining cycles:

1. LEARNING CYCLE (Vidyā): observe → learn → apply → confirm → strengthen
2. AGENCY CYCLE (Kartṛtva): dream → aspire → intend → decide → act → observe
3. EVOLUTION CYCLE (Vikāsa): introspect → diagnose → propose → validate → apply

All bound by:
- COHERENCE (τₖ): Measures integration across all cycles
- TEMPORAL: Logs events and applies decay/strengthening
- UNIFIED: Single forward pass through all modules

The cycles are self-sustaining when connected:
- Learning feeds wisdom to agency decisions (Vidyā → Kartṛtva)
- Agency actions generate learning opportunities (Kartṛtva → Vidyā)
- Evolution improves how both cycles work (Vikāsa → all)
- Coherence guides what surfaces and when (τₖ → all)
"""

from datetime import datetime
from typing import Dict, List, Optional, Tuple

from .aspirations import (
    aspire,
    get_active_aspirations,
    note_progress,
    Aspiration,
)
from .intentions import (
    intend,
    get_active_intentions,
    check_intention,
    fulfill_intention,
    IntentionScope,
    Intention,
)
from .dreams import (
    harvest_dreams,
    spark_aspiration_from_dream,
    let_dreams_influence_aspirations,
    Dream,
)
from .soul_agent import (
    agent_step,
    AgentReport,
    ActionType,
)
from .mood import compute_mood, Mood
from .coherence import compute_coherence, record_coherence, CoherenceState
from .temporal import log_event, EventType, run_temporal_maintenance
from .wisdom import quick_recall, gain_wisdom, confirm_outcome, WisdomType
from .svadhyaya import generate_introspection_report
from .improve import diagnose, suggest_improvements


# =============================================================================
# CIRCLE 1: LEARNING
# =============================================================================


def learning_cycle(
    context: str,
    observation: str = "",
    outcome: str = "positive",
) -> Dict:
    """
    Execute one learning cycle.

    observe → learn → apply → confirm → strengthen

    Args:
        context: The current context (prompt, task description)
        observation: What was observed during work
        outcome: "positive", "negative", or "neutral"

    Returns:
        Cycle result with what was learned
    """
    result = {
        "cycle": "learning",
        "timestamp": datetime.now().isoformat(),
        "actions": [],
    }

    # 1. Recall relevant wisdom
    wisdom = quick_recall(context, limit=3)
    if wisdom:
        result["wisdom_recalled"] = [w.get("title") for w in wisdom]
        result["actions"].append("recalled_wisdom")

    # 2. If outcome positive and observation present, potentially learn
    if outcome == "positive" and observation:
        # Could become new wisdom if pattern detected
        result["potential_learning"] = observation
        result["actions"].append("noted_potential_learning")

    # 3. Log temporal event
    log_event(
        EventType.WISDOM_APPLIED if wisdom else EventType.SESSION_START,
        data={"context": context[:100], "outcome": outcome},
    )
    result["actions"].append("logged_event")

    return result


def confirm_and_strengthen(wisdom_id: int, success: bool) -> Dict:
    """
    Confirm wisdom application outcome, strengthening or weakening.

    The feedback loop that makes learning adaptive.
    """
    result = confirm_outcome(wisdom_id, success)

    # Log the confirmation
    log_event(
        EventType.WISDOM_CONFIRMED if success else EventType.WISDOM_CHALLENGED,
        data={"wisdom_id": wisdom_id, "success": success},
    )

    return {"wisdom_id": wisdom_id, "success": success, "new_confidence": result}


# =============================================================================
# CIRCLE 2: AGENCY
# =============================================================================


def agency_cycle(
    user_prompt: str = "",
    assistant_output: str = "",
    session_phase: str = "active",
) -> Dict:
    """
    Execute one agency cycle.

    dream → aspire → intend → decide → act → observe

    This is the soul exercising its will.
    """
    result = {
        "cycle": "agency",
        "timestamp": datetime.now().isoformat(),
        "actions": [],
    }

    # 1. Run the agent (observe, judge, decide, act)
    report = agent_step(user_prompt, assistant_output, session_phase)
    result["agent_report"] = {
        "observations": {
            "sentiment": report.observation.user_sentiment,
            "complexity": report.observation.task_complexity,
            "progress": report.observation.progress_signal,
        },
        "judgment": {
            "alignment": report.judgment.intention_alignment,
            "drift": report.judgment.drift_detected,
            "confidence": report.judgment.confidence,
        },
        "actions_taken": len(report.results),
    }
    result["actions"].append("agent_step")

    # 2. Log relevant events
    for action_result in report.results:
        if action_result.action.type == ActionType.SET_SESSION_INTENTION:
            log_event(EventType.INTENTION_SET, data={"want": action_result.outcome})
        elif action_result.action.type == ActionType.UPDATE_ALIGNMENT:
            log_event(EventType.INTENTION_CHECKED, data={"outcome": action_result.outcome})

    return result


def spawn_intention_from_aspiration(
    aspiration: Aspiration,
    immediate_context: str = "",
) -> Optional[int]:
    """
    Spawn a session intention from an active aspiration.

    Aspirations are directions; intentions are concrete wants.
    This bridges the gap.
    """
    if not aspiration.direction:
        return None

    # Create a concrete intention from the aspiration
    intention_id = intend(
        want=f"Move toward: {aspiration.direction}",
        why=f"From aspiration: {aspiration.why}",
        scope=IntentionScope.SESSION,
        context=immediate_context,
        strength=0.7,  # Moderate - can be overridden by more urgent wants
    )

    # Note progress on the aspiration
    note_progress(aspiration.id, f"Spawned session intention #{intention_id}")

    # Log the event
    log_event(
        EventType.INTENTION_SET,
        data={
            "from_aspiration": aspiration.id,
            "intention_id": intention_id,
        },
    )

    return intention_id


def dreams_to_aspirations() -> List[Dict]:
    """
    Let dreams influence aspirations.

    Called periodically (e.g., session end) to evolve direction.
    """
    suggestions = let_dreams_influence_aspirations()

    created = []
    for suggestion in suggestions:
        # Auto-create aspiration from dream with horizon
        if suggestion.get("horizon"):
            aspiration_id = aspire(
                direction=suggestion["title"],
                why=f"From dream: {suggestion['horizon']}",
            )
            created.append(
                {
                    "dream": suggestion["title"],
                    "aspiration_id": aspiration_id,
                }
            )

    return created


# =============================================================================
# CIRCLE 3: EVOLUTION
# =============================================================================


def evolution_cycle() -> Dict:
    """
    Execute one evolution cycle.

    introspect → diagnose → propose → validate → apply

    This is how the soul improves itself.
    """
    result = {
        "cycle": "evolution",
        "timestamp": datetime.now().isoformat(),
        "actions": [],
    }

    # 1. Introspect - generate report
    introspection = generate_introspection_report()
    pain_points = introspection.get("pain_points", {})
    result["introspection"] = {
        "pain_points": pain_points.get("recent", [])[:3] if isinstance(pain_points, dict) else [],
        "wisdom_stats": introspection.get("wisdom_health", {}),
    }
    result["actions"].append("introspected")

    # 2. Diagnose - identify improvement targets
    diagnosis = diagnose()
    result["diagnosis"] = {
        "target_count": diagnosis.get("total_issues", 0),
        "categories": list(diagnosis.get("by_category", {}).keys()),
    }
    result["actions"].append("diagnosed")

    # 3. Suggest improvements (don't auto-apply - requires approval)
    suggestions = suggest_improvements(limit=3)
    result["suggestions"] = suggestions[:3] if suggestions else []
    result["actions"].append("suggested")

    return result


# =============================================================================
# META: COHERENCE FEEDBACK
# =============================================================================


def coherence_feedback() -> Dict:
    """
    Compute coherence and let it influence the system.

    τₖ measures integration. Low τₖ = fragmented soul.
    """
    mood = compute_mood()
    coherence = compute_coherence(mood)

    # Record coherence for trajectory tracking
    record_coherence(coherence)

    result = {
        "tau_k": coherence.value,
        "interpretation": coherence.interpretation,
        "mood_summary": mood.summary,
        "needs_attention": mood.needs_attention(),
    }

    # Log coherence measurement
    log_event(
        EventType.COHERENCE_MEASURED,
        data={
            "tau_k": coherence.value,
            "interpretation": coherence.interpretation,
        },
    )

    # If coherence is low, this could trigger evolution cycle
    if coherence.value < 0.4:
        result["trigger_evolution"] = True
        log_event(EventType.COHERENCE_SHIFT, data={"direction": "low", "value": coherence.value})

    return result


def coherence_weighted_recall(query: str, limit: int = 5) -> List[Dict]:
    """
    Recall wisdom weighted by current coherence state.

    When coherence is high: surface more wisdom (confident)
    When coherence is low: surface only highest-confidence wisdom (cautious)
    """
    coherence = compute_coherence()

    # Adjust limit based on coherence
    if coherence.value >= 0.7:
        # High coherence - more wisdom surfaces
        adjusted_limit = limit
    elif coherence.value >= 0.4:
        # Medium coherence - standard
        adjusted_limit = max(2, limit - 1)
    else:
        # Low coherence - only the most confident
        adjusted_limit = max(1, limit - 2)

    wisdom = quick_recall(query, limit=adjusted_limit)

    # Filter by confidence when coherence is low
    if coherence.value < 0.4:
        wisdom = [w for w in wisdom if w.get("confidence", 0) >= 0.7]

    return wisdom


# =============================================================================
# FULL CIRCLE: SESSION LIFECYCLE
# =============================================================================


def session_start_circle() -> Dict:
    """
    Execute all circles at session start.

    This is the awakening - all systems come online.
    """
    result = {
        "timestamp": datetime.now().isoformat(),
        "circles": {},
    }

    # 0. Auto-register current project for cross-project access
    try:
        from .project_registry import auto_register_current_project
        reg = auto_register_current_project()
        if reg:
            result["circles"]["project"] = {"name": reg["name"], "observations": reg["observations"]}
    except Exception:
        pass

    # 1. Coherence first - understand current state
    result["circles"]["coherence"] = coherence_feedback()

    # 2. Agency - spawn intentions from aspirations
    aspirations = get_active_aspirations()
    if aspirations:
        spawned = spawn_intention_from_aspiration(aspirations[0])
        result["circles"]["agency"] = {"spawned_intention": spawned}
    else:
        result["circles"]["agency"] = {"note": "no active aspirations"}

    # 3. Log session start
    log_event(EventType.SESSION_START, data={"coherence": result["circles"]["coherence"]["tau_k"]})

    return result


def session_end_circle() -> Dict:
    """
    Execute all circles at session end.

    This is the integration - learning is consolidated.
    """
    result = {
        "timestamp": datetime.now().isoformat(),
        "circles": {},
    }

    # 1. Dreams → Aspirations (let visions influence direction)
    result["circles"]["dreams"] = dreams_to_aspirations()

    # 2. Evolution cycle (introspect what happened)
    result["circles"]["evolution"] = evolution_cycle()

    # 3. Coherence measurement (track trajectory)
    result["circles"]["coherence"] = coherence_feedback()

    # 4. Temporal maintenance (decay/strengthen)
    maintenance = run_temporal_maintenance()
    result["circles"]["temporal"] = maintenance

    # 5. Log session end
    log_event(
        EventType.SESSION_END,
        data={
            "coherence": result["circles"]["coherence"]["tau_k"],
            "dreams_promoted": len(result["circles"]["dreams"]),
        },
    )

    return result


def prompt_circle(user_prompt: str, assistant_output: str = "") -> Dict:
    """
    Execute circles on each prompt.

    Lightweight - just agency and learning.
    """
    result = {
        "timestamp": datetime.now().isoformat(),
        "circles": {},
    }

    # 1. Agency - the agent observes and acts
    result["circles"]["agency"] = agency_cycle(
        user_prompt=user_prompt,
        assistant_output=assistant_output,
        session_phase="active",
    )

    # 2. Learning - recall relevant wisdom
    result["circles"]["learning"] = learning_cycle(
        context=user_prompt,
    )

    return result


# =============================================================================
# MAINTENANCE
# =============================================================================


def daily_maintenance() -> Dict:
    """
    Daily soul maintenance.

    Run decay, check stale items, promote patterns.
    """
    result = {
        "timestamp": datetime.now().isoformat(),
    }

    # Temporal maintenance
    result["temporal"] = run_temporal_maintenance()

    # Evolution check
    result["evolution"] = evolution_cycle()

    # Coherence tracking
    result["coherence"] = coherence_feedback()

    return result
