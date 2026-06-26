# CC-Soul Architecture

Technical architecture of cc-soul v4.0, a persistent identity and memory system for Claude Code.

---

## Table of Contents

- [System Overview](#system-overview)
- [Components](#components)
- [Storage Layer (chitta-field)](#storage-layer-chitta-field)
- [Semantic Index (ANN)](#semantic-index-ann)
- [Cortical Index (SDR)](#cortical-index-sdr)
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
│  │ hooks/*.sh  │                 │ MCP Server     │           │
│  │ (context    │                 │ (chitta-mcp)   │           │
│  │  injection) │                 │                │           │
│  └──────┬──────┘                 └───────┬────────┘           │
│         │         Unix Socket            │                   │
│         └────────────┬───────────────────┘                   │
│                      ▼                                       │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │                    CHITTAD DAEMON (C++)                  │ │
│  │                                                         │ │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │ │
│  │  │ Thread Pool   │  │ RPC Handler  │  │ Subconscious │  │ │
│  │  │ (2-16 workers)│  │ (100+ tools) │  │ (background) │  │ │
│  │  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘  │ │
│  │         │                 │                  │          │ │
│  │         └────────────┬────┘──────────────────┘          │ │
│  │                      ▼                                  │ │
│  │  ┌─────────────────────────────────────────────────┐    │ │
│  │  │              DuckDBMind (C++ orchestrator)       │    │ │
│  │  │                                                  │    │ │
│  │  │  Embedder ←→ VakYantra (ONNX)                   │    │ │
│  │  │  ResonanceLearner (Bayesian self-tuning)         │    │ │
│  │  │  ThemeManager (xMemory)                          │    │ │
│  │  │  SessionContext (priming, topics)                 │    │ │
│  │  │                    │                             │    │ │
│  │  │          C FFI boundary                         │    │ │
│  │  │                    ▼                             │    │ │
│  │  │  ┌───────────────────────────────────────────┐  │    │ │
│  │  │  │        chitta-field (Rust library)         │  │    │ │
│  │  │  │                                            │  │    │ │
│  │  │  │  SemanticIndex (IVF + LSH ANN)             │  │    │ │
│  │  │  │  CorticalIndex (SDR sparse codes)          │  │    │ │
│  │  │  │  KeywordIndex (BM25)                       │  │    │ │
│  │  │  │  TripletStore (knowledge graph)            │  │    │ │
│  │  │  │  SymbolIndex + CallGraph (code intel)      │  │    │ │
│  │  │  │  TemporalIndex, ThemeOrgan, Registries     │  │    │ │
│  │  │  │            │                               │  │    │ │
│  │  │  │  ┌─────────┴──────────────────────────┐   │  │    │ │
│  │  │  │  │  ~/.claude/mind/chitta-field/       │   │  │    │ │
│  │  │  │  │  {inst_id}_{seqno}.seg (op logs)   │   │  │    │ │
│  │  │  │  │  chitta.snapshot (periodic dump)   │   │  │    │ │
│  │  │  │  └────────────────────────────────────┘   │  │    │ │
│  │  │  └───────────────────────────────────────────┘  │    │ │
│  │  └─────────────────────────────────────────────────┘    │ │
│  └─────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────┘
```

### Key Design Decisions

- **chitta-field** (pure Rust static library) as the storage layer — no external database, no NFS locks
- **Per-instance segment files** for multi-writer safety: each chittad instance owns its `{inst_id}_{seqno}.seg`
- **In-RAM indexes** rebuilt from op log on open; snapshots accelerate cold start
- **Unix domain socket** for IPC between Claude Code and the daemon
- **Auto-scaling thread pool** (2-16 workers) with watchdog for slow request detection

---

## Components

### Binaries

| Binary | Purpose | Source |
|--------|---------|--------|
| `chittad` | Daemon: socket server + RPC handler + subconscious | `chitta/src/simple_cli.cpp` |
| `chitta` | CLI tool: direct command-line access | `chitta/src/simple_cli.cpp` |

### Core Classes (C++ daemon)

| Class | File | Role |
|-------|------|------|
| `DuckDBMind` | `mind/duckdb_mind.hpp` | Central orchestrator: remember, recall, resonate, self-tune |
| `DuckDBRpcHandler` | `rpc/duckdb_handler.hpp` | JSON-RPC 2.0 handler, 100+ registered tools |
| `Embedder` | `mind/embedder.hpp` | Embedding with LRU cache and circuit breaker |
| `AntahkaranaYantra` | `vak_onnx.hpp` | ONNX Runtime inference for bge-base-en-v1.5 |
| `Subconscious` | `mind/subconscious.hpp` | Background thread: patterns, hygiene, embedding |
| `ThemeManager` | `theme_manager.hpp` | xMemory hierarchical memory organization |
| `CodeIntel` | `code_intel.hpp` | Tree-sitter symbol extraction (9 languages) |
| `SymbolResolver` | `symbol_resolver.hpp` | Cross-file symbol resolution for call graphs |
| `ThreadPool` | `rpc/thread_pool.hpp` | Auto-scaling worker pool with watchdog |
| `ProvenanceSpine` | `provenance.hpp` | Knowledge source tracking and trust scoring |

### Core Modules (Rust — chitta-field)

| Module | File | Role |
|--------|------|------|
| `ChittaField` | `field.rs` | Unified API: open, put_memory, recall, flush, snapshot |
| `SemanticIndex` | `hnsw.rs` | ANN search: IVF coarse quantizer + LSH probing |
| `CorticalIndex` | `organ/cortex.rs` | SDR sparse codes (64-of-16384 active bits), sub-ms recall |
| `KeywordIndex` | `organ/keyword.rs` | BM25 full-text search |
| `TripletStore` | `organ/triplet.rs` | Subject/predicate/object knowledge graph |
| `SymbolIndex` | `organ/symbol.rs` | Code symbols with semantic search |
| `CallGraph` | `organ/callgraph.rs` | Function call edges |
| `TemporalIndex` | `organ/temporal.rs` | Time-range queries with kind/realm filters |

---

## Storage Layer (chitta-field)

### Design

chitta-field is a pure Rust static library — no external database engine, no NFS lock manager. All state lives in RAM, written through to an append-only op log on disk.

```
~/.claude/mind/chitta-field/
├── {instance_id}_{first_seqno}.seg   # Op log segment (one per writer process)
├── chitta.snapshot                    # Full state snapshot (accelerates cold start)
├── chitta.hnsw                        # Serialized HNSW main graph
├── chitta.delta.hnsw                  # HNSW delta graph (active after 5 000 memories)
├── chitta.emb                         # Embedding sidecar (8-byte magic CTEMB + count + {u64 id, f32×EMBED_DIM} records)
└── chitta.bin                         # Binary/compressed sidecar
```

### Multi-writer model (Upanishads)

Each process that opens `ChittaField` is assigned a unique `InstanceId`. All writes go to `{instance_id}_{seqno}.seg`, owned exclusively by that process — no cross-process coordination required. On open, the library scans all `*.seg` files, replays them in sequence-number order, and reconstructs the full in-RAM state. This is safe on NFS without lock managers.

### Op Log

Each op log entry is a length-prefixed MessagePack frame with a CRC32 checksum:

```
[frame_len: u32] [crc32: u32] [op: msgpack]
```

Op types include: `PutMemory`, `UpdateState`, `Forget`, `AddAssocEdge`, `AddTriplet`, `UpsertSymbol`, `AddCallEdge`, `TranscriptEvent`, `TaskEvent`, `SessionEvent`.

### Memory record

| Field | Type | Description |
|-------|------|-------------|
| `memory_id` | `u64` | Monotonically allocated, per-instance |
| `chunk_hash` | `[u8; 32]` | SHA-256 of (kind, realm, content, embedding) for dedup |
| `kind` | `String` | Semantic category (wisdom, episode, ssl, fact, ...) |
| `realm` | `String` | Namespace / project scope |
| `content` | `Vec<u8>` | Raw bytes (typically UTF-8 text) |
| `embedding` | `Vec<f32>` | 1024-dim BGE embedding (bge-large-en-v1.5 default; build-time configurable via `CHITTA_EMBED_DIM`) |
| `confidence` | `f32` | 0.0-1.0 |
| `decay_rate` | `f32` | Strength loss per time unit; 0.0 = pinned |
| `strength` | `f32` | Current salience (decays over time) |
| `sparse_code` | `Option<SparseCode>` | 64 active bit indices into 16,384-dim cortex |
| `authored_at_ms` | `i64` | Original authorship timestamp |

### Snapshot acceleration

`cf_save_full_snapshot` serializes the entire in-RAM state (payloads, states, all indexes) to a single bincode file. On next open, the library reads the snapshot magic, finds the snapshot with the highest seqno using only 16 bytes per file (`peek_seqno`), loads that one, then replays only the op log entries that follow it.

Snapshot format is versioned (magic `0xF011_5741_7E00_0004`). Old snapshots (magic `...0003`) are transparently migrated on load. The current on-disk layout is also referred to as the "V23 sectioned format" — each major section (payloads, states, HNSW, cortex, keyword index, etc.) is written as a discrete tagged block, allowing partial reads and forward-compatible extension.

---

## Semantic Index (ANN)

The `SemanticIndex` (in `hnsw.rs`) provides approximate nearest-neighbour search via a two-tier HNSW graph, with IVF + LSH as a coarse candidate filter.

### Architecture

```
Query embedding
      │
      ▼
┌─────────────────────────────────┐
│  HNSW graph (main)              │  Single graph up to 2 000 memories
│  • Navigate greedy from entry   │  M=16, EF_SEARCH=64
│  • Collect ef candidates        │
└──────────────┬──────────────────┘
               │ (>= 5 000 memories: delta graph also queried)
               ▼
┌─────────────────────────────────┐
│  HNSW delta graph               │  Merges into main at DELTA_MERGE_RATIO=0.10
└──────────────┬──────────────────┘
               │
               ▼
┌─────────────────────────────────┐
│  IVF + LSH (candidate filter)   │  256 centroids, 4 tables × 12 bits
│  MIN_PROBES=6 … MAX_PROBES=24   │  1 024-16 384 candidates
└──────────────┬──────────────────┘
               │
               ▼
┌─────────────────────────────────┐
│  Exact cosine reranking         │  MIN_CANDIDATES=1 024 → top-k
└─────────────────────────────────┘
```

Flat scan is permanently disabled (`FLAT_SCAN_MAX=0`) as of 2026-06-11; ANN was validated at 154K memories before removal.

Per-realm HNSW graphs activate at `PER_REALM_HNSW_THRESHOLD=500` memories per realm.

Embedding mmap (`memmap2`) activates at `EMB_MMAP_MIN=500_000` memories; below that, embeddings are heap-allocated.

### Parameters

| Constant | Value | Source | Meaning |
|----------|-------|--------|---------|
| `HNSW_M` | 16 | `hnsw.rs:31` | Neighbours per node (inner layers) |
| `HNSW_M0` | 32 | `hnsw.rs:32` | Neighbours per node (layer 0) |
| `HNSW_EF_CONSTRUCTION` | 200 | `hnsw.rs:33` | Build-time candidate list size |
| `HNSW_EF_SEARCH` | 64 | `hnsw.rs:34` | Query-time candidate list size |
| `HNSW_ML` | 0.36067 | `hnsw.rs:36` | Level multiplier (1/ln(16)) |
| `HNSW_THRESHOLD` | 2 000 | `hnsw.rs:21` | Single-graph cutover |
| `HNSW_TIER2_THRESHOLD` | 5 000 | `hnsw.rs:27` | Delta-graph activation |
| `HNSW_DELTA_MERGE_RATIO` | 0.10 | `hnsw.rs:29` | Delta/main size ratio triggers merge |
| `PER_REALM_HNSW_THRESHOLD` | 500 | `hnsw.rs:39` | Per-realm graph activation |
| `FLAT_SCAN_MAX` | 0 | `hnsw.rs:49` | Flat scan disabled |
| `EMB_MMAP_MIN` | 500 000 | `hnsw.rs:56` | mmap activation threshold |
| `COARSE_CENTROIDS` | 256 | `hnsw.rs:11` | IVF random-projection partitions |
| `COARSE_ASSIGNMENTS` | 2 | `hnsw.rs:12` | Centroids per memory |
| `LSH_TABLES` | 4 | `hnsw.rs:15` | LSH hash tables |
| `LSH_BITS` | 12 | `hnsw.rs:16` | Bits per signature (4 096 buckets/table) |
| `MIN_PROBES` | 6 | `hnsw.rs:13` | Min centroid probes per query |
| `MAX_PROBES` | 24 | `hnsw.rs:14` | Max centroid probes per query |
| `MIN_CANDIDATES` | 1 024 | `hnsw.rs:17` | Floor on reranking pool |
| `MAX_CANDIDATES` | 16 384 | `hnsw.rs:18` | Cap before exact reranking |

The centroids and LSH planes are fixed random unit vectors seeded deterministically. The coarse index is persisted in snapshots; LSH structures are rebuilt from stored embeddings on load.

---

## Cortical Index (SDR)

The `CorticalIndex` (in `organ/cortex.rs`) provides sub-millisecond associative recall without a learned ANN structure.

Each memory's embedding is encoded into a **Sparse Distributed Representation**: exactly K=64 active features out of N=16,384. Encoding uses a product-key decomposition: when `EMBED_DIM=768`, the embedding is split into two 384-dim halves, each scored against 128-centroid sub-dictionaries, and the top-K atoms are selected from the 256-candidate shortlist — O(√N · d) instead of O(N · d). The split dimensions scale with `EMBED_DIM`.

Recall is a bitset intersection: query SDR vs memory SDR, count shared active bits. This is O(K) per candidate and runs in sub-millisecond at tens of thousands of memories.

A `ProductQuantizer` compresses residual embeddings (32 subvectors, 256 centroids each) to 32 bytes for scale.

### HDC Module (hdc.rs)

A standalone hyperdimensional computing module runs in parallel with the SDR cortical index. It uses 8192-bit binary vectors (128 `u64` words) and bag-of-words bundling: term vectors are XOR-combined via majority voting to produce document hypervectors. Recall is by Hamming distance. The HDC path is lighter-weight than the SDR path and can serve as a fast pre-filter or standalone recall mode.

### FEP Attractor Network (v5.3)

The encoder, prototype index, and association graph jointly minimize free energy per Spisak & Friston (Neurocomputing 2026):

- **Self-orthogonalizing encoder** — FEP-derived learning rule: prediction error + complexity penalty (λ=1e-4) + Gram-Schmidt decorrelation (1% per step) between co-active atoms. Representations naturally become orthogonal, resisting catastrophic forgetting.
- **Asymmetric prototype transitions** — Forward direction (a→b) gets full coupling delta; reverse (b→a) gets 0.3×. Sequential recall order encodes temporal asymmetry.
- **Attractor settle** — `CorticalIndex::attractor_settle()` iteratively blends query with prototype centroids and follows directed transitions (3-5 steps). Partial cues converge to stored attractor basins.
- **Surprise-modulated plasticity** — Reconstruction error at encode time updates `MemoryState.surprise`. High surprise → slow decay (unique info); low surprise → fast decay (redundant).
- **Adaptive vigilance** — Prototype creation threshold adjusts based on aggregate reconstruction error. High error → more prototypes; low error → coarser clustering.
- **Hopfield network** (`organ/hopfield.rs`) — Asymmetric energy-based attractor network over memory co-activations. `settle()` propagates activation through multi-hop directed couplings with dynamic neighbor discovery.
- **Free-energy merge criterion** — `find_dup_pairs` checks whether merging reduces total free energy (accuracy loss vs complexity gain) instead of using a fixed cosine threshold.

---

## Iterative Resonance (CTM-inspired)

Inspired by the Continuous Thought Machine (Sakana AI, 2025), recall is not a single forward pass. `full_resonate()` runs up to 3 iterative passes per query, refining the query embedding toward the retrieved memory centroid each pass.

### Pass loop

```
q₀ = original query embedding (anchor — never modified)
qₜ = q₀

for t in 0..3:
    results = semantic_recall(qₜ) + bm25(qₜ) + spreading_activation(results)
    H_t = entropy(score_distribution(results))

    if |H_t - H_{t-1}| < 0.01 or top-k IDs unchanged:
        break  ← early stop

    qₜ₊₁ = normalize(0.7·q₀ + 0.3·mean(top-k embeddings))
            ↑ anchored to q₀ — prevents drift
```

### Post-pass learning

After the final pass, `cf_record_recall_batch` atomically commits all learning in chitta-field (Rust):

| Learning | Mechanism |
|----------|-----------|
| Access touch | `access_count++`, `last_accessed_ms` updated per retrieved memory |
| Retrieval context | 32-dim quantized query sketch appended to `RetrievalHistory` (FIFO, 8 entries) |
| Retrieval signature | Cached mean of stored context sketches — used to boost future recall |
| Co-activation stats | `CoActivationStats.sim_count` and `diversity_count` updated for every co-retrieved pair |
| Hebbian edges | `CoRetrieved` assoc edge weight += `base_delta × (sim_count × diversity_count)`, capped at 16× |

### Context-aware reranking

`SemanticIndex::search_with_signature_boost` adds a retrieval-signature prior to cosine scoring:

```
final_score = cosine(query, embedding) + β × max(0, dot(query_ctx_32, memory_signature_32))
```

Memories with empty retrieval history receive zero boost. The C++ layer passes a 32-dim projected query sketch; the boost is computed entirely in Rust during the final reranking step.

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
| `Artha` | Meaning | 1024-dim embedding vector + certainty (default; matches `EMBED_DIM`) |
| `AntahkaranaYantra` | Inner instrument | ONNX Runtime inference engine |
| `SmritiYantra` | Memory machine | Caching wrapper (LRU, 10000 entries) |
| `ShantaYantra` | Silent machine | Zero-vector fallback |

### Model

- **Model**: bge-large-en-v1.5 (default, `EMBED_DIM=1024`); build-time configurable via `CHITTA_EMBED_DIM` (must be a multiple of 64)
- **Dimensions**: 1024 (default)
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

Vector similarity search via chitta-field's `SemanticIndex` (IVF + LSH ANN). Returns top-k memories by cosine similarity to query embedding. For small pre-filtered candidate sets (realm filter), falls back to exact cosine over the allowed set.

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

### ScoringPipeline (v5.11+)

Post-resonance scoring is handled by the `ScoringPipeline`, a neuroplastic trait-based architecture that replaces the previous hardcoded scoring constants. The pipeline applies 18 composable scoring factors in sequence:

| # | Factor | Source | Description |
|---|--------|--------|-------------|
| 1 | Relevance | Cosine similarity | Base semantic match score |
| 2 | ACT-R | Access history | Anderson & Schooler (1991) power-law decay over access timestamps |
| 3 | Strength | Memory strength | Reinforcement from repeated access |
| 4 | Confidence | Bayesian confidence | Mean of beta posterior over observation history |
| 5 | Surprise | FEP §2.3 | Reconstruction error as surprise signal |
| 6 | Arousal | Affect dimensions | Flashbulb memory effect: high-arousal memories boosted |
| 7 | MoodCongruence | Query affect | Bower (1981): valence/arousal alignment between query and memory |
| 8 | FrustrationEscalation | Query affect | Negative valence + high arousal boosts corrections/preferences |
| 9 | Status | Demotion tier | Tier-based weight (hot > warm > cool > cold) |
| 10 | Epistemic | Node type | Type-based multiplier (corrections, preferences weighted higher) |
| 11 | Kind | Node kind | Fine-grained kind multiplier |
| 12 | RealmReliability | Realm stats | Per-realm reliability based on historical feedback |
| 13 | InterferenceDensity | Competitor crowding | Price of Meaning: penalty for local competitor density |
| 14 | SpacingBoost | Access spacing | Geometry of Forgetting: well-spaced accesses boost recall |
| 15 | PredictionBoost | Markov chain | Layer 3: predicted-next-needed memories get boosted |
| 16 | SurpriseDomain | Surprise Memory | Layer 4: memories that were "actual" in a surprise event get boosted; "expected" get suppressed |
| 17 | EpistemicDebt | Epistemic Debt | Layer 5: memories in domains with open uncertainty get boosted |
| 18 | IntegrationWeight | Integration Kernel | Layer 6: learned recall source weight applied as multiplier |

Each factor has independent `weight`, `bias`, and type-specific parameters. All configuration lives in `scoring.json` with hot-reload support — no rebuild required to tune scoring behavior.

```
final_score = Σ (factor_weight × factor_score + factor_bias)
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

Before storing, `DuckDBMind::remember()` applies (pre-flight in C++ before calling the chitta-field FFI):

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
    int8_t data[EMBED_DIM];   // EMBED_DIM bytes (vs 4×EMBED_DIM bytes for float32)
    float scale, offset;       // Reconstruction: float = data[i] * scale + offset
    // 75% storage savings at default EMBED_DIM=1024
};
```

### Binary Vectors

```cpp
struct BinaryVector {
    uint64_t bits[EMBED_DIM/64];  // EMBED_DIM bits, one per dimension
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
| SessionStart | `session-start-hook.sh` | Load soul context, start daemon |
| UserPromptSubmit | `prompt-hook.sh` | Surface relevant memories |
| Stop | `stop-hook.sh` | Auto-learning, checkpoints, ledger save |
| PreCompact | `pre-compact-hook.sh` | Save state before compaction |
| PreToolUse | `pre-tool-hook.sh` | Surface file memories, command corrections |
| PostToolUse | `post-bash-hook.sh` | Record significant file changes |

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
3. `prompt-hook.sh` extracts the message and calls `full_resonate`
4. Relevant memories are injected into Claude's context as `<system-reminder>`
5. Claude sees memories naturally, without explicit tool calls

---

## Build System

```bash
# 1. Build chitta-field (Rust static library)
cd chitta-field && ./build.sh build --release && cd ..

# 2. Build C++ daemon (links libchitta_field.a)
cd chitta && cmake --build build --parallel
```

### Dependencies

| Library | Purpose |
|---------|---------|
| chitta-field | Memory substrate (Rust static lib, included as submodule) |
| ONNX Runtime | Embedding inference |
| tree-sitter | Code parsing (+ 9 language grammars) |
| nlohmann_json | JSON handling |
| CRoaring | Bitmap operations |

**chitta-field Rust deps (key)**: `serde`/`serde_json`/`rmp-serde` (op serialization), `bincode` (snapshots), `memmap2` (embedding mmap), `parking_lot` (locks), `crc32fast`, `sha2`, `rayon`, `roaring`, `smallvec`, `pyo3` (Python FFI bindings).

### Outputs

| Target | Description |
|--------|-------------|
| `chittad` | Daemon binary |
| `chitta` | CLI binary |
| `libchitta_field.a` | Rust memory substrate (linked into chittad) |

---

## References

Theoretical foundations and inspirations cited throughout the codebase.

### Memory & Recall

| Concept | Source | Used In |
|---------|--------|---------|
| ACT-R base-level activation | Anderson, J.R. & Schooler, L.J. (1991). "Reflections of the environment in memory." *Psychological Science*, 2(6), 396–408. | `chitta-field/src/scoring/factors.rs` — power-law decay over access timestamps |
| Free Energy Principle (surprise) | Friston, K. (2010). "The free-energy principle: a unified brain theory?" *Nature Reviews Neuroscience*, 11(2), 127–138. | `chitta-field/src/scoring/factors.rs` — reconstruction error as surprise signal |
| Mood-congruent memory | Bower, G.H. (1981). "Mood and memory." *American Psychologist*, 36(2), 129–148. | `chitta-field/src/scoring/factors.rs` — valence/arousal alignment between query and memory |
| Flashbulb memory | Brown, R. & Kulik, J. (1977). "Flashbulb memories." *Cognition*, 5(1), 73–99. | `chitta-field/src/scoring/factors.rs` — high-arousal memories boosted |

### Interference & Forgetting

| Concept | Source | Used In |
|---------|--------|---------|
| Price of Meaning (no-escape theorem) | Arora, S. et al. (2023). "The Price of Meaning: On the Computational Costs of Rich Representations." Workshop paper. | `chitta-field/src/scoring/factors.rs` — interference density penalty; `chitta-field/src/store.rs` — lure detection |
| Geometry of Forgetting | Sorscher, B. et al. (2022). "The neural population geometry of forgetting." *Neural Information Processing Systems*. | `chitta-field/src/scoring/factors.rs` — spacing boost; `chitta-field/src/store.rs` — per-realm embedding geometry |

### Context Engineering

| Concept | Source | Used In |
|---------|--------|---------|
| Latent Briefing (KV cache compaction) | Ramp Labs (2025). "Latent Briefing: KV Cache Compaction for Multi-Agent LLM Orchestration." Technical report. | `chitta/include/chitta/rpc/handlers/trajectory_compact.hpp` — attention-weighted turn selection with MAD thresholding |
| Personal Brain OS (progressive disclosure) | Koylan (2025). "Building a Personal Brain OS with Claude." Blog post / X thread. | Context assembly pattern in `smart_context` — L1 router → L2 module → L3 data |
| xMemory (hierarchical theme retrieval) | Wu, Y. et al. (2024). "xMemory: A Hierarchical Memory System for LLM Agents." *arXiv preprint*. | `chitta/include/chitta/rpc/handlers/field_misc.hpp` — theme system, two-stage retrieval |
| Letta (MemGPT) context repository | Packer, C. et al. (2023). "MemGPT: Towards LLMs as Operating Systems." *arXiv preprint*. | `chitta/include/chitta/rpc/handlers/field_memory_ops.hpp` — memory versioning, merge queue |

### Scoring & Search

| Concept | Source | Used In |
|---------|--------|---------|
| Reciprocal Rank Fusion | Cormack, G.V. et al. (2009). "Reciprocal Rank Fusion outperforms Condorcet and individual Rank Learning Methods." *SIGIR 2009*. | `chitta/include/chitta/rpc/field_handler.hpp` — hybrid recall (BM25 + semantic) |
| BM25 | Robertson, S.E. et al. (1994). "Okapi at TREC-3." *NIST Special Publication*. | `chitta-field/src/store.rs` — keyword recall |
| HNSW | Malkov, Y.A. & Yashunin, D.A. (2020). "Efficient and robust approximate nearest neighbor search using HNSW graphs." *IEEE TPAMI*. | `chitta-field/src/store.rs` — semantic recall index |
| MAD thresholding | Leys, C. et al. (2013). "Detecting outliers: Do not use standard deviation around the mean." *Journal of Experimental Social Psychology*. | `trajectory_compact.hpp` — adaptive turn selection threshold |

### Meta-Memory (Layers 4-6)

| Concept | Source | Used In |
|---------|--------|---------|
| Free Energy Principle (prediction error) | Friston, K. (2010). "The free-energy principle: a unified brain theory?" *Nature Reviews Neuroscience*, 11(2), 127–138. | `chitta-field/src/organ/surprise.rs` — surprise memory store; `scoring/factors.rs` — SurpriseDomainFactor |
| Epistemic vigilance | Sperber, D. et al. (2010). "Epistemic vigilance." *Mind & Language*, 25(4), 359–393. | `chitta-field/src/organ/epistemic_debt.rs` — uncertainty tracking with fragility scores |
| Mixture of Experts gating | Shazeer, N. et al. (2017). "Outrageously large neural networks: the sparsely-gated mixture-of-experts layer." *ICLR 2017*. | `chitta-field/src/organ/integration.rs` — learned recall source weights |

---

## Meta-Memory Layers (v5.13+)

Six organ layers extend core recall with structured metacognition. Each layer follows the organ pattern: Rust store → WAL replay → FFI → C++ handlers → MCP tools.

### Layer 1: Executable Constraints

Prolog-style logic engine embedded in chitta-field. Facts are asserted/retracted at runtime; unification and chain queries execute against the fact base.

| Tool | Description |
|------|-------------|
| `assert_fact` | Assert a Prolog-style fact (e.g., `prefers(user, rust)`) |
| `retract_fact` | Retract a previously asserted fact |
| `query_unify` | Unify a query pattern against the fact base |
| `query_chain` | Chain multiple query patterns with shared variables |
| `explain_fact` | Show provenance chain for a derived fact |
| `branch_create` | Create a hypothetical branch of the fact base |
| `branch_resolve` | Merge or discard a hypothetical branch |

### Layer 2: Trigger Tissue

Event-condition-action rules that fire automatically when conditions are met.

| Tool | Description |
|------|-------------|
| `trigger_add` | Define a trigger with event pattern, condition, and action |
| `trigger_list` | List active triggers with match counts |
| `trigger_fire` | Manually fire a trigger |
| `trigger_dismiss` | Dismiss/deactivate a trigger |

### Layer 3: Predictive Memory

Markov chain access predictor that learns transition probabilities between memory accesses.

| Tool | Description |
|------|-------------|
| `predict_needed` | Get predicted next-needed memories from the access predictor |

### Layer 4: Surprise Memory

Prediction error tuples that reveal blind spots. Records what was expected vs what actually happened. Recurring surprise patterns surface as "blind spots" — domains/actions where predictions consistently fail.

**WAL op code**: `OP_RECORD_SURPRISE = 39`

**Scoring**: `SurpriseDomainFactor` — memories that were the "actual" outcome in a surprise event get boosted (`1 + 0.15 * magnitude`); memories that were the "expected" (wrong) prediction get suppressed (`1 - 0.10 * magnitude`).

| Tool | Description |
|------|-------------|
| `record_surprise` | Record a prediction error event with context, action, expected/actual, magnitude, domain |
| `query_surprises` | Filter surprise events by domain, realm, magnitude, time |
| `get_blind_spots` | Aggregated surprise patterns — domains/actions where predictions consistently fail |
| `surprise_stats` | Summary statistics: counts, avg magnitude, domain breakdown |

### Layer 5: Epistemic Debt

Uncertainty boundaries and competing hypotheses. Tracks beliefs that are fragile — where multiple explanations compete and no discriminating test has been applied.

**WAL op codes**: `OP_REGISTER_DEBT = 40`, `OP_UPDATE_DEBT = 41`

**Scoring**: `EpistemicDebtFactor` — memories in domains with open epistemic debt get a `1.1x` boost (surfacing relevant context when uncertainty is active).

| Tool | Description |
|------|-------------|
| `register_debt` | Register an uncertainty with competing hypotheses and fragility score |
| `resolve_debt` | Mark a debt as resolved with explanation |
| `defer_debt` | Defer a debt for later investigation |
| `query_debts` | Filter debts by status, domain, fragility |
| `get_fragile_decisions` | Open debts sorted by fragility — decisions most likely to be wrong |
| `debt_stats` | Summary statistics: counts by status, avg fragility |

### Layer 6: Integration Kernel

Recall source arbitration with learned weights. Tracks which recall sources (semantic, keyword, temporal, artifact, association) are useful per domain, updating weights via Bayesian feedback.

**WAL op codes**: `OP_UPDATE_SOURCE_WEIGHT = 42`, `OP_RECORD_FEEDBACK = 43`

**Scoring**: `IntegrationWeightFactor` — applies the learned source weight as a multiplier (default 1.0, range [0, 2]).

| Tool | Description |
|------|-------------|
| `record_feedback` | Record whether a recall source was useful for a domain |
| `get_source_weights` | View learned source weights per domain |
| `update_source_weight` | Manual weight override |
| `integration_stats` | Per-source success rates across all domains |

### Autonomous Learning (Moves 1-6)

Closes feedback loops from prediction errors to memory adaptation.

**WAL op codes (bytes 44-48)**:
- `OP_SURPRISE_CREDIT = 44` — SurpriseLearning hysteresis gate (`|credit|>=0.75 AND same_dir_streak>=2`); strength delta = `±min(0.08, 0.02+0.06×excess)`
- `OP_UPSERT_WISDOM_CANDIDATE = 45` / `OP_UPDATE_WISDOM_LIFECYCLE = 46` — WisdomPromotion (thresholds: `support_count>=4`, `cross_session_count>=2`, `promotion_score>=0.72`, `contradiction_count==0`)
- `OP_UPDATE_SCORER = 47` — LearnedScoringModel weight delta
- `OP_ATTACH_DEBT_EVIDENCE = 48` — AttachDebtEvidence link

| Tool | Description |
|------|-------------|
| `surprise_learning_stats` | Rolling surprise credit stats — tracked memories, gates passed |
| `upsert_wisdom_candidate` | Create/update wisdom candidate from clustered surprises |
| `update_wisdom_lifecycle` | Advance candidate: candidate → provisional → trusted → demoted |
| `query_wisdom_candidates` | Query by lifecycle stage and/or domain |
| `wisdom_promotion_stats` | Pipeline overview — candidates by lifecycle stage |
| `attach_debt_evidence` | Attach evidence (memory IDs + confidence) to an epistemic debt |
| `update_scorer_model` | Apply learned weight deltas from outcome calibration |
| `learned_scorer_stats` | Current model version, factor count, loss |
| `effective_scorer_weights` | Baseline + learned deltas for all scoring factors |

### Layer 7: Intervention Ledger

WAL-backed organ tracking agent actions before execution, observations during execution, and causal attribution after outcome. Closes the loop from "what did I intend?" → "what did I observe?" → "why did it succeed or fail?" → learning subsystem update.

**WAL op codes**: `OP_START_INTERVENTION = 49`, `OP_ADD_OBSERVATION = 50`, `OP_CLOSE_INTERVENTION = 51`, `OP_RECORD_ATTRIBUTION = 52`

**Attribution routing**: `MemoryRecallError` → surprise credit; `SourceTrustError` → integration kernel weight decrease; `ProcedureError` → skill memory demotion. Secondary class supported at 0.5× confidence.

**Stale auto-close**: subconscious learning cycle closes interventions open longer than 30 minutes.

| Tool | Description |
|------|-------------|
| `start_intervention` | Begin tracking before execution — intent, action type, preconditions, expected observables |
| `add_observation` | Record observation during execution (stdout, diff, test result, user feedback) |
| `close_intervention` | Close with outcome status (Succeeded=1, Failed=2, Partial=3, Aborted=4) |
| `record_attribution` | Attribute to causal class and route feedback to learning subsystem |
| `query_interventions` | Query ledger by realm/session/status |
| `get_intervention` | Get single intervention with observations and attributions |
| `intervention_stats` | Ledger statistics — total, open, succeeded, failed counts |
| `list_open_interventions` | All currently open (in-progress) interventions |

### Layer 8: Agent Protocol Memory

WAL-backed organ for tracking multi-step task ownership across agent loops. Agents lose causality because they have no record of what they delegated, what evidence they produced, or what questions remain open. Layer 8 makes delegation, evidence provenance, and completion criteria first-class persistent objects.

**WAL op codes**: `OP_REGISTER_TASK = 53`, `OP_UPDATE_TASK = 54`, `OP_ADD_DELEGATION = 55`, `OP_LINK_EVIDENCE = 56`, `OP_ADD_PROBE = 57`, `OP_RESOLVE_PROBE = 58`, `OP_SET_CRITERION = 59`

**Data model**:
- `TaskContract` — goal, constraints, acceptance_criteria, priority, status FSM (Active/Blocked/Completed/Failed/Abandoned), deadline, parent_task_id, tags
- `DelegationEdge` — from_agent → to_agent with handoff_note; status (Active/Completed/Recalled)
- `EvidenceLink` — memory_id linked to task with EvidenceKind (Observation/Artifact/Result/Analysis/UserFeedback), relevance, producer. Idempotent by (task_id, memory_id).
- `PendingProbe` — open question with expected_answerer, priority; resolves to Answered/Dismissed
- `CompletionCriterion` — criterion text upserted idempotently; marked met/unmet with evidence note

**Subconscious integration**: learning cycle auto-completes tasks whose all criteria are met (`tasks_auto_completed` stat).

| Tool | Description |
|------|-------------|
| `register_task` | Register task contract with goal, constraints, criteria, priority, deadline |
| `update_task` | Update status; optionally attach intervention or tag |
| `add_delegation` | Record agent handoff with from/to/handoff_note |
| `link_evidence` | Link memory to task as typed evidence (idempotent by task_id, memory_id) |
| `add_probe` | Add open question blocking/informing task completion |
| `resolve_probe` | Mark probe Answered or Dismissed with answer text |
| `set_criterion` | Upsert completion criterion (idempotent by text); mark met/unmet |
| `get_task` | Full task view — contract, delegations, evidence, probes, criteria |
| `query_tasks` | Filter by realm/session/status/priority |
| `agent_protocol_stats` | Total tasks, delegations, evidence links, probes, criteria counts |

---

*Version 5.17 — Agent protocol memory (Layer 8, 10 MCP tools, WAL ops 53-59), intervention ledger (Layer 7, 8 MCP tools, WAL ops 49-52), autonomous learning pipeline (surprise credit, wisdom promotion, learned scorer), subconscious learning cycle, 18-factor scoring pipeline, chitta-field organic substrate.*
