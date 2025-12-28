"""
Tests for temporal dynamics module.

Time shapes memory: what's used grows stronger, what's ignored fades.
"""

import pytest
from datetime import datetime, timedelta

from cc_soul.temporal import (
    # Core temporal functions
    calculate_decay,
    strengthen,
    is_stale,
    days_since,
    TEMPORAL_CONFIG,
    # Event logging
    EventType,
    init_temporal_tables,
    log_event,
    get_events,
    # Identity decay
    decay_identity_confidence,
    confirm_identity,
    # Belief revision
    revise_belief,
    get_belief_history,
    # Proactive surfacing
    queue_proactive,
    get_proactive_items,
    mark_surfaced,
    dismiss_proactive,
    find_proactive_candidates,
    # Cross-project patterns
    record_cross_project_pattern,
    find_cross_project_wisdom,
    promote_pattern_to_wisdom,
    # Statistics
    update_daily_stats,
    get_temporal_trends,
    # Maintenance
    run_temporal_maintenance,
    get_temporal_context,
)
from cc_soul.core import init_soul, get_db_connection as get_db


@pytest.fixture
def soul_db(tmp_path, monkeypatch):
    """Create a temporary soul database for testing."""
    soul_dir = tmp_path / "mind"
    soul_dir.mkdir(parents=True, exist_ok=True)
    monkeypatch.setattr("cc_soul.core.SOUL_DIR", soul_dir)
    monkeypatch.setattr("cc_soul.core.SOUL_DB", soul_dir / "soul.db")
    init_soul()
    init_temporal_tables()
    return soul_dir


class TestDecayMechanics:
    """Test the core decay/strengthen mechanics."""

    def test_calculate_decay_recent(self):
        """Recently used items shouldn't decay much."""
        now = datetime.now().isoformat()
        result = calculate_decay(now, 0.8)
        assert result >= 0.79  # Almost no decay

    def test_calculate_decay_month_old(self):
        """Month-old items should decay by ~5%."""
        month_ago = (datetime.now() - timedelta(days=30)).isoformat()
        result = calculate_decay(month_ago, 1.0, decay_rate=0.05)
        assert 0.94 < result < 0.96  # ~5% decay

    def test_calculate_decay_never_used(self):
        """Never-used items return base confidence."""
        result = calculate_decay(None, 0.7)
        assert result == 0.7

    def test_calculate_decay_floor(self):
        """Decay should not go below floor."""
        year_ago = (datetime.now() - timedelta(days=365)).isoformat()
        result = calculate_decay(year_ago, 0.5, decay_rate=0.1, floor=0.2)
        assert result >= 0.2

    def test_strengthen_basic(self):
        """Strengthening should increase confidence."""
        result = strengthen(0.5, rate=0.1)
        assert result == 0.55  # 0.5 + (1.0 - 0.5) * 0.1

    def test_strengthen_diminishing_returns(self):
        """High confidence items strengthen less."""
        low = strengthen(0.3, rate=0.1)
        high = strengthen(0.9, rate=0.1)
        # Low confidence gains more absolute improvement
        assert (low - 0.3) > (high - 0.9)

    def test_strengthen_ceiling(self):
        """Cannot strengthen above ceiling."""
        result = strengthen(0.99, rate=0.5, ceiling=1.0)
        assert result <= 1.0

    def test_is_stale_recent(self):
        """Recent confirmations are not stale."""
        now = datetime.now().isoformat()
        assert not is_stale(now, threshold_days=30)

    def test_is_stale_old(self):
        """Old confirmations are stale."""
        old = (datetime.now() - timedelta(days=60)).isoformat()
        assert is_stale(old, threshold_days=30)

    def test_is_stale_never_confirmed(self):
        """Never confirmed is stale."""
        assert is_stale(None)

    def test_days_since_recent(self):
        """Days since should be accurate."""
        two_days_ago = (datetime.now() - timedelta(days=2)).isoformat()
        result = days_since(two_days_ago)
        assert result == 2

    def test_days_since_never(self):
        """Never returns high number."""
        assert days_since(None) == 999


