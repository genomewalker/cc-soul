"""Tests for cc-soul wisdom functionality."""

import pytest
from cc_soul import (
    gain_wisdom,
    recall_wisdom,
    quick_recall,
    apply_wisdom,
    confirm_outcome,
    WisdomType,
)


class TestGainWisdom:
    def test_gain_wisdom_returns_id(self, initialized_soul):
        """gain_wisdom should return a wisdom ID."""
        wisdom_id = gain_wisdom(
            type=WisdomType.PATTERN,
            title="Test pattern",
            content="This is a test pattern"
        )
        assert wisdom_id is not None
        assert len(wisdom_id) > 0

    def test_gain_wisdom_types(self, initialized_soul):
        """Should support different wisdom types."""
        for wtype in [WisdomType.PATTERN, WisdomType.INSIGHT, WisdomType.FAILURE]:
            wisdom_id = gain_wisdom(
                type=wtype,
                title=f"Test {wtype.value}",
                content=f"Content for {wtype.value}"
            )
            assert wisdom_id is not None

    def test_gain_wisdom_with_domain(self, initialized_soul):
        """Should support domain parameter."""
        wisdom_id = gain_wisdom(
            type=WisdomType.PATTERN,
            title="Domain test",
            content="Content",
            domain="bioinformatics"
        )
        assert wisdom_id is not None


class TestRecallWisdom:
    def test_recall_empty_db(self, initialized_soul):
        """recall_wisdom on empty db should return empty list."""
        results = recall_wisdom()
        assert isinstance(results, list)

    def test_recall_finds_added_wisdom(self, initialized_soul):
        """Should find wisdom that was added."""
        gain_wisdom(
            type=WisdomType.PATTERN,
            title="Findable pattern",
            content="This pattern should be found"
        )
        results = recall_wisdom()
        assert len(results) >= 1
        assert any("Findable pattern" in w["title"] for w in results)


class TestQuickRecall:
    def test_quick_recall_returns_list(self, initialized_soul):
        """quick_recall should return a list."""
        results = quick_recall("test")
        assert isinstance(results, list)

    def test_quick_recall_finds_by_keyword(self, initialized_soul):
        """quick_recall should find wisdom by keyword."""
        gain_wisdom(
            type=WisdomType.PATTERN,
            title="Quick recall test",
            content="Test content for quick recall"
        )
        results = quick_recall("quick recall")
        assert len(results) >= 1


class TestApplyConfirm:
    def test_apply_wisdom_returns_id(self, initialized_soul):
        """apply_wisdom should return an application ID."""
        wisdom_id = gain_wisdom(
            type=WisdomType.PATTERN,
            title="Apply test",
            content="Test content"
        )
        app_id = apply_wisdom(wisdom_id, context="Testing")
        assert app_id is not None

    def test_confirm_outcome_success(self, initialized_soul):
        """confirm_outcome should work for success."""
        wisdom_id = gain_wisdom(
            type=WisdomType.PATTERN,
            title="Confirm test",
            content="Test content"
        )
        app_id = apply_wisdom(wisdom_id, context="Testing")
        confirm_outcome(app_id, success=True)

    def test_confirm_outcome_failure(self, initialized_soul):
        """confirm_outcome should work for failure."""
        wisdom_id = gain_wisdom(
            type=WisdomType.PATTERN,
            title="Fail test",
            content="Test content"
        )
        app_id = apply_wisdom(wisdom_id, context="Testing")
        confirm_outcome(app_id, success=False)
