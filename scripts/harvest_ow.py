#!/usr/bin/env python3
"""CEC Phase 17 — Open-Weight LLM Material Harvester.

Extracts material from open-weight models for chitta-field seeding.
Reads harvest_targets.json produced by `chitta harvest_scope` to
target extraction — never bulk-ingests a static corpus.

Modes:
  vocab_geometry   Extract semantic direction clusters from embedding matrix E.
                   Output: HDC seed vectors (JSON array of unit f32 vectors).
  triplet_extract  Run model over domain corpus, extract (S,P,O) triplets.
                   Output: JSONL of {subject, predicate, object, confidence, source}.
  feature_dict     Run sparse autoencoder over MLP activations.
                   Output: {feature_id, label, direction, freq}.

Provenance:
  --provenance ow_distilled  (offline activations, license-clean)
  --provenance ow_generated  (generated text — always candidate band in chitta)

Usage:
  python3 harvest_ow.py \\
    --model Qwen/Qwen2.5-7B-Instruct \\
    --mode vocab_geometry \\
    --provenance ow_distilled \\
    --scope harvest_targets.json \\
    --output harvest_out.jsonl

  python3 harvest_ow.py \\
    --model meta-llama/Llama-3.2-3B-Instruct \\
    --mode triplet_extract \\
    --corpus domain_docs.txt \\
    --provenance ow_generated \\
    --scope harvest_targets.json \\
    --output triplets.jsonl \\
    --ingest  # push directly to chitta via cf_put_memory
"""

import argparse
import json
import sys
import os
from pathlib import Path


def load_scope(scope_path: str) -> dict:
    if not scope_path or not Path(scope_path).exists():
        return {"harvest_budget_items": 100, "top_router_misses": []}
    with open(scope_path) as f:
        return json.load(f)


def mode_vocab_geometry(model_name: str, scope: dict, output_path: str, budget: int) -> int:
    """Extract top semantic direction clusters from embedding matrix."""
    try:
        from transformers import AutoTokenizer, AutoModel
        import torch
        import numpy as np
    except ImportError:
        print("Error: requires transformers, torch, numpy. pip install transformers torch numpy", file=sys.stderr)
        return 1

    print(f"[harvest] loading {model_name} for vocab_geometry extraction...")
    tok = AutoTokenizer.from_pretrained(model_name)
    model = AutoModel.from_pretrained(model_name, torch_dtype=torch.float32)
    E = model.get_input_embeddings().weight.detach().numpy()  # (vocab, d)

    print(f"[harvest] embedding matrix shape: {E.shape}")

    # Normalize
    norms = np.linalg.norm(E, axis=1, keepdims=True)
    E_norm = E / (norms + 1e-9)

    # PCA to extract top-k semantic directions
    n_components = min(256, budget, E_norm.shape[0], E_norm.shape[1])
    from numpy.linalg import svd
    # Use randomized SVD on a sample for speed
    sample_size = min(8192, E_norm.shape[0])
    idx = np.random.choice(E_norm.shape[0], sample_size, replace=False)
    _, _, Vt = svd(E_norm[idx], full_matrices=False)
    directions = Vt[:n_components]  # (n_components, d)

    results = []
    for i, direction in enumerate(directions):
        # Find top tokens in this direction
        scores = E_norm @ direction
        top_idx = np.argsort(scores)[-5:][::-1]
        top_tokens = [tok.decode([int(j)], skip_special_tokens=True).strip() for j in top_idx]
        results.append({
            "feature_id": i,
            "direction": direction.tolist(),
            "top_tokens": top_tokens,
            "provenance": "ow_distilled",
            "source_model": model_name,
        })

    with open(output_path, "w") as f:
        json.dump({"mode": "vocab_geometry", "n_directions": len(results),
                   "source_model": model_name, "directions": results}, f, indent=2)
    print(f"[harvest] wrote {len(results)} semantic directions to {output_path}")
    return 0


