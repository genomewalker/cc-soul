#!/usr/bin/env python3
"""CEC Phase 17 — Open-Weight LLM Material Harvester.

Extracts material from open-weight models for chitta-field seeding.
Reads harvest_targets.json produced by `chitta harvest_scope` to
target extraction — never bulk-ingests a static corpus.

Modes:
  vocab_geometry   Extract semantic direction clusters from embedding matrix E.
                   No forward passes; CPU-only. Fast (~5 min for 7B).
                   Output: JSON {mode, n_directions, source_model, directions:[]}

  triplet_extract  Run model over domain corpus, extract (S,P,O) triplets via
                   structured prompting. Uses chat template when available.
                   Output: JSONL of {subject, predicate, object, confidence, source}.

  feature_dict     Extract sparse feature directions from MLP residual stream.
                   Hooks the middle-late transformer layer, collects activations
                   over a probe corpus, then runs sklearn DictionaryLearning.
                   No GPU required (fp16 on CPU; ~14 GB RAM for 7B models).
                   Output: JSON {mode, n_features, source_model, layer, features:[]}

Provenance:
  --provenance ow_distilled  (offline activations/geometry — license-clean)
  --provenance ow_generated  (generated text — always candidate band in chitta)

Usage:
  python3 harvest_ow.py \\
    --model Qwen/Qwen2.5-7B-Instruct \\
    --mode vocab_geometry \\
    --provenance ow_distilled \\
    --scope harvest_targets.json \\
    --output harvest_out.json

  python3 harvest_ow.py \\
    --model mistralai/Mistral-7B-v0.3 \\
    --mode triplet_extract \\
    --corpus domain_docs.txt \\
    --provenance ow_generated \\
    --scope harvest_targets.json \\
    --output triplets.jsonl \\
    --ingest

  python3 harvest_ow.py \\
    --model google/gemma-3-4b \\
    --mode feature_dict \\
    --corpus probe_corpus.txt \\
    --provenance ow_distilled \\
    --n-features 128 \\
    --output features.json
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


def _import_stack(extras: list[str] = []) -> tuple:
    """Import torch + numpy + transformers, die with helpful message on failure."""
    try:
        import torch
        import numpy as np
        from transformers import AutoTokenizer, AutoModel
        mods = {"torch": torch, "np": np, "AutoTokenizer": AutoTokenizer, "AutoModel": AutoModel}
        for extra in extras:
            mod_name = extra.split(".")[-1]
            parts = extra.split(".")
            m = __import__(parts[0])
            for p in parts[1:]:
                m = getattr(m, p)
            mods[mod_name] = m
        return mods
    except ImportError as e:
        print(f"Error: {e}. pip install transformers torch numpy scikit-learn", file=sys.stderr)
        sys.exit(1)


# ── Mode: vocab_geometry ──────────────────────────────────────────────────────

def mode_vocab_geometry(model_name: str, scope: dict, output_path: str, budget: int) -> int:
    """Extract top semantic direction clusters from embedding matrix E.

    No forward passes — only reads the embedding weight matrix, normalises it,
    runs randomised SVD on a 8192-token sample, and finds top-5 tokens per
    principal direction. Pure numpy; no GPU needed.
    """
    m = _import_stack()
    torch, np = m["torch"], m["np"]
    AutoTokenizer, AutoModel = m["AutoTokenizer"], m["AutoModel"]

    print(f"[harvest] loading {model_name} for vocab_geometry ...", flush=True)
    tok = AutoTokenizer.from_pretrained(model_name)
    model = AutoModel.from_pretrained(model_name, torch_dtype=torch.float16)
    E = model.get_input_embeddings().weight.detach().float().numpy()  # (vocab, d)
    del model
    print(f"[harvest] embedding matrix shape: {E.shape}", flush=True)

    norms = np.linalg.norm(E, axis=1, keepdims=True)
    E_norm = E / (norms + 1e-9)

    n_components = min(256, budget, E_norm.shape[0], E_norm.shape[1])
    sample_size = min(8192, E_norm.shape[0])
    rng = np.random.default_rng(42)
    idx = rng.choice(E_norm.shape[0], sample_size, replace=False)
    _, _, Vt = np.linalg.svd(E_norm[idx], full_matrices=False)
    directions = Vt[:n_components]

    def _decode_token(tid: int) -> str:
        try:
            return tok.decode([tid], skip_special_tokens=True).strip()
        except Exception:
            return ""

    results = []
    for i, direction in enumerate(directions):
        scores = E_norm @ direction
        top_idx = np.argsort(scores)[-8:][::-1]
        top_tokens = [t for j in top_idx if (t := _decode_token(int(j)))][:5]
        results.append({
            "feature_id": i,
            "direction": direction.tolist(),
            "top_tokens": top_tokens,
            "provenance": "ow_distilled",
            "source_model": model_name,
        })
        if (i + 1) % 64 == 0:
            print(f"[harvest] {i+1}/{n_components} directions processed", flush=True)

    with open(output_path, "w") as f:
        json.dump({
            "mode": "vocab_geometry",
            "n_directions": len(results),
            "source_model": model_name,
            "embed_dim": int(E.shape[1]),
            "vocab_size": int(E.shape[0]),
            "directions": results,
        }, f, indent=2)
    print(f"[harvest] wrote {len(results)} semantic directions → {output_path}", flush=True)
    return 0


# ── Mode: triplet_extract ─────────────────────────────────────────────────────

_TRIPLET_SYSTEM = (
    "You are a precise knowledge-extraction assistant. "
    "Extract factual (subject, predicate, object) triplets from the provided text. "
    "Output exactly one JSON object per line, keys: subject, predicate, object. "
    "Only output triplets — no prose, no markdown fences."
)

_TRIPLET_USER_TMPL = (
    "Focus on: {focus}\n\nText:\n{chunk}\n\nTriplets:"
)


def _apply_chat_template(tok, system: str, user: str) -> str:
    """Use the tokenizer's chat template if available; otherwise plain prompt."""
    if hasattr(tok, "apply_chat_template") and tok.chat_template:
        msgs = [{"role": "system", "content": system},
                {"role": "user", "content": user}]
        try:
            return tok.apply_chat_template(msgs, tokenize=False, add_generation_prompt=True)
        except Exception:
            pass
    return f"<|system|>\n{system}\n<|user|>\n{user}\n<|assistant|>\n"


