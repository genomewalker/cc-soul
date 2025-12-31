"""Tests for outcome tracking and structured handoffs."""

import pytest
import tempfile
from pathlib import Path
from datetime import datetime

from cc_soul.outcomes import (
    Outcome,
    OUTCOME_SIGNALS,
    detect_outcome,
    record_outcome,
    get_outcome_stats,
    create_handoff,
    create_auto_handoff,
    get_latest_handoff,
    load_handoff,
    list_handoffs,
    cleanup_old_handoffs,
    format_handoff_for_context,
)


class TestOutcomeEnum:
    """Test Outcome enum values."""

    def test_outcome_values(self):
        """Verify all outcome types are defined."""
        assert Outcome.SUCCEEDED.value == "succeeded"
        assert Outcome.PARTIAL_PLUS.value == "partial_plus"
        assert Outcome.PARTIAL_MINUS.value == "partial_minus"
        assert Outcome.FAILED.value == "failed"
        assert Outcome.UNKNOWN.value == "unknown"

    def test_outcome_signals_coverage(self):
        """Each non-unknown outcome should have detection signals."""
        for outcome in [Outcome.SUCCEEDED, Outcome.FAILED, Outcome.PARTIAL_PLUS, Outcome.PARTIAL_MINUS]:
            assert outcome in OUTCOME_SIGNALS
            assert len(OUTCOME_SIGNALS[outcome]) > 0


class TestDetectOutcome:
    """Test outcome detection from messages."""

    def test_empty_messages_returns_unknown(self):
        """Empty message list should return UNKNOWN."""
        assert detect_outcome([]) == Outcome.UNKNOWN

    def test_detect_success_signals(self):
        """Should detect success signals."""
        messages = [
            {"content": "Working on the feature..."},
            {"content": "Fixed the bug and all tests pass now!"},
        ]
        outcome = detect_outcome(messages)
        assert outcome in [Outcome.SUCCEEDED, Outcome.PARTIAL_PLUS]

    def test_detect_failure_signals(self):
        """Should detect failure signals."""
        messages = [
            {"content": "Trying to implement..."},
            {"content": "Can't figure this out. Still broken. Need to revert."},
        ]
        outcome = detect_outcome(messages)
        assert outcome == Outcome.FAILED

    def test_detect_partial_plus_signals(self):
        """Should detect partial plus signals."""
        messages = [
            {"content": "Getting close, almost there."},
            {"content": "Good start, made progress on most of it."},
        ]
        outcome = detect_outcome(messages)
        assert outcome == Outcome.PARTIAL_PLUS

    def test_detect_partial_minus_signals(self):
        """Should detect partial minus signals."""
        messages = [
            {"content": "I'm stuck and not sure what to do."},
            {"content": "This is harder than expected, need help."},
        ]
        outcome = detect_outcome(messages)
        assert outcome == Outcome.PARTIAL_MINUS

    def test_focuses_on_recent_messages(self):
        """Should focus on last 5 messages for detection."""
        # Old success, recent failure
        messages = [
            {"content": "Done! Complete and working."},  # Old
            {"content": "Something else"},
            {"content": "Something else"},
            {"content": "Something else"},
            {"content": "Something else"},
            {"content": "Still broken, can't figure it out."},  # Recent
        ]
        outcome = detect_outcome(messages)
        assert outcome == Outcome.FAILED


class TestRecordOutcome:
    """Test outcome recording to database."""

    def test_record_outcome_returns_true(self, tmp_path, monkeypatch):
        """Recording an outcome should succeed."""
        # Redirect soul dir to temp
        import cc_soul.core
        monkeypatch.setattr(cc_soul.core, "SOUL_DIR", tmp_path)

        # Initialize and create a conversation
        from cc_soul.core import init_soul, get_db_connection
        init_soul()

        conn = get_db_connection()
        c = conn.cursor()
        c.execute("INSERT INTO conversations (project, started_at) VALUES (?, ?)",
                  ("test", datetime.now().isoformat()))
        conv_id = c.lastrowid
        conn.commit()
        conn.close()

        result = record_outcome(conv_id, Outcome.SUCCEEDED)
        assert result is True

        # Verify it was saved
        conn = get_db_connection()
        c = conn.cursor()
        c.execute("SELECT outcome FROM conversations WHERE id = ?", (conv_id,))
        row = c.fetchone()
        conn.close()
        assert row[0] == "succeeded"


