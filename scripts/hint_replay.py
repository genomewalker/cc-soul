#!/usr/bin/env python3
"""hint_replay.py — Quality-replay gate for the realtime-hint backend swap.

Runs the SAME turns through both extractors and emits a side-by-side for
hand-judging whether the C++ HintYantra (the chitta-hintd candidate) is at
least as good as the production Python llama_cpp path:

  - Python: scripts/hint_realtime.py's exact SYSTEM + _extract_hint (one
    inference/turn, the live production logic) — extraction only, no remember.
  - C++:    `chittad hint_extract` (HintYantra::extract, stdin batch).

Both use the same v5-model-b GGUF, so this isolates prompt + decode differences.
Reusable: re-run after any hint-model re-tune to re-validate before cutover.

Usage:
    python3 hint_replay.py --turns-file turns.txt \
        [--model PATH] [--chittad-bin PATH] [--out report.json]
"""

import argparse, json, subprocess, sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE))
from hint_realtime import SYSTEM, _extract_hint, DEFAULT_MODEL  # reuse prod logic

DEFAULT_CHITTAD = Path.home() / ".claude/bin/chittad"


def _read_turns(path: Path) -> list:
    return [ln.strip() for ln in path.read_text().splitlines() if ln.strip()]


def _cpp_hints(turns: list, chittad: Path, model: Path) -> list:
    """One hint per input line via the resident-capable C++ extractor."""
    import os
    env = os.environ.copy()
    env["CHITTA_HINT_MODEL"] = str(model)
    proc = subprocess.run(
        [str(chittad), "hint_extract"],
        input="\n".join(turns) + "\n",
        capture_output=True, text=True, timeout=600, env=env,
    )
    lines = proc.stdout.splitlines()
    # Pad/truncate to len(turns) so alignment is never silently lost.
    out = [lines[i] if i < len(lines) else "" for i in range(len(turns))]
    return [h.strip() for h in out]


def _py_hints(turns: list, model: Path) -> list:
    from llama_cpp import Llama
    llm = Llama(model_path=str(model), n_ctx=512, n_gpu_layers=0,
                n_threads=2, verbose=False)
    return [_extract_hint(llm, t) for t in turns]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--turns-file", required=True)
    ap.add_argument("--model", default=str(DEFAULT_MODEL))
    ap.add_argument("--chittad-bin", default=str(DEFAULT_CHITTAD))
    ap.add_argument("--out", default="")
    args = ap.parse_args()

    turns = _read_turns(Path(args.turns_file))
    model = Path(args.model)
    sys.stderr.write(f"[replay] {len(turns)} turns; model={model}\n")

    sys.stderr.write("[replay] C++ HintYantra (chittad hint_extract)...\n")
    cpp = _cpp_hints(turns, Path(args.chittad_bin), model)
    sys.stderr.write("[replay] Python llama_cpp (hint_realtime logic)...\n")
    py = _py_hints(turns, model)

    rows = [{"i": i, "turn": t, "cpp": cpp[i], "py": py[i]}
            for i, t in enumerate(turns)]
    report = {"system_py": SYSTEM, "n": len(turns), "rows": rows}

    if args.out:
        Path(args.out).write_text(json.dumps(report, ensure_ascii=False, indent=2))
        sys.stderr.write(f"[replay] wrote {args.out}\n")

    # Markdown table to stdout for hand-judging
    print(f"# hint replay — {len(turns)} turns (cpp = HintYantra, py = production)\n")
    for r in rows:
        print(f"## [{r['i']}] {r['turn'][:160]}")
        print(f"- **cpp**: {r['cpp'] or '∅ (no hint)'}")
        print(f"- **py** : {r['py'] or '∅ (no hint)'}\n")


if __name__ == "__main__":
    main()
