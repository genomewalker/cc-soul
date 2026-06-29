#!/usr/bin/env python3
"""G0 recall grader — nDCG@10 scoring against memory-ID ground truth.

Two-phase design:
  --bootstrap   Discover gold memory IDs by running bootstrap queries, save to
                grade-recall-goldids.json. Must be run once (or after memories
                change significantly) before grading.
  (default)     Load gold IDs, run natural-language queries, compute nDCG@10.

nDCG@10: for each query, rank position of gold memories in recall@LIMIT results.
DCG = sum(1/log2(pos+2) for gold hits in top LIMIT). nDCG = DCG/IDCG.
Binary relevance (1 if gold ID in results, 0 otherwise).

Usage:
  grade-recall.py --bootstrap [--limit 20]
  grade-recall.py [--limit 20] [--min 0.7] [--quiet]

Exit 0 if mean_nDCG@LIMIT >= --min, else 1.
"""
import argparse, json, math, os, subprocess, sys
from datetime import datetime, timezone
from pathlib import Path

# Cross-encoder reranker — loaded once, used for all queries
_reranker = None
_RERANKER_MODEL = "cross-encoder/ms-marco-MiniLM-L-12-v2"
_RERANK_FETCH_MUL = 4  # fetch this many × limit, then rerank to top-limit

_sigmoid = lambda x: 1.0 / (1.0 + math.exp(-x))
ABSTAIN_SCORE_THRESHOLD = 0.6   # epistemic: sigmoid(cross-encoder) below threshold → undetermined
ABSTAIN_BAND_EPS = 0.05         # posterior-band: gold/non-gold score gap < ε → rank is noise

def _load_reranker():
    global _reranker
    if _reranker is not None:
        return _reranker
    try:
        import warnings
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            from sentence_transformers import CrossEncoder
            _reranker = CrossEncoder(_RERANKER_MODEL)
    except Exception:
        _reranker = False  # sentinel: tried and failed
    return _reranker

GOLDEN_VERSION = 3

# Natural-language queries a user would actually type, paired with:
#   bootstrap_query: a targeted query to find the gold memory during bootstrap
#   gold_signature:  unique string that ONLY appears in the target memory (not in the query)
#   desc:            human label
GOLDEN_SET = [
    {
        "query": "chaos nodes",
        "bootstrap_query": "dandycomp ssh direct no slurm chaos nodes",
        "gold_signature": "dandycomp*",  # matches dandycomp*fl and dandycomp*-fl variants
        "desc": "cluster alias",
    },
    {
        "query": "how does provenance deduplication work",
        "bootstrap_query": "content_prov_idx done signal write-gate dedup hash provenance",
        "gold_signature": "[done] provenance",
        "desc": "prov dedup gate",
    },
    {
        "query": "how does domain reliability work",
        "bootstrap_query": "domain reliability beta prior decay record_success can only decay",
        "gold_signature": "can only decay",
        "desc": "reliability wiring",
    },
    {
        "query": "why does recall get capped per realm",
        "bootstrap_query": "stratify recall realm cap stratify_recall_hits per-realm slots Thompson",
        "gold_signature": "stratify_recall_hits",
        "desc": "sampler fix",
    },
    {
        "query": "NFS resurrection problem",
        "bootstrap_query": "Isilon NFS resurrection snapshot deleted files resurrect",
        "gold_signature": "Isilon",
        "desc": "NFS gotcha",
    },
    {
        "query": "dream sweep gap filling",
        "bootstrap_query": "dream wander gap curiosity cross-session distillation sweep",
        "gold_signature": "dream-sweep",
        "desc": "dream sweep",
    },
    {
        "query": "forward bet prediction horizon",
        "bootstrap_query": "prediction horizon status:open forward bet wisdom testable",
        "gold_signature": "status:open",
        "desc": "forward bet",
    },
    {
        "query": "how to build chitta-field",
        "bootstrap_query": "chitta-field build cmake cargo rust toolchain 1.92 rustup build.sh",
        "gold_signature": "1.92",
        "desc": "build toolchain",
    },
    {
        "query": "how does semantic search work",
        "bootstrap_query": "HNSW semantic search embedding bge cosine similarity RRF chitta-field",
        "gold_signature": "hnsw",  # matches both SSL memories and NL-expansion variants
        "desc": "search arch",
    },
    {
        "query": "distillation pipeline wisdom episode",
        "bootstrap_query": "distill wisdom episode ssl synthesis pipeline",
        "gold_signature": "distill",
        "desc": "distillation pipeline",
    },
]

