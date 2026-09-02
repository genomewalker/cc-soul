#!/usr/bin/env python3
"""Weak-supervision label harvester for the recall/admission calibration.

The gold sets (hooks/*goldids*.json) have n<=3 queries, so any learned gate or
per-lane Platt fit is DOA. This mines hundreds of (query -> relevant-memory) pairs
from session transcripts already on disk, at zero GPU cost: for each user turn, take
the distinctive atoms the assistant's reply introduced (paths/hashes/accessions/
uuids/identifiers NOT already in the question), and map them to memories that contain
the same atom. Co-occurrence of a rare atom in both the reply and a memory is weak
evidence that memory was the source — not truth. Below ~500 labels use these to
validate monotone rules (rare-atom override, BM25 margin); above, to fit a gate.

Usage: harvest-labels.py <mems.json> "<transcripts_glob>" [out.jsonl]
  mems.json      : {mem_id: {"content": ...}} or {mem_id: "content"}
  transcripts    : glob of Claude Code *.jsonl session files
"""

import collections
import glob
import json
import os
import re
import statistics
import sys

# Atom extractors mirror chitta-field/src/organ/artifact.rs and bench/atom_mem_profile.py:
# paths, content hashes, accessions, uuids, snake/camel identifiers. Deterministic, no GPU.
_kv = re.compile(r"(?:input|output|out|log|path):\s*([^\s|,;]+)")
_abs = re.compile(r"(/[\w./\-]{12,})")
_urlish = re.compile(r"^//|(?:^|/)(?:www\.|[\w-]+\.(?:org|com|net|io|gov|edu)/)")
_uuid = re.compile(r"\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b")
_hex = re.compile(r"\b[0-9a-f]{8,64}\b")
_acc = re.compile(r"\b(?:GC[AF]_\d{9}\.\d+|[A-Z]{2,6}\d{4,}(?:\.\d+)?)\b")
_snake = re.compile(r"\b[a-z][a-z0-9]*(?:_[a-z0-9]+){1,}\b")
_camel = re.compile(r"\b[A-Z][a-z]+(?:[A-Z][a-z]+){1,}\b")


def _hashish(t):
    return any(c in "abcdef" for c in t) and any(c.isdigit() for c in t)


def _valid_abs(p):
    if _urlish.search(p):
        return False
    b = p[1:]
    return "/" in b or "." in b or any(c.isdigit() for c in b)


def atoms(c):
    out = set()
    for m in _kv.finditer(c):
        p = m.group(1)
        if len(p) >= 8 and "/" in p and not _urlish.search(p):
            out.add(p)
    for p in _abs.findall(c):
        if len(p) >= 12 and _valid_abs(p):
            out.add(p)
    out |= set(_uuid.findall(c))
    out |= {m for m in _hex.findall(c) if _hashish(m)}
    out |= set(_acc.findall(c))
    out |= {m for m in _snake.findall(c) if len(m) >= 8}
    out |= {m for m in _camel.findall(c) if len(m) >= 8}
    return out


def load_mems(path):
    M = json.load(open(path))

    def content(v):
        return (v.get("content") or "") if isinstance(v, dict) else str(v or "")

    return {k: content(v) for k, v in M.items()}


def _text(msg):
    c = msg.get("content") if isinstance(msg, dict) else None
    if isinstance(c, str):
        return c
    if isinstance(c, list):
        return " ".join(
            b.get("text", "") for b in c if isinstance(b, dict) and b.get("type") == "text"
        )
    return ""


def turns(path):
    """Yield (user_text, assistant_text) for each adjacent user->assistant pair."""
    pending = None
    for line in open(path):
        line = line.strip()
        if not line:
            continue
        try:
            r = json.loads(line)
        except json.JSONDecodeError:
            continue
        m = r.get("message") or r
        role = m.get("role") or r.get("type")
        if role in ("user", "human"):
            pending = _text(m)
        elif role == "assistant" and pending is not None:
            yield pending, _text(m)
            pending = None


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    mems_path, tglob = sys.argv[1], sys.argv[2]
    out_path = sys.argv[3] if len(sys.argv) > 3 else "labels.jsonl"

    # An atom is useful supervision only when RARE: a hash/path present in one memory
    # pins that memory; a common identifier in hundreds is noise (the bridge lane's df
    # gate, same logic). Skip atoms with document frequency > MAX_DF.
    max_df = int(os.environ.get("HARVEST_MAX_DF", "4"))

    mems = load_mems(mems_path)
    idx = collections.defaultdict(set)  # atom -> {mem_id}
    for mid, c in mems.items():
        for a in atoms(c):
            idx[a].add(mid)

    labels, n_turns = [], 0
    for tf in glob.glob(tglob):
        for q, a in turns(tf):
            n_turns += 1
            if not q or not a:
                continue
            reply_only = atoms(a) - atoms(q)  # atoms the assistant INTRODUCED
            hit = collections.defaultdict(set)  # mem_id -> evidence atoms
            for atom in reply_only:
                mids = idx.get(atom, ())
                if not mids or len(mids) > max_df:
                    continue  # unknown or too common
                for mid in mids:
                    hit[mid].add(atom)
            for mid, ats in hit.items():
                labels.append(
                    {
                        "query": q[:500],
                        "relevant_id": mid,
                        "evidence_atoms": sorted(ats)[:8],
                        "source": tf.rsplit("/", 1)[-1],
                    }
                )

    with open(out_path, "w") as f:
        for L in labels:
            f.write(json.dumps(L) + "\n")

    qs = len({L["query"] for L in labels})
    ms = len({L["relevant_id"] for L in labels})
    print(f"turns={n_turns}  labels={len(labels)}  distinct_queries={qs}  distinct_mems={ms}")
    print(f"atom_index={len(idx)} atoms over {len(mems)} memories  ->  {out_path}")
    if labels:
        per_q = collections.Counter(L["query"] for L in labels)
        print(
            f"labels/query: median={statistics.median(per_q.values()):.0f} "
            f"max={max(per_q.values())}"
        )


if __name__ == "__main__":
    main()
