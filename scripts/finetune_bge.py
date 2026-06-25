#!/usr/bin/env python3
"""Fine-tune Jina-v2-base embedding model on chitta SSL→NL memory pairs.

Uses MultipleNegativesRankingLoss with in-batch negatives.  Training data
uses Jina instruction prefixes (search_query: / search_document:) so the
fine-tuned model preserves the E5/Jina retrieval convention.

After training, convert to GGUF for chitta:
  python llama.cpp/convert_hf_to_gguf.py <output-dir> \
      --outtype q8_0 --outfile ~/.claude/bin/jina-v2-base-finetuned.gguf
  # Then: edit chittad.service to point --embed-model at the new GGUF,
  # restart daemon, and re-embed all memories with: chitta reindex --all

Usage:
    python scripts/finetune_bge.py [--pairs PATH] [--output DIR] [--epochs N]

Requires: pip install sentence-transformers>=3.0 datasets
"""

import argparse
import json
import os
import sys
from pathlib import Path


def load_pairs(path: str) -> list[dict]:
    pairs = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                pairs.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return pairs


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pairs",
                        default=os.path.expanduser("~/.claude/training/ssl_nl_pairs.jsonl"))
    parser.add_argument("--output",
                        default=os.path.expanduser("~/.claude/models/jina-v2-base-finetuned"))
    parser.add_argument("--base-model", default="jinaai/jina-embeddings-v2-base-en")
    parser.add_argument("--epochs", type=int, default=3)
    parser.add_argument("--batch-size", type=int, default=8)
    parser.add_argument("--lr", type=float, default=2e-5)
    parser.add_argument("--warmup-ratio", type=float, default=0.1)
    parser.add_argument("--max-length", type=int, default=256)
    args = parser.parse_args()

    if not Path(args.pairs).exists():
        print(f"pairs not found: {args.pairs}", file=sys.stderr)
        sys.exit(1)

    pairs = load_pairs(args.pairs)
    print(f"Loaded {len(pairs)} pairs from {args.pairs}")

    try:
        from sentence_transformers import SentenceTransformer
        from sentence_transformers.losses import MultipleNegativesRankingLoss
        from sentence_transformers.training_args import SentenceTransformerTrainingArguments
        from sentence_transformers.trainer import SentenceTransformerTrainer
        from datasets import Dataset
    except ImportError as e:
        print(f"Missing dependency: {e}", file=sys.stderr)
        print("pip install 'sentence-transformers>=3.0' datasets", file=sys.stderr)
        sys.exit(1)

    # Build dataset — anchor=NL query, positive=SSL memory.
    # Add Jina instruction prefixes so fine-tuned model preserves the retrieval convention.
    anchors = ["search_query: " + p["query"] for p in pairs if p.get("query") and p.get("pos")]
    positives = ["search_document: " + p["pos"] for p in pairs if p.get("query") and p.get("pos")]
    print(f"Training examples: {len(anchors)}")

    n_eval = max(10, len(anchors) // 10)
    train_ds = Dataset.from_dict({"anchor": anchors[n_eval:], "positive": positives[n_eval:]})
    eval_ds  = Dataset.from_dict({"anchor": anchors[:n_eval], "positive": positives[:n_eval]})

    model = SentenceTransformer(args.base_model)
    model.max_seq_length = args.max_length

    loss = MultipleNegativesRankingLoss(model)

    steps_per_epoch = len(train_ds) // args.batch_size
    training_args = SentenceTransformerTrainingArguments(
        output_dir=args.output,
        num_train_epochs=args.epochs,
        per_device_train_batch_size=args.batch_size,
        per_device_eval_batch_size=args.batch_size,
        learning_rate=args.lr,
        warmup_ratio=args.warmup_ratio,
        eval_strategy="steps",
        eval_steps=max(50, steps_per_epoch // 2),
        save_strategy="epoch",
        load_best_model_at_end=False,
        fp16=True,
        logging_steps=20,
        report_to="none",
    )

    trainer = SentenceTransformerTrainer(
        model=model,
        args=training_args,
        train_dataset=train_ds,
        eval_dataset=eval_ds,
        loss=loss,
    )

    trainer.train()
    model.save_pretrained(args.output)
    print(f"\nSaved: {args.output}")
    print(f"\nConvert to GGUF for chitta:")
    print(f"  python llama.cpp/convert_hf_to_gguf.py {args.output} \\")
    print(f"      --outtype q8_0 --outfile /projects/caeg/scratch/kbd606/tmp/jina-v2-base-finetuned.gguf")
    print(f"  # Deploy:")
    print(f"  install -m 0755 /projects/caeg/scratch/kbd606/tmp/jina-v2-base-finetuned.gguf ~/.claude/bin/")
    print(f"  systemctl --user edit --force chittad  # update --embed-model path")
    print(f"  systemctl --user restart chittad")
    print(f"  chitta reindex --all  # re-embed 140k memories with new model")


if __name__ == "__main__":
    main()
