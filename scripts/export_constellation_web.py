#!/usr/bin/env python3
"""Export constellation.json → docs/constellation-data.json (web-safe, no direction vectors).

Run after each constellation rebuild:
  python3 scripts/export_constellation_web.py \
    --input /path/to/constellation.json \
    --output docs/constellation-data.json
"""
import argparse, json, sys
from pathlib import Path


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--input", default="constellation.json")
    p.add_argument("--output", default="docs/constellation-data.json")
    args = p.parse_args()

    with open(args.input) as f:
        c = json.load(f)

    dirs = []
    for d in c.get("directions", []):
        dirs.append({
            "id":               d["feature_id"],
            "tokens":           d["top_tokens"][:6],
            "consensus":        round(d.get("consensus_score", 0), 3),
            "n_models":         d.get("n_supporting_models", 1),
            "models":           d.get("supporting_models", []),
            "provenance":       d.get("provenance", "ow_distilled"),
        })

    out = {
        "n_directions":   len(dirs),
        "source_models":  c.get("source_models", []),
        "n_input_models": c.get("n_input_models", 0),
        "directions":     dirs,
    }
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    with open(args.output, "w") as f:
        json.dump(out, f, separators=(",", ":"))

    kb = Path(args.output).stat().st_size // 1024
    print(f"Exported {len(dirs)} directions → {args.output} ({kb}KB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
