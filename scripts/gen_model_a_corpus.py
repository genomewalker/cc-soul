#!/usr/bin/env python3
"""
gen_model_a_corpus.py — Build training corpus for Model A (retrospective enricher).

Model A task: SSL notation memory → natural language hint.
Training data: existing chitta memories labelled by Ollama (qwen3.6:27b).

Usage:
    python3 gen_model_a_corpus.py \\
        [--chitta-bin PATH] \\
        [--out PATH] \\
        [--ollama-url URL] \\
        [--ollama-model MODEL] \\
        [--limit N] \\
        [--dry-run]
"""

import argparse, json, re, sys, time, urllib.request
from pathlib import Path

DEFAULT_OUT   = "/maps/projects/caeg/scratch/kbd606/tmp/model_a_corpus_chatml.jsonl"
OLLAMA_URL    = "http://dandygpun01fl:11434"
OLLAMA_MODEL  = "qwen3.6:27b"
CHITTA_BIN    = Path.home() / ".claude/bin/chitta"

SYSTEM_A = (
    "You are converting a compact memory notation into a natural language retrieval hint. "
    "Output a single concise factual sentence in third person. "
    "Preserve all technical details (paths, versions, names). "
    "If the input is a job/task record or operational log, output nothing."
)

# Skip job/operational records
SKIP_PREFIXES = ("[job]", "[task]", "[agent-task]", "[thread]",
                 "[OPERATIONAL]", "[PROBE]", "Phase ", "phase ", "[turn_")

# Only include memory types likely to have useful SSL notation
INCLUDE_KINDS = {"insight", "wisdom", "signal", "hint", "correction", "episode"}


def _skip(content: str) -> bool:
    c = content.strip()
    for p in SKIP_PREFIXES:
        if c.startswith(p):
            return True
    # skip very long memories (likely logs)
    if len(c) > 600:
        return True
    # skip purely conversational content
    if re.match(r'^(yes|no|ok|sure|done|got it)[.!]?$', c, re.I):
        return True
    return False


def list_memories(chitta_bin: Path, limit: int) -> list[dict]:
    import subprocess
    r = subprocess.run(
        [str(chitta_bin), "list_memories_brief", "--limit", str(limit)],
        capture_output=True, text=True, timeout=30,
    )
    if r.returncode != 0:
        sys.stderr.write(f"[gen_a] list_memories_brief failed: {r.stderr[:200]}\n")
        return []
    try:
        data = json.loads(r.stdout)
        return data if isinstance(data, list) else data.get("memories", [])
    except Exception:
        # try line-by-line
        mems = []
        for line in r.stdout.splitlines():
            try:
                mems.append(json.loads(line))
            except Exception:
                pass
        return mems


def ollama_rewrite(content: str, url: str, model: str) -> str:
    body = json.dumps({
        "model": model,
        "prompt": content[:500],
        "system": SYSTEM_A,
        "stream": False,
        "think": False,
        "options": {"temperature": 0.1, "num_predict": 80},
    }).encode()
    req = urllib.request.Request(
        f"{url}/api/generate",
        data=body,
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as r:
            resp = json.loads(r.read())
        hint = resp.get("response", "").strip()
        if not hint or len(hint) < 6:
            return ""
        # truncate at first sentence end
        for sep in (".", "\n"):
            idx = hint.find(sep)
            if 0 < idx < 150:
                return hint[:idx + 1].strip()
        return hint[:150]
    except Exception as e:
        sys.stderr.write(f"[gen_a] Ollama error: {e}\n")
        return ""


def to_chatml(content: str, hint: str) -> dict:
    return {
        "conversations": [
            {"from": "system",  "value": SYSTEM_A},
            {"from": "human",   "value": content.strip()[:500]},
            {"from": "gpt",     "value": hint},
        ]
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--chitta-bin",   default=str(CHITTA_BIN))
    ap.add_argument("--out",          default=DEFAULT_OUT)
    ap.add_argument("--ollama-url",   default=OLLAMA_URL)
    ap.add_argument("--ollama-model", default=OLLAMA_MODEL)
    ap.add_argument("--limit",        type=int, default=2000)
    ap.add_argument("--dry-run",      action="store_true")
    args = ap.parse_args()

    print(f"[gen_a] listing memories (limit={args.limit})...", flush=True)
    memories = list_memories(Path(args.chitta_bin), args.limit)
    print(f"[gen_a] got {len(memories)} memories", flush=True)

    candidates = [m for m in memories
                  if not _skip(m.get("content", ""))
                  and m.get("kind", "signal") in INCLUDE_KINDS]
    print(f"[gen_a] {len(candidates)} candidates after filtering", flush=True)

    if args.dry_run:
        print("\n[dry-run] sample inputs:")
        for m in candidates[:10]:
            print(f"  {m['content'][:100]}")
        return

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    written = skipped = 0
    with out_path.open("w") as fh:
        for i, m in enumerate(candidates):
            content = m.get("content", "").strip()
            hint = ollama_rewrite(content, args.ollama_url, args.ollama_model)
            if not hint:
                skipped += 1
                continue
            fh.write(json.dumps(to_chatml(content, hint)) + "\n")
            written += 1
            if i % 50 == 0:
                print(f"[gen_a] {i}/{len(candidates)}  written={written}  skipped={skipped}", flush=True)
            time.sleep(0.05)

    print(f"\n[gen_a] done — {written} examples → {args.out}")
    print(f"[gen_a] skipped {skipped} (Ollama returned empty)")


if __name__ == "__main__":
    main()
