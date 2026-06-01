#!/usr/bin/env python3
"""Quick offline embedding-quality check for the public ssl_distiller candidate.

Task: SSL-line → source-passage retrieval over the synthetic corpus. For each held-out
example the SSL distillation is the query and its passage is the gold document; all other
passages are distractors. Reports Recall@1 / MRR plus anisotropy (|mean unit vector|) and
mean unrelated-pair cosine — the discriminability signal that motivated mean-centering.

Compares three embedders on the SAME task so the verdict is apples-to-apples:
  * candidate  — base Qwen2.5-1.5B + the trained LoRA adapter (mean-pooled hidden states)
  * base       — base Qwen2.5-1.5B, untrained (did the fine-tune help?)
  * nomic      — nomic-embed-text-v1.5 (the public deployment baseline to beat)

This is a SANITY signal on a small corpus, not a generalization verdict (the 300-example
proof run trains on all of it). Run on a GPU node:
    python eval_public_embed.py --adapter ssl_lora_public_adapter --limit 120
"""

import argparse
import json
from pathlib import Path

import numpy as np
import torch
from transformers import AutoModel, AutoModelForCausalLM, AutoTokenizer

HERE = Path(__file__).resolve().parent


def cached_snapshot(repo_dirname: str) -> str:
    import os
    hub = Path(os.environ.get("HF_HOME", Path.home() / ".cache/huggingface")) / "hub"
    snaps = sorted((hub / repo_dirname / "snapshots").glob("*"))
    if not snaps:
        raise SystemExit(f"{repo_dirname} not cached under {hub}")
    return str(snaps[0])


def mean_pool(last_hidden, mask):
    m = mask.unsqueeze(-1).float()
    return (last_hidden * m).sum(1) / m.sum(1).clamp(min=1e-9)


@torch.no_grad()
def embed_causal(texts, model, tok, device, prefix="", bs=16):
    out = []
    for i in range(0, len(texts), bs):
        batch = [prefix + t for t in texts[i:i + bs]]
        enc = tok(batch, padding=True, truncation=True, max_length=512,
                  return_tensors="pt").to(device)
        h = model(**enc, output_hidden_states=True).hidden_states[-1]
        out.append(mean_pool(h, enc["attention_mask"]).float().cpu().numpy())
    return np.concatenate(out, 0)


@torch.no_grad()
def embed_nomic(texts, model, tok, device, prefix, bs=32):
    out = []
    for i in range(0, len(texts), bs):
        batch = [prefix + t for t in texts[i:i + bs]]
        enc = tok(batch, padding=True, truncation=True, max_length=512,
                  return_tensors="pt").to(device)
        h = model(**enc).last_hidden_state
        out.append(mean_pool(h, enc["attention_mask"]).float().cpu().numpy())
    return np.concatenate(out, 0)


def normed(x, center=False):
    if center:
        x = x - x.mean(0, keepdims=True)
    return x / (np.linalg.norm(x, axis=1, keepdims=True) + 1e-9)


def retrieval_metrics(q, d):
    # q[i] should retrieve d[i]; sims[i,j] = cos(q_i, d_j)
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
    off = s[~np.eye(n, dtype=bool)]
    return float(np.linalg.norm(mu)), float(off.mean())


def report(name, qv, dv):
    for center in (False, True):
        q, d = normed(qv, center), normed(dv, center)
        r1, mrr = retrieval_metrics(q, d)
        amu, acos = anisotropy(d)
        tag = "centered" if center else "raw     "
        print(f"  {name:10s} [{tag}]  R@1={r1:.3f}  MRR={mrr:.3f}  "
              f"|mu|={amu:.3f}  unrel_cos={acos:.3f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", type=Path, default=HERE / "training_data_sft_synth.jsonl")
    ap.add_argument("--adapter", default="ssl_lora_public_adapter")
    ap.add_argument("--limit", type=int, default=120)
    args = ap.parse_args()

    rows = [json.loads(l) for l in open(args.corpus)]
    rows = rows[-args.limit:] if args.limit else rows
    passages = [r["input"] for r in rows]
    queries = [r["output"] for r in rows]
    print(f"Eval on {len(rows)} (SSL→passage) pairs\n")

    device = "cuda" if torch.cuda.is_available() else "cpu"
    qwen = cached_snapshot("models--Qwen--Qwen2.5-1.5B-Instruct")
    tok = AutoTokenizer.from_pretrained(qwen)
    if tok.pad_token is None:
        tok.pad_token = tok.eos_token

    base = AutoModelForCausalLM.from_pretrained(qwen, torch_dtype=torch.float16).to(device).eval()
    print("== base Qwen ==")
    report("base", embed_causal(queries, base, tok, device, "search_query: "),
           embed_causal(passages, base, tok, device, "search_document: "))

    from peft import PeftModel
    cand = PeftModel.from_pretrained(base, args.adapter).merge_and_unload()
    print("== candidate (LoRA) ==")
    report("candidate", embed_causal(queries, cand, tok, device, "search_query: "),
           embed_causal(passages, cand, tok, device, "search_document: "))
    del base, cand
    if device == "cuda":
        torch.cuda.empty_cache()

    try:
        nmodel = cached_snapshot("models--nomic-ai--nomic-embed-text-v1.5")
        ntok = AutoTokenizer.from_pretrained(nmodel)
        nm = AutoModel.from_pretrained(nmodel, trust_remote_code=True,
                                       torch_dtype=torch.float16).to(device).eval()
        print("== nomic-embed-text-v1.5 ==")
        report("nomic", embed_nomic(queries, nm, ntok, device, "search_query: "),
               embed_nomic(passages, nm, ntok, device, "search_document: "))
    except SystemExit:
        print("== nomic == SKIPPED (not cached; run: huggingface-cli download "
              "nomic-ai/nomic-embed-text-v1.5)")


if __name__ == "__main__":
    main()
