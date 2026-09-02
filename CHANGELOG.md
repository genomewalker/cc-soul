# Changelog

All notable changes to chitta (formerly cc-soul; renamed 2026-09-02, see
[docs/RENAME.md](docs/RENAME.md)) are documented here.

> **Gap notice, 2026-09-02.** Entries below jump from 5.71.0 to 5.41.3.
> Versions 5.42.0 through 5.70.x were released without changelog entries;
> roughly 419 commits landed in that window. The git history and the GitHub
> release notes are the record for that period. This is a documentation gap,
> not a period of inactivity.

## [5.71.0] - 2026-09-02

### Changed — project renamed `cc-soul` → `chitta`

- Plugin identity is now `chitta`, marketplace `genomewalker-chitta`, repository
  `github.com/genomewalker/chitta`. Skills are namespaced by plugin name, so
  `/cc-soul:recap` becomes `/chitta:recap`.
- Every `CC_SOUL_*` environment variable keeps working through an alias shim in
  `hooks/lib.sh` plus inline fallbacks at pre-source read sites. If both names
  are set, `CHITTA_*` wins.
- Unchanged on purpose: file and directory names, the `chitta-field` submodule,
  binaries, systemd unit names, the `.cc-soul-realm` dotfile, stored realm names
  (`project:cc-soul` is still a valid realm), `docs/CNAME`, and historical
  changelog entries. Full migration steps and the alias table:
  [docs/RENAME.md](docs/RENAME.md).

### Added — recall-biased pre-filter

- `recall` now fetches a wide candidate pool (`CHITTA_RECALL_POOL`, default 60,
  capped at 160) and over-selects it down to a rerank budget with scalar-only
  keep rules. A candidate survives if it is already inside `limit`, contains a
  literal query token, shares realm and kind with a top-5 hit at half the maximum
  score, or is a one-hop association neighbour of the head.
- A/B on the 30-query golden set, mean nDCG@20 with the reranker on: **0.435**
  pre-filter off, **0.487** on. On by default; `prefilter: false` or
  `CHITTA_RECALL_PREFILTER=0` restores the previous narrow-pool path exactly.

### Added — analogy lane, keyword realm scoping, lane ablation

- `recall_analogy`: proportional (`a:b::c:?`) and structural relation-shape
  retrieval over the triplet lane using vector-symbolic binding. No model call.
- `smart_recall`'s keyword legs are now realm-scoped, closing a cross-realm bleed.
- `CHITTA_ABLATE_LANES` disables named hook recall lanes (`sem`, `ctx`, `hyb`,
  `kw`, `corr`, `xr`) for one agent process. Consumed by `hooks/prompt-core.sh`;
  there is no daemon-side ablation flag.

### Added — SMRITI-Bench

- `benchmarks/smriti/`: an outcome-grounded memory benchmark for coding agents.
  A task passes an objective check command or it does not; no model judge, no
  reference-answer overlap score. First full matrix (9 tasks, off/on, 3 trials,
  n=27 per condition): success 23/27 off versus 27/27 on, paired token ratio
  median 0.52. MUI v2 rescore on the same data: 0.630.
- Not yet run: a live matrix over the six newer tasks or any ablation condition.

### Added — outcome ledger, MDL consolidation gate

- `chitta-mcp/outcome_ledger.py` and `hooks/outcome-ledger.sh`: fail-open JSONL
  append of `injected`, `bash_outcome` and `session_end` events, with an offline
  joiner computing Wilson-lower-bound credit per memory.
- `chitta-mcp/mdl_gate.py` runs in **shadow mode only**. It writes to
  `mdl_gate_shadow.jsonl` and does not block distillation.

### Added — session registry, thread inference, resume selector

- `chitta-mcp/session_registry.py`, `thread_inference.py`, `resume_selector.py`,
  with tests under `chitta-mcp/tests/`.

### Fixed

- Removed `eval` and Python source interpolation from hooks.
- `.claude-plugin/plugin.json` declared `chitta-field >= 5.0.0`; the submodule
  version is 2.7.12. Corrected to `>= 2.1.0`, the documented snapshot-format
  rollback floor.

### Documentation

- `docs/tools.html` and `docs/API.md` are now generated from a live daemon by
  `scripts/gen-tools-docs.py` instead of hand-maintained. The hand-written page
  claimed "150+ tools" and listed 241; the real surface is 343.
- New pages: `docs/recall.html` (the retrieval pipeline) and
  `docs/benchmarks.html` (SMRITI-Bench, LongMemEval, LoCoMo).

