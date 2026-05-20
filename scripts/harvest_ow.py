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
import struct
import sys
import os
import urllib.request
import subprocess
import time
from pathlib import Path


# ── GGUF header-only vocab reader ─────────────────────────────────────────────
# Parses only the KV metadata section of a GGUF file (no tensor data loaded).
# Returns the tokenizer.ggml.tokens list without ever touching the tensor bytes.

_GGUF_MAGIC = 0x46554747  # "GGUF"
_GGUF_TYPES = {0:"u8",1:"i8",2:"u16",3:"i16",4:"u32",5:"i32",
               6:"f32",7:"bool",8:"str",9:"arr",10:"u64",11:"i64",12:"f64"}

def _gguf_read_str(f) -> str:
    (n,) = struct.unpack("<Q", f.read(8))
    return f.read(n).decode("utf-8", errors="replace")

def _gguf_read_val(f, vtype: int):
    if vtype == 8:   return _gguf_read_str(f)
    if vtype == 9:   # array
        (et,) = struct.unpack("<I", f.read(4))
        (cnt,) = struct.unpack("<Q", f.read(8))
        return [_gguf_read_val(f, et) for _ in range(cnt)]
    sizes = {0:1,1:1,2:2,3:2,4:4,5:4,6:4,7:1,10:8,11:8,12:8}
    fmts  = {0:"B",1:"b",2:"H",3:"h",4:"I",5:"i",6:"f",7:"?",10:"Q",11:"q",12:"d"}
    n = sizes[vtype]
    (v,) = struct.unpack("<" + fmts[vtype], f.read(n))
    return v

def gguf_read_vocab(gguf_path: str) -> list[str]:
    """Read tokenizer.ggml.tokens from a GGUF file without loading tensors."""
    with open(gguf_path, "rb") as f:
        magic, ver = struct.unpack("<II", f.read(8))
        if magic != _GGUF_MAGIC:
            raise ValueError(f"Not a GGUF file: {gguf_path}")
        (n_tensors, n_kv) = struct.unpack("<QQ", f.read(16))
        kv: dict = {}
        for _ in range(n_kv):
            key = _gguf_read_str(f)
            (vtype,) = struct.unpack("<I", f.read(4))
            val = _gguf_read_val(f, vtype)
            kv[key] = val
    tokens = kv.get("tokenizer.ggml.tokens", [])
    return [t if isinstance(t, str) else str(t) for t in tokens]


# ── Ollama embedding API ───────────────────────────────────────────────────────