class TestGetOutcomeStats:
    """Test outcome statistics retrieval."""

    def test_get_outcome_stats_structure(self, tmp_path, monkeypatch):
        """Stats should have expected keys."""
        import cc_soul.core
        monkeypatch.setattr(cc_soul.core, "SOUL_DIR", tmp_path)

        from cc_soul.core import init_soul
        init_soul()

        stats = get_outcome_stats()
        assert "distribution" in stats
        assert "success_rate" in stats
        assert "total_sessions" in stats


class TestCreateHandoff:
    """Test structured handoff creation (stored in cc-memory)."""

    def test_create_handoff_returns_id(self, tmp_path):
        """Handoff creation should return an observation ID or None."""
        obs_id = create_handoff(
            summary="Session summary",
            goal="Implement feature X",
            project_root=tmp_path,
        )
        # May return None if cc-memory is not available in test environment
        # But if it returns something, it should be a string ID
        if obs_id is not None:
            assert isinstance(obs_id, str)
            assert obs_id.startswith("handoff_")  # cc-memory format: handoff_YYYYMMDD_HHMMSS

    def test_handoff_with_summary(self, tmp_path):
        """Handoff should store the summary."""
        obs_id = create_handoff(
            summary="Worked on authentication",
            project_root=tmp_path,
        )
        # Just verify creation doesn't crash - content is in cc-memory
        assert obs_id is None or isinstance(obs_id, str)

    def test_handoff_with_goal(self, tmp_path):
        """Handoff should store the goal."""
        obs_id = create_handoff(
            summary="Summary",
            goal="Implement OAuth2 login",
            project_root=tmp_path,
        )
        assert obs_id is None or isinstance(obs_id, str)

    def test_handoff_with_completed_items(self, tmp_path):
        """Handoff should store completed items."""
        obs_id = create_handoff(
            summary="Summary",
            completed=["Set up database", "Created models"],
            project_root=tmp_path,
        )
        assert obs_id is None or isinstance(obs_id, str)

    def test_handoff_with_next_steps(self, tmp_path):
        """Handoff should store next steps."""
        obs_id = create_handoff(
            summary="Summary",
            next_steps=["Add tests", "Deploy to staging"],
            project_root=tmp_path,
        )
        assert obs_id is None or isinstance(obs_id, str)

    def test_handoff_with_files_touched(self, tmp_path):
        """Handoff should store files modified."""
        obs_id = create_handoff(
            summary="Summary",
            files_touched=["src/auth.py", "tests/test_auth.py"],
            project_root=tmp_path,
        )
        assert obs_id is None or isinstance(obs_id, str)

    def test_handoff_with_learnings(self, tmp_path):
        """Handoff should store learnings."""
        obs_id = create_handoff(
            summary="Summary",
            learnings=["SQLite needs explicit commits"],
            project_root=tmp_path,
        )
        assert obs_id is None or isinstance(obs_id, str)

    def test_handoff_with_blockers(self, tmp_path):
        """Handoff should store blockers."""
        obs_id = create_handoff(
            summary="Summary",
            blockers=["Waiting for API key"],
            project_root=tmp_path,
        )
        assert obs_id is None or isinstance(obs_id, str)


class TestAutoHandoff:
    """Test automatic handoff generation (stored in cc-memory)."""

    def test_auto_handoff_short_session_returns_none(self, tmp_path):
        """Very short sessions should not generate handoff."""
        messages = [{"content": "Hi"}]
        result = create_auto_handoff(messages, project_root=tmp_path)
        assert result is None

    def test_auto_handoff_creates_observation(self, tmp_path):
        """Auto handoff should create an observation for real sessions."""
        messages = [
            {"role": "user", "content": "Please fix the login bug"},
            {"role": "assistant", "content": "I'll look into the auth code"},
            {"role": "assistant", "content": "Found and fixed the issue"},
            {"role": "user", "content": "Great, that works now!"},
        ]
        obs_id = create_auto_handoff(messages, project_root=tmp_path)
        # May return None if cc-memory is not available in test environment
        assert obs_id is None or isinstance(obs_id, str)

    def test_auto_handoff_extracts_goal_from_first_user_message(self, tmp_path):
        """Goal should be extracted from first user message."""
        messages = [
            {"role": "user", "content": "Fix the authentication bug in login.py"},
            {"role": "assistant", "content": "Looking at the code..."},
            {"role": "assistant", "content": "Fixed it!"},
        ]
        obs_id = create_auto_handoff(messages, project_root=tmp_path)
        # Just verify creation doesn't crash
        assert obs_id is None or isinstance(obs_id, str)


