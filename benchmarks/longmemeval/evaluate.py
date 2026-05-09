#!/usr/bin/env python3
"""
LongMemEval-S Retrieval Benchmark for chitta

Measures session-level retrieval recall (R@K, NDCG@10, MRR) against the
LongMemEval-S dataset (500 questions, ~53 haystack sessions each).

This is a retrieval-only evaluation — no answer generation. Comparable to
agentmemory's reported 95.2% R@5, which is also retrieval-only.

Usage:
    python evaluate.py --data /path/to/longmemeval_s_cleaned.json
    python evaluate.py --data /path/to/longmemeval_s_cleaned.json --samples 50
    python evaluate.py --data /path/to/longmemeval_s_cleaned.json --k 1 3 5 10
"""

import json
import subprocess
import argparse
import time
import math
import sys
import os
import re
from pathlib import Path
from typing import Optional
from concurrent.futures import ThreadPoolExecutor, as_completed

CHITTA = Path.home() / ".claude/bin/chitta"
REALM_PREFIX = "lme-bench"
SESSION_PREFIX = "SESSION_ID:"


def chitta_run(args: list[str]) -> str:
    r = subprocess.run([str(CHITTA)] + args, capture_output=True, text=True)
    if r.returncode != 0 and r.stderr:
        print(f"[warn] chitta error: {r.stderr.strip()[:200]}", file=sys.stderr)
    return r.stdout.strip()


def realm_for(qid: str) -> str:
    return f"{REALM_PREFIX}-{qid}"


CHUNK_SIZE = 512
CHUNK_OVERLAP = 64


def chunk_session(sess_id: str, turns: list) -> list[str]:
    """Split session into overlapping chunks, each prefixed with SESSION_ID for extraction."""
    # Flatten all turns into one text
    flat = "\n".join(
        f"{t.get('role','')}: {t.get('content','')}"
        for t in turns if t.get("content")
    )
    chunks = []
    step = CHUNK_SIZE - CHUNK_OVERLAP
    for i in range(0, max(1, len(flat)), step):
        chunk_text = f"{SESSION_PREFIX}{sess_id}\n{flat[i:i+CHUNK_SIZE]}"
        chunks.append(chunk_text)
        if i + CHUNK_SIZE >= len(flat):
            break
    return chunks


def ingest_question(qid: str, haystack_sessions: list, haystack_session_ids: list) -> float:
    """Ingest all sessions for a question into an isolated realm. Returns elapsed seconds."""
    realm = realm_for(qid)
    t0 = time.time()
    for sess_id, turns in zip(haystack_session_ids, haystack_sessions):
        for chunk in chunk_session(sess_id, turns):
            chitta_run(["remember", "--content", chunk, "--realm", realm, "--type", "episode",
                        "--source_session", sess_id])
    return time.time() - t0


def extract_session_ids(results: list[dict]) -> list[str]:
    """Parse SESSION_ID: prefixes from recalled memory texts."""
    ids = []
    for r in results:
        text = r.get("text", "")
        m = re.search(rf"{re.escape(SESSION_PREFIX)}(\S+)", text)
        if m:
            ids.append(m.group(1))
    return ids


def recall_question(qid: str, question: str, k: int, mode: str = "chunk_dedup") -> list[str]:
    """Run recall, return top-K unique session IDs.

    mode="chunk_dedup": semantic recall + regex SESSION_ID extraction (original approach)
    mode="session_engram": uses native recall_session tool (Phase 4 aggregation)
    """
    realm = realm_for(qid)
    if mode == "session_engram":
        raw = chitta_run(["recall_session", "--query", question, "--realm", realm,
                          "--limit", str(k), "--json"])
        try:
            data = json.loads(raw)
            results = data.get("results", [])
        except json.JSONDecodeError:
            return []
        return [r["session_id"] for r in results if r.get("session_id")]

    # Default: chunk_dedup — fetch many chunks and deduplicate by SESSION_ID prefix
    fetch_limit = k * 8
    raw = chitta_run(["recall", "--query", question, "--realm", realm, "--limit", str(fetch_limit), "--json"])
    try:
        data = json.loads(raw)
        results = data.get("results", [])
    except json.JSONDecodeError:
        return []
    seen = []
    seen_set = set()
    for r in results:
        text = r.get("text", "")
        m = re.search(rf"{re.escape(SESSION_PREFIX)}(\S+)", text)
        if m:
            sid = m.group(1)
            if sid not in seen_set:
                seen_set.add(sid)
                seen.append(sid)
        if len(seen) >= k:
            break
    return seen


def cleanup_realm(qid: str):
    """Delete all episode memories ingested for this question's realm."""
    chitta_run(["forget_kind", "--kind", "episode", "--realm", realm_for(qid), "--limit", "5000"])


def reciprocal_rank(retrieved: list[str], gold: set[str]) -> float:
    for i, sid in enumerate(retrieved, 1):
        if sid in gold:
            return 1.0 / i
    return 0.0


def ndcg(retrieved: list[str], gold: set[str], k: int = 10) -> float:
    dcg = sum(
        (1.0 / math.log2(i + 2)) for i, sid in enumerate(retrieved[:k]) if sid in gold
    )
    ideal_hits = min(len(gold), k)
    idcg = sum(1.0 / math.log2(i + 2) for i in range(ideal_hits))
    return dcg / idcg if idcg > 0 else 0.0