def _ollama_ensure_running(ollama_bin: str = "ollama",
                           base_url: str = "http://localhost:11434") -> bool:
    try:
        urllib.request.urlopen(f"{base_url}/api/tags", timeout=5)
        return True
    except Exception:
        pass
    # Only try to start a local server when using localhost
    if "localhost" not in base_url and "127.0.0.1" not in base_url:
        print(f"[harvest] remote Ollama at {base_url} not reachable", file=sys.stderr)
        return False
    print("[harvest] starting ollama serve ...", flush=True)
    subprocess.Popen([ollama_bin, "serve"],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(20):
        time.sleep(2)
        try:
            urllib.request.urlopen(f"{base_url}/api/tags", timeout=2)
            print("[harvest] ollama ready", flush=True)
            return True
        except Exception:
            pass
    return False

def _ollama_find_gguf(model_tag: str, ollama_bin: str = "ollama") -> str | None:
    """Locate the local GGUF blob for an Ollama model via `ollama show --modelfile`."""
    try:
        r = subprocess.run([ollama_bin, "show", "--modelfile", model_tag],
                           capture_output=True, text=True, timeout=15)
        for line in r.stdout.splitlines():
            if line.startswith("FROM "):
                path = line[5:].strip()
                if Path(path).exists():
                    return path
    except Exception:
        pass
    return None


def _ollama_embed_batch(model_tag: str, texts: list[str],
                        base_url: str = "http://localhost:11434") -> list[list[float]] | None:
    payload = json.dumps({"model": model_tag, "input": texts}).encode()
    req = urllib.request.Request(
        f"{base_url}/api/embed",
        data=payload, method="POST",
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=300) as resp:
            data = json.load(resp)
            return data.get("embeddings") or data.get("embedding")
    except Exception as e:
        print(f"[harvest] ollama embed error: {e}", flush=True)
        return None


def _ollama_chat(model_tag: str, system: str, user: str, max_tokens: int = 384,
                 base_url: str = "http://localhost:11434") -> str:
    """Call Ollama /api/chat and return the assistant message text."""
    payload = json.dumps({
        "model": model_tag,
        "messages": [{"role": "system", "content": system},
                     {"role": "user",   "content": user}],
        "stream": False,
        "options": {"num_predict": max_tokens, "temperature": 0},
    }).encode()
    req = urllib.request.Request(
        f"{base_url}/api/chat", data=payload, method="POST",
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            return json.load(resp).get("message", {}).get("content", "")
    except Exception as e:
        print(f"[harvest] ollama chat error: {e}", flush=True)
        return ""


def _fetch_soul_memories(chitta_bin: str, n: int = 200) -> list[str]:
    """Return up to n memory texts from chitta for soul-anchored seeding."""
    try:
        r = subprocess.run(
            [chitta_bin, "recall", "--query", ".", "--limit", str(n), "--text-only"],
            capture_output=True, text=True, timeout=30,
        )
        texts = [l.strip() for l in r.stdout.splitlines() if l.strip()]
        return texts[:n]
    except Exception as e:
        print(f"[harvest] soul-anchor: could not fetch memories: {e}", flush=True)
        return []


def mode_vocab_geometry_gguf(gguf_path: str, ollama_model: str, scope: dict,
                              output_path: str, budget: int,
                              ollama_bin: str = "ollama",
                              soul_anchor: bool = False,
                              chitta_bin: str = "chitta",
                              base_url: str = "http://localhost:11434") -> int:
    """vocab_geometry for a local GGUF model via Ollama embeddings API.

    Reads the vocabulary from the GGUF header (no tensor data loaded), then
    queries Ollama /api/embed for batches of sampled tokens, assembles the
    embedding matrix E, and runs the same PCA pipeline as the HuggingFace path.

    With --soul-anchor: existing soul memories are embedded and injected into
    the matrix before SVD so the resulting directions span the semantic
    neighbourhood of the mind's current content — a targeted constellation
    rather than generic model geometry.
    """
    import numpy as np

    print(f"[harvest] reading GGUF vocab from {gguf_path} ...", flush=True)
    tokens = gguf_read_vocab(gguf_path)
    print(f"[harvest] GGUF vocab size: {len(tokens)}", flush=True)

    if not _ollama_ensure_running(ollama_bin, base_url=base_url):
        print("Error: could not start Ollama server.", file=sys.stderr)
        return 1

    # Sample up to 8192 tokens for the embedding matrix
    import random
    rng = random.Random(42)
    sample_size = min(8192, budget * 32, len(tokens))
    sampled = rng.sample(tokens, sample_size)
    # Always include common words to anchor the geometry
    anchors = ["the","of","and","in","to","is","that","it","was","he",
               "she","they","we","you","for","on","are","with","as","at"]
    sampled = list(dict.fromkeys(anchors + sampled))[:sample_size]

    print(f"[harvest] querying Ollama/{ollama_model} embeddings for {len(sampled)} tokens ...",
          flush=True)
    batch_size = 64
    rows: list[list[float]] = []
    valid_tokens: list[str] = []
    for i in range(0, len(sampled), batch_size):
        batch = sampled[i:i+batch_size]
        embs = _ollama_embed_batch(ollama_model, batch, base_url=base_url)
        if embs is None:
            continue
        for tok, emb in zip(batch, embs):
            if emb:
                rows.append(emb)
                valid_tokens.append(tok)
        if (i // batch_size + 1) % 10 == 0:
            print(f"[harvest] {len(rows)}/{len(sampled)} embeddings collected", flush=True)

    # ── Soul-anchor: embed existing memories and inject into the matrix ──────
    # The soul's existing content defines a constellation in model embedding
    # space. By including those vectors before SVD, the principal directions
    # are biased toward the semantic neighbourhood the mind already occupies —
    # rather than generic model geometry.
    soul_row_count = 0
    if soul_anchor:
        _cbin = os.path.expanduser(f"~/.claude/bin/{chitta_bin}") \
            if not os.path.isabs(chitta_bin) else chitta_bin
        print(f"[harvest] soul-anchor: fetching memories from {_cbin} ...", flush=True)
        soul_texts = _fetch_soul_memories(_cbin, n=min(400, budget * 4))
        if soul_texts:
            print(f"[harvest] soul-anchor: embedding {len(soul_texts)} memories ...", flush=True)
            for i in range(0, len(soul_texts), batch_size):
                batch = soul_texts[i:i+batch_size]
                embs = _ollama_embed_batch(ollama_model, batch, base_url=base_url)
                if embs is None:
                    continue
                for text, emb in zip(batch, embs):
                    if emb:
                        # Weight soul vectors 3× to bias SVD toward mind content
                        rows.extend([emb, emb, emb])
                        valid_tokens.extend([f"[soul]", f"[soul]", f"[soul]"])
                        soul_row_count += 1
            print(f"[harvest] soul-anchor: injected {soul_row_count} memory vectors "
                  f"(3× weight) into embedding matrix", flush=True)
        else:
            print("[harvest] soul-anchor: no memories returned, using random sampling only",
                  flush=True)

    if not rows:
        print("Error: no embeddings returned from Ollama.", file=sys.stderr)
        return 1

    E = np.array(rows, dtype=np.float32)
    print(f"[harvest] embedding matrix: {E.shape}", flush=True)

    norms = np.linalg.norm(E, axis=1, keepdims=True)
    E_norm = E / (norms + 1e-9)

    n_components = min(256, budget, E_norm.shape[0], E_norm.shape[1])
    _, _, Vt = np.linalg.svd(E_norm, full_matrices=False)
    directions = Vt[:n_components]

    results = []
    for i, direction in enumerate(directions):
        scores = E_norm @ direction
        top_idx = scores.argsort()[-8:][::-1]
        top_toks = [valid_tokens[j] for j in top_idx
                    if valid_tokens[j].strip() and valid_tokens[j] != "[soul]"][:5]
        results.append({
            "feature_id": i,
            "direction": direction.tolist(),
            "top_tokens": top_toks,
            "provenance": "ow_distilled",
            "source_model": ollama_model,
        })

    with open(output_path, "w") as f:
        json.dump({
            "mode": "vocab_geometry",
            "n_directions": len(results),
            "source_model": ollama_model,
            "gguf_path": gguf_path,
            "embed_dim": int(E.shape[1]),
            "vocab_size": len(tokens),
            "sampled_tokens": len(valid_tokens),
            "soul_anchored": soul_anchor,
            "soul_memory_vectors": soul_row_count,
            "directions": results,
        }, f, indent=2)
    print(f"[harvest] wrote {len(results)} semantic directions → {output_path}", flush=True)
    return 0


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

def _extract_embedding_matrix(model, torch) -> "np.ndarray | None":
    """Extract static token embedding matrix from any HuggingFace model.

    Tries common attribute paths across causal LM, BERT, ESM, T5, and
    encoder-decoder architectures. Returns float32 numpy array (vocab, d).
    """
    import numpy as np
    candidates = [
        lambda m: m.get_input_embeddings().weight,
        lambda m: m.embeddings.word_embeddings.weight,
        lambda m: m.shared.weight,          # T5 / mT5
        lambda m: m.encoder.embed_tokens.weight,
        lambda m: m.model.embed_tokens.weight,
        lambda m: m.transformer.wte.weight,
    ]
    for fn in candidates:
        try:
            w = fn(model)
            if w is not None:
                return w.detach().float().numpy()
        except (AttributeError, TypeError):
            continue
    return None


def mode_vocab_geometry(model_name: str, scope: dict, output_path: str, budget: int,
                        augment_seqs: str = "") -> int:
    """Extract top semantic direction clusters from embedding matrix E.

    Works with causal LMs, BERT/ESM encoders, T5, and protein/DNA models.

    For small-vocab models (protein/DNA, vocab ≤ 256): uses ALL tokens
    and augments with mean-pooled contextual embeddings from example
    sequences if --augment-seqs is provided (path to a FASTA or plain
    text file, one sequence per line).
    """
    m = _import_stack()
    torch, np = m["torch"], m["np"]
    AutoTokenizer, AutoModel = m["AutoTokenizer"], m["AutoModel"]

    print(f"[harvest] loading {model_name} for vocab_geometry ...", flush=True)
    tok = AutoTokenizer.from_pretrained(model_name, trust_remote_code=True)
    try:
        model = AutoModel.from_pretrained(model_name, torch_dtype=torch.float16,
                                          trust_remote_code=True)
    except Exception:
        model = AutoModel.from_pretrained(model_name, trust_remote_code=True)

    E_raw = _extract_embedding_matrix(model, torch)
    if E_raw is None:
        print(f"Error: could not extract embedding matrix from {model_name}",
              file=sys.stderr)
        return 1
    print(f"[harvest] static embedding matrix: {E_raw.shape}", flush=True)

    rows = list(E_raw)
    vocab_size = E_raw.shape[0]
    is_small_vocab = vocab_size <= 512  # protein/DNA models

    # For small-vocab bio models: augment with contextual embeddings from
    # example sequences — captures evolutionary/structural information that
    # static token embeddings alone miss.
    if is_small_vocab and augment_seqs and Path(augment_seqs).exists():
        print(f"[harvest] small vocab ({vocab_size}) — augmenting with contextual "
              f"embeddings from {augment_seqs} ...", flush=True)
        seqs = []
        with open(augment_seqs) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith(">"):  # skip FASTA headers
                    seqs.append(line[:512])   # cap length
        if seqs:
            model.eval()
            device = "cuda" if torch.cuda.is_available() else "cpu"
            model = model.to(device)
            with torch.no_grad():
                for seq in seqs[:min(500, budget * 5)]:
                    try:
                        enc = tok(seq, return_tensors="pt", truncation=True,
                                  max_length=512).to(device)
                        out = model(**enc)
                        # mean-pool last hidden state
                        vec = out.last_hidden_state.mean(dim=1).squeeze()
                        rows.append(vec.cpu().float().numpy())
                    except Exception:
                        continue
            print(f"[harvest] augmented to {len(rows)} vectors", flush=True)
        model = model.cpu()

    del model

    E = np.array(rows, dtype=np.float32)
    norms = np.linalg.norm(E, axis=1, keepdims=True)
    E_norm = E / (norms + 1e-9)

    # For small vocab, use all rows; otherwise sample up to 8192.
    if is_small_vocab:
        sample_idx = np.arange(len(rows))
    else:
        sample_size = min(8192, len(rows))
        rng = np.random.default_rng(42)
        sample_idx = rng.choice(len(rows), sample_size, replace=False)

    n_components = min(256, budget, len(sample_idx), E_norm.shape[1])
    _, _, Vt = np.linalg.svd(E_norm[sample_idx], full_matrices=False)
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
        # For bio models with tiny vocab, include all token IDs in range
        top_tokens = [t for j in top_idx
                      if j < vocab_size and (t := _decode_token(int(j)))][:5]
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
            "vocab_size": vocab_size,
            "augmented_vectors": len(rows) - vocab_size,
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


def mode_triplet_extract_ollama(ollama_model: str, corpus_path: str, scope: dict,
                                output_path: str, provenance: str, budget: int,
                                ollama_bin: str = "ollama",
                                base_url: str = "http://localhost:11434") -> int:
    """Generate (S,P,O) triplets from a domain corpus via Ollama /api/chat.

    Drop-in replacement for mode_triplet_extract — no transformers/torch required.
    """
    if not corpus_path or not Path(corpus_path).exists():
        print(f"Error: corpus not found: {corpus_path}", file=sys.stderr)
        return 1

    if not _ollama_ensure_running(ollama_bin):
        print("Error: could not start Ollama server.", file=sys.stderr)
        return 1

    target_patterns = [m.get("pattern", "") for m in scope.get("top_router_misses", [])]
    focus = ", ".join(target_patterns[:5]) if target_patterns else "general factual knowledge"

    corpus = Path(corpus_path).read_text(encoding="utf-8", errors="replace")
    chunk_size = 600
    chunks = [corpus[i:i+chunk_size].strip()
              for i in range(0, len(corpus), chunk_size)
              if corpus[i:i+chunk_size].strip()]

    print(f"[harvest] triplet_extract via Ollama/{ollama_model}, "
          f"{min(len(chunks), budget)} chunks (focus: {focus})", flush=True)

    results = []
    for i, chunk in enumerate(chunks[:budget]):
        user_msg = _TRIPLET_USER_TMPL.format(focus=focus, chunk=chunk)
        out = _ollama_chat(ollama_model, _TRIPLET_SYSTEM, user_msg, base_url=base_url)
        for line in out.strip().splitlines():
            line = line.strip().lstrip("- ").strip()
            if not line or not line.startswith("{"):
                continue
            try:
                t = json.loads(line)
                if all(k in t for k in ("subject", "predicate", "object")):
                    t["provenance"] = provenance
                    t["source_model"] = ollama_model
                    t.setdefault("confidence", 0.6)
                    results.append(t)
            except json.JSONDecodeError:
                pass
        if (i + 1) % 10 == 0:
            print(f"[harvest] {i+1}/{min(len(chunks), budget)} chunks, "
                  f"{len(results)} triplets", flush=True)

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
    p.add_argument("--model", default="",
                   help="HuggingFace model name or local path (not needed with --gguf)")
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
    p.add_argument("--gguf", default="",
                   help="(vocab_geometry) path to local GGUF blob; uses Ollama API for embeddings")
    p.add_argument("--ollama-model", default="",
                   help="(vocab_geometry --gguf) Ollama model tag, e.g. gemma4:26b")
    p.add_argument("--ollama-bin", default="ollama",
                   help="Path to ollama binary (default: ollama in PATH)")
    p.add_argument("--ollama-base-url", default="http://localhost:11434",
                   help="Ollama API base URL (default: http://localhost:11434)")
    p.add_argument("--soul-anchor", action="store_true",
                   help="(vocab_geometry) embed existing soul memories via Ollama and inject "
                        "them into the SVD matrix — directions span the mind's semantic "
                        "neighbourhood rather than generic model geometry")
    p.add_argument("--chitta-bin", default="chitta",
                   help="chitta binary name or path (default: chitta)")
    p.add_argument("--augment-seqs", default="",
                   help="(vocab_geometry --model, bio models) FASTA or plain-text file "
                        "of sequences (one per line). Mean-pooled contextual embeddings "
                        "are added to the SVD matrix — critical for protein/DNA models "
                        "with small static vocabularies (ESM-2, DNABERT, NT).")
    p.add_argument("--ingest", action="store_true",
                   help="Push output summary to chitta memory after completion")
    args = p.parse_args()

    if not args.model and not args.gguf:
        p.error("either --model or --gguf is required")

    scope = load_scope(args.scope)
    budget = scope.get("harvest_budget_items", 100)
    print(f"[harvest] scope: diagnosis={scope.get('turiya_diagnosis','unknown')} budget={budget}",
          flush=True)

    ollama_tag = args.ollama_model  # non-empty → Ollama path

    if args.mode == "vocab_geometry":
        if args.gguf:
            tag = ollama_tag or Path(args.gguf).parent.parent.name
            rc = mode_vocab_geometry_gguf(
                args.gguf, tag, scope, args.output, budget, args.ollama_bin,
                soul_anchor=args.soul_anchor, chitta_bin=args.chitta_bin,
                base_url=args.ollama_base_url)
        elif ollama_tag:
            gguf = _ollama_find_gguf(ollama_tag, args.ollama_bin)
            if not gguf:
                print(f"Error: could not find GGUF for Ollama model '{ollama_tag}'. "
                      f"Run `ollama pull {ollama_tag}` first, or pass --gguf explicitly.",
                      file=sys.stderr)
                return 1
            print(f"[harvest] found GGUF: {gguf}", flush=True)
            rc = mode_vocab_geometry_gguf(
                gguf, ollama_tag, scope, args.output, budget, args.ollama_bin,
                soul_anchor=args.soul_anchor, chitta_bin=args.chitta_bin,
                base_url=args.ollama_base_url)
        else:
            if not args.model:
                p.error("either --model or --ollama-model (or --gguf) is required")
            rc = mode_vocab_geometry(args.model, scope, args.output, budget,
                                      augment_seqs=args.augment_seqs)
    elif args.mode == "triplet_extract":
        if ollama_tag:
            rc = mode_triplet_extract_ollama(
                ollama_tag, args.corpus, scope, args.output, args.provenance,
                budget, args.ollama_bin, args.ollama_base_url)
        else:
            if not args.model:
                p.error("--model or --ollama-model is required for triplet_extract")
            rc = mode_triplet_extract(
                args.model, args.corpus, scope, args.output, args.provenance, budget)
    elif args.mode == "feature_dict":
        if ollama_tag:
            print("Note: feature_dict requires internal model activations — "
                  "Ollama API does not expose these. Use --model for this mode.",
                  file=sys.stderr)
            return 1
        if not args.model:
            p.error("--model is required for feature_dict")
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
