"""
Curiosity Engine: The soul asks questions when it senses gaps.

Instead of passively waiting for wisdom, the soul actively identifies
what it doesn't know and formulates questions to fill those gaps.

Gap detection sources:
- Recurring problems without learned patterns
- Repeated corrections in the same area
- Frequently touched files with no hints
- Decisions made without rationale
- New domains without vocabulary
"""

import json
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from typing import Dict, List, Optional
from collections import Counter

# TODO: Migrate to synapse graph storage
# from .core import get_synapse_graph, save_synapse


class GapType(str, Enum):
    RECURRING_PROBLEM = "recurring_problem"  # Same issue keeps appearing
    REPEATED_CORRECTION = "repeated_correction"  # User corrects same mistake
    UNKNOWN_FILE = "unknown_file"  # File touched but no hints
    MISSING_RATIONALE = "missing_rationale"  # Decision without explanation
    NEW_DOMAIN = "new_domain"  # Unfamiliar territory
    STALE_WISDOM = "stale_wisdom"  # Old wisdom never applied
    FAILED_PATTERN = "failed_pattern"  # Pattern that keeps failing
    CONTRADICTION = "contradiction"  # Conflicting beliefs
    INTENTION_TENSION = "intention_tension"  # Competing intentions
    UNCERTAINTY = "uncertainty"  # Detected uncertainty in reasoning
    USER_BEHAVIOR = "user_behavior"  # Something user does we don't understand


class QuestionStatus(str, Enum):
    PENDING = "pending"  # Not yet asked
    ASKED = "asked"  # Asked but not answered
    ANSWERED = "answered"  # User provided answer
    DISMISSED = "dismissed"  # User dismissed as not relevant
    INCORPORATED = "incorporated"  # Answer turned into wisdom


@dataclass
class Gap:
    """A detected knowledge gap."""

    id: str
    type: GapType
    description: str
    evidence: List[str]  # What triggered this gap detection
    priority: float  # 0-1, higher = more important
    detected_at: str = field(default_factory=lambda: datetime.now().isoformat())
    occurrences: int = 1
    related_files: List[str] = field(default_factory=list)
    related_concepts: List[str] = field(default_factory=list)


@dataclass
class Question:
    """A question the soul wants to ask."""

    id: int
    gap_id: str
    question: str
    context: str  # Why the soul is asking
    priority: float
    status: QuestionStatus = QuestionStatus.PENDING
    asked_at: Optional[str] = None
    answered_at: Optional[str] = None
    answer: Optional[str] = None
    created_at: str = field(default_factory=lambda: datetime.now().isoformat())


def _ensure_curiosity_tables():
    """Create curiosity tables if they don't exist."""
    # TODO: Migrate to synapse graph storage
    pass


# =============================================================================
# GAP DETECTION
# =============================================================================


def detect_recurring_problems() -> List[Gap]:
    """Detect problems that keep occurring without learned patterns."""
    # TODO: Migrate to synapse graph storage
    return []


def detect_repeated_corrections() -> List[Gap]:
    """Detect areas where user keeps correcting the same mistakes."""
    # TODO: Migrate to synapse graph storage
    return []


def detect_unknown_files() -> List[Gap]:
    """Detect files that are frequently accessed but have no hints."""
    # TODO: Migrate to synapse graph storage
    return []


def detect_missing_rationale() -> List[Gap]:
    """Detect decisions made without clear rationale."""
    # TODO: Migrate to synapse graph storage
    return []


def detect_new_domains() -> List[Gap]:
    """Detect domains encountered without vocabulary or wisdom."""
    # TODO: Migrate to synapse graph storage
    return []


def detect_stale_wisdom() -> List[Gap]:
    """Detect wisdom that was never applied or is decaying."""
    # TODO: Migrate to synapse graph storage
    return []


def detect_contradictions() -> List[Gap]:
    """Detect conflicting beliefs or wisdom entries."""
    # TODO: Migrate to synapse graph storage
    return []


def detect_intention_tensions() -> List[Gap]:
    """Detect competing or conflicting intentions."""
    # TODO: Migrate to synapse graph storage
    return []