def recall_at_k(retrieved: list[str], gold: set[str], k: int) -> bool:
    return any(sid in gold for sid in retrieved[:k])


def eval_one(i: int, total: int, q: dict, k_values: list[int], mode: str = "chunk_dedup") -> dict:
    qid = q["question_id"]
    qtype = q["question_type"]
    gold = set(q["answer_session_ids"])
    max_k = max(k_values)

    t_ingest = ingest_question(qid, q["haystack_sessions"], q["haystack_session_ids"])
    retrieved = recall_question(qid, q["question"], max_k, mode=mode)
    cleanup_realm(qid)

    rr = reciprocal_rank(retrieved, gold)
    ndcg_val = ndcg(retrieved, gold, 10)
    hits = {k: recall_at_k(retrieved, gold, k) for k in k_values}

    hit_str = " ".join(f"R@{k}={'✓' if hits[k] else '✗'}" for k in k_values)
    print(f"[{i+1}/{total}] {qid} ({qtype})  MRR={rr:.2f} {hit_str}  ingest={t_ingest:.1f}s", flush=True)

    return {
        "question_id": qid,
        "question_type": qtype,
        "gold": list(gold),
        "retrieved": retrieved,
        "mrr": rr,
        "ndcg10": ndcg_val,
        **{f"R@{k}": hits[k] for k in k_values},
    }


def run_benchmark(data: list[dict], samples: Optional[int], k_values: list[int], workers: int = 4, mode: str = "chunk_dedup") -> dict:
    if samples:
        data = data[:samples]

    q_types = sorted(set(q["question_type"] for q in data))
    n = len(data)

    per_question = []
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {pool.submit(eval_one, i, n, q, k_values, mode): i for i, q in enumerate(data)}
        for fut in as_completed(futures):
            per_question.append(fut.result())

    # Sort back to original order for reproducibility
    order = {q["question_id"]: i for i, q in enumerate(data)}
    per_question.sort(key=lambda r: order[r["question_id"]])

    r_at_k_counts = {k: sum(1 for r in per_question if r[f"R@{k}"]) for k in k_values}
    ndcg_sum = sum(r["ndcg10"] for r in per_question)
    mrr_sum = sum(r["mrr"] for r in per_question)
    type_total: dict[str, int] = {}
    type_counts: dict[str, dict[int, int]] = {}
    for r in per_question:
        qt = r["question_type"]
        type_total[qt] = type_total.get(qt, 0) + 1
        type_counts.setdefault(qt, {k: 0 for k in k_values})
        for k in k_values:
            if r[f"R@{k}"]:
                type_counts[qt][k] += 1

    overall = {f"R@{k}": r_at_k_counts[k] / n for k in k_values}
    overall["NDCG@10"] = ndcg_sum / n
    overall["MRR"] = mrr_sum / n

    by_type = {}
    for qt in q_types:
        nt = type_total.get(qt, 0)
        by_type[qt] = {f"R@{k}": type_counts.get(qt, {}).get(k, 0) / nt for k in k_values} if nt else {}
        by_type[qt]["count"] = nt

    return {
        "total": n,
        "k_values": k_values,
        "workers": workers,
        "mode": mode,
        "overall": overall,
        "by_type": by_type,
        "per_question": per_question,
    }


def print_summary(results: dict):
    print("\n" + "=" * 60)
    print("LongMemEval-S Retrieval Benchmark — chitta")
    print("=" * 60)
    print(f"Questions evaluated: {results['total']}")
    print()
    print("Overall:")
    for metric, val in results["overall"].items():
        bar = "█" * int(val * 20)
        print(f"  {metric:<10} {val:.3f}  {bar}")
    print()
    print("By question type:")
    for qt, metrics in results["by_type"].items():
        n = metrics.pop("count", 0)
        vals = "  ".join(f"{m}={v:.3f}" for m, v in metrics.items())
        print(f"  {qt:<35} n={n:3d}  {vals}")
        metrics["count"] = n


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", default="/projects/caeg/scratch/kbd606/tmp/longmemeval/longmemeval_s_cleaned.json")
    parser.add_argument("--samples", type=int, default=None, help="Limit to first N questions")
    parser.add_argument("--k", type=int, nargs="+", default=[1, 3, 5, 10])
    parser.add_argument("--workers", type=int, default=4, help="Parallel workers")
    parser.add_argument("--mode", default="chunk_dedup", choices=["chunk_dedup", "session_engram"],
                        help="Recall mode: chunk_dedup (default) or session_engram")
    parser.add_argument("--out", default="/projects/caeg/scratch/kbd606/tmp/longmemeval/chitta_results.json")
    args = parser.parse_args()

    with open(args.data) as f:
        data = json.load(f)

    print(f"Dataset: {len(data)} questions")
    print(f"Running: {args.samples or len(data)} questions, K={args.k}")
    print(f"Chitta:  {CHITTA}")
    print()

    t0 = time.time()
    results = run_benchmark(data, args.samples, args.k, workers=args.workers, mode=args.mode)
    results["elapsed_seconds"] = time.time() - t0

    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(results, f, indent=2)

    print_summary(results)
    print(f"\nResults saved to: {args.out}")
    print(f"Total time: {results['elapsed_seconds']/60:.1f} min")


if __name__ == "__main__":
    main()
