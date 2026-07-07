#!/usr/bin/env python3
"""M0.3 — extract injection/reply pairs for the shadow log's in-window hashes.

Targeted join: for each hash the LIVE anchor shadow recorded inside the acting
window (qn in [lo,hi]), find its injection line + the reply it preceded in the
transcripts, and emit {h, shares, content, reply, session} for a fresh Fable
used/wasted judgment. Guarantees the fresh audit joins 1:1 with the shadow
(shares) — no random-sampling overlap loss. Feeds anchor_validate.py.
"""

import argparse
import glob
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from injection_precision import injected_lines, assistant_text, is_self_referential

MAX_REPLY_CHARS = 3000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shadow", default="/home/kbd606/.claude/mind/.inj_anchor_shadow.jsonl")
    ap.add_argument("--glob", default="/home/kbd606/.claude/projects/*/*.jsonl")
    ap.add_argument("--qn-lo", type=int, default=3)
    ap.add_argument("--qn-hi", type=int, default=50)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    # in-window hash -> shares (last write wins)
    want = {}
    for line in open(args.shadow):
        line = line.strip()
        if not line:
            continue
        try:
            d = json.loads(line)
        except Exception:
            continue
        if args.qn_lo <= int(d.get("qn", 0)) <= args.qn_hi:
            want[d["h"]] = int(d["shares"])
    print(f"targeting {len(want)} in-window shadow hashes")

    found = {}
    for path in sorted(glob.glob(args.glob)):
        if is_self_referential(path):
            continue
        try:
            records = [json.loads(l) for l in open(path) if l.strip()]
        except Exception:
            continue
        pending = None
        for o in records:
            inj = injected_lines(o)
            if inj:
                pending = inj
                continue
            reply = assistant_text(o)
            if reply and pending:
                for lane, score, content, h in pending:
                    if h in want and h not in found:
                        found[h] = {
                            "h": h, "shares": want[h], "lane": lane, "score": score,
                            "content": content, "reply": reply[:MAX_REPLY_CHARS],
                            "session": Path(path).stem,
                        }
                pending = None

    with open(args.out, "w") as f:
        for rec in found.values():
            f.write(json.dumps(rec) + "\n")
    miss = len(want) - len(found)
    print(f"extracted {len(found)}/{len(want)} pairs -> {args.out}  (unmatched {miss}: injected this session, reply not yet in transcript, or non-sem)")
    from collections import Counter
    print("shares split of extracted:", dict(Counter(r["shares"] for r in found.values())))


if __name__ == "__main__":
    main()
