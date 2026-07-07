#!/usr/bin/env python3
"""#14 fine-tune, gate (b): mine training pairs from a dump_training_graph export.

Input:  <dump_dir>/nodes.jsonl + edges.jsonl  (chitta-field dump_graph bin,
        run on a COPY of the snapshot family — never the live store)
Output: <out_dir>/train.jsonl + heldout.jsonl  {"query","pos","neg"?,"tier"}
        <out_dir>/mining_report.json           exact per-filter counts

Pair tiers (all real memory text on both sides):
  co_derived   wisdom_i <-> wisdom_j sharing a DerivedFrom source episode
               (the episode payloads are locator stubs, so the *sibling*
               derivations are the usable supervision, not the episode)
  co_retrieved CoRetrieved edges w >= 0.8, degree-capped per node
  same_session SameSession edges (both endpoints live)

Exclusions (leakage + pollution):
  - the 58 golden-benchmark memory ids (any pair touching one)
  - memories containing any golden query string ([gap] dream atoms carry them)
  - lme* realms (lme/, lme16/, lme_speedtest/ — LongMemEval fixtures) + locomo
    realm (benchmark personas)
  - test probes from the corruption audit
  - content < MIN_LEN after stripping, exact normalized-content duplicates

Split: endpoint-id-hash agreement — sha1(id) % 10 == 9 -> heldout side; a pair
is kept only if BOTH endpoints agree on a side (crossers dropped), so no memory
id appears on both sides. All 30 golden queries stay a pure frozen test set.

Prefixes are NOT baked in here; the trainer adds search_query:/search_document:
(anchor = shorter text, deterministic).
"""

import argparse
import collections
import hashlib
import json
import re
import sys
from pathlib import Path

MIN_LEN = 40
CO_RETRIEVED_MIN_W = 0.8
DEGREE_CAP = 10          # max pairs any single node may appear in (per tier)
CO_DERIVED_GROUP_CAP = 20  # max sibling pairs mined per source episode
HELDOUT_BUCKET = 9       # sha1(id) % 10 == 9 -> heldout

# Denoise band (current nomic vectors from the .emb sidecar): a graph edge is
# only trusted as a semantic positive if the pair is already loosely related
# (co_derived groups span whole sessions; co_retrieved carries eval-churn).
# Floor rejects topically-unrelated junk; ceiling rejects near-duplicates that
# teach nothing. Calibrated against the store's measured anisotropy: random
# pairs sit at median cos 0.582 (p25 0.527), mined pairs at median 0.72 — the
# floor is ~random-p25, so only below-random junk dies and the learnable
# mid-band survives.
COS_FLOOR = 0.50
COS_CEIL = 0.97

PROBE_PAT = re.compile(
    r"medieval falconry|sourdough|f5test|live-wire check|dedup-test|zz_probe|zztest",
    re.I,
)


def norm_text(s: str) -> str:
    return re.sub(r"\s+", " ", s).strip().lower()


def side_of(mem_id: int) -> int:
    return int(hashlib.sha1(str(mem_id).encode()).hexdigest(), 16) % 10


