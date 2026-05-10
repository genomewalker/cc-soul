#!/usr/bin/env python3
"""
LongMemEval distillation benchmark.

For each question, distils each haystack session via LLM (SSL v0.3) then
imports the memories with import_soul --realm --source_session.
Tests both chunk_dedup and session_engram recall on the ingested memories.

Usage:
    python bench_distill.py [--data FILE] [--samples N] [--model MODEL]
                            [--out FILE] [--workers N] [--no-cleanup]
"""
import json, os, re, subprocess, sys, time, tempfile, threading
from pathlib import Path
from urllib.request import urlopen, Request
from urllib.error import URLError

CHITTA = Path.home() / ".claude/bin/chitta"
REALM_PREFIX = "lme-distill"
SESSION_PREFIX = "SESSION_ID:"

SSL_SYSTEM = (
    "You are a knowledge distiller. Extract learnings in SSL v0.3 format. "
    "Output ONLY SSL-formatted lines. No preamble, no explanation."
)

SSL_PROMPT_SUFFIX = (
    "\n\n---\n\nOutput ONLY SSL-formatted learnings, one per line. "
    "Minimum 3, maximum 15 lines. Each line must start with a type tag "
    "like [SOLUTION], [DECISION], [PREFERENCE], [GOTCHA], [PATTERN], "
    "[FAILURE], [CORRECTION], [EVENT], [BELIEF], or [OPERATIONAL]."
)

# Read the extraction prompt from the header if available, else inline a compact version
_PROMPT_HEADER_PATH = (
    Path(__file__).parents[2]
    / "chitta/include/chitta/ssl_prompt.hpp"
)

def _load_extraction_prompt() -> str:
    try:
        text = _PROMPT_HEADER_PATH.read_text()
        m = re.search(r'constexpr const char\* EXTRACTION_PROMPT = R"\((.+?)\)";',
                      text, re.DOTALL)
        if m:
            return m.group(1).strip()
    except Exception:
        pass
    return (
        "Extract learnings from this conversation in SSL v0.3 format.\n\n"
        "Types: [SOLUTION], [DECISION], [PREFERENCE], [GOTCHA], [PATTERN], "
        "[FAILURE], [CORRECTION], [EVENT], [BELIEF], [OPERATIONAL]\n\n"
        "Emit 3–15 lines. Each line starts with a type tag followed by the content."
    )

EXTRACTION_PROMPT = _load_extraction_prompt()


# ── Endpoint discovery ────────────────────────────────────────────────────────

def _probe(url: str) -> bool:
    try:
        req = Request(f"{url}/v1/models", headers={"Accept": "application/json"})
        with urlopen(req, timeout=4) as r:
            return b"data" in r.read(512)
    except Exception:
        return False


def find_endpoint() -> str | None:
    candidates = []
    for f in Path("/tmp").glob("ollama-server-*.url"):
        url = f.read_text().strip()
        if url:
            candidates.append(url)
    candidates.append("http://localhost:11434")
    seen = set()
    for url in candidates:
        if url in seen:
            continue
        seen.add(url)
        if _probe(url):
            return url
    return None


# ── LLM distillation ─────────────────────────────────────────────────────────

def _format_turns(session_id: str, turns: list) -> str:
    lines = [f"[Session: {session_id}]"]
    for t in turns:
        role = t.get("role", "")
        content = t.get("content", "")
        if not content:
            continue
        # Truncate very long turns to keep within context
        if len(content) > 3000:
            content = content[:3000] + "…"
        lines.append(f"{role}: {content}")
    return "\n".join(lines)


