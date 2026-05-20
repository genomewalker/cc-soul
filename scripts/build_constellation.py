#!/usr/bin/env python3
"""CEC Phase 17 — Multi-Model Constellation Builder.

Takes vocab_geometry JSON files from multiple open-weight models and builds
a unified semantic constellation by finding cross-model consensus directions
via token-overlap clustering.

The constellation IS the prior knowledge structure of a blank mind: geometric
relationships between concepts distilled from all open-weight models, without
requiring training data. A chitta-field seeded with the constellation reasons
from the first memory because the semantic space is already structured.

Usage:
  python3 build_constellation.py \\
    --inputs harvest_ow/*.json \\
    --output constellation.json

  # Then seed HDC codebook:
  chitta seed_hdc_geometry --json_path constellation.json
"""

import argparse
import json
import os
import sys
from pathlib import Path


def load_geometry(path: str) -> dict | None:
    try:
        with open(path) as f:
            d = json.load(f)
        if d.get("mode") not in ("vocab_geometry", "constellation"):
            return None
        if not d.get("directions"):
            return None
        return d
    except Exception as e:
        print(f"[constellation] skipping {path}: {e}", file=sys.stderr)
        return None


def token_overlap_score(toks_a: list[str], toks_b: list[str]) -> float:
    """Jaccard similarity between two top-token lists."""
    a, b = set(t.lower().strip() for t in toks_a if t.strip()), \
           set(t.lower().strip() for t in toks_b if t.strip())
    if not a or not b:
        return 0.0
    return len(a & b) / len(a | b)


def build_constellation(geometries: list[dict], n_directions: int,
                        min_models: int, overlap_threshold: float) -> list[dict]:
    """Build a constellation from all directions across all models.

    Strategy: union-with-ranking. ALL directions are included — each model
    learned something unique. Directions agreed on by multiple models rank
    higher (higher consensus_score), but single-model directions are not
    discarded: they represent knowledge only that model captured.

    min_models is used only for reporting, not filtering.
    Clustering merges near-duplicate directions (same concept, different models)
    so the codebook stays compact rather than repeating the same axis N times.
    """
    # Flatten all directions across all models
    all_dirs: list[dict] = []
    for geo in geometries:
        model = geo.get("source_model", "unknown")
        for d in geo["directions"]:
            if not d.get("top_tokens"):
                continue
            all_dirs.append({
                "source_model": model,
                "feature_id":   d["feature_id"],
                "top_tokens":   d["top_tokens"],
                "direction":    d.get("direction"),
            })

    print(f"[constellation] {len(all_dirs)} total directions from {len(geometries)} models",
          flush=True)

    # Greedy clustering: merge near-duplicate directions (same concept, multiple models).
    # A cluster may have support from 1 model (unique knowledge) or many (consensus).
    used = set()
    clusters: list[list[dict]] = []

    for i, seed in enumerate(all_dirs):
        if i in used:
            continue
        cluster = [seed]
        used.add(i)
        for j, cand in enumerate(all_dirs):
            if j in used:
                continue
            if cand["source_model"] == seed["source_model"]:
                continue  # don't merge same-model directions
            score = token_overlap_score(seed["top_tokens"], cand["top_tokens"])
            if score >= overlap_threshold:
                cluster.append(cand)
                used.add(j)
        clusters.append(cluster)  # keep ALL clusters — no min_models filter

    n_consensus = sum(1 for c in clusters
                      if len(set(d["source_model"] for d in c)) >= min_models)
    print(f"[constellation] {len(clusters)} clusters total: "
          f"{n_consensus} cross-model (≥{min_models} models), "
          f"{len(clusters)-n_consensus} unique to one model", flush=True)

    # Sort: consensus directions first, then unique-model directions.
    # Within each tier, sort by cluster size (more evidence = more reliable).
    def cluster_score(c: list[dict]) -> float:
        n_models = len(set(d["source_model"] for d in c))
        n_dirs   = len(c)
        # +1000 bonus per agreeing model so consensus always outranks uniques
        return float(n_models * 1000 + n_dirs)

    clusters.sort(key=cluster_score, reverse=True)
    clusters = clusters[:n_directions]

    # Build output directions
    results = []
    for i, cluster in enumerate(clusters):
        # Consensus tokens = tokens appearing in majority of cluster members
        from collections import Counter
        tok_counter: Counter = Counter()
        for d in cluster:
            for t in d["top_tokens"]:
                tok_counter[t.strip()] += 1
        threshold = max(1, len(cluster) // 2)
        consensus_toks = [t for t, c in tok_counter.most_common(20)
                          if c >= threshold and t][:8]

        # Use direction vector from the model with the largest vocab (most expressive)
        best_dir = max(cluster, key=lambda d: len(d.get("direction") or []))
        direction_vec = best_dir.get("direction")

        n_models = len(set(d["source_model"] for d in cluster))
        results.append({
            "feature_id":     i,
            "top_tokens":     consensus_toks,
            "direction":      direction_vec,
            "consensus_score": n_models / len(geometries),
            "n_supporting_models": n_models,
            "supporting_models": sorted(set(d["source_model"] for d in cluster)),
            "provenance": "ow_distilled",
        })

    return results


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--inputs", nargs="+", required=True,
                   help="vocab_geometry JSON files (glob or explicit list)")
    p.add_argument("--output", default="constellation.json",
                   help="Output constellation JSON (default: constellation.json)")
    p.add_argument("--n-directions", type=int, default=256,
                   help="Max constellation directions to output (default: 256)")
    p.add_argument("--min-models", type=int, default=2,
                   help="Min models that must agree on a direction (default: 2)")
    p.add_argument("--overlap-threshold", type=float, default=0.25,
                   help="Min Jaccard token overlap to cluster two directions (default: 0.25)")
    args = p.parse_args()

    # Expand globs
    import glob
    paths: list[str] = []
    for pat in args.inputs:
        expanded = glob.glob(pat)
        paths.extend(expanded if expanded else [pat])

    geometries = [g for path in paths if (g := load_geometry(path)) is not None]
    if len(geometries) < 2:
        print(f"Error: need at least 2 valid vocab_geometry files, got {len(geometries)}",
              file=sys.stderr)
        return 1

    source_models = [g.get("source_model", "?") for g in geometries]
    print(f"[constellation] building from {len(geometries)} models: {source_models}",
          flush=True)

    directions = build_constellation(
        geometries, args.n_directions, args.min_models, args.overlap_threshold)

    if not directions:
        print("Error: no consensus directions found — try lowering --overlap-threshold "
              "or --min-models", file=sys.stderr)
        return 1

    # Compute coverage stats
    multi_model = sum(1 for d in directions if d["n_supporting_models"] >= 3)
    print(f"[constellation] {len(directions)} directions: "
          f"{multi_model} backed by ≥3 models, "
          f"{len(directions)-multi_model} by 2 models", flush=True)

    out = {
        "mode":           "constellation",
        "n_directions":   len(directions),
        "source_models":  source_models,
        "n_input_models": len(geometries),
        "min_models":     args.min_models,
        "overlap_threshold": args.overlap_threshold,
        "directions":     directions,
    }
    with open(args.output, "w") as f:
        json.dump(out, f, indent=2)
    print(f"[constellation] wrote {len(directions)} constellation directions → {args.output}",
          flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
