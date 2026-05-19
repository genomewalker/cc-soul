#!/usr/bin/env python3
"""
LongMemEval Benchmark Runner for cc-soul
arXiv:2410.10813 — ICLR 2025

Uses the exact judge prompts from the paper's evaluate_qa.py with gpt-4o-mini.
Produces hypothesis JSONL compatible with their evaluate_qa.py for independent re-scoring.

Dataset structure (per item):
  question_id, question_type, question, answer, question_date,
  haystack_dates, haystack_session_ids, haystack_sessions, answer_session_ids
Each haystack_sessions entry is a list of turns: [{role, content, has_answer}, …]

question_type: single-session-user | single-session-assistant | single-session-preference
               | multi-session | temporal-reasoning | knowledge-update

Usage:
  python3 scripts/benchmark_longmemeval.py                             # oracle, all 500
  python3 scripts/benchmark_longmemeval.py --dataset longmemeval_s --limit 50
  python3 scripts/benchmark_longmemeval.py --judge-model gpt-4o --limit 20
  OPENAI_API_KEY=sk-... python3 scripts/benchmark_longmemeval.py
"""

import argparse
import json
import os
import subprocess
import sys
import time
import urllib.request
from datetime import datetime
from pathlib import Path
from typing import Optional

CHITTA = Path.home() / ".claude/bin/chitta"
DATA_DIR = Path("/projects/caeg/scratch/kbd606/tmp")
RESULTS_DIR = Path(__file__).parent.parent / "benchmarks" / "results"
HF_BASE = "https://huggingface.co/datasets/xiaowu0162/longmemeval/resolve/main"

DATASETS = {
    "longmemeval_oracle": (DATA_DIR / "longmemeval_oracle.json", f"{HF_BASE}/longmemeval_oracle"),
    "longmemeval_s":      (DATA_DIR / "longmemeval_s.json",      f"{HF_BASE}/longmemeval_s"),
    "longmemeval_m":      (DATA_DIR / "longmemeval_m.json",      f"{HF_BASE}/longmemeval_m"),
}

QUESTION_TYPES = [
    "single-session-user", "single-session-assistant", "single-session-preference",
    "multi-session", "temporal-reasoning", "knowledge-update",
]


# ── exact judge prompts from LongMemEval evaluate_qa.py ───────────────────────

def get_anscheck_prompt(task: str, question: str, answer: str, response: str,
                        abstention: bool = False) -> str:
    if abstention:
        return (
            "I will give you an unanswerable question, an explanation, and a response from a model. "
            "Please answer yes if the model correctly identifies the question as unanswerable. "
            "The model could say that the information is incomplete, or some other information is given "
            "but the asked information is not.\n\n"
            f"Question: {question}\n\nExplanation: {answer}\n\nModel Response: {response}\n\n"
            "Does the model correctly identify the question as unanswerable? Answer yes or no only."
        )
    if task in ("single-session-user", "single-session-assistant", "multi-session"):
        tmpl = (
            "I will give you a question, a correct answer, and a response from a model. "
            "Please answer yes if the response contains the correct answer. Otherwise, answer no. "
            "If the response is equivalent to the correct answer or contains all the intermediate "
            "steps to get the correct answer, you should also answer yes. If the response only "
            "contains a subset of the information required by the answer, answer no. "
            "\n\nQuestion: {q}\n\nCorrect Answer: {a}\n\nModel Response: {r}\n\n"
            "Is the model response correct? Answer yes or no only."
        )
    elif task == "temporal-reasoning":
        tmpl = (
            "I will give you a question, a correct answer, and a response from a model. "
            "Please answer yes if the response contains the correct answer. Otherwise, answer no. "
            "If the response is equivalent to the correct answer or contains all the intermediate "
            "steps to get the correct answer, you should also answer yes. If the response only "
            "contains a subset of the information required by the answer, answer no. "
            "In addition, do not penalize off-by-one errors for the number of days. "
            "If the question asks for the number of days/weeks/months, etc., and the model makes "
            "off-by-one errors (e.g., predicting 19 days when the answer is 18), the model's "
            "response is still correct. "
            "\n\nQuestion: {q}\n\nCorrect Answer: {a}\n\nModel Response: {r}\n\n"
            "Is the model response correct? Answer yes or no only."
        )
    elif task == "knowledge-update":
        tmpl = (
            "I will give you a question, a correct answer, and a response from a model. "
            "Please answer yes if the response contains the correct answer. Otherwise, answer no. "
            "If the response contains some previous information along with an updated answer, "
            "the response should be considered as correct as long as the updated answer is the "
            "required answer.\n\nQuestion: {q}\n\nCorrect Answer: {a}\n\nModel Response: {r}\n\n"
            "Is the model response correct? Answer yes or no only."
        )
    elif task == "single-session-preference":
        tmpl = (
            "I will give you a question, a rubric for desired personalized response, and a response "
            "from a model. Please answer yes if the response satisfies the desired response. "
            "Otherwise, answer no. The model does not need to reflect all the points in the rubric. "
            "The response is correct as long as it recalls and utilizes the user's personal "
            "information correctly.\n\nQuestion: {q}\n\nRubric: {a}\n\nModel Response: {r}\n\n"
            "Is the model response correct? Answer yes or no only."
        )
    else:
        raise NotImplementedError(f"Unknown task type: {task}")
    return tmpl.format(q=question, a=answer, r=response)


