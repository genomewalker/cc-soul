# Changelog

All notable changes to cc-soul are documented here.

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
