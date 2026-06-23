#!/usr/bin/env python3
"""hopfield-probe.py — validate Modern Hopfield / DAM retrieval over the chitta candidate set.

For each test query:
  1. chitta recall --limit CANDIDATES → top-M candidates (HNSW ranking)
  2. Re-embed all candidates + query with bge-large-en-v1.5
  3. Run T-step DAM update: s(t+1) = X @ softmax(β * X.T @ s(t))
  4. Report: HNSW top-1 vs DAM winner at each β
  5. "Associative flip" = DAM converges to different memory than HNSW top-1

Falsification gate: zero flips across all β/query combos → Field-RAG is cosine kNN in a costume.

Usage:
  hopfield-probe.py [--candidates 200] [--steps 10] [--model BAAI/bge-large-en-v1.5]
"""
import argparse, json, os, subprocess, sys
import numpy as np

BETAS = [5, 10, 25, 50, 100]

TEST_QUERIES = [
    {"query": "chaos nodes",                        "target_hint": "dandycomp"},
    {"query": "NFS resurrection problem",           "target_hint": "Isilon"},
    {"query": "how does domain reliability work",   "target_hint": "record_partial_success"},
    {"query": "dream sweep gap filling",            "target_hint": "dream-sweep"},
    {"query": "how to build chitta-field",          "target_hint": "1.93.0"},
]

CHITTA = os.environ.get("CHITTA_BIN", os.path.expanduser("~/.claude/bin/chitta"))
BOLD = "\033[1m"; RESET = "\033[0m"; GREEN = "\033[32m"; RED = "\033[31m"
CYAN = "\033[36m"; YELLOW = "\033[33m"; DIM = "\033[2m"


def chitta_recall(query: str, limit: int) -> list[dict]:
    env = dict(os.environ, SQZ_NO_DEDUP="1")
    out = subprocess.run(
        [CHITTA, "recall", "--query", query, "--limit", str(limit), "--json"],
        capture_output=True, text=True, timeout=120, env=env,
    )
    try:
        return json.loads(out.stdout).get("results", [])
    except Exception:
        return []


def softmax(x: np.ndarray) -> np.ndarray:
    x = x - x.max()
    e = np.exp(x)
    return e / e.sum()


def dam_update(X: np.ndarray, s: np.ndarray, beta: float, steps: int) -> tuple[np.ndarray, list[float]]:
    """T-step Modern Hopfield update. X: (dim, N) col-normalized, s: (dim,) unit vec."""
    deltas = []
    for _ in range(steps):
        weights = softmax(beta * (X.T @ s))   # (N,)
        s_new = X @ weights                    # (dim,)
        norm = np.linalg.norm(s_new)
        s_new = s_new / (norm + 1e-12)
        delta = float(np.linalg.norm(s_new - s))
        deltas.append(delta)
        s = s_new
        if delta < 1e-6:
            break
    return s, deltas


def probe_query(model, item: dict, candidates: int, steps: int) -> dict:
    query = item["query"]
    hint  = item["target_hint"]

    results = chitta_recall(query, candidates)
    if not results:
        return {"query": query, "error": "no results from chitta"}

    texts = [r["text"] for r in results]
    sims  = [r.get("similarity", r.get("relevance", 0.0)) for r in results]

    target_idx = next((i for i, t in enumerate(texts) if hint.lower() in t.lower()), None)

    all_texts  = texts + [query]
    embeddings = model.encode(all_texts, normalize_embeddings=True, show_progress_bar=False)
    X  = embeddings[:-1].T    # (dim, N)
    s0 = embeddings[-1]       # (dim,)

    per_beta = {}
    for beta in BETAS:
        s_T, deltas = dam_update(X, s0.copy(), float(beta), steps)
        scores = X.T @ s_T
        dam_idx = int(np.argmax(scores))
        per_beta[beta] = {
            "dam_winner_idx":  dam_idx,
            "dam_winner_text": texts[dam_idx][:80],
            "flip":            dam_idx != 0,
            "target_hit":      hint.lower() in texts[dam_idx].lower(),
            "n_steps":         len(deltas),
            "final_delta":     deltas[-1],
        }

    return {
        "query":        query,
        "n_candidates": len(results),
        "hnsw_top1":    texts[0][:80],
        "hnsw_top1_sim":sims[0],
        "target_hint":  hint,
        "target_rank":  target_idx,
        "per_beta":     per_beta,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--candidates", type=int, default=200, help="HNSW pool size")
    ap.add_argument("--steps",      type=int, default=10,  help="DAM relaxation steps")
    ap.add_argument("--model", default="BAAI/bge-large-en-v1.5")
    a = ap.parse_args()

    print(f"{BOLD}Loading {a.model}...{RESET}", flush=True)
    from sentence_transformers import SentenceTransformer
    model = SentenceTransformer(a.model)

    total_flips  = 0
    target_hits  = 0
    total_combos = 0

    for item in TEST_QUERIES:
        print(f"\n{BOLD}── {item['query']!r}  (target hint: {item['target_hint']}) ──{RESET}")
        r = probe_query(model, item, a.candidates, a.steps)

        if "error" in r:
            print(f"  {RED}{r['error']}{RESET}")
            continue

        target_label = f"rank {r['target_rank']}" if r["target_rank"] is not None else f"{RED}not in pool{RESET}"
        print(f"  pool={r['n_candidates']}  target in HNSW pool: {target_label}")
        print(f"  {DIM}HNSW top-1 (sim={r['hnsw_top1_sim']:.3f}): {r['hnsw_top1']}{RESET}")

        for beta, pb in r["per_beta"].items():
            flip_tag   = f"{YELLOW}FLIP   {RESET}" if pb["flip"]        else f"{DIM}same   {RESET}"
            target_tag = f" {GREEN}TARGET HIT{RESET}" if pb.get("target_hit") else ""
            print(f"  β={beta:>3}  {flip_tag}  steps={pb['n_steps']:>2}  δ={pb['final_delta']:.5f}"
                  f"  → {DIM}{pb['dam_winner_text']}{RESET}{target_tag}")
            if pb["flip"]:
                total_flips += 1
            if pb.get("target_hit"):
                target_hits += 1
            total_combos += 1

    print(f"\n{BOLD}── Falsification summary ──{RESET}")
    print(f"  associative flips : {total_flips}/{total_combos}  β×query combos")
    print(f"  target hits       : {target_hits}/{len(TEST_QUERIES) * len(BETAS)}  (DAM converged to correct memory)")

    if total_flips == 0:
        print(f"\n{RED}{BOLD}VERDICT: ZERO FLIPS — Field-RAG is cosine kNN in a costume. Kill the project.{RESET}")
        sys.exit(1)
    elif target_hits > 0:
        print(f"\n{GREEN}{BOLD}VERDICT: {target_hits} target hits — energy landscape has meaningful basins. Proceed to Rust Phase 1.{RESET}")
    else:
        print(f"\n{YELLOW}{BOLD}VERDICT: {total_flips} flips but zero target hits — landscape reshaping, not improving. "
              f"Investigate β window or candidate pool.{RESET}")


if __name__ == "__main__":
    main()
