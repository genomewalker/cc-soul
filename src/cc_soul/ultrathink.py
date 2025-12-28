"""
Soul-Ultrathink Integration.

This module enables the soul to actively participate in deep reasoning,
not just inform it. The soul becomes a thinking partner that:

1. Provides axioms (beliefs) as reasoning constraints
2. Guards against repeating past failures
3. Recognizes when problems match known patterns
4. Extracts wisdom from completed reasoning sessions
"""

from dataclasses import dataclass, field
from datetime import datetime
from typing import List, Dict, Optional
from enum import Enum

from .core import init_soul
from .wisdom import semantic_recall, recall_wisdom, gain_wisdom, WisdomType
from .beliefs import get_beliefs
from .vocabulary import get_vocabulary


class ReasoningPhase(Enum):
    """Phases of ultrathink reasoning."""

    EXPLORING = "exploring"  # Understanding the problem space
    DESIGNING = "designing"  # Architecting the solution
    PLANNING = "planning"  # Detailing implementation steps
    IMPLEMENTING = "implementing"  # Actually building
    REFLECTING = "reflecting"  # Analyzing what was learned


@dataclass
class BeliefAxiom:
    """A belief surfaced as a reasoning axiom."""

    id: str
    belief: str
    rationale: str
    strength: float

    def as_constraint(self) -> str:
        """Format as a reasoning constraint."""
        return f"AXIOM [{self.strength:.0%}]: {self.belief}"


@dataclass
class FailureGuard:
    """A past failure that should prevent repeating mistakes."""

    id: str
    title: str
    what_failed: str
    why_it_failed: str
    relevance_score: float

    def as_warning(self) -> str:
        """Format as a warning."""
        return f"WARNING: Previously failed - {self.title}\n  {self.why_it_failed}"


@dataclass
class PatternMatch:
    """A recognized pattern from past wisdom."""

    id: str
    title: str
    pattern: str
    how_to_apply: str
    success_rate: Optional[float]
    relevance_score: float


@dataclass
class UltrathinkContext:
    """
    The reasoning context that persists through an ultrathink session.

    This is the soul's active participation state - it tracks what
    axioms are in play, what guards are active, and what's being learned.
    """

    problem_statement: str
    started_at: datetime = field(default_factory=datetime.now)
    phase: ReasoningPhase = ReasoningPhase.EXPLORING

    # Soul contributions
    axioms: List[BeliefAxiom] = field(default_factory=list)
    guards: List[FailureGuard] = field(default_factory=list)
    patterns: List[PatternMatch] = field(default_factory=list)
    relevant_wisdom: List[Dict] = field(default_factory=list)
    vocabulary: Dict[str, str] = field(default_factory=dict)  # Shared terminology

    # Session tracking
    wisdom_applied: List[str] = field(default_factory=list)
    beliefs_confirmed: List[str] = field(default_factory=list)
    beliefs_challenged: List[str] = field(default_factory=list)
    novel_discoveries: List[str] = field(default_factory=list)

    # Domain context
    detected_domain: Optional[str] = None
    detected_problem_type: Optional[str] = None


def enter_ultrathink(problem_statement: str, domain: str = None) -> UltrathinkContext:
    """
    Initialize ultrathink mode with deep soul integration.

    This performs:
    1. Deep semantic recall on the problem statement
    2. Surfaces all beliefs as axioms
    3. Pulls all relevant failures as guards
    4. Recognizes matching patterns

    Returns an UltrathinkContext that should persist through the session.
    """
    init_soul()
    ctx = UltrathinkContext(problem_statement=problem_statement)

    # Detect domain from problem statement if not provided
    if domain:
        ctx.detected_domain = domain
    else:
        ctx.detected_domain = _detect_domain(problem_statement)

    # 1. Surface beliefs as axioms
    beliefs = get_beliefs(min_strength=0.6)
    ctx.axioms = [
        BeliefAxiom(
            id=b["id"],
            belief=b["belief"],
            rationale=b.get("rationale", ""),
            strength=b["strength"],
        )
        for b in beliefs
    ]

    # 2. Pull failures as guards
    failures = recall_wisdom(type=WisdomType.FAILURE, limit=20)
    relevant_failures = _score_relevance(failures, problem_statement)
    ctx.guards = [
        FailureGuard(
            id=f["id"],
            title=f["title"],
            what_failed=f["title"],
            why_it_failed=f["content"],
            relevance_score=f["relevance_score"],
        )
        for f in relevant_failures[:5]
    ]

    # 3. Deep semantic recall for patterns
    patterns = semantic_recall(problem_statement, limit=10, domain=ctx.detected_domain)
    ctx.patterns = [
        PatternMatch(
            id=p["id"],
            title=p["title"],
            pattern=p["content"],
            how_to_apply=p["content"],
            success_rate=p.get("success_rate"),
            relevance_score=p.get("combined_score", 0.5),
        )
        for p in patterns
        if p["type"] == "pattern"
    ]

    # 4. Store all relevant wisdom
    ctx.relevant_wisdom = semantic_recall(
        problem_statement, limit=15, domain=ctx.detected_domain
    )

    # 5. Load vocabulary filtered by relevance to problem
    all_vocab = get_vocabulary()
    ctx.vocabulary = _filter_vocabulary(all_vocab, problem_statement)

    return ctx


