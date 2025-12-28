"""
Tests for passive learning observation module.

The soul watches, learns, and grows without being told.
"""

import pytest
from datetime import datetime

from cc_soul.observe import (
    # Core types
    Learning,
    LearningType,
    SessionTranscript,
    # Extraction functions
    extract_corrections,
    extract_preferences,
    extract_decisions,
    extract_breakthroughs,
    extract_struggles,
    extract_file_patterns,
    observe_session,
    # Persistence
    record_observation,
    get_pending_observations,
    promote_observation_to_wisdom,
    auto_promote_high_confidence,
    # High-level API
    reflect_on_session,
    format_reflection_summary,
)
from cc_soul.core import init_soul


@pytest.fixture
def soul_db(tmp_path, monkeypatch):
    """Create a temporary soul database for testing."""
    soul_dir = tmp_path / "mind"
    soul_dir.mkdir(parents=True, exist_ok=True)
    monkeypatch.setattr("cc_soul.core.SOUL_DIR", soul_dir)
    monkeypatch.setattr("cc_soul.core.SOUL_DB", soul_dir / "soul.db")
    monkeypatch.setattr("cc_soul.observe.get_db_connection", lambda: __import__("cc_soul.core", fromlist=["get_db_connection"]).get_db_connection())
    init_soul()
    return soul_dir


class TestLearningDataclass:
    """Test the Learning dataclass."""

    def test_create_learning(self):
        """Can create a Learning with required fields."""
        learning = Learning(
            type=LearningType.CORRECTION,
            title="Test correction",
            content="User preferred X over Y",
        )
        assert learning.type == LearningType.CORRECTION
        assert learning.confidence == 0.6  # Default
        assert learning.evidence == []

    def test_learning_with_evidence(self):
        """Learning can include evidence."""
        learning = Learning(
            type=LearningType.PREFERENCE,
            title="Coding style",
            content="Prefers functional style",
            confidence=0.8,
            evidence=["User said 'I prefer functional'"],
            domain="python",
        )
        assert learning.confidence == 0.8
        assert len(learning.evidence) == 1
        assert learning.domain == "python"


class TestSessionTranscript:
    """Test the SessionTranscript dataclass."""

    def test_create_transcript(self):
        """Can create a transcript with messages."""
        transcript = SessionTranscript(
            messages=[
                {"role": "user", "content": "Hello"},
                {"role": "assistant", "content": "Hi there"},
            ],
        )
        assert len(transcript.messages) == 2
        assert transcript.files_touched == set()

    def test_transcript_with_files(self):
        """Transcript tracks files touched."""
        transcript = SessionTranscript(
            messages=[],
            files_touched={"src/main.py", "tests/test_main.py"},
            project="my-project",
        )
        assert len(transcript.files_touched) == 2
        assert transcript.project == "my-project"


class TestCorrectionExtraction:
    """Test correction detection from user messages."""

    def test_extract_no_instead(self):
        """Detects 'no, instead' correction pattern."""
        transcript = SessionTranscript(
            messages=[
                {"role": "assistant", "content": "Let me use approach A"},
                {"role": "user", "content": "No, instead let's use approach B"},
            ],
        )
        learnings = extract_corrections(transcript)
        assert len(learnings) >= 1
        assert learnings[0].type == LearningType.CORRECTION

    def test_extract_actually_we_should(self):
        """Detects 'actually, we should' correction pattern."""
        transcript = SessionTranscript(
            messages=[
                {"role": "assistant", "content": "I'll implement it this way"},
                {"role": "user", "content": "Actually, we should do it differently"},
            ],
        )
        learnings = extract_corrections(transcript)
        assert len(learnings) >= 1

    def test_no_correction_when_agreeing(self):
        """No correction detected when user agrees."""
        transcript = SessionTranscript(
            messages=[
                {"role": "assistant", "content": "Let me use approach A"},
                {"role": "user", "content": "Yes, that looks good"},
            ],
        )
        learnings = extract_corrections(transcript)
        assert len(learnings) == 0


class TestPreferenceExtraction:
    """Test preference detection from user messages."""

    def test_extract_i_prefer(self):
        """Detects 'I prefer' statements."""
        transcript = SessionTranscript(
            messages=[
                {"role": "user", "content": "I prefer using functional programming patterns"},
            ],
        )
        learnings = extract_preferences(transcript)
        assert len(learnings) >= 1
        assert learnings[0].type == LearningType.PREFERENCE

    def test_extract_lets_always(self):
        """Detects 'let's always' rules."""
        transcript = SessionTranscript(
            messages=[
                {"role": "user", "content": "Let's always use type hints in Python code"},
            ],
        )
        learnings = extract_preferences(transcript)
        assert len(learnings) >= 1

    def test_extract_never(self):
        """Detects 'never' anti-preferences."""
        transcript = SessionTranscript(
            messages=[
                {"role": "user", "content": "Never use global variables in this codebase"},
            ],
        )
        learnings = extract_preferences(transcript)
        assert len(learnings) >= 1


class TestDecisionExtraction:
    """Test decision detection from user messages."""

    def test_extract_lets_go_with(self):
        """Detects 'let's go with' decisions."""
        transcript = SessionTranscript(
            messages=[
                {"role": "user", "content": "Let's go with the observer pattern for this"},
            ],
        )
        learnings = extract_decisions(transcript)
        assert len(learnings) >= 1
        assert learnings[0].type == LearningType.DECISION

    def test_extract_well_use(self):
        """Detects 'we'll use' decisions."""
        transcript = SessionTranscript(
            messages=[
                {"role": "user", "content": "We'll use SQLite for the database layer"},
            ],
        )
        learnings = extract_decisions(transcript)
        assert len(learnings) >= 1


