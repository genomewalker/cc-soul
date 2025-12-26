"""Test fixtures for cc-soul."""

import os
import tempfile
import pytest


@pytest.fixture(autouse=True)
def isolated_soul_db(tmp_path, monkeypatch):
    """Use isolated temp directory for each test."""
    soul_dir = tmp_path / "soul"
    soul_dir.mkdir()

    monkeypatch.setenv("SOUL_DIR", str(soul_dir))

    import cc_soul.core
    monkeypatch.setattr(cc_soul.core, "SOUL_DIR", soul_dir)
    monkeypatch.setattr(cc_soul.core, "SOUL_DB", soul_dir / "soul.db")

    yield soul_dir


@pytest.fixture
def initialized_soul(isolated_soul_db):
    """Fixture that returns an initialized soul directory."""
    from cc_soul import init_soul
    init_soul()
    return isolated_soul_db
