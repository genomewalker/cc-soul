#!/usr/bin/env python3
"""Fine-tune BGE-small embedding model on chitta memory pairs.

Usage:
    python scripts/finetune_bge.py [--pairs PATH] [--output DIR] [--epochs N]

Requires: pip install sentence-transformers datasets
"""

import argparse
import json
import os
import sys
from pathlib import Path


def load_pairs(path: str) -> list[dict]:
    """Load query-passage pairs from JSONL."""
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
    parser = argparse.ArgumentParser(description="Fine-tune BGE-small on chitta pairs")
    parser.add_argument("--pairs", default=os.path.expanduser("~/.claude/training/pairs.jsonl"),
                        help="Path to training pairs JSONL")
    parser.add_argument("--output", default=os.path.expanduser("~/.claude/models/bge-finetuned"),
                        help="Output directory for fine-tuned model")
    parser.add_argument("--base-model", default="BAAI/bge-small-en-v1.5",
                        help="Base model to fine-tune")
    parser.add_argument("--epochs", type=int, default=3, help="Training epochs")
    parser.add_argument("--batch-size", type=int, default=16, help="Batch size")
    parser.add_argument("--lr", type=float, default=2e-5, help="Learning rate")
    parser.add_argument("--warmup-ratio", type=float, default=0.1, help="Warmup ratio")
    parser.add_argument("--max-length", type=int, default=512, help="Max sequence length")
    args = parser.parse_args()

    if not Path(args.pairs).exists():
        print(f"Error: pairs file not found: {args.pairs}", file=sys.stderr)
        print("Run 'chitta export_training_pairs' first to generate training data.", file=sys.stderr)
        sys.exit(1)

    pairs = load_pairs(args.pairs)
    if len(pairs) < 10:
        print(f"Error: only {len(pairs)} pairs found. Need at least 10 for training.", file=sys.stderr)
        sys.exit(1)

    print(f"Loaded {len(pairs)} training pairs from {args.pairs}")

    try:
        from sentence_transformers import SentenceTransformer, InputExample, losses
        from sentence_transformers.evaluation import InformationRetrievalEvaluator
        from torch.utils.data import DataLoader
    except ImportError:
        print("Error: sentence-transformers not installed.", file=sys.stderr)
        print("Install with: pip install sentence-transformers", file=sys.stderr)
        sys.exit(1)

    # Build training examples
    train_examples = []
    has_negatives = any("neg" in p for p in pairs)

    for pair in pairs:
        query = pair.get("query", "")
        pos = pair.get("pos", "")
        neg = pair.get("neg", "")

        if not query or not pos:
            continue

        if has_negatives and neg:
            train_examples.append(InputExample(texts=[query, pos, neg]))
        else:
            train_examples.append(InputExample(texts=[query, pos]))

    print(f"Built {len(train_examples)} training examples "
          f"({'triplet' if has_negatives else 'pair'} mode)")

    # Load model
    print(f"Loading base model: {args.base_model}")
    model = SentenceTransformer(args.base_model)
    model.max_seq_length = args.max_length

    # DataLoader
    train_dataloader = DataLoader(train_examples, shuffle=True, batch_size=args.batch_size)

    # Loss function
    if has_negatives:
        train_loss = losses.TripletLoss(model=model)
    else:
        train_loss = losses.MultipleNegativesRankingLoss(model=model)

    # Build eval set (10% held out)
    eval_size = max(1, len(pairs) // 10)
    eval_pairs = pairs[:eval_size]

    queries = {str(i): p["query"] for i, p in enumerate(eval_pairs) if p.get("query")}
    corpus = {str(i): p["pos"] for i, p in enumerate(eval_pairs) if p.get("pos")}
    relevant_docs = {str(i): {str(i)} for i in range(len(eval_pairs))
                     if eval_pairs[i].get("query") and eval_pairs[i].get("pos")}

    evaluator = None
    if len(queries) >= 5:
        evaluator = InformationRetrievalEvaluator(
            queries=queries,
            corpus=corpus,
            relevant_docs=relevant_docs,
            name="chitta-eval",
        )

    # Train
    warmup_steps = int(len(train_dataloader) * args.epochs * args.warmup_ratio)
    print(f"Training for {args.epochs} epochs, {warmup_steps} warmup steps")

    os.makedirs(args.output, exist_ok=True)

    model.fit(
        train_objectives=[(train_dataloader, train_loss)],
        epochs=args.epochs,
        warmup_steps=warmup_steps,
        evaluator=evaluator,
        evaluation_steps=max(100, len(train_dataloader) // 2),
        output_path=args.output,
        optimizer_params={"lr": args.lr},
    )

    print(f"\nFine-tuned model saved to: {args.output}")
    print(f"To use with chitta, convert to ONNX:")
    print(f"  python -m optimum.exporters.onnx --model {args.output} {args.output}/onnx/")
    print(f"Then point chitta's ONNX embedder to {args.output}/onnx/model.onnx")


if __name__ == "__main__":
    main()