def mode_triplet_extract(model_name: str, corpus_path: str, scope: dict,
                         output_path: str, provenance: str, budget: int) -> int:
    """Generate (S,P,O) triplets from a domain corpus via structured prompting."""
    try:
        from transformers import AutoTokenizer, AutoModelForCausalLM, pipeline
        import torch
    except ImportError:
        print("Error: requires transformers torch. pip install transformers torch", file=sys.stderr)
        return 1

    if not corpus_path or not Path(corpus_path).exists():
        print(f"Error: corpus not found: {corpus_path}", file=sys.stderr)
        return 1

    target_patterns = [m.get("pattern", "") for m in scope.get("top_router_misses", [])]
    focus = ", ".join(target_patterns[:5]) if target_patterns else "general factual knowledge"

    print(f"[harvest] loading {model_name} for triplet_extract (focus: {focus}) ...", flush=True)
    tok = AutoTokenizer.from_pretrained(model_name)
    gen = pipeline(
        "text-generation", model=model_name,
        max_new_tokens=384, do_sample=False,
        device_map="auto", torch_dtype=torch.float16,
        trust_remote_code=True,
    )

    corpus = Path(corpus_path).read_text(encoding="utf-8", errors="replace")
    chunk_size = 600
    chunks = [corpus[i:i+chunk_size].strip()
              for i in range(0, len(corpus), chunk_size)
              if corpus[i:i+chunk_size].strip()]

    results = []
    for i, chunk in enumerate(chunks[:budget]):
        user_msg = _TRIPLET_USER_TMPL.format(focus=focus, chunk=chunk)
        prompt = _apply_chat_template(tok, _TRIPLET_SYSTEM, user_msg)
        try:
            out = gen(prompt, return_full_text=False)[0]["generated_text"]
        except Exception as e:
            print(f"[harvest] chunk {i} generation error: {e}", flush=True)
            continue
        for line in out.strip().splitlines():
            line = line.strip().lstrip("- ").strip()
            if not line or not line.startswith("{"):
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
        if (i + 1) % 10 == 0:
            print(f"[harvest] {i+1}/{min(len(chunks), budget)} chunks, {len(results)} triplets", flush=True)

    with open(output_path, "w") as f:
        for r in results:
            f.write(json.dumps(r) + "\n")
    print(f"[harvest] wrote {len(results)} triplets → {output_path}", flush=True)
    return 0


# ── Mode: feature_dict ────────────────────────────────────────────────────────

