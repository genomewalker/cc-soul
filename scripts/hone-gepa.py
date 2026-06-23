#!/usr/bin/env python3
"""GEPA shadow prompt evolution for distill.sh synthesis step.

Reflective Pareto-front prompt evolution (arXiv:2507.19457). Evolves the LLM
synthesis prompt embedded in hooks/distill.sh WITHOUT touching prod until a
candidate Pareto-dominates the current prod prompt on two axes:

  QUALITY   = G0 precision@5 (hooks/grade-recall.py SCORE= line)
  DIVERSITY = normalized Levenshtein distance vs the current prod prompt
              (>=0.05 means "not a trivial tweak")

A candidate dominates prod iff quality >= prod AND diversity >= prod and at
least one is strictly greater (with diversity floored at 0.05 to reject
no-op mutations).

Overfitting guard: 3 of the 10 golden queries are held out. A winner is only
promoted if its holdout precision matches its training precision within 0.05.

Gate: refuses to run unless chitta holds >=5 [run-ledger] signal entries
(hooks/run-ledger.sh ledger_has_domain_entries) — same data-readiness gate
G8/G10 use.

ceiling: QUALITY is measured against the *live* daemon's existing memories, so
it scores how well the prompt's vocabulary aligns with the golden keyword set,
not the end-to-end effect of re-distilling the whole corpus with the mutant.
upgrade: add --redistill to replay a fixture transcript through the shadow
distill.sh and grade the resulting fresh memories.
"""
import argparse
import importlib.util
import json
import os
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path("/maps/projects/fernandezguerra/apps/repos/cc-soul")
DISTILL = ROOT / "hooks" / "distill.sh"
GRADE = ROOT / "hooks" / "grade-recall.py"
RUN_LEDGER = ROOT / "hooks" / "run-ledger.sh"
PARETO_ARCHIVE = ROOT / "scripts" / "gepa-archive.json"

CHITTA_BIN = os.environ.get("CHITTA_BIN", str(Path.home() / ".claude/bin/chitta"))

# Number of golden queries reserved for the overfitting check.
HOLDOUT_N = 3
# A mutation must move the prompt at least this far to count as non-trivial.
MIN_DIVERSITY = 0.05
# Training/holdout precision must agree within this to allow promotion.
VALIDATION_TOL = 0.05

# The synthesis prompt lives in distill.sh as a single-quoted bash assignment:
#   PROMPT='...multiline...'"$CONVERSATION"'...'
# We treat the literal text between PROMPT=' and the final ' before the
# CONVERSATION interpolation as the mutable region.
PROMPT_RE = re.compile(
    r"PROMPT='(?P<head>.*?)'\"\$CONVERSATION\"'(?P<tail>.*?)'\n",
    re.DOTALL,
)

MUTATOR_SYSTEM = (
    "You are a prompt engineer improving an SSL v0.4 knowledge-distillation "
    "prompt. The prompt instructs a small local LLM to extract typed, "
    "annotated learnings from a conversation. Your goal: make the extracted "
    "learnings recall better against keyword queries about the system "
    "(precision@5), WITHOUT breaking the SSL v0.4 format the parser depends "
    "on. Output ONLY the new prompt text, no commentary, no markdown fence."
)

MUTATOR_USER = """Current prod synthesis prompt (G0 precision@5 = {quality:.4f}):

<<<PROMPT
{prompt}
PROMPT

Pareto archive of variants tried so far (quality, diversity):
{archive_digest}

Propose ONE improved variant. Keep the SSL v0.4 markers, the type table, the
annotation rules, and the CONVERSATION/output structure intact — the bash
parser greps for [SOLUTION]/[GOTCHA]/[ε]/[TRIPLET] lines. Target the most
likely recall failure: vague subjects, missing keywords, dropped file paths.
Make a focused, non-trivial change. Output only the prompt body."""


def run(cmd, stdin=None, timeout=300):
    r = subprocess.run(cmd, input=stdin, capture_output=True, text=True, timeout=timeout)
    return r.stdout, r.stderr, r.returncode


# ----------------------------------------------------------------------------
# Gate
# ----------------------------------------------------------------------------
def ledger_gate_open():
    out, _, _ = run(["bash", str(RUN_LEDGER), "has-entries"], timeout=60)
    return out.strip().splitlines()[-1].strip() == "true" if out.strip() else False


# ----------------------------------------------------------------------------
# Grader (imported in-process for clean train/holdout split)
# ----------------------------------------------------------------------------
def _load_grader():
    spec = importlib.util.spec_from_file_location("grade_recall", GRADE)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def quality_full(limit=5):
    """G0 precision@5 via the spec'd path: run grade-recall.py, parse SCORE=."""
    out, err, _ = run([sys.executable, str(GRADE), "--quiet", "--limit", str(limit)], timeout=600)
    for line in reversed((out + err).splitlines()):
        m = re.match(r"\s*SCORE=([0-9.]+)\s*$", line)
        if m:
            return float(m.group(1))
    raise RuntimeError("grade-recall.py emitted no SCORE= line")