RESULTS_PATH    = Path(__file__).resolve().parent / "grade-recall-results.json"
GOLD_IDS_PATH   = Path(__file__).resolve().parent / "grade-recall-goldids.json"

GREEN = "\033[32m"; RED = "\033[31m"; YELLOW = "\033[33m"
BOLD = "\033[1m"; DIM = "\033[2m"; RESET = "\033[0m"


GRADE_REALM = "project:cc-soul"


def _recall_raw(query: str, limit: int, strategy: str = "") -> list[dict]:
    env = dict(os.environ, SQZ_NO_DEDUP="1")
    cmd = ["chitta", "recall", "--query", query, "--limit", str(limit),
           "--realm", GRADE_REALM, "--json"]
    if strategy:
        cmd += ["--strategy", strategy]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=60, env=env)
        return json.loads(out.stdout).get("results", [])
    except Exception:
        return []


RRF_STRATEGIES = ("hybrid", "bm25", "field")  # multi-strategy fusion sources
_RRF_K = 60  # RRF constant; 60 is standard literature default


def _rerank_pool(query: str, pool: list[dict], limit: int) -> tuple[list[dict], list[float] | None]:
    """Cross-encoder rerank a candidate pool. Returns (top_limit, normed_scores_or_None)."""
    reranker = _load_reranker()
    if not reranker or len(pool) <= limit:
        return pool[:limit], None
    pairs = [(query, h.get("text", "")) for h in pool]
    raw = reranker.predict(pairs)
    ranked = sorted(zip(raw, pool), key=lambda x: -float(x[0]))
    top = ranked[:limit]
    return [h for _, h in top], [_sigmoid(float(s)) for s, _ in top]


def _rank_rrf(query: str, limit: int) -> tuple[list[dict], list[float] | None]:
    """Reciprocal Rank Fusion across RRF_STRATEGIES, then cross-encoder rerank.

    Each strategy contributes 1/(k+rank+1) to every hit's RRF score.
    The fused pool is reranked by cross-encoder for final ordering.
    """
    fetch = limit * _RERANK_FETCH_MUL
    all_hits: dict[str, dict] = {}
    rrf_scores: dict[str, float] = {}
    for strategy in RRF_STRATEGIES:
        for rank, hit in enumerate(_recall_raw(query, fetch, strategy)):
            hid = str(hit["id"])
            all_hits.setdefault(hid, hit)
            rrf_scores[hid] = rrf_scores.get(hid, 0.0) + 1.0 / (_RRF_K + rank + 1)
    pool = [all_hits[hid] for hid in sorted(rrf_scores, key=lambda x: -rrf_scores[x])]
    return _rerank_pool(query, pool, limit)


def _rank(query: str, limit: int, strategy: str = "") -> tuple[list[dict], list[float] | None]:
    """Fetch + rerank. strategy='rrf' runs multi-strategy RRF fusion.
    Returns (hits, sigmoid_normed_scores) or (hits, None) if no reranker."""
    if strategy == "rrf":
        return _rank_rrf(query, limit)
    reranker = _load_reranker()
    fetch = limit * _RERANK_FETCH_MUL if reranker else limit
    results = _recall_raw(query, fetch, strategy)
    if not reranker or len(results) <= limit:
        return results[:limit], None
    pairs = [(query, h.get("text", "")) for h in results]
    raw = reranker.predict(pairs)
    ranked = sorted(zip(raw, results), key=lambda x: -float(x[0]))
    top = ranked[:limit]
    return [h for _, h in top], [_sigmoid(float(s)) for s, _ in top]


def recall(query: str, limit: int, strategy: str = "") -> list[dict]:
    hits, _ = _rank(query, limit, strategy)
    return hits


def ndcg(positions: list[int], n_gold: int, k: int) -> float:
    dcg = sum(1.0 / math.log2(p + 2) for p in positions if p < k)
    idcg = sum(1.0 / math.log2(i + 2) for i in range(min(n_gold, k)))
    return dcg / idcg if idcg > 0 else 0.0


