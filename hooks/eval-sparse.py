#!/usr/bin/env python3
"""Offline sparse-retrieval evaluator — the go/no-go gate for the GPU-free paradigm.

Tasks 4/5 (promote BM25/atom to lane-0, demote GPU dense to async rescue) are only
safe if sparse lanes alone recover the relevant memories on the retrieval path the
prod hook actually runs: REALM-SCOPED recall over REAL queries. This measures exactly
that, offline over mems.json against the harvested labels — no daemon, no GPU, no
graph pollution. For each real (query -> {relevant_id}) group, restricted to the
relevant memory's realm, it computes recall@k for:
  bm25   : Okapi BM25 over content tokens
  atom   : exact rare-atom (df<=ATOM_DF within realm) overlap, the bridge lane
  union  : reciprocal-rank fusion of the two
mems.json carries no embeddings, so the dense baseline must come from the live daemon
separately — this bounds only the sparse side. High realm-scoped union recall is the
green light to make dense lazy; low means dense earns its keep.

Usage: eval-sparse.py <mems.json> <labels.jsonl> [k=10]
"""

import collections
import json
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from importlib import import_module

atoms = import_module("harvest-labels").atoms  # reuse the exact atom extractor

_word = re.compile(r"[a-z0-9_]{2,}")


def toks(s):
    return _word.findall(s.lower())


_NOISE_PREFIX = ("{", "[", "<", "tool_result", "result of", "✻", "⎿")


def is_real_query(q):
    """Drop conversational fragments and tool/notification turns — not retrieval probes."""
    s = q.strip()
    if len(s) < 25:
        return False
    if s.lower().startswith(_NOISE_PREFIX):
        return False
    if len(set(toks(s))) < 4:
        return False
    return True


class RealmIndex:
    """BM25 + rare-atom index over one realm's documents."""

    def __init__(self, docs):  # docs: list[(mem_id, content)]
        self.ids = [d[0] for d in docs]
        cont = [d[1] for d in docs]
        self.N = len(cont)
        self.doc_tok = [toks(c) for c in cont]
        self.dl = [len(t) for t in self.doc_tok]
        self.avgdl = sum(self.dl) / max(self.N, 1)
        self.postings = collections.defaultdict(list)
        for d, ts in enumerate(self.doc_tok):
            for term, tf in collections.Counter(ts).items():
                self.postings[term].append((d, tf))
        self.idf = {
            t: math.log(1 + (self.N - len(p) + 0.5) / (len(p) + 0.5))
            for t, p in self.postings.items()
        }
        atom_df = int(os.environ.get("ATOM_DF", "8"))
        ai = collections.defaultdict(set)
        for d, c in enumerate(cont):
            for a in atoms(c):
                ai[a].add(d)
        self.atom_idx = {a: s for a, s in ai.items() if len(s) <= atom_df}
        self.k1, self.b = 1.5, 0.75

    def bm25(self, q, k):
        sc = collections.defaultdict(float)
        for term in set(toks(q)):
            p = self.postings.get(term)
            if not p:
                continue
            w = self.idf[term]
            for d, tf in p:
                sc[d] += (
                    w
                    * (tf * (self.k1 + 1))
                    / (tf + self.k1 * (1 - self.b + self.b * self.dl[d] / self.avgdl))
                )
        return [self.ids[d] for d, _ in sorted(sc.items(), key=lambda x: -x[1])[:k]]

    def atom(self, q, k):
        sc = collections.Counter()
        for a in atoms(q):
            for d in self.atom_idx.get(a, ()):
                sc[d] += 1
        return [self.ids[d] for d, _ in sc.most_common(k)]

    def union(self, q, k):
        rr = collections.defaultdict(float)
        for lst in (self.bm25(q, k), self.atom(q, k)):
            for r, mid in enumerate(lst):
                rr[mid] = max(rr[mid], 1.0 / (r + 1))
        return [mid for mid, _ in sorted(rr.items(), key=lambda x: -x[1])[:k]]


def main():
    mems_path, labels_path = sys.argv[1], sys.argv[2]
    k = int(sys.argv[3]) if len(sys.argv) > 3 else 10

    M = json.load(open(mems_path))
    realm_of = {i: (v.get("realm") if isinstance(v, dict) else None) for i, v in M.items()}
    content_of = {
        i: ((v.get("content") or "") if isinstance(v, dict) else str(v or "")) for i, v in M.items()
    }

    # group labels by query, keep only real queries whose relevant ids are in the corpus
    groups = collections.defaultdict(set)
    for line in open(labels_path):
        L = json.loads(line)
        if L["relevant_id"] in M and is_real_query(L["query"]):
            groups[L["query"]].add(L["relevant_id"])
    if not groups:
        sys.exit("no real-query labels whose relevant_id is in mems.json")

    # realms we actually need, and per-realm doc buckets (built once, cached)
    need = set()
    for rel in groups.values():
        need |= {realm_of[i] for i in rel}
    bucket = collections.defaultdict(list)
    for i, r in realm_of.items():
        if r in need:
            bucket[r].append((i, content_of[i]))
    index = {r: RealmIndex(docs) for r, docs in bucket.items()}

    hits = {"bm25": 0, "atom": 0, "union": 0}
    tot = 0
    corpus_sizes = []
    for q, rel in groups.items():
        # scope to the majority realm of this query's relevant memories
        realm = collections.Counter(realm_of[i] for i in rel).most_common(1)[0][0]
        idx = index[realm]
        rel_in = {i for i in rel if realm_of[i] == realm}
        if not rel_in:
            continue
        tot += 1
        corpus_sizes.append(idx.N)
        for name in ("bm25", "atom", "union"):
            if rel_in & set(getattr(idx, name)(q, k)):
                hits[name] += 1

    med = sorted(corpus_sizes)[len(corpus_sizes) // 2] if corpus_sizes else 0
    print(f"real-query groups={tot}  k={k}  median realm corpus={med}  realms={sorted(need)}")
    for name in ("bm25", "atom", "union"):
        print(f"  recall@{k} {name:5} = {hits[name] / tot:.3f}  ({hits[name]}/{tot})")


if __name__ == "__main__":
    main()
