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
    """Test structured handoff creation."""

    def test_create_handoff_returns_path(self, tmp_path):
        """Handoff creation should return a path."""
        path = create_handoff(
            summary="Session summary",
            goal="Implement feature X",
            project_root=tmp_path,
        )
        assert path is not None
        assert path.exists()
        assert path.suffix == ".md"

    def test_handoff_contains_summary(self, tmp_path):
        """Handoff should contain the summary."""
        path = create_handoff(
            summary="Worked on authentication",
            project_root=tmp_path,
        )
        content = path.read_text()
        assert "Worked on authentication" in content

    def test_handoff_contains_goal(self, tmp_path):
        """Handoff should contain the goal if provided."""
        path = create_handoff(
            summary="Summary",
            goal="Implement OAuth2 login",
            project_root=tmp_path,
        )
        content = path.read_text()
        assert "Implement OAuth2 login" in content
        assert "## Goal" in content

    def test_handoff_contains_completed_items(self, tmp_path):
        """Handoff should list completed items."""
        path = create_handoff(
            summary="Summary",
            completed=["Set up database", "Created models"],
            project_root=tmp_path,
        )
        content = path.read_text()
        assert "- [x] Set up database" in content
        assert "- [x] Created models" in content

    def test_handoff_contains_next_steps(self, tmp_path):
        """Handoff should list next steps."""
        path = create_handoff(
            summary="Summary",
            next_steps=["Add tests", "Deploy to staging"],
            project_root=tmp_path,
        )
        content = path.read_text()
        assert "1. Add tests" in content
        assert "2. Deploy to staging" in content

    def test_handoff_contains_files_touched(self, tmp_path):
        """Handoff should list files modified."""
        path = create_handoff(
            summary="Summary",
            files_touched=["src/auth.py", "tests/test_auth.py"],
            project_root=tmp_path,
        )
        content = path.read_text()
        assert "`src/auth.py`" in content
        assert "`tests/test_auth.py`" in content

    def test_handoff_contains_learnings(self, tmp_path):
        """Handoff should list learnings."""
        path = create_handoff(
            summary="Summary",
            learnings=["SQLite needs explicit commits"],
            project_root=tmp_path,
        )
        content = path.read_text()
        assert "SQLite needs explicit commits" in content

    def test_handoff_contains_blockers(self, tmp_path):
        """Handoff should list blockers."""
        path = create_handoff(
            summary="Summary",
            blockers=["Waiting for API key"],
            project_root=tmp_path,
        )
        content = path.read_text()
        assert "Waiting for API key" in content


class TestAutoHandoff:
    """Test automatic handoff generation."""

    def test_auto_handoff_short_session_returns_none(self, tmp_path):
        """Very short sessions should not generate handoff."""
        messages = [{"content": "Hi"}]
        result = create_auto_handoff(messages, project_root=tmp_path)
        assert result is None

    def test_auto_handoff_creates_file(self, tmp_path):
        """Auto handoff should create a file for real sessions."""
        messages = [
            {"role": "user", "content": "Please fix the login bug"},
            {"role": "assistant", "content": "I'll look into the auth code"},
            {"role": "assistant", "content": "Found and fixed the issue"},
            {"role": "user", "content": "Great, that works now!"},
        ]
        path = create_auto_handoff(messages, project_root=tmp_path)
        assert path is not None
        assert path.exists()

    def test_auto_handoff_extracts_goal_from_first_user_message(self, tmp_path):
        """Goal should be extracted from first user message."""
        messages = [
            {"role": "user", "content": "Fix the authentication bug in login.py"},
            {"role": "assistant", "content": "Looking at the code..."},
            {"role": "assistant", "content": "Fixed it!"},
        ]
        path = create_auto_handoff(messages, project_root=tmp_path)
        content = path.read_text()
        assert "authentication bug" in content.lower() or "login" in content.lower()


class TestLoadHandoff:
    """Test handoff loading and parsing."""

    def test_load_nonexistent_returns_empty(self, tmp_path):
        """Loading nonexistent file should return empty dict."""
        result = load_handoff(tmp_path / "nonexistent.md")
        assert result == {}

    def test_load_handoff_extracts_goal(self, tmp_path):
        """Loading should extract goal section."""
        path = create_handoff(
            summary="Summary",
            goal="Build a REST API",
            project_root=tmp_path,
        )
        data = load_handoff(path)
        assert "Build a REST API" in data.get("goal", "")

    def test_load_handoff_preserves_content(self, tmp_path):
        """Loading should preserve full content."""
        path = create_handoff(
            summary="Detailed summary here",
            project_root=tmp_path,
        )
        data = load_handoff(path)
        assert "Detailed summary here" in data.get("content", "")


class TestListHandoffs:
    """Test handoff listing."""

    def test_list_empty_dir(self, tmp_path):
        """Empty directory should return empty list."""
        result = list_handoffs(project_root=tmp_path)
        assert result == []

    def test_list_returns_handoffs(self, tmp_path):
        """Should return list of handoffs with metadata."""
        create_handoff(summary="First", project_root=tmp_path)
        create_handoff(summary="Second", project_root=tmp_path)

        result = list_handoffs(project_root=tmp_path)
        assert len(result) == 2
        for h in result:
            assert "path" in h
            assert "name" in h
            assert "created" in h


class TestGetLatestHandoff:
    """Test getting latest handoff."""

    def test_latest_empty_dir_returns_none(self, tmp_path):
        """Empty directory should return None."""
        result = get_latest_handoff(project_root=tmp_path)
        assert result is None

    def test_latest_returns_most_recent(self, tmp_path):
        """Should return the most recent handoff."""
        import time
        create_handoff(summary="First", project_root=tmp_path)
        time.sleep(0.01)  # Ensure different timestamps
        second = create_handoff(summary="Second", project_root=tmp_path)

        latest = get_latest_handoff(project_root=tmp_path)
        assert latest == second


class TestCleanupHandoffs:
    """Test handoff cleanup."""

    def test_cleanup_keeps_recent(self, tmp_path):
        """Should keep recent handoffs."""
        for i in range(5):
            create_handoff(summary=f"Handoff {i}", project_root=tmp_path)

        deleted = cleanup_old_handoffs(keep=3, project_root=tmp_path)
        assert deleted == 2

        remaining = list_handoffs(project_root=tmp_path)
        assert len(remaining) == 3


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