## [5.41.3] - 2026-05-21

### Added — LongMemEval benchmark pipeline (78% on longmemeval_s)

- `scripts/benchmark_longmemeval.py`: production-ready benchmark harness against
  the LongMemEval dataset. Ingests all haystack turns per question (no cap),
  prioritising user turns by length score. Sequential ingestion with 120 s recall
  timeout; no pipeline overlap that previously caused watchdog kills.
- Recall strategy: hybrid BM25+HDC (limit 30) + `recall_session` for non-temporal
  questions + keyword second-pass recall (strips stop words, re-queries BM25).
- `source_session` metadata on every ingested turn enables session-level
  noisy-OR aggregation via `recall_session`.
- Synthesis via `claude -p` with raw-memories context (no date-parse truncation);
  codex CLI fallback if claude unavailable.
- Achieves **78.0% (39/50)** on `longmemeval_s`, matching the best prior run.

### Added — `flush_embeddings` RPC tool

- `cf_flush_embedding_queue` FFI export, C++ handler `tool_flush_embeddings`,
  registered as `flush_embeddings` tool. Returns `{flushed: N}`.

### Fixed — `subconscious.sh` stray-daemon detection

- `kill_stray_daemons()` now only targets daemons with **no `--path` flag**
  (those that would default to the same mind path). Daemons started with an
  explicit `--path <dir>` (e.g. isolated benchmark daemons) are left untouched.

## [5.32.0] - 2026-05-19

### Added — CEC Phase 8: Cross-Session Motif Q-Values

- `CdawgState.q_value`: per-state expected-success scalar, persisted in CDAWG.
- `CdawgOrgan::update_q(terminal_sym, reward, α, γ)`: TD(0) propagation up the
  suffix-link tree, weighted by `1/|endpos|` so generic states don't absorb all
  signal. Called on every non-legacy outcome event with reward ±1.
- `CdawgOrgan::top_q_states(prefix, k)`: BFS from a prefix state, returns top-k
  reachable states by Q-value.
- `store.rs::recall_motif_value(tool, entity, k)`: maps Q-states to `RecallHit`
  with normalised score `(q+1)/2` and next-action predictions from outgoing transitions.
- New tool: **`recall_motif_value`** — "which action sequences have highest expected
  success rate from this context?"
- FFI: `cf_recall_motif_value` → JSON array `[{state_id, q_value, support, content}]`.

## [5.31.0] - 2026-05-19

### Added — CEC Phase 7: Causal Refutation Ledger

Rules that know they're wrong.

- `refutation_ledger.rs` (`RefutationLedger`): antecedent-indexed HashMap tracking
  per-rule `support` (sym_a → sym_b observed) and `contradict` (sym_a → anything
  else observed) counts with two hysteresis thresholds:
  - `refute_ratio > 0.4` → rule flips to `Refuted(ts)`; writes
    `(rule_N, "refuted_by", "contradict_at_ts=...")` triplet.
  - `refute_ratio < 0.2` → rule reinstated to `Live`.
- `SequiturRule.contradict_count`: persisted count of false-positive antecedent firings.
- `log_event()` calls `ledger.observe(prev_sym, curr_sym, ts)` on every append.
- `consolidation_pass()` calls `ledger.seed_from_rules()` after each Sequitur run.
- `store.rs::refutation_stats(k)`: human-readable top-k by refute_ratio with live/
  refuted counts.
- New tool: **`refutation_stats`** — shows which promoted procedural rules are
  being actively falsified.

## [5.30.0] - 2026-05-19

### Added — CEC Phase 6: Counterfactual Recall via CDAWG Sibling Edges

- `CounterfactualHit` struct: `{symbol, fail_ratio, taken_fail_ratio, delta, support,
  wilson_fail_lower}`.
- `CdawgOrgan::counterfactual_alternatives(context, taken_sym, min_support, k)`:
  walks to prefix state, enumerates sibling transitions, ranks by
  `delta = taken_fail_ratio - alt_fail_ratio` (positive = alternative is better).
  Wilson lower bound (90% CI, z=1.645) used as tiebreaker for uncertain estimates.
- `store.rs::recall_counterfactual(tool, entity, outcome, k)`: builds context from
  last 4 EventTape symbols, delegates to CDAWG.
- New tool: **`recall_counterfactual`** — "what alternative tool/entity would have
  had a lower failure rate in the same context?"
- Gate: `min_support = 5` (same as failure_pattern).

