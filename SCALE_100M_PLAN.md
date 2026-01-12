# cc-soul 100M+ Scale Implementation Plan

## Status: All 12 Components Complete (Jan 2026)

### Implementation Status:
| Component | Header | Wired | Notes |
|-----------|--------|-------|-------|
| QueryRouter | ✅ | ✅ | `recall()` routes by intent |
| QuotaManager | ✅ | ✅ | `remember()` checks quotas |
| UtilityDecay | ✅ | ✅ | `recall()` + `feedback()` tracked |
| AttractorDampener | ✅ | ✅ | `recall()` dampens over-retrieved |
| ProvenanceSpine | ✅ | ✅ | `remember()` records source metadata |
| TruthMaintenance | ✅ | ✅ | `recall()` annotates conflicts |
| RealmScoping | ✅ | ✅ | `recall()` filters by realm |
| SynthesisQueue | ✅ | ✅ | `recall()` tracks staged wisdom |
| EvalHarness | ✅ | ✅ | RPC: `eval_run`, `eval_add_test` |
| EpiplexityTest | ✅ | ✅ | RPC: `epiplexity_check`, `epiplexity_drift` |
| ReviewQueue | ✅ | ✅ | RPC: `review_list`, `review_decide`, `review_stats` |
| GapInquiry | ✅ | ✅ | `recall()` tracks gap encounters |

**Enable via MindConfig:**
```cpp
// Core scale components
config.enable_quota_manager = true;
config.enable_utility_decay = true;
config.enable_attractor_dampener = true;

// Priority 1 components
config.enable_provenance = true;
config.enable_realm_scoping = true;
config.enable_truth_maintenance = true;
config.session_id = "session-123";
config.default_realm = "project:my-project";

// Priority 3 components
config.enable_query_routing = true;  // Intent-based query routing
```

## Overview
Target: 100M+ nodes on single node (128GB RAM, NVMe SSD)
Latency: p50 <100ms, p99 <500ms
Workload: 10-50 QPS burst, 100-1000 inserts/day

## Phase 1: Critical Infrastructure [COMPLETE]

### 1.1 GraphStore Mmap Rewrite [DONE]
**Goal**: Eliminate in-memory triplet duplication, enable 100M triplets
**Files**: `mmap.hpp`, `mmap_graph_store.hpp`
**Completed**:
- Added `resize()` method to MappedRegion class
- Created MmapGraphStore with CSR format for subject/object lookups
- Mmap-backed arrays with O(1) lookups
- Streaming iteration support

### 1.2 ConnectionPool Scale-up [DONE]
**Goal**: Remove 16GB hard cap, support 100M HNSW connections
**Files**: `connection_pool.hpp`
**Completed**:
- MAX_SIZE: 16GB → 256GB (configurable)
- Added `compact()` method with OffsetUpdateCallback
- GROWTH_FACTOR: 2.0 → 1.5 with 64MB alignment
- Added `fragmentation()` metric

### 1.3 64-bit Offsets [DONE]
**Goal**: Remove 4GB/32-bit limits throughout
**Files**: `quantized.hpp`, `blob_store.hpp`, `unified_index.hpp`
**Completed**:
- NodeMeta: vector_offset, payload_offset, edge_offset → uint64_t
- BlobStore: MAX_SIZE → 256GB
- UNIFIED_VERSION: 1 → 2

## Phase 2: Query Performance [COMPLETE]

### 2.1 Indexed Retrieval Path [DONE]
**Goal**: Remove O(N) scans from query path
**Files**: `unified_index.hpp`
**Completed**:
- `search_two_stage()` now uses HNSW-based O(log N) search
- Deprecated `search_binary_brute()` (O(N))

### 2.2 BM25 Segmentation [DONE]
**Goal**: Scale BM25 to 100M documents
**Files**: `scoring.hpp`
**Completed**:
- MAX_DOCUMENTS: 10M → 100M
- MAX_VOCAB: 10M

### 2.3 TagIndex Compaction
**Goal**: Efficient tag filtering at 100M nodes
**Status**: SlotTagIndex with roaring bitmaps already in place

## Phase 3: 12 Recommendations [COMPLETE]

### 3.1 Query Compass Router [DONE]
**Files**: `query_router.hpp`
- Intent classification: TripletLookup, TagFilter, SemanticSearch, ExactMatch, Hybrid
- Confidence-based routing with fallback chain

### 3.2 Type Quotas & Budgeter [DONE]
**Files**: `quota_manager.hpp`
- Configurable quotas per NodeType
- LRU eviction by confidence
- Budget alerts and auto-eviction

### 3.3 Utility-Calibrated Decay [DONE]
**Files**: `utility_decay.hpp`
- Usage tracking (recall_count, positive/negative feedback)
- Survival curves: frequently-used decays slower
- Adaptive decay rate calculation

