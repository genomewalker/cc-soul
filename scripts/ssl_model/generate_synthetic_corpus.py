#!/usr/bin/env python3
"""Multi-teacher synthetic SSL-distillation corpus generator (public, no personal data).

Produces a PUBLISHABLE training corpus for the public `ssl_distiller` embedding model so
fresh cc-soul installs can ship a task-matched 1536-d model instead of generic nomic-768.
Unlike generate_web_corpus.py (single Claude teacher, random-pool negatives), this:

  * fans out across several teachers (Anthropic + OpenAI / "gpt & codex") so the SFT
    targets carry diverse distillation styles, reducing single-teacher bias, and
  * builds REAL format-preference DPO pairs — `chosen` = a well-formed SSL distillation,
    `rejected` = a deterministically perturbed (format-broken) variant of the SAME lines —
    instead of unrelated random negatives. A fraction of relevance negatives (random pool)
    is kept so the model also learns topical grounding.

All content is synthetic (TOPICS below); no session/memory data is read. Output schemas
match the trainers verbatim:
  SFT (02_finetune.py):  {"input": passage, "output": ssl, "teacher": name, "source": "synthetic"}
  DPO (02b_dpo.py):      {"prompt": passage, "chosen": ssl, "rejected": bad, "kind": ..., "source": "synthetic"}

Backends are auto-skipped when their API key is absent, so this runs with whatever is
configured (ANTHROPIC_API_KEY and/or OPENAI_API_KEY). Run unattended or via SLURM:

    OPENAI_API_KEY=... ANTHROPIC_API_KEY=... \
        python generate_synthetic_corpus.py --target 2000 \
            --anthropic-model claude-haiku-4-5 --openai-model gpt-5.5
"""

import argparse
import hashlib
import json
import os
import random
import sys
import time
from pathlib import Path

# Reuse the curated topic list, SSL prompt template, parser, and relevance-negative pool
# from the existing single-teacher generator so the two stay in lockstep.
from generate_web_corpus import (  # noqa: E402
    PROMPT_TEMPLATE,
    REJECTION_POOL,
    TOPICS,
    parse_examples,
)

HERE = Path(__file__).resolve().parent
SFT_OUT = HERE / "training_data_sft_synth.jsonl"
DPO_OUT = HERE / "training_data_dpo_synth.jsonl"

SSL_TYPES = ["SOLUTION", "GOTCHA", "PATTERN", "DECISION", "PREFERENCE", "FAILURE"]


# ── Teachers ──────────────────────────────────────────────────────────────────

class Teacher:
    """A model that turns the shared prompt into raw SSL-example text."""

    name = "base"

    def generate(self, prompt: str, max_tokens: int = 4096) -> str:
        raise NotImplementedError


class AnthropicTeacher(Teacher):
    def __init__(self, model: str):
        import anthropic

        self.name = f"anthropic:{model}"
        self.model = model
        self._client = anthropic.Anthropic()
        self._rate_limit = anthropic.RateLimitError

    def generate(self, prompt: str, max_tokens: int = 4096) -> str:
        msg = self._client.messages.create(
            model=self.model,
            max_tokens=max_tokens,
            messages=[{"role": "user", "content": prompt}],
        )
        return msg.content[0].text


class OpenAITeacher(Teacher):
    def __init__(self, model: str):
        import openai

        self.name = f"openai:{model}"
        self.model = model
        self._client = openai.OpenAI()

    def generate(self, prompt: str, max_tokens: int = 4096) -> str:
        resp = self._client.chat.completions.create(
            model=self.model,
            max_completion_tokens=max_tokens,
            messages=[{"role": "user", "content": prompt}],
        )
        return resp.choices[0].message.content


def build_teachers(args) -> list:
    teachers = []
    if os.environ.get("ANTHROPIC_API_KEY"):
        try:
            teachers.append(AnthropicTeacher(args.anthropic_model))
        except Exception as e:  # pragma: no cover - import/auth guard
            print(f"[skip] anthropic: {e}", file=sys.stderr)
    else:
        print("[skip] anthropic: ANTHROPIC_API_KEY unset", file=sys.stderr)
    if os.environ.get("OPENAI_API_KEY"):
        try:
            teachers.append(OpenAITeacher(args.openai_model))
        except Exception as e:  # pragma: no cover
            print(f"[skip] openai: {e}", file=sys.stderr)
    else:
        print("[skip] openai: OPENAI_API_KEY unset", file=sys.stderr)
    return teachers


# ── Format-preference negative (the real DPO signal) ─────────────────────────

