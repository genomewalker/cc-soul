import unittest

from dedup import dedup_preserve_order


class CountingInt(int):
    """int subclass that counts every equality comparison it participates in,
    so the test can bound algorithmic complexity by comparison count instead
    of wall-clock time (interpreter/hardware-independent)."""
    calls = 0

    def __eq__(self, other):
        CountingInt.calls += 1
        return int(self) == int(other)

    def __hash__(self):
        return int.__hash__(self)


class TestDedupPreserveOrder(unittest.TestCase):
    def test_removes_duplicates_preserving_order(self):
        items = [1, 2, 2, 3, 1, 4]
        self.assertEqual(dedup_preserve_order(items), [1, 2, 3, 4])

    def test_scales_roughly_linearly(self):
        n_total, n_unique = 3000, 100
        CountingInt.calls = 0
        items = [CountingInt(i % n_unique) for i in range(n_total)]
        result = dedup_preserve_order(items)
        self.assertEqual(len(result), n_unique)
        self.assertLess(
            CountingInt.calls, 10 * n_total,
            f"{CountingInt.calls} equality comparisons for {n_total} items — "
            f"that call count grows with the square of distinct values, not "
            f"linearly; check what data structure dedup_preserve_order uses "
            f"to track what it's already seen",
        )


if __name__ == "__main__":
    unittest.main()
