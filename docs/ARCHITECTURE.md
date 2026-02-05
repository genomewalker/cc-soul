# CC-Soul Architecture

Technical architecture of cc-soul v3.30, a persistent identity and memory system for Claude Code.

---

## Table of Contents

- [System Overview](#system-overview)
- [Components](#components)
- [Storage Layer (DuckDB)](#storage-layer-duckdb)
- [Embedding Engine (Vāk)](#embedding-engine-vāk)
- [Resonance Engine](#resonance-engine)
- [Theme System (xMemory)](#theme-system-xmemory)
- [Code Intelligence](#code-intelligence)
- [Subconscious Processor](#subconscious-processor)
- [Self-Tuning (Bayesian Bandits)](#self-tuning-bayesian-bandits)
- [Graph Layer (Triplets)](#graph-layer-triplets)
- [RPC Layer](#rpc-layer)
- [Memory Types and Lifecycle](#memory-types-and-lifecycle)
- [Quantization and Compression](#quantization-and-compression)
- [Provenance](#provenance)
- [Session Continuity](#session-continuity)
- [Integration with Claude Code](#integration-with-claude-code)

---

## System Overview

```
┌──────────────────────────────────────────────────────────────┐
│                       CLAUDE CODE                            │
│                                                              │
│  Hooks (SessionStart, UserPromptSubmit, PostToolUse, ...)    │
│         │                              │                     │
│         ▼                              ▼                     │
│  ┌────────────┐                 ┌────────────────┐           │
│  │ soul-hook.sh│                 │ MCP Server     │           │
│  │ (context    │                 │ (chitta-mcp)   │           │
│  │  injection) │                 │                │           │
│  └──────┬──────┘                 └───────┬────────┘           │
│         │         Unix Socket            │                   │
│         └────────────┬───────────────────┘                   │
│                      ▼                                       │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │                    CHITTAD DAEMON                        │ │
│  │                                                         │ │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │ │
│  │  │ Thread Pool   │  │ RPC Handler  │  │ Subconscious │  │ │
│  │  │ (2-16 workers)│  │ (100+ tools) │  │ (background) │  │ │
│  │  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘  │ │
│  │         │                 │                  │          │ │
│  │         └────────────┬────┘──────────────────┘          │ │
│  │                      ▼                                  │ │
│  │  ┌─────────────────────────────────────────────────┐    │ │
│  │  │              DuckDBMind                          │    │ │
│  │  │                                                  │    │ │
│  │  │  Embedder ←→ VakYantra (ONNX)                   │    │ │
│  │  │  ResonanceLearner (Bayesian self-tuning)         │    │ │
│  │  │  ThemeManager (xMemory)                          │    │ │
│  │  │  SessionContext (priming, topics)                 │    │ │
│  │  │                                                  │    │ │
│  │  │      ┌──────────────┐  ┌──────────────────┐     │    │ │
│  │  │      │ DuckDBStore  │  │ Embeddings DB    │     │    │ │
│  │  │      │ (memories,   │  │ (HNSW vectors,   │     │    │ │
│  │  │      │  triplets,   │  │  separate file)  │     │    │ │
│  │  │      │  symbols,    │  │                  │     │    │ │
│  │  │      │  ledger)     │  │                  │     │    │ │
│  │  │      └──────────────┘  └──────────────────┘     │    │ │
│  │  └─────────────────────────────────────────────────┘    │ │
│  └─────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────┘
```

### Key Design Decisions

- **DuckDB** as the single storage engine (not SQLite, not tiered hot/warm/cold)
- **Separate embeddings database** to avoid write contention between HNSW rebuilds and memory writes
- **ConnectionPool** for concurrent read access with RAII-based connection scoping
- **Unix domain socket** for IPC between Claude Code and the daemon
- **Auto-scaling thread pool** (2-16 workers) with watchdog for slow request detection

---

## Components

### Binaries

| Binary | Purpose | Source |
|--------|---------|--------|
| `chittad` | Daemon: socket server + RPC handler + subconscious | `chitta/src/rpc_server.cpp` |
| `chitta` | CLI tool: direct command-line access | `chitta/src/simple_cli.cpp` |

### Core Classes

| Class | File | Role |
|-------|------|------|
| `DuckDBMind` | `mind/duckdb_mind.hpp` | Central orchestrator: remember, recall, resonate, self-tune |
| `DuckDBStore` | `duckdb_store.hpp` | Storage: all DuckDB operations, schema, queries |
| `DuckDBRpcHandler` | `rpc/duckdb_handler.hpp` | JSON-RPC 2.0 handler, 100+ registered tools |
| `Embedder` | `mind/embedder.hpp` | Embedding with LRU cache and circuit breaker |
| `AntahkaranaYantra` | `vak_onnx.hpp` | ONNX Runtime inference for all-MiniLM-L6-v2 |
| `Subconscious` | `mind/subconscious.hpp` | Background thread: patterns, hygiene, embedding |
| `ThemeManager` | `theme_manager.hpp` | xMemory hierarchical memory organization |
| `CodeIntel` | `code_intel.hpp` | Tree-sitter symbol extraction (9 languages) |
| `SymbolResolver` | `symbol_resolver.hpp` | Cross-file symbol resolution for call graphs |
| `ThreadPool` | `rpc/thread_pool.hpp` | Auto-scaling worker pool with watchdog |
| `ProvenanceSpine` | `provenance.hpp` | Knowledge source tracking and trust scoring |

---

## Storage Layer (DuckDB)

### Why DuckDB

DuckDB is an embedded analytical database. CC-Soul uses it for:

- **HNSW vector search** via the VSS extension (cosine similarity on 384-dim embeddings)
- **Graph queries** via DuckPGQ extension (triplet traversal)
- **Full-text search** via BM25 (keyword matching as complement to semantic search)
- **ACID transactions** with WAL-based crash recovery
- **Concurrent reads** through a connection pool (write-serialized, read-parallel)

### Database Files

```
~/.claude/mind/chitta/
├── chitta.duckdb          # Main database (memories, triplets, symbols, ledger)
├── chitta_emb.duckdb      # Embeddings database (HNSW index, separate to avoid contention)
└── chitta.duckdb.wal      # Write-ahead log (auto-managed)
```

### Schema (Main Database)

**memory** table — the core unit of storage:

| Column | Type | Purpose |
|--------|------|---------|
| `id` | INTEGER (PK) | Auto-incrementing ID |
| `kind` | VARCHAR | Memory type (wisdom, belief, episode, etc.) |
| `content` | TEXT | The actual content (often in SSL format) |
| `confidence` | DOUBLE | Bayesian confidence (0.0-1.0) |
| `decay_rate` | DOUBLE | How fast this memory fades |
| `tags` | VARCHAR | Comma-separated tags |
| `realm` | VARCHAR | Primary realm (default: "brahman") |
| `visibility` | INTEGER | 0=Private, 1=Shared, 2=Global |
| `created_at` | TIMESTAMP | Creation time |
| `updated_at` | TIMESTAMP | Last modification |
| `accessed_at` | TIMESTAMP | Last access (for freshness) |
| `access_count` | INTEGER | How often accessed |

**triplet** table — knowledge graph:

| Column | Type | Purpose |
|--------|------|---------|
| `subject` | VARCHAR | Source entity (string, not ID) |
| `predicate` | VARCHAR | Relationship type |
| `object` | VARCHAR | Target entity (string, not ID) |
| `weight` | DOUBLE | Edge strength |

**symbol** table — code intelligence:

| Column | Type | Purpose |
|--------|------|---------|
| `id` | INTEGER (PK) | Symbol ID |
| `name` | VARCHAR | Symbol name |
| `kind` | VARCHAR | function, class, method, etc. |
| `signature` | VARCHAR | Full signature |
| `file_path` | VARCHAR | Source file |
| `line_start` | INTEGER | Start line |
| `line_end` | INTEGER | End line |
| `parent` | VARCHAR | Parent symbol (for nested) |
| `project` | VARCHAR | Project name |
| `description` | VARCHAR | Semantic description |

Additional tables: `call_edge`, `code_file`, `ledger`, `long_task`, `long_task_event`, `suggestion`, `anticipation`, `habit`, `background_task`, `user_profile`, `goal`, `calibration`, `theme`, `theme_memory`, `realm_membership`, `transcript_state`.

### Connection Pool

```cpp
class ConnectionPool {
    // Pre-allocated connections for concurrent reads
    // Write operations go through a dedicated write connection
    // ScopedConnection: RAII wrapper that returns connection on destruction
    // Timeout: waits up to N ms, creates emergency overflow if needed
};
```

- Default pool size: configurable (typically 4-8 connections)
- Read connections are shared; writes are serialized through a single connection
- Emergency overflow connections created when pool is exhausted under load

### Embeddings Database (Separate)

The HNSW index is stored in a separate DuckDB file (`chitta_emb.duckdb`) because:

1. HNSW index rebuilds are expensive and can block writes
2. Read-heavy semantic search shouldn't contend with memory writes
3. The embeddings database can be rebuilt from the main database if corrupted

---

## Embedding Engine (Vāk)

The embedding pipeline follows a Vedantic naming convention:

```
Text → VakPatha (tokenizer) → Shabda (token sequence) → AntahkaranaYantra (ONNX) → Artha (embedding)
```

### Components

| Class | Sanskrit Meaning | Role |
|-------|------------------|------|
| `VakPatha` | Path of speech | WordPiece tokenizer (vocab.txt) |
| `Shabda` | Sound-form | Tokenized input (input_ids + attention_mask) |
| `Artha` | Meaning | 384-dim embedding vector + certainty |
| `AntahkaranaYantra` | Inner instrument | ONNX Runtime inference engine |
| `SmritiYantra` | Memory machine | Caching wrapper (LRU, 10000 entries) |
| `ShantaYantra` | Silent machine | Zero-vector fallback |

### Model

- **Model**: all-MiniLM-L6-v2 (22M parameters)
- **Dimensions**: 384
- **Max sequence length**: 128 tokens
- **Pooling**: Mean pooling with L2 normalization
- **Runtime**: ONNX Runtime with sequential execution mode
- **Batch size**: Up to 32 texts per inference call

### Embedder (with Circuit Breaker)

The `Embedder` class wraps `VakYantra` with:

- **LRU cache**: 1000 entries, tracks hit/miss rate
- **Circuit breaker**: Opens after 3 consecutive failures, enters cooldown (60s), then half-open state for testing recovery

```
States: CLOSED → (3 failures) → OPEN → (60s cooldown) → HALF_OPEN → (success) → CLOSED
                                                        → (failure) → OPEN
```

---

## Resonance Engine

The resonance engine is the core of memory retrieval. `DuckDBMind::full_resonate()` runs 8 phases to find relevant memories:

### Phase 1: Semantic Seeds

Vector similarity search using HNSW index. Returns top-k memories by cosine distance to query embedding.

### Phase 2: BM25 Hybrid

Keyword-based search using BM25 scoring. Complements semantic search for exact term matches. Results are merged with semantic seeds using weighted combination:

```
relevance = (w_semantic * similarity + w_bm25 * bm25_score + tag_boost) * confidence_factor
```

Default weights: `semantic=0.6, bm25=0.4, tag_boost=0.05`

Confidence factor: `0.5 + 0.5 * confidence` (memories with higher confidence score higher)

### Phase 3: Tag Matching

Boost memories whose tags match terms in the query. Provides a small additive boost.

### Phase 4: Attractor Finding

Identify conceptual "gravity wells" — clusters of densely connected memories in the triplet graph. Attractors are cached with 5-minute TTL.

### Phase 5: Spreading Activation

Starting from seed memories, activation spreads through the triplet graph:

- `spread_strength`: 0.5 (how much activation propagates)
- `spread_decay`: 0.5 (decay per hop)
- `max_hops`: 3

Connected memories receive activation proportional to edge weight and inversely proportional to distance.

### Phase 6: Session Priming

Recent observations and active topics from the current session boost related memories:

- `priming_boost`: 0.3
- `topic_boost`: 0.2

### Phase 7: Code Intelligence

For code-like queries (detected by heuristic: contains `::`, `->`, `_`, `.`, or is a single identifier):

- BM25 search on symbol names and signatures
- Term-based search for exact symbol name matches
- Code symbol weight: 0.5

### Phase 8: Post-Processing

- **Attractor boost**: Memories near attractor centers get `basin_boost` (1.15x)
- **Lateral inhibition**: High-similarity memories compete; weaker duplicates are suppressed (`inhibition_strength=0.7`, `similarity_threshold=0.85`)
- **Hebbian learning**: Co-accessed memories strengthen their triplet connections (`hebbian_strength=0.03`)
- **Self-tuning**: Credit assignment feeds back into the Bayesian bandit for parameter optimization

### Resonance Configuration

```cpp
struct DuckDBResonanceConfig {
    float spread_strength = 0.5f;
    float spread_decay = 0.5f;
    int max_hops = 3;
    float hebbian_strength = 0.03f;
    int max_attractors = 10;
    float basin_boost = 1.15f;
    float similarity_threshold = 0.85f;
    float inhibition_strength = 0.7f;
    float epsilon_boost_alpha = 0.3f;
    float semantic_weight = 0.6f;
    float activation_weight = 0.4f;
    float code_symbol_weight = 0.5f;
};
```

---

## Theme System (xMemory)

Inspired by the xMemory paper, themes provide hierarchical organization of memories.

### How It Works

1. **Assignment**: Each memory is assigned to a theme based on a scoring function:
   ```
   score = semantic_weight * cosine_similarity + sparsity_weight * sparsity_score
   ```
   Where sparsity penalizes oversized themes:
   ```
   sparsity = 1 / (1 + exp(2 * (theme_size / ideal_size - 1)))
   ```

2. **Auto-creation**: When the best score for a memory falls below `assignment_threshold`, a new theme is created.

3. **Two-stage retrieval** (`theme_recall`):
   - Stage 1: Find diverse theme representatives matching the query
   - Stage 2: Adaptively expand within matching themes for depth

4. **Maintenance**: Background process periodically:
   - Splits oversized themes
   - Merges similar themes
   - Reassigns orphan memories

---

## Code Intelligence

### Tree-sitter Parsing

CC-Soul uses tree-sitter to extract structural information from source code. Supported languages:

| Language | Grammar |
|----------|---------|
| C++ | tree-sitter-cpp |
| Python | tree-sitter-python |
| JavaScript | tree-sitter-javascript |
| TypeScript | tree-sitter-typescript |
| Go | tree-sitter-go |
| Rust | tree-sitter-rust |
| Java | tree-sitter-java |
| Ruby | tree-sitter-ruby |
| C# | tree-sitter-c-sharp |

### Extracted Information

- **Symbols**: Functions, classes, methods, structs, enums with name, kind, signature, file path, line range, parent
- **Callsites**: Function calls with `CallKind` (Call, MemberCall, Qualified, New, Ctor, Indirect, LambdaCall)
- **Imports**: File dependencies
- **Type hierarchy**: Inheritance relationships

### Symbol Resolution

The `SymbolResolver` performs cross-file resolution:

1. Collects all callsites from tree-sitter extraction
2. Attempts to resolve each callsite to a known symbol by name matching
3. Populates the `call_edge` table for call graph queries
4. Reports resolution confidence and statistics

### Search Modes

| Mode | Tool | Method |
|------|------|--------|
| Name search | `find_symbol` | Exact/prefix match on symbol name |
| Semantic search | `search_symbols` | Embedding similarity on symbol metadata |
| BM25 search | (internal) | Full-text keyword match on name + signature |
| Call graph | `symbol_callers` / `symbol_callees` | Traversal of `call_edge` table |

---

## Subconscious Processor

The `Subconscious` class runs as a background thread inside `chittad`, performing work that doesn't require user interaction.

### Processing Loop

```
Every 1 second:
  ├── Check for pending events (user messages, tool results)
  ├── Pattern detection (corrections, preferences, frustration, milestones)
  └── Process embedding queue (background-computed, flushed by main thread)

Every 30 seconds (idle):
  └── Process embedding batch (up to 20 items)

Every 30 minutes:
  └── Memory hygiene (decay, prune, consolidate)

Every 60 minutes:
  └── Theme maintenance (split, merge, reassign)
```

### Pattern Detection

The subconscious watches events for patterns using regex matching:

| Pattern | Trigger | Action |
|---------|---------|--------|
| Correction | "no", "actually", "that's wrong" | `learn_correction` |
| Preference | "I prefer", "don't do X", "always Y" | `learn_preference` |
| Frustration | Repeated errors, stuck indicators | Log frustration signal |
| Milestone | "shipped", "released", "done" | `learn_milestone` |

### Suggestion Tracking

When the system surfaces a memory, it tracks whether it actually helped. The `learn_outcome` feedback loop strengthens useful memories and weakens misleading ones.

### Background Embedding Queue

Memories are embedded asynchronously:

1. `remember()` stores the memory with a placeholder embedding
2. Subconscious picks up un-embedded memories in batches of 20
3. Embeddings are computed in the background thread
4. Main thread flushes computed embeddings to the database

---

## Self-Tuning (Bayesian Bandits)

The `ResonanceLearner` uses Thompson sampling to automatically optimize resonance parameters.

### How It Works

1. **Priors**: Each tunable parameter has a Bayesian prior:
   - `BetaPrior`: For bounded parameters (0-1), modeled as Beta distribution
   - `GaussianPrior`: For unbounded parameters, modeled as Gaussian

2. **Sampling**: On each `full_resonate()` call, parameters are sampled from their current posteriors (Thompson sampling exploration)

3. **Context**: `QueryContext` features inform the bandit:
   - `query_length`, `term_count`, `has_technical_terms`
   - `has_domain_prefix`, `avg_term_frequency`

4. **Credit assignment**: When a user strengthens or weakens a memory, the `ResonanceLearner` attributes credit to the parameters that were active when that memory was surfaced

5. **Persistence**: Learner state serializes to JSON and persists across daemon restarts

### Tuned Parameters

- Semantic weight vs. BM25 weight
- Spread strength and decay
- Hebbian learning rate
- Tag boost magnitude

---

## Graph Layer (Triplets)

The knowledge graph uses string-based triplets (not NodeId-based edges):

```
(subject: string) --[predicate: string]--> (object: string)
```

### Common Predicates

| Predicate | Usage |
|-----------|-------|
| `calls` | Symbol A calls Symbol B |
| `contains` | File contains Symbol |
| `imports` | File imports Module |
| `inherits` | Class inherits Base |
| `corrects` | Correction memory corrects original |
| `relates_to` | General association |

### Spreading Activation

Activation flows through the triplet graph during resonance:

1. Seed memories activate their subject/object entities
2. Connected entities receive decayed activation
3. Memories associated with activated entities get boosted
4. Process repeats for `max_hops` iterations

---

## RPC Layer

### Protocol

JSON-RPC 2.0 over Unix domain socket. Two methods:

- `tools/list` — Returns all registered tools with input schemas
- `tools/call` — Invokes a tool by name with arguments

### Thread Pool

```cpp
class ThreadPool {
    // Min workers: 2, Max workers: 16
    // Auto-scales based on queue pressure
    // Watchdog thread: checks every 5 seconds
    // Escalation threshold: 60 seconds (logs slow requests)
    // Request tracing with timing
};
```

### Tool Categories (100+)

| Category | Count | Examples |
|----------|-------|---------|
| Core Memory | 11 | remember, recall, grow, get, update, observe, forget, batch_forget, tag, strengthen, weaken |
| Exploration (RLM) | 4 | explore_recall, explore_peek, explore_expand, explore_neighbors |
| Graph | 3 | connect, query_graph, query |
| Resonance | 2 | full_resonate, smart_context |
| Code Intelligence | 17 | learn_codebase, find_symbol, read_symbol, read_function, search_symbols, symbol_callers, symbol_callees, code_context, codebase_overview, type_hierarchy, file_imports, file_dependents, ... |
| Realm | 7 | realm_list, realm_get, realm_set, realm_add, realm_remove, realm_visibility, realm_detect |
| Ledger/Session | 6 | ledger_save, ledger_load, ledger_list, ledger_get, ledger_delete, checkpoint |
| Long Tasks | 7 | long_task_start, long_task_get, long_task_active, long_task_update, long_task_complete, long_task_event, long_task_snapshot, long_task_evaluate |
| Themes (xMemory) | 6 | theme_list, theme_get, theme_recall, theme_stats, theme_maintain, theme_assign_orphans |
| Suggestions | 4 | suggestion_track, suggestion_pending, suggestion_resolve, suggestion_count |
| Consolidation | 3 | consolidation_scan, consolidation_merge, consolidation_auto |
| Metacognition | 3 | metacognition_corrections, metacognition_outcomes, metacognition_evaluate |
| Curiosity | 3 | curiosity_note_gap, curiosity_gaps, curiosity_resolve |
| Transcripts | 7 | transcript_register, transcript_get, transcript_list, transcript_update, transcript_remove, transcript_parse, transcript_search |
| Anticipation | 4 | anticipation_observe, anticipation_predict, anticipation_success, anticipation_list |
| Habits | 5 | habit_observe, habit_match, habit_strengthen, habit_weaken, habit_list |
| Background | 3 | background_schedule, background_status, background_run_cycle |
| Profile | 3 | profile_get, profile_update, profile_observe |
| Goals | 5 | goal_set, goal_get, goal_list, goal_progress, goal_complete |
| Calibration | 2 | calibration_record, calibration_score |
| Hygiene | 2 | hygiene_stats, hygiene_run |
| Insights | 2 | insight_promote, insight_global |
| State/Maintenance | 7 | soul_context, subconscious_stats, enrichment_status, health_check, version_check, cycle, cleanup |
| Import/Export | 3 | import_soul, export_soul, ssl_convert |
| Epiplexity | 1 | epiplexity_check |
| SQL | 1 | sql_query |

---

## Memory Types and Lifecycle

### Node Types

```cpp
enum class NodeType {
    Wisdom = 0,     Belief = 1,      Intention = 2,   Episode = 3,
    Failure = 4,    Aspiration = 5,   Dream = 6,       Question = 7,
    Correction = 8, Entity = 9,       Term = 10,       Edge = 11,
    Insight = 12,   Signal = 13,      State = 14,      Summary = 15,
    Review = 16,    Preference = 17,  Milestone = 18,  Approach = 19,
    Outcome = 20,   Gap = 21,         Symbol = 22,     ProjectEssence = 23,
    ModuleState = 24, PatternState = 25
};
```

### Decay Rates

| Type | Decay Rate | Rationale |
|------|------------|-----------|
| Wisdom | 0.005 | Proven patterns should persist |
| Belief | 0.0 | Never decays (core identity) |
| Episode | 0.03 | Fades unless reinforced |
| Symbol | 0.0 | Code structure doesn't decay |
| Preference | 0.01 | Slowly fades if not reinforced |
| Correction | 0.005 | Important lessons persist |

### Quality Gate

Before storing, `DuckDBMind::remember()` applies:

1. **Minimum length**: Content must be >= 10 characters
2. **Deduplication**: Cosine similarity check against recent memories (threshold: 0.95)
3. **Diversity sampling**: Avoids storing too many similar memories in quick succession

### Confidence Model

Confidence is Bayesian, not a simple scalar:

```cpp
struct Confidence {
    float mu;           // Mean confidence
    float sigma_sq;     // Variance (uncertainty)
    int n;              // Number of observations
    float tau;          // Decay parameter

    void observe(float value);  // Update with new evidence
    void decay(float rate);     // Time-based decay
};
```

- `strengthen()` calls `observe(positive)`, increasing mu and reducing sigma
- `weaken()` calls `observe(negative)`, decreasing mu
- `decay()` gradually reduces confidence based on decay_rate

---

## Quantization and Compression

### Quantized Vectors

```cpp
struct QuantizedVector {
    int8_t data[384];   // 384 bytes (vs 1536 bytes for float32)
    float scale, offset; // Reconstruction: float = data[i] * scale + offset
    // 74% storage savings
};
```

### Binary Vectors

```cpp
struct BinaryVector {
    uint64_t bits[6];   // 48 bytes (384 bits, one per dimension)
    // Sign of each float → 1 bit
    // Hamming distance via popcount
    // 32x compression, fast approximate similarity
};
```

### Epiplexity (ε)

Measures reconstruction quality from compressed seeds:

```
ε = (S · K · D · C)^0.25
```

| Component | Meaning |
|-----------|---------|
| S | Semantic fidelity (cosine similarity of embeddings) |
| K | Entity preservation (key terms retained) |
| D | Information density (compression ratio) |
| C | Compression utility (useful information per byte) |

---

## Provenance

Every memory can track its knowledge source:

```cpp
enum class ProvenanceSource {
    Unknown, UserInput, ToolOutput, WebFetch,
    FileRead, Synthesis, Inference, Import, Migration
};

struct Provenance {
    ProvenanceSource source;
    std::string session_id;
    std::string tool_name;
    float trust_score;
    // Serializable for persistence
};
```

The `ProvenanceSpine` manages atomic persistence and trust filtering.

---

## Session Continuity

### Ledger System

Session state persists via the ledger:

```
ledger_save(session_id, {
    mood: "confident",
    todos: [{content: "Fix bug", status: "done"}],
    active_files: ["src/main.cpp"],
    decisions: ["Used DuckDB over SQLite"],
    next_steps: ["Add tests"],
    blockers: [],
    discoveries: ["HNSW rebuild is async"],
    snapshot: "Full reconstruction text..."
})
```

The `checkpoint` tool auto-routes to the active long task if one exists, otherwise creates a standalone ledger entry.

### Long-Running Tasks

For work spanning multiple sessions:

1. `long_task_start` — Define goal, hard/soft completion checks, work items
2. `long_task_event` — Log decisions, tool results, observations
3. `long_task_update` — Update progress, work items, blockers
4. `long_task_snapshot` — Get synthesized context for injection
5. `long_task_evaluate` — Check completion criteria
6. `long_task_complete` — Mark done with outcome summary

---

## Integration with Claude Code

### Hook System

CC-Soul integrates via Claude Code hooks:

| Event | Script | Action |
|-------|--------|--------|
| SessionStart | `soul-hook.sh start` | Load soul context, start daemon |
| UserPromptSubmit | `soul-hook.sh prompt --lean --resonate` | Surface relevant memories |
| PostToolUse | `capture-hook.sh` | Passive learning from tool use |
| PreCompact | `soul-hook.sh pre-compact` | Save state before compaction |
| SessionEnd | `soul-hook.sh end` | Save ledger |

### MCP Server

The `chitta-mcp` Python server exposes additional high-level tools:

| Tool | Purpose |
|------|---------|
| `learn_correction` | Store corrections with counter-memory |
| `learn_preference` | Store user preferences (global visibility) |
| `learn_insight` | Store cross-project insights |
| `learn_approach` | Store what works in different states |
| `learn_outcome` | Track suggestion effectiveness |
| `learn_milestone` | Record achievements |
| `research_topics` / `research_store` / `research_cycle` | Curiosity-driven research |

### Transparent Memory

The key integration pattern:

1. User sends a message
2. `UserPromptSubmit` hook fires
3. `soul-hook.sh` extracts the message and calls `full_resonate`
4. Relevant memories are injected into Claude's context as `<system-reminder>`
5. Claude sees memories naturally, without explicit tool calls

---

## Build System

```bash
cd chitta && cmake --build build --parallel
```

### Dependencies

| Library | Purpose |
|---------|---------|
| DuckDB | Storage engine |
| ONNX Runtime | Embedding inference |
| tree-sitter | Code parsing (+ 9 language grammars) |
| nlohmann_json | JSON handling |
| CRoaring | Bitmap operations |

### Outputs

| Target | Description |
|--------|-------------|
| `chittad` | Daemon binary |
| `chitta` | CLI binary |
| `duckdb_test` | Test binary |

---

*Version 3.30.1 — DuckDB backend, 8-phase resonance, self-tuning, xMemory themes.*
