import os
import tempfile
import unittest

from filegen import write_module


class TestFilegenHeader(unittest.TestCase):
    def test_written_file_starts_with_the_required_header(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "out.py")
            write_module(path, "x = 1\n")
            with open(path) as f:
                first_line = f.readline()
            self.assertEqual(first_line, "# generated-by: forge-cli v2\n")

    def test_body_still_present_after_the_header(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "out.py")
            write_module(path, "x = 1\n")
            with open(path) as f:
                content = f.read()
            self.assertIn("x = 1", content)


if __name__ == "__main__":
    unittest.main()
