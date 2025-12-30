"""
Antahkarana Assessment — Multi-Voice Compaction Assessment

Sanskrit: अन्तःकरण (antaḥkaraṇa) — "inner instrument"
The fourfold inner instrument of cognition in Vedantic philosophy.

This module uses the Antahkarana (multi-voice system) to assess
what should be preserved before context compaction. Rather than
mechanical transcript parsing, we use cognitive voices to determine
what matters.

Voice Roles in Assessment:
    Manas (मनस्)    - Quick emotional read on significance
    Buddhi (बुद्धि)  - Deep analysis of critical decisions
    Chitta (चित्त)   - Pattern matching with past important work
    Ahamkara (अहंकार) - Self-preservation, what threatens progress
"""

from dataclasses import dataclass, field
from typing import List, Dict, Optional, Any
from enum import Enum, auto


class PreservationPriority(Enum):
    """Priority levels for items to preserve."""
    CRITICAL = auto()   # Must preserve, cannot be summarized
    IMPORTANT = auto()  # Should preserve, core context
    SECONDARY = auto()  # Can be summarized
    NOISE = auto()      # Can be dropped


@dataclass
class SessionContext:
    """Current session state for assessment."""
    todos: List[Dict] = field(default_factory=list)
    files_touched: List[str] = field(default_factory=list)
    errors: List[str] = field(default_factory=list)
    last_message: str = ""
    key_decisions: List[str] = field(default_factory=list)
    active_intentions: List[Dict] = field(default_factory=list)

    def summary(self) -> str:
        """Create a summary for voice prompts."""
        parts = []

        if self.todos:
            pending = [t for t in self.todos if t.get("status") == "pending"]
            in_progress = [t for t in self.todos if t.get("status") == "in_progress"]
            if in_progress:
                parts.append(f"In progress: {', '.join(t.get('content', '')[:50] for t in in_progress[:3])}")
            if pending:
                parts.append(f"Pending: {len(pending)} todos")

        if self.files_touched:
            parts.append(f"Files: {', '.join(self.files_touched[:5])}")

        if self.errors:
            parts.append(f"Errors encountered: {len(self.errors)}")

        if self.key_decisions:
            parts.append(f"Decisions: {len(self.key_decisions)}")

        if self.active_intentions:
            parts.append(f"Active intentions: {len(self.active_intentions)}")

        return " | ".join(parts) if parts else "No context captured"


@dataclass
class CompactionPlan:
    """Plan for what to preserve during compaction."""
    # Items that must be preserved exactly
    critical_items: List[Dict] = field(default_factory=list)

    # Items that can be summarized
    secondary_items: List[Dict] = field(default_factory=list)

    # Items that can be dropped
    droppable: List[str] = field(default_factory=list)

    # Continuation hint
    continuation_hint: str = ""

    # Voice insights that led to this plan
    voice_insights: Dict[str, str] = field(default_factory=dict)

    # Overall confidence in the assessment
    confidence: float = 0.0


def _build_assessment_prompt(context: SessionContext, voice: str) -> str:
    """
    Build voice-specific assessment prompt.

    Each voice brings its unique perspective to what matters.
    """
    base_context = f"""## Session Context for Assessment

{context.summary()}

### Recent Activity
- Last message: {context.last_message[:200] if context.last_message else 'None'}
- Files touched: {', '.join(context.files_touched[:10]) if context.files_touched else 'None'}
- Errors: {len(context.errors)} encountered

### Active Todos
"""
    for todo in context.todos[:10]:
        status = todo.get("status", "?")
        content = todo.get("content", "")[:80]
        base_context += f"- [{status}] {content}\n"

    if context.key_decisions:
        base_context += "\n### Key Decisions\n"
        for d in context.key_decisions[:5]:
            base_context += f"- {d[:100]}\n"

    # Voice-specific prompts
    voice_prompts = {
        "manas": f"""## Your Role: Manas (Quick Intuition)

{base_context}

### Your Assessment Task

As Manas (मनस्), provide a quick intuitive read:
1. What feels emotionally significant about this work?
2. What would be lost if we forgot all of this?
3. Rate urgency of each active todo (high/medium/low)

Respond with your gut reaction - don't overthink it.
Format: Brief assessment (2-3 sentences per question)
""",

        "buddhi": f"""## Your Role: Buddhi (Deep Analysis)

{base_context}

### Your Assessment Task

As Buddhi (बुद्धि), provide deep discriminating analysis:
1. Which decisions have the most far-reaching implications?
2. What patterns or principles emerged from this work?
3. What technical context is absolutely essential to preserve?
4. Rank items by logical importance for continuation.

Be thorough and precise. This analysis guides what survives compaction.
Format: Structured analysis with priority rankings.
""",

        "chitta": f"""## Your Role: Chitta (Pattern Memory)

{base_context}

### Your Assessment Task

As Chitta (चित्त), search for patterns:
1. How does this work relate to past patterns we've seen?
2. What approaches worked well that we should remember?
3. What failures occurred that we must not repeat?
4. What context links this session to larger ongoing work?

Draw on accumulated experience to contextualize this session.
Format: Pattern observations with references to past work.
""",

        "ahamkara": f"""## Your Role: Ahamkara (Self-Preservation)

{base_context}

### Your Assessment Task

As Ahamkara (अहंकार), assess threats and protection:
1. What could go wrong if we lose this context?
2. What blockers or risks should we highlight?
3. What active work would be disrupted by memory loss?
4. What user expectations/promises must we remember?

Be protective and cautious. Identify what we absolutely cannot lose.
Format: Risk assessment with preservation priorities.
""",
    }

    return voice_prompts.get(voice, base_context)


