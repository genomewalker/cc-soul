#!/usr/bin/env python3
"""
Fast LongMemEval comparison: ingest once, test chunk_dedup vs session_engram.

Uses CHUNK_SIZE=8192 (no overlap) to reduce ingestion from ~1100 chunks/question
to ~90 chunks/question (~12x faster). Tests both modes on same ingested data.
"""
import json, math, re, subprocess, sys, time
from pathlib import Path

CHITTA = Path.home() / ".claude/bin/chitta"
REALM_PREFIX = "lme-fast"
SESSION_PREFIX = "SESSION_ID:"
CHUNK_SIZE = 8192

def chitta_run(args):
    r = subprocess.run([str(CHITTA)] + args, capture_output=True, text=True)
    if r.returncode != 0 and r.stderr:
        print(f"[warn] {r.stderr.strip()[:200]}", file=sys.stderr)
    return r.stdout.strip()

def realm_for(qid):
    return f"{REALM_PREFIX}-{qid}"

def chunk_session(sess_id, turns):
    flat = "\n".join(
        f"{t.get('role','')}: {t.get('content','')}"
        for t in turns if t.get("content")
    )
    chunks = []
    for i in range(0, max(1, len(flat)), CHUNK_SIZE):
        chunks.append(f"{SESSION_PREFIX}{sess_id}\n{flat[i:i+CHUNK_SIZE]}")
        if i + CHUNK_SIZE >= len(flat):
            break
    return chunks

def ingest(qid, sessions, session_ids):
    realm = realm_for(qid)
    t0 = time.time()
    n = 0
    for sid, turns in zip(session_ids, sessions):
        for chunk in chunk_session(sid, turns):
            chitta_run(["remember", "--content", chunk, "--realm", realm,
                        "--type", "episode", "--source_session", sid])
            n += 1
    return n, time.time() - t0

def recall_chunk_dedup(qid, question, k):
    realm = realm_for(qid)
    raw = chitta_run(["recall", "--query", question, "--realm", realm,
                      "--limit", str(k * 5), "--json"])
    try:
        parsed = json.loads(raw) if raw else []
        hits = parsed.get("results", []) if isinstance(parsed, dict) else (parsed if isinstance(parsed, list) else [])
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

def recall_session_engram(qid, question, k):
    realm = realm_for(qid)
    raw = chitta_run(["recall_session", "--query", question, "--realm", realm,
                      "--limit", str(k)])
    ids = []
    for line in raw.splitlines():
        # Text format: "[76%] [2 chunks] <session_id>"
        m = re.match(r"\s*\[\d+%\]\s+\[\d+ chunks\]\s+(\S+)", line)
        if m:
            ids.append(m.group(1))
    return ids[:k]

def cleanup(qid):
    chitta_run(["forget_kind", "--kind", "episode", "--realm", realm_for(qid),
                "--limit", "5000"])

def r_at_k(retrieved, gold, k):
    return any(s in gold for s in retrieved[:k])

def mrr(retrieved, gold):
    for i, s in enumerate(retrieved):
        if s in gold:
            return 1.0 / (i + 1)
    return 0.0

def main():
    import argparse
    p = argparse.ArgumentParser()
    p.add_argument("--data", default="/projects/caeg/scratch/kbd606/tmp/longmemeval/longmemeval_s_cleaned.json")
    p.add_argument("--samples", type=int, default=5)
    p.add_argument("--out", default="/projects/caeg/scratch/kbd606/tmp/longmemeval/results_fast.json")
    args = p.parse_args()

    data = json.load(open(args.data))[:args.samples]
    K = [1, 3, 5, 10]
    modes = ["chunk_dedup", "session_engram"]
    metrics = {m: {f"R@{k}": [] for k in K} | {"MRR": []} for m in modes}
    per_q = []

    for i, q in enumerate(data):
        qid = q["question_id"]
        gold = set(q["answer_session_ids"])
        print(f"\n[{i+1}/{len(data)}] {qid}  gold={sorted(gold)}", flush=True)

        n_chunks, t_ingest = ingest(qid, q["haystack_sessions"], q["haystack_session_ids"])
        print(f"  ingested {n_chunks} chunks in {t_ingest:.1f}s", flush=True)

        row = {"question_id": qid, "gold": sorted(gold)}
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
            print(f"  {mode}: R@5={hits['R@5']}  R@10={hits['R@10']}  MRR={mrr_val:.2f}  top={retrieved[:3]}", flush=True)

        cleanup(qid)
        per_q.append(row)

    print("\n\n=== RESULTS ===")
    print(f"{'Metric':<10}", end="")
    for m in modes:
        print(f"  {m:<18}", end="")
    print()
    print("-" * 50)
    for k in K:
        key = f"R@{k}"
        print(f"{key:<10}", end="")
        for m in modes:
            v = sum(metrics[m][key]) / len(metrics[m][key])
            print(f"  {v:.4f}{'':14}", end="")
        print()
    print(f"{'MRR':<10}", end="")
    for m in modes:
        v = sum(metrics[m]["MRR"]) / len(metrics[m]["MRR"])
        print(f"  {v:.4f}{'':14}", end="")
    print()

    out = {"samples": len(data), "chunk_size": CHUNK_SIZE, "modes": {
        m: {k: sum(v)/len(v) for k, v in metrics[m].items()} for m in modes
    }, "per_question": per_q}
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    json.dump(out, open(args.out, "w"), indent=2)
    print(f"\nSaved: {args.out}")

if __name__ == "__main__":
    main()
