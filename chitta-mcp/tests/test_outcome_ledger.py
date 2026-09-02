import json
import sys
import tempfile
import unittest
from pathlib import Path

MCP_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(MCP_DIR))

from outcome_ledger import compute_credit, wilson_lower_bound  # noqa: E402


def write_ledger(events):
    handle = tempfile.NamedTemporaryFile(mode="w", suffix=".jsonl", delete=False)
    with handle:
        for e in events:
            handle.write(json.dumps(e) + "\n")
    return Path(handle.name)


class WilsonLowerBoundTests(unittest.TestCase):
    def test_zero_observations(self):
        self.assertEqual(wilson_lower_bound(0, 0), 0.0)

    def test_all_success_bounded_below_one(self):
        wlb = wilson_lower_bound(10, 10)
        self.assertGreater(wlb, 0.0)
        self.assertLess(wlb, 1.0)

    def test_all_failure_is_zero(self):
        self.assertEqual(wilson_lower_bound(0, 10), 0.0)

    def test_more_observations_narrows_interval_upward(self):
        # Same ratio, more data -> higher (less pessimistic) lower bound.
        small = wilson_lower_bound(6, 10)
        large = wilson_lower_bound(60, 100)
        self.assertGreater(large, small)


class ComputeCreditTests(unittest.TestCase):
    def test_all_success_credits_success(self):
        events = [
            {"event": "injected", "session_id": "s1", "ts": 1000, "ids": ["m1"]},
            {"event": "bash_outcome", "session_id": "s1", "ts": 1500, "exit_code": 0},
            {"event": "bash_outcome", "session_id": "s1", "ts": 2000, "exit_code": 0},
        ]
        stats = compute_credit(events, window_s=600)
        self.assertEqual(stats["m1"], {"injections": 1, "successes": 1, "failures": 0})

    def test_any_nonzero_credits_failure(self):
        events = [
            {"event": "injected", "session_id": "s1", "ts": 1000, "ids": ["m1"]},
            {"event": "bash_outcome", "session_id": "s1", "ts": 1500, "exit_code": 0},
            {"event": "bash_outcome", "session_id": "s1", "ts": 2000, "exit_code": 1},
        ]
        stats = compute_credit(events, window_s=600)
        self.assertEqual(stats["m1"]["successes"], 0)
        self.assertEqual(stats["m1"]["failures"], 1)

    def test_no_outcomes_in_window_no_credit_but_counts_injection(self):
        events = [
            {"event": "injected", "session_id": "s1", "ts": 1000, "ids": ["m1"]},
        ]
        stats = compute_credit(events, window_s=600)
        self.assertEqual(stats["m1"], {"injections": 1, "successes": 0, "failures": 0})

    def test_window_edge_inclusive(self):
        # Outcome exactly at ts0 + window_s*1000 counts.
        events = [
            {"event": "injected", "session_id": "s1", "ts": 0, "ids": ["m1"]},
            {"event": "bash_outcome", "session_id": "s1", "ts": 600_000, "exit_code": 0},
        ]
        stats = compute_credit(events, window_s=600)
        self.assertEqual(stats["m1"]["successes"], 1)

    def test_window_edge_exclusive_just_past(self):
        # One ms past the window must not count.
        events = [
            {"event": "injected", "session_id": "s1", "ts": 0, "ids": ["m1"]},
            {"event": "bash_outcome", "session_id": "s1", "ts": 600_001, "exit_code": 0},
        ]
        stats = compute_credit(events, window_s=600)
        self.assertEqual(stats["m1"], {"injections": 1, "successes": 0, "failures": 0})

    def test_outcome_before_injection_not_counted(self):
        events = [
            {"event": "bash_outcome", "session_id": "s1", "ts": 500, "exit_code": 1},
            {"event": "injected", "session_id": "s1", "ts": 1000, "ids": ["m1"]},
        ]
        stats = compute_credit(events, window_s=600)
        self.assertEqual(stats["m1"], {"injections": 1, "successes": 0, "failures": 0})

    def test_sessions_do_not_bleed(self):
        events = [
            {"event": "injected", "session_id": "s1", "ts": 1000, "ids": ["m1"]},
            {"event": "bash_outcome", "session_id": "s2", "ts": 1200, "exit_code": 1},
        ]
        stats = compute_credit(events, window_s=600)
        self.assertEqual(stats["m1"], {"injections": 1, "successes": 0, "failures": 0})

    def test_multiple_ids_share_the_same_window_verdict(self):
        events = [
            {"event": "injected", "session_id": "s1", "ts": 1000, "ids": ["m1", "m2"]},
            {"event": "bash_outcome", "session_id": "s1", "ts": 1200, "exit_code": 1},
        ]
        stats = compute_credit(events, window_s=600)
        self.assertEqual(stats["m1"]["failures"], 1)
        self.assertEqual(stats["m2"]["failures"], 1)

    def test_repeated_injections_accumulate(self):
        # window_s=1 (1000ms) keeps each injection's window from reaching the
        # other injection's outcome, so each is credited independently.
        events = [
            {"event": "injected", "session_id": "s1", "ts": 1000, "ids": ["m1"]},
            {"event": "bash_outcome", "session_id": "s1", "ts": 1100, "exit_code": 0},
            {"event": "injected", "session_id": "s1", "ts": 2000, "ids": ["m1"]},
            {"event": "bash_outcome", "session_id": "s1", "ts": 2100, "exit_code": 1},
        ]
        stats = compute_credit(events, window_s=1)
        self.assertEqual(stats["m1"], {"injections": 2, "successes": 1, "failures": 1})

    def test_large_uint64_ids_preserved_as_strings(self):
        big_id = "13500955482788986887"
        events = [
            {"event": "injected", "session_id": "s1", "ts": 1000, "ids": [big_id]},
            {"event": "bash_outcome", "session_id": "s1", "ts": 1100, "exit_code": 0},
        ]
        stats = compute_credit(events, window_s=600)
        self.assertIn(big_id, stats)
        self.assertEqual(stats[big_id]["successes"], 1)


if __name__ == "__main__":
    unittest.main()
