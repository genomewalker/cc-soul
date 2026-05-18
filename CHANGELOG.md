# Changelog

All notable changes to cc-soul are documented here.

## [5.21.66] - 2026-05-18

### Fixed — Daemon Lock Starvation

- **Pre-embed write tools before exclusive lock** — `remember`, `observe`, `grow`, `learn*` now compute embeddings (pure ML inference on `yantra_`, no `field_store_` access) before acquiring `rpc_mutex_` exclusively. Previously 30–60 s ML inference held the write lock, causing `health_check` and all reads to queue for 20+ minutes.
- **Thread pool min workers 2 → 8** — pool now starts with 8 workers instead of 2, providing spare capacity when individual workers are occupied by slow operations.

## [5.21.65] - 2026-05-18

### Performance — Memory & Startup Overhaul

Daemon RAM reduced from **22–26 GB → 2–4 GB**. Snapshot size reduced from **4.2 GB → 420 MB**.

Root cause was runaway triplet accumulation: the distillation pipeline re-extracted the same subject-predicate-object facts on every run with no deduplication, producing ~14 million duplicate and invalidated entries over time.

#### Fixed

- **`TripletStore` deduplication** — `add()` checks for an existing live `(subject, predicate, object)` entry before inserting; duplicates update weight in place. Eliminates the primary source of unbounded growth.
- **Invalidated triplet purge** — `purge_invalidated()` removes all `valid_to_ms != 0` entries at load time and before snapshot save. Cleared 13.7M dead entries (3.4 GB → 110 MB) on first startup after upgrade.
- **Derived index skip** — `TripletStore`'s three lookup indexes (`by_subject`, `by_object`, `by_predicate`) are cleared before serialization and rebuilt after deserialization. Previously serialized as redundant string copies (~3× snapshot bloat).
- **`AssocEdge` deduplication** — `add_assoc_edge()` upserts by `(dst, edge_type)` instead of appending.
- **`malloc_trim` after store open** — freed pages from load-time migration are returned to the OS immediately.
- **Streaming snapshot load** — deserialization now uses `BufReader` (1 MB read-ahead) instead of `std::fs::read()` which allocated the entire file as `Vec<u8>` before any deserialization.
- **Daemon lock race** — `acquire_lock()` now runs before `early_server` binds the socket; competing startups fail in milliseconds.
- **Coactivation stats cap** — `coactivation_stats` pruned to top-20 pairs per memory at load and save time.

## [5.21.45] - 2026-05-16

### Added
- **v10 snapshot format** — embeddings stored in a `.emb` sidecar (magic header + count + flat `{id, f32×256}` records); binary sparse codes in a `.bin` sidecar. Reduces main bincode snapshot size. Both sidecars are structured for future `mmap` access.
- **Two-tier HNSW** (chitta-field) — `delta_hnsw` activates above 100K memories (`HNSW_TIER2_THRESHOLD`). New inserts go to the small delta graph O(log N_delta) instead of the large base graph O(log N_total). Delta is merged into the base at checkpoint when it exceeds 10% of base size. Persisted as a `.delta.hnsw` sidecar alongside the base graph.
- **SSL NL gloss** — SSL memories are re-embedded with a natural-language prefix via a one-time migration. `gloss_ssl_content()` translates SSL triplets (subject/predicate/object) into readable English sentences before embedding, improving semantic recall of structured memories.
- **Alias memories** — when a new SSL memory is stored, a linked `kind=alias` NL memory is automatically created and connected via an `alias-of` triplet. Keeps NL and SSL representations synchronized without duplicating content.
- **Embed worker** — idle gate removed; the background embed worker now runs every 30 s during active sessions instead of waiting for an explicit activity signal.

## [5.20.6] - 2026-04-16

### Fixed
- **Queue JSONL corruption** — hooks now enqueue via native `chitta queue_write` (single-syscall atomic append, JSON compaction); removes the ~40K/day `[queue] FAILED: json parse error` burst caused by multi-line `jq` output.
- **Chain continuity warning spam** — `OpLog::replay()` now resets `chain_head = ZERO_HASH` at instance-id prefix boundaries so independent per-instance segment chains no longer trigger header-level warnings (previously ~120K/day on startup).
- **Dead-letter interleaving** — `sandbox::append_line_atomic()` uses a single POSIX `write()` with `O_APPEND | O_CLOEXEC` and an EINTR/short-write loop; replaces two-step `ofstream::<<` calls that could interleave under concurrency.
- **`queue_write` CLI arg parsing** — now uses a positional-args vector so flags interleaved after the subcommand (e.g. `chitta queue_write --json ...`) no longer shift the tool/args slots.
- **`lib.sh` fallback** — when the `chitta` binary is absent, the bash fallback now compacts the JSON via `python3` and fails loudly instead of silently injecting a malformed line.
- **Dead-letter failure visibility** — `QueueProcessor::write_failed_item()` now logs append failures and inner exceptions to stderr instead of swallowing them.
- **Safer deploy** — `CLAUDE.md` documents atomic `install` (temp+rename) before `systemctl restart`, preserving daemon uptime across the binary swap.

