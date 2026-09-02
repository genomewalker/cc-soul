#!/usr/bin/env python3
"""SMRITI-Bench scorer: reads a results JSONL and computes headline metrics.

Per condition: success rate with a Wilson 95% interval, mean tokens.
Headline: ΔSR (on - off), ΔT (on - off tokens), MUI (memory utility index),
and per-lane ablation deltas when ablate:<lane> conditions are present.
"""

import argparse
import json
import math
import sys
from collections import defaultdict


def wilson_interval(successes: int, n: int, z: float = 1.96):
    if n == 0:
        return (0.0, 0.0, 0.0)
    p = successes / n
    denom = 1 + z * z / n
    center = (p + z * z / (2 * n)) / denom
    half = (z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n))) / denom
    return (p, max(0.0, center - half), min(1.0, center + half))


def load_records(path):
    with open(path) as f:
        return [json.loads(line) for line in f if line.strip()]


def memory_was_used(record) -> bool:
    """Proxy for "the agent drew on a planted memory": does a prefix of the
    memory's content appear verbatim in the transcript? Weak — see README
    Threats to Validity (a good agent may apply a convention without quoting
    it; a bad one may quote it and still get the fix wrong)."""
    transcript = record.get("transcript", "").lower()
    for content in record.get("planted_memory_contents", []):
        needle = content.lower()[:40]
        if needle and needle in transcript:
            return True
    return False


def score(records):
    by_condition = defaultdict(list)
    for r in records:
        by_condition[r["condition"]].append(r)

    per_condition = {}
    for condition, rs in sorted(by_condition.items()):
        n = len(rs)
        successes = sum(1 for r in rs if r["passed"])
        p, lo, hi = wilson_interval(successes, n)
        tokens = [r["tokens_used"] for r in rs]
        # injected_confirmed is None for records where nothing was planted (off);
        # the confirmation rate is only over records where it's True/False.
        confirmable = [r for r in rs if r.get("injected_confirmed") is not None]
        confirmed = sum(1 for r in confirmable if r["injected_confirmed"])
        per_condition[condition] = {
            "n": n,
            "success_rate": p,
            "wilson_95ci": [lo, hi],
            # mean_tokens is uncached input+output (README "Token accounting" /
            # ClaudeCodeAdapter docstring) -- delta_tokens below is computed from it.
            "mean_tokens": sum(tokens) / n if n else 0.0,
            "mean_input_tokens": sum(r.get("input_tokens", 0) for r in rs) / n if n else 0.0,
            "mean_output_tokens": sum(r.get("output_tokens", 0) for r in rs) / n if n else 0.0,
            "mean_cache_read_tokens": sum(r.get("cache_read_tokens", 0) for r in rs) / n if n else 0.0,
            # Fraction of this condition's records where an outcome-ledger
            # 'injected' event during the agent's call actually named one of
            # the planted ids (runner.read_injected_ids_in_window). None when
            # nothing was planted in this condition (e.g. off). A record with
            # injected_confirmed=False is NOT a valid memory-on sample -- the
            # memory never reached the child session -- see README "Threats
            # to validity" > "Unconfirmed injection".
            "injection_confirmation_rate": (confirmed / len(confirmable)) if confirmable else None,
        }

    headline = {}
    if "off" in per_condition and "on" in per_condition:
        headline["delta_sr"] = per_condition["on"]["success_rate"] - per_condition["off"]["success_rate"]
        headline["delta_tokens"] = per_condition["on"]["mean_tokens"] - per_condition["off"]["mean_tokens"]

        on_wins = [r for r in by_condition["on"] if r["passed"]]
        used = sum(1 for r in on_wins if memory_was_used(r))
        headline["mui"] = used / len(on_wins) if on_wins else None
        headline["mui_n_wins"] = len(on_wins)
        # Surface this at headline level too: a low rate here calls delta_sr
        # itself into question, not just the per-condition detail.
        headline["on_injection_confirmation_rate"] = per_condition["on"]["injection_confirmation_rate"]

    for label in by_condition:
        if label.startswith("ablate:") and "on" in per_condition:
            lane = label.split(":", 1)[1]
            headline.setdefault("ablation_delta_sr", {})[lane] = (
                per_condition["on"]["success_rate"] - per_condition[label]["success_rate"]
            )

    return {"per_condition": per_condition, "headline": headline}


def main():
    parser = argparse.ArgumentParser(description="SMRITI-Bench scorer")
    parser.add_argument("results", help="path to results/<run_id>.jsonl")
    args = parser.parse_args()

    records = load_records(args.results)
    if not records:
        print("no records", file=sys.stderr)
        sys.exit(1)

    print(json.dumps(score(records), indent=2))


if __name__ == "__main__":
    main()