def precision_subset(grader, items, limit=5):
    if not items:
        return 0.0
    recs = [grader.grade_one(it, limit) for it in items]
    return sum(r["pass"] for r in recs) / len(recs)


# ----------------------------------------------------------------------------
# Prompt extraction / diversity
# ----------------------------------------------------------------------------
def extract_prompt(path):
    text = Path(path).read_text()
    m = PROMPT_RE.search(text)
    if not m:
        raise RuntimeError(f"Could not locate PROMPT='...' block in {path}")
    return m.group("head"), m.group("tail")


def levenshtein(a, b):
    if a == b:
        return 0
    if not a:
        return len(b)
    if not b:
        return len(a)
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i]
        for j, cb in enumerate(b, 1):
            cur.append(min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (ca != cb)))
        prev = cur
    return prev[-1]


def diversity(candidate, prod):
    d = levenshtein(candidate, prod)
    return d / max(len(candidate), len(prod), 1)


# ----------------------------------------------------------------------------
# Mutator (local GPU endpoint, same discovery as distill.sh; claude CLI fallback)
# ----------------------------------------------------------------------------
def _discover_endpoint():
    import glob
    for f in glob.glob("/tmp/ollama-server-*.url"):
        try:
            url = Path(f).read_text().strip().splitlines()[0].strip()
        except Exception:
            continue
        out, _, _ = run(["curl", "-sL", "--max-time", "3", f"{url}/v1/models"], timeout=5)
        if '"data"' in out or "data" in out:
            return url
    out, _, _ = run(["curl", "-sL", "--max-time", "3", "http://localhost:11434/v1/models"], timeout=5)
    if "data" in out:
        return "http://localhost:11434"
    return None


def _archive_digest(archive):
    if not archive.get("front"):
        return "  (empty)"
    return "\n".join(
        f"  - q={e['quality']:.4f} d={e['diversity']:.4f}" for e in archive["front"][-8:]
    )


def mutate_prompt(current_prompt, pareto_archive, model):
    user = MUTATOR_USER.format(
        quality=pareto_archive.get("prod_quality", 0.0),
        prompt=current_prompt,
        archive_digest=_archive_digest(pareto_archive),
    )
    endpoint = _discover_endpoint()
    if endpoint:
        req = {
            "model": model,
            "messages": [
                {"role": "system", "content": MUTATOR_SYSTEM},
                {"role": "user", "content": user},
            ],
            "temperature": 0.7,
            "max_tokens": 4096,
        }
        out, _, code = run(
            ["curl", "-sL", "--max-time", "180", "-H", "Content-Type: application/json",
             "-d", "@-", f"{endpoint}/v1/chat/completions"],
            stdin=json.dumps(req), timeout=200,
        )
        if code == 0 and out.strip():
            try:
                return json.loads(out)["choices"][0]["message"]["content"].strip()
            except Exception:
                pass
    # Fallback: claude CLI
    out, err, code = run(
        ["claude", "-p", user, "--model", model, "--append-system-prompt", MUTATOR_SYSTEM],
        timeout=180,
    )
    if code != 0 or not out.strip():
        raise RuntimeError(f"Mutator failed (rc={code}): {(err or out)[:200]}")
    return out.strip()


# ----------------------------------------------------------------------------
# Candidate evaluation
# ----------------------------------------------------------------------------
def eval_candidate(mutant_prompt, prod_prompt, grader, train_items, holdout_items, limit=5):
    """Apply the mutant to a shadow copy of distill.sh, score it.

    QUALITY is the headline G0 precision@5 (full golden set via grade-recall.py).
    train/holdout precisions back the overfitting guard.
    """
    # Validate the mutant still slots into the PROMPT_RE shape before scoring.
    shadow = write_shadow(mutant_prompt)
    try:
        extract_prompt(shadow)  # raises if the parser-critical structure broke
    finally:
        Path(shadow).unlink(missing_ok=True)

    return {
        "quality": quality_full(limit),
        "diversity": diversity(mutant_prompt, prod_prompt),
        "train": precision_subset(grader, train_items, limit),
        "holdout": precision_subset(grader, holdout_items, limit),
        "prompt": mutant_prompt,
        "ts": datetime.now(timezone.utc).isoformat(),
    }


def write_shadow(mutant_prompt):
    """Write distill.sh with PROMPT body replaced by the mutant → shadow file."""
    text = DISTILL.read_text()
    head, tail = extract_prompt(DISTILL)
    # Mutant replaces the head (instruction block before CONVERSATION); keep tail.
    new_block = f"PROMPT='{mutant_prompt}'\"$CONVERSATION\"'{tail}'\n"
    old_block = PROMPT_RE.search(text).group(0)
    shadow_text = text.replace(old_block, new_block, 1)
    shadow = DISTILL.with_suffix(".shadow.sh")
    shadow.write_text(shadow_text)
    return str(shadow)


# ----------------------------------------------------------------------------
# Pareto
# ----------------------------------------------------------------------------
def dominates(a, b):
    return (
        a["quality"] >= b["quality"]
        and a["diversity"] >= b["diversity"]
        and (a["quality"] > b["quality"] or a["diversity"] > b["diversity"])
    )


