#!/usr/bin/env python3
"""
Generate fasttext training corpus for hook intent classifier.

Sources:
  1. Synthetic examples via claude haiku
  2. Chitta memories (--from-chitta)
  3. Session logs (--from-logs)

Output: fasttext format  __label__<cat> <text>

Categories: correction, preference, belief, milestone, neutral
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

CHITTA_BIN = os.environ.get("CHITTA_BIN", Path.home() / ".claude/bin/chitta")

CATEGORIES = {
    "correction": (
        "user telling an AI coding assistant it made a mistake, got something wrong, "
        "used wrong approach, invented a path/fact, forgot something, or needs to redo something"
    ),
    "preference": (
        "user expressing a workflow preference, style rule, or instruction for how the AI "
        "should always or never behave going forward"
    ),
    "belief": (
        "user stating a project convention, team standard, how their codebase works, "
        "or a persistent fact about their setup"
    ),
    "milestone": (
        "user expressing that something works, a task succeeded, was shipped, merged, "
        "deployed, or completed"
    ),
    "neutral": (
        "normal coding question, debugging request, asking for help, or general technical "
        "query with no correction/preference/belief/milestone intent"
    ),
}

COUNTS = {
    "correction": 400,
    "preference": 400,
    "belief": 400,
    "milestone": 300,
    "neutral": 600,
}

PROMPT = """\
Generate {n} diverse, realistic one-line user messages to an AI coding assistant that are: {description}

Rules:
- Each line is one example
- Vary phrasing, vocabulary, formality, length (5–40 words)
- Include typos and informal language occasionally
- No numbering, no quotes, no markdown formatting
- Mix short punchy and longer detailed messages
- Output ONLY the messages, one per line, nothing else
"""


def call_haiku(prompt: str, model: str) -> list[str]:
    result = subprocess.run(
        ["claude", "-p", prompt, "--model", model],
        capture_output=True, text=True, timeout=120,
    )
    if result.returncode != 0:
        print(f"  [haiku error]: {result.stderr[:200]}", file=sys.stderr)
        return []
    lines = [l.strip() for l in result.stdout.splitlines() if l.strip()]
    return [re.sub(r"^\d+[\.\)]\s*", "", l) for l in lines]


def chitta_export(kind: str, limit: int = 300) -> list[str]:
    query_map = {
        "correction": "mistake wrong incorrect should not forgot invented path",
        "preference": "prefer always use never use style workflow rule",
        "belief": "convention standard we always our codebase project norm",
        "milestone": "it works shipped done success deployed finished merged",
    }
    query = query_map.get(kind, kind)
    result = subprocess.run(
        [str(CHITTA_BIN), "recall", "--query", query, "--tag", kind, "--limit", str(limit), "--toon"],
        capture_output=True, text=True, timeout=30,
    )
    lines = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line or line.startswith("Found"):
            continue
        line = re.sub(r"^\[.*?\]\s*", "", line)
        line = re.sub(r"@\S+", "", line).strip()
        if 10 < len(line) < 300:
            lines.append(line)
    return lines


def session_log_export(logs_dir: Path, limit_per_log: int = 20) -> list[str]:
    lines = []
    for p in sorted(logs_dir.rglob("*.jsonl"))[-50:]:
        try:
            with open(p) as f:
                for raw in f:
                    try:
                        obj = json.loads(raw)
                    except Exception:
                        continue
                    role = obj.get("role") or (obj.get("message") or {}).get("role")
                    content = obj.get("content") or (obj.get("message") or {}).get("content", "")
                    if role != "user":
                        continue
                    if isinstance(content, list):
                        content = " ".join(
                            b.get("text", "") for b in content if isinstance(b, dict)
                        )
                    content = content.strip()
                    if 10 < len(content) < 300 and "\n" not in content[:50]:
                        lines.append(content)
                        if len(lines) >= limit_per_log * 50:
                            break
        except Exception:
            continue
    return lines[:3000]


def write_corpus(path: Path, entries: list[tuple[str, str]]) -> None:
    import gzip
    opener = gzip.open if str(path).endswith(".gz") else open
    with opener(path, "wt", encoding="utf-8") as f:
        for label, text in entries:
            text = text.replace("\n", " ").replace("\r", " ").strip()
            if text:
                f.write(f"__label__{label} {text}\n")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default="claude-haiku-4-5-20251001")
    ap.add_argument("--from-chitta", action="store_true")
    ap.add_argument("--from-logs", action="store_true")
    ap.add_argument("--logs-dir", default=str(Path.home() / ".claude/projects"))
    ap.add_argument("--out", default="hook-corpus.txt")
    ap.add_argument("--no-synthetic", action="store_true")
    args = ap.parse_args()

    entries: list[tuple[str, str]] = []

    if not args.no_synthetic:
        for cat, desc in CATEGORIES.items():
            n = COUNTS[cat]
            print(f"[gen] synthetic {cat} ({n} examples)...", file=sys.stderr)
            batch_size = 100
            generated = []
            for _ in range((n + batch_size - 1) // batch_size):
                want = min(batch_size, n - len(generated))
                lines = call_haiku(PROMPT.format(n=want, description=desc), args.model)
                generated.extend(lines[:want])
                if len(generated) >= n:
                    break
            print(f"  got {len(generated)}", file=sys.stderr)
            entries.extend((cat, l) for l in generated)

    if args.from_chitta:
        chitta_map = {"correction": "correction", "preference": "wisdom", "belief": "belief"}
        for cat, kind in chitta_map.items():
            lines = chitta_export(kind, limit=300)
            print(f"[chitta] {cat}/{kind}: {len(lines)} memories", file=sys.stderr)
            entries.extend((cat, l) for l in lines)

    if args.from_logs:
        logs = session_log_export(Path(args.logs_dir))
        print(f"[logs] extracted {len(logs)} user turns (labeling as neutral)", file=sys.stderr)
        entries.extend(("neutral", l) for l in logs)

    import random
    random.shuffle(entries)
    out = Path(args.out)
    write_corpus(out, entries)
    print(f"[gen] wrote {len(entries)} examples → {out}", file=sys.stderr)

    counts: dict[str, int] = {}
    for label, _ in entries:
        counts[label] = counts.get(label, 0) + 1
    for label, n in sorted(counts.items()):
        print(f"  {label:12s}: {n}", file=sys.stderr)


if __name__ == "__main__":
    main()