def perturb_ssl(ssl: str, rng: random.Random) -> str:
    """Degrade well-formed SSL into a plausible-but-wrong distillation: the kind of output
    a weaker model produces. Used as the DPO `rejected` so training rewards strict format.
    Applies a random subset of: drop the [TYPE] tag, swap in the wrong type, strip the SSL
    arrows, shuffle line order, or truncate a line at the first arrow."""
    lines = [l for l in ssl.split("\n") if l.strip()]
    if not lines:
        return ssl
    ops = rng.sample(
        ["detype", "wrongtype", "dearrow", "shuffle", "truncate"],
        k=rng.randint(1, 3),
    )
    out = list(lines)
    if "shuffle" in ops and len(out) > 1:
        rng.shuffle(out)
    new = []
    for line in out:
        if "detype" in ops:
            line = line.split("]", 1)[-1].lstrip() if "]" in line else line
        elif "wrongtype" in ops and line.startswith("["):
            close = line.find("]")
            if close != -1:
                line = f"[{rng.choice(SSL_TYPES)}]" + line[close + 1 :]
        if "dearrow" in ops:
            line = line.replace("→", " ").replace("->", " ")
        if "truncate" in ops and "→" in line:
            line = line.split("→", 1)[0].strip()
        new.append(line)
    bad = "\n".join(new)
    return bad if bad.strip() and bad != ssl else lines[0]


# ── Main ──────────────────────────────────────────────────────────────────────

def passage_hash(text: str) -> str:
    return hashlib.sha1(text.strip().lower().encode()).hexdigest()[:16]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--target", type=int, default=2000, help="SFT examples to accumulate")
    ap.add_argument("--anthropic-model", default="claude-haiku-4-5")
    ap.add_argument("--openai-model", default="gpt-5.5")
    ap.add_argument("--rejected-relevance-frac", type=float, default=0.25,
                    help="fraction of DPO pairs whose rejected is an unrelated pool line "
                         "(relevance negative) rather than a perturbed-format negative")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--sft-out", type=Path, default=SFT_OUT)
    ap.add_argument("--dpo-out", type=Path, default=DPO_OUT)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    teachers = build_teachers(args)
    if not teachers:
        print("No teachers available (set ANTHROPIC_API_KEY and/or OPENAI_API_KEY).",
              file=sys.stderr)
        return 2
    print(f"Teachers: {', '.join(t.name for t in teachers)}", flush=True)

    seen: set = set()
    for path in (args.sft_out, args.dpo_out):
        path.parent.mkdir(parents=True, exist_ok=True)
    if args.sft_out.exists():
        with open(args.sft_out) as f:
            for line in f:
                try:
                    seen.add(passage_hash(json.loads(line)["input"]))
                except Exception:
                    pass
    written = len(seen)
    print(f"Existing unique passages: {written}", flush=True)

    errors = 0
    sft_f = open(args.sft_out, "a")
    dpo_f = open(args.dpo_out, "a")
    batch = 0
    try:
        while written < args.target:
            batch += 1
            teacher = teachers[batch % len(teachers)]
            topics = ", ".join(rng.sample(TOPICS, 5))
            prompt = PROMPT_TEMPLATE.format(topics=topics)
            try:
                print(f"batch {batch} via {teacher.name} ({written}/{args.target})", flush=True)
                text = teacher.generate(prompt)
                examples = parse_examples(text)
            except Exception as e:
                msg = type(e).__name__
                print(f"  error: {msg}: {e}", file=sys.stderr, flush=True)
                errors += 1
                if errors > 15:
                    print("too many errors, stopping", file=sys.stderr)
                    break
                time.sleep(10 if "RateLimit" in msg else 5)
                continue

            for ex in examples:
                if written >= args.target:
                    break
                h = passage_hash(ex["text"])
                if h in seen:
                    continue
                seen.add(h)
                written += 1
                sft_f.write(json.dumps({
                    "input": ex["text"], "output": ex["ssl"],
                    "teacher": teacher.name, "source": "synthetic",
                }) + "\n")
                if rng.random() < args.rejected_relevance_frac:
                    rejected, kind = rng.choice(REJECTION_POOL), "relevance"
                else:
                    rejected, kind = perturb_ssl(ex["ssl"], rng), "format"
                dpo_f.write(json.dumps({
                    "prompt": ex["text"], "chosen": ex["ssl"], "rejected": rejected,
                    "kind": kind, "teacher": teacher.name, "source": "synthetic",
                }) + "\n")
            sft_f.flush()
            dpo_f.flush()
    finally:
        sft_f.close()
        dpo_f.close()

    print(f"\nDone. unique passages: {written}, errors: {errors}", flush=True)
    print(f"SFT: {args.sft_out}\nDPO: {args.dpo_out}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
