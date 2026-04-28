# Outlier handler audit (compact / trajectory_compact / field_lookup)

Date: 2026-04-28
Scope: the three handlers flagged by `2026-04-28-handler-metrics.md` as
unclassified real-logic outliers (~820 LOC combined).

## What's in them

### compact.hpp — 286 LOC
`tool_compact_context`. Memory-aware message pruning for conversation
windows. Phases: (1) score each turn (recency + semantic_sim(query) +
1 − memory_coverage), (2) greedy keep by token budget, system always
kept, (3) optional distill of novel drops into soul memories, (4)
return kept verbatim + stats.

External deps: `yantra_->embed`, `field_store_->recall` (for
memory_coverage), `field_store_->remember` (for distill).

### trajectory_compact.hpp — 339 LOC
`tool_trajectory_compact`. Attention-weighted turn selection from a
session transcript for downstream agent context transfer.
Algorithm: parse jsonl → embed task → cosine sim per turn → **MAD
adaptive threshold** (median + k·MAD, k=1.5) → token budget →
chronological return. Cites Ramp Labs latent briefing + Leys 2013 MAD.

External deps: `yantra_->embed`, jsonl file IO.

### field_lookup.hpp — 243 LOC
`tool_lookup`. Unified lookup tool. Intent classification
(`QueryIntentClassifier`) → 5-backend weighted RRF fusion:
{semantic, keyword, triplet, temporal, code} with intent-driven
weight presets and early-exit at keyword score ≥ 0.90.

External deps: `field_store_->recall_keyword/recall/recall_temporal
/query_subject/search_symbols_by_name`, `yantra_->embed`,
`QueryIntentClassifier` (C++ class).

## Cross-handler duplication (pre-existing tech debt)

| Utility | Where | LOC each |
|---|---|---:|
| Token estimator (`words * 1.3f`) | compact, trajectory_compact (verbatim copy) | ~10 |
| Cosine similarity | compact (inline), implicit in field_store_ scores elsewhere | ~10 |
| Transcript parser (jsonl + system-reminder strip) | trajectory_compact, **field_session.hpp** | ~50 |
| `extract_content` (string-or-array) | compact, others likely | ~15 |

**Implication:** ~80–150 LOC of dedup-eligible utility is scattered
across handlers. Hoisting these into shared utility headers should
happen *before* any collapse — it shrinks the rewrite target and
exposes which handlers were inflated by copy-paste.

## Classification update

All three remain REAL_LOGIC. None are hidden glue, none are
trivially collapsible. They are genuinely algorithmic and would
need translation, not pass-through replacement.

## Translation friction (sharpens the collapse cost estimate)

The real boundary risk is **not** the handler code — it's the
**embedding subsystem (`yantra_`)**. All three outliers depend on
embedding via the C++ vakyantra path (SIMD, threaded, tuned).
Collapsing C++ → Rust requires either:

1. **Keep yantra in C++**, expose as Rust→C++ FFI (inverts current
   FFI direction). Adds a new boundary layer.
2. **Port yantra to Rust.** Performance review required; SIMD
   crates exist but parity isn't free.
3. **Inline embedding via candle/burn** in Rust. Largest scope,
   highest payoff.

Decision on yantra dominates the pass-2 effort estimate far more
than the handler count does.

## Pre-pass-2 cleanup candidates (cheap, no boundary commitment)

These reduce the rewrite surface regardless of direction:

1. Hoist `estimate_tokens` into a shared header (drops ~10 LOC dup
   in compact + trajectory_compact + likely others).
2. Hoist transcript parser shared between trajectory_compact and
   field_session.
3. Hoist `extract_content` (json-message → string) somewhere
   common.

Each is a single-file refactor, behaviour-preserving, and would
land cleanly on `main` without committing to pass-2 direction.

## Recommended next step (decision-ready)

Either:
- **A.** Stop autonomous work; user decides on COLLAPSE direction
  given the corrected ~400–500h cost and yantra boundary question.
- **B.** Proceed with the three pre-pass-2 cleanups above (~2–4h,
  zero risk, useful in either direction).