# ── OpenAI judge ──────────────────────────────────────────────────────────────

_openai_client = None
_openai_available: Optional[bool] = None


def _get_openai_client(model: str):
    global _openai_client, _openai_available
    if _openai_available is False:
        return None
    if _openai_client is None:
        key = os.environ.get("OPENAI_API_KEY", "")
        if not key:
            _openai_available = False
            return None
        try:
            from openai import OpenAI
            _openai_client = OpenAI(api_key=key)
            _openai_available = True
        except Exception:
            _openai_available = False
    return _openai_client


def judge_with_openai(task: str, question: str, gold: str, hypothesis: str,
                      model: str, abstention: bool = False) -> Optional[bool]:
    client = _get_openai_client(model)
    if client is None:
        return None
    prompt = get_anscheck_prompt(task, question, gold, hypothesis, abstention)
    try:
        resp = client.chat.completions.create(
            model=model,
            messages=[{"role": "user", "content": prompt}],
            n=1, temperature=0, max_tokens=10,
        )
        text = resp.choices[0].message.content.strip().lower()
        return "yes" in text
    except Exception as e:
        print(f"  [openai error] {e}", file=sys.stderr)
        return None


_codex_cli: Optional[str] = None

def _find_codex_cli() -> Optional[str]:
    global _codex_cli
    if _codex_cli is not None:
        return _codex_cli or None
    for candidate in ["codex", str(Path.home() / ".local/bin/codex")]:
        try:
            r = subprocess.run([candidate, "--version"], capture_output=True, timeout=5)
            if r.returncode == 0:
                _codex_cli = candidate
                return candidate
        except Exception:
            pass
    _codex_cli = ""
    return None


def judge_with_codex(task: str, question: str, gold: str, hypothesis: str,
                     abstention: bool = False) -> Optional[bool]:
    """Judge using `codex exec` non-interactive mode — no chitta socket contention."""
    cli = _find_codex_cli()
    if not cli:
        return None
    prompt = get_anscheck_prompt(task, question, gold, hypothesis, abstention)
    try:
        r = subprocess.run(
            [cli, "exec", "--"],
            input=prompt, capture_output=True, text=True, timeout=60,
        )
        text = r.stdout.strip().lower()
        if "yes" in text:
            return True
        if "no" in text:
            return False
    except Exception:
        pass
    return None


def judge_heuristic(gold: str, hypothesis: str) -> bool:
    """Token-F1 fallback (≥50% overlap)."""
    g = set(gold.lower().split())
    h = set(hypothesis.lower().split())
    return len(g & h) / max(len(g), 1) >= 0.5


# ── data download ─────────────────────────────────────────────────────────────

def ensure_data(dataset: str) -> Path:
    path, url = DATASETS[dataset]
    if path.exists() and path.stat().st_size > 1000:
        return path
    print(f"Downloading {dataset}…")
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": "cc-soul-benchmark"})
    with urllib.request.urlopen(req, timeout=300) as r, open(path, "wb") as f:
        total = int(r.headers.get("Content-Length", 0))
        downloaded = 0
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
            downloaded += len(chunk)
            if total:
                print(f"\r  {downloaded/total:.0%}", end="", flush=True)
    print(f"\r  {path.stat().st_size // 1024 // 1024}MB → {path}")
    return path


# ── chitta RPC ────────────────────────────────────────────────────────────────