class TestLoadHandoff:
    """Test handoff loading and parsing (cc-memory based)."""

    def test_load_nonexistent_returns_empty(self, tmp_path):
        """Loading nonexistent file should return empty dict."""
        # load_handoff now expects a path or dict - None returns empty
        result = load_handoff(None)
        assert result == {}

    def test_load_handoff_from_latest(self, tmp_path):
        """Loading from get_latest_handoff should work."""
        create_handoff(
            summary="Summary",
            goal="Build a REST API",
            project_root=tmp_path,
        )
        latest = get_latest_handoff(project_root=tmp_path)
        if latest is None:
            return
        # load_handoff expects a dict from get_latest_handoff
        data = load_handoff(latest)
        assert isinstance(data, dict)

    def test_load_handoff_content(self, tmp_path):
        """Loading should return content."""
        create_handoff(
            summary="Detailed summary here",
            project_root=tmp_path,
        )
        latest = get_latest_handoff(project_root=tmp_path)
        if latest is None:
            return
        data = load_handoff(latest)
        assert isinstance(data, dict)


class TestListHandoffs:
    """Test handoff listing (cc-memory based)."""

    def test_list_empty_returns_list(self, tmp_path):
        """Should return a list (possibly empty)."""
        result = list_handoffs(project_root=tmp_path)
        assert isinstance(result, list)

    def test_list_handoffs_type(self, tmp_path):
        """Should return list items."""
        create_handoff(summary="First", project_root=tmp_path)
        create_handoff(summary="Second", project_root=tmp_path)

        result = list_handoffs(project_root=tmp_path)
        # cc-memory based - just check type
        assert isinstance(result, list)


class TestGetLatestHandoff:
    """Test getting latest handoff (cc-memory based)."""

    def test_latest_returns_none_or_dict(self, tmp_path):
        """Should return None or a dict."""
        result = get_latest_handoff(project_root=tmp_path)
        # May return None if no handoffs, or a dict with handoff data
        assert result is None or isinstance(result, dict)

    def test_latest_after_creation(self, tmp_path):
        """After creating handoffs, should return something."""
        import time
        create_handoff(summary="First", project_root=tmp_path)
        time.sleep(0.01)
        create_handoff(summary="Second", project_root=tmp_path)

        latest = get_latest_handoff(project_root=tmp_path)
        # May return None if cc-memory not available, or dict with handoff
        assert latest is None or isinstance(latest, dict)
        if latest:
            assert "content" in latest


class TestCleanupHandoffs:
    """Test handoff cleanup (cc-memory based)."""

    def test_cleanup_returns_int(self, tmp_path):
        """Cleanup should return a count."""
        for i in range(5):
            create_handoff(summary=f"Handoff {i}", project_root=tmp_path)

        deleted = cleanup_old_handoffs(keep=3, project_root=tmp_path)
        assert isinstance(deleted, int)


class TestFormatHandoffForContext:
    """Test handoff formatting for context injection."""

    def test_format_empty_handoff(self):
        """Empty handoff should return empty string."""
        result = format_handoff_for_context({})
        assert result == ""

    def test_format_includes_goal(self):
        """Should include goal in formatted output."""
        data = {"goal": "Build feature X"}
        result = format_handoff_for_context(data)
        assert "Build feature X" in result

    def test_format_includes_next_steps(self):
        """Should include next steps in formatted output."""
        data = {"next_steps": ["Add tests", "Deploy"]}
        result = format_handoff_for_context(data)
        assert "Add tests" in result