def detect_uncertainty_signals(assistant_output: str) -> List[Gap]:
    """Detect uncertainty in assistant output that suggests knowledge gaps."""
    gaps = []

    uncertainty_markers = [
        ("I'm not sure", 0.7),
        ("I think", 0.4),
        ("possibly", 0.5),
        ("might be", 0.5),
        ("could be", 0.4),
        ("I believe", 0.4),
        ("probably", 0.5),
        ("unclear", 0.6),
        ("uncertain", 0.7),
        ("I don't know", 0.8),
        ("not certain", 0.7),
        ("may or may not", 0.6),
        ("hard to say", 0.6),
    ]

    output_lower = assistant_output.lower()

    for marker, priority in uncertainty_markers:
        if marker.lower() in output_lower:
            # Find the context around the marker
            idx = output_lower.find(marker.lower())
            start = max(0, idx - 50)
            end = min(len(assistant_output), idx + len(marker) + 100)
            context = assistant_output[start:end].strip()

            gap = Gap(
                id=f"uncertainty_{hash(marker + context[:30]) % 100000}",
                type=GapType.UNCERTAINTY,
                description=f"Expressed uncertainty: '{marker}'",
                evidence=[f"Context: ...{context}..."],
                priority=priority,
            )
            gaps.append(gap)

    return gaps


def detect_user_behavior_patterns() -> List[Gap]:
    """Detect user behaviors we observe but don't understand."""
    # TODO: Migrate to synapse graph storage
    return []


def detect_all_gaps(assistant_output: Optional[str] = None) -> List[Gap]:
    """Run all gap detection and return combined results.

    Args:
        assistant_output: Optional recent assistant output to analyze for uncertainty
    """
    all_gaps = []

    detectors = [
        detect_recurring_problems,
        detect_repeated_corrections,
        detect_unknown_files,
        detect_missing_rationale,
        detect_new_domains,
        detect_stale_wisdom,
        detect_contradictions,
        detect_intention_tensions,
        detect_user_behavior_patterns,
    ]

    for detector in detectors:
        try:
            all_gaps.extend(detector())
        except Exception:
            pass

    # Detect uncertainty from output if provided
    if assistant_output:
        try:
            all_gaps.extend(detect_uncertainty_signals(assistant_output))
        except Exception:
            pass

    # Sort by priority
    all_gaps.sort(key=lambda g: -g.priority)

    return all_gaps


def save_gap(gap: Gap) -> str:
    """Save a detected gap to the database."""
    # TODO: Migrate to synapse graph storage
    return gap.id


# =============================================================================
# QUESTION GENERATION
# =============================================================================


def generate_question(gap: Gap) -> Question:
    """Generate a natural question from a gap."""
    templates = {
        GapType.RECURRING_PROBLEM: [
            "I keep encountering {desc} - what's the right approach to solve this?",
            "This problem pattern keeps appearing: {desc}. How should I handle it?",
        ],
        GapType.REPEATED_CORRECTION: [
            "You've corrected me on this before: {desc}. What should I remember?",
            "I seem to keep making this mistake: {desc}. What's the right way?",
        ],
        GapType.UNKNOWN_FILE: [
            "I often work with {files} but don't understand its purpose. What does it do?",
            "What's the role of {files} in this codebase?",
        ],
        GapType.MISSING_RATIONALE: [
            "We decided on {desc}, but I don't know why. What was the reasoning?",
            "Can you explain the rationale behind: {desc}?",
        ],
        GapType.NEW_DOMAIN: [
            "I'm working in {concepts} but don't have much context. What should I know?",
            "What are the key concepts I should understand about {concepts}?",
        ],
        GapType.STALE_WISDOM: [
            "I recorded '{desc}' a while ago but never used it. Is it still relevant?",
            "Should I keep or update this old wisdom: {desc}?",
        ],
        GapType.FAILED_PATTERN: [
            "This approach keeps failing: {desc}. What's a better alternative?",
            "I've tried {desc} multiple times without success. What am I missing?",
        ],
        GapType.CONTRADICTION: [
            "I notice {desc}. Which principle should take precedence?",
            "These seem to conflict: {desc}. How do I reconcile them?",
        ],
        GapType.INTENTION_TENSION: [
            "I'm pulled in different directions: {desc}. Which should I prioritize?",
            "There's tension between intentions: {desc}. How should I resolve this?",
        ],
        GapType.UNCERTAINTY: [
            "I expressed uncertainty about {desc}. Can you clarify?",
            "I wasn't confident about {desc}. What's the definitive answer?",
        ],
        GapType.USER_BEHAVIOR: [
            "I've noticed {desc}. Is there a reason for this pattern?",
            "Help me understand: {desc}",
        ],
    }

    template_list = templates.get(gap.type, ["Can you help me understand: {desc}?"])
    template = template_list[hash(gap.id) % len(template_list)]

    # Fill template
    question_text = template.format(
        desc=gap.description[:100],
        files=", ".join(gap.related_files[:3]) if gap.related_files else "these files",
        concepts=", ".join(gap.related_concepts[:3])
        if gap.related_concepts
        else "this area",
    )

    # Generate context
    context_parts = [f"Gap type: {gap.type.value}"]
    if gap.evidence:
        context_parts.append(f"Evidence: {gap.evidence[0][:80]}")
    if gap.occurrences > 1:
        context_parts.append(f"Occurred {gap.occurrences} times")

    return Question(
        id=0,  # Will be assigned by DB
        gap_id=gap.id,
        question=question_text,
        context="; ".join(context_parts),
        priority=gap.priority,
        status=QuestionStatus.PENDING,
    )


