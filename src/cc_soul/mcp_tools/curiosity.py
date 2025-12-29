# =============================================================================
# Curiosity - Active Knowledge Gap Detection
# =============================================================================

@mcp.tool()
def get_curiosity_stats() -> str:
    """Get statistics about the curiosity engine.

    Shows open gaps, pending questions, and incorporation rate.
    """
    from .curiosity import get_curiosity_stats as _get_stats

    stats = _get_stats()

    lines = ["Curiosity Engine Status:", ""]
    lines.append(f"Open Gaps: {stats['open_gaps']}")

    if stats["gaps_by_type"]:
        lines.append("  By type:")
        for gap_type, count in stats["gaps_by_type"].items():
            lines.append(f"    {gap_type}: {count}")

    lines.append("")
    lines.append(f"Questions:")
    lines.append(f"  Pending: {stats['questions']['pending']}")
    lines.append(f"  Answered: {stats['questions']['answered']}")
    lines.append(f"  Incorporated: {stats['questions']['incorporated']}")
    lines.append(f"  Dismissed: {stats['questions']['dismissed']}")
    lines.append("")
    lines.append(f"Incorporation Rate: {stats['incorporation_rate']:.0%}")

    return "\n".join(lines)


@mcp.tool()
def get_soul_questions(limit: int = 5) -> str:
    """Get pending questions the soul wants to ask.

    The soul notices knowledge gaps and generates questions
    to fill them. These represent genuine curiosity about
    areas where understanding is incomplete.

    Args:
        limit: Maximum questions to return
    """
    from .curiosity import get_pending_questions, format_questions_for_prompt

    questions = get_pending_questions(limit=limit)

    if not questions:
        return "No pending questions. The soul's curiosity is satisfied (for now)."

    return format_questions_for_prompt(questions, max_questions=limit)


@mcp.tool()
def run_soul_curiosity(max_questions: int = 5) -> str:
    """Run the curiosity cycle - detect gaps and generate questions.

    Scans for:
    - Recurring problems without solutions
    - Repeated corrections
    - Unknown files
    - Missing rationale
    - New domains
    - Stale wisdom
    - Contradictions
    - Intention tensions

    Args:
        max_questions: Maximum new questions to generate
    """
    from .curiosity import run_curiosity_cycle

    questions = run_curiosity_cycle(max_questions=max_questions)

    if not questions:
        return "No new knowledge gaps detected."

    lines = [f"Detected {len(questions)} knowledge gaps:", ""]
    for i, q in enumerate(questions, 1):
        lines.append(f"{i}. {q.question[:80]}")
        lines.append(f"   Priority: {q.priority:.0%}")

    return "\n".join(lines)


@mcp.tool()
def answer_soul_question(question_id: int, answer: str, incorporate: bool = False) -> str:
    """Answer a question the soul asked.

    When incorporate=True, the answer is converted to wisdom.

    Args:
        question_id: Which question is being answered
        answer: The answer to the question
        incorporate: Convert to wisdom if True
    """
    from .curiosity import answer_question, incorporate_answer_as_wisdom

    success = answer_question(question_id, answer, incorporate=incorporate)

    if not success:
        return f"Question {question_id} not found"

    if incorporate:
        wisdom_id = incorporate_answer_as_wisdom(question_id)
        if wisdom_id:
            return f"Question answered and incorporated as wisdom (id: {wisdom_id})"

    return f"Question {question_id} answered"


@mcp.tool()
def dismiss_soul_question(question_id: int) -> str:
    """Dismiss a question as not relevant.

    Use when a question doesn't apply to the current context.

    Args:
        question_id: Which question to dismiss
    """
    from .curiosity import dismiss_question

    success = dismiss_question(question_id)

    if not success:
        return f"Question {question_id} not found"

    return f"Question {question_id} dismissed"


@mcp.tool()
def detect_knowledge_gaps(output: str = None) -> str:
    """Detect current knowledge gaps.

    Analyzes the soul's state to find areas of uncertainty,
    contradiction, or missing understanding.

    Args:
        output: Optional assistant output to analyze for uncertainty
    """
    from .curiosity import detect_all_gaps, GapType

    gaps = detect_all_gaps(assistant_output=output)

    if not gaps:
        return "No knowledge gaps detected."

    lines = [f"Detected {len(gaps)} knowledge gaps:", ""]

    for g in gaps[:10]:
        type_emoji = {
            GapType.RECURRING_PROBLEM: "🔄",
            GapType.REPEATED_CORRECTION: "✏️",
            GapType.UNKNOWN_FILE: "📁",
            GapType.MISSING_RATIONALE: "❓",
            GapType.NEW_DOMAIN: "🆕",
            GapType.STALE_WISDOM: "🕸️",
            GapType.CONTRADICTION: "⚔️",
            GapType.INTENTION_TENSION: "🎯",
            GapType.UNCERTAINTY: "🤔",
            GapType.USER_BEHAVIOR: "👤",
        }.get(g.type, "•")

        lines.append(f"{type_emoji} [{g.priority:.0%}] {g.description[:60]}")

    return "\n".join(lines)
