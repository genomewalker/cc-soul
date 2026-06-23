#!/usr/bin/env python3
"""G0 golden-set recall grader.

Runs each golden query against the live chitta daemon, scores hit@5
(case-insensitive OR-match of expected keywords against the top-5 result
text), aggregates the mean as precision@5, writes a timestamped JSON
report, and prints a colored summary.

Usage: grade-recall.py [--limit 5] [--min 0.7] [--quiet]
Exit code: 0 if precision@5 >= --min, else 1.
"""
import argparse
import json
import subprocess
import sys
import os
from datetime import datetime, timezone
from pathlib import Path

GOLDEN_VERSION = 1

GOLDEN_SET = [
    {"query": "chaos nodes", "expected": ["dandycomp", "dandycomp01fl", "dandycomp*fl"], "desc": "cluster alias"},
    {"query": "content_prov_idx", "expected": ["done", "signal", "dedup", "provenance"], "desc": "prov dedup gate"},
    {"query": "record_partial_success", "expected": ["0.3", "recurrence", "domain_reliability"], "desc": "reliability wiring"},
    {"query": "stratify_recall_hits", "expected": ["realm", "cap", "recall_with_fallback"], "desc": "sampler fix"},
    {"query": "NFS resurrection", "expected": ["Isilon", "snapshot", "resurrection"], "desc": "NFS gotcha"},
    {"query": "dream wander gap filling", "expected": ["gap", "dream", "curiosity"], "desc": "dream sweep"},
    {"query": "forward bet prediction", "expected": ["prediction", "horizon", "status:open", "wisdom"], "desc": "forward bet"},
    {"query": "chitta field build toolchain", "expected": ["1.93.0", "cargo", "rustc"], "desc": "build toolchain"},
    {"query": "HNSW semantic search", "expected": ["semantic", "embedding", "hnsw", "cosine"], "desc": "search arch"},
    {"query": "distillation wisdom episode", "expected": ["distill", "wisdom", "episode", "ssl"], "desc": "distillation pipeline"},
]

RESULTS_PATH = Path(__file__).resolve().parent / "grade-recall-results.json"

GREEN = "\033[32m"
RED = "\033[31m"
BOLD = "\033[1m"
RESET = "\033[0m"


def recall(query, limit):
    env = dict(os.environ, SQZ_NO_DEDUP="1")
    out = subprocess.run(
        ["chitta", "recall", "--query", query, "--limit", str(limit), "--json"],
        capture_output=True, text=True, timeout=30, env=env,
    )
    data = json.loads(out.stdout)
    return [r.get("text", "") for r in data.get("results", [])]


def grade_one(item, limit):
    texts = recall(item["query"], limit)
    blob = "\n".join(texts).lower()
    hits = [kw for kw in item["expected"] if kw.lower() in blob]
    return {
        "query": item["query"],
        "desc": item["desc"],
        "expected": item["expected"],
        "hits": hits,
        "n_results": len(texts),
        "score": 1.0 if hits else 0.0,
        "pass": bool(hits),
    }


def main():
    ap = argparse.ArgumentParser(description="G0 golden-set recall grader")
    ap.add_argument("--limit", type=int, default=5)
    ap.add_argument("--min", type=float, default=0.7)
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()

    records = [grade_one(it, a.limit) for it in GOLDEN_SET]
    npass = sum(r["pass"] for r in records)
    precision = npass / len(records)
    ts = datetime.now(timezone.utc).isoformat()

    report = {
        "ts": ts,
        "version": GOLDEN_VERSION,
        "limit": a.limit,
        "precision_at_5": precision,
        "n": len(records),
        "pass": npass,
        "fail": len(records) - npass,
        "records": records,
    }
    RESULTS_PATH.write_text(json.dumps(report, indent=2) + "\n")

    if not a.quiet:
        for r in records:
            color = GREEN if r["pass"] else RED
            mark = "PASS" if r["pass"] else "FAIL"
            print(f"{color}[{mark}]{RESET} {r['query']!r:42} hits={r['hits']}")
        verdict = GREEN if precision >= a.min else RED
        print(f"{BOLD}{verdict}precision@{a.limit} = {precision:.3f} "
              f"({npass}/{len(records)}){RESET}")
        print(f"report: {RESULTS_PATH}")

    print(f"SCORE={precision:.4f}")
    sys.exit(0 if precision >= a.min else 1)


if __name__ == "__main__":
    main()