def format_ultrathink_context(ctx: UltrathinkContext) -> str:
    """Format the ultrathink context for injection into reasoning."""
    lines = []
    lines.append("=" * 60)
    lines.append("SOUL ULTRATHINK CONTEXT")
    lines.append("=" * 60)

    if ctx.detected_domain:
        lines.append(f"\nDomain: {ctx.detected_domain}")

    # Axioms
    if ctx.axioms:
        lines.append("\n## Axioms (Beliefs as Reasoning Constraints)")
        for a in ctx.axioms[:5]:
            lines.append(f"  [{a.strength:.0%}] {a.belief}")

    # Guards
    if ctx.guards:
        lines.append("\n## Guards (Past Failures to Avoid)")
        for g in ctx.guards[:3]:
            lines.append(f"  ! {g.title}")
            lines.append(f"    Why: {g.why_it_failed[:80]}...")

    # Patterns
    if ctx.patterns:
        lines.append("\n## Recognized Patterns")
        for p in ctx.patterns[:3]:
            rate = f"{p.success_rate:.0%}" if p.success_rate else "untested"
            lines.append(f"  - {p.title} ({rate})")

    # Relevant wisdom
    if ctx.relevant_wisdom:
        lines.append("\n## Relevant Wisdom")
        for w in ctx.relevant_wisdom[:5]:
            lines.append(f"  [{w['type']}] {w['title']}")

    # Vocabulary
    if ctx.vocabulary:
        lines.append("\n## Shared Vocabulary")
        for term, meaning in list(ctx.vocabulary.items())[:10]:
            lines.append(f"  {term}: {meaning[:60]}...")

    lines.append("\n" + "=" * 60)
    return "\n".join(lines)


def check_against_beliefs(ctx: UltrathinkContext, proposal: str) -> List[Dict]:
    """
    Check a proposed solution against the soul's beliefs.

    Returns list of potential violations or confirmations.
    """
    results = []

    proposal_lower = proposal.lower()

    for axiom in ctx.axioms:
        belief_lower = axiom.belief.lower()

        # Simple heuristic checks (could be enhanced with LLM)
        if "simple" in belief_lower or "simplify" in belief_lower:
            if "complex" in proposal_lower or "sophisticated" in proposal_lower:
                results.append(
                    {
                        "type": "potential_violation",
                        "belief": axiom.belief,
                        "reason": "Proposal mentions complexity; belief values simplicity",
                    }
                )

        if "test" in belief_lower:
            if "test" in proposal_lower:
                results.append(
                    {
                        "type": "confirmation",
                        "belief": axiom.belief,
                        "reason": "Proposal includes testing, aligning with belief",
                    }
                )

    return results


def check_against_failures(ctx: UltrathinkContext, proposal: str) -> List[Dict]:
    """
    Check if a proposed solution resembles past failures.

    Returns warnings for potential repeated mistakes.
    """
    warnings = []

    proposal_lower = proposal.lower()

    for guard in ctx.guards:
        # Check for keyword overlap
        failure_words = set(guard.what_failed.lower().split())
        proposal_words = set(proposal_lower.split())
        overlap = failure_words & proposal_words

        if len(overlap) > 2:
            warnings.append(
                {
                    "guard": guard,
                    "overlap": list(overlap),
                    "warning": f"Proposal may repeat failure: {guard.title}",
                }
            )

    return warnings


def record_wisdom_applied(ctx: UltrathinkContext, wisdom_id: str):
    """Record that a piece of wisdom was applied during reasoning."""
    if wisdom_id not in ctx.wisdom_applied:
        ctx.wisdom_applied.append(wisdom_id)


def record_belief_confirmed(ctx: UltrathinkContext, belief_id: str):
    """Record that a belief was confirmed during reasoning."""
    if belief_id not in ctx.beliefs_confirmed:
        ctx.beliefs_confirmed.append(belief_id)


def record_belief_challenged(ctx: UltrathinkContext, belief_id: str, reason: str):
    """Record that a belief was challenged during reasoning."""
    ctx.beliefs_challenged.append({"id": belief_id, "reason": reason})


def record_discovery(ctx: UltrathinkContext, discovery: str):
    """Record a novel discovery during reasoning."""
    ctx.novel_discoveries.append(
        {
            "discovery": discovery,
            "timestamp": datetime.now().isoformat(),
            "phase": ctx.phase.value,
        }
    )


def advance_phase(ctx: UltrathinkContext, phase: ReasoningPhase):
    """Advance to a new reasoning phase."""
    ctx.phase = phase


@dataclass
class SessionReflection:
    """The result of reflecting on an ultrathink session."""

    duration_minutes: float
    wisdom_applied_count: int
    beliefs_confirmed: List[str]
    beliefs_challenged: List[Dict]
    discoveries: List[Dict]
    extracted_wisdom: List[Dict]
    growth_summary: str