def distil_session(endpoint: str, model: str, session_id: str, turns: list) -> str:
    """Call LLM with SSL extraction prompt; return raw SSL text."""
    conversation = _format_turns(session_id, turns)
    user_content = EXTRACTION_PROMPT + "\n\n" + conversation + SSL_PROMPT_SUFFIX

    payload = json.dumps({
        "model": model,
        "messages": [
            {"role": "system", "content": SSL_SYSTEM},
            {"role": "user",   "content": user_content},
        ],
        "temperature": 0.3,
        "max_tokens": 2048,
    }).encode()

    req = Request(
        f"{endpoint}/v1/chat/completions",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urlopen(req, timeout=120) as r:
        resp = json.loads(r.read())
    return resp["choices"][0]["message"]["content"]


def parse_ssl_lines(ssl_text: str) -> list[str]:
    """Return lines that are valid SSL entries (start with [TYPE])."""
    types = r"SOLUTION|GOTCHA|DECISION|PATTERN|PREFERENCE|FAILURE|CORRECTION|EVENT|BELIEF|OPERATIONAL"
    out = []
    for line in ssl_text.splitlines():
        line = line.strip()
        if re.match(rf"\[(?:{types})\]", line):
            out.append(line)
    return out


# ── chitta helpers ────────────────────────────────────────────────────────────

def chitta_run(args: list[str]) -> str:
    r = subprocess.run([str(CHITTA)] + args, capture_output=True, text=True)
    if r.returncode != 0 and r.stderr:
        print(f"[warn] {r.stderr.strip()[:200]}", file=sys.stderr)
    return r.stdout.strip()


def realm_for(qid: str) -> str:
    return f"{REALM_PREFIX}-{qid}"


def ingest_session(session_id: str, turns: list, realm: str,
                   endpoint: str, model: str) -> int:
    """Distil one session and import via import_soul. Returns memory count."""
    try:
        ssl_text = distil_session(endpoint, model, session_id, turns)
    except Exception as e:
        print(f"    [warn] LLM failed for {session_id}: {e}", file=sys.stderr)
        return 0

    lines = parse_ssl_lines(ssl_text)
    if not lines:
        return 0

    content = "\n".join(lines)
    chitta_run([
        "import_soul",
        "--content", content,
        "--realm", realm,
        "--source_session", session_id,
        "--confidence", "0.8",
    ])
    return len(lines)


def ingest_all(qid: str, sessions: list, session_ids: list,
               endpoint: str, model: str, workers: int = 1) -> tuple[int, float]:
    """Ingest all sessions for a question. Returns (total_memories, elapsed_s)."""
    realm = realm_for(qid)
    t0 = time.time()
    total = 0
    lock = threading.Lock()

    def _worker(items):
        nonlocal total
        for sid, turns in items:
            n = ingest_session(sid, turns, realm, endpoint, model)
            with lock:
                total += n

    pairs = list(zip(session_ids, sessions))
    if workers <= 1:
        _worker(pairs)
    else:
        chunk = max(1, len(pairs) // workers)
        threads = []
        for i in range(0, len(pairs), chunk):
            t = threading.Thread(target=_worker, args=(pairs[i:i+chunk],))
            t.start()
            threads.append(t)
        for t in threads:
            t.join()

    return total, time.time() - t0


# ── Recall ────────────────────────────────────────────────────────────────────

def recall_chunk_dedup(qid: str, question: str, k: int) -> list[str]:
    realm = realm_for(qid)
    raw = chitta_run(["recall", "--query", question, "--realm", realm,
                      "--limit", str(k * 5), "--json"])
    try:
        parsed = json.loads(raw) if raw else []
        hits = (parsed.get("results", []) if isinstance(parsed, dict)
                else (parsed if isinstance(parsed, list) else []))
    except Exception:
        hits = []
    seen, ids = set(), []
    for h in hits:
        text = h.get("text", "")
        m = re.search(rf"{re.escape(SESSION_PREFIX)}(\S+)", text)
        if m:
            sid = m.group(1)
            if sid not in seen:
                seen.add(sid)
                ids.append(sid)
        if len(ids) >= k:
            break
    return ids


def recall_session_engram(qid: str, question: str, k: int) -> list[str]:
    realm = realm_for(qid)
    raw = chitta_run(["recall_session", "--query", question,
                      "--realm", realm, "--limit", str(k)])
    ids = []
    for line in raw.splitlines():
        m = re.match(r"\s*\[\d+%\]\s+\[\d+ chunks\]\s+(\S+)", line)
        if m:
            ids.append(m.group(1))
    return ids[:k]


def cleanup(qid: str):
    chitta_run(["forget_kind", "--kind", "wisdom", "--realm", realm_for(qid),
                "--limit", "5000"])


# ── Metrics ───────────────────────────────────────────────────────────────────

def r_at_k(retrieved: list, gold: set, k: int) -> bool:
    return any(s in gold for s in retrieved[:k])


def mrr(retrieved: list, gold: set) -> float:
    for i, s in enumerate(retrieved):
        if s in gold:
            return 1.0 / (i + 1)
    return 0.0


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    import argparse
    p = argparse.ArgumentParser()
    p.add_argument("--data", default=(
        "/projects/caeg/scratch/kbd606/tmp/longmemeval/longmemeval_s_cleaned.json"
    ))
    p.add_argument("--samples",  type=int, default=5)
    p.add_argument("--model",    default="gemma4:27b")
    p.add_argument("--out",      default=(
        "/projects/caeg/scratch/kbd606/tmp/longmemeval/results_distill.json"
    ))
    p.add_argument("--workers",  type=int, default=1,
                   help="Parallel session ingestion threads")
    p.add_argument("--no-cleanup", action="store_true",
                   help="Keep realm after benchmark (for debugging)")
    args = p.parse_args()

    endpoint = find_endpoint()
    if endpoint is None:
        print("ERROR: No reachable Ollama endpoint found.", file=sys.stderr)
        print("Start with: chitta-gpu start gemma4:27b", file=sys.stderr)
        sys.exit(1)
    print(f"[distill] Endpoint: {endpoint}  Model: {args.model}")

    data = json.load(open(args.data))[:args.samples]
    K = [1, 3, 5, 10]
    modes = ["chunk_dedup", "session_engram"]
    metrics = {m: {f"R@{k}": [] for k in K} | {"MRR": []} for m in modes}
    per_q = []

    for i, q in enumerate(data):
        qid = q["question_id"]
        gold = set(q["answer_session_ids"])
        n_sess = len(q["haystack_sessions"])
        print(f"\n[{i+1}/{len(data)}] {qid}  gold={sorted(gold)}  sessions={n_sess}",
              flush=True)

        n_mems, t_ingest = ingest_all(
            qid, q["haystack_sessions"], q["haystack_session_ids"],
            endpoint, args.model, workers=args.workers,
        )
        print(f"  ingested {n_mems} memories in {t_ingest:.1f}s "
              f"({t_ingest/n_sess:.1f}s/session)", flush=True)

        if n_mems == 0:
            print("  [skip] no memories ingested", flush=True)
            cleanup(qid)
            continue

        row = {"question_id": qid, "gold": sorted(gold),
               "n_memories": n_mems, "ingest_s": round(t_ingest, 1)}

        for mode in modes:
            if mode == "chunk_dedup":
                retrieved = recall_chunk_dedup(qid, q["question"], max(K))
            else:
                retrieved = recall_session_engram(qid, q["question"], max(K))

            hits = {f"R@{k}": r_at_k(retrieved, gold, k) for k in K}
            mrr_val = mrr(retrieved, gold)
            for k in K:
                metrics[mode][f"R@{k}"].append(float(hits[f"R@{k}"]))
            metrics[mode]["MRR"].append(mrr_val)
            row[mode] = {"retrieved": retrieved[:10], **hits, "MRR": mrr_val}
            print(f"  {mode}: R@5={hits['R@5']}  R@10={hits['R@10']}  "
                  f"MRR={mrr_val:.2f}  top={retrieved[:3]}", flush=True)

        if not args.no_cleanup:
            cleanup(qid)
        per_q.append(row)

    if not per_q:
        print("\nNo questions processed.")
        return

    print("\n\n=== RESULTS ===")
    print(f"{'Metric':<10}", end="")
    for m in modes:
        print(f"  {m:<20}", end="")
    print()
    print("-" * 52)
    for k in K:
        key = f"R@{k}"
        print(f"{key:<10}", end="")
        for m in modes:
            v = sum(metrics[m][key]) / max(1, len(metrics[m][key]))
            print(f"  {v:.4f}{'':16}", end="")
        print()
    print(f"{'MRR':<10}", end="")
    for m in modes:
        v = sum(metrics[m]["MRR"]) / max(1, len(metrics[m]["MRR"]))
        print(f"  {v:.4f}{'':16}", end="")
    print()

    out = {
        "samples": len(per_q),
        "model": args.model,
        "endpoint": endpoint,
        "modes": {
            m: {k: sum(v) / max(1, len(v)) for k, v in metrics[m].items()}
            for m in modes
        },
        "per_question": per_q,
    }
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    json.dump(out, open(args.out, "w"), indent=2)
    print(f"\nSaved: {args.out}")


if __name__ == "__main__":
    main()
