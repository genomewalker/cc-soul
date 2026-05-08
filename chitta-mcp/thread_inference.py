#!/usr/bin/env python3
"""
Thread inference: decide whether current session extends/creates/pivots/seals a thread.
Reads transcript JSONL, computes TF-IDF fingerprint, compares to existing threads.
Pure stdlib — no numpy/sklearn.
"""
import argparse
import json
import math
import re
import sys
import time
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from task_ledger import thread_list, thread_create, thread_seal, thread_update

_STOPWORDS = {
    "the", "a", "an", "and", "or", "but", "in", "on", "at", "to", "for",
    "of", "with", "by", "from", "is", "are", "was", "were", "be", "been",
    "have", "has", "had", "do", "does", "did", "will", "would", "could",
    "should", "may", "might", "can", "this", "that", "these", "those",
    "it", "its", "we", "i", "you", "they", "he", "she", "what", "how",
    "when", "where", "which", "who", "not", "no", "yes", "if", "then",
    "so", "just", "use", "run", "add", "get", "set", "let", "put", "make",
    "also", "now", "need", "want", "like", "as", "up", "out", "into",
    "please", "thanks", "ok", "okay", "sure", "yes", "good", "great",
    "can", "let", "please", "going", "here", "there", "then", "them",
}


def extract_user_turns(transcript_path: str, limit: int = 15) -> list[str]:
    texts: list[str] = []
    try:
        with open(transcript_path) as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    entry = json.loads(line)
                    if entry.get("type") != "user":
                        continue
                    msg = entry.get("message", {})
                    if isinstance(msg, dict):
                        for block in msg.get("content", []):
                            if isinstance(block, dict) and block.get("type") == "text":
                                texts.append(block.get("text", ""))
                    elif isinstance(msg, str):
                        texts.append(msg)
                except Exception:
                    pass
    except Exception:
        pass
    return texts[-limit:]


def tokenize(text: str) -> list[str]:
    words = re.findall(r"[a-zA-Z][a-zA-Z0-9_-]{2,}", text.lower())
    return [w for w in words if w not in _STOPWORDS]


def fingerprint(texts: list[str]) -> dict[str, float]:
    """Compute TF-IDF-like term weights across a list of user messages."""
    if not texts:
        return {}
    doc_tokens = [set(tokenize(t)) for t in texts]
    all_tokens = [tok for dt in doc_tokens for tok in dt]
    if not all_tokens:
        return {}
    tf = Counter(all_tokens)
    n = len(texts)
    total = len(all_tokens)
    result: dict[str, float] = {}
    for term, count in tf.items():
        df = sum(1 for dt in doc_tokens if term in dt)
        idf = math.log((n + 1) / (df + 1)) + 1
        result[term] = (count / total) * idf
    return result


def cosine(a: dict, b: dict) -> float:
    if not a or not b:
        return 0.0
    keys = set(a) & set(b)
    if not keys:
        return 0.0
    dot = sum(a[k] * b[k] for k in keys)
    na = math.sqrt(sum(v * v for v in a.values()))
    nb = math.sqrt(sum(v * v for v in b.values()))
    if na == 0 or nb == 0:
        return 0.0
    return dot / (na * nb)


def _title_from_fingerprint(fp: dict) -> str:
    top = sorted(fp.items(), key=lambda x: -x[1])[:5]
    return " ".join(w for w, _ in top) or "session"


def infer(transcript_path: str, realm: str, min_turns: int = 3) -> dict:
    texts = extract_user_turns(transcript_path, limit=15)
    if len(texts) < min_turns:
        return {"action": "skip", "reason": f"too few turns ({len(texts)}<{min_turns})"}

    fp = fingerprint(texts)
    fp_json = json.dumps(fp)
    title = _title_from_fingerprint(fp)
    now = time.time()

    active_threads = thread_list(realm=realm, status="active", limit=10)

    if not active_threads:
        tid = thread_create(title=title, realm=realm, fingerprint=fp_json)
        return {
            "action": "create",
            "thread_id": tid,
            "title": title,
            "top_terms": list(fp)[:8],
        }

    best_score = 0.0
    best_thread: dict | None = None
    for t in active_threads:
        try:
            stored_fp = json.loads(t.get("topic_fingerprint") or "{}")
        except Exception:
            stored_fp = {}
        score = cosine(fp, stored_fp)
        if score > best_score:
            best_score = score
            best_thread = t

    if best_score >= 0.55:
        thread_update(best_thread["thread_id"], last_active_at=now, topic_fingerprint=fp_json)
        return {
            "action": "extend",
            "thread_id": best_thread["thread_id"],
            "title": best_thread["title"],
            "score": round(best_score, 3),
        }

    if best_score >= 0.30:
        thread_seal(best_thread["thread_id"], reason=f"topic_pivot score={best_score:.2f}")
        tid = thread_create(title=title, realm=realm, fingerprint=fp_json,
                            parent=best_thread["thread_id"])
        return {
            "action": "pivot",
            "thread_id": tid,
            "sealed_id": best_thread["thread_id"],
            "title": title,
            "score": round(best_score, 3),
        }

    tid = thread_create(title=title, realm=realm, fingerprint=fp_json)
    return {
        "action": "create",
        "thread_id": tid,
        "title": title,
        "score": round(best_score, 3),
    }


def _cli() -> None:
    p = argparse.ArgumentParser(prog="thread_inference")
    p.add_argument("--transcript", required=True)
    p.add_argument("--realm", default="")
    p.add_argument("--min-turns", type=int, default=3, dest="min_turns")
    args = p.parse_args()
    result = infer(args.transcript, args.realm, args.min_turns)
    print(json.dumps(result, default=str))


if __name__ == "__main__":
    _cli()
