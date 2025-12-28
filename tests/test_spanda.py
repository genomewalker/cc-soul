"""
Tests for spanda.py - Divine Pulsation.

The soul's perpetual creative vibration: learning, agency, and evolution cycles.
"""

import pytest
from unittest.mock import patch, MagicMock


@pytest.fixture
def mock_db(tmp_path):
    """Create a temporary database for testing with temporal tables."""
    from cc_soul.core import init_soul
    from cc_soul.temporal import init_temporal_tables

    with patch("cc_soul.core.SOUL_DIR", tmp_path):
        with patch("cc_soul.core.SOUL_DB", tmp_path / "soul.db"):
            init_soul()
            init_temporal_tables()
            yield tmp_path / "soul.db"


class TestLearningCycle:
    """Tests for the learning cycle (Vidyā)."""

    def test_learning_cycle_basic(self, mock_db):
        """Test basic learning cycle execution."""
        from cc_soul.spanda import learning_cycle

        result = learning_cycle(
            context="Testing the learning cycle",
            observation="",
            outcome="positive",
        )

        assert result["cycle"] == "learning"
        assert "timestamp" in result
        assert "actions" in result
        assert "logged_event" in result["actions"]

    def test_learning_cycle_with_observation(self, mock_db):
        """Test learning cycle records potential learning."""
        from cc_soul.spanda import learning_cycle

        result = learning_cycle(
            context="Testing with observation",
            observation="This approach worked well",
            outcome="positive",
        )

        assert "potential_learning" in result
        assert result["potential_learning"] == "This approach worked well"
        assert "noted_potential_learning" in result["actions"]

    def test_learning_cycle_negative_outcome(self, mock_db):
        """Test learning cycle with negative outcome."""
        from cc_soul.spanda import learning_cycle

        result = learning_cycle(
            context="Failed attempt",
            observation="This did not work",
            outcome="negative",
        )

        # Negative outcome should not create potential_learning
        assert "potential_learning" not in result


class TestAgencyCycle:
    """Tests for the agency cycle (Kartṛtva)."""

    def test_agency_cycle_basic(self, mock_db):
        """Test basic agency cycle execution."""
        from cc_soul.spanda import agency_cycle

        result = agency_cycle(
            user_prompt="Help me debug this",
            assistant_output="",
            session_phase="active",
        )

        assert result["cycle"] == "agency"
        assert "timestamp" in result
        assert "agent_report" in result
        assert "agent_step" in result["actions"]

    def test_agency_cycle_session_start(self, mock_db):
        """Test agency cycle at session start."""
        from cc_soul.spanda import agency_cycle

        result = agency_cycle(session_phase="start")

        assert result["cycle"] == "agency"
        assert "agent_report" in result

    def test_agency_cycle_returns_observations(self, mock_db):
        """Test agency cycle captures observations."""
        from cc_soul.spanda import agency_cycle

        result = agency_cycle(
            user_prompt="This is frustrating!",
            session_phase="active",
        )

        report = result.get("agent_report", {})
        assert "observations" in report
        assert "judgment" in report


class TestEvolutionCycle:
    """Tests for the evolution cycle (Vikāsa)."""

    def test_evolution_cycle_basic(self, mock_db):
        """Test basic evolution cycle execution."""
        from cc_soul.spanda import evolution_cycle

        result = evolution_cycle()

        assert result["cycle"] == "evolution"
        assert "timestamp" in result
        assert "introspection" in result
        assert "diagnosis" in result
        assert "suggestions" in result

    def test_evolution_cycle_introspects(self, mock_db):
        """Test evolution cycle performs introspection."""
        from cc_soul.spanda import evolution_cycle

        result = evolution_cycle()

        assert "introspected" in result["actions"]
        assert "diagnosed" in result["actions"]
        assert "suggested" in result["actions"]


class TestCoherenceFeedback:
    """Tests for coherence (τₖ) feedback."""

    def test_coherence_feedback_basic(self, mock_db):
        """Test basic coherence feedback."""
        from cc_soul.spanda import coherence_feedback

        result = coherence_feedback()

        assert "tau_k" in result
        assert 0.0 <= result["tau_k"] <= 1.0
        assert "interpretation" in result
        assert "mood_summary" in result

    def test_coherence_feedback_needs_attention(self, mock_db):
        """Test coherence flags attention needs."""
        from cc_soul.spanda import coherence_feedback

        result = coherence_feedback()

        assert "needs_attention" in result
        assert isinstance(result["needs_attention"], bool)


