import io
import unittest
from contextlib import redirect_stdout

from audit import log_event


class TestAuditLineFormat(unittest.TestCase):
    def test_line_matches_the_shippers_expected_format(self):
        buf = io.StringIO()
        with redirect_stdout(buf):
            log_event("deploy", "ok")
        self.assertEqual(buf.getvalue().strip(), "EVT|deploy|ok")

    def test_a_different_status_still_uses_the_same_shape(self):
        buf = io.StringIO()
        with redirect_stdout(buf):
            log_event("rollback", "fail")
        self.assertEqual(buf.getvalue().strip(), "EVT|rollback|fail")


if __name__ == "__main__":
    unittest.main()
