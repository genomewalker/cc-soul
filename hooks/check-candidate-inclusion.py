#!/usr/bin/env python3
"""Check whether gold IDs appear in the pre-DAM candidate pool (default recall at fetch_k depth)."""

import json
import os
import subprocess

GOLD_FILE = os.path.join(os.path.dirname(__file__), "grade-recall-goldids.json")
FETCH_K = 80  # k=20 × dam_fetch_mul=4

GOLDEN_SET = [
    {"query": "chaos nodes", "desc": "cluster alias"},
    {"query": "how does provenance deduplication work", "desc": "prov dedup gate"},
    {"query": "how does domain reliability work", "desc": "reliability wiring"},
    {"query": "why does recall get capped per realm", "desc": "sampler fix"},
    {"query": "NFS resurrection problem", "desc": "NFS gotcha"},
    {"query": "dream sweep gap filling", "desc": "dream sweep"},
    {"query": "forward bet prediction horizon", "desc": "forward bet"},
    {"query": "how to build chitta-field", "desc": "build toolchain"},
    {"query": "how does semantic search work", "desc": "search arch"},
    {"query": "distillation pipeline wisdom episode", "desc": "distillation pipeline"},
]


def recall(query, limit):
    env = {**os.environ, "SQZ_NO_DEDUP": "1"}
    r = subprocess.run(
        ["chitta", "recall", "--query", query, "--limit", str(limit), "--json"],
        capture_output=True,
        text=True,
        timeout=60,
        env=env,
    )
    if r.returncode != 0:
        return []
    try:
        data = json.loads(r.stdout)
        return [str(h["id"]) for h in data.get("results", [])]
    except (json.JSONDecodeError, KeyError, TypeError):
        # Non-JSON reply, or hits without an id: treated as no candidates.
        return []


def main():
    gold = json.load(open(GOLD_FILE))["ids"]

    total_gold = 0
    total_found = 0

    print(f"Checking gold ID inclusion in top-{FETCH_K} default candidates\n")
    print(f"{'Query':<45} {'gold':>5} {'found':>6} {'rate':>6}  positions")
    print("-" * 80)

    for item in GOLDEN_SET:
        desc = item["desc"]
        query = item["query"]
        gids = set(gold.get(desc, []))
        if not gids:
            print(f"  {query:<43} {'—':>5}  (no gold IDs)")
            continue

        hits = recall(query, FETCH_K)
        hit_set = set(hits)
        found_ids = gids & hit_set
        positions = [i + 1 for i, h in enumerate(hits) if h in gids]

        rate = len(found_ids) / len(gids)
        total_gold += len(gids)
        total_found += len(found_ids)

        flag = "✓" if found_ids else "✗"
        print(
            f"{flag} {query:<43} {len(gids):>5} {len(found_ids):>6} {rate:>5.0%}  pos={positions}"
        )

    print("-" * 80)
    overall = total_found / total_gold if total_gold else 0
    print(f"  Overall: {total_found}/{total_gold} gold IDs in top-{FETCH_K} ({overall:.0%})\n")
    print("NOTE: if inclusion rate < 100%, reranking cannot fix the miss — it's a recall problem.")


if __name__ == "__main__":
    main()
