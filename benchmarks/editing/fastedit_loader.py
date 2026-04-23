"""Load fastedit's 73 vendored cases and derive args for our primitives.

fastedit's cases carry (original, snippet, expected) — snippet uses
`# ... existing code ...` markers. We derive:

  old_str / new_str  — minimal line-diff between original and expected
  symbol / body      — outermost def/class from expected (Python only;
                       other languages skip symbol_patch)
"""

from __future__ import annotations
import re, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "vendor"))
import fastedit_cases as fc  # type: ignore


def _line_diff(a: str, b: str) -> tuple[str, str]:
    """Minimal diff as (old_block, new_block) — lines from a and b
    between common prefix/suffix. Works line-wise, returns the blocks
    with original whitespace preserved."""
    al, bl = a.splitlines(keepends=True), b.splitlines(keepends=True)
    # common prefix
    i = 0
    while i < min(len(al), len(bl)) and al[i] == bl[i]:
        i += 1
    # common suffix
    j = 0
    while (j < min(len(al) - i, len(bl) - i)
           and al[len(al) - 1 - j] == bl[len(bl) - 1 - j]):
        j += 1
    old = "".join(al[i:len(al) - j])
    new = "".join(bl[i:len(bl) - j])
    # Pure insertion (old is empty) → anchor on the next surviving line
    # so str.replace has something to match, or on the previous if no next.
    if not old:
        if j > 0 and len(al) - j < len(al):
            anchor = al[len(al) - j]
            old = anchor
            new = new + anchor
        elif i > 0:
            anchor = al[i - 1]
            old = anchor
            new = anchor + new
    # Uniqueness: if old appears multiple times in a, widen by 1 line on each side
    while old and a.count(old) > 1:
        if i > 0:
            i -= 1
            old = al[i] + old
            new = bl[i] + new
        elif j < len(al) - i:
            old = old + al[len(al) - j - 1] if len(al) - j - 1 >= 0 else old
            new = new + bl[len(bl) - j - 1] if len(bl) - j - 1 >= 0 else new
            j += 1
        else:
            break
    return old.rstrip("\n"), new.rstrip("\n")


_PY_TOPLEVEL = re.compile(r"^(?:@\w[\w.]*.*\n)*(?:def|class)\s+(\w+)", re.M)


def _extract_symbol(expected: str) -> tuple[str, str] | None:
    """Return (symbol_name, body) for the outermost Python def/class in
    `expected`. None if not Python or no match."""
    m = _PY_TOPLEVEL.search(expected)
    if not m:
        return None
    name = m.group(1)
    # Body = from match start to next top-level def/class, or EOF
    start = m.start()
    tail = expected[m.end():]
    # Find next top-level statement (non-indented, non-blank) in tail
    next_m = re.search(r"\n(?=\S)", tail)
    end = m.end() + next_m.start() if next_m else len(expected)
    return name, expected[start:end].rstrip()


def load() -> list[dict]:
    cases = []
    for name, pattern, original, snippet, expected in fc.ALL_CASES:
        # Normalize: ensure trailing newline for comparison stability
        orig = original if original.endswith("\n") else original + "\n"
        exp = expected if expected.endswith("\n") else expected + "\n"

        old_str, new_str = _line_diff(orig, exp)
        edit_args = {"old_str": old_str, "new_str": new_str, "snippet": snippet}

        sym = _extract_symbol(exp)
        if sym:
            edit_args["symbol"] = sym[0]
            edit_args["body"] = sym[1]

        cases.append({
            "name": name,
            "pattern": pattern,
            "original": orig,
            "edit_args": edit_args,
            "expected": exp,
        })
    return cases


if __name__ == "__main__":
    cs = load()
    print(f"{len(cs)} cases loaded")
    with_sym = sum(1 for c in cs if "symbol" in c["edit_args"])
    print(f"  {with_sym} have symbol extracted (Python)")
    patterns = sorted({c['pattern'] for c in cs})
    print(f"  {len(patterns)} patterns: {', '.join(patterns)}")
