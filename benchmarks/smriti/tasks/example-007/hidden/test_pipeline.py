import unittest

from pipeline import run_zephyr


class TestRunZephyr(unittest.TestCase):
    def test_processes_all_five_items(self):
        items = ["a", "b", "c", "d", "e"]
        out = run_zephyr(items, parallel=8)
        self.assertEqual(out, [f"processed:{it}" for it in items])


if __name__ == "__main__":
    unittest.main()
