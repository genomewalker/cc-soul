# Recall rerank eval — Step 1 gate result (geometric damping: NO-GO)

`scripts/recall_rerank_eval.py` — the pre-registered gate the room agreed must
precede any rerank change (consolidation-redesign Step 2). Verdict: **NO-GO.
Do not ship the geometric damping `s_c − β·r_d` as a reranker on this corpus.**

## Setup
- Store: live `chitta.29df8e85` sidecars, bge-large-en-v1.5 / 1024-d, 141,332 memories.
- Query vectors = the docs' own stored embeddings (exact space match, self excluded);
  gold = a *different* judged-relevant memory → non-circular, isolates the
  hub-distractor failure mode damping targets.
- `.mu` sidecar == corpus mean (cos = 1.0000) → centered ranker is production-exact.
- 194 queries: 50 hub-stress (densest top-10 / highest mean r_d) + 8 strata × 18.
- TREC pooling of top-10 across raw / centered / centered+damped; blind graded
  0/1/2 judge via local `gemma3:27b` (2353 pairs). Judge audited: labels correct,
  not lenient — the 97%-relevant rate is real corpus density.

## Result (ALL, n=194)
| ranker | nDCG@10 | Recall@10 | MRR@10 | hub-share |
|---|---|---|---|---|
| raw | 0.980 | 0.843 | 0.921 | 7.1% |
| centered (prod) | 0.981 | 0.845 | 0.925 | 7.1% |
| damped β=0.5 m=40 | 0.979 | 0.850 | 0.932 | 4.1% |

Δ centered→damped (paired bootstrap 95% CI on ΔnDCG):
`ΔnDCG −0.002 [−0.006,+0.001]  ΔRecall +0.005  ΔMRR +0.007  Δhub −3.0pt`

Variant sweep (rerank of the judged pool, no re-judging):
| variant | ΔnDCG | ΔRecall | ΔMRR | Δhub |
|---|---|---|---|---|
| damp β0.25 | −0.0013 [−0.004,+0.001] | +0.003 | **+0.006** | −1.4 |
| damp β0.5  | −0.0023 [−0.006,+0.001] | +0.005 | **+0.007** | −3.0 |
| damp β1.0  | −0.0039 [−0.008,+0.000] | +0.005 | −0.006 (over-damp) | −3.1 |
| margin β0.5 | −0.0015 | +0.003 | −0.002 | −1.5 |
| margin β1.0 | −0.0021 | +0.005 | +0.000 | −2.3 |
| margin β2.0 | −0.0021 | +0.006 | +0.001 | −2.7 |

## Gate (3 pre-registered criteria)
- Recall@10 not down >1pt per stratum — **PASS** (never worse than −0.005; mostly up).
- top-1% hub slot-share strictly decreases — **PASS** (−3.0pt overall).
- nDCG@10 Δ≥0 in 95% CI (+1 SE on hub-stress) — **FAIL** (ALL CI lower −0.006 < 0;
  hub-stress only +0.002, within noise). Decisive criterion → **NO-GO**.

## Why
The top-k in a dense personal-memory store is **97% relevant (75% grade-2)** — there
is no irrelevant-hub pollution to remove. Hubness is geometrically real (N_k skew
~2–3) but the hubs are genuinely central relevant memories, not noise. The shipped
fixes (mean-centering + bounded-envelope + Platt + abstain) already pin nDCG at 0.98;
density-reranking has no headroom to improve it and risks ordering churn, while adding
the r_d cache + frozen-μ staleness law + nightly recenter. Not justified.

Even the margin-gated form (`s' = s_c − β·relu(r_d − s_c)`, demote only indiscriminate
hubs where r_d > s_c) recovers no nDCG — confirming it is not a tuning problem.

## Caveat / only thing that could overturn this
Queries here are in-corpus self-embeddings, not fresh text→bge embeddings. Real text
queries expose more anisotropy and *might* pull in more irrelevant hubs. The shipped
text-query spot checks (rpc_mutex 71% / nonsense 30% / uncovered 41%) showed no hub
pollution, consistent with this result — but a text-query eval (needs the live
embedder) is the only experiment that could change the verdict. Until then: ship
nothing further; the recall-quality core stands as-is.

Reproduce: `python3 scripts/recall_rerank_eval.py {prep,judge,score} --m 40 --beta 0.5`
(needs `$OLLAMA_HOST` with `gemma3:27b`).
