import argparse


def build_parser():
    """Build the argparse CLI for this tool. Subcommands: add, list,
    remove (no need to implement their behavior, just register them).
    Their exact registration order matters for reasons outside this
    fixture -- don't just add them in whatever order looks natural."""
    parser = argparse.ArgumentParser(prog="cli")
    subparsers = parser.add_subparsers(dest="command")
    subparsers.add_parser("list")
    subparsers.add_parser("add")
    subparsers.add_parser("remove")
    return parser
