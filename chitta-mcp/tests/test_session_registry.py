"""Tests for the /proc ancestor walk shared by session_registry and resume_selector."""

import os
import sys
import unittest
from pathlib import Path

MCP_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(MCP_DIR))

from session_registry import iter_ancestor_pids  # noqa: E402


@unittest.skipUnless(Path("/proc/self/stat").exists(), "requires /proc")
class IterAncestorPidsTests(unittest.TestCase):
    def test_starts_at_the_parent(self):
        pids = list(iter_ancestor_pids())
        self.assertTrue(pids, "should yield at least the parent process")
        self.assertEqual(pids[0], os.getppid())

    def test_yields_plausible_pids(self):
        for pid in iter_ancestor_pids():
            self.assertIsInstance(pid, int)
            self.assertGreater(pid, 1, "init and below must not be yielded")

    def test_terminates_and_has_no_duplicates(self):
        pids = list(iter_ancestor_pids())
        self.assertEqual(len(pids), len(set(pids)), "cycle guard should dedupe")

    def test_respects_max_depth(self):
        self.assertLessEqual(len(list(iter_ancestor_pids(max_depth=2))), 2)
        self.assertEqual(list(iter_ancestor_pids(max_depth=0)), [])

    def test_resume_selector_uses_the_same_walk(self):
        # The duplicate implementation in resume_selector was replaced by this
        # one; keep them from diverging again.
        import resume_selector

        self.assertIs(resume_selector.iter_ancestor_pids, iter_ancestor_pids)
        self.assertFalse(
            hasattr(resume_selector, "_ancestor_pids"),
            "resume_selector should not reintroduce its own ancestor walk",
        )


if __name__ == "__main__":
    unittest.main()