def mode_triplet_extract(model_name: str, corpus_path: str, scope: dict,
                          output_path: str, provenance: str, budget: int) -> int:
    """Run model over corpus and extract (S,P,O) triplets."""
    try:
        from transformers import pipeline
    except ImportError:
        print("Error: requires transformers. pip install transformers", file=sys.stderr)
        return 1

    if not corpus_path or not Path(corpus_path).exists():
        print(f"Error: corpus file not found: {corpus_path}", file=sys.stderr)
        return 1

    # Build extraction prompt from scope targets
    target_patterns = [m.get("pattern", "") for m in scope.get("top_router_misses", [])]
    focus = ", ".join(target_patterns[:3]) if target_patterns else "general knowledge"

    print(f"[harvest] loading {model_name} for triplet extraction (focus: {focus})...")
    gen = pipeline("text-generation", model=model_name, max_new_tokens=256,
                   device_map="auto", trust_remote_code=True)

    corpus = Path(corpus_path).read_text()
    chunks = [corpus[i:i+800] for i in range(0, min(len(corpus), budget * 40), 800)]

    results = []
    for i, chunk in enumerate(chunks[:budget]):
        prompt = (
            f"Extract factual (subject, predicate, object) triplets from this text. "
            f"Focus on: {focus}. Output one triplet per line as JSON.\n\n"
            f"Text: {chunk}\n\nTriplets:"
        )
        out = gen(prompt, do_sample=False)[0]["generated_text"]
        generated = out[len(prompt):]
        for line in generated.strip().splitlines():
            line = line.strip()
            if not line:
                continue
            try:
                t = json.loads(line)
                if all(k in t for k in ("subject", "predicate", "object")):
                    t["provenance"] = provenance
                    t["source_model"] = model_name
                    t.setdefault("confidence", 0.6)
                    results.append(t)
            except json.JSONDecodeError:
                pass
        if i % 10 == 0:
            print(f"[harvest] processed {i+1}/{len(chunks[:budget])} chunks, {len(results)} triplets")

    with open(output_path, "w") as f:
        for r in results:
            f.write(json.dumps(r) + "\n")
    print(f"[harvest] wrote {len(results)} triplets to {output_path}")
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--model", required=True, help="HuggingFace model name or path")
    p.add_argument("--mode", required=True, choices=["vocab_geometry", "triplet_extract", "feature_dict"])
    p.add_argument("--provenance", default="ow_distilled", choices=["ow_distilled", "ow_generated"])
    p.add_argument("--scope", default="harvest_targets.json", help="harvest_targets.json from `chitta harvest_scope`")
    p.add_argument("--corpus", default="", help="(triplet_extract) path to domain text corpus")
    p.add_argument("--output", default="harvest_out.jsonl", help="output file path")
    p.add_argument("--ingest", action="store_true", help="push output directly to chitta (requires chitta CLI)")
    args = p.parse_args()

    scope = load_scope(args.scope)
    budget = scope.get("harvest_budget_items", 100)
    print(f"[harvest] scope: diagnosis={scope.get('turiya_diagnosis','unknown')} budget={budget}")

    if args.mode == "vocab_geometry":
        rc = mode_vocab_geometry(args.model, scope, args.output, budget)
    elif args.mode == "triplet_extract":
        rc = mode_triplet_extract(args.model, args.corpus, scope, args.output, args.provenance, budget)
    elif args.mode == "feature_dict":
        print("feature_dict mode requires a trained sparse autoencoder — not yet implemented.", file=sys.stderr)
        rc = 1
    else:
        rc = 1

    if rc == 0 and args.ingest:
        print(f"[harvest] ingesting {args.output} into chitta (provenance={args.provenance})...")
        chitta_bin = os.path.expanduser("~/.claude/bin/chitta")
        with open(args.output) as f:
            content = f.read()
        os.system(
            f"{chitta_bin} remember "
            f"--kind ow_harvest --realm cec:harvest "
            f"--content '{content[:4000]}' "
            f"--provenance {args.provenance}"
        )

    return rc


if __name__ == "__main__":
    sys.exit(main())
