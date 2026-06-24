#!/usr/bin/env python3
"""Sweep fetch_k to measure gold_in_pool@K — decisive experiment for recall vs encoder bottleneck."""

import json, subprocess, os, sys

GOLD_FILE = os.path.join(os.path.dirname(__file__), "grade-recall-goldids.json")
FETCH_KS  = [80, 160, 320, 640]

QUERIES = [
    {"query": "chaos nodes",                          "desc": "cluster alias"},
    {"query": "NFS resurrection problem",              "desc": "NFS gotcha"},
    {"query": "dream sweep gap filling",               "desc": "dream sweep"},
    {"query": "distillation pipeline wisdom episode",  "desc": "distillation pipeline"},
]

GRADE_REALM = "project:cc-soul"

def recall(query, limit):
    env = {**os.environ, "SQZ_NO_DEDUP": "1"}
    r = subprocess.run(
        ["chitta", "recall", "--query", query, "--limit", str(limit),
         "--realm", GRADE_REALM, "--json"],
        capture_output=True, text=True, timeout=120, env=env,
    )
    if r.returncode != 0:
        return []
    try:
        data = json.loads(r.stdout)
        return [str(h["id"]) for h in data.get("results", [])]
    except Exception:
        return []

def main():
    gold = json.load(open(GOLD_FILE))["ids"]

    # Fetch each k level separately (HNSW ef_search scales with k; single large call times out).
    print(f"Fetching candidates per query per k...")
    fetched = {}  # desc -> (gids, {k: [hit_ids]})
    for item in QUERIES:
        desc = item["desc"]
        gids = set(gold.get(desc, []))
        if not gids:
            continue
        by_k = {}
        for k in FETCH_KS:
            hits = recall(item["query"], k)
            by_k[k] = hits
            found = len(gids & set(hits))
            print(f"  {item['query'][:30]:<30} k={k:>4}: {len(hits):>4} results, {found}/{len(gids)} gold")
        fetched[desc] = (gids, by_k)

    # Sweep fetch_k by slicing the pre-fetched list.
    print(f"\ngold_in_pool@K by query\n")
    header = f"{'Query':<30}" + "".join(f"  k={k:>4}" for k in FETCH_KS)
    print(header)
    print("-" * len(header))

    totals = {k: [0, 0] for k in FETCH_KS}  # [found, total]

    for item in QUERIES:
        desc = item["desc"]
        if desc not in fetched:
            continue
        gids, by_k = fetched[desc]
        row = f"{item['query'][:30]:<30}"
        for k in FETCH_KS:
            pool = set(by_k.get(k, []))
            found = len(gids & pool)
            rate = found / len(gids)
            row += f"  {found}/{len(gids)} {rate:>3.0%}"
            totals[k][0] += found
            totals[k][1] += len(gids)
        print(row)

    print("-" * len(header))
    total_row = f"{'TOTAL':<30}"
    for k in FETCH_KS:
        f, t = totals[k]
        total_row += f"  {f}/{t} {f/t:>3.0%}" if t else "  —"
    print(total_row)

    print("\nInterpretation:")
    k_max = max(FETCH_KS)
    f_max, t_max = totals[k_max]
    if t_max:
        rate_max = f_max / t_max
        if rate_max < 0.5:
            print(f"  ⚠ Even at k={k_max}: {f_max}/{t_max} ({rate_max:.0%}) — ENCODER BOTTLENECK")
            print("  → Re-embedding with current encoder (jina-v5, NV-Retriever) is required.")
        else:
            print(f"  ✓ Gold reachable at depth — DEPTH/ORACLE problem, not encoder.")
            print("  → Increase fetch_k, HyDE query expansion, oracle rebuild.")

if __name__ == "__main__":
    main()
