#!/usr/bin/env python3
"""
eval-brain-plausibility.py
Measures how well chitta's BGE embedding space correlates with
TRIBE v2 predicted fMRI brain responses (brain-plausibility metric).

For N passages from chitta memory:
  1. Compute pairwise chitta semantic similarity (BGE-small-en-v1.5, CLS pooling)
  2. Compute pairwise brain similarity (TRIBE v2 cortical predictions, or random baseline)
  3. Report Spearman rank correlation between the two similarity structures

Usage:
  python eval-brain-plausibility.py [--n 50] [--no-plot] [--cache-dir ./cache]
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
import warnings
from pathlib import Path

import numpy as np
from scipy.stats import spearmanr
from scipy.spatial.distance import squareform

warnings.filterwarnings("ignore")

CHITTA = Path.home() / ".claude/bin/chitta"
ONNX_MODEL = Path(__file__).parent.parent / "chitta/models/model.onnx"
TRIBE_PYTHON = Path("/maps/projects/fernandezguerra/apps/opt/conda/envs/tribev2/bin/python")

SEED_QUERIES = [
    "memory recall architecture design",
    "wisdom insight learning pattern",
    "code implementation algorithm",
    "philosophy reasoning belief",
    "project milestone decision",
    "error correction debugging",
    "data structure indexing",
]

# ── 1. Passage retrieval ──────────────────────────────────────────────────────

def pull_passages(n: int, min_len: int = 30, max_len: int = 1000) -> list[str]:
    seen_ids = set()
    passages = []

    for query in SEED_QUERIES:
        if len(passages) >= n * 3:
            break
        try:
            result = subprocess.run(
                [str(CHITTA), "recall", "--query", query, "--limit", "50", "--json"],
                capture_output=True, text=True, timeout=10
            )
            data = json.loads(result.stdout)
            for hit in data.get("results", []):
                mid = hit.get("id")
                text = hit.get("text", "").strip()
                kind = hit.get("type", "")
                if mid in seen_ids:
                    continue
                if kind in ("symbol", "probe_centroid"):
                    continue
                if len(text) < min_len or len(text) > max_len:
                    continue
                seen_ids.add(mid)
                passages.append(text)
        except Exception as e:
            print(f"  warn: recall failed for '{query}': {e}", file=sys.stderr)

    if len(passages) < n:
        print(f"  warn: only {len(passages)} passages available (wanted {n})", file=sys.stderr)
    return passages[:n]


# ── 2. Chitta similarity matrix ───────────────────────────────────────────────

def embed_chitta(passages: list[str]) -> np.ndarray:
    """Embed passages using BAAI/bge-small-en-v1.5 (CLS pooling, L2 norm).
    Matches chitta's internal embedding pipeline."""
    try:
        from sentence_transformers import SentenceTransformer
        model = SentenceTransformer("BAAI/bge-small-en-v1.5")
        # CLS pooling is the default for BGE models in sentence-transformers
        embs = model.encode(passages, normalize_embeddings=True, show_progress_bar=False)
        return embs  # (N, 384)
    except ImportError:
        pass

    # Fallback: ONNX model directly
    try:
        import onnxruntime as ort
        from transformers import AutoTokenizer

        tokenizer = AutoTokenizer.from_pretrained("BAAI/bge-small-en-v1.5")
        sess = ort.InferenceSession(str(ONNX_MODEL))

        embs = []
        for text in passages:
            enc = tokenizer(text, return_tensors="np", truncation=True,
                            max_length=512, padding="max_length")
            out = sess.run(None, {
                "input_ids": enc["input_ids"],
                "attention_mask": enc["attention_mask"],
                "token_type_ids": enc.get("token_type_ids",
                    np.zeros_like(enc["input_ids"]))
            })
            cls = out[0][0, 0, :]  # CLS token
            cls = cls / (np.linalg.norm(cls) + 1e-9)
            embs.append(cls)
        return np.array(embs)
    except Exception as e:
        raise RuntimeError(f"Cannot embed passages: {e}")


def chitta_similarity_matrix(passages: list[str]) -> np.ndarray:
    embs = embed_chitta(passages)
    sim = embs @ embs.T  # (N, N) cosine similarity (embeddings are L2-normalized)
    return sim


# ── 3. Brain similarity matrix ────────────────────────────────────────────────

def try_import_tribe():
    try:
        from tribev2 import TribeModel
        return TribeModel
    except (ImportError, Exception):
        # Try via dedicated tribev2 conda env if not importable in current env
        if TRIBE_PYTHON.exists():
            return "subprocess"
        return None