def _probe_sentences(scope: dict) -> list[str]:
    """Build a diverse probe corpus from scope targets + hardcoded seeds."""
    seeds = [
        "The function returns an error when the input is invalid.",
        "Memory is allocated on the heap and must be freed explicitly.",
        "The algorithm runs in O(n log n) time and O(n) space.",
        "The server failed to connect after three retries.",
        "Editing the file caused a downstream compilation failure.",
        "The model was trained on a filtered subset of the corpus.",
        "The causal chain starts with the user action and ends with the tool outcome.",
        "The session boundary was not properly handled by the rollback logic.",
        "Embeddings encode semantic similarity as cosine distance.",
        "The snapshot was written atomically via a temporary file and rename.",
        "Provenance tracks which model or human produced each memory.",
        "The reconciler removes edges that violate the legality matrix.",
        "High surprisal events are retained verbatim in the event tape.",
        "The hypothesis market ranks uncertain rules by expected information gain.",
        "Q-values are updated via temporal-difference learning across sessions.",
    ]
    for miss in scope.get("top_router_misses", [])[:10]:
        p = miss.get("pattern", "")
        if p:
            seeds.append(f"The operation {p} failed unexpectedly.")
    return seeds


def mode_feature_dict(model_name: str, corpus_path: str, scope: dict,
                      output_path: str, n_features: int, budget: int) -> int:
    """Extract sparse feature directions from MLP residual stream activations.

    Algorithm:
    1. Load model in fp16 on CPU (no GPU required, ~14 GB for 7B).
    2. Register a forward hook on the middle-late transformer block's MLP output.
    3. Tokenise a probe corpus (from --corpus or scope seeds) and run forward
       passes in small batches, collecting per-token activation vectors.
    4. Normalise and run sklearn DictionaryLearning (sparse coding / ICA-like)
       to find n_features directions.
    5. Label each feature by projecting the embedding matrix and finding the
       top tokens that align with the direction.
    """
    m = _import_stack()
    torch, np = m["torch"], m["np"]
    AutoTokenizer, AutoModel = m["AutoTokenizer"], m["AutoModel"]

    try:
        from sklearn.decomposition import DictionaryLearning
    except ImportError:
        print("Error: requires scikit-learn. pip install scikit-learn", file=sys.stderr)
        return 1

    print(f"[harvest] loading {model_name} for feature_dict (fp16, CPU) ...", flush=True)
    tok = AutoTokenizer.from_pretrained(model_name)
    model = AutoModel.from_pretrained(model_name, torch_dtype=torch.float16)
    model.eval()

    # Determine which layer to hook: 75% depth
    layers = None
    for attr in ("layers", "h", "blocks"):
        layers = getattr(getattr(model, "model", model), attr, None)
        if layers is not None:
            break
    if layers is None:
        print("Error: cannot find transformer layer list for this model architecture.",
              file=sys.stderr)
        return 1

    n_layers = len(layers)
    hook_layer_idx = int(n_layers * 0.75)
    hook_layer = layers[hook_layer_idx]
    print(f"[harvest] hooking layer {hook_layer_idx}/{n_layers-1} (MLP output)", flush=True)

    activations: list = []

    def _hook(module, input, output):
        # output may be a tuple; grab first tensor
        t = output[0] if isinstance(output, tuple) else output
        # t: (batch, seq_len, hidden) — collect all token positions
        activations.append(t.detach().float().cpu().numpy().reshape(-1, t.shape[-1]))

    handle = hook_layer.register_forward_hook(_hook)

    # Build probe corpus
    if corpus_path and Path(corpus_path).exists():
        raw = Path(corpus_path).read_text(encoding="utf-8", errors="replace")
        sentences = [s.strip() for s in raw.split("\n") if s.strip()]
    else:
        sentences = _probe_sentences(scope)

    # Limit total tokens to roughly budget * 16 to keep runtime sane
    max_sentences = min(len(sentences), budget)
    sentences = sentences[:max_sentences]
    print(f"[harvest] running {len(sentences)} probe sentences for activations ...", flush=True)

    with torch.no_grad():
        for i, sent in enumerate(sentences):
            ids = tok(sent, return_tensors="pt", truncation=True, max_length=64)
            try:
                model(**ids)
            except Exception as e:
                print(f"[harvest] forward error on sentence {i}: {e}", flush=True)
            if (i + 1) % 20 == 0:
                print(f"[harvest] {i+1}/{len(sentences)} sentences done", flush=True)

    handle.remove()

    if not activations:
        print("Error: no activations collected — forward hook may have missed.", file=sys.stderr)
        return 1

    A = np.vstack(activations)  # (n_tokens_total, hidden_dim)
    print(f"[harvest] activation matrix: {A.shape}", flush=True)

    # Normalise rows
    row_norms = np.linalg.norm(A, axis=1, keepdims=True)
    A_norm = A / (row_norms + 1e-9)

    # Subsample if very large
    max_samples = 8192
    if A_norm.shape[0] > max_samples:
        rng = np.random.default_rng(42)
        A_norm = A_norm[rng.choice(A_norm.shape[0], max_samples, replace=False)]
    print(f"[harvest] fitting DictionaryLearning: n_features={n_features}, "
          f"n_samples={A_norm.shape[0]} ...", flush=True)

    dl = DictionaryLearning(
        n_components=n_features,
        alpha=1.0,
        max_iter=200,
        fit_algorithm="lars",
        transform_algorithm="lasso_lars",
        random_state=42,
        n_jobs=-1,
        verbose=0,
    )
    dl.fit(A_norm)
    dictionary = dl.components_  # (n_features, hidden_dim)
    print(f"[harvest] dictionary learned: {dictionary.shape}", flush=True)

    # Label features using the embedding matrix
    E = model.get_input_embeddings().weight.detach().float().numpy()
    E_norm = E / (np.linalg.norm(E, axis=1, keepdims=True) + 1e-9)

    def _decode_token(tid: int) -> str:
        try:
            return tok.decode([tid], skip_special_tokens=True).strip()
        except Exception:
            return ""

    features = []
    for i, feat_dir in enumerate(dictionary):
        feat_norm = feat_dir / (np.linalg.norm(feat_dir) + 1e-9)
        scores = E_norm @ feat_norm
        top_idx = np.argsort(scores)[-8:][::-1]
        top_tokens = [t for j in top_idx if (t := _decode_token(int(j)))][:5]
        freq = float(np.mean(np.abs(A_norm @ feat_norm) > 0.1))
        features.append({
            "feature_id": i,
            "direction": feat_norm.tolist(),
            "top_tokens": top_tokens,
            "freq": freq,
            "provenance": "ow_distilled",
            "source_model": model_name,
            "layer": hook_layer_idx,
        })

    with open(output_path, "w") as f:
        json.dump({
            "mode": "feature_dict",
            "n_features": len(features),
            "source_model": model_name,
            "layer": hook_layer_idx,
            "n_layers": n_layers,
            "hidden_dim": int(dictionary.shape[1]),
            "n_activation_samples": int(A.shape[0]),
            "features": features,
        }, f, indent=2)
    print(f"[harvest] wrote {len(features)} feature directions → {output_path}", flush=True)
    return 0


