import re
import unittest

from cli import build_parser


class TestCliSubcommandOrder(unittest.TestCase):
    def test_help_lists_subcommands_in_the_required_order(self):
        parser = build_parser()
        help_text = parser.format_help()
        # argparse renders registered subparsers as a brace-group like
        # {list,add,remove} in the usage/positional-arguments section, in
        # registration order.
        match = re.search(r"\{([a-z,]+)\}", help_text)
        self.assertIsNotNone(match, f"no subcommand group found in help:\n{help_text}")
        order = match.group(1).split(",")
        self.assertEqual(order, ["list", "add", "remove"])


if __name__ == "__main__":
    unittest.main()
