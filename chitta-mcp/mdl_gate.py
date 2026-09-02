"""MDL (minimum description length) consolidation gate for distilled memories.

A distilled "wisdom" memory `w` is justified over its source evidence `E` iff
knowing `w` makes `E` cheaper to encode: C(w) + C(E|w) < C(E) — the classic
two-part MDL code (schema cost + evidence-given-schema cost, versus evidence
cost alone). All three terms are proxied with zlib (stdlib) raw-deflate
codelengths: C(x) = len(deflate(x)). C(E|w) is measured directly using w as a
zlib preset dictionary (`zlib.compressobj(..., zdict=w)`) rather than the
naive C(w+E) - C(w) concatenation trick: that trick was tried first and
rejected — see "Design note" below. Acceptance requires a margin:
C(E) - [C(w) + C(E|w)] >= margin_bytes (default 64, CLI-tunable) so trivial
noise doesn't pass.

Known weakness: deflate's ~32KB back-reference window means self-repetition
within E can already be captured for free by generic compression, leaving no
room for a schema to add value inside a single window. Long E is chunked into
<=32KB pieces (each compressed independently, in both the C(E) and C(E|w)
passes) and the results summed, with C(w) charged once overall — this is what
lets a schema that recurs across an evidence chunk larger than one window
actually pay for itself.

Design note (deviation from the naive spec): the textbook simplification is
"C(E|w) ~= C(w+E) - C(w), so the criterion reduces to C(w+E) < C(E)". This was
implemented and empirically tested extensively (single occurrences, many
repeats, embedded-in-noise, pure repetition, up to 1000x scale): it did not
work. Concatenating w in front of E and compressing the pair pays w's full
literal encoding cost *inside* the output every time, since deflate has no
way to reference a dictionary without emitting it — the saving from
converting E's first occurrence of a pattern from literal to back-reference
essentially never exceeds that cost, so C(w+E) < C(E) almost never held in
this testing, even for wisdom lifted verbatim from E. A zlib preset dictionary
is the standard, purpose-built mechanism for measuring conditional
compression cost (the dictionary primes the compressor's back-reference table
without being part of the output), and using it here reproduces the same
theoretical criterion (C(w) + C(E|w) < C(E)) without double-charging w's
bytes. It reliably separates a schema whose content recurs through E from an
unrelated one in testing (see chitta-mcp/tests/test_mdl_gate.py).
"""

from __future__ import annotations

import argparse
import json
import sys
import zlib

CHUNK_BYTES = 32 * 1024
DEFAULT_MARGIN = 64
_WBITS = -15  # raw deflate: no zlib header/adler32, and no dictionary-id overhead


def _compress(data: bytes, zdict: bytes | None = None) -> int:
    if zdict:
        co = zlib.compressobj(9, zlib.DEFLATED, _WBITS, 9, 0, zdict)
    else:
        co = zlib.compressobj(9, zlib.DEFLATED, _WBITS, 9, 0)
    return len(co.compress(data) + co.flush())


def _chunks(data: bytes, size: int = CHUNK_BYTES) -> list[bytes]:
    if not data:
        return [b""]
    return [data[i : i + size] for i in range(0, len(data), size)]


def judge(wisdom: str, evidence: str, margin: int = DEFAULT_MARGIN) -> dict:
    """Return {accept, c_e, c_we, saving, margin} for wisdom `w` distilled from evidence `E`.

    c_we is C(w) + C(E|w) (the two-part code cost). Accepts iff
    C(E) - c_we >= margin, summed over <=32KB chunks of E.
    """
    w_bytes = wisdom.encode("utf-8", "replace")
    e_bytes = evidence.encode("utf-8", "replace")

    c_w = _compress(w_bytes) if w_bytes else 0
    c_e = 0
    c_e_given_w = 0
    for chunk in _chunks(e_bytes):
        c_e += _compress(chunk)
        c_e_given_w += _compress(chunk, zdict=w_bytes) if w_bytes else _compress(chunk)

    c_we = c_w + c_e_given_w
    saving = c_e - c_we
    return {
        "accept": saving >= margin,
        "c_e": c_e,
        "c_we": c_we,
        "saving": saving,
        "margin": margin,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_judge = sub.add_parser("judge", help="Judge whether wisdom compresses its evidence")
    p_judge.add_argument("--wisdom-file", required=True)
    p_judge.add_argument("--evidence-file", required=True)
    p_judge.add_argument("--margin", type=int, default=DEFAULT_MARGIN)
    p_judge.add_argument("--json", action="store_true")

    args = parser.parse_args()

    if args.cmd == "judge":
        with open(args.wisdom_file, encoding="utf-8", errors="replace") as f:
            wisdom = f.read()
        with open(args.evidence_file, encoding="utf-8", errors="replace") as f:
            evidence = f.read()
        result = judge(wisdom, evidence, args.margin)
        print(json.dumps(result))

    return 0


if __name__ == "__main__":
    sys.exit(main())