def _rpc(tool: str, arguments: dict, timeout: int = 30) -> Optional[dict]:
    req = json.dumps({
        "jsonrpc": "2.0", "id": 1,
        "method": "tools/call",
        "params": {"name": tool, "arguments": arguments},
    })
    try:
        r = subprocess.run(
            [str(CHITTA)], input=req, capture_output=True, text=True, timeout=timeout,
        )
        for line in r.stdout.splitlines():
            if line.startswith("{"):
                resp = json.loads(line)
                return resp.get("result")
    except Exception as e:
        print(f"  [rpc error] {tool}: {e}", file=sys.stderr)
    return None


def remember(content: str, realm: str, tags: list, valid_from: Optional[str] = None) -> bool:
    args: dict = {"content": content, "realm": realm, "tags": tags, "visibility": 0}
    if valid_from:
        args["valid_from"] = valid_from
    return _rpc("remember", args, timeout=20) is not None


def recall(query: str, realm: str, limit: int = 10) -> str:
    result = _rpc("recall", {
        "query": query, "limit": limit,
        "strategy": "hybrid", "realm": realm,
    }, timeout=30)
    if not result:
        return ""
    content = result.get("content", [])
    return content[0].get("text", "") if content else ""


def forget_realm(realm: str):
    _rpc("forget", {"query": realm, "tag": realm.replace("/", "-"), "cascade": "false"}, timeout=30)


def extract_answer(hits: str) -> str:
    """Return best-effort answer from recall hits."""
    skip = ("Found ", "No results", "Error", "[warn", "[rpc")
    for line in hits.splitlines():
        line = line.strip()
        if line and not any(line.startswith(p) for p in skip):
            return line[:300]
    return "I don't know"


# ── main loop ─────────────────────────────────────────────────────────────────

