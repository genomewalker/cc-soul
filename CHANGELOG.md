# Changelog

All notable changes to cc-soul are documented here.

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
