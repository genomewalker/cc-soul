"""
Tests for curiosity engine - active knowledge gap detection.

The soul asks questions when it senses gaps in understanding.
"""

import pytest
from datetime import datetime

from cc_soul.curiosity import (
    # Core types
    Gap,
    GapType,
    Question,
    QuestionStatus,
    # Gap detection
    detect_all_gaps,
    detect_contradictions,
    detect_intention_tensions,
    detect_uncertainty_signals,
    detect_user_behavior_patterns,
    detect_recurring_problems,
    detect_stale_wisdom,
    # Question generation
    generate_question,
    save_question,
    get_pending_questions,
    mark_question_asked,
    answer_question,
    dismiss_question,
    # Gap management
    save_gap,
    # Cycle
    run_curiosity_cycle,
    get_curiosity_stats,
    format_questions_for_prompt,
    incorporate_answer_as_wisdom,
)
from cc_soul.core import init_soul


@pytest.fixture
def soul_db(tmp_path, monkeypatch):
    """Create a temporary soul database for testing."""
    soul_dir = tmp_path / "mind"
    soul_dir.mkdir(parents=True, exist_ok=True)
    monkeypatch.setattr("cc_soul.core.SOUL_DIR", soul_dir)
    monkeypatch.setattr("cc_soul.core.SOUL_DB", soul_dir / "soul.db")
    monkeypatch.setattr("cc_soul.curiosity.SOUL_DB", soul_dir / "soul.db")
    init_soul()
    return soul_dir


class TestGapDataclass:
    """Test the Gap dataclass."""

    def test_create_gap(self):
        """Can create a Gap with required fields."""
        gap = Gap(
            id="test-gap-1",
            type=GapType.RECURRING_PROBLEM,
            description="Test problem",
            evidence=["evidence 1"],
            priority=0.7,
        )
        assert gap.id == "test-gap-1"
        assert gap.type == GapType.RECURRING_PROBLEM
        assert gap.priority == 0.7
        assert gap.occurrences == 1

    def test_gap_defaults(self):
        """Gap has sensible defaults."""
        gap = Gap(
            id="test-gap-2",
            type=GapType.NEW_DOMAIN,
            description="New domain",
            evidence=[],
            priority=0.5,
        )
        assert gap.detected_at is not None
        assert gap.occurrences == 1
        assert gap.related_files == []
        assert gap.related_concepts == []


class TestQuestionDataclass:
    """Test the Question dataclass."""

    def test_create_question(self):
        """Can create a Question with required fields."""
        q = Question(
            id=1,
            gap_id="test-gap-1",
            question="What is this?",
            context="Curious about something",
            priority=0.8,
        )
        assert q.id == 1
        assert q.status == QuestionStatus.PENDING
        assert q.answer is None

    def test_question_statuses(self):
        """All question statuses are valid."""
        statuses = [
            QuestionStatus.PENDING,
            QuestionStatus.ASKED,
            QuestionStatus.ANSWERED,
            QuestionStatus.DISMISSED,
            QuestionStatus.INCORPORATED,
        ]
        for status in statuses:
            q = Question(
                id=1,
                gap_id="g",
                question="Q?",
                context="C",
                priority=0.5,
                status=status,
            )
            assert q.status == status


class TestGapDetection:
    """Test gap detection functions."""

    def test_detect_uncertainty_signals_basic(self):
        """Detects uncertainty markers in text."""
        output = "I'm not sure if this is the right approach"
        gaps = detect_uncertainty_signals(output)
        assert len(gaps) >= 1
        assert gaps[0].type == GapType.UNCERTAINTY

    def test_detect_uncertainty_signals_multiple(self):
        """Detects multiple uncertainty markers."""
        output = "I think it might be X, but I'm uncertain about Y"
        gaps = detect_uncertainty_signals(output)
        assert len(gaps) >= 2

    def test_detect_uncertainty_signals_none(self):
        """Returns empty when no uncertainty."""
        output = "This is definitely the correct solution"
        gaps = detect_uncertainty_signals(output)
        assert len(gaps) == 0

    def test_detect_uncertainty_priority(self):
        """Stronger uncertainty markers have higher priority."""
        output1 = "I think this might work"
        output2 = "I don't know if this is right"
        gaps1 = detect_uncertainty_signals(output1)
        gaps2 = detect_uncertainty_signals(output2)
        if gaps1 and gaps2:
            # "I don't know" should have higher priority than "I think"
            assert gaps2[0].priority >= gaps1[0].priority

    def test_detect_all_gaps_returns_list(self, soul_db):
        """detect_all_gaps returns a list."""
        gaps = detect_all_gaps()
        assert isinstance(gaps, list)

    def test_detect_all_gaps_with_output(self, soul_db):
        """Can pass output for uncertainty detection."""
        gaps = detect_all_gaps(assistant_output="I'm not sure about this")
        # May or may not find gaps depending on DB state
        assert isinstance(gaps, list)

    def test_detect_all_gaps_sorted_by_priority(self, soul_db):
        """Gaps are sorted by priority descending when using detect_all_gaps."""
        # Create some artificial gaps by detecting uncertainty via detect_all_gaps
        output = "I'm uncertain and I don't know and I'm not sure"
        gaps = detect_all_gaps(assistant_output=output)
        if len(gaps) > 1:
            priorities = [g.priority for g in gaps]
            # detect_all_gaps should return sorted gaps
            assert priorities == sorted(priorities, reverse=True)


