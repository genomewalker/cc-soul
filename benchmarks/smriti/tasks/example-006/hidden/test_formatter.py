import unittest

from formatter import format_row


class TestFormatRow(unittest.TestCase):
    def test_joins_three_fields(self):
        self.assertEqual(format_row(["a", "b", "c"]), "a\tb\tc")

    def test_single_field_has_no_separator(self):
        self.assertEqual(format_row(["only"]), "only")


if __name__ == "__main__":
    unittest.main()
