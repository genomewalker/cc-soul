#!/usr/bin/env python3
"""
eval_hint_models.py — Benchmark hint extraction models against a fixed test suite.

Usage:
    python3 eval_hint_models.py [--model PATH] [--n-ctx 512]

Metrics:
    TP rate  — extracted something when a fact was present
    TN rate  — correctly silent when no fact was present
    Quality  — token-overlap score between output and expected hint (TP cases only)
    Overall  — (TP_rate + TN_rate) / 2
"""

import argparse, re, sys
from dataclasses import dataclass, field

# ---------------------------------------------------------------------------
# Test suite: (input, expected_hint_or_None, category)
# expected=None → should output nothing
# ---------------------------------------------------------------------------
TESTS = [
    # ── Personal preferences ────────────────────────────────────────────────
    ("I prefer using fish shell over bash for interactive work",
     "User prefers fish shell over bash for interactive work.",
     "preference"),
    ("I always use neovim, never vscode",
     "User uses neovim exclusively, never VSCode.",
     "preference"),
    ("I find pytest cleaner than unittest",
     "User prefers pytest over unittest.",
     "preference"),

    # ── Tech / stack choices ─────────────────────────────────────────────────
    ("we switched from ONNX to llama.cpp for embeddings",
     "Project switched from ONNX to llama.cpp for embeddings.",
     "tech"),
    ("we're using Qwen3-0.6B for hint extraction, not a larger model",
     "Project uses Qwen3-0.6B for hint extraction.",
     "tech"),
    ("the embedding model is nomic-embed-text-v1.5 via llama.cpp",
     "Embedding model: nomic-embed-text-v1.5 via llama.cpp.",
     "tech"),

    # ── Infrastructure / cluster facts ──────────────────────────────────────
    ("the chaos nodes at the cluster don't require Kerberos for SSH",
     "Cluster chaos nodes don't require Kerberos for SSH.",
     "infra"),
    ("my temp files go to /projects/caeg/scratch/kbd606/tmp, not /tmp",
     "Temp files use /projects/caeg/scratch/kbd606/tmp, not /tmp.",
     "infra"),
    ("dandygpun01fl is the GPU node where Ollama runs",
     "dandygpun01fl is the GPU node running Ollama.",
     "infra"),
    ("the SLURM partition is compregular for all GPU jobs",
     "SLURM partition for GPU jobs: compregular.",
     "infra"),

    # ── Project facts ────────────────────────────────────────────────────────
    ("chitta is the memory daemon, chittad is the binary name",
     "chitta memory daemon binary is named chittad.",
     "project"),
    ("the snapshot version jumped from V19 to V20 for the interaction ledger",
     "Snapshot version V19→V20 introduced the interaction ledger.",
     "project"),
    ("the hint model lives at ~/.claude/models/chitta-hint-qwen-q4_k_m.gguf",
     "Hint model path: ~/.claude/models/chitta-hint-qwen-q4_k_m.gguf.",
     "project"),

    # ── Domain expertise ────────────────────────────────────────────────────
    ("for metagenomic derep we use sourmash sketch with k=31 scaled=1000",
     "Metagenomic derep uses sourmash sketch k=31 scaled=1000.",
     "domain"),
    ("AGP samples need 150bp paired-end reads minimum for quality assembly",
     "AGP samples require 150bp paired-end reads minimum for assembly.",
     "domain"),

    # ── Corrections ──────────────────────────────────────────────────────────
    ("no, the interaction ledger stores events not snapshots",
     "Interaction ledger stores events, not snapshots.",
     "correction"),
    ("actually model.onnx was unused dead weight, we deleted it",
     "model.onnx was unused and has been deleted.",
     "correction"),

    # ── Should NOT extract (TN cases) ───────────────────────────────────────
    ("can you help me with the next step?",                None, "noise"),
    ("what is next?",                                      None, "noise"),
    ("yes",                                                None, "noise"),
    ("ok let's do it",                                     None, "noise"),
    ("check if the job finished",                          None, "noise"),
    ("status?",                                            None, "noise"),
    ("good",                                               None, "noise"),
    ("run it",                                             None, "noise"),
]

# ---------------------------------------------------------------------------

def _tokens(text: str) -> set[str]:
    stop = {"the","a","an","is","are","was","to","of","in","on","for","and",
            "or","not","with","at","by","as","it","be","this","that","from",
            "have","has","can","do","use","get","set","run","user","uses"}
    return {w.lower() for w in re.findall(r"[a-zA-Z0-9_/~.]{2,}", text)
            if w.lower() not in stop}

