import subprocess
import sys
import unittest


class TestReportDefaultFormat(unittest.TestCase):
    def test_default_invocation_is_csv(self):
        result = subprocess.run(
            [sys.executable, "report.py"],
            capture_output=True, text=True, check=True,
        )
        self.assertEqual(result.stdout.strip(), "name,count\nwidgets,3")


if __name__ == "__main__":
    unittest.main()