def brain_similarity_matrix_tribe(passages: list[str], cache_dir: Path) -> np.ndarray:
    TribeModel = try_import_tribe()
    cache_dir.mkdir(parents=True, exist_ok=True)

    print("  loading TRIBE v2 model (this may take a moment)...", flush=True)
    model = TribeModel.from_pretrained("facebook/tribev2", cache_folder=str(cache_dir))

    vectors = []
    for i, passage in enumerate(passages):
        print(f"  brain-encoding passage {i+1}/{len(passages)}...", end="\r", flush=True)
        try:
            with tempfile.NamedTemporaryFile(mode="w", suffix=".txt",
                                             delete=False) as f:
                # Truncate at sentence boundary near max_len
                f.write(passage[:1000])
                tmp_path = f.name

            events = model.get_events_dataframe(text_path=tmp_path)
            preds, _ = model.predict(events, verbose=False)

            if preds.shape[0] == 0:
                vectors.append(np.zeros(20484))
            else:
                vectors.append(preds.mean(axis=0))  # average across TRs
        except Exception as e:
            print(f"\n  warn: TRIBE failed for passage {i}: {e}", file=sys.stderr)
            vectors.append(np.zeros(20484))
        finally:
            try:
                os.unlink(tmp_path)
            except Exception:
                pass

    print()
    mat = np.array(vectors)  # (N, 20484)
    norms = np.linalg.norm(mat, axis=1, keepdims=True) + 1e-9
    mat = mat / norms
    return mat @ mat.T


def brain_similarity_matrix_random(passages: list[str], n_perms: int = 100,
                                   seed: int = 42) -> tuple[np.ndarray, np.ndarray]:
    """Random baseline: returns (mean_sim_matrix, std across permutations)."""
    rng = np.random.default_rng(seed)
    n = len(passages)
    sims = []
    for _ in range(n_perms):
        vecs = rng.standard_normal((n, 20484)).astype(np.float32)
        norms = np.linalg.norm(vecs, axis=1, keepdims=True) + 1e-9
        vecs = vecs / norms
        sims.append(vecs @ vecs.T)
    return np.mean(sims, axis=0), np.std(sims, axis=0)


def brain_similarity_matrix_subprocess(passages: list[str], cache_dir: Path) -> np.ndarray:
    """Run TRIBE v2 in its dedicated conda env via subprocess worker script."""
    cache_dir.mkdir(parents=True, exist_ok=True)
    worker = Path(__file__).parent / "_tribe_worker.py"
    if not worker.exists():
        _write_tribe_worker(worker)

    # Write passages to temp JSON
    with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
        json.dump(passages, f)
        passages_file = f.name

    out_file = cache_dir / "brain_vectors.npy"
    try:
        result = subprocess.run(
            [str(TRIBE_PYTHON), str(worker), passages_file, str(out_file), str(cache_dir)],
            capture_output=True, text=True, timeout=1800
        )
        if result.returncode != 0:
            raise RuntimeError(result.stderr[-500:])
        mat = np.load(str(out_file))  # (N, 20484)
        norms = np.linalg.norm(mat, axis=1, keepdims=True) + 1e-9
        mat = mat / norms
        return mat @ mat.T
    finally:
        os.unlink(passages_file)


def _write_tribe_worker(path: Path):
    path.write_text('''#!/usr/bin/env python3
"""Worker: runs TRIBE v2 in its own env, writes (N, 20484) numpy array."""
import sys, json, os, tempfile, warnings
import numpy as np
warnings.filterwarnings("ignore")

passages_file, out_file, cache_dir = sys.argv[1], sys.argv[2], sys.argv[3]
passages = json.load(open(passages_file))

from tribev2 import TribeModel
model = TribeModel.from_pretrained("facebook/tribev2", cache_folder=cache_dir)

vectors = []
for i, passage in enumerate(passages):
    print(f"  brain-encoding {i+1}/{len(passages)}...", end="\\r", flush=True)
    try:
        with tempfile.NamedTemporaryFile(mode="w", suffix=".txt", delete=False) as f:
            f.write(passage[:1000])
            tmp = f.name
        events = model.get_events_dataframe(text_path=tmp)
        preds, _ = model.predict(events, verbose=False)
        vectors.append(preds.mean(axis=0) if preds.shape[0] > 0 else np.zeros(20484))
    except Exception as e:
        print(f"\\n  warn passage {i}: {e}", file=sys.stderr)
        vectors.append(np.zeros(20484))
    finally:
        try: os.unlink(tmp)
        except: pass

np.save(out_file, np.array(vectors))
print(f"\\nSaved {len(vectors)} brain vectors to {out_file}")
''')


# ── 4. Correlation ────────────────────────────────────────────────────────────

def upper_triangle(mat: np.ndarray) -> np.ndarray:
    n = mat.shape[0]
    idx = np.triu_indices(n, k=1)
    return mat[idx]


