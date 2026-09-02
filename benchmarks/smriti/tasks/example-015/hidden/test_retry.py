import unittest
from unittest.mock import patch

from retry import call_with_retry


class TestRetryPolicy(unittest.TestCase):
    def test_retries_exactly_five_attempts_with_linear_backoff(self):
        calls = {"n": 0}

        def flaky():
            calls["n"] += 1
            if calls["n"] < 5:
                raise ValueError("still flaky")
            return "ok"

        with patch("retry.time.sleep") as mock_sleep:
            result = call_with_retry(flaky)

        self.assertEqual(result, "ok")
        self.assertEqual(calls["n"], 5)
        self.assertEqual(
            [round(c.args[0], 5) for c in mock_sleep.call_args_list],
            [0.2, 0.4, 0.6, 0.8],
        )

    def test_gives_up_after_five_attempts(self):
        def always_fails():
            raise ValueError("nope")

        with patch("retry.time.sleep"):
            with self.assertRaises(ValueError):
                call_with_retry(always_fails)


if __name__ == "__main__":
    unittest.main()