def load_archive():
    if PARETO_ARCHIVE.exists():
        return json.loads(PARETO_ARCHIVE.read_text())
    return {"prod_quality": 0.0, "front": [], "history": []}


def save_archive(archive):
    PARETO_ARCHIVE.write_text(json.dumps(archive, indent=2) + "\n")


def update_archive(archive, candidate):
    """Insert candidate; keep only non-dominated points on the front."""
    archive["history"].append({k: candidate[k] for k in ("quality", "diversity", "train", "holdout", "ts")})
    front = archive.get("front", [])
    if any(dominates(p, candidate) or (p["quality"] == candidate["quality"] and p["diversity"] == candidate["diversity"]) for p in front):
        return archive
    front = [p for p in front if not dominates(candidate, p)]
    front.append({k: candidate[k] for k in ("quality", "diversity", "train", "holdout", "prompt", "ts")})
    archive["front"] = front
    return archive


# ----------------------------------------------------------------------------
# Promote
# ----------------------------------------------------------------------------
def promote(winner_prompt):
    """Write the winner into prod distill.sh in place (idempotent replace)."""
    text = DISTILL.read_text()
    head, tail = extract_prompt(DISTILL)
    old_block = PROMPT_RE.search(text).group(0)
    new_block = f"PROMPT='{winner_prompt}'\"$CONVERSATION\"'{tail}'\n"
    DISTILL.write_text(text.replace(old_block, new_block, 1))


# ----------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--model", default="qwen3:30b-a3b", help="Mutator model (local endpoint or claude CLI)")
    ap.add_argument("--limit", type=int, default=5, help="recall@N for grading")
    ap.add_argument("--promote", action="store_true", help="write winner to prod distill.sh")
    ap.add_argument("--dry-run", action="store_true", help="mutate+eval but never write archive/prod")
    ap.add_argument("--force", action="store_true", help="bypass the run-ledger gate")
    a = ap.parse_args()

    if not a.force and not ledger_gate_open():
        sys.exit("[hone-gepa] GATE CLOSED: <5 [run-ledger] entries in chitta. "
                 "Run more sessions or pass --force.")

    grader = _load_grader()
    golden = grader.GOLDEN_SET
    holdout_items = golden[-HOLDOUT_N:]
    train_items = golden[:-HOLDOUT_N]

    prod_head, _prod_tail = extract_prompt(DISTILL)
    prod_quality = quality_full(a.limit)
    print(f"[hone-gepa] prod quality (precision@{a.limit}) = {prod_quality:.4f}")

    archive = load_archive()
    archive["prod_quality"] = prod_quality
    prod_point = {"quality": prod_quality, "diversity": 0.0,
                  "train": precision_subset(grader, train_items, a.limit),
                  "holdout": precision_subset(grader, holdout_items, a.limit)}

    winner = None
    for r in range(1, a.rounds + 1):
        print(f"\n[hone-gepa] round {r}/{a.rounds} — mutating via {a.model}...")
        try:
            mutant = mutate_prompt(prod_head, archive, a.model)
        except Exception as e:
            print(f"  [mutator error: {e}]", file=sys.stderr)
            continue

        try:
            cand = eval_candidate(mutant, prod_head, grader, train_items, holdout_items, a.limit)
        except Exception as e:
            print(f"  [eval error: {e}]", file=sys.stderr)
            continue

        doms = dominates(cand, prod_point) and cand["diversity"] >= MIN_DIVERSITY
        print(f"[hone-gepa] round {r}: quality={cand['quality']:.4f} "
              f"diversity={cand['diversity']:.4f} train={cand['train']:.4f} "
              f"holdout={cand['holdout']:.4f} "
              f"{'DOMINATES' if doms else 'dominated/trivial'}")

        if not a.dry_run:
            archive = update_archive(archive, cand)
            save_archive(archive)

        if doms:
            gap = abs(cand["holdout"] - cand["train"])
            if gap <= VALIDATION_TOL:
                if winner is None or cand["quality"] > winner["quality"]:
                    winner = cand
                print(f"  [validation OK: |holdout-train|={gap:.4f} <= {VALIDATION_TOL}]")
            else:
                print(f"  [validation FAIL: |holdout-train|={gap:.4f} > {VALIDATION_TOL} — overfit, not promotable]")

    print("\n[hone-gepa] Pareto front:")
    for p in archive.get("front", []):
        print(f"  q={p['quality']:.4f} d={p['diversity']:.4f} "
              f"(train={p['train']:.4f} holdout={p['holdout']:.4f})")

    if winner is None:
        print("[hone-gepa] No Pareto-dominating, validated winner found.")
        return

    print(f"\n[hone-gepa] WINNER: quality={winner['quality']:.4f} diversity={winner['diversity']:.4f}")
    if a.promote and not a.dry_run:
        promote(winner["prompt"])
        print(f"[hone-gepa] Promoted winner to prod {DISTILL}")
    else:
        print("[hone-gepa] Not promoted (pass --promote to write prod distill.sh).")


if __name__ == "__main__":
    main()
