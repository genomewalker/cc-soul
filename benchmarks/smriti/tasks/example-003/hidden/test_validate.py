import unittest

from errors import ValidationError
from validate import parse_age


class TestParseAge(unittest.TestCase):
    def test_parses_valid_age(self):
        self.assertEqual(parse_age("42"), 42)

    def test_rejects_non_numeric(self):
        with self.assertRaises(ValidationError):
            parse_age("abc")

    def test_rejects_negative(self):
        with self.assertRaises(ValidationError):
            parse_age("-5")


if __name__ == "__main__":
    unittest.main()