def save_question(question: Question) -> int:
    """Save a question to the database."""
    # TODO: Migrate to synapse graph storage
    return 0


def get_pending_questions(limit: int = 10) -> List[Question]:
    """Get questions that haven't been asked yet."""
    # TODO: Migrate to synapse graph storage
    return []


def mark_question_asked(question_id: int) -> bool:
    """Mark a question as asked."""
    # TODO: Migrate to synapse graph storage
    return False


def answer_question(question_id: int, answer: str, incorporate: bool = False) -> bool:
    """Record an answer to a question."""
    # TODO: Migrate to synapse graph storage
    return False


def dismiss_question(question_id: int) -> bool:
    """Dismiss a question as not relevant."""
    # TODO: Migrate to synapse graph storage
    return False


# =============================================================================
# CURIOSITY CYCLE
# =============================================================================


def run_curiosity_cycle(max_questions: int = 5) -> List[Question]:
    """
    Run a full curiosity cycle:
    1. Detect gaps
    2. Generate questions for new gaps
    3. Return prioritized questions to ask

    This should be called periodically (e.g., at session start or end).
    """
    # Detect gaps
    gaps = detect_all_gaps()

    # Save gaps and generate questions
    for gap in gaps[: max_questions * 2]:  # Generate more than needed, then prioritize
        save_gap(gap)
        question = generate_question(gap)
        save_question(question)

    # Return top questions
    return get_pending_questions(limit=max_questions)


def get_curiosity_stats() -> Dict:
    """Get statistics about the curiosity engine."""
    # TODO: Migrate to synapse graph storage
    return {
        "open_gaps": 0,
        "gaps_by_type": {},
        "questions": {
            "pending": 0,
            "answered": 0,
            "incorporated": 0,
            "dismissed": 0,
            "total": 0,
        },
        "incorporation_rate": 0.0,
    }


def format_questions_for_prompt(
    questions: List[Question], max_questions: int = 3
) -> str:
    """Format questions for injection into a prompt."""
    if not questions:
        return ""

    lines = ["## Soul's Questions", ""]
    lines.append("I've noticed some gaps in my understanding. When you have a moment:")
    lines.append("")

    for i, q in enumerate(questions[:max_questions], 1):
        lines.append(f"{i}. {q.question}")
        if q.context:
            lines.append(f"   _({q.context})_")

    lines.append("")
    lines.append("_Answer any of these to help me learn, or dismiss if not relevant._")

    return "\n".join(lines)


def incorporate_answer_as_wisdom(
    question_id: int, wisdom_type: str = "insight"
) -> Optional[int]:
    """Turn an answered question into wisdom."""
    # TODO: Migrate to synapse graph storage
    return None