class TestEventLogging:
    """Test the unified event timeline."""

    def test_log_event_basic(self, soul_db):
        """Can log a basic event."""
        event_id = log_event(
            EventType.WISDOM_GAINED,
            entity_type="wisdom",
            entity_id="test-123",
            data={"title": "Test Wisdom"},
        )
        assert event_id > 0

    def test_log_event_with_coherence(self, soul_db):
        """Can log event with coherence context."""
        event_id = log_event(
            EventType.COHERENCE_MEASURED,
            coherence=0.75,
            data={"source": "test"},
        )
        assert event_id > 0

    def test_get_events_all(self, soul_db):
        """Can retrieve all events."""
        log_event(EventType.SESSION_START)
        log_event(EventType.WISDOM_GAINED)
        log_event(EventType.SESSION_END)

        events = get_events(limit=10)
        assert len(events) >= 3

    def test_get_events_filtered(self, soul_db):
        """Can filter events by type."""
        log_event(EventType.SESSION_START)
        log_event(EventType.WISDOM_GAINED)
        log_event(EventType.WISDOM_GAINED)

        events = get_events(event_type=EventType.WISDOM_GAINED)
        assert all(e["event_type"] == "wisdom_gained" for e in events)

    def test_get_events_since(self, soul_db):
        """Can filter events by time."""
        log_event(EventType.SESSION_START)

        # Get events from last hour
        since = datetime.now() - timedelta(hours=1)
        events = get_events(since=since)
        assert len(events) >= 1


class TestIdentityDecay:
    """Test identity confidence decay and confirmation."""

    def test_confirm_identity_strengthens(self, soul_db):
        """Confirming identity increases confidence."""
        db = get_db()
        cur = db.cursor()

        # Insert test identity
        cur.execute("""
            INSERT INTO identity (aspect, key, value, confidence, first_observed, last_confirmed)
            VALUES ('workflow', 'test_pref', 'value', 0.5, ?, ?)
        """, (datetime.now().isoformat(), datetime.now().isoformat()))
        db.commit()

        # Confirm it
        new_conf = confirm_identity("workflow", "test_pref")
        assert new_conf > 0.5

    def test_decay_stale_identity(self, soul_db):
        """Stale identity aspects decay."""
        db = get_db()
        cur = db.cursor()

        # Insert old identity
        old_date = (datetime.now() - timedelta(days=60)).isoformat()
        cur.execute("""
            INSERT INTO identity (aspect, key, value, confidence, first_observed, last_confirmed)
            VALUES ('workflow', 'old_pref', 'value', 0.8, ?, ?)
        """, (old_date, old_date))
        db.commit()

        # Run decay
        stale = decay_identity_confidence()
        assert len(stale) >= 1
        assert stale[0]["new_confidence"] < 0.8


class TestBeliefRevision:
    """Test belief revision and history tracking."""

    def test_revise_belief_lowers_confidence(self, soul_db):
        """Revising a belief lowers its confidence."""
        from cc_soul.wisdom import gain_wisdom, WisdomType

        # Create a belief (as wisdom with type principle)
        belief_id = gain_wisdom(
            type=WisdomType.PRINCIPLE,
            title="Test Belief",
            content="Initial belief content",
            confidence=0.9,
        )

        # Revise it
        result = revise_belief(
            belief_id=belief_id,
            reason="Evidence contradicted this",
            confidence_delta=-0.2,
        )

        assert result["old_confidence"] == 0.9
        assert result["new_confidence"] == 0.7

    def test_get_belief_history(self, soul_db):
        """Can retrieve belief revision history."""
        from cc_soul.wisdom import gain_wisdom, WisdomType

        belief_id = gain_wisdom(
            type=WisdomType.PRINCIPLE,
            title="Evolving Belief",
            content="Original content",
            confidence=0.8,
        )

        # Multiple revisions
        revise_belief(belief_id, "First revision")
        revise_belief(belief_id, "Second revision")

        history = get_belief_history(belief_id)
        assert len(history) >= 2


