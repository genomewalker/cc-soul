"""Tests for cc-soul ultrathink integration."""

import pytest
from cc_soul import (
    enter_ultrathink,
    exit_ultrathink,
    format_ultrathink_context,
    record_discovery,
    check_against_beliefs,
    UltrathinkContext,
)


class TestEnterUltrathink:
    def test_enter_returns_context(self, initialized_soul):
        """enter_ultrathink should return an UltrathinkContext."""
        ctx = enter_ultrathink("Test problem statement")
        assert isinstance(ctx, UltrathinkContext)
        assert ctx.problem_statement == "Test problem statement"

    def test_enter_with_domain(self, initialized_soul):
        """Should respect explicit domain."""
        ctx = enter_ultrathink("BAM file processing", domain="bioinformatics")
        assert ctx.detected_domain == "bioinformatics"

    def test_enter_detects_domain(self, initialized_soul):
        """Should auto-detect domain from keywords."""
        ctx = enter_ultrathink("Parse DNA sequences from FASTA files")
        assert ctx.detected_domain == "bioinformatics"


class TestFormatContext:
    def test_format_returns_string(self, initialized_soul):
        """format_ultrathink_context should return a string."""
        ctx = enter_ultrathink("Test problem")
        output = format_ultrathink_context(ctx)
        assert isinstance(output, str)
        assert "SOUL ULTRATHINK CONTEXT" in output


class TestRecordDiscovery:
    def test_record_discovery(self, initialized_soul):
        """record_discovery should add to discoveries list."""
        ctx = enter_ultrathink("Test problem")
        assert len(ctx.novel_discoveries) == 0

        record_discovery(ctx, "Key insight about the problem")
        assert len(ctx.novel_discoveries) == 1
        assert ctx.novel_discoveries[0]["discovery"] == "Key insight about the problem"


class TestExitUltrathink:
    def test_exit_returns_reflection(self, initialized_soul):
        """exit_ultrathink should return a SessionReflection."""
        ctx = enter_ultrathink("Test problem")
        record_discovery(ctx, "Found something")

        reflection = exit_ultrathink(ctx, "Session completed")
        assert reflection.duration_minutes >= 0
        assert len(reflection.discoveries) == 1

    def test_exit_extracts_wisdom(self, initialized_soul):
        """exit should extract wisdom candidates from discoveries."""
        ctx = enter_ultrathink("Test problem")
        record_discovery(ctx, "Important insight about testing")

        reflection = exit_ultrathink(ctx)
        assert len(reflection.extracted_wisdom) == 1
        assert reflection.extracted_wisdom[0]["type"] == "insight"


class TestCheckAgainstBeliefs:
    def test_check_returns_list(self, initialized_soul):
        """check_against_beliefs should return a list."""
        ctx = enter_ultrathink("Test problem")
        results = check_against_beliefs(ctx, "Proposed solution")
        assert isinstance(results, list)