### Added
- Regression test `replay_across_independent_instance_chains` in `chitta-field/src/log.rs` covering the instance-boundary chain-reset invariant.

## [5.3.0] - 2026-04-01

> **Note (2026-04-16):** The FEP / Hopfield / adaptive-vigilance items below are **partially shipped**. The chitta-field sibling checkout now exports the six `cf_*` symbols with the ABI declared in `chitta/include/chitta/field_store.hpp` (`cf_reconstruction_error`, `cf_memory_surprise` are real; `cf_search_attractor`, `cf_hopfield_co_retrieval`, `cf_hopfield_stats`, `cf_adapt_vigilance` are stubs returning `CF_NOT_IMPLEMENTED` / empty `"{}"`). The pinned submodule at `cc-soul/chitta-field@762463a` does NOT yet carry these exports; `hopfield.rs`, `attractor_settle`, asymmetric prototype transitions, surprise-modulated plasticity, and the self-orthogonalising sparse encoder are still unimplemented on both sides. Bump the submodule once the full set lands.

### Added
- **FEP attractor network** — self-orthogonalizing memory representations derived from the Free Energy Principle (Spisak & Friston, Neurocomputing 2026). Three-phase integration across chitta-field and the C++ daemon.
- **Asymmetric prototype transitions** — `strengthen_transition` gives full delta to forward direction (a→b), 0.3× to reverse (b→a). Sequential recall order encodes temporal asymmetry.
- **Asymmetric triplet weights** — new `reverse_weight` field on `TripletEntry` with `#[serde(default)]` backward compatibility. New triplets default to `reverse_weight = weight × 0.3`.
- **Surprise-modulated plasticity** — `MemoryState.surprise` stores reconstruction error from sparse encoder. `PlasticityLearner` uses it: high surprise → slow decay, low surprise → fast decay.
- **Attractor-based pattern completion** — `CorticalIndex::attractor_settle()` iteratively blends query with prototype centroids and follows asymmetric transitions (3-5 steps). New `Route::Attractor` in route learner.
- **Hopfield network module** (`hopfield.rs`) — asymmetric energy-based attractor network over memory co-activations. `settle()` propagates activation through multi-hop directed couplings with dynamic neighbor discovery.
- **Self-orthogonalizing sparse encoder** — Hebbian update replaced with FEP-derived rule: prediction error + complexity penalty (λ=1e-4) + Gram-Schmidt partial decorrelation (1% per step) between active atoms.
- **Adaptive vigilance** — `CorticalIndex::adapt_vigilance()` lowers vigilance when prediction error is high (more prototypes needed), raises it when model is accurate.
- **Free-energy merge criterion** — `find_dup_pairs` uses `cf_reconstruction_error` to check whether merging reduces total free energy (accuracy loss vs complexity gain), with cosine threshold fallback.
- **New FFI exports** — `cf_reconstruction_error`, `cf_memory_surprise`, `cf_search_attractor`, `cf_hopfield_co_retrieval`, `cf_hopfield_stats`, `cf_adapt_vigilance`.

### Fixed
- **Negative `start_turn` in `read_transcript`** — negative offsets (Python-style `-5` = 5 from end) now resolve correctly instead of wrapping to huge unsigned values.
- **CLI parser with negative numbers** — `-5` after `--start_turn` no longer treated as a flag; numeric values with leading minus are recognized as argument values.

## [5.0.0] - 2026-03-29

### Added
- **Pre-tool hook exit 2 blocking** — hook executes compact alternative itself (e.g. `head -200 + wc -l` instead of `cat`), returns output, and exits 2 to block the original call. Real token savings.
- **chitta-research integration** — autonomous research OS built on chitta-field; 7 specialized agents, belief graph, Brahman constitution.

### Changed
- Pre-tool hook timeout raised from 3s to 10s.
- Pre-tool hook now fires before the daemon availability gate so advisories reach Claude even during daemon startup.

## [4.0.88] - 2026-03-28