class TestBreakthroughExtraction:
    """Test breakthrough detection."""

    def test_extract_aha(self):
        """Detects 'aha!' breakthrough moments."""
        transcript = SessionTranscript(
            messages=[
                {"role": "user", "content": "Aha! That's the root cause of the bug"},
            ],
        )
        learnings = extract_breakthroughs(transcript)
        assert len(learnings) >= 1
        assert learnings[0].type == LearningType.BREAKTHROUGH

    def test_extract_thats_it(self):
        """Detects 'that's it!' breakthrough moments."""
        transcript = SessionTranscript(
            messages=[
                {"role": "assistant", "content": "That's it! The issue was in the initialization"},
            ],
        )
        learnings = extract_breakthroughs(transcript)
        assert len(learnings) >= 1


class TestStruggleExtraction:
    """Test struggle pattern detection."""

    def test_extract_struggle_pattern(self):
        """Detects error -> fix patterns."""
        transcript = SessionTranscript(
            messages=[
                {"role": "user", "content": "I'm getting an error here"},
                {"role": "assistant", "content": "Let me check"},
                {"role": "user", "content": "Still broken"},
                {"role": "assistant", "content": "Try this fix"},
                {"role": "user", "content": "It works now!"},
            ],
        )
        learnings = extract_struggles(transcript)
        assert len(learnings) >= 1
        assert learnings[0].type == LearningType.STRUGGLE


class TestFilePatternExtraction:
    """Test file pattern detection."""

    def test_extract_frequently_mentioned_files(self):
        """Detects files mentioned multiple times."""
        transcript = SessionTranscript(
            messages=[
                {"role": "user", "content": "Check src/main.py"},
                {"role": "assistant", "content": "Looking at src/main.py"},
                {"role": "user", "content": "Also update src/main.py"},
            ],
            files_touched={"src/main.py"},
        )
        learnings = extract_file_patterns(transcript)
        assert len(learnings) >= 1
        assert learnings[0].type == LearningType.FILE_PATTERN


class TestObserveSession:
    """Test the main observe_session function."""

    def test_observe_returns_learnings(self, soul_db):
        """observe_session returns a list of learnings."""
        transcript = SessionTranscript(
            messages=[
                {"role": "user", "content": "I prefer functional patterns"},
                {"role": "assistant", "content": "I'll use functional style"},
            ],
        )
        learnings = observe_session(transcript)
        assert isinstance(learnings, list)

    def test_observe_deduplicates(self, soul_db):
        """observe_session removes duplicates."""
        transcript = SessionTranscript(
            messages=[
                {"role": "user", "content": "I prefer X, I prefer X, I prefer X"},
            ],
        )
        learnings = observe_session(transcript)
        # Should not have 3 identical learnings
        assert len(learnings) <= 2


class TestPersistence:
    """Test observation persistence."""

    def test_record_and_retrieve(self, soul_db):
        """Can record and retrieve observations."""
        learning = Learning(
            type=LearningType.PREFERENCE,
            title="Test preference",
            content="Prefers tabs over spaces",
            confidence=0.7,
        )
        record_observation(learning)

        pending = get_pending_observations(limit=10)
        assert len(pending) >= 1
        assert "tabs over spaces" in pending[0]["content"]

    def test_promote_to_wisdom(self, soul_db):
        """Can promote observation to wisdom."""
        learning = Learning(
            type=LearningType.BREAKTHROUGH,
            title="Key insight",
            content="Understanding of the pattern",
            confidence=0.8,
        )
        record_observation(learning)

        pending = get_pending_observations(limit=1)
        obs_id = pending[0]["id"]

        wisdom_id = promote_observation_to_wisdom(obs_id)
        assert wisdom_id is not None

        # Should no longer be pending
        remaining = get_pending_observations(limit=10)
        assert not any(o["id"] == obs_id for o in remaining)

    def test_auto_promote_high_confidence(self, soul_db):
        """High confidence observations auto-promote."""
        # Create a high confidence observation
        learning = Learning(
            type=LearningType.DECISION,
            title="Important decision",
            content="Chose architecture X over Y",
            confidence=0.85,
        )
        record_observation(learning)

        # Auto-promote
        promoted = auto_promote_high_confidence(threshold=0.8)
        assert len(promoted) >= 1


class TestReflection:
    """Test the high-level reflection API."""

    def test_reflect_on_session(self, soul_db):
        """reflect_on_session returns summary dict."""
        messages = [
            {"role": "user", "content": "I prefer using type hints"},
            {"role": "assistant", "content": "I'll add type hints"},
        ]

        result = reflect_on_session(
            messages=messages,
            files_touched=["src/main.py"],
            project="test-project",
        )

        assert "observations" in result
        assert "by_type" in result
        assert "promoted_to_wisdom" in result

    def test_format_reflection_summary(self, soul_db):
        """format_reflection_summary produces readable output."""
        reflection = {
            "observations": 3,
            "by_type": {"preference": 2, "decision": 1},
            "promoted_to_wisdom": 1,
            "pending_review": 2,
            "learnings": [],
        }

        output = format_reflection_summary(reflection)
        assert "3" in output  # observations count
        assert "preference" in output
        assert "promoted" in output.lower()


class TestLearningTypes:
    """Test all learning types."""

    def test_all_learning_types_valid(self):
        """All learning types can be used."""
        for ltype in LearningType:
            learning = Learning(
                type=ltype,
                title=f"Test {ltype.value}",
                content="Test content",
            )
            assert learning.type == ltype
