#!/usr/bin/env python3
"""
GEPA prompt optimization loop — mutate/evaluate/promote cycle.

Usage:
  hone-gepa.py --prompt <file_or_skill_id> --grader <script> [options]

The grader script receives the prompt on stdin and must print a float score
on stdout (0.0–1.0). Each accepted variant is stored in chitta skill registry.

Example (agentelo grader):
  hone-gepa.py \\
    --prompt writeup/haiku-honed-prompt-v1.md \\
    --grader ~/hone/examples/agentelo-multi-challenge.sh \\
    --model claude-haiku-4-5-20251001 \\
    --mutator claude-sonnet-4-6 \\
    --iters 20 --parallel 5

GEPA loop:
  0. Score seed prompt → set as current best
  1. Mutator proposes a variant (diff reasoning)
  2. Grader evaluates variant → score
  3. If score > best: accept, store in chitta, update best
  4. Repeat until --iters exhausted or --target reached
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path


CHITTA_BIN = os.environ.get("CHITTA_BIN", Path.home() / ".claude/bin/chitta")

MUTATOR_SYSTEM = """\
You are a prompt engineer optimizing a system prompt for a coding agent.
You will receive the current best prompt and a performance score.
Your task: propose a SINGLE improved variant.

Rules:
- Keep changes minimal and targeted — do not rewrite wholesale
- Identify the most likely failure mode given the task description
- One focused change beats many scattered tweaks
- Output ONLY the new prompt text, no commentary, no markdown fencing
"""

MUTATOR_USER = """\
Current best prompt (score={score:.4f}):

{prompt}

Task: {task_desc}

Propose an improved variant that addresses the most likely failure mode.
Output only the prompt text.
"""


def run(cmd: list, stdin: str | None = None, timeout: int = 300) -> tuple[str, int]:
    result = subprocess.run(
        cmd, input=stdin, capture_output=True, text=True, timeout=timeout
    )
    return result.stdout.strip(), result.returncode


def score_prompt(prompt: str, grader: str, parallel: int, timeout: int) -> float:
    with tempfile.NamedTemporaryFile(mode="w", suffix=".txt", delete=False) as f:
        f.write(prompt)
        tmp = f.name
    try:
        out, code = run([grader, tmp], timeout=timeout)
        if code != 0:
            print(f"  [grader error rc={code}]", file=sys.stderr)
            return 0.0
        last_float = None
        for line in reversed(out.splitlines()):
            try:
                last_float = float(line.strip())
                break
            except ValueError:
                continue
        return last_float if last_float is not None else 0.0
    finally:
        os.unlink(tmp)


def mutate_prompt(prompt: str, score: float, task_desc: str, mutator: str) -> str:
    user_msg = MUTATOR_USER.format(prompt=prompt, score=score, task_desc=task_desc)
    out, code = run(
        ["claude", "-p", user_msg, "--model", mutator, "--system", MUTATOR_SYSTEM],
        timeout=120,
    )
    if code != 0 or not out.strip():
        raise RuntimeError(f"Mutator failed (rc={code}): {out[:200]}")
    return out.strip()


def chitta_skill_store(name: str, prompt: str, score: float, iter_n: int) -> str | None:
    payload = json.dumps({
        "name": name,
        "body": prompt,
        "tags": ["hone-gepa", f"iter:{iter_n}", f"score:{score:.4f}"],
        "metadata": {"score": score, "iter": iter_n, "ts": datetime.utcnow().isoformat()},
    })
    out, code = run([str(CHITTA_BIN), "skill", "upload", "--json", payload], timeout=10)
    if code != 0:
        print(f"  [chitta store failed: {out[:100]}]", file=sys.stderr)
        return None
    try:
        return json.loads(out).get("id")
    except Exception:
        return None


def load_prompt(source: str) -> str:
    p = Path(source)
    if p.exists():
        return p.read_text().strip()
    # Try chitta skill lookup
    out, code = run([str(CHITTA_BIN), "skill", "read", source], timeout=10)
    if code == 0 and out:
        return out.strip()
    raise FileNotFoundError(f"Cannot load prompt from: {source}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prompt", required=True, help="Seed prompt: file path or chitta skill id")
    ap.add_argument("--grader", required=True, help="Grader script (receives prompt file path, prints float score)")
    ap.add_argument("--model", default="claude-haiku-4-5-20251001", help="Model being optimized")
    ap.add_argument("--mutator", default="claude-sonnet-4-6", help="Mutator model")
    ap.add_argument("--iters", type=int, default=20, help="Max GEPA iterations")
    ap.add_argument("--target", type=float, default=0.95, help="Stop early if score reaches target")
    ap.add_argument("--grader-timeout", type=int, default=7200, help="Grader timeout (s)")
    ap.add_argument("--task-desc", default="AI coding agent fixing bugs in open-source projects",
                    help="Task description for mutator context")
    ap.add_argument("--skill-prefix", default="hone", help="Prefix for chitta skill names")
    ap.add_argument("--out", help="Write final best prompt to this file")
    args = ap.parse_args()

    if not Path(args.grader).is_file():
        sys.exit(f"Grader not found: {args.grader}")

    seed = load_prompt(args.prompt)
    skill_name = f"{args.skill_prefix}/{args.model}/seed"

    print(f"[hone-gepa] model={args.model} mutator={args.mutator} iters={args.iters}")
    print(f"[hone-gepa] Scoring seed prompt...")
    seed_score = score_prompt(seed, args.grader, getattr(args, "parallel", 5), args.grader_timeout)
    print(f"[hone-gepa] Seed score: {seed_score:.4f}")

    chitta_skill_store(f"{args.skill_prefix}/{args.model}/iter:0", seed, seed_score, 0)

    best_prompt = seed
    best_score = seed_score
    history = [{"iter": 0, "score": seed_score, "accepted": True}]

    for i in range(1, args.iters + 1):
        print(f"\n[hone-gepa] iter {i}/{args.iters} — mutating...")
        try:
            candidate = mutate_prompt(best_prompt, best_score, args.task_desc, args.mutator)
        except Exception as e:
            print(f"  [mutator error: {e}]", file=sys.stderr)
            continue

        print(f"[hone-gepa] iter {i} — grading...")
        score = score_prompt(candidate, args.grader, getattr(args, "parallel", 5), args.grader_timeout)
        delta = score - best_score
        accepted = score > best_score
        status = f"+{delta:.4f} ACCEPTED" if accepted else f"{delta:.4f} rejected"
        print(f"[hone-gepa] iter {i} score={score:.4f} best={best_score:.4f} {status}")

        chitta_skill_store(f"{args.skill_prefix}/{args.model}/iter:{i}", candidate, score, i)
        history.append({"iter": i, "score": score, "accepted": accepted})

        if accepted:
            best_prompt = candidate
            best_score = score
            chitta_skill_store(f"{args.skill_prefix}/{args.model}/best", candidate, score, i)

        if best_score >= args.target:
            print(f"[hone-gepa] Target {args.target} reached at iter {i}, stopping.")
            break

    print(f"\n[hone-gepa] Done. Best score: {best_score:.4f} (seed: {seed_score:.4f}, delta: {best_score - seed_score:+.4f})")
    print("[hone-gepa] History:")
    for h in history:
        mark = "✓" if h["accepted"] else "·"
        print(f"  {mark} iter {h['iter']:2d}: {h['score']:.4f}")

    if args.out:
        Path(args.out).write_text(best_prompt)
        print(f"[hone-gepa] Best prompt written to {args.out}")


if __name__ == "__main__":
    main()