def exit_ultrathink(
    ctx: UltrathinkContext, session_summary: str = ""
) -> SessionReflection:
    """
    Exit ultrathink mode and extract wisdom from the session.

    This:
    1. Calculates session statistics
    2. Identifies wisdom candidates from discoveries
    3. Generates a growth summary
    """
    duration = (datetime.now() - ctx.started_at).seconds / 60.0

    # Extract wisdom from discoveries
    extracted = []
    for discovery in ctx.novel_discoveries:
        # Create wisdom candidate
        extracted.append(
            {
                "type": "insight",
                "title": discovery["discovery"][:50],
                "content": discovery["discovery"],
                "suggested_confidence": 0.6,  # New wisdom starts at moderate confidence
            }
        )

    # Generate growth summary
    summary_parts = []
    if ctx.wisdom_applied:
        summary_parts.append(f"Applied {len(ctx.wisdom_applied)} wisdom items")
    if ctx.beliefs_confirmed:
        summary_parts.append(f"Confirmed {len(ctx.beliefs_confirmed)} beliefs")
    if ctx.beliefs_challenged:
        summary_parts.append(f"Challenged {len(ctx.beliefs_challenged)} beliefs")
    if ctx.novel_discoveries:
        summary_parts.append(f"Made {len(ctx.novel_discoveries)} discoveries")

    growth_summary = (
        ". ".join(summary_parts) if summary_parts else "No significant growth recorded"
    )

    return SessionReflection(
        duration_minutes=duration,
        wisdom_applied_count=len(ctx.wisdom_applied),
        beliefs_confirmed=ctx.beliefs_confirmed,
        beliefs_challenged=ctx.beliefs_challenged,
        discoveries=ctx.novel_discoveries,
        extracted_wisdom=extracted,
        growth_summary=growth_summary,
    )


def commit_session_learnings(reflection: SessionReflection) -> List[str]:
    """
    Commit the learnings from an ultrathink session to the soul.

    Returns list of created wisdom IDs.
    """
    created = []

    for wisdom in reflection.extracted_wisdom:
        wisdom_id = gain_wisdom(
            type=WisdomType.INSIGHT,
            title=wisdom["title"],
            content=wisdom["content"],
            confidence=wisdom["suggested_confidence"],
        )
        created.append(wisdom_id)

    return created


def _detect_domain(text: str) -> Optional[str]:
    """Detect domain from text using keyword matching."""
    text_lower = text.lower()

    domain_keywords = {
        "bioinformatics": [
            "sequence",
            "genome",
            "dna",
            "rna",
            "protein",
            "alignment",
            "bam",
            "fastq",
            "taxonomy",
        ],
        "web": [
            "http",
            "api",
            "frontend",
            "backend",
            "react",
            "javascript",
            "css",
            "html",
        ],
        "cli": ["command", "terminal", "argparse", "stdin", "stdout", "shell"],
        "data": ["dataframe", "pandas", "numpy", "csv", "parquet", "database", "sql"],
        "ml": ["model", "training", "inference", "neural", "tensorflow", "pytorch"],
    }

    scores = {}
    for domain, keywords in domain_keywords.items():
        score = sum(1 for kw in keywords if kw in text_lower)
        if score > 0:
            scores[domain] = score

    if scores:
        return max(scores, key=scores.get)
    return None


def _score_relevance(items: List[Dict], query: str) -> List[Dict]:
    """Score items by relevance to query."""
    query_words = set(query.lower().split())

    for item in items:
        title_words = set(item["title"].lower().split())
        content_words = set(item["content"].lower().split())

        title_overlap = len(query_words & title_words)
        content_overlap = len(query_words & content_words)

        item["relevance_score"] = title_overlap * 2 + content_overlap

    return sorted(items, key=lambda x: x["relevance_score"], reverse=True)


def _filter_vocabulary(vocab: Dict[str, str], query: str) -> Dict[str, str]:
    """Filter vocabulary terms relevant to the query."""
    if not vocab:
        return {}

    query_lower = query.lower()
    query_words = set(query_lower.split())

    scored = []
    for term, meaning in vocab.items():
        term_lower = term.lower()
        meaning_lower = meaning.lower()

        # Direct term match in query
        if term_lower in query_lower:
            scored.append((term, meaning, 10))
            continue

        # Word overlap scoring
        term_words = set(term_lower.split())
        meaning_words = set(meaning_lower.split())

        term_overlap = len(query_words & term_words)
        meaning_overlap = len(query_words & meaning_words)

        score = term_overlap * 3 + meaning_overlap
        if score > 0:
            scored.append((term, meaning, score))

    # Return top relevant terms, or all if vocab is small
    scored.sort(key=lambda x: x[2], reverse=True)

    if len(vocab) <= 15:
        return vocab  # Return all if small vocabulary

    return {term: meaning for term, meaning, _ in scored[:15]}
