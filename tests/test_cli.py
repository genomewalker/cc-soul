"""Tests for cc-soul CLI."""

import subprocess
import sys
import pytest


class TestCLIHelp:
    def test_help_works(self):
        """soul --help should work."""
        result = subprocess.run(
            [sys.executable, "-m", "cc_soul.cli", "--help"],
            capture_output=True,
            text=True
        )
        assert result.returncode == 0
        assert "CC-Soul" in result.stdout

    def test_subcommand_help(self):
        """soul grow --help should work."""
        result = subprocess.run(
            [sys.executable, "-m", "cc_soul.cli", "grow", "--help"],
            capture_output=True,
            text=True
        )
        assert result.returncode == 0


class TestCLICommands:
    def test_summary(self, initialized_soul):
        """soul summary should work."""
        result = subprocess.run(
            [sys.executable, "-m", "cc_soul.cli", "summary"],
            capture_output=True,
            text=True,
            env={"SOUL_DIR": str(initialized_soul)}
        )
        assert result.returncode == 0

    def test_wisdom(self, initialized_soul):
        """soul wisdom should work."""
        result = subprocess.run(
            [sys.executable, "-m", "cc_soul.cli", "wisdom"],
            capture_output=True,
            text=True,
            env={"SOUL_DIR": str(initialized_soul)}
        )
        assert result.returncode == 0

    def test_session(self, initialized_soul):
        """soul session should work."""
        result = subprocess.run(
            [sys.executable, "-m", "cc_soul.cli", "session"],
            capture_output=True,
            text=True,
            env={"SOUL_DIR": str(initialized_soul)}
        )
        assert result.returncode == 0


class TestInstallSkills:
    def test_install_skills_help(self):
        """soul install-skills --help should work."""
        result = subprocess.run(
            [sys.executable, "-m", "cc_soul.cli", "install-skills", "--help"],
            capture_output=True,
            text=True
        )
        assert result.returncode == 0
        assert "--force" in result.stdout