### 3.4 Provenance Spine [DONE]
**Files**: `provenance.hpp`
- Full metadata: source, session, tool, user, timestamp
- Trust scoring folded into confidence
- Trust filters at recall time

### 3.5 Contradiction Loom [DONE]
**Files**: `truth_maintenance.hpp`
- Explicit Contradicts edge tracking
- Resolution nodes with rationale
- Conflict surfacing at query time

### 3.6 Realm Scoping Graph [DONE]
**Files**: `realm_scoping.hpp`
- Realm nodes with ScopedTo edges
- Gate recall by current realm
- Cross-realm transfer with inheritance

### 3.7 Two-Stage Wisdom Foundry [DONE]
**Files**: `synthesis_queue.hpp`
- Staging queue for new wisdom
- Evidence requirements for promotion
- Quarantine period before integration

### 3.8 Attractor Dampener [DONE]
**Files**: `attractor_dampener.hpp`
- Hebbian update limits per node
- Over-retrieval detection and decay boost
- Diversity injection metrics

### 3.9 Golden Recall Harness [DONE]
**Files**: `eval_harness.hpp`
- Canonical query sets with expected results
- Precision/recall/F1 metrics
- Seed reconstruction validation

### 3.10 Epiplexity Self-Test [DONE]
**Files**: `epiplexity_test.hpp`
- LLM reconstruction testing
- ε drift detection and alerts
- Historical measurement tracking

### 3.11 Wisdom Review Queue [DONE]
**Files**: `review_queue.hpp`
- Accept/reject/edit/defer workflow
- Quality ratings and feedback
- Batch review mode with persistence

### 3.12 Gap-Driven Inquiry [DONE]
**Files**: `gap_inquiry.hpp`
- Generate questions from Gap nodes
- Priority queue by importance/encounters
- Answer storage and resolution tracking

## Phase 4: Integration & Testing [COMPLETE]

### 4.1 Integration Testing [DONE]
**Tests added for all Phase 3 components:**
- QueryRouter: intent classification tests
- QuotaManager: type quotas and eviction
- UtilityDecay: usage tracking and adaptive decay
- ProvenanceSpine: trust filtering
- TruthMaintenance: contradiction detection/resolution
- RealmScoping: visibility and isolation
- SynthesisQueue: staging and promotion
- AttractorDampener: over-retrieval dampening
- EvalHarness: golden recall tests
- EpiplexityTest: compression quality
- ReviewQueue: human oversight workflow
- GapInquiry: active learning

### 4.2 Scale Benchmarks [DONE]
**10K nodes benchmark results:**
- Insert: 1,965 ops/sec
- Search: 18 ms/query (100 queries avg)
- Lookup: 145 us/lookup

### 4.3 Migration Path
- UNIFIED_VERSION bumped to 2 for new NodeMeta format
- Backward compatibility via WAL replay

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Memory blowup during build | Streaming builders, checkpoints |
| Query regression | Golden Recall Harness, A/B testing |
| Data corruption | WAL + snapshots, checksums |
| Latency spikes | Background compaction, rate limiting |

## Success Metrics

- [ ] 100M nodes loaded in <1 hour
- [ ] p50 query latency <100ms
- [ ] p99 query latency <500ms
- [ ] Memory usage <100GB at 100M nodes
- [x] All 12 recommendations implemented
- [x] All 12 components wired into Mind
- [ ] Golden Recall Harness passing (needs test data)

## What's Missing (Next Steps)

### Priority 1: Core Runtime Wiring [DONE]
1. ~~**ProvenanceSpine** → `remember()`: Record source metadata on every insert~~
2. ~~**RealmScoping** → `recall()`: Filter results by current realm~~
3. ~~**TruthMaintenance** → `recall()`: Annotate conflicts in results~~

### Priority 2: RPC/CLI Exposure [DONE]
4. ~~**EvalHarness** → `eval_run`, `eval_add_test` RPC tools~~
5. ~~**ReviewQueue** → `review_list`, `review_decide`, `review_stats` RPC tools~~
6. ~~**EpiplexityTest** → `epiplexity_check`, `epiplexity_drift` RPC tools~~

### Priority 3: Pipeline Integration [DONE]
7. ~~**SynthesisQueue** → `recall()` tracks staged wisdom recalls~~
8. ~~**GapInquiry** → `recall()` tracks gap encounters~~
9. ~~**QueryRouter** → Routes queries based on intent classification~~

### Priority 4: Delete/Eviction Support (Future)
10. WAL delete entries for proper node removal
11. Full eviction in `maybe_evict_for_quota()`

### Nice to Have (Future)
- Persistence for UtilityDecay, AttractorDampener state
- Persistence for ProvenanceSpine, RealmScoping, TruthMaintenance state
- Persistence for SynthesisQueue, GapInquiry state
- Cross-session realm context
- Batch review CLI mode
