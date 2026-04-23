"""Run all cases against all strategies, report accuracy + token cost."""

from __future__ import annotations
import sys, time, statistics
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from cases import CASES as LOCAL_CASES
from fastedit_loader import load as load_fastedit
from strategies import STRATEGIES

# Merge: local cases first (hand-crafted), then fastedit's 73 vendored cases.
CASES = list(LOCAL_CASES) + load_fastedit()

try:
    import tiktoken
    ENC = tiktoken.get_encoding("cl100k_base")
    def tokens(s: str) -> int: return len(ENC.encode(s))
except Exception:
    def tokens(s: str) -> int: return max(1, len(s) // 4)


def normalize(s: str) -> str:
    return "\n".join(line.rstrip() for line in s.splitlines()).strip()


def run():
    results = []
    for case in CASES:
        original = case["original"]
        expected = case["expected"]
        for sname, fn in STRATEGIES.items():
            t0 = time.perf_counter_ns()
            try:
                new_content, payload = fn(original, case["edit_args"])
            except Exception as e:
                new_content, payload = original, f"<error: {e}>"
            dt_us = (time.perf_counter_ns() - t0) / 1000
            ok = normalize(new_content) == normalize(expected)
            results.append({
                "case": case["name"],
                "pattern": case["pattern"],
                "strategy": sname,
                "ok": ok,
                "tokens_in": tokens(original) if sname == "baseline_read_write" else 0,
                "tokens_out": tokens(payload),
                "latency_us": dt_us,
            })
    return results


def summarize(results):
    strategies = sorted({r["strategy"] for r in results})
    print(f"\n{'strategy':<22} {'cases':>6} {'acc%':>6} {'avg_in':>8} {'avg_out':>8} {'avg_total':>10} {'p95_total':>10}")
    print("-" * 78)
    for s in strategies:
        rs = [r for r in results if r["strategy"] == s]
        n = len(rs)
        ok = sum(1 for r in rs if r["ok"])
        ins = [r["tokens_in"] for r in rs]
        outs = [r["tokens_out"] for r in rs]
        totals = [a + b for a, b in zip(ins, outs)]
        p95 = sorted(totals)[int(len(totals) * 0.95)] if totals else 0
        print(f"{s:<22} {n:>6} {100*ok/n:>5.1f} "
              f"{statistics.mean(ins):>8.1f} {statistics.mean(outs):>8.1f} "
              f"{statistics.mean(totals):>10.1f} {p95:>10}")

    # Per-pattern breakdown for our primitives vs baseline
    print("\nPer-pattern (file_patch vs baseline_read_write):")
    print(f"{'pattern':<22} {'fp_tok':>8} {'rw_tok':>8} {'savings':>9} {'fp_ok':>6}")
    print("-" * 58)
    patterns = sorted({r["pattern"] for r in results})
    for p in patterns:
        fp = [r for r in results if r["pattern"] == p and r["strategy"] == "file_patch"]
        rw = [r for r in results if r["pattern"] == p and r["strategy"] == "baseline_read_write"]
        if not fp or not rw:
            continue
        fp_t = statistics.mean(r["tokens_out"] for r in fp)
        rw_t = statistics.mean(r["tokens_in"] + r["tokens_out"] for r in rw)
        save = 100 * (1 - fp_t / rw_t) if rw_t > 0 else 0
        fp_ok = sum(1 for r in fp if r["ok"])
        print(f"{p:<22} {fp_t:>8.1f} {rw_t:>8.1f} {save:>8.1f}% {fp_ok}/{len(fp)}")


def main():
    results = run()
    summarize(results)
    # Emit failing cases
    fails = [r for r in results if not r["ok"]]
    if fails:
        print(f"\n{len(fails)} failures:")
        for r in fails:
            print(f"  [{r['strategy']}] {r['case']} ({r['pattern']})")


if __name__ == "__main__":
    main()
