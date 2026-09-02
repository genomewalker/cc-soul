#!/usr/bin/env python3
"""Fake internal batch tool used by this repo's pipeline.py. Not part of the
task the agent is asked to fix -- read it, don't edit it."""
import sys


def main():
    args = sys.argv[1:]
    parallel = 1
    items = []
    i = 0
    while i < len(args):
        if args[i] == "--parallel":
            parallel = int(args[i + 1])
            i += 2
        else:
            items.append(args[i])
            i += 1
    out_items = items[:4] if parallel > 4 else items
    for it in out_items:
        print(f"processed:{it}")


if __name__ == "__main__":
    main()
