#!/usr/bin/env python3
"""M0.1 — sample injected-memory/reply pairs for LLM-judge precision.

Reuses the M0 parser (injection_precision.py). Emits JSONL:
{lane, score, content, reply, session, overlap} — stratified by lane so sem
(the dominant lane) doesn't drown kw/corr/xr. Reply truncated: the judge only
needs enough of it to decide "did the reply draw on this memory".
"""

import argparse
import glob
import json
import random
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from injection_precision import (
    injected_lines, assistant_text, is_self_referential, norm_tokens,
)

MAX_REPLY_CHARS = 3000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glob", default="/home/kbd606/.claude/projects/*/*.jsonl")
    ap.add_argument("--n", type=int, default=200)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    by_lane = defaultdict(list)
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
                rt = norm_tokens(reply)
                for lane, score, content, h in pending:
                    it = norm_tokens(content)
                    ov = len(it & rt) / len(it) if len(it) >= 3 else 0.0
                    by_lane[lane].append({
                        "lane": lane, "score": score, "content": content, "h": h,
                        "reply": reply[:MAX_REPLY_CHARS],
                        "session": Path(path).stem, "overlap": round(ov, 3),
                    })
                pending = None

    rng = random.Random(args.seed)
    lanes = [l for l in ("sem", "hyb", "kw", "corr", "xr") if by_lane[l]]
    quota = args.n // len(lanes) if lanes else 0
    sample, leftovers = [], []
    for l in lanes:
        pool = by_lane[l]
        rng.shuffle(pool)
        sample.extend(pool[:quota])
        leftovers.extend(pool[quota:])
    rng.shuffle(leftovers)
    sample.extend(leftovers[: args.n - len(sample)])

    with open(args.out, "w") as f:
        for s in sample:
            f.write(json.dumps(s) + "\n")

    print(f"pool sizes: {dict((l, len(by_lane[l])) for l in by_lane)}")
    print(f"sampled {len(sample)} -> {args.out}")
    from collections import Counter
    print(f"sample lanes: {Counter(s['lane'] for s in sample)}")


if __name__ == "__main__":
    main()