### Fixed
- `daemon_available()` helper missing from `lib.sh` — all hooks were silently exiting.
- Daemon gate moved after rewrite check so large-file advisory fires even when daemon is unavailable.
- Advisory prefixed with `BEFORE RUNNING` so it surfaces in `system-reminder`.

## [4.0.82–4.0.87] - 2026-03-26

### Changed
- Pre-tool hook rewrites fall back to `additionalContext` advisory (`updatedInput` not supported in Claude Code v2.1.87+).
- `hookSpecificOutput` wrapper used for correct hook protocol.
- Pipe characters stripped from filename extraction; advisory renamed to `Large output warning`.
- Rewrite advisory accumulates with chitta recall rather than exiting early.

## [4.0.79] - 2026-03-25

### Added
- **RTK-style command rewriting** — pre-tool hook detects dangerous or verbose Bash patterns and rewrites them before execution: `cat` on large files → `head`; unbounded `find /` → `find -maxdepth 5`; unbounded `grep -r` → `grep | head -200`.
- **Output-type-aware chitta recall** — TestResults, BuildOutput, LogOutput routed to separate memory categories.

## [4.0.74] - 2026-03-22

### Added
- **PoE domain reliability** — corrections automatically lower recall scores for the target realm. Mistakes don't just get overwritten; the field learns not to trust that domain.
- **Turn discipline** — warns after 15 turns without storing a memory, prompting consolidation.

## [4.0.73] - 2026-03-21

### Added
- **chitta_thinking** — C++ thinking block extractor fires every 10 turns, mining `thinking` blocks for perception-change moments and storing them as memories.

## [4.0.68] - 2026-03-18

### Added
- **HNSW semantic index** in chitta-field — activates automatically above 2,000 memories; sub-millisecond dense recall at scale.

## [4.0.65] - 2026-03-15

### Added
- **chitta_migrate unified** — auto-detects `soul.db` (SQLite) or `chitta.duckdb` and delegates to `chitta_import` without manual configuration.

## [4.0.x] - 2026-02 to 2026-03

### Added
- **chitta-field** — pure Rust memory substrate replaces DuckDB entirely. Sparse codes (64/16,384), WAL, cortical posting index, organic decay tiers, multi-instance writes.
- **FilterLevel on BM25 code ingestion** — Signatures-only or MinimalContext modes for leaner code indexing.
- **recall_with_fallback chain** — semantic → BM25 → recency. Recall never returns empty.
- **MacOS portability** — all Linux-only APIs (`inotify`, `/proc/self/exe`) guarded with `#ifdef __linux__`. Daemon builds and runs on macOS.
- **Milestone auto-detection** — milestones detected in conversation stored directly via write queue.
- **memory-intercept.sh** — PostToolUse:Write hook captures Write operations asynchronously.
- **log-bash-history.sh** — PostToolUse async hook for Bash history pattern learning.
- **`statusMessage` in hooks** — live status text during hook execution.
- **brain plausibility metric** — Spearman correlation of chitta recall vs TRIBE v2 fMRI predictions.

### Removed
- DuckDB backend (replaced by chitta-field Rust substrate).

## [3.36.0] - 2026-02-08

### Added
- **Native MCP Tools** — Auto-detect session_id and realm for 34 tools (SESSION_TOOLS, REALM_STORE_TOOLS, REALM_FILTER_TOOLS)
- **Intelligent Memory System** — 7 query types (Aspect, Entity, Temporal, Exploratory, Relationship, Code, Meta) with intent-driven routing
- **13 Fact Aspects** — Classification system for preferences, corrections, insights, failures, decisions, approaches, milestones, goals, habits, beliefs, wisdom, code, gaps
- **Standardized tool limits** — Interactive (10-15), List (20-30), Batch (50+)

### Changed
- Query routing: classification first, search second (300-450ms vs 1200-2400ms)

## [3.35.x] - 2026-02

### Added
- Companion activation mode
- Rich ledger extraction
- Auto-distillation of episode patterns

### Changed
- Embedding model: all-MiniLM-L6-v2 → bge-base-en-v1.5 (768 dimensions)

## [3.30.x] - 2026-01

### Removed
- PostgreSQL backend (DuckDB-only now)

## [3.27.x] - 2025-12

### Added
- xMemory-inspired theme system
- Hierarchical organization
- Two-stage retrieval

## [3.17.x]

### Added
- SSL enforcement
- Code intel protection
- Cost tracking

## [3.16.x]

### Added
- Background distillation
- Code enrichment

## [3.x]

### Added
- DuckDB backend
- Tree-sitter parsing
- Call graphs

## [2.x]

- C++ rewrite (Chitta engine)

## [1.x]

- Initial Python implementation