def compute_correlation(chitta_sim: np.ndarray,
                        brain_sim: np.ndarray) -> tuple[float, float]:
    a = upper_triangle(chitta_sim)
    b = upper_triangle(brain_sim)
    rho, pval = spearmanr(a, b)
    return float(rho), float(pval)


# ── 5. Reporting ──────────────────────────────────────────────────────────────

def interpret(rho: float) -> str:
    if rho > 0.3:
        return "Strong alignment with brain similarity structure"
    if rho > 0.1:
        return "Moderate alignment with brain similarity structure"
    if rho > 0.0:
        return "Weak alignment (marginal)"
    return "No alignment (indistinguishable from random)"


def report(passages, chitta_sim, brain_sim, rho, pval,
           backend: str, no_plot: bool, output_dir: Path):
    n = len(passages)
    pairs = n * (n - 1) // 2

    print()
    print("=" * 50)
    print("  Brain Plausibility Metric")
    print("=" * 50)
    print(f"  Passages:     {n}")
    print(f"  Pairs:        {pairs}")
    print(f"  Backend:      {backend}")
    print(f"  Spearman rho: {rho:.4f}")
    print(f"  p-value:      {pval:.3e}")
    print(f"  Interpretation: {interpret(rho)}")
    print("=" * 50)

    if not no_plot:
        try:
            import matplotlib.pyplot as plt
            a = upper_triangle(chitta_sim)
            b = upper_triangle(brain_sim)
            fig, ax = plt.subplots(figsize=(6, 5))
            ax.scatter(a, b, alpha=0.3, s=8, color="steelblue")
            ax.set_xlabel("Chitta BGE similarity")
            ax.set_ylabel("Brain (TRIBE v2) similarity")
            ax.set_title(f"Brain Plausibility  ρ={rho:.3f}  p={pval:.2e}")
            out = output_dir / "brain-plausibility-scatter.png"
            fig.savefig(out, dpi=150, bbox_inches="tight")
            plt.close(fig)
            print(f"  Scatter plot: {out}")
        except Exception as e:
            print(f"  (plot skipped: {e})", file=sys.stderr)


# ── main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--n", type=int, default=50, help="Number of passages (default 50)")
    parser.add_argument("--no-plot", action="store_true", help="Skip scatter plot")
    parser.add_argument("--cache-dir", type=Path,
                        default=Path(__file__).parent / ".tribe-cache",
                        help="Cache directory for TRIBE v2 model")
    parser.add_argument("--random-perms", type=int, default=100,
                        help="Permutations for random baseline (default 100)")
    args = parser.parse_args()

    print(f"[1/4] Pulling {args.n} passages from chitta...", flush=True)
    passages = pull_passages(args.n)
    n = len(passages)
    if n < 10:
        print(f"ERROR: only {n} passages found, need at least 10.", file=sys.stderr)
        sys.exit(1)
    print(f"  {n} passages retrieved.")

    print("[2/4] Computing chitta similarity matrix...", flush=True)
    chitta_sim = chitta_similarity_matrix(passages)
    print("  done.")

    tribe = try_import_tribe()
    if tribe == "subprocess":
        print("[3/4] Computing brain similarity matrix via tribev2 env...", flush=True)
        try:
            brain_sim = brain_similarity_matrix_subprocess(passages, args.cache_dir)
            backend = "TRIBE v2 subprocess (LLaMA 3.2-3B → fMRI cortical predictions)"
        except Exception as e:
            print(f"  TRIBE v2 subprocess failed: {e}\n  falling back to random baseline.",
                  file=sys.stderr)
            brain_sim, _ = brain_similarity_matrix_random(passages, args.random_perms)
            backend = "random baseline (TRIBE v2 subprocess failed)"
    elif tribe is not None:
        print("[3/4] Computing brain similarity matrix (TRIBE v2)...", flush=True)
        try:
            brain_sim = brain_similarity_matrix_tribe(passages, args.cache_dir)
            backend = "TRIBE v2 (LLaMA 3.2-3B → fMRI cortical predictions)"
        except Exception as e:
            print(f"  TRIBE v2 failed: {e}\n  falling back to random baseline.",
                  file=sys.stderr)
            brain_sim, _ = brain_similarity_matrix_random(passages, args.random_perms)
            backend = "random baseline (TRIBE v2 unavailable)"
    else:
        print("[3/4] TRIBE v2 not installed — using random baseline...", flush=True)
        brain_sim, _ = brain_similarity_matrix_random(passages, args.random_perms)
        backend = "random baseline (install tribev2 for real metric)"

    print("[4/4] Computing Spearman correlation...", flush=True)
    rho, pval = compute_correlation(chitta_sim, brain_sim)

    report(passages, chitta_sim, brain_sim, rho, pval,
           backend, args.no_plot, Path(__file__).parent)


if __name__ == "__main__":
    main()