def run(dataset: str, limit: int, keep_data: bool, dry_run: bool,
        verbose: bool, judge_model: str):
    path = ensure_data(dataset)
    with open(path) as f:
        items = json.load(f)
    if limit:
        items = items[:limit]

    print(f"Loaded {len(items)} questions from {dataset}")
    using_openai = bool(os.environ.get("OPENAI_API_KEY"))
    using_codex = not using_openai and bool(_find_codex_cli())
    if using_openai:
        judge_mode = f"{judge_model} (LongMemEval exact prompts)"
    elif using_codex:
        judge_mode = "codex exec (LongMemEval exact prompts)"
    else:
        judge_mode = "token-F1 heuristic"
    print(f"Judge: {judge_mode}")

    RESULTS_DIR.mkdir(parents=True, exist_ok=True)

    scores: dict[str, list[bool]] = {qt: [] for qt in QUESTION_TYPES}
    latencies: list[float] = []
    rows: list[dict] = []
    hypotheses: list[dict] = []  # LongMemEval-format JSONL for their evaluate_qa.py

    for idx, item in enumerate(items):
        qid       = item["question_id"]
        question  = item["question"]
        gold      = item["answer"]
        qtype     = item["question_type"]
        dates     = item.get("haystack_dates", [])
        sessions  = item.get("haystack_sessions", [])
        abstention = "_abs" in qid
        realm     = f"lme/{qid}"

        print(f"[{idx+1}/{len(items)}] {qtype} — {question[:65]}…", end="", flush=True)

        if dry_run:
            print()
            continue

        # --- Ingest ---
        turns_ingested = 0
        for i, session in enumerate(sessions):
            date = dates[i] if i < len(dates) else None
            tags = [realm.replace("/", "-"), f"qt:{qtype.replace('-', '_')}"]
            for turn in session:
                content = turn.get("content", "").strip()
                if not content:
                    continue
                remember(f"[{turn['role']}] {content}",
                         realm=realm, tags=tags, valid_from=date)
                turns_ingested += 1

        # --- Recall ---
        t0 = time.perf_counter()
        hits = recall(question, realm=realm, limit=10)
        latency_ms = (time.perf_counter() - t0) * 1000
        latencies.append(latency_ms)

        hypothesis = extract_answer(hits)

        # --- Judge: OpenAI → codex → token-F1 ---
        if using_openai:
            correct = judge_with_openai(qtype, question, gold, hypothesis,
                                        model=judge_model, abstention=abstention)
        else:
            correct = None
        if correct is None:
            correct = judge_with_codex(qtype, question, gold, hypothesis,
                                       abstention=abstention)
        if correct is None:
            correct = judge_heuristic(gold, hypothesis)

        scores.setdefault(qtype, []).append(correct)
        hypotheses.append({"question_id": qid, "hypothesis": hypothesis})

        print(f"  {'✓' if correct else '✗'}  {latency_ms:.0f}ms  turns={turns_ingested}")
        if verbose:
            print(f"  hyp:  {hypothesis[:100]}")
            print(f"  gold: {gold[:100]}")

        rows.append({
            "qid": qid, "qtype": qtype, "correct": correct,
            "latency_ms": round(latency_ms),
            "hypothesis": hypothesis[:250], "gold": gold[:250],
        })

        if not keep_data:
            forget_realm(realm)

    if dry_run:
        print(f"\nDry run: {len(items)} questions, nothing ingested.")
        return

    # ── Report ────────────────────────────────────────────────────────────────
    git_sha = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"],
        capture_output=True, text=True,
    ).stdout.strip() or "unknown"

    total_q = len(rows)
    total_correct = sum(r["correct"] for r in rows)
    overall = total_correct / total_q if total_q else 0.0
    p50 = sorted(latencies)[len(latencies) // 2] if latencies else 0
    p95 = sorted(latencies)[int(len(latencies) * 0.95)] if latencies else 0

    print(f"\n{'='*60}")
    print(f"  LongMemEval — {dataset}  ({git_sha})")
    print(f"  Judge: {judge_mode}")
    print(f"  Overall: {overall:.1%}  ({total_correct}/{total_q})")
    print(f"  p50={p50:.0f}ms  p95={p95:.0f}ms")
    print(f"{'='*60}")
    for qt in QUESTION_TYPES:
        res = scores.get(qt, [])
        if res:
            print(f"  {qt:35s} {sum(res)/len(res):.1%}  ({sum(res)}/{len(res)})")

    # ── Write outputs ─────────────────────────────────────────────────────────
    out = RESULTS_DIR / f"longmemeval-{git_sha}.md"
    hyp_out = RESULTS_DIR / f"longmemeval-{git_sha}.hyp.jsonl"

    # Hypothesis JSONL (compatible with their evaluate_qa.py)
    hyp_out.write_text("\n".join(json.dumps(h) for h in hypotheses) + "\n")

    now = datetime.now().strftime("%Y-%m-%d %H:%M UTC")
    md = [
        f"# LongMemEval — {git_sha}",
        f"",
        f"**Dataset**: `{dataset}`  **Judge**: `{judge_mode}`  **Date**: {now}  **n**: {total_q}",
        f"",
        "| model | overall | p50_ms | p95_ms |",
        "|---|---|---|---|",
        f"| cc-soul/{git_sha} | {overall:.3f} | {p50:.0f} | {p95:.0f} |",
        f"",
        "## By question type",
        f"",
        "| type | accuracy | n |",
        "|---|---|---|",
    ]
    for qt in QUESTION_TYPES:
        res = scores.get(qt, [])
        if res:
            md.append(f"| {qt} | {sum(res)/len(res):.1%} | {len(res)} |")
    md += ["", "## Per-question", ""]
    md.append("| qid | type | ✓ | ms | hypothesis | gold |")
    md.append("|---|---|---|---|---|---|")
    for r in rows:
        h = r["hypothesis"].replace("|", "\\|").replace("\n", " ")[:80]
        g = r["gold"].replace("|", "\\|").replace("\n", " ")[:80]
        md.append(f"| {r['qid']} | {r['qtype']} | {'✓' if r['correct'] else '✗'} | {r['latency_ms']} | {h} | {g} |")

    out.write_text("\n".join(md) + "\n")
    (out.with_suffix(".json")).write_text(
        json.dumps({"dataset": dataset, "sha": git_sha, "judge": judge_mode, "rows": rows}, indent=2))

    print(f"\nResults  → {out}")
    print(f"Hyp JSONL → {hyp_out}  (re-score: python evaluate_qa.py gpt-4o-mini {hyp_out} {path})")


# ── CLI ───────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="LongMemEval benchmark for cc-soul")
    ap.add_argument("--dataset", default="longmemeval_oracle",
                    choices=list(DATASETS), metavar="DATASET",
                    help=f"One of: {', '.join(DATASETS)} (default: longmemeval_oracle)")
    ap.add_argument("--limit",       type=int, default=0,
                    help="Max questions (0=all)")
    ap.add_argument("--judge-model", default="gpt-4o-mini",
                    help="OpenAI model for judging (default: gpt-4o-mini)")
    ap.add_argument("--keep-data",   action="store_true",
                    help="Keep ingested turns after each question")
    ap.add_argument("--dry-run",     action="store_true",
                    help="Inspect questions without running")
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()

    if not CHITTA.exists():
        sys.exit(f"chitta not found at {CHITTA}")

    run(args.dataset, args.limit, args.keep_data, args.dry_run,
        args.verbose, args.judge_model)


if __name__ == "__main__":
    main()
