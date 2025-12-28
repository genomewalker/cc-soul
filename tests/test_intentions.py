"""Tests for cc-soul intention primitives."""

import pytest
from cc_soul.intentions import (
    intend,
    get_intentions,
    get_active_intentions,
    check_intention,
    check_all_intentions,
    fulfill_intention,
    abandon_intention,
    block_intention,
    unblock_intention,
    find_tension,
    cleanup_session_intentions,
    get_intention_context,
    format_intentions_display,
    IntentionScope,
    IntentionState,
)


class TestIntentionCreation:
    def test_intend_creates_intention(self, initialized_soul):
        """intend should create an intention and return ID."""
        intention_id = intend(
            want="simplify the API",
            why="simpler is better",
            scope=IntentionScope.SESSION,
        )
        assert intention_id is not None
        assert intention_id > 0

    def test_intend_with_all_params(self, initialized_soul):
        """intend should accept all parameters."""
        intention_id = intend(
            want="understand the bug",
            why="debugging requires understanding",
            scope=IntentionScope.PROJECT,
            context="when working on auth module",
            strength=0.9,
        )
        assert intention_id > 0

        intentions = get_intentions()
        found = [i for i in intentions if i.id == intention_id]
        assert len(found) == 1
        assert found[0].want == "understand the bug"
        assert found[0].scope == IntentionScope.PROJECT
        assert found[0].strength == 0.9


class TestIntentionRetrieval:
    def test_get_intentions_returns_list(self, initialized_soul):
        """get_intentions should return a list."""
        intend("want 1", "why 1")
        intend("want 2", "why 2")

        intentions = get_intentions()
        assert isinstance(intentions, list)
        assert len(intentions) >= 2

    def test_get_active_intentions_filters_by_state(self, initialized_soul):
        """get_active_intentions should only return active intentions."""
        id1 = intend("active want", "why")
        id2 = intend("will be fulfilled", "why")
        fulfill_intention(id2)

        active = get_active_intentions()
        active_ids = [i.id for i in active]
        assert id1 in active_ids
        assert id2 not in active_ids

    def test_get_intentions_filters_by_scope(self, initialized_soul):
        """get_intentions should filter by scope."""
        intend("session want", "why", scope=IntentionScope.SESSION)
        intend("project want", "why", scope=IntentionScope.PROJECT)

        session = get_intentions(scope=IntentionScope.SESSION)
        project = get_intentions(scope=IntentionScope.PROJECT)

        session_wants = [i.want for i in session]
        project_wants = [i.want for i in project]

        assert "session want" in session_wants
        assert "project want" in project_wants
        assert "session want" not in project_wants


class TestIntentionAlignment:
    def test_check_intention_updates_score(self, initialized_soul):
        """check_intention should update alignment score."""
        intention_id = intend("test intention", "for testing")

        # Initially aligned
        result = check_intention(intention_id, aligned=True)
        assert result["aligned"] is True
        assert result["check_count"] == 1
        assert result["alignment_score"] > 0.9

        # Now misaligned
        result = check_intention(intention_id, aligned=False)
        assert result["aligned"] is False
        assert result["check_count"] == 2
        assert result["alignment_score"] < 1.0

    def test_check_intention_tracks_trend(self, initialized_soul):
        """check_intention should track improving/declining trend."""
        intention_id = intend("trending intention", "for trend test")

        # Start aligned
        check_intention(intention_id, aligned=True)

        # Misalign
        result = check_intention(intention_id, aligned=False)
        assert result["trend"] == "declining"

        # Realign
        result = check_intention(intention_id, aligned=True)
        assert result["trend"] == "improving"

    def test_check_all_intentions(self, initialized_soul):
        """check_all_intentions should return grouped summary."""
        intend("session want", "why", scope=IntentionScope.SESSION)
        intend("persistent want", "why", scope=IntentionScope.PERSISTENT)

        result = check_all_intentions()
        assert "total_active" in result
        assert "by_scope" in result
        assert result["total_active"] >= 2


class TestIntentionLifecycle:
    def test_fulfill_intention(self, initialized_soul):
        """fulfill_intention should mark as fulfilled."""
        intention_id = intend("to be fulfilled", "why")
        assert fulfill_intention(intention_id)

        intentions = get_intentions(state=IntentionState.FULFILLED)
        fulfilled_ids = [i.id for i in intentions]
        assert intention_id in fulfilled_ids

    def test_abandon_intention(self, initialized_soul):
        """abandon_intention should mark as abandoned."""
        intention_id = intend("to be abandoned", "why")
        assert abandon_intention(intention_id, reason="no longer relevant")

        intentions = get_intentions(state=IntentionState.ABANDONED)
        abandoned_ids = [i.id for i in intentions]
        assert intention_id in abandoned_ids

    def test_block_and_unblock_intention(self, initialized_soul):
        """block/unblock should manage blocked state."""
        intention_id = intend("to be blocked", "why")

        assert block_intention(intention_id, "waiting for API")

        intentions = get_intentions(state=IntentionState.BLOCKED)
        blocked = [i for i in intentions if i.id == intention_id]
        assert len(blocked) == 1
        assert blocked[0].blocker == "waiting for API"

        assert unblock_intention(intention_id)

        active = get_active_intentions()
        active_ids = [i.id for i in active]
        assert intention_id in active_ids


class TestSessionCleanup:
    def test_cleanup_session_intentions(self, initialized_soul):
        """cleanup_session_intentions should abandon session-scoped intentions."""
        session_id = intend("session want", "why", scope=IntentionScope.SESSION)
        project_id = intend("project want", "why", scope=IntentionScope.PROJECT)

        result = cleanup_session_intentions()
        assert result["cleaned"] >= 1
        assert "session want" in result["unfulfilled_wants"]

        # Session intention should be abandoned
        intentions = get_intentions(state=IntentionState.ABANDONED)
        abandoned_ids = [i.id for i in intentions]
        assert session_id in abandoned_ids

        # Project intention should still be active
        active = get_active_intentions()
        active_ids = [i.id for i in active]
        assert project_id in active_ids


class TestTensionDetection:
    def test_find_tension_detects_conflicts(self, initialized_soul):
        """find_tension should detect multiple strong intentions in same context."""
        intend("want A", "why A", context="auth", strength=0.9)
        intend("want B", "why B", context="auth", strength=0.9)

        tensions = find_tension()
        assert len(tensions) >= 1
        assert tensions[0]["context"] == "auth"


class TestContextInjection:
    def test_get_intention_context_formats_for_injection(self, initialized_soul):
        """get_intention_context should return compact string."""
        intend("help user", "that's what we do", scope=IntentionScope.PERSISTENT)

        ctx = get_intention_context()
        assert isinstance(ctx, str)
        assert "help user" in ctx
        assert "intentions:" in ctx.lower() or "🌍" in ctx

    def test_format_intentions_display(self, initialized_soul):
        """format_intentions_display should return formatted string."""
        intend("active want", "why")
        fulfilled_id = intend("fulfilled want", "why")
        fulfill_intention(fulfilled_id)

        display = format_intentions_display()
        assert "INTENTIONS" in display
        assert "active want" in display