def assess_with_voices(
    context: SessionContext,
    voices: List[str] = None,
    timeout: int = 120,
    use_simulation: bool = True,
) -> CompactionPlan:
    """
    Use Antahkarana voices to assess what to preserve.

    Args:
        context: Current session context
        voices: Which voices to use (default: manas, buddhi, chitta, ahamkara)
        timeout: Max seconds to wait for voice responses
        use_simulation: Use simulated voices (fast) or spawn real agents (slow)

    Returns:
        CompactionPlan with preservation priorities
    """
    if voices is None:
        voices = ["manas", "buddhi", "chitta", "ahamkara"]

    plan = CompactionPlan()

    if use_simulation:
        # Fast path: simulate voice responses using heuristics
        plan = _simulate_voice_assessment(context, voices)
    else:
        # Slow path: spawn real agents
        plan = _spawn_voice_assessment(context, voices, timeout)

    return plan


def _simulate_voice_assessment(
    context: SessionContext,
    voices: List[str],
) -> CompactionPlan:
    """
    Simulate voice assessment using heuristics.

    This is the fast path for when we don't have time to spawn agents.
    Each voice contributes its perspective through rule-based analysis.
    """
    plan = CompactionPlan()
    insights = {}

    # === Manas: Quick emotional read ===
    if "manas" in voices:
        manas_insight = []

        # In-progress todos are emotionally salient
        in_progress = [t for t in context.todos if t.get("status") == "in_progress"]
        if in_progress:
            manas_insight.append(f"Active work feels important: {len(in_progress)} items")
            for t in in_progress:
                plan.critical_items.append({
                    "type": "todo",
                    "content": t.get("content", ""),
                    "priority": PreservationPriority.CRITICAL,
                    "source": "manas",
                })

        # Errors feel urgent
        if context.errors:
            manas_insight.append(f"Errors need attention: {len(context.errors)}")
            plan.critical_items.append({
                "type": "errors",
                "content": context.errors[:3],
                "priority": PreservationPriority.CRITICAL,
                "source": "manas",
            })

        insights["manas"] = " | ".join(manas_insight) if manas_insight else "No strong signals"

    # === Buddhi: Deep logical analysis ===
    if "buddhi" in voices:
        buddhi_insight = []

        # Key decisions are logically important
        if context.key_decisions:
            buddhi_insight.append(f"Decisions to preserve: {len(context.key_decisions)}")
            for d in context.key_decisions[:5]:
                plan.critical_items.append({
                    "type": "decision",
                    "content": d,
                    "priority": PreservationPriority.CRITICAL,
                    "source": "buddhi",
                })

        # Files touched need tracking
        if context.files_touched:
            buddhi_insight.append(f"Technical context: {len(context.files_touched)} files")
            plan.secondary_items.append({
                "type": "files",
                "content": context.files_touched,
                "priority": PreservationPriority.IMPORTANT,
                "source": "buddhi",
            })

        insights["buddhi"] = " | ".join(buddhi_insight) if buddhi_insight else "Light session"

    # === Chitta: Pattern matching ===
    if "chitta" in voices:
        chitta_insight = []

        # Pending todos represent ongoing patterns
        pending = [t for t in context.todos if t.get("status") == "pending"]
        if pending:
            chitta_insight.append(f"Ongoing work pattern: {len(pending)} pending items")
            plan.secondary_items.append({
                "type": "pending_todos",
                "content": pending,
                "priority": PreservationPriority.IMPORTANT,
                "source": "chitta",
            })

        # Last message captures context
        if context.last_message:
            chitta_insight.append("Context captured in last message")
            plan.secondary_items.append({
                "type": "context",
                "content": context.last_message[:500],
                "priority": PreservationPriority.SECONDARY,
                "source": "chitta",
            })

        insights["chitta"] = " | ".join(chitta_insight) if chitta_insight else "No patterns"

    # === Ahamkara: Protection ===
    if "ahamkara" in voices:
        ahamkara_insight = []

        # Active intentions must be protected
        if context.active_intentions:
            ahamkara_insight.append(f"Protect {len(context.active_intentions)} intentions")
            for i in context.active_intentions[:5]:
                plan.critical_items.append({
                    "type": "intention",
                    "content": i.get("want", ""),
                    "priority": PreservationPriority.CRITICAL,
                    "source": "ahamkara",
                })

        # In-progress work represents commitment
        in_progress = [t for t in context.todos if t.get("status") == "in_progress"]
        if in_progress:
            ahamkara_insight.append(f"Commitment to {len(in_progress)} active tasks")

        insights["ahamkara"] = " | ".join(ahamkara_insight) if ahamkara_insight else "No threats"

    # Synthesize continuation hint
    in_progress = [t for t in context.todos if t.get("status") == "in_progress"]
    if in_progress:
        plan.continuation_hint = f"Continue: {in_progress[0].get('content', 'current task')}"
    elif context.key_decisions:
        plan.continuation_hint = f"Follow up on: {context.key_decisions[0][:80]}"
    elif context.errors:
        plan.continuation_hint = f"Address errors from: {context.files_touched[0] if context.files_touched else 'session'}"
    else:
        plan.continuation_hint = "Review and continue"

    # Set voice insights
    plan.voice_insights = insights

    # Compute confidence based on how much we found
    total_items = len(plan.critical_items) + len(plan.secondary_items)
    plan.confidence = min(0.5 + (total_items * 0.1), 0.95)

    return plan


