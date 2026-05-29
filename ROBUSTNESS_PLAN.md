# chitta-field Robustness Plan (Opus 4.8 + room room-7424d2b5)

Derived from a gpt-5.5 + opus-4.7 design room, corrected against the real code.
Root cause of the migration saga: reactive single-step ops + implicit identity +
lock-fairness starvation. Five PRs, smallest/highest-leverage first.

## PR1 — Unblock recall: consolidation_pass lock hygiene  ← IMPLEMENT FIRST
**Failure:** `consolidation_pass` (store.rs:1938) holds `event_tape.read()` across the
~30s `run_sequitur`; parking_lot task-fairness then blocks every queued tape writer
(`put_memory`/`log_event`) AND every subsequent reader → recall times out.
**Fix:**
- Clone the tape once under a brief read (`EventTape: Clone`), drop the guard, run
  `run_sequitur` + rule_data build + `fep_prior.rebuild_from_tape` + turiya sample
  against the **clone** → live `event_tape` lock held for ms, not 30s.
- Keep Phase-12 `compress_low_surprisal` on the live tape (it mutates; short write).
- Batch the per-rule `add_triplet` (store.rs:1964-1973, up to 5 writes/rule) into one
  collected pass to cut WAL/triplet_store lock churn.
**Test:** recall responds <2s while a consolidation_pass runs.

## PR2 — Single source of truth for embedding text
**Failure:** `re_embed` embedded raw content; live backfill uses
`"search_document: " + content + "\n" + gloss` (hardcoded in C++ `Subconscious::embed_loop`).
Divergence → query/doc space mismatch → ~5% cosine.
**Fix:** one `embedding_input(content) -> String` (Rust-owned, FFI-exposed or a single
shared C++ helper) used by ingest, backfill, re_embed. Tag with `text_format_version`.

## PR3 — Store identity + safe selection (prevents contamination)
**Failure:** `peek_seqno` reads only `{magic,seqno}` (snapshot.rs:1016); loader picks pure
max-seqno (field.rs:367) — dim never consulted; WAL gate is only `seg.max_seqno <
snapshot_seqno` (store.rs:5310) → 768 snapshot or foreign WAL with higher seqno wins.
**Fix:** store-global `StoreHeader{magic, format_version, embed_dim, model_id,
lineage_epoch, writer_uuid, vector_space_id}` in a `.shdr` sidecar. Selection filters
`(dim,model,lineage)` before seqno; WAL fenced by `(lineage_epoch,writer_uuid)`.
`validate_on_load` fail-closed (compiled EMBED_DIM == model dim == header dim).

## PR4 — Single-writer guarantee
**Failure:** Restart=always + execv self-update + hooks → concurrent daemons on one store.
**Fix:** `flock(LOCK_EX|LOCK_NB)` on `store.lock` for daemon lifetime (auto-releases on
crash); bump `lineage_epoch` on acquire. Gate execv self-update on
unchanged `format_version`/`embed_dim`, else require operator restart.

## PR5 — Atomic rebuild + migration of existing store
- `migrate-store-format`: stamp existing d6326d69 with `.shdr` (lineage_epoch=0) +
  per-artifact vector_space_id. Use per-payload `embedding_dim` as truth (NOT `.emb`
  size-division — ragged when `pending_embed>0`). No re-embed.
- `rebuild_embeddings`: clone-build into staging dir → fsync → `rename()` (atomic;
  loader already prunes orphan sidecars by hash). Idempotency key
  `(model_id, embed_dim, text_format_version, hash(glosses))`.

## Stopgap currently in place
`--no-hygiene` now also disables `enable_sleep_consolidation` (simple_cli.cpp) — partial;
PR1 is the real fix.

---

## Status (as implemented)

- **PR1 — DONE.** `consolidation_pass` clones `event_tape`/FEP tape under a brief read,
  runs sequitur/rebuild against the clone; live lock held for ms.
- **PR2 — DONE (scoped).** Single `ssl::retrieval_text(content)` helper (ssl_gloss.hpp)
  is the one source of the embedded document text (`content + "\n" + gloss`) across live
  backfill, re_embed, ingester, native_distiller, recall-time query embed. The
  embedder-specific `search_document:`/`search_query:` prefix stays per-embedder (llama
  adds it internally; ONNX prepends manually) — that asymmetry is intentional, not the
  divergence PR2 targeted. The double-prefix bug on the backfill path is removed.
- **PR3 — PARTIAL.** Dim-aware *fencing* on load is DONE: `field.rs` refuses a snapshot
  whose embedded payloads are ALL at a foreign `embedding_dim` (catches a wholesale 768-d
  snapshot winning on seqno; cannot brick a store that has any payload at the compiled
  `EMBED_DIM`). The full `StoreHeader`/`.shdr` sidecar + `(dim,model,lineage)` selection +
  WAL `(lineage_epoch,writer_uuid)` fencing is NOT done — deferred (new on-disk format,
  wants its own tested PR). Foreign-WAL contamination is currently handled by quarantine.
- **PR4 — ALREADY SATISFIED.** `acquire_lock` (daemon_lifecycle.cpp) takes
  `fcntl(F_SETLK, F_WRLCK)` on the lock file for the daemon lifetime, non-blocking,
  auto-released on crash — i.e. the single-writer guarantee PR4 asked for. The
  `lineage_epoch` bump + execv-gate sub-points depend on PR3's header → deferred with it.
- **PR5 — DEFERRED.** Atomic staging+rename rebuild and `migrate-store-format` depend on
  the PR3 header; large, wants its own PR. No re-embed was needed (the binary_vec fix
  recovered recall on the existing 1536-d store).

### Also fixed this session (outside the original 5 PRs)
- **binary_vec `[u64; 4]` → `[u64; BINARY_WORDS]`** (hnsw.rs) — leftover from EMBED_DIM=256;
  `codes[..24].try_into::<[u64;4]>()` failed → all-zero Hamming codes → arbitrary
  candidates → true NN discarded. THE recall-quality root cause. No re-embed required.
- **Recall score display** (field_handler.hpp `display_pct` + field_memory_recall.cpp) —
  the `[NN%]` shown (and gated by prompt-core.sh's 30% `MIN_CONFIDENCE`) was the composite
  ranking `score` (product of ~20 factors, caps ~3%), not the cosine. Now displays the raw
  semantic `similarity` when present, falling back to the composite for keyword-only hits.
  Fixes both the cosmetic 3%-for-cos-0.97 oddity AND the functional bug where every strong
  semantic hit was dropped below the prompt-injection confidence gate.