def bootstrap(limit: int, strategy: str = "") -> dict[str, list[str]]:
    """Discover gold memory IDs for each golden query. Returns {desc: [id, ...]}."""
    gold_ids: dict[str, list[str]] = {}
    for item in GOLDEN_SET:
        sig = item["gold_signature"].lower()
        results = recall(item["bootstrap_query"], limit, strategy)
        hits = [r for r in results if sig in r.get("text", "").lower()]
        if not hits:
            results2 = recall(item["query"], limit, strategy)
            hits = [r for r in results2 if sig in r.get("text", "").lower()]
        ids = [h["id"] for h in hits[:3]]  # top-3 ranked; avoids IDCG inflation from broad signatures
        gold_ids[item["desc"]] = ids
        status = f"{GREEN}found {len(ids)} gold(s){RESET}" if ids else f"{RED}NOT FOUND{RESET}"
        print(f"  {item['desc']:35} {status}")
        if ids:
            for h in hits[:2]:
                print(f"    {DIM}{h['text'][:80]}{RESET}")
    return gold_ids


def grade_one(item: dict, gold_ids: dict, limit: int, strategy: str = "") -> dict:
    ids = gold_ids.get(item["desc"], [])
    sig = item["gold_signature"].lower()
    results, scores = _rank(item["query"], limit, strategy)
    result_ids = [r["id"] for r in results]
    result_texts = [r.get("text", "").lower() for r in results]

    # Sig-based positions: primary stable key (survives re-ingest/NFS resurrection)
    sig_positions = [i for i, t in enumerate(result_texts) if sig in t]

    # ID-based positions: secondary (may be stale after re-ingest)
    id_positions = [result_ids.index(gid) for gid in ids if gid in result_ids]

    # Union: take best coverage; sig is durable, ID is exact
    positions = sorted(set(sig_positions) | set(id_positions))
    sig_fallback = bool(sig_positions) and not id_positions

    n_gold = max(len(ids), len(positions), 1)
    score = ndcg(positions, n_gold, limit)

    # Abstain signals — two distinct triggers, do not collapse
    epistemic_abstain = False   # cross-encoder unconfident (w ≤ threshold)
    band_abstain = False        # gold/non-gold scores too close to trust ranking
    gold_max_score = None
    if scores and positions:
        gold_scores = [scores[p] for p in positions if p < len(scores)]
        if gold_scores:
            gold_max_score = max(gold_scores)
            best_gold_pos = min(positions)
            if gold_max_score < ABSTAIN_SCORE_THRESHOLD:
                epistemic_abstain = True
            # Band abstain: only when a non-gold item is ranked above the best gold
            # and their scores are within ε — forced ranking is noise, not signal.
            if best_gold_pos > 0:
                blocker_score = scores[best_gold_pos - 1]  # item ranked just above best gold
                if blocker_score - gold_max_score < ABSTAIN_BAND_EPS:
                    band_abstain = True

    return {
        "query": item["query"],
        "desc": item["desc"],
        "gold_ids": ids,
        "n_gold": len(ids),
        "n_results": len(results),
        "gold_positions": positions,
        "sig_fallback": sig_fallback,
        "ndcg": score,
        "pass": score >= 0.5,
        "epistemic_abstain": epistemic_abstain,
        "band_abstain": band_abstain,
        "abstain": epistemic_abstain or band_abstain,
        "gold_max_score": gold_max_score,
    }


