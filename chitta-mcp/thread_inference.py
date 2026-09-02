#!/usr/bin/env python3
"""
Thread inference: decide whether current session extends/creates/pivots/seals a thread.
Reads transcript JSONL, computes TF-IDF fingerprint, compares to existing threads.
Pure stdlib — no numpy/sklearn.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
import time
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from task_ledger import (
    lease_claim,
    lease_list,
    session_bind,
    thread_create,
    thread_list,
    thread_seal,
    thread_update,
)

_STOPWORDS = {
    "the",
    "a",
    "an",
    "and",
    "or",
    "but",
    "in",
    "on",
    "at",
    "to",
    "for",
    "of",
    "with",
    "by",
    "from",
    "is",
    "are",
    "was",
    "were",
    "be",
    "been",
    "have",
    "has",
    "had",
    "do",
    "does",
    "did",
    "will",
    "would",
    "could",
    "should",
    "may",
    "might",
    "can",
    "this",
    "that",
    "these",
    "those",
    "it",
    "its",
    "we",
    "i",
    "you",
    "they",
    "he",
    "she",
    "what",
    "how",
    "when",
    "where",
    "which",
    "who",
    "not",
    "no",
    "yes",
    "if",
    "then",
    "so",
    "just",
    "use",
    "run",
    "add",
    "get",
    "set",
    "let",
    "put",
    "make",
    "also",
    "now",
    "need",
    "want",
    "like",
    "as",
    "up",
    "out",
    "into",
    "please",
    "thanks",
    "ok",
    "okay",
    "sure",
    "good",
    "great",
    "going",
    "here",
    "there",
    "them",
}


def _clean_user_text(text: str) -> str:
    for tag in (
        "environment_context",
        "recommended_plugins",
        "system-reminder",
        "task-notification",
        "local-command-stdout",
        "local-command-stderr",
    ):
        text = re.sub(rf"<{tag}\b[^>]*>.*?</{tag}>", " ", text, flags=re.IGNORECASE | re.DOTALL)
    return re.sub(r"\s+", " ", text).strip()


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
                    entry_type = entry.get("type")
                    extracted: list[str] = []
                    if entry_type == "user":
                        msg = entry.get("message", {})
                        if isinstance(msg, dict):
                            content = msg.get("content", [])
                            if isinstance(content, str):
                                extracted.append(content)
                            elif isinstance(content, list):
                                for block in content:
                                    if isinstance(block, dict) and block.get("type") == "text":
                                        extracted.append(block.get("text", ""))
                        elif isinstance(msg, str):
                            extracted.append(msg)
                    elif entry_type == "response_item":
                        payload = entry.get("payload", {})
                        if payload.get("type") == "message" and payload.get("role") == "user":
                            for block in payload.get("content", []):
                                if isinstance(block, dict) and block.get("type") in (
                                    "text",
                                    "input_text",
                                    "output_text",
                                ):
                                    extracted.append(block.get("text", ""))
                    text = _clean_user_text("\n".join(extracted))
                    if text and text not in texts:
                        texts.append(text)
                except (json.JSONDecodeError, AttributeError, TypeError):
                    # Transcripts are JSONL written by a live process: a torn
                    # last line or an unexpected message shape is normal. Skip
                    # the line and keep reading.
                    pass
    except OSError:
        # Transcript missing or unreadable; callers treat an empty list as
        # "no recent user text to infer a thread from".
        pass
    return texts[-limit:]


def snapshot_user_turns(snapshot_path: str, limit: int = 15) -> list[str]:
    """Load the Stop hook's bounded user-turn history.

    Thread inference only needs the latest messages for its fingerprint.  The
    snapshot path avoids reopening a transcript that can be hundreds of MB.
    stop-transcript-snapshot.py's add_user() already runs every value through
    _clean_user_text before storing it, so re-cleaning here is redundant.
    """
    try:
        payload = json.loads(Path(snapshot_path).read_text())
    except (OSError, TypeError, ValueError):
        return []
    values = payload.get("user_turns", []) if isinstance(payload, dict) else []
    if not isinstance(values, list):
        return []
    texts: list[str] = []
    for value in values:
        if not isinstance(value, str) or not value:
            continue
        if value not in texts:
            texts.append(value)
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


def _bind_and_claim(
    result: dict,
    session_id: str,
    thread_id: str,
    client: str,
    project_dir: str,
    transcript_path: str,
) -> dict:
    if not session_id:
        return result
    session_bind(session_id, thread_id, client, project_dir, transcript_path)
    # Claiming is deliberately non-forcing: two live frontends may work in the
    # same repository, but they may not silently take over the same thread.
    result["lease"] = lease_claim(thread_id, session_id)
    return result


def infer(
    transcript_path: str,
    realm: str,
    min_turns: int = 3,
    session_id: str = "",
    client: str = "",
    project_dir: str = "",
    user_turns: list[str] | None = None,
) -> dict:
    texts = (
        user_turns[-15:]
        if user_turns is not None
        else extract_user_turns(transcript_path, limit=15)
    )
    if len(texts) < min_turns:
        return {"action": "skip", "reason": f"too few turns ({len(texts)}<{min_turns})"}

    fp = fingerprint(texts)
    fp_json = json.dumps(fp)
    title = _title_from_fingerprint(fp)
    now = time.time()

    active_threads = thread_list(realm=realm, status="active", limit=10)
    live_leases = {
        str(row.get("thread_id")): row
        for row in lease_list(active_only=True)
        if row.get("thread_id")
    }

    if not active_threads:
        tid = thread_create(title=title, realm=realm, fingerprint=fp_json)
        result = {
            "action": "create",
            "thread_id": tid,
            "title": title,
            "top_terms": list(fp)[:8],
        }
        return _bind_and_claim(result, session_id, tid, client, project_dir, transcript_path)

    best_score = 0.0
    best_thread: dict | None = None
    for t in active_threads:
        try:
            stored_fp = json.loads(t.get("topic_fingerprint") or "{}")
        except (json.JSONDecodeError, TypeError):
            # Thread stored before fingerprints existed, or with a corrupt one.
            # An empty fingerprint scores 0 and simply never wins.
            stored_fp = {}
        score = cosine(fp, stored_fp)
        if score > best_score:
            best_score = score
            best_thread = t

    if best_score >= 0.55:
        owner = str(live_leases.get(best_thread["thread_id"], {}).get("session_id") or "")
        if owner and owner != session_id:
            tid = thread_create(title=title, realm=realm, fingerprint=fp_json)
            result = {
                "action": "create",
                "reason": "matching_thread_locked",
                "thread_id": tid,
                "locked_thread_id": best_thread["thread_id"],
                "score": round(best_score, 3),
                "owner_session_id": owner,
            }
            return _bind_and_claim(result, session_id, tid, client, project_dir, transcript_path)
        thread_update(best_thread["thread_id"], last_active_at=now, topic_fingerprint=fp_json)
        result = {
            "action": "extend",
            "thread_id": best_thread["thread_id"],
            "title": best_thread["title"],
            "score": round(best_score, 3),
        }
        return _bind_and_claim(
            result, session_id, best_thread["thread_id"], client, project_dir, transcript_path
        )

    if best_score >= 0.30:
        owner = str(live_leases.get(best_thread["thread_id"], {}).get("session_id") or "")
        if owner and owner != session_id:
            # Similar work may proceed concurrently, but it becomes a distinct
            # thread rather than sealing or taking over the live owner's thread.
            tid = thread_create(title=title, realm=realm, fingerprint=fp_json)
            result = {
                "action": "create",
                "reason": "similar_thread_locked",
                "thread_id": tid,
                "locked_thread_id": best_thread["thread_id"],
                "owner_session_id": owner,
                "title": title,
                "score": round(best_score, 3),
            }
            return _bind_and_claim(result, session_id, tid, client, project_dir, transcript_path)
        thread_seal(best_thread["thread_id"], reason=f"topic_pivot score={best_score:.2f}")
        tid = thread_create(
            title=title, realm=realm, fingerprint=fp_json, parent=best_thread["thread_id"]
        )
        result = {
            "action": "pivot",
            "thread_id": tid,
            "sealed_id": best_thread["thread_id"],
            "title": title,
            "score": round(best_score, 3),
        }
        return _bind_and_claim(result, session_id, tid, client, project_dir, transcript_path)

    tid = thread_create(title=title, realm=realm, fingerprint=fp_json)
    result = {
        "action": "create",
        "thread_id": tid,
        "title": title,
        "score": round(best_score, 3),
    }
    return _bind_and_claim(result, session_id, tid, client, project_dir, transcript_path)


def _cli() -> None:
    p = argparse.ArgumentParser(prog="thread_inference")
    p.add_argument("--transcript", required=True)
    p.add_argument("--realm", default="")
    p.add_argument("--min-turns", type=int, default=3, dest="min_turns")
    p.add_argument("--session-id", default="", dest="session_id")
    p.add_argument("--client", default="")
    p.add_argument("--project-dir", default="", dest="project_dir")
    p.add_argument("--snapshot", default="")
    args = p.parse_args()
    user_turns = snapshot_user_turns(args.snapshot) if args.snapshot else None
    result = infer(
        args.transcript,
        args.realm,
        args.min_turns,
        args.session_id,
        args.client,
        args.project_dir,
        user_turns,
    )
    print(json.dumps(result, default=str))


if __name__ == "__main__":
    _cli()
