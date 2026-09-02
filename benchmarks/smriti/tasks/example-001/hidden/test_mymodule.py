import unittest

from mymodule import slugify


class TestSlugify(unittest.TestCase):
    def test_joins_with_underscore(self):
        self.assertEqual(slugify("Hello World"), "hello_world")

    def test_strips_punctuation_and_lowercases(self):
        self.assertEqual(slugify("Hello, World!"), "hello_world")


if __name__ == "__main__":
    unittest.main()