def main():
    ap = argparse.ArgumentParser(description="G0 recall grader (nDCG@10)")
    ap.add_argument("--bootstrap", action="store_true", help="discover gold IDs and save")
    ap.add_argument("--limit", type=int, default=20)
    ap.add_argument("--bootstrap-limit", type=int, default=640, help="depth for bootstrap (default 640)")
    ap.add_argument("--min", type=float, default=0.7)
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--no-reranker", action="store_true", help="skip cross-encoder re-ranking")
    ap.add_argument("--strategy", default="", help="recall strategy: 'hybrid'|'bm25'|'field' or 'rrf' (multi-strategy RRF+rerank)")
    a = ap.parse_args()
    if a.no_reranker:
        global _reranker
        _reranker = False

    if a.bootstrap:
        blimit = a.bootstrap_limit
        print(f"{BOLD}Bootstrapping gold memory IDs (limit={blimit}, strategy={a.strategy or 'default'})...{RESET}")
        gold_ids = bootstrap(blimit, a.strategy)
        GOLD_IDS_PATH.write_text(json.dumps({"version": GOLDEN_VERSION, "ids": gold_ids}, indent=2) + "\n")
        print(f"\n{GREEN}Saved: {GOLD_IDS_PATH}{RESET}")
        found = sum(1 for v in gold_ids.values() if v)
        print(f"{found}/{len(GOLDEN_SET)} queries have gold IDs.")
        return

    # Load gold IDs
    if not GOLD_IDS_PATH.exists():
        print(f"{RED}No gold IDs file. Run: grade-recall.py --bootstrap{RESET}", file=sys.stderr)
        sys.exit(2)

    stored = json.loads(GOLD_IDS_PATH.read_text())
    gold_ids = stored.get("ids", {})

    records = [grade_one(it, gold_ids, a.limit, a.strategy) for it in GOLDEN_SET]
    mean_ndcg = sum(r["ndcg"] for r in records) / len(records) if records else 0.0
    active = [r for r in records if not r["abstain"]]
    mean_ndcg_active = sum(r["ndcg"] for r in active) / len(active) if active else 0.0
    n_pass = sum(1 for r in records if r["pass"] and not r["abstain"])
    n_fail = sum(1 for r in records if not r["pass"] and not r["abstain"])
    n_abstain = sum(1 for r in records if r["abstain"])
    ts = datetime.now(timezone.utc).isoformat()

    report = {
        "ts": ts,
        "version": GOLDEN_VERSION,
        "limit": a.limit,
        "metric": "mean_nDCG",
        "score": mean_ndcg,
        "score_active": mean_ndcg_active,
        "n": len(records),
        "n_pass": n_pass,
        "n_fail": n_fail,
        "n_abstain": n_abstain,
        "records": records,
    }
    RESULTS_PATH.write_text(json.dumps(report, indent=2) + "\n")

    # Store [gap] memories for abstain cases — seeds future dream curiosity
    for r in records:
        if r.get("abstain"):
            reason = "epistemic" if r.get("epistemic_abstain") else "band"
            gap_content = (
                f"[gap] query={r['query']!r} reason={reason} "
                f"nDCG={r['ndcg']:.3f} gold_score={r.get('gold_max_score') or 'n/a'}"
            )
            subprocess.run(
                ["chitta", "remember", "--content", gap_content,
                 "--realm", "project:cc-soul", "--type", "wisdom",
                 "--tags", "gap,abstain,grader", "--visibility", "1"],
                capture_output=True, timeout=10,
            )

    if not a.quiet:
        for r in records:
            if r["abstain"]:
                reason = "epistemic" if r["epistemic_abstain"] else "band"
                score_str = f"w={r['gold_max_score']:.2f}" if r["gold_max_score"] is not None else "w=?"
                pos_str = str(r["gold_positions"]) if r["gold_positions"] else "not found"
                print(f"{YELLOW}[ABSTAIN/{reason}]{RESET} {r['query']!r:45} nDCG={r['ndcg']:.3f}  pos={pos_str}  {score_str}")
            else:
                color = GREEN if r["pass"] else RED
                mark = "PASS" if r["pass"] else "FAIL"
                pos_str = str(r["gold_positions"]) if r["gold_positions"] else "not found"
                no_gold = f"{YELLOW}(no gold IDs){RESET}" if not r["n_gold"] else ""
                sig_tag = f"{DIM}[sig]{RESET}" if r.get("sig_fallback") else ""
                score_tag = f"  w={r['gold_max_score']:.2f}" if r["gold_max_score"] is not None else ""
                print(f"{color}[{mark}]{RESET} {r['query']!r:45} nDCG={r['ndcg']:.3f}  pos={pos_str}{score_tag} {no_gold}{sig_tag}")
        verdict = GREEN if mean_ndcg >= a.min else RED
        print(f"\n{BOLD}{verdict}mean nDCG@{a.limit} = {mean_ndcg:.3f}{RESET}  (min={a.min})")
        if n_abstain:
            verdict2 = GREEN if mean_ndcg_active >= a.min else RED
            print(f"  active (excl. {n_abstain} abstain): {BOLD}{verdict2}{mean_ndcg_active:.3f}{RESET}  "
                  f"pass={n_pass} fail={n_fail} abstain={n_abstain}")
        print(f"report: {RESULTS_PATH}")

    print(f"SCORE={mean_ndcg:.4f}")
    sys.exit(0 if mean_ndcg >= a.min else 1)


if __name__ == "__main__":
    main()