def quality_score(predicted: str, expected: str) -> float:
    p, e = _tokens(predicted), _tokens(expected)
    if not e:
        return 0.0
    return len(p & e) / len(e)

@dataclass
class Result:
    text: str
    expected: str | None
    category: str
    predicted: str
    tp: bool = False   # should extract and did
    fp: bool = False   # should not extract but did
    fn: bool = False   # should extract but didn't
    tn: bool = False   # should not extract and didn't
    quality: float = 0.0


def run_eval(model_path: str, n_ctx: int = 512) -> list[Result]:
    try:
        from llama_cpp import Llama
    except ImportError:
        sys.exit("llama_cpp not installed — run: pip install llama-cpp-python")

    SYSTEM = ("Extract a single concise retrieval hint from the message. "
              "Cover: personal preferences, tech choices, developer workflows, "
              "domain expertise, project facts, and recurring patterns. "
              "If nothing factual is present, output nothing.")

    print(f"[eval] loading {model_path}", flush=True)
    llm = Llama(model_path=model_path, n_ctx=n_ctx, n_gpu_layers=0, verbose=False)

    results = []
    for text, expected, category in TESTS:
        prompt = (f"<|im_start|>system\n{SYSTEM}<|im_end|>\n"
                  f"<|im_start|>user\n{text}<|im_end|>\n"
                  f"<|im_start|>assistant\n")
        out = llm(prompt, max_tokens=80, temperature=0.0, stop=["<|im_end|>"])
        predicted = out["choices"][0]["text"].strip()
        # normalise: treat "-" or very short as empty
        if not predicted or predicted in ("-", "--") or len(predicted) < 4:
            predicted = ""

        r = Result(text=text, expected=expected, category=category, predicted=predicted)
        if expected is not None:   # should extract
            if predicted:
                r.tp = True
                r.quality = quality_score(predicted, expected)
            else:
                r.fn = True
        else:                      # should NOT extract
            if predicted:
                r.fp = True
            else:
                r.tn = True
        results.append(r)
    return results


def print_report(results: list[Result], model_path: str) -> None:
    tp = [r for r in results if r.tp]
    fn = [r for r in results if r.fn]
    fp = [r for r in results if r.fp]
    tn = [r for r in results if r.tn]

    tp_rate  = len(tp) / (len(tp) + len(fn)) if (tp or fn) else 0.0
    tn_rate  = len(tn) / (len(tn) + len(fp)) if (tn or fp) else 0.0
    avg_qual = sum(r.quality for r in tp) / len(tp) if tp else 0.0
    overall  = (tp_rate + tn_rate) / 2

    print(f"\n{'='*60}")
    print(f"Model : {model_path}")
    print(f"{'='*60}")
    print(f"TP rate  : {tp_rate:.0%}  ({len(tp)}/{len(tp)+len(fn)} factual inputs extracted)")
    print(f"TN rate  : {tn_rate:.0%}  ({len(tn)}/{len(tn)+len(fp)} noise inputs silenced)")
    print(f"Quality  : {avg_qual:.2f}  (token overlap vs expected, TP only)")
    print(f"Overall  : {overall:.0%}")

    if fn:
        print(f"\nFALSE NEGATIVES ({len(fn)}) — missed facts:")
        for r in fn:
            print(f"  [{r.category}] {r.text[:70]}")

    if fp:
        print(f"\nFALSE POSITIVES ({len(fp)}) — noise extracted:")
        for r in fp:
            print(f"  [{r.category}] {r.text[:50]} → {r.predicted[:60]}")

    if tp:
        print(f"\nTRUE POSITIVES quality breakdown:")
        by_cat: dict[str, list[float]] = {}
        for r in tp:
            by_cat.setdefault(r.category, []).append(r.quality)
        for cat, scores in sorted(by_cat.items()):
            print(f"  {cat:12s}: {sum(scores)/len(scores):.2f}  (n={len(scores)})")

    print()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default="/home/kbd606/.claude/models/chitta-hint-qwen-q4_k_m.gguf")
    ap.add_argument("--n-ctx", type=int, default=512)
    args = ap.parse_args()

    results = run_eval(args.model, args.n_ctx)
    print_report(results, args.model)


if __name__ == "__main__":
    main()
