#!/usr/bin/env python3
"""Benchmark proven open embedding models for the PUBLIC cc-soul default.

Same SSL-line -> source-passage retrieval task as eval_public_embed.py, but across several
off-the-shelf embedders loaded via sentence-transformers (correct per-model pooling/prompts).
Reports Recall@1 / MRR (raw + mean-centered, as chitta embeds) and anisotropy, so we can pick
the best quality/size tradeoff to wire into Option A as the public default.

Each model is loaded from its LOCAL cache snapshot (not the repo id) so it runs on the
offline GPU node without any HF API call. Models not cached are skipped.

    python bench_embedders.py --limit 250
"""

import argparse
import json
import os
from pathlib import Path

import numpy as np
from sentence_transformers import SentenceTransformer

HERE = Path(__file__).resolve().parent

# name, repo dirname (models--org--name), trust_remote_code, query prompt, doc prompt, dim
CANDIDATES = [
    ("nomic-768", "models--nomic-ai--nomic-embed-text-v1.5", True,
     "search_query: ", "search_document: ", 768),
    ("bge-large-1024", "models--BAAI--bge-large-en-v1.5", False,
     "Represent this sentence for searching relevant passages: ", "", 1024),
    ("qwen3-emb-0.6B-1024", "models--Qwen--Qwen3-Embedding-0.6B", True,
     "Instruct: Given a query, retrieve passages that answer it\nQuery: ", "", 1024),
    ("gte-qwen2-1.5B-1536", "models--Alibaba-NLP--gte-Qwen2-1.5B-instruct", True,
     "Instruct: Given a query, retrieve passages that answer it\nQuery: ", "", 1536),
]


def snapshot(repo_dirname):
    hub = Path(os.environ.get("HF_HOME", Path.home() / ".cache/huggingface")) / "hub"
    snaps = sorted((hub / repo_dirname / "snapshots").glob("*"))
    return str(snaps[0]) if snaps else None


def normed(x, center=False):
    if center:
        x = x - x.mean(0, keepdims=True)
    return x / (np.linalg.norm(x, axis=1, keepdims=True) + 1e-9)


def metrics(q, d):
    sims = q @ d.T
    ranks = (-sims).argsort(1)
    gold = np.arange(len(q))
    pos = np.array([np.where(ranks[i] == gold[i])[0][0] for i in range(len(q))])
    return (pos == 0).mean(), (1.0 / (pos + 1)).mean()


def anisotropy(unit):
    mu = unit.mean(0)
    n = min(len(unit), 400)
    idx = np.random.RandomState(0).choice(len(unit), n, replace=False)
    s = unit[idx] @ unit[idx].T
    return float(np.linalg.norm(mu)), float(s[~np.eye(n, dtype=bool)].mean())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", type=Path, default=HERE / "training_data_sft_synth.jsonl")
    ap.add_argument("--limit", type=int, default=250)
    args = ap.parse_args()

    rows = [json.loads(l) for l in open(args.corpus)]
    rows = rows[-args.limit:] if args.limit else rows
    passages = [r["input"] for r in rows]
    queries = [r["output"] for r in rows]
    print(f"Benchmark on {len(rows)} SSL->passage pairs\n")
    print(f"{'model':22s} {'dim':>5s}  {'R@1':>6s} {'MRR':>6s}  {'R@1c':>6s} {'MRRc':>6s}  "
          f"{'|mu|':>6s} {'unrel':>6s}")
    print("-" * 78)

    import torch
    dtype = torch.float16 if torch.cuda.is_available() else torch.float32
    for name, repo, trust, qp, dp, dim in CANDIDATES:
        path = snapshot(repo)
        if not path:
            print(f"{name:22s} {dim:5d}  SKIPPED (not cached)")
            continue
        try:
            m = SentenceTransformer(path, trust_remote_code=trust,
                                    model_kwargs={"torch_dtype": dtype})
            qv = m.encode([qp + q for q in queries], batch_size=32,
                          show_progress_bar=False, normalize_embeddings=False)
            dv = m.encode([dp + p for p in passages], batch_size=32,
                          show_progress_bar=False, normalize_embeddings=False)
            qv, dv = np.asarray(qv, np.float32), np.asarray(dv, np.float32)
            r1, mrr = metrics(normed(qv), normed(dv))
            r1c, mrrc = metrics(normed(qv, True), normed(dv, True))
            amu, acos = anisotropy(normed(dv))
            print(f"{name:22s} {dim:5d}  {r1:6.3f} {mrr:6.3f}  {r1c:6.3f} {mrrc:6.3f}  "
                  f"{amu:6.3f} {acos:6.3f}")
            del m
            if torch.cuda.is_available():
                torch.cuda.empty_cache()
        except Exception as e:
            print(f"{name:22s} {dim:5d}  ERROR {type(e).__name__}: {str(e)[:60]}")


if __name__ == "__main__":
    main()