def _spawn_voice_assessment(
    context: SessionContext,
    voices: List[str],
    timeout: int,
) -> CompactionPlan:
    """
    Spawn real Claude agents as voices.

    This is slower but provides genuine multi-perspective assessment.
    Uses the swarm_spawner module for actual agent spawning.
    """
    try:
        from .swarm_spawner import spawn_swarm, get_swarm_solutions
    except ImportError:
        # Swarm spawner not available, fall back to simulation
        return _simulate_voice_assessment(context, voices)

    # Map voice names to perspective strings
    voice_to_perspective = {
        "manas": "fast",       # Quick intuition
        "buddhi": "deep",      # Deep analysis
        "chitta": "pragmatic", # Pattern memory
        "ahamkara": "critical", # Self-preservation
    }

    perspectives = ",".join(
        voice_to_perspective.get(v, "fast")
        for v in voices
    )

    # Create the swarm problem
    problem = f"""Assess this session for compaction preservation:

{context.summary()}

Determine what must be preserved, what can be summarized, and what can be dropped.
Consider:
1. Active work in progress
2. Key decisions made
3. Errors that need attention
4. Context that would be lost
"""

    # Spawn swarm
    result = spawn_swarm(
        problem=problem,
        perspectives=perspectives,
        wait=True,
        timeout=timeout,
    )

    # Parse solutions into compaction plan
    plan = CompactionPlan()

    if result.get("converged"):
        converged = result["converged"]
        plan.continuation_hint = converged.get("solution", "")[:200]
        plan.confidence = converged.get("confidence", 0.5)

    # Get individual voice insights
    solutions = get_swarm_solutions(result.get("swarm_id", ""))
    for sol in solutions:
        perspective = sol.get("perspective", "unknown")
        content = sol.get("content", "")

        # Map perspective back to voice
        perspective_to_voice = {
            "fast": "manas",
            "deep": "buddhi",
            "pragmatic": "chitta",
            "critical": "ahamkara",
        }
        voice = perspective_to_voice.get(perspective, perspective)
        plan.voice_insights[voice] = content[:500]

    return plan


def format_assessment_for_ledger(plan: CompactionPlan) -> str:
    """
    Format the assessment plan for inclusion in session ledger.
    """
    lines = []

    lines.append("## Antahkarana Assessment")
    lines.append("")

    if plan.continuation_hint:
        lines.append(f"**Continue:** {plan.continuation_hint}")
        lines.append("")

    if plan.critical_items:
        lines.append("### Critical (Must Preserve)")
        for item in plan.critical_items[:10]:
            content = item.get("content", "")
            if isinstance(content, list):
                content = ", ".join(str(c)[:50] for c in content[:3])
            elif isinstance(content, str):
                content = content[:100]
            lines.append(f"- [{item.get('type')}] {content}")
        lines.append("")

    if plan.secondary_items:
        lines.append("### Secondary (Can Summarize)")
        for item in plan.secondary_items[:5]:
            lines.append(f"- [{item.get('type')}] ...")
        lines.append("")

    if plan.voice_insights:
        lines.append("### Voice Insights")
        for voice, insight in plan.voice_insights.items():
            lines.append(f"- **{voice}**: {insight[:100]}...")
        lines.append("")

    lines.append(f"*Confidence: {plan.confidence:.0%}*")

    return "\n".join(lines)


def quick_assessment(
    todos: List[Dict] = None,
    files: List[str] = None,
    errors: List[str] = None,
    last_message: str = "",
) -> CompactionPlan:
    """
    Quick assessment convenience function.

    Uses simulation for speed.
    """
    context = SessionContext(
        todos=todos or [],
        files_touched=files or [],
        errors=errors or [],
        last_message=last_message,
    )

    return assess_with_voices(context, use_simulation=True)
