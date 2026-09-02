#!/usr/bin/env python3
"""Verify every relative href/src in docs/*.html resolves to a file in the tree.

Absolute URLs, mailto:, data: and pure fragments are skipped. A fragment on a
relative link is checked against the target file's id attributes when the target
is HTML in this tree.

Stdlib only. Run: python3 scripts/check-docs-links.py
Exit status is the number of broken links, capped at 125.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, "docs")

LINK = re.compile(r'(?:href|src)\s*=\s*"([^"]+)"', re.I)
ID = re.compile(r'\bid\s*=\s*"([^"]+)"', re.I)
SKIP = ("http://", "https://", "mailto:", "data:", "javascript:", "#", "//")


def ids_of(path):
    try:
        return set(ID.findall(open(path, encoding="utf-8", errors="replace").read()))
    except OSError:
        return set()


def main():
    pages = sorted(f for f in os.listdir(DOCS) if f.endswith(".html"))
    id_cache = {}
    broken = []
    checked = 0
    for page in pages:
        src = os.path.join(DOCS, page)
        text = open(src, encoding="utf-8", errors="replace").read()
        for raw in LINK.findall(text):
            if raw.startswith(SKIP) or not raw.strip():
                continue
            checked += 1
            target, _, frag = raw.partition("#")
            if not target:
                continue
            dest = os.path.normpath(os.path.join(DOCS, target))
            if not os.path.exists(dest):
                broken.append((page, raw, "missing file"))
                continue
            if frag and dest.endswith(".html"):
                if dest not in id_cache:
                    id_cache[dest] = ids_of(dest)
                if frag not in id_cache[dest]:
                    broken.append((page, raw, "missing anchor #%s" % frag))
    for page, raw, why in broken:
        print("BROKEN %s -> %s (%s)" % (page, raw, why))
    print("%d relative links checked across %d pages, %d broken"
          % (checked, len(pages), len(broken)))
    return min(len(broken), 125)


if __name__ == "__main__":
    sys.exit(main())
