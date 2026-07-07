#!/usr/bin/env python3
"""M0.2 — validate the turn-entity anchor gate before flipping it to enforce.

Joins the LIVE shadow log (hash -> shares_entity, recorded at injection time with
the real user turn) against a FRESH Fable-judged audit (hash -> used). Prints the
confusion matrix and the two enforce gates from Fable's guardrail:

  GATE-A precision lift : precision(shares=1) >= 2x precision(shares=0)
  GATE-B used-recall    : the gate keeps >= 90% of USED items (drops <= 10%)

Both must hold on OUT-OF-SAMPLE (fresh-session) data or enforce stays off — this
is the anti-#14 guardrail: precision alone is Goodhartable by silently dropping
the paraphrase-used memories (used without sharing a verbatim entity), so we
require used-recall to survive too.

Inputs:
  --shadow  .inj_anchor_shadow.jsonl  ({h,conf,shares,qn}, appended by prompt-core.sh)
  --labels  labels.jsonl              ({h, used} or {h, verdict:"USED"/"WASTED"/"UNCLEAR"})
  --qn-lo/--qn-hi  the query-size window the gate acts in (default 3..50)

A hash present in labels but absent from shadow = injected before the gate
shipped (or a non-sem lane); reported as unjoined, excluded from the matrix.
"""

import argparse
import json
from collections import defaultdict


def load_shadow(path, qn_lo, qn_hi):
    # hash -> shares (last write wins; shares is stable per hash/turn). Only
    # in-window entries, since the gate only acts inside [qn_lo, qn_hi].
    sh = {}
    for line in open(path):
        line = line.strip()
        if not line:
            continue
        try:
            d = json.loads(line)
        except Exception:
            continue
        qn = int(d.get("qn", 0))
        if qn < qn_lo or qn > qn_hi:
            continue
        sh[d["h"]] = int(d["shares"])
    return sh


def load_labels(path):
    # hash -> used(bool); drop UNCLEAR.
    lab = {}
    for line in open(path):
        line = line.strip()
        if not line:
            continue
        d = json.loads(line)
        if "used" in d:
            u = bool(d["used"])
        else:
            v = str(d.get("verdict", "")).upper()
            if v not in ("USED", "WASTED"):
                continue
            u = v == "USED"
        lab[d["h"]] = u
    return lab


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shadow", default="/home/kbd606/.claude/mind/.inj_anchor_shadow.jsonl")
    ap.add_argument("--labels", required=True)
    ap.add_argument("--qn-lo", type=int, default=3)
    ap.add_argument("--qn-hi", type=int, default=50)
    ap.add_argument("--lift", type=float, default=2.0)
    ap.add_argument("--used-recall", type=float, default=0.90)
    args = ap.parse_args()

    shadow = load_shadow(args.shadow, args.qn_lo, args.qn_hi)
    labels = load_labels(args.labels)

    cm = defaultdict(int)  # (shares, used) -> count
    joined = 0
    unjoined = 0
    for h, used in labels.items():
        if h not in shadow:
            unjoined += 1
            continue
        cm[(shadow[h], used)] += 1
        joined += 1

    s1u = cm[(1, True)]; s1w = cm[(1, False)]
    s0u = cm[(0, True)]; s0w = cm[(0, False)]
    total_used = s1u + s0u
    n1 = s1u + s1w
    n0 = s0u + s0w

    print(f"joined={joined} unjoined(no shadow / pre-gate / non-sem)={unjoined} "
          f"window=qn[{args.qn_lo},{args.qn_hi}]")
    print("confusion (shares x used):")
    print(f"  shares=1: used={s1u:4d}  wasted={s1w:4d}  (kept by gate)")
    print(f"  shares=0: used={s0u:4d}  wasted={s0w:4d}  (dropped by gate)")

    if joined == 0 or total_used == 0:
        print("VERDICT: insufficient joined data — accrue more shadow + a fresh audit.")
        return

    p_all = total_used / joined
    p1 = s1u / n1 if n1 else float("nan")
    p0 = s0u / n0 if n0 else float("nan")
    used_recall = s1u / total_used  # fraction of USED items the gate KEEPS
    dropped_used = s0u

    print(f"precision(all)      = {p_all:.3f}  ({total_used}/{joined})")
    print(f"precision(shares=1) = {p1:.3f}  (this is post-enforce precision)")
    print(f"precision(shares=0) = {p0:.3f}  (what the gate removes)")
    print(f"used-recall(gate)   = {used_recall:.3f}  (drops {dropped_used} USED of {total_used})")

    # Fable's two gates.
    gate_a = (p0 == 0 and p1 > 0) or (p0 > 0 and p1 >= args.lift * p0)
    gate_b = used_recall >= args.used_recall
    print(f"GATE-A precision-lift (>= {args.lift}x): {'PASS' if gate_a else 'FAIL'}")
    print(f"GATE-B used-recall (>= {args.used_recall:.0%}): {'PASS' if gate_b else 'FAIL'}")
    if gate_a and gate_b:
        print("=> ENFORCE OK: set CC_SOUL_ANCHOR_ENFORCE=1 (precision rises AND used survives).")
    else:
        print("=> KEEP SHADOW: at least one gate failed — enforcing would repeat the #14 mirage.")


if __name__ == "__main__":
    main()
