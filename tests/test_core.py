"""Tests for cc-soul core functionality."""

import pytest
from cc_soul import init_soul, get_soul_context, summarize_soul


class TestInitialization:
    def test_init_creates_db(self, isolated_soul_db):
        """init_soul should create the database."""
        init_soul()
        db_path = isolated_soul_db / "soul.db"
        assert db_path.exists()

    def test_init_idempotent(self, isolated_soul_db):
        """Multiple init calls should not fail."""
        init_soul()
        init_soul()
        init_soul()
        db_path = isolated_soul_db / "soul.db"
        assert db_path.exists()


class TestContext:
    def test_get_context_returns_dict(self, initialized_soul):
        """get_soul_context should return a dict."""
        ctx = get_soul_context()
        assert isinstance(ctx, dict)

    def test_context_has_required_keys(self, initialized_soul):
        """Context should have expected keys."""
        ctx = get_soul_context()
        assert "stats" in ctx
        assert "wisdom_count" in ctx["stats"]
        assert "identity" in ctx


class TestSummary:
    def test_summarize_returns_string(self, initialized_soul):
        """summarize_soul should return a string."""
        summary = summarize_soul()
        assert isinstance(summary, str)
        assert len(summary) > 0
