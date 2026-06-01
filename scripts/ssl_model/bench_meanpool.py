#!/usr/bin/env python3
"""Chitta-accurate embedder benchmark: MEAN pooling for every candidate.

chitta's llama embedder hardcodes LLAMA_POOLING_TYPE_MEAN (vak_llama.hpp), so a model's
*native* pooling (bge=CLS, Qwen3-Embedding=last-token) is NOT what chitta realizes. This
re-runs the SSL->passage task with masked-mean pooling for all models (matching chitta) so
the ranking reflects production reality. Compare against bench_embedders.py (native pooling)
to see how much each model depends on a pooling chitta would have to be taught to use.
"""

import argparse
import json
import os
from pathlib import Path

import numpy as np
import torch
from transformers import AutoModel, AutoTokenizer

HERE = Path(__file__).resolve().parent
# name, repo dirname, trust_remote_code, query prefix, doc prefix
CANDIDATES = [
    ("nomic-768", "models--nomic-ai--nomic-embed-text-v1.5", True,
     "search_query: ", "search_document: "),
    ("bge-large-1024", "models--BAAI--bge-large-en-v1.5", False,
     "Represent this sentence for searching relevant passages: ", ""),
    ("qwen3-emb-0.6B-1024", "models--Qwen--Qwen3-Embedding-0.6B", True,
     "Instruct: Given a query, retrieve passages that answer it\nQuery: ", ""),
]


def snapshot(repo):
    hub = Path(os.environ.get("HF_HOME", Path.home() / ".cache/huggingface")) / "hub"
    s = sorted((hub / repo / "snapshots").glob("*"))
    return str(s[0]) if s else None


def normed(x, center=False):
    if center:
        x = x - x.mean(0, keepdims=True)
    return x / (np.linalg.norm(x, axis=1, keepdims=True) + 1e-9)


def metrics(q, d):
    sims = q @ d.T
    ranks = (-sims).argsort(1)
    g = np.arange(len(q))
    pos = np.array([np.where(ranks[i] == g[i])[0][0] for i in range(len(q))])
    # R@1, R@3, R@5, R@10, MRR
    return ((pos == 0).mean(), (pos < 3).mean(), (pos < 5).mean(),
            (pos < 10).mean(), (1.0 / (pos + 1)).mean())


def aniso(u):
    mu = u.mean(0)
    n = min(len(u), 400)
    idx = np.random.RandomState(0).choice(len(u), n, replace=False)
    s = u[idx] @ u[idx].T
    return float(np.linalg.norm(mu)), float(s[~np.eye(n, dtype=bool)].mean())


@torch.no_grad()
def mean_embed(texts, model, tok, device, prefix, bs=16):
    out = []
    for i in range(0, len(texts), bs):
        enc = tok([prefix + t for t in texts[i:i + bs]], padding=True, truncation=True,
                  max_length=512, return_tensors="pt").to(device)
        h = model(**enc).last_hidden_state
        m = enc["attention_mask"].unsqueeze(-1).float()
        out.append(((h * m).sum(1) / m.sum(1).clamp(min=1e-9)).float().cpu().numpy())
    return np.concatenate(out, 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", type=Path, default=HERE / "training_data_sft_synth.jsonl")
    ap.add_argument("--limit", type=int, default=250)
    args = ap.parse_args()
    rows = [json.loads(l) for l in open(args.corpus)][-args.limit:]
    passages = [r["input"] for r in rows]
    queries = [r["output"] for r in rows]
    print(f"MEAN-pooled (chitta-accurate), centered, on {len(rows)} pairs "
          f"(1 gold among {len(rows)} distractors)\n")
    print(f"{'model':22s}  {'R@1':>5s} {'R@3':>5s} {'R@5':>5s} {'R@10':>5s} {'MRR':>5s}  {'|mu|':>6s}")
    print("-" * 64)
    device = "cuda" if torch.cuda.is_available() else "cpu"
    dtype = torch.float16 if device == "cuda" else torch.float32
    for name, repo, trust, qp, dp in CANDIDATES:
        path = snapshot(repo)
        if not path:
            print(f"{name:22s}  SKIPPED (not cached)"); continue
        try:
            tok = AutoTokenizer.from_pretrained(path, trust_remote_code=trust)
            model = AutoModel.from_pretrained(path, trust_remote_code=trust,
                                              torch_dtype=dtype).to(device).eval()
            qv = mean_embed(queries, model, tok, device, qp)
            dv = mean_embed(passages, model, tok, device, dp)
            r1, r3, r5, r10, mrr = metrics(normed(qv, True), normed(dv, True))
            amu, _ = aniso(normed(dv))
            print(f"{name:22s}  {r1:5.3f} {r3:5.3f} {r5:5.3f} {r10:5.3f} {mrr:5.3f}  {amu:6.3f}")
            del model
            if device == "cuda":
                torch.cuda.empty_cache()
        except Exception as e:
            print(f"{name:22s}  ERROR {type(e).__name__}: {str(e)[:55]}")


if __name__ == "__main__":
    main()