## [5.29.0] - 2026-05-19

### Added — Causal Episode Compiler (CEC) — Phases 1–5

Embedding-free agentic memory substrate that indexes state transitions
`(tool, entity, outcome)` rather than content. Runs in parallel with the
existing HNSW/BM25/HDC recall lanes, which are untouched.

**Phase 1 — Event Tape + CDAWG (v5.25.0)**
- `EventTape`: append-only log of `(tool_id, entity_key, outcome_class, session_id, ts_ms)`.
  Entity canonicalization is deterministic (file paths → repo-relative, URLs → hostname,
  freeform → first 40 chars lowercased).
- `CdawgOrgan`: online Compact Directed Acyclic Word Graph over packed u64 symbols
  `(tool_id << 40 | outcome_class << 32 | entity_key)`. Ephemeral — rebuilt from
  EventTape at every daemon load.
- `EventTape` serialized in `FullSnapshot` (V13 → V14 migration); existing memories
  receive a synthetic `legacy` event for warm-start.
- New CLI/MCP tools: `log_event`, `recall_last_action`, `recall_failure_pattern`,
  `recall_causal`.

**Phase 2 — TD(λ) + PMI Causal Antecedents + Roaring (v5.26.0)**
- `CdawgState.endpos` upgraded from `Vec<u32>` to `RoaringBitmap` (O(1) cardinality
  for PMI denominator).
- `push_td_credit()`: backward eligibility traces over last 16 events, δ=+0.1 success /
  −0.2 fail, γ=0.9 decay. Skips synthetic `legacy`/`remember` events.
- `recall_causal_antecedent`: returns PMI-ranked predecessor patterns —
  `log(count(X,Y) × N / count(X) / count(Y))`.

**Phase 3 — Surprisal-Gated Writes + Involuntary Injection (v5.27.0)**
- `put_memory` computes PPM surprisal before logging the event. If
  `surprisal > 2.0 nats`, the memory's `decay_rate` is halved (surprising
  memories burn in harder).
- `hooks/prompt-core.sh`: if `recall_failure_pattern` finds a pattern with
  `fail_ratio > 0.7 && fail_count ≥ 3`, a one-line `⚠ CEC:` warning is
  prepended to `SYSTEM_MSG` at every turn start (involuntary injection).

**Phase 4 — HDC as Heteroassociative Binder (v5.28.0)**
- `EpisodeHdcStore`: each CEC event encoded as
  `bind(R_tool, t) XOR bind(R_entity, e) XOR bind(R_outcome, o)`.
- `recall_hdcbind(known_role, known_val, query_role)`: XOR-unbind query —
  given e.g. `outcome=fail`, returns most associated tools by Hamming distance
  against the codebook.
- Ephemeral — rebuilt from EventTape at daemon load. Per-role `EpBundle`
  bit-count accumulators, not serialized.

**Phase 5 — Sequitur Grammar + Procedural KG Promotion (v5.29.0)**
- `sequitur.rs`: `run_sequitur()` counts all bigrams (consecutive symbol pairs)
  in the EventTape with frequency ≥ 5 (RePair-style grammar induction).
- `consolidation_pass()`: promotes rules to the triplet KG as four predicates:
  `compresses`, `avg_outcome`, `support`, `tape_range`. Subjects use the scheme
  `rule:tool(entity,outcome)→tool(entity,outcome)`.
- Dedup: `triplet_store.query_subject()` check prevents re-inserting on repeated runs.
- Auto-triggers every 500 events inside `log_event()`.
- `consolidation_pass --preview true --k N`: dry run showing top-N rules.
- On the live 57K-memory instance: 215 procedural rules promoted on first run,
  queryable via `chitta query --subject "rule:..."`.

## [5.21.68] - 2026-05-18

### Fixed — Pre-embed All Medium-Frequency Write Handlers

Completed the pre-embed pattern for the remaining write tools: `update`, `consolidation_merge`, `curiosity_note_gap`, `propose_change`, `profile_update`, `profile_observe`, `goal_set`, `reconsolidate`. All 15 write handlers that call `embed_text()`/`embed_ssl_aware()` now compute embeddings outside `rpc_mutex_`.

## [5.21.67] - 2026-05-18

### Fixed — More Pre-embed Before Lock

Extended the pre-embed pattern to `suggestion_track`, `anticipation_observe`, and `create_episode` — three high-frequency write tools called by hooks and the distillation pipeline that were holding the exclusive `rpc_mutex_` lock during ML inference.

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
