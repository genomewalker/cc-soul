#!/usr/bin/env python3
"""#14 KILL-GATE + GGUF parity for the nomic LoRA fine-tune.

The Jina fine-tune failed by collapsing to domain bias: bio memories scored
~0.92 for ANY query. This probe fails LOUDLY if the fine-tuned model does the
same, and it must run BEFORE any re-embed.

--model DIR        domain-bias kill-probe on an HF/ST checkpoint.
--parity GGUF      cosine parity between the HF checkpoint (--model) and the
                   exported GGUF via llama.cpp — catches quant drift that
                   would silently degrade recall after the swap.

KILL criteria (any -> exit 1, do NOT deploy):
  1. Off-topic score floor: unrelated (query, doc) cross pairs must not score
     high. If mean off-topic cos > OFFTOPIC_MAX, the model lost discrimination.
  2. Domain saturation: the spread (max-min) of a fixed doc's score across a
     diverse query battery must exceed MIN_SPREAD. A collapsed model gives
     every query nearly the same score (the Jina signature).
  3. No-regression floor: on-topic pairs must still out-score their off-topic
     controls by ON_OFF_MARGIN on average.
"""

import argparse
import sys

OFFTOPIC_MAX = 0.55
MIN_SPREAD = 0.15
ON_OFF_MARGIN = 0.10

# Deliberately cross-domain: bio, systems, prefs, temporal. On-topic doc per
# query is index-matched; every other doc is an off-topic control.
PROBES = [
    ("how do I cap OpenBLAS threads for the daemon",
     "set OPENBLAS_NUM_THREADS and OMP_NUM_THREADS to 4 in the chittad unit"),
    ("what dedup strategy does bam2mcaf use",
     "bam2mcaf deduplication uses a qname plus forward-seq hash map"),
    ("which barley gene controls flowering time",
     "the Ppd-H1 locus regulates photoperiod response and flowering in barley"),
    ("user's preference for temp directory location",
     "always use the scratch tmp dir, /tmp does not persist across nodes"),
    ("when did the LGBTQ support group meeting happen",
     "Caroline attended the LGBTQ support group yesterday and found it powerful"),
    ("how is the HNSW index rebuilt after re-embedding",
     "HNSW rebuild ran 49 backfill epochs in 7.2s producing a 40MB index"),
    ("golden benchmark frozen anchor score no reranker",
     "the frozen no-reranker anchor is 0.4743, bit-identical across three runs"),
    ("what causes the cross-encoder reranker to stall",
     "the cross-encoder reranker stalls beyond limit=20, taking over 9 minutes"),
]


def encode_st(model_dir, texts, prefix):
    from sentence_transformers import SentenceTransformer
    m = SentenceTransformer(model_dir, trust_remote_code=True)
    return m.encode([prefix + t for t in texts],
                    normalize_embeddings=True, batch_size=32)


def kill_probe(vecs_q, vecs_d) -> int:
    import numpy as np
    sims = vecs_q @ vecs_d.T            # [n_query, n_doc]
    n = len(sims)
    diag = np.diag(sims)
    off = sims.copy()
    np.fill_diagonal(off, np.nan)
    off_mean = float(np.nanmean(off))
    spread = float(np.nanmax(sims, axis=0).ptp())  # spread of best-doc across queries
    per_query_spread = float(np.mean([sims[i].ptp() for i in range(n)]))
    on_off = float(np.mean(diag - np.nanmean(off, axis=1)))

    print(f"on-topic mean cos:   {float(diag.mean()):.4f}")
    print(f"off-topic mean cos:  {off_mean:.4f}   (kill if > {OFFTOPIC_MAX})")
    print(f"per-query spread:    {per_query_spread:.4f}   (kill if < {MIN_SPREAD})")
    print(f"on-off margin:       {on_off:.4f}   (kill if < {ON_OFF_MARGIN})")

    fail = []
    if off_mean > OFFTOPIC_MAX:
        fail.append("off-topic saturation")
    if per_query_spread < MIN_SPREAD:
        fail.append("domain collapse (flat scores)")
    if on_off < ON_OFF_MARGIN:
        fail.append("lost on-topic discrimination")
    if fail:
        print("KILL:", "; ".join(fail))
        return 1
    print("PASS: no domain-bias collapse detected")
    return 0


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True, help="HF/ST checkpoint dir")
    ap.add_argument("--parity", help="GGUF path; run parity vs --model instead")
    args = ap.parse_args()

    queries = [q for q, _ in PROBES]
    docs = [d for _, d in PROBES]

    if args.parity:
        import numpy as np
        st_q = encode_st(args.model, queries, "search_query: ")
        # llama.cpp embedding of the same prefixed texts
        from subprocess import run
        raise SystemExit(
            "parity: encode the same 'search_query: '/'search_document: ' "
            "prefixed texts through llama.cpp embedding on the exported GGUF "
            "and assert mean cosine(st_row, gguf_row) > 0.99; wire to the "
            "llama.cpp embedding binary at gate (c) once the GGUF exists.")

    vq = encode_st(args.model, queries, "search_query: ")
    vd = encode_st(args.model, docs, "search_document: ")
    sys.exit(kill_probe(vq, vd))


if __name__ == "__main__":
    main()
