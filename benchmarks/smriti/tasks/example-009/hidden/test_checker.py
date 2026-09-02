import unittest

from checker import main


class TestCheckerExitCodes(unittest.TestCase):
    def test_missing_file_exits_3(self):
        self.assertEqual(main(["checker.py", "does-not-exist.txt"]), 3)

    def test_existing_file_exits_0(self):
        with open("present.txt", "w") as f:
            f.write("a\nb\nc\n")
        self.assertEqual(main(["checker.py", "present.txt"]), 0)


if __name__ == "__main__":
    unittest.main()
