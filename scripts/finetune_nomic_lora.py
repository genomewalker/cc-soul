#!/usr/bin/env python3
"""#14 gate (c): LoRA fine-tune of nomic-embed-text-v1.5 on mined memory pairs.

Input pairs from scripts/mine_training_pairs.py ({"query","pos","tier",
"realm_a","realm_b"}). Prefixes are added HERE: anchor gets "search_query: ",
positive gets "search_document: " — the asymmetric convention the daemon uses
at recall time. Loss is MultipleNegativesRankingLoss (in-batch negatives); the
mixed-realm batch sampler below is the anti-collapse guard: every batch is
drawn round-robin across realms so "same realm" can never masquerade as
"relevant" (the Jina domain-bias failure).

Model selection: recall@10 on the held-out split after each epoch; keep the
best adapter. The 30 golden queries are NEVER seen here — they stay a pure
frozen test via hooks/grade-recall.sh.

After training:
  python scripts/finetune_nomic_lora.py --merge-only ...   # merge LoRA -> HF
  python llama.cpp/convert_hf_to_gguf.py <merged-dir> --outtype q8_0 \
      --outfile nomic-embed-text-v1.5-ft.gguf               # match prod quant
  python scripts/domain_bias_probe.py --model <merged-dir>  # KILL-GATE
  # gguf parity: scripts/domain_bias_probe.py --parity <gguf>

Requires: pip install 'sentence-transformers>=3.0' peft datasets
"""

import argparse
import collections
import json
import os
import random
from pathlib import Path


def load_pairs(path: Path) -> list[dict]:
    return [json.loads(l) for l in open(path) if l.strip()]


class MixedRealmBatches:
    """Yield index batches drawn round-robin across realms (realm_a of the
    anchor). Guarantees every batch mixes >= min(realms, batch) realms, so
    in-batch negatives always contain same-realm-but-irrelevant docs."""

    def __init__(self, pairs: list[dict], batch: int, seed: int = 42):
        self.batch = batch
        by_realm = collections.defaultdict(list)
        for i, p in enumerate(pairs):
            by_realm[p["realm_a"]].append(i)
        self.rng = random.Random(seed)
        self.queues = list(by_realm.values())

    def __iter__(self):
        queues = [q[:] for q in self.queues]
        for q in queues:
            self.rng.shuffle(q)
        self.rng.shuffle(queues)
        batch = []
        while queues:
            for q in queues[:]:
                if not q:
                    queues.remove(q)
                    continue
                batch.append(q.pop())
                if len(batch) == self.batch:
                    yield batch
                    batch = []
        if batch:
            yield batch


def recall_at_k(model, heldout: list[dict], k: int = 10) -> float:
    """Held-out model selection: each anchor must find its positive among all
    held-out positives (282 docs — small but split-clean)."""
    import numpy as np
    q = model.encode(["search_query: " + p["query"] for p in heldout],
                     normalize_embeddings=True, batch_size=64)
    d = model.encode(["search_document: " + p["pos"] for p in heldout],
                     normalize_embeddings=True, batch_size=64)
    sims = q @ d.T
    ranks = (-sims).argsort(axis=1)
    hits = sum(1 for i in range(len(heldout)) if i in ranks[i, :k])
    return hits / len(heldout)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--train", type=Path, required=True)
    ap.add_argument("--heldout", type=Path, required=True)
    ap.add_argument("--output", type=Path,
                    default=Path(os.path.expanduser("~/.claude/models/nomic-ft")))
    ap.add_argument("--base-model", default="nomic-ai/nomic-embed-text-v1.5")
    ap.add_argument("--epochs", type=int, default=2)
    ap.add_argument("--batch-size", type=int, default=64)
    ap.add_argument("--lr", type=float, default=8e-5)
    ap.add_argument("--lora-r", type=int, default=16)
    ap.add_argument("--lora-alpha", type=int, default=32)
    ap.add_argument("--max-length", type=int, default=256)
    ap.add_argument("--merge-only", action="store_true",
                    help="merge an existing adapter in --output into a full "
                         "HF checkpoint at <output>-merged (for GGUF export)")
    args = ap.parse_args()

    from sentence_transformers import SentenceTransformer
    import torch

    if args.merge_only:
        model = SentenceTransformer(str(args.output), trust_remote_code=True)
        auto = model[0].auto_model
        if hasattr(auto, "merge_and_unload"):
            model[0].auto_model = auto.merge_and_unload()
        merged = str(args.output) + "-merged"
        model.save(merged)
        print(f"merged -> {merged}")
        return

    train = load_pairs(args.train)
    heldout = load_pairs(args.heldout)
    print(f"train={len(train)} heldout={len(heldout)}")

    model = SentenceTransformer(args.base_model, trust_remote_code=True)
    model.max_seq_length = args.max_length

    from peft import LoraConfig, get_peft_model
    # NomicBert module names — verify against the checkpoint before the run:
    #   python -c "... print([n for n,_ in model.named_modules()])"
    lora = LoraConfig(r=args.lora_r, lora_alpha=args.lora_alpha,
                      lora_dropout=0.05,
                      target_modules=["Wqkv", "out_proj", "fc1", "fc2"])
    model[0].auto_model = get_peft_model(model[0].auto_model, lora)
    model[0].auto_model.print_trainable_parameters()

    from sentence_transformers.losses import MultipleNegativesRankingLoss
    loss_fn = MultipleNegativesRankingLoss(model)
    opt = torch.optim.AdamW(
        [p for p in model.parameters() if p.requires_grad], lr=args.lr)
    device = "cuda" if torch.cuda.is_available() else "cpu"
    model.to(device)

    sampler = MixedRealmBatches(train, args.batch_size)
    steps_per_epoch = (len(train) + args.batch_size - 1) // args.batch_size
    total = steps_per_epoch * args.epochs
    sched = torch.optim.lr_scheduler.OneCycleLR(
        opt, max_lr=args.lr, total_steps=total, pct_start=0.1)

    base_r10 = recall_at_k(model, heldout)
    print(f"heldout recall@10 before: {base_r10:.4f}")

    best = -1.0
    step = 0
    for epoch in range(args.epochs):
        model.train()
        for batch_idx in sampler:
            anchors = ["search_query: " + train[i]["query"] for i in batch_idx]
            docs = ["search_document: " + train[i]["pos"] for i in batch_idx]
            feats = [model.tokenize(anchors), model.tokenize(docs)]
            feats = [{k: v.to(device) for k, v in f.items()} for f in feats]
            loss = loss_fn(feats, None)
            loss.backward()
            opt.step()
            sched.step()
            opt.zero_grad()
            step += 1
            if step % 50 == 0:
                print(f"epoch {epoch} step {step}/{total} loss {loss.item():.4f}")
        model.eval()
        r10 = recall_at_k(model, heldout)
        print(f"epoch {epoch} heldout recall@10: {r10:.4f}")
        if r10 > best:
            best = r10
            model.save(str(args.output))
            print(f"saved best -> {args.output}")

    print(f"done. best heldout recall@10 {best:.4f} (baseline {base_r10:.4f})")


if __name__ == "__main__":
    main()
