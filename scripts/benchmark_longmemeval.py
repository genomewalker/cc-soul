#!/usr/bin/env python3
"""
LongMemEval Benchmark Runner for cc-soul
arXiv:2410.10813 — ICLR 2025

Dataset structure (per item):
  question_id, question_type, question, answer, question_date,
  haystack_dates, haystack_session_ids, haystack_sessions, answer_session_ids

Each haystack_sessions entry is a list of turns: [{role, content, has_answer}, …]
question_type: single-session-user | single-session-assistant | single-session-preference
               | multi-session | temporal-reasoning | knowledge-update

Ingest: remember() each turn → recall(question) → Haiku synthesize → Haiku judge

Usage:
  python3 scripts/benchmark_longmemeval.py                         # oracle, all 500
  python3 scripts/benchmark_longmemeval.py --dataset longmemeval_s --limit 50
  python3 scripts/benchmark_longmemeval.py --dry-run --limit 5
"""

import argparse
import json
import subprocess
import sys
import time
import urllib.request
from datetime import datetime
from pathlib import Path
from typing import Optional

CHITTA = Path.home() / ".claude/bin/chitta"

def _ensure_api_key():
    """Load ANTHROPIC_API_KEY from ~/.claude/config.json if not already set."""
    import os
    if os.environ.get("ANTHROPIC_API_KEY"):
        return
    try:
        cfg = json.loads((Path.home() / ".claude/config.json").read_text())
        key = cfg.get("primaryApiKey", "")
        if key:
            os.environ["ANTHROPIC_API_KEY"] = key
    except Exception:
        pass

_ensure_api_key()
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

JUDGE_SYSTEM = (
    "You are an answer judge. Given a question, a gold answer, and a system answer, "
    "output JSON only: {\"correct\": true|false, \"reason\": \"one sentence\"}. "
    "Be lenient on phrasing; focus on factual match. "
    "For knowledge-update questions, the newest answer is correct. "
    "If the gold answer is N/A or none, correct means the system said it doesn't know."
)


# ── data download ─────────────────────────────────────────────────────────────

def ensure_data(dataset: str) -> Path:
    path, url = DATASETS[dataset]
    if path.exists() and path.stat().st_size > 1000:
        return path
    print(f"Downloading {dataset} from HuggingFace… ({url})")
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": "cc-soul-benchmark"})
    with urllib.request.urlopen(req, timeout=300) as r, open(path, "wb") as f:
        total = int(r.headers.get("Content-Length", 0))
        downloaded = 0
        while True:
            chunk = r.read(1 << 20)  # 1MB
            if not chunk:
                break
            f.write(chunk)
            downloaded += len(chunk)
            if total:
                print(f"\r  {downloaded / total:.0%}", end="", flush=True)
    print(f"\r  {path.stat().st_size // 1024 // 1024}MB saved to {path}")
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
            [str(CHITTA)],
            input=req, capture_output=True, text=True, timeout=timeout,
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
    tag = realm.replace("/", "-")
    _rpc("forget", {"query": realm, "tag": tag, "cascade": "false"}, timeout=30)


# ── LLM helpers ───────────────────────────────────────────────────────────────

_haiku_available: Optional[bool] = None

def _haiku(system: str, user: str, max_tokens: int = 200) -> Optional[str]:
    global _haiku_available
    if _haiku_available is False:
        return None
    try:
        import anthropic
        client = anthropic.Anthropic()
        resp = client.messages.create(
            model="claude-haiku-4-5-20251001",
            max_tokens=max_tokens,
            system=system,
            messages=[{"role": "user", "content": user}],
        )
        _haiku_available = True
        return resp.content[0].text.strip()
    except Exception as e:
        msg = str(e)
        if "credit balance" in msg or "billing" in msg.lower() or "insufficient" in msg.lower():
            if _haiku_available is None:
                print("\n  [warn] Haiku unavailable (credits); falling back to heuristics", flush=True)
            _haiku_available = False
        return None


def synthesize(question: str, hits: str) -> str:
    """Return LLM answer, or best-effort extract from hits when Haiku unavailable."""
    if not hits.strip():
        return "I don't know"
    result = _haiku(
        "Answer using ONLY the memory excerpts provided. "
        "If the answer isn't there, say 'I don't know'. One sentence max.",
        f"Memories:\n{hits[:2000]}\n\nQuestion: {question}",
        max_tokens=150,
    )
    if result is not None:
        return result
    # Heuristic fallback: return first substantive content line (skip "Found N results" header)
    skip_prefixes = ("Found ", "No results", "Error", "[warn", "[rpc")
    for line in hits.splitlines():
        line = line.strip()
        if line and not any(line.startswith(p) for p in skip_prefixes):
            return line[:200]
    return "I don't know"