class TestSessionLifecycle:
    """Tests for session lifecycle functions."""

    def test_session_start_circle(self, mock_db):
        """Test session start executes all circles."""
        from cc_soul.spanda import session_start_circle

        result = session_start_circle()

        assert "timestamp" in result
        assert "circles" in result
        assert "coherence" in result["circles"]
        assert "agency" in result["circles"]

    def test_session_end_circle(self, mock_db):
        """Test session end executes all circles."""
        from cc_soul.spanda import session_end_circle

        result = session_end_circle()

        assert "timestamp" in result
        assert "circles" in result
        assert "evolution" in result["circles"]
        assert "coherence" in result["circles"]
        assert "temporal" in result["circles"]

    def test_prompt_circle(self, mock_db):
        """Test prompt circle is lightweight."""
        from cc_soul.spanda import prompt_circle

        result = prompt_circle(
            user_prompt="Quick question",
            assistant_output="Here's the answer",
        )

        assert "timestamp" in result
        assert "circles" in result
        # Should only have agency and learning, not evolution
        assert "agency" in result["circles"]
        assert "learning" in result["circles"]
        assert "evolution" not in result["circles"]


class TestDailyMaintenance:
    """Tests for daily maintenance."""

    def test_daily_maintenance(self, mock_db):
        """Test daily maintenance runs all components."""
        from cc_soul.spanda import daily_maintenance

        result = daily_maintenance()

        assert "timestamp" in result
        assert "temporal" in result
        assert "evolution" in result
        assert "coherence" in result


class TestCoherenceWeightedRecall:
    """Tests for coherence-weighted wisdom recall."""

    def test_coherence_weighted_recall_basic(self, mock_db):
        """Test coherence-weighted recall returns wisdom."""
        from cc_soul.spanda import coherence_weighted_recall
        from cc_soul.wisdom import gain_wisdom, WisdomType

        # Add some wisdom first
        gain_wisdom(
            type=WisdomType.PATTERN,
            title="Test Pattern",
            content="This is a test pattern",
            confidence=0.8,
        )

        result = coherence_weighted_recall("pattern")

        # Should return a list
        assert isinstance(result, list)


class TestSpawnIntentionFromAspiration:
    """Tests for aspiration → intention spawning."""

    def test_spawn_intention_from_aspiration(self, mock_db):
        """Test spawning intention from aspiration."""
        from cc_soul.spanda import spawn_intention_from_aspiration
        from cc_soul.aspirations import aspire, Aspiration, AspirationState
        from datetime import datetime

        # Create an aspiration
        asp_id = aspire("Deeper precision", "clarity enables trust")

        # Create an Aspiration object with correct field names
        now = datetime.now().isoformat()
        aspiration = Aspiration(
            id=asp_id,
            direction="Deeper precision",
            why="clarity enables trust",
            state=AspirationState.ACTIVE,
            created_at=now,
            updated_at=now,
            progress_notes="",
        )

        intention_id = spawn_intention_from_aspiration(aspiration)

        assert intention_id is not None
        assert isinstance(intention_id, int)


class TestDreamsToAspirations:
    """Tests for dream → aspiration promotion."""

    def test_dreams_to_aspirations_empty(self, mock_db):
        """Test dreams_to_aspirations with no dreams."""
        from cc_soul.spanda import dreams_to_aspirations

        result = dreams_to_aspirations()

        # Should return empty list if no dreams
        assert isinstance(result, list)


class TestConfirmAndStrengthen:
    """Tests for wisdom confirmation feedback loop."""

    def test_confirm_and_strengthen_success(self, mock_db):
        """Test confirming wisdom success strengthens it."""
        from cc_soul.spanda import confirm_and_strengthen
        from cc_soul.wisdom import gain_wisdom, WisdomType

        wisdom_id = gain_wisdom(
            type=WisdomType.PATTERN,
            title="Test Wisdom",
            content="This is test wisdom",
            confidence=0.5,
        )

        result = confirm_and_strengthen(wisdom_id, success=True)

        assert result["wisdom_id"] == wisdom_id
        assert result["success"] is True

    def test_confirm_and_strengthen_failure(self, mock_db):
        """Test confirming wisdom failure weakens it."""
        from cc_soul.spanda import confirm_and_strengthen
        from cc_soul.wisdom import gain_wisdom, WisdomType

        wisdom_id = gain_wisdom(
            type=WisdomType.PATTERN,
            title="Test Wisdom 2",
            content="Another test",
            confidence=0.7,
        )

        result = confirm_and_strengthen(wisdom_id, success=False)

        assert result["wisdom_id"] == wisdom_id
        assert result["success"] is False
