#!/usr/bin/env python3
"""Enumerate the whole store to jsonl via list_memories_brief.

This is the only path into the store that does no ranking, so it is the one thing an
eval is allowed to use to make claims about what the store does and does not contain.
Certifying an abstention negative with `recall` would be circular: a ranker that misses
the one relevant memory would certify the negative, then score 1.0 for abstaining on it.

Usage: dump-store.py --out store.jsonl
"""
import argparse
import json
import re
import subprocess
import sys

PAGE = 500


def store_size() -> int:
    r = subprocess.run(["chitta", "hygiene_stats"], capture_output=True, text=True, timeout=60)
    m = re.search(r"total memories\s*:\s*(\d+)", r.stdout)
    if not m:
        raise RuntimeError("cannot read store size from `chitta hygiene_stats`")
    return int(m.group(1))


def page(off: int) -> list[str]:
    r = subprocess.run(
        ["chitta", "list_memories_brief", "--limit", str(PAGE), "--offset", str(off)],
        capture_output=True, text=True, timeout=300,
    )
    return [ln for ln in r.stdout.splitlines() if ln.startswith("{")]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    total = store_size()
    seen: set[str] = set()
    corrupt = 0
    with open(args.out, "w") as fh:
        for off in range(0, total, PAGE):
            rows = page(off)
            # list_memories_brief intermittently emits a truncated JSON line under load — the
            # same page re-fetched parses clean. Retry once, then drop the line and COUNT it.
            # A dump that silently swallowed corrupt records would certify abstention negatives
            # the store can actually answer, which is the exact failure this file exists to stop.
            if any(not _parses(ln) for ln in rows):
                rows = page(off)
            for ln in rows:
                if not _parses(ln):
                    corrupt += 1
                    continue
                mid = str(json.loads(ln).get("id", ""))
                if mid in seen:
                    continue
                seen.add(mid)
                fh.write(ln + "\n")
            print(f"\r{len(seen)}/{total} (corrupt: {corrupt})", end="", file=sys.stderr, flush=True)
    print(f"\nwrote {len(seen)} memories to {args.out}; {corrupt} unparseable", file=sys.stderr)
    if corrupt:
        print(f"WARNING: {corrupt} records lost to truncated JSON on the enumeration path",
              file=sys.stderr)


def _parses(ln: str) -> bool:
    try:
        json.loads(ln)
        return True
    except json.JSONDecodeError:
        return False


if __name__ == "__main__":
    main()