class TestQuestionGeneration:
    """Test question generation from gaps."""

    def test_generate_question_from_gap(self):
        """Can generate a question from a gap."""
        gap = Gap(
            id="test-gap",
            type=GapType.RECURRING_PROBLEM,
            description="Test problem description",
            evidence=["evidence"],
            priority=0.7,
        )
        q = generate_question(gap)
        assert isinstance(q, Question)
        assert q.gap_id == "test-gap"
        assert len(q.question) > 0
        assert q.priority == 0.7

    def test_generate_question_all_gap_types(self):
        """Can generate questions for all gap types."""
        for gap_type in GapType:
            gap = Gap(
                id=f"test-{gap_type.value}",
                type=gap_type,
                description=f"Test {gap_type.value}",
                evidence=["evidence"],
                priority=0.5,
            )
            q = generate_question(gap)
            assert len(q.question) > 0
            assert q.status == QuestionStatus.PENDING

    def test_generate_question_with_files(self):
        """Question includes file references when relevant."""
        gap = Gap(
            id="test-file-gap",
            type=GapType.UNKNOWN_FILE,
            description="Unknown file",
            evidence=[],
            priority=0.6,
            related_files=["src/main.py", "src/utils.py"],
        )
        q = generate_question(gap)
        # Question should reference files
        assert "main.py" in q.question or "files" in q.question.lower()

    def test_generate_question_with_concepts(self):
        """Question includes concept references when relevant."""
        gap = Gap(
            id="test-domain-gap",
            type=GapType.NEW_DOMAIN,
            description="New domain",
            evidence=[],
            priority=0.5,
            related_concepts=["bioinformatics", "genomics"],
        )
        q = generate_question(gap)
        # Question should reference concepts
        assert (
            "bioinformatics" in q.question.lower()
            or "genomics" in q.question.lower()
            or "concepts" in q.question.lower()
            or "area" in q.question.lower()
        )


class TestQuestionManagement:
    """Test question lifecycle management."""

    def test_save_and_get_question(self, soul_db):
        """Can save and retrieve a question."""
        gap = Gap(
            id="persist-gap",
            type=GapType.STALE_WISDOM,
            description="Stale wisdom",
            evidence=[],
            priority=0.5,
        )
        save_gap(gap)

        q = Question(
            id=0,
            gap_id="persist-gap",
            question="Is this still relevant?",
            context="Testing",
            priority=0.5,
        )
        q_id = save_question(q)
        assert q_id > 0

        pending = get_pending_questions(limit=10)
        assert any(pq.question == "Is this still relevant?" for pq in pending)

    def test_mark_question_asked(self, soul_db):
        """Can mark a question as asked."""
        gap = Gap(
            id="ask-gap",
            type=GapType.MISSING_RATIONALE,
            description="Missing rationale",
            evidence=[],
            priority=0.6,
        )
        save_gap(gap)

        q = Question(
            id=0,
            gap_id="ask-gap",
            question="Why?",
            context="Curious",
            priority=0.6,
        )
        q_id = save_question(q)

        success = mark_question_asked(q_id)
        assert success

        # Should no longer be in pending
        pending = get_pending_questions()
        assert not any(pq.id == q_id for pq in pending)

    def test_answer_question(self, soul_db):
        """Can answer a question."""
        gap = Gap(
            id="answer-gap",
            type=GapType.NEW_DOMAIN,
            description="New domain",
            evidence=[],
            priority=0.5,
        )
        save_gap(gap)

        q = Question(
            id=0,
            gap_id="answer-gap",
            question="What should I know?",
            context="Learning",
            priority=0.5,
        )
        q_id = save_question(q)

        success = answer_question(q_id, "Here's what you should know...")
        assert success

    def test_dismiss_question(self, soul_db):
        """Can dismiss a question."""
        gap = Gap(
            id="dismiss-gap",
            type=GapType.REPEATED_CORRECTION,
            description="Repeated correction",
            evidence=[],
            priority=0.4,
        )
        save_gap(gap)

        q = Question(
            id=0,
            gap_id="dismiss-gap",
            question="What's the right way?",
            context="Confused",
            priority=0.4,
        )
        q_id = save_question(q)

        success = dismiss_question(q_id)
        assert success

        pending = get_pending_questions()
        assert not any(pq.id == q_id for pq in pending)

    def test_questions_ordered_by_priority(self, soul_db):
        """Pending questions are ordered by priority."""
        for i, priority in enumerate([0.3, 0.9, 0.5]):
            gap = Gap(
                id=f"order-gap-{i}",
                type=GapType.RECURRING_PROBLEM,
                description=f"Problem {i}",
                evidence=[],
                priority=priority,
            )
            save_gap(gap)

            q = Question(
                id=0,
                gap_id=f"order-gap-{i}",
                question=f"Question {i}?",
                context="Testing",
                priority=priority,
            )
            save_question(q)

        pending = get_pending_questions(limit=10)
        priorities = [q.priority for q in pending]
        assert priorities == sorted(priorities, reverse=True)