def judge_answer(question: str, gold: str, system_answer: str) -> bool:
    raw = _haiku(
        JUDGE_SYSTEM,
        f"Question: {question}\nGold: {gold}\nSystem: {system_answer}",
        max_tokens=150,
    )
    if raw is not None:
        try:
            return bool(json.loads(raw).get("correct", False))
        except Exception:
            pass
    # Fallback: token overlap ≥ 50%
    g = set(gold.lower().split())
    s = set(system_answer.lower().split())
    return len(g & s) / max(len(g), 1) >= 0.5


# ── main loop ─────────────────────────────────────────────────────────────────

def run(dataset: str, limit: int, keep_data: bool, dry_run: bool, verbose: bool):
    path = ensure_data(dataset)
    with open(path) as f:
        items = json.load(f)
    if limit:
        items = items[:limit]
    print(f"Loaded {len(items)} questions from {dataset}")

    RESULTS_DIR.mkdir(parents=True, exist_ok=True)

    scores: dict[str, list[bool]] = {qt: [] for qt in QUESTION_TYPES}
    latencies: list[float] = []
    rows: list[dict] = []

    for idx, item in enumerate(items):
        qid = item["question_id"]
        question = item["question"]
        gold = item["answer"]
        qtype = item["question_type"]
        dates = item.get("haystack_dates", [])
        sessions = item.get("haystack_sessions", [])
        realm = f"lme/{qid}"

        print(f"[{idx+1}/{len(items)}] {qtype} — {question[:65]}…", end="")

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
                text = f"[{turn['role']}] {content}"
                remember(text, realm=realm, tags=tags, valid_from=date)
                turns_ingested += 1

        # --- Recall ---
        t0 = time.perf_counter()
        hits = recall(question, realm=realm, limit=10)
        latency_ms = (time.perf_counter() - t0) * 1000
        latencies.append(latency_ms)

        # --- Synthesize + judge ---
        answer = synthesize(question, hits)
        correct = judge_answer(question, gold, answer)
        scores.setdefault(qtype, []).append(correct)

        print(f"  {'✓' if correct else '✗'}  {latency_ms:.0f}ms  turns={turns_ingested}")
        if verbose:
            print(f"  answer: {answer[:100]}")
            print(f"  gold:   {gold[:100]}")

        rows.append({
            "qid": qid, "qtype": qtype, "correct": correct,
            "latency_ms": round(latency_ms),
            "answer": answer[:250], "gold": gold[:250],
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
    print(f"  Overall: {overall:.1%}  ({total_correct}/{total_q})")
    print(f"  p50={p50:.0f}ms  p95={p95:.0f}ms")
    print(f"{'='*60}")
    for qt in QUESTION_TYPES:
        res = scores.get(qt, [])
        if res:
            print(f"  {qt:35s} {sum(res)/len(res):.1%}  ({sum(res)}/{len(res)})")

    # ── Markdown ──────────────────────────────────────────────────────────────
    out = RESULTS_DIR / f"longmemeval-{git_sha}.md"
    now = datetime.utcnow().strftime("%Y-%m-%d %H:%M UTC")
    md = [
        f"# LongMemEval — {git_sha}",
        f"",
        f"**Dataset**: `{dataset}`  **Date**: {now}  **n**: {total_q}",
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
    md.append("| qid | type | ✓ | ms | answer | gold |")
    md.append("|---|---|---|---|---|---|")
    for r in rows:
        a = r["answer"].replace("|", "\\|").replace("\n", " ")[:80]
        g = r["gold"].replace("|", "\\|").replace("\n", " ")[:80]
        md.append(f"| {r['qid']} | {r['qtype']} | {'✓' if r['correct'] else '✗'} | {r['latency_ms']} | {a} | {g} |")
    out.write_text("\n".join(md) + "\n")

    raw = out.with_suffix(".json")
    raw.write_text(json.dumps({"dataset": dataset, "sha": git_sha, "rows": rows}, indent=2))
    print(f"\nResults → {out}")


# ── CLI ───────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="LongMemEval benchmark for cc-soul")
    ap.add_argument("--dataset", default="longmemeval_oracle",
                    choices=list(DATASETS), metavar="DATASET",
                    help=f"One of: {', '.join(DATASETS)} (default: longmemeval_oracle)")
    ap.add_argument("--limit",     type=int, default=0,    help="Max questions (0=all)")
    ap.add_argument("--keep-data", action="store_true",    help="Keep ingested turns after each question")
    ap.add_argument("--dry-run",   action="store_true",    help="Inspect questions without running")
    ap.add_argument("--verbose",   "-v", action="store_true")
    args = ap.parse_args()

    if not CHITTA.exists():
        sys.exit(f"chitta not found at {CHITTA}")

    run(args.dataset, args.limit, args.keep_data, args.dry_run, args.verbose)


if __name__ == "__main__":
    main()