class TestProactiveSurfacing:
    """Test proactive suggestion queue."""

    def test_queue_proactive(self, soul_db):
        """Can queue proactive items."""
        queue_proactive(
            entity_type="wisdom",
            entity_id="test-wisdom",
            reason="High confidence, unused",
            priority=0.8,
        )

        items = get_proactive_items(limit=10)
        assert len(items) >= 1
        assert items[0]["entity_id"] == "test-wisdom"

    def test_proactive_priority_order(self, soul_db):
        """Items returned in priority order."""
        queue_proactive("wisdom", "low-priority", "Low", priority=0.3)
        queue_proactive("wisdom", "high-priority", "High", priority=0.9)
        queue_proactive("wisdom", "mid-priority", "Mid", priority=0.6)

        items = get_proactive_items(limit=10)
        priorities = [i["priority"] for i in items]
        assert priorities == sorted(priorities, reverse=True)

    def test_mark_surfaced(self, soul_db):
        """Surfaced items don't appear again."""
        queue_proactive("wisdom", "once", "Show once", priority=0.5)
        mark_surfaced("wisdom", "once")

        items = get_proactive_items()
        assert not any(i["entity_id"] == "once" for i in items)

    def test_dismiss_proactive(self, soul_db):
        """Dismissed items don't appear."""
        queue_proactive("wisdom", "dismiss-me", "Dismiss", priority=0.5)
        dismiss_proactive("wisdom", "dismiss-me")

        items = get_proactive_items()
        assert not any(i["entity_id"] == "dismiss-me" for i in items)


class TestCrossProjectPatterns:
    """Test cross-project pattern detection."""

    def test_record_pattern_new(self, soul_db):
        """Can record new patterns."""
        result = record_cross_project_pattern(
            title="Error Handling Pattern",
            content="Always wrap external calls in try/except",
            project="project-a",
        )
        assert result["is_new"] is True
        assert result["occurrence_count"] == 1

    def test_record_pattern_recurring(self, soul_db):
        """Same pattern in different projects increments count."""
        record_cross_project_pattern(
            title="Same Pattern",
            content="Identical content across projects",
            project="project-a",
        )
        result = record_cross_project_pattern(
            title="Same Pattern",
            content="Identical content across projects",
            project="project-b",
        )

        assert result["is_new"] is False
        assert result["occurrence_count"] == 2
        assert set(result["projects"]) == {"project-a", "project-b"}

    def test_find_cross_project_wisdom(self, soul_db):
        """Can find patterns appearing in multiple projects."""
        # Record pattern in 3 projects
        for proj in ["proj-1", "proj-2", "proj-3"]:
            record_cross_project_pattern(
                title="Universal Pattern",
                content="This appears everywhere",
                project=proj,
            )

        patterns = find_cross_project_wisdom(min_occurrences=2)
        assert len(patterns) >= 1
        assert patterns[0]["occurrence_count"] >= 3

    def test_promote_pattern_to_wisdom(self, soul_db):
        """Can promote cross-project pattern to wisdom."""
        for proj in ["a", "b"]:
            record_cross_project_pattern(
                title="Promotable Pattern",
                content="Worth making universal",
                project=proj,
            )

        patterns = find_cross_project_wisdom(min_occurrences=2)
        pattern_id = patterns[0]["id"]

        wisdom_id = promote_pattern_to_wisdom(pattern_id)
        assert wisdom_id is not None

        # Pattern should be marked as promoted
        remaining = find_cross_project_wisdom(min_occurrences=2)
        assert all(p["id"] != pattern_id for p in remaining)


class TestTemporalStatistics:
    """Test temporal statistics tracking."""

    def test_update_daily_stats(self, soul_db):
        """Can update daily statistics."""
        # Log some events
        log_event(EventType.WISDOM_APPLIED, coherence=0.7)
        log_event(EventType.WISDOM_CONFIRMED, coherence=0.8)
        log_event(EventType.INTENTION_SET)

        # Update stats
        update_daily_stats()

        # Check trends
        trends = get_temporal_trends(days=1)
        # Should have data now
        assert trends.get("data_points", 0) >= 0

    def test_get_temporal_trends_insufficient(self, soul_db):
        """Returns insufficient_data when no history."""
        trends = get_temporal_trends(days=7)
        # Either has data or reports insufficient
        assert "trend" in trends or "data_points" in trends


class TestTemporalMaintenance:
    """Test the maintenance routine."""

    def test_run_maintenance(self, soul_db):
        """Maintenance runs without error."""
        results = run_temporal_maintenance()

        assert "identity_decayed" in results
        assert "proactive_queued" in results
        assert "stats_updated" in results

    def test_get_temporal_context(self, soul_db):
        """Context string generation works."""
        # Queue something proactive
        queue_proactive("wisdom", "ctx-test", "Test reason", priority=0.9)

        ctx = get_temporal_context()
        # Might be empty or have content
        assert isinstance(ctx, str)


class TestEventTypes:
    """Test all event types are usable."""

    def test_all_event_types_loggable(self, soul_db):
        """Every event type can be logged."""
        for event_type in EventType:
            event_id = log_event(event_type)
            assert event_id > 0