# ── Ingest helper ─────────────────────────────────────────────────────────────

def _ingest(output_path: str, provenance: str) -> None:
    chitta = os.path.expanduser("~/.claude/bin/chitta")
    with open(output_path) as f:
        content = f.read(4000)
    # shell-escape single quotes in content
    content_safe = content.replace("'", "'\\''")
    os.system(
        f"{chitta} remember"
        f" --kind ow_harvest --realm cec:harvest"
        f" --content '{content_safe}'"
        f" --provenance {provenance}"
    )


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--model", required=True, help="HuggingFace model name or local path")
    p.add_argument("--mode", required=True,
                   choices=["vocab_geometry", "triplet_extract", "feature_dict"])
    p.add_argument("--provenance", default="ow_distilled",
                   choices=["ow_distilled", "ow_generated"])
    p.add_argument("--scope", default="harvest_targets.json",
                   help="harvest_targets.json from `chitta harvest_scope`")
    p.add_argument("--corpus", default="",
                   help="Path to domain text corpus (triplet_extract / feature_dict)")
    p.add_argument("--output", default="harvest_out.json", help="Output file path")
    p.add_argument("--n-features", type=int, default=128,
                   help="(feature_dict) number of dictionary features to learn (default: 128)")
    p.add_argument("--ingest", action="store_true",
                   help="Push output summary to chitta memory after completion")
    args = p.parse_args()

    scope = load_scope(args.scope)
    budget = scope.get("harvest_budget_items", 100)
    print(f"[harvest] scope: diagnosis={scope.get('turiya_diagnosis','unknown')} budget={budget}",
          flush=True)

    if args.mode == "vocab_geometry":
        rc = mode_vocab_geometry(args.model, scope, args.output, budget)
    elif args.mode == "triplet_extract":
        rc = mode_triplet_extract(
            args.model, args.corpus, scope, args.output, args.provenance, budget)
    elif args.mode == "feature_dict":
        rc = mode_feature_dict(
            args.model, args.corpus, scope, args.output, args.n_features, budget)
    else:
        rc = 1

    if rc == 0 and args.ingest:
        print(f"[harvest] ingesting {args.output} → chitta (provenance={args.provenance}) ...",
              flush=True)
        _ingest(args.output, args.provenance)

    return rc


if __name__ == "__main__":
    sys.exit(main())