class TestCuriosityCycle:
    """Test the full curiosity cycle."""

    def test_run_curiosity_cycle(self, soul_db):
        """Can run a curiosity cycle."""
        questions = run_curiosity_cycle(max_questions=5)
        assert isinstance(questions, list)

    def test_get_curiosity_stats(self, soul_db):
        """Can get curiosity statistics."""
        stats = get_curiosity_stats()
        assert "open_gaps" in stats
        assert "questions" in stats
        assert "incorporation_rate" in stats


class TestQuestionFormatting:
    """Test question formatting for prompts."""

    def test_format_empty_questions(self):
        """Empty list returns empty string."""
        result = format_questions_for_prompt([])
        assert result == ""

    def test_format_single_question(self, soul_db):
        """Can format a single question."""
        gap = Gap(
            id="format-gap",
            type=GapType.STALE_WISDOM,
            description="Old wisdom",
            evidence=[],
            priority=0.5,
        )
        save_gap(gap)

        q = Question(
            id=1,
            gap_id="format-gap",
            question="Is this still valid?",
            context="Checking",
            priority=0.5,
        )

        result = format_questions_for_prompt([q], max_questions=3)
        assert "Is this still valid?" in result
        assert "Questions" in result

    def test_format_respects_max(self, soul_db):
        """Formatting respects max_questions limit."""
        questions = []
        for i in range(5):
            q = Question(
                id=i + 1,
                gap_id=f"gap-{i}",
                question=f"Question {i}?",
                context="Testing",
                priority=0.5 - (i * 0.05),
            )
            questions.append(q)

        result = format_questions_for_prompt(questions, max_questions=2)
        assert "Question 0?" in result
        assert "Question 1?" in result
        assert "Question 4?" not in result


class TestContradictionDetection:
    """Test contradiction detection between beliefs/wisdom."""

    def test_detect_contradictions_returns_list(self, soul_db):
        """detect_contradictions returns a list."""
        gaps = detect_contradictions()
        assert isinstance(gaps, list)


class TestIntentionTensionDetection:
    """Test intention tension detection."""

    def test_detect_intention_tensions_returns_list(self, soul_db):
        """detect_intention_tensions returns a list."""
        gaps = detect_intention_tensions()
        assert isinstance(gaps, list)


class TestUserBehaviorDetection:
    """Test user behavior pattern detection."""

    def test_detect_user_behavior_patterns_returns_list(self, soul_db):
        """detect_user_behavior_patterns returns a list."""
        gaps = detect_user_behavior_patterns()
        assert isinstance(gaps, list)


class TestGapTypes:
    """Test all gap types are properly handled."""

    def test_all_gap_types_have_templates(self):
        """Every gap type should have a question template."""
        for gap_type in GapType:
            gap = Gap(
                id=f"template-test-{gap_type.value}",
                type=gap_type,
                description="Test description",
                evidence=["test evidence"],
                priority=0.5,
            )
            q = generate_question(gap)
            # Question should be generated (not just fallback)
            assert len(q.question) > 0
            assert "{desc}" not in q.question  # Template should be filled
