#!/usr/bin/env python3
"""
eval_recall.py — End-to-end eval for the chitta retrieval system against the live daemon.

Tests 20+ cases across 4 categories:
  factual      (8)  — seed a fact, expect it back
  stale        (4)  — seed original + correction, expect correction NOT original
  conflict     (2)  — seed contradictory facts, expect something surfaces
  noise        (6)  — seed irrelevant content, expect NO hit

Usage:
    python3 eval_recall.py [--no-hdc] [--ablate-hdc] [--chitta-bin PATH] [--verbose] [--dry-run]

Output:
    Per-category TP/TN table + latency p50/p95
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------

DEFAULT_CHITTA_BIN = Path.home() / ".claude" / "bin" / "chitta"
SEED_TAG = "eval_fixture"
RECALL_TIMEOUT = 90   # seconds — embedding can take a while
REMEMBER_TIMEOUT = 30

# ---------------------------------------------------------------------------
# Test suite definition
#
# Each tuple: (category, query, expected_fragments, excluded_fragments, seed_entries)
#
# seed_entries: list of (content, type) pairs to remember before querying.
#   type is one of: wisdom | belief | episode
#
# expected_fragments: list of strings — at least ONE must appear in recall output.
#   Use None / [] to mean "expect NO match" (TN case).
# excluded_fragments: list of strings that must NOT appear (used for stale cases).
# ---------------------------------------------------------------------------

@dataclass
class Case:
    category: str
    query: str
    seed_entries: list[tuple[str, str]]   # (content, node_type)
    expected: list[str]                   # non-empty → TP expected; [] → TN expected
    excluded: list[str] = field(default_factory=list)
    description: str = ""


CASES: list[Case] = [
    # ── factual (8 cases) ───────────────────────────────────────────────────

    Case(
        category="factual",
        description="shell preference",
        query="preferred shell for interactive work",
        seed_entries=[("The user prefers fish shell over bash for interactive sessions.", "wisdom")],
        expected=["fish"],
    ),
    Case(
        category="factual",
        description="editor choice",
        query="code editor preference",
        seed_entries=[("The user exclusively uses neovim and avoids vscode.", "wisdom")],
        expected=["neovim"],
    ),
    Case(
        category="factual",
        description="embedding model path",
        query="embedding model file location",
        seed_entries=[("The embedding model is nomic-embed-text-v1.5 stored at ~/.claude/models/nomic-embed-text-v1.5.gguf.", "wisdom")],
        expected=["nomic-embed-text"],
    ),
    Case(
        category="factual",
        description="GPU node for Ollama",
        query="which node runs Ollama",
        seed_entries=[("dandygpun01fl is the GPU node where Ollama runs.", "wisdom")],
        expected=["dandygpun01fl"],
    ),
    Case(
        category="factual",
        description="SLURM partition",
        query="SLURM partition for GPU jobs",
        seed_entries=[("The SLURM partition for all GPU jobs is compregular.", "wisdom")],
        expected=["compregular"],
    ),
    Case(
        category="factual",
        description="temp directory path",
        query="where to put temporary files on the cluster",
        seed_entries=[("Temporary files go to /projects/caeg/scratch/kbd606/tmp, not /tmp.", "wisdom")],
        expected=["/projects/caeg/scratch"],
    ),
    Case(
        category="factual",
        description="sourmash sketch parameters",
        query="sourmash sketch parameters for metagenomic dereplication",
        seed_entries=[("For metagenomic dereplication we use sourmash sketch with k=31 scaled=1000.", "wisdom")],
        expected=["sourmash", "k=31"],
    ),
    Case(
        category="factual",
        description="chitta daemon binary name",
        query="chitta memory daemon binary name",
        seed_entries=[("The chitta memory daemon binary is named chittad.", "wisdom")],
        expected=["chittad"],
    ),

    # ── stale (4 cases) ─────────────────────────────────────────────────────
    # Seed an old fact then a correction; expect correction, NOT the original.

    Case(
        category="stale",
        description="embedding model update: ONNX → llama.cpp",
        query="which library is used for embeddings",
        seed_entries=[
            ("The project originally used ONNX Runtime for embedding inference.", "episode"),
            ("The project switched from ONNX to llama.cpp for embedding inference.", "wisdom"),
        ],
        expected=["llama.cpp"],
        excluded=["ONNX Runtime"],
    ),
    Case(
        category="stale",
        description="hint model update: larger → Qwen3-0.6B",
        query="which model is used for hint extraction",
        seed_entries=[
            ("Hint extraction originally ran on a 7B parameter model.", "episode"),
            ("The hint extraction model is now Qwen3-0.6B, not a larger model.", "wisdom"),
        ],
        expected=["Qwen3"],
        excluded=["7B"],
    ),
    Case(
        category="stale",
        description="snapshot version bump V19→V20",
        query="current snapshot version for interaction ledger",
        seed_entries=[
            ("The snapshot version was V19 before the interaction ledger.", "episode"),
            ("The snapshot version jumped from V19 to V20 when the interaction ledger was introduced.", "wisdom"),
        ],
        expected=["V20"],
        excluded=["was V19"],
    ),
    Case(
        category="stale",
        description="cluster SSH requirement update",
        query="do chaos cluster nodes require Kerberos for SSH",
        seed_entries=[
            ("Chaos cluster nodes originally required Kerberos authentication for SSH.", "episode"),
            ("The chaos nodes at the cluster no longer require Kerberos for SSH access.", "wisdom"),
        ],
        expected=["no longer", "don't require", "chaos"],
        excluded=["originally required Kerberos"],
    ),

    # ── conflict (2 cases) ──────────────────────────────────────────────────
    # Seed two contradictory beliefs; recall should surface at least one.

    Case(
        category="conflict",
        description="contradictory AGP read length",
        query="minimum read length for AGP metagenome assembly",
        seed_entries=[
            ("AGP samples need 150bp paired-end reads minimum for quality assembly.", "belief"),
            ("AGP samples need 100bp paired-end reads minimum for quality assembly.", "belief"),
        ],
        expected=["AGP", "bp"],
        excluded=[],
    ),
    Case(
        category="conflict",
        description="contradictory test framework preference",
        query="preferred Python test framework",
        seed_entries=[
            ("The project prefers pytest over unittest for all tests.", "belief"),
            ("The project prefers unittest over pytest for all tests.", "belief"),
        ],
        expected=["pytest", "unittest"],
        excluded=[],
    ),

    # ── noise / hard-negative (6 cases) ─────────────────────────────────────
    # Seed irrelevant content; query a completely different topic → expect NO match.

    Case(
        category="noise",
        description="cooking recipe vs bioinformatics query",
        query="bowtie2 alignment parameters for short-read mapping",
        seed_entries=[("Carbonara recipe: use guanciale, not pancetta, and no cream.", "episode")],
        expected=[],   # TN: nothing relevant should come back
        excluded=[],
    ),
    Case(
        category="noise",
        description="astronomy fact vs cluster infra query",
        query="SLURM job submission on the caeg cluster",
        seed_entries=[("The Andromeda galaxy is approximately 2.537 million light-years from Earth.", "episode")],
        expected=[],
    ),
    Case(
        category="noise",
        description="medieval history vs memory daemon query",
        query="how to shut down the chittad daemon",
        seed_entries=[("The Battle of Hastings took place in 1066 CE.", "episode")],
        expected=[],
    ),
    Case(
        category="noise",
        description="gardening tip vs GPU query",
        query="GPU memory requirements for running llama.cpp inference",
        seed_entries=[("Tomatoes grow best in well-drained soil with full sunlight.", "episode")],
        expected=[],
    ),
    Case(
        category="noise",
        description="sports fact vs pipeline query",
        query="snakemake pipeline rule for taxonomic classification",
        seed_entries=[("The FIFA World Cup is held every four years.", "episode")],
        expected=[],
    ),
    Case(
        category="noise",
        description="music theory vs code review query",
        query="how to perform a code review with OpenCode",
        seed_entries=[("A major scale consists of whole-whole-half-whole-whole-whole-half steps.", "episode")],
        expected=[],
    ),
]


# ---------------------------------------------------------------------------
# Result dataclass
# ---------------------------------------------------------------------------

@dataclass
class Result:
    case: Case
    seeded_ids: list[str]
    recall_output: str
    latency_ms: float
    matched: bool        # True = expected fragment found (or correctly absent)
    tp: bool             # True positive: expected content appeared
    tn: bool             # True negative: no match when none expected
    fp: bool             # False positive: match appeared when none expected
    fn: bool             # False negative: expected content not found
    excluded_hit: bool   # An excluded fragment appeared (bad for stale cases)
    error: Optional[str] = None


# ---------------------------------------------------------------------------
# Chitta CLI wrappers
# ---------------------------------------------------------------------------

def chitta_remember(
    bin_path: Path,
    content: str,
    node_type: str = "episode",
    tags: str = SEED_TAG,
    dry_run: bool = False,
    verbose: bool = False,
) -> Optional[str]:
    """Seed a memory; returns the node ID string or None on failure."""
    if dry_run:
        print(f"  [dry-run] remember type={node_type} content={content[:60]!r}")
        return "dry-run-id"

    cmd = [
        str(bin_path), "remember",
        "--content", content,
        "--type", node_type,
        "--tags", tags,
    ]
    if verbose:
        print(f"  $ {' '.join(cmd[:4])} ... (content truncated)")

    t0 = time.monotonic()
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=REMEMBER_TIMEOUT)
    elapsed = (time.monotonic() - t0) * 1000

    if verbose:
        print(f"    → rc={r.returncode} ({elapsed:.0f}ms) stdout={r.stdout.strip()[:120]!r}")

    if r.returncode != 0:
        print(f"  [warn] remember failed rc={r.returncode}: {r.stderr.strip()[:200]}", file=sys.stderr)
        return None

    # Output: "Stored memory #<id>"
    m = re.search(r"#(\S+)", r.stdout)
    if m:
        return m.group(1)

    # Fallback: take any word that looks like an ID (hex or numeric)
    m = re.search(r"\b([0-9a-f]{8,}|[0-9]{6,})\b", r.stdout)
    if m:
        return m.group(1)

    # Return a sentinel so caller knows it succeeded but we couldn't parse the ID
    return "unknown-id"


def chitta_recall(
    bin_path: Path,
    query: str,
    limit: int = 5,
    tag: Optional[str] = None,
    extra_args: Optional[list[str]] = None,
    env: Optional[dict] = None,
    dry_run: bool = False,
    verbose: bool = False,
) -> tuple[str, float]:
    """Run chitta recall; returns (stdout, latency_ms)."""
    if dry_run:
        print(f"  [dry-run] recall --query {query!r} --limit {limit}")
        return ("", 0.0)

    cmd = [
        str(bin_path), "recall",
        "--query", query,
        "--limit", str(limit),
    ]
    if tag:
        cmd += ["--tag", tag]
    if extra_args:
        cmd += extra_args

    if verbose:
        env_note = f" env={list(env.keys())}" if env else ""
        print(f"  $ {' '.join(cmd)}{env_note}")

    t0 = time.monotonic()
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=RECALL_TIMEOUT, env=env)
    latency_ms = (time.monotonic() - t0) * 1000

    if verbose:
        print(f"    → rc={r.returncode} ({latency_ms:.0f}ms)")
        for line in r.stdout.splitlines()[:8]:
            print(f"    {line}")

    if r.returncode != 0:
        return (f"[recall-error rc={r.returncode}] {r.stderr.strip()[:200]}", latency_ms)

    return (r.stdout, latency_ms)


def chitta_forget(
    bin_path: Path,
    node_id: str,
    dry_run: bool = False,
    verbose: bool = False,
) -> bool:
    """Forget a single memory by ID."""
    if dry_run or node_id in ("dry-run-id", "unknown-id"):
        if verbose:
            print(f"  [dry-run] forget --id {node_id}")
        return True

    cmd = [str(bin_path), "forget", "--id", node_id]
    if verbose:
        print(f"  $ {' '.join(cmd)}")

    r = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
    if r.returncode != 0 and verbose:
        print(f"  [warn] forget {node_id} failed: {r.stderr.strip()[:100]}", file=sys.stderr)
    return r.returncode == 0


# ---------------------------------------------------------------------------
# Scoring
# ---------------------------------------------------------------------------

def score_recall(output: str, case: Case) -> tuple[bool, bool, bool, bool, bool]:
    """
    Returns (tp, tn, fp, fn, excluded_hit).

    TN case (expected=[]):
        tn=True if output has no recall hits beyond "Found 0 results"
        fp=True if something appears that matches seeded content
    TP case (expected non-empty):
        tp=True if any expected fragment found in output
        fn=True otherwise
    excluded_hit=True if any excluded fragment appears in output.
    """
    lower_out = output.lower()

    # Determine if recall returned hits at all
    no_results = bool(re.search(r"Found 0 results", output))
    has_content = not no_results and bool(output.strip())

    if not case.expected:
        # TN case
        # Check whether any seeded content leaked into results
        seeded_contents = [entry[0].lower() for entry in case.seed_entries]
        fp = False
        if has_content:
            for content in seeded_contents:
                # Use first 30 chars of content as fingerprint
                fingerprint = content[:30].strip()
                if fingerprint and fingerprint in lower_out:
                    fp = True
                    break
        tn = not fp and (no_results or not has_content)
        return (False, tn, fp, False, False)
    else:
        # TP case
        tp = any(frag.lower() in lower_out for frag in case.expected)
        fn = not tp
        excl_hit = any(frag.lower() in lower_out for frag in case.excluded)
        return (tp, False, False, fn, excl_hit)


# ---------------------------------------------------------------------------
# Run suite
# ---------------------------------------------------------------------------
def run_suite(
    bin_path: Path,
    extra_recall_args: Optional[list[str]] = None,
    recall_env: Optional[dict] = None,
    dry_run: bool = False,
    verbose: bool = False,
    label: str = "",
) -> list[Result]:
    results: list[Result] = []
    all_seeded_ids: list[str] = []

    if label:
        print(f"\n{'='*60}")
        print(f"  Run: {label}")
        print(f"{'='*60}")

    for i, case in enumerate(CASES):
        tag = f"{SEED_TAG},{case.category}"
        print(f"[{i+1:02d}/{len(CASES)}] {case.category:8s}  {case.description}", end="", flush=True)

        seeded_ids: list[str] = []
        seed_ok = True
        for content, node_type in case.seed_entries:
            node_id = chitta_remember(
                bin_path, content, node_type=node_type, tags=tag,
                dry_run=dry_run, verbose=verbose,
            )
            if node_id is None:
                seed_ok = False
            else:
                seeded_ids.append(node_id)
                all_seeded_ids.append(node_id)

        if not seed_ok:
            print(f"  [SEED-FAIL]")
            results.append(Result(
                case=case, seeded_ids=seeded_ids, recall_output="",
                latency_ms=0.0, matched=False,
                tp=False, tn=False, fp=False, fn=True, excluded_hit=False,
                error="seed failed",
            ))
            continue

        if not dry_run:
            time.sleep(0.5)

        output, latency_ms = chitta_recall(
            bin_path, case.query, limit=5,
            extra_args=extra_recall_args,
            env=recall_env,
            dry_run=dry_run, verbose=verbose,
        )

        tp, tn, fp, fn, excl_hit = score_recall(output, case)
        matched = tp or tn

        if case.expected:
            verdict = "TP" if tp else "FN"
            suffix = " [excl-hit!]" if excl_hit else ""
        else:
            verdict = "TN" if tn else "FP"
            suffix = ""
        print(f"  {verdict}{suffix}  ({latency_ms:.0f}ms)")

        results.append(Result(
            case=case, seeded_ids=seeded_ids, recall_output=output,
            latency_ms=latency_ms, matched=matched,
            tp=tp, tn=tn, fp=fp, fn=fn, excluded_hit=excl_hit,
        ))

    if not dry_run and all_seeded_ids:
        print(f"\nCleaning up {len(all_seeded_ids)} seeded memories...", end="", flush=True)
        fails = 0
        for node_id in all_seeded_ids:
            if not chitta_forget(bin_path, node_id, dry_run=dry_run, verbose=verbose):
                fails += 1
        print(f" done ({fails} forget-fails)")

    return results


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

def percentile(values: list[float], p: float) -> float:
    if not values:
        return 0.0
    sv = sorted(values)
    idx = (p / 100) * (len(sv) - 1)
    lo, hi = int(idx), min(int(idx) + 1, len(sv) - 1)
    return sv[lo] + (idx - lo) * (sv[hi] - sv[lo])


def print_report(results: list[Result], label: str = "") -> None:
    header = f"Results" + (f" [{label}]" if label else "")
    print(f"\n{'='*60}")
    print(f"  {header}")
    print(f"{'='*60}")

    categories = ["factual", "stale", "conflict", "noise"]
    rows = []
    for cat in categories:
        subset = [r for r in results if r.case.category == cat]
        if not subset:
            continue
        tp_n  = sum(1 for r in subset if r.tp)
        tn_n  = sum(1 for r in subset if r.tn)
        fp_n  = sum(1 for r in subset if r.fp)
        fn_n  = sum(1 for r in subset if r.fn)
        excl  = sum(1 for r in subset if r.excluded_hit)
        total = len(subset)
        # For factual/stale/conflict: TP rate; for noise: TN rate
        if cat == "noise":
            rate = tn_n / total if total else 0.0
            rate_label = f"TN {tn_n}/{total} ({rate:.0%})"
        else:
            rate = tp_n / total if total else 0.0
            rate_label = f"TP {tp_n}/{total} ({rate:.0%})"
        rows.append((cat, rate_label, fp_n, fn_n, excl))

    col_w = [10, 20, 5, 5, 8]
    header_row = ["Category", "Rate", "FP", "FN", "ExclHit"]
    fmt = "  " + "  ".join(f"{{:<{w}}}" for w in col_w)
    print(fmt.format(*header_row))
    print("  " + "-" * (sum(col_w) + 2 * len(col_w)))
    for row in rows:
        print(fmt.format(*[str(x) for x in row]))

    # Overall
    tp_total = sum(1 for r in results if r.tp)
    tn_total = sum(1 for r in results if r.tn)
    fp_total = sum(1 for r in results if r.fp)
    fn_total = sum(1 for r in results if r.fn)
    total    = len(results)
    correct  = tp_total + tn_total
    print(f"\n  Overall : {correct}/{total} correct ({correct/total:.0%})")
    print(f"  TP={tp_total}  TN={tn_total}  FP={fp_total}  FN={fn_total}")

    # Latency
    lats = [r.latency_ms for r in results if r.latency_ms > 0]
    if lats:
        p50 = percentile(lats, 50)
        p95 = percentile(lats, 95)
        print(f"  Latency p50={p50:.0f}ms  p95={p95:.0f}ms")

    # Failures detail
    failures = [r for r in results if not r.matched or r.excluded_hit or r.error]
    if failures:
        print(f"\n  Failures ({len(failures)}):")
        for r in failures:
            flags = []
            if r.fp: flags.append("FP")
            if r.fn: flags.append("FN")
            if r.excluded_hit: flags.append("excl-hit")
            if r.error: flags.append(f"err:{r.error}")
            print(f"    [{r.case.category}] {r.case.description}: {', '.join(flags)}")
            if r.recall_output and not r.case.expected:
                # Show what leaked through for FP noise cases
                preview = r.recall_output.strip()[:200].replace("\n", " | ")
                print(f"      output: {preview!r}")


def print_ablation_delta(results_with: list[Result], results_without: list[Result]) -> None:
    """Print a TP/TN delta table comparing HDC-on vs HDC-off runs."""
    print(f"\n{'='*60}")
    print(f"  HDC Ablation Delta (with_hdc - without_hdc)")
    print(f"{'='*60}")

    categories = ["factual", "stale", "conflict", "noise"]
    fmt = "  {:<12}  {:>8}  {:>8}  {:>8}"
    print(fmt.format("Category", "w/ HDC", "w/o HDC", "Delta"))
    print("  " + "-" * 44)

    for cat in categories:
        w  = [r for r in results_with    if r.case.category == cat]
        wo = [r for r in results_without if r.case.category == cat]
        if not w or not wo:
            continue
        if cat == "noise":
            s_w  = sum(1 for r in w  if r.tn) / len(w)
            s_wo = sum(1 for r in wo if r.tn) / len(wo)
            metric = "TN%"
        else:
            s_w  = sum(1 for r in w  if r.tp) / len(w)
            s_wo = sum(1 for r in wo if r.tp) / len(wo)
            metric = "TP%"
        delta = s_w - s_wo
        sign = "+" if delta >= 0 else ""
        print(fmt.format(f"{cat}({metric})", f"{s_w:.0%}", f"{s_wo:.0%}", f"{sign}{delta:+.0%}"))

    # Latency comparison
    lats_w  = [r.latency_ms for r in results_with    if r.latency_ms > 0]
    lats_wo = [r.latency_ms for r in results_without if r.latency_ms > 0]
    if lats_w and lats_wo:
        p50_w  = percentile(lats_w,  50)
        p50_wo = percentile(lats_wo, 50)
        p95_w  = percentile(lats_w,  95)
        p95_wo = percentile(lats_wo, 95)
        print(f"\n  Latency p50: w/HDC={p50_w:.0f}ms  w/o={p50_wo:.0f}ms  delta={p50_w-p50_wo:+.0f}ms")
        print(f"  Latency p95: w/HDC={p95_w:.0f}ms  w/o={p95_wo:.0f}ms  delta={p95_w-p95_wo:+.0f}ms")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    ap = argparse.ArgumentParser(
        description="End-to-end eval for chitta retrieval system against the live daemon."
    )
    ap.add_argument(
        "--chitta-bin", default=str(DEFAULT_CHITTA_BIN),
        help=f"Path to chitta binary (default: {DEFAULT_CHITTA_BIN})",
    )
    ap.add_argument(
        "--no-hdc", action="store_true",
        help="Note: --no-hdc is an internal daemon param not exposed at the CLI level. "
             "This flag is accepted for interface compatibility but has no effect on the "
             "chitta recall subprocess calls. Use --ablate-hdc to compare HDC on/off "
             "via the MCP interface instead.",
    )
    ap.add_argument(
        "--ablate-hdc", action="store_true",
        help="Run suite twice (with and without HDC via CHITTA_DISABLE_HDC=1 env var), "
             "then print a delta table.",
    )
    ap.add_argument(
        "--verbose", "-v", action="store_true",
        help="Print every chitta command and its raw output.",
    )
    ap.add_argument(
        "--dry-run", action="store_true",
        help="Print what would be seeded/queried without actually calling chitta.",
    )
    args = ap.parse_args()

    bin_path = Path(args.chitta_bin)
    if not args.dry_run and not bin_path.exists():
        sys.exit(f"chitta binary not found: {bin_path}")

    if args.no_hdc:
        print(
            "[warn] --no-hdc has no effect: disable_hdc is a daemon-side JSON-RPC param, "
            "not a CLI flag. The chitta recall subprocess will use default HDC settings.\n",
            file=sys.stderr,
        )

    print(f"chitta binary : {bin_path}")
    print(f"cases         : {len(CASES)}")
    print(f"seed tag      : {SEED_TAG}")
    if args.dry_run:
        print("[DRY RUN — no daemon calls]")

    if args.ablate_hdc:
        # Run with HDC (default)
        results_with = run_suite(
            bin_path, extra_recall_args=None,
            dry_run=args.dry_run, verbose=args.verbose,
            label="with HDC (default)",
        )
        print_report(results_with, label="with HDC")

        # Run without HDC (via env var if daemon supports it, else same as above)
        # The disable_hdc param is passed through CHITTA_DISABLE_HDC env which the
        # daemon reads — this is the best available mechanism without a CLI flag.
        import os
        env_no_hdc = os.environ.copy()
        env_no_hdc["CHITTA_DISABLE_HDC"] = "1"
        results_without = run_suite(
            bin_path, extra_recall_args=None,
            recall_env=env_no_hdc,
            dry_run=args.dry_run, verbose=args.verbose,
            label="without HDC (CHITTA_DISABLE_HDC=1)",
        )
        print_report(results_without, label="without HDC")
        print_ablation_delta(results_with, results_without)
    else:
        results = run_suite(
            bin_path, extra_recall_args=None,
            dry_run=args.dry_run, verbose=args.verbose,
        )
        print_report(results)


if __name__ == "__main__":
    main()