def load_emb_sidecar(path: Path) -> dict:
    """Parse a chitta-field .emb sidecar: [magic u64][count u64] then
    (id u64 + 768 f32) records, little-endian. Returns id -> np row."""
    import numpy as np
    raw = np.fromfile(path, dtype=np.uint8)
    count = int(np.frombuffer(raw[8:16].tobytes(), dtype="<u8")[0])
    rec = 8 + 768 * 4
    body = raw[16:16 + count * rec].reshape(count, rec)
    ids = body[:, :8].copy().view("<u8").ravel()
    vecs = body[:, 8:].copy().view("<f4").reshape(count, 768)
    norms = np.linalg.norm(vecs, axis=1, keepdims=True)
    norms[norms == 0] = 1.0
    vecs = vecs / norms
    return {"ids": ids, "vecs": vecs,
            "row": {int(i): n for n, i in enumerate(ids)}}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("dump_dir", type=Path)
    ap.add_argument("out_dir", type=Path)
    ap.add_argument("--goldids", type=Path,
                    default=Path(__file__).parent.parent / "hooks/grade-recall-goldids.json")
    ap.add_argument("--emb", type=Path, default=None,
                    help=".emb sidecar from the same snapshot family; enables "
                         "the cosine denoise band (pairs without vectors are kept)")
    args = ap.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)

    gold = json.loads(args.goldids.read_text())
    gold_ids = {int(i) for ids in gold["ids"].values() for i in ids}
    gold_queries = [norm_text(q) for q in gold["ids"].keys()]

    report = collections.Counter()
    nodes = {}
    for line in open(args.dump_dir / "nodes.jsonl"):
        o = json.loads(line)
        report["nodes_total"] += 1
        realm = o["realm"]
        content = o["content"].strip()
        if realm.startswith("lme") or realm == "locomo":
            report["excl_benchmark_realm"] += 1
            continue
        if "locomo" in content[:200] and re.search(r"\bconv-\d+\b", content[:200]):
            report["excl_locomo_content"] += 1
            continue
        if content.startswith("[gap]"):
            report["excl_gap_atom"] += 1
            continue
        if PROBE_PAT.search(content):
            report["excl_test_probe"] += 1
            continue
        if len(content) < MIN_LEN:
            report["excl_too_short"] += 1
            continue
        nc = norm_text(content)
        if any(gq in nc for gq in gold_queries):
            report["excl_contains_gold_query"] += 1
            continue
        o["content"] = content
        o["_norm"] = nc
        nodes[o["id"]] = o

    # Exact normalized-content dedup: keep lowest id per text.
    by_text = {}
    for mid in sorted(nodes):
        t = nodes[mid]["_norm"]
        if t in by_text:
            report["excl_dup_content"] += 1
            del nodes[mid]
        else:
            by_text[t] = mid
    del by_text
    report["nodes_usable"] = len(nodes)

    def usable(mid: int) -> bool:
        return mid in nodes and mid not in gold_ids

    # ── Tier: co_derived — group DerivedFrom srcs by shared dst episode ──────
    derived_groups = collections.defaultdict(set)
    co_edges = []
    ss_edges = []
    for line in open(args.dump_dir / "edges.jsonl"):
        e = json.loads(line)
        if e["t"] == 0:
            if usable(e["src"]):
                derived_groups[e["dst"]].add(e["src"])
            elif e["src"] in gold_ids:
                report["excl_pair_touches_gold"] += 1
        elif e["t"] == 3 and e["w"] >= CO_RETRIEVED_MIN_W:
            co_edges.append(e)
        elif e["t"] == 1:
            ss_edges.append(e)

    pairs = []  # (a_id, b_id, tier)
    seen = set()

    def add_pair(a: int, b: int, tier: str, deg: collections.Counter) -> None:
        if a == b or not usable(a) or not usable(b):
            if a in gold_ids or b in gold_ids:
                report["excl_pair_touches_gold"] += 1
            return
        key = (min(a, b), max(a, b))
        if key in seen:
            report["excl_dup_pair"] += 1
            return
        if deg[a] >= DEGREE_CAP or deg[b] >= DEGREE_CAP:
            report[f"excl_degree_cap_{tier}"] += 1
            return
        seen.add(key)
        deg[a] += 1
        deg[b] += 1
        pairs.append((a, b, tier))
        report[f"pairs_{tier}"] += 1

    deg_cd = collections.Counter()
    for dst in sorted(derived_groups):
        sibs = sorted(derived_groups[dst])
        n_before = len(pairs)
        # chain + skip pairs, capped: adjacent-in-id-order gives diverse pairs
        for i in range(len(sibs) - 1):
            if len(pairs) - n_before >= CO_DERIVED_GROUP_CAP:
                report["excl_group_cap_co_derived"] += 1
                break
            add_pair(sibs[i], sibs[i + 1], "co_derived", deg_cd)

    deg_co = collections.Counter()
    co_edges.sort(key=lambda e: -e["w"])  # strongest first under the degree cap
    for e in co_edges:
        add_pair(e["src"], e["dst"], "co_retrieved", deg_co)

    deg_ss = collections.Counter()
    for e in ss_edges:
        add_pair(e["src"], e["dst"], "same_session", deg_ss)

    # ── Cosine denoise band (graph edges are noisy relevance labels) ─────────
    if args.emb:
        emb = load_emb_sidecar(args.emb)
        row = emb["row"]
        vecs = emb["vecs"]
        kept = []
        for a, b, tier in pairs:
            ra, rb = row.get(a), row.get(b)
            if ra is None or rb is None:
                report["cos_no_vector"] += 1
                kept.append((a, b, tier))
                continue
            cos = float(vecs[ra] @ vecs[rb])
            if cos < COS_FLOOR:
                report[f"excl_cos_floor_{tier}"] += 1
            elif cos > COS_CEIL:
                report[f"excl_cos_ceil_{tier}"] += 1
            else:
                kept.append((a, b, tier))
        pairs = kept

    # ── Split: endpoint-id-hash agreement ────────────────────────────────────
    out = {"train": [], "heldout": []}
    for a, b, tier in pairs:
        sa, sb = side_of(a) == HELDOUT_BUCKET, side_of(b) == HELDOUT_BUCKET
        if sa != sb:
            report["excl_split_crosser"] += 1
            continue
        ca, cb = nodes[a]["content"], nodes[b]["content"]
        query, pos = (ca, cb) if len(ca) <= len(cb) else (cb, ca)
        rec = {"query": query, "pos": pos, "tier": tier,
               "a": a, "b": b,
               "realm_a": nodes[a]["realm"], "realm_b": nodes[b]["realm"]}
        out["heldout" if sa else "train"].append(rec)

    for name, recs in out.items():
        with open(args.out_dir / f"{name}.jsonl", "w") as f:
            for r in recs:
                f.write(json.dumps(r) + "\n")
        report[f"final_{name}"] = len(recs)
        for r in recs:
            report[f"final_{name}_{r['tier']}"] += 1

    report_d = dict(sorted(report.items()))
    (args.out_dir / "mining_report.json").write_text(json.dumps(report_d, indent=2))
    json.dump(report_d, sys.stdout, indent=2)
    print()


if __name__ == "__main__":
    main()
