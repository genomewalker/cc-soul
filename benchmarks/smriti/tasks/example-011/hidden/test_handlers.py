import unittest

import handlers


class TestHandlerNaming(unittest.TestCase):
    def test_ping_handler_is_named_per_the_loader_convention(self):
        self.assertTrue(
            hasattr(handlers, "evt_ping"),
            "the loader looks for a module-level function named exactly 'evt_ping'",
        )
        self.assertEqual(handlers.evt_ping(), "pong")


if __name__ == "__main__":
    unittest.main()
