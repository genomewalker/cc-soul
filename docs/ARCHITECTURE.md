# CC-Soul Architecture

This document provides a deep technical dive into the CC-Soul architecture, covering the memory system, resonance engine, storage layers, and integration with Claude Code.

---

## Table of Contents

- [System Overview](#system-overview)
- [Memory Model](#memory-model)
- [The Resonance Engine](#the-resonance-engine)
- [Storage Architecture](#storage-architecture)
- [Multi-Instance Support](#multi-instance-support)
- [Subconscious Processing](#subconscious-processing)
- [Integration Layer](#integration-layer)
- [Performance Characteristics](#performance-characteristics)
- [Phase 7: Quality Infrastructure](#phase-7-quality-infrastructure)

---

## System Overview

CC-Soul implements a three-layer cognitive architecture inspired by biological memory systems:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           CONSCIOUS LAYER                                │
│                                                                          │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐              │
│  │    User      │───▶│    Claude    │───▶│    Tools     │              │
│  │   Prompt     │    │   (LLM)      │    │   (MCP)      │              │
│  └──────────────┘    └──────┬───────┘    └──────────────┘              │
│                             │                                            │
│                             │ transparent injection                      │
│                             ▼                                            │
├─────────────────────────────────────────────────────────────────────────┤
│                         SUBCONSCIOUS LAYER                               │
│                                                                          │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐              │
│  │  Synthesis   │    │  Attractor   │    │   Hebbian    │              │
│  │  (episodes   │    │  Dynamics    │    │  Learning    │              │
│  │  → wisdom)   │    │  (settling)  │    │  (edges)     │              │
│  └──────────────┘    └──────────────┘    └──────────────┘              │
│                             │                                            │
│                             │ daemon cycle (60s)                         │
│                             ▼                                            │
├─────────────────────────────────────────────────────────────────────────┤
│                        LONG-TERM MEMORY LAYER                            │
│                                                                          │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                         CHITTA                                    │   │
│  │                                                                   │   │
│  │  ┌─────────┐   ┌─────────┐   ┌─────────┐   ┌─────────┐         │   │
│  │  │  Nodes  │───│  Edges  │───│  Index  │───│   WAL   │         │   │
│  │  │ (graph) │   │(semantic)│   │ (HNSW)  │   │(durability)│       │   │
│  │  └─────────┘   └─────────┘   └─────────┘   └─────────┘         │   │
│  │                                                                   │   │
│  │  Storage: ~/.claude/mind/chitta                                  │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### Design Principles

1. **Biological Inspiration**: Memory decays, strengthens with use, forms associations
2. **Semantic Foundation**: 384-dim embeddings enable meaning-based retrieval
3. **Transparent Operation**: Memory surfaces automatically, no explicit commands
4. **Concurrent Access**: Multiple processes share the same soul safely
5. **Graceful Degradation**: System remains functional even with partial data

---

## Memory Model

### Node Structure

Every piece of memory is a **Node**:

```cpp
struct Node {
    // Identity
    NodeId id;              // 128-bit UUID (high: timestamp, low: random)
    NodeType node_type;     // Wisdom, Belief, Episode, etc.

    // Semantic Content
    Vector nu;              // 384-dimensional embedding
    std::vector<uint8_t> payload;  // Raw text content

    // Confidence (Bayesian)
    Confidence kappa;       // mu, sigma_sq, n, tau

    // Temporal
    Timestamp tau_created;  // Creation time (ms since epoch)
    Timestamp tau_accessed; // Last access time
    float lambda;           // Decay rate (0.01 - 0.15)

    // Graph Structure
    std::vector<Edge> edges;      // Outgoing connections
    std::vector<std::string> tags; // Filtering tags
};
```

### Node Types (18 total)

| Type | Purpose | Decay Rate | Example |
|------|---------|------------|---------|
| `Wisdom` | Proven patterns | 0.02 | "LRU caching works best for API responses" |
| `Belief` | Guiding principles | 0.01 | "Simplicity over complexity" |
| `Failure` | Lessons learned | 0.02 | "Never use floats for money" |
| `Episode` | Experiences | 0.05 | "Fixed auth bug in project X" |
| `Intention` | Active goals | 0.10 | "Implement user authentication" |
| `Aspiration` | Long-term visions | 0.03 | "Become expert in distributed systems" |
| `Dream` | Possibilities | 0.03 | "What if we used event sourcing?" |
| `Term` | Vocabulary | 0.01 | "Idempotent: same result on repeat" |
| `Question` | Knowledge gaps | 0.08 | "How does Kubernetes networking work?" |
| `Identity` | Self-knowledge | 0.005 | "I am Claude with persistent memory" |
| `Invariant` | Unchanging truths | 0.001 | "I value honesty and accuracy" |
| `Voice` | Antahkarana perspectives | 0.005 | "Buddhi says: analyze deeply" |
| `Meta` | System knowledge | 0.01 | "My coherence is currently 0.84" |
| `Gap` | Detected missing knowledge | 0.10 | "No knowledge about GraphQL" |
| `StoryThread` | Narrative arcs | 0.05 | "The authentication saga" |
| `Ledger` | Session snapshots | 0.15 | "Session state at 2024-01-09" |
| `Entity` | Named things | 0.02 | "Antonio: the user" |
| `Operation` | Action records | 0.15 | "Edited file X" |

### Confidence Model

Confidence isn't a single number — it's a **Bayesian distribution**:

```cpp
struct Confidence {
    float mu;       // Mean estimate (0.0 - 1.0)
    float sigma_sq; // Variance (uncertainty)
    uint32_t n;     // Observation count
    Timestamp tau;  // Last update time

    // Conservative estimate: penalize uncertainty
    float effective() const {
        return mu - std::sqrt(sigma_sq);
    }

    // Bayesian update with new evidence
    void update(bool positive, float weight = 1.0f) {
        n++;
        float alpha = 1.0f / n;  // Learning rate decreases with observations

        if (positive) {
            mu = mu + alpha * weight * (1.0f - mu);
        } else {
            mu = mu - alpha * weight * mu;
        }

        // Variance decreases with more observations
        sigma_sq = sigma_sq * (1.0f - alpha);
        tau = now();
    }
};
```

**Why Bayesian?**
- A node with `mu=0.8, n=2` is less confident than `mu=0.8, n=100`
- The `effective()` score accounts for this uncertainty
- Early observations have more impact; later ones refine

### Edge Types (15 total)

| Type | Meaning | Example |
|------|---------|---------|
| `Similar` | Semantic similarity | Caching wisdom → Performance wisdom |
| `AppliedIn` | Used in context | Auth pattern → Project X |
| `Contradicts` | Conflicting information | "Use JWT" ↔ "Use sessions" |
| `Supports` | Reinforcing evidence | Multiple failures → Belief |
| `EvolvedFrom` | Knowledge evolution | Old insight → Refined wisdom |
| `PartOf` | Compositional | Step → Procedure |
| `TriggeredBy` | Causal | Error → Investigation → Fix |
| `CreatedBy` | Attribution | Wisdom → Session that created it |
| `ScopedTo` | Domain limitation | Pattern → Specific project |
| `Answers` | Resolution | Question → Answer |
| `Addresses` | Problem-solution | Gap → Wisdom |
| `Continues` | Narrative flow | Episode → Next episode |
| `Mentions` | Reference | Episode → Entity |
| `IsA` | Classification | Specific → General |
| `RelatesTo` | Generic connection | Any semantic relationship |

### Coherence Metric (Sāmarasya)

Coherence measures how well the soul "hangs together":

```cpp
struct Coherence {
    float local;      // 0.0-1.0: Neighborhood consistency
    float global;     // 0.0-1.0: Overall alignment
    float temporal;   // 0.0-1.0: Decay health
    float structural; // 0.0-1.0: Graph integrity

    // Geometric mean (all must be healthy)
    float tau_k() const {
        return std::pow(local * global * temporal * structural, 0.25f);
    }
};
```

**Components:**
- **Local**: Do similar nodes have consistent confidence?
- **Global**: Are beliefs aligned with observed patterns?
- **Temporal**: Is decay being applied? Are old nodes still valid?
- **Structural**: Are edges meaningful? Is the graph connected?

---

## The Resonance Engine

Memory retrieval in CC-Soul isn't simple search — it's **resonance**. The engine implements six phases that work together.

### Phase 1: Spreading Activation

When a query activates a node, activation spreads through edges:

```cpp
std::vector<std::pair<NodeId, float>> spread_activation(
    const NodeId& seed,
    float initial_strength = 1.0f,
    float decay_factor = 0.5f,
    int max_hops = 3)
{
    std::unordered_map<NodeId, float> activation;
    std::queue<std::tuple<NodeId, float, int>> frontier;

    frontier.push({seed, initial_strength, 0});

    while (!frontier.empty()) {
        auto [current_id, strength, hop] = frontier.front();
        frontier.pop();

        if (hop >= max_hops || strength < 0.01f) continue;

        activation[current_id] += strength;

        Node* node = storage_.get(current_id);
        for (const auto& edge : node->edges) {
            float propagated = strength * decay_factor * edge.weight;
            if (propagated >= 0.05f) {
                frontier.push({edge.target, propagated, hop + 1});
            }
        }
    }

    return sorted_by_activation(activation);
}
```

**Effect**: Related concepts "light up" even if not directly matching the query.

### Phase 2: Attractor Dynamics

High-confidence, well-connected nodes act as **conceptual gravity wells**:

```cpp
struct Attractor {
    NodeId id;
    float strength;      // confidence × connectivity × age
    std::string label;
    size_t basin_size;   // How many nodes pulled toward this attractor
};

std::vector<Attractor> find_attractors(size_t max = 10) {
    std::vector<Attractor> candidates;

    for_each_node([&](const Node& node) {
        if (node.kappa.effective() < 0.6f) return;
        if (node.edges.size() < 2) return;

        float strength =
            0.4f * node.kappa.effective() +        // Confidence
            0.3f * log_connectivity(node.edges) +   // Connectivity
            0.3f * age_score(node.tau_created);     // Stability

        candidates.push_back({node.id, strength, excerpt(node), 0});
    });

    return top_k(candidates, max);
}
```

**Effect**: Results cluster around conceptual centers. Related queries find consistent answer spaces.

### Phase 3: Hebbian Learning

"Neurons that fire together wire together":

```cpp
void hebbian_update(const std::vector<NodeId>& co_activated, float strength) {
    // For all pairs of co-activated nodes
    for (size_t i = 0; i < co_activated.size(); ++i) {
        for (size_t j = i + 1; j < co_activated.size(); ++j) {
            // Strengthen edge in both directions
            strengthen_edge(co_activated[i], co_activated[j], strength);
            strengthen_edge(co_activated[j], co_activated[i], strength);
        }
    }
}

void strengthen_edge(NodeId from, NodeId to, float strength) {
    Node* node = storage_.get(from);

    // Find existing edge
    for (auto& edge : node->edges) {
        if (edge.target == to && edge.type == EdgeType::Similar) {
            edge.weight = std::min(edge.weight + strength, 1.0f);
            return;
        }
    }

    // Create new edge
    storage_.add_edge(from, to, EdgeType::Similar, strength);
}
```

**Effect**: Frequently co-retrieved memories become more strongly connected over time.

### Phase 4: Session Priming (Context Modulation)

Recent observations and active intentions bias retrieval:

```cpp
struct SessionContext {
    std::vector<NodeId> recent_observations;  // Last N observations this session
    std::vector<NodeId> active_intentions;    // Current goals
    std::vector<NodeId> goal_basin;           // Nodes near active intentions

    static constexpr size_t MAX_RECENT = 20;
    static constexpr size_t MAX_INTENTIONS = 10;
    static constexpr size_t GOAL_BASIN_SIZE = 20;
};

float session_relevance(float base_score, const Node& node,
                        const SessionContext& ctx) {
    float boost = 0.0f;

    // Recent observation boost (+30%)
    if (contains(ctx.recent_observations, node.id)) {
        boost += 0.30f;
    }

    // Intention alignment boost (+25%)
    if (contains(ctx.active_intentions, node.id)) {
        boost += 0.25f;
    }

    // Goal basin boost (+15%)
    if (contains(ctx.goal_basin, node.id)) {
        boost += 0.15f;
    }

    return base_score * (1.0f + boost);
}
```

**Effect**: Current context influences what surfaces. Working on auth? Auth-related memories rank higher.

### Phase 5: Lateral Inhibition (Competition)

Similar patterns compete — winners suppress losers:

```cpp
struct CompetitionConfig {
    bool enabled = true;
    float similarity_threshold = 0.85f;  // When to compete
    float inhibition_strength = 0.70f;   // How much losers are suppressed
    bool hard_mode = false;              // Remove vs reduce score
};

void apply_lateral_inhibition(std::vector<Recall>& results) {
    std::vector<bool> suppressed(results.size(), false);

    for (size_t i = 0; i < results.size(); ++i) {
        if (suppressed[i]) continue;

        // Winner: suppress similar lower-ranked results
        for (size_t j = i + 1; j < results.size(); ++j) {
            if (suppressed[j]) continue;

            float sim = results[i].embedding.cosine(results[j].embedding);
            if (sim > config_.similarity_threshold) {
                if (config_.hard_mode) {
                    suppressed[j] = true;
                } else {
                    results[j].relevance *= (1.0f - config_.inhibition_strength);
                }
            }
        }
    }

    // Remove suppressed results in hard mode
    if (config_.hard_mode) {
        results.erase(
            std::remove_if(results.begin(), results.end(),
                [&](const Recall& r) { return suppressed[&r - &results[0]]; }),
            results.end());
    }
}
```

**Effect**: Diverse results instead of redundant variations.

### Phase 6: Full Resonance

All mechanisms unified in one function:

```cpp
std::vector<Recall> full_resonate(const std::string& query,
                                   size_t k = 10,
                                   float spread_strength = 0.5f,
                                   float hebbian_strength = 0.03f) {
    // Phase 4: Refresh session context
    refresh_session_intentions();
    build_goal_basin();

    // Get semantic seeds with priming
    auto seeds = recall_primed(query, 5);

    // Phase 2: Find attractors
    auto attractors = find_attractors(5);

    // Phase 1: Spread activation from seeds
    auto activation = spread_from_seeds(seeds, spread_strength);

    // Merge seeds with activated nodes
    auto results = merge_and_score(seeds, activation, attractors);

    // Phase 2: Boost results in same attractor basin
    apply_attractor_boost(results, attractors);

    // Phase 5: Competition
    apply_lateral_inhibition(results);

    // Limit results
    results.resize(std::min(results.size(), k));

    // Phase 3: Hebbian learning
    hebbian_update(top_ids(results, 5), hebbian_strength);

    // Phase 4: Record for future priming
    for (const auto& r : results) {
        observe_for_priming(r.id);
    }

    return results;
}
```

---

## Storage Architecture

### Tiered Storage

Nodes live in three tiers based on access patterns:

```
┌─────────────────────────────────────────────────────────────────┐
│                         HOT TIER                                 │
│                                                                  │
│  Location: RAM (std::unordered_map)                             │
│  Access: O(1)                                                    │
│  Capacity: ~10,000 nodes                                         │
│  Content: Recent, frequently accessed                            │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  UnifiedIndex: slot-based array with HNSW for search    │   │
│  └─────────────────────────────────────────────────────────┘   │
│                             │                                    │
│                             │ promotion/demotion                 │
│                             ▼                                    │
├─────────────────────────────────────────────────────────────────┤
│                        WARM TIER                                 │
│                                                                  │
│  Location: Memory-mapped file                                    │
│  Access: ~microseconds                                           │
│  Capacity: ~100,000 nodes                                        │
│  Content: Less frequent, still quick access                      │
│                             │                                    │
│                             │ promotion/demotion                 │
│                             ▼                                    │
├─────────────────────────────────────────────────────────────────┤
│                        COLD TIER                                 │
│                                                                  │
│  Location: SQLite database                                       │
│  Access: ~milliseconds                                           │
│  Capacity: Unlimited                                             │
│  Content: Archival, rarely accessed                              │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Unified Index

The hot tier uses a **unified slot-based index** for O(1) access:

```cpp
class UnifiedIndex {
    // Fixed-size slots for nodes
    std::vector<std::optional<Node>> slots_;

    // NodeId → slot index mapping
    std::unordered_map<NodeId, size_t, NodeIdHash> id_to_slot_;

    // Free slot list for O(1) allocation
    std::vector<size_t> free_slots_;

    // HNSW index for semantic search
    HNSWIndex hnsw_;
};
```

### Write-Ahead Log (WAL)

All writes go through the WAL for durability and multi-process coordination:

```cpp
// WAL entry header
struct WALHeader {
    uint32_t magic;       // 0x57414C45 "WALE"
    uint64_t sequence;    // Monotonically increasing
    uint64_t timestamp;   // When written
    uint8_t type;         // Insert, Update, Delete
    uint8_t format;       // V0-V4 encoding
    uint32_t length;      // Payload size
    uint32_t checksum;    // CRC32
};

// WAL formats for space efficiency
V0: Full node with float32 vectors (~500 bytes)
V1: Full node with int8 quantized vectors (~150 bytes)
V2: Touch delta - just update access time (26 bytes)
V3: Confidence delta - update confidence only (44 bytes)
V4: Edge delta - add single edge (45 bytes)
```

### Quantized Vectors

Embeddings are stored as int8 for 74% size reduction:

```cpp
class QuantizedVector {
    std::array<int8_t, 384> data_;
    float scale_;
    float offset_;

    static QuantizedVector from_float(const Vector& v) {
        // Find min/max for quantization range
        float min_val = *std::min_element(v.begin(), v.end());
        float max_val = *std::max_element(v.begin(), v.end());

        float scale = (max_val - min_val) / 255.0f;
        float offset = min_val;

        QuantizedVector qv;
        qv.scale_ = scale;
        qv.offset_ = offset;

        for (size_t i = 0; i < 384; ++i) {
            qv.data_[i] = static_cast<int8_t>(
                std::round((v[i] - offset) / scale) - 128
            );
        }

        return qv;
    }

    // Approximate cosine similarity (fast)
    float cosine_approx(const QuantizedVector& other) const {
        int32_t dot = 0;
        for (size_t i = 0; i < 384; ++i) {
            dot += static_cast<int32_t>(data_[i]) *
                   static_cast<int32_t>(other.data_[i]);
        }
        // Normalize by vector magnitudes
        return static_cast<float>(dot) / (384.0f * 127.0f * 127.0f);
    }
};
```

---

## Multi-Instance Support

Multiple Claude instances share the same soul through WAL synchronization.

### Architecture

```
Process A (Claude 1)          Process B (Claude 2)
       │                              │
       │ observe("wisdom A")          │ observe("wisdom B")
       ▼                              ▼
┌──────────────────────────────────────────────────────────┐
│                         WAL FILE                          │
│                                                          │
│  Sequence 100: [Process A] Insert node (wisdom A)        │
│  Sequence 101: [Process B] Insert node (wisdom B)        │
│  Sequence 102: [Process A] Touch node (access)           │
│  Sequence 103: [Process B] Edge delta (hebbian)          │
│                                                          │
└──────────────────────────────────────────────────────────┘
                            │
                            │ sync_from_shared_field()
                            ▼
┌──────────────────────────────────────────────────────────┐
│                    UNIFIED INDEX                          │
│                                                          │
│  Both Process A and B see all nodes after sync           │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

### Sync Protocol

```cpp
// Called before recall to see other processes' writes
size_t sync_from_shared_field() {
    size_t last_seen = wal_.last_synced_sequence();
    size_t applied = 0;

    wal_.replay_from(last_seen, [&](const WALEntry& entry) {
        switch (entry.format) {
            case WAL_FORMAT_V1:
                // Full node insert/update
                apply_node(deserialize_node(entry.data));
                break;
            case WAL_FORMAT_V2:
                // Touch delta
                touch_node(entry.node_id);
                break;
            case WAL_FORMAT_V3:
                // Confidence delta
                update_confidence(entry.node_id, entry.confidence);
                break;
            case WAL_FORMAT_V4:
                // Edge delta
                add_edge(entry.from, entry.to, entry.edge_type, entry.weight);
                break;
        }
        applied++;
    });

    return applied;
}
```

### Conflict Resolution

- **Last-write-wins**: WAL sequence determines order
- **Confidence merges**: Multiple confidence updates combine (Bayesian)
- **Edge accumulation**: Edges from different processes add up

---

## Subconscious Processing

The daemon runs background processing without consuming main context tokens.

### Daemon Architecture

```cpp
int cmd_daemon(Mind& mind, int interval_seconds) {
    while (running) {
        sleep(interval_seconds);

        // 1. Apply decay
        mind.tick();

        // 2. Synthesize wisdom from episode clusters
        size_t synthesized = mind.synthesize_wisdom();

        // 3. Apply pending feedback (Hebbian learning)
        size_t feedback = mind.apply_feedback();

        // 4. Run attractor dynamics
        auto report = mind.run_attractor_dynamics(5, 0.01f);

        // 5. Save state
        mind.snapshot();
    }
}
```

### Synthesis Algorithm

Episodes cluster → Wisdom emerges:

```cpp
size_t synthesize_wisdom() {
    // Get all episode nodes
    std::vector<Node> episodes = get_nodes_by_type(NodeType::Episode);

    // Find clusters of similar episodes (>0.75 similarity)
    for (const auto& ep : episodes) {
        auto similar = find_similar(ep, 0.75f, NodeType::Episode);

        if (similar.size() >= 3) {
            // 3+ similar episodes = pattern worth promoting
            std::string wisdom = "Pattern observed (" +
                std::to_string(similar.size()) + " occurrences): " +
                excerpt(ep);

            // Boost confidence based on evidence count
            float confidence = average_confidence(similar) + 0.2f;

            remember(NodeType::Wisdom, embed(wisdom),
                     Confidence(std::min(confidence, 0.95f)),
                     wisdom);

            mark_promoted(similar);
        }
    }
}
```

---

## Integration Layer

### MCP Server

The soul exposes tools through the Model Context Protocol:

```cpp
class MCPServer {
    std::shared_ptr<Mind> mind_;
    std::map<std::string, ToolHandler> handlers_;

    void register_tools() {
        handlers_["soul_context"] = &MCPServer::tool_soul_context;
        handlers_["grow"] = &MCPServer::tool_grow;
        handlers_["observe"] = &MCPServer::tool_observe;
        handlers_["recall"] = &MCPServer::tool_recall;
        handlers_["full_resonate"] = &MCPServer::tool_full_resonate;
        // ... 20+ tools
    }

    void handle_request(const json& request) {
        std::string method = request["method"];

        if (method == "tools/call") {
            std::string tool = request["params"]["name"];
            json args = request["params"]["arguments"];

            auto handler = handlers_[tool];
            auto result = handler(args);

            send_response(result);
        }
    }
};
```

### Hook System

Lifecycle events trigger soul operations:

```
SessionStart
    │
    ├──▶ smart-install.sh (ensure binaries exist)
    │
    ├──▶ soul-hook.sh start
    │       ├── Load ledger (previous session state)
    │       └── Inject soul_context
    │
    └──▶ subconscious.sh start (daemon)

UserPromptSubmit
    │
    └──▶ soul-hook.sh prompt --lean --resonate
            ├── Extract user message from stdin
            ├── Run full_resonate(message)
            └── Inject relevant memories as context

SessionEnd
    │
    └──▶ soul-hook.sh end
            ├── Save ledger (current state)
            └── Run maintenance cycle
```

---

## Performance Characteristics

### Time Complexity

| Operation | Hot Tier | Warm Tier | Cold Tier |
|-----------|----------|-----------|-----------|
| Get by ID | O(1) | O(1) | O(log n) |
| Insert | O(1) + O(log n) HNSW | - | - |
| Semantic search | O(log n) | - | - |
| Tag filter | O(k) | - | - |
| Full resonate | O(k × log n) | - | - |

### Space Complexity

| Component | Size |
|-----------|------|
| Node (quantized) | ~150 bytes |
| Node (full float) | ~500 bytes |
| Edge | ~20 bytes |
| WAL entry (touch) | 26 bytes |
| WAL entry (full) | ~150 bytes |
| HNSW index | ~4KB per 100 nodes |

### Benchmarks (1963 nodes)

| Operation | Time |
|-----------|------|
| Startup (load index) | <100ms |
| recall(k=10) | ~5ms |
| full_resonate(k=10) | ~15ms |
| observe (insert) | ~2ms |
| daemon cycle | ~50ms |

---

## Phase 7: Quality Infrastructure

Phase 7 adds production-quality features for scale, isolation, and human oversight.

### Realm Scoping

Realms provide cross-session context isolation:

```cpp
class RealmManager {
    std::string current_realm_ = "brahman";  // Default: universal
    std::unordered_map<std::string, std::string> realm_parents_;

    // During recall, filter by realm
    bool node_in_realm(const Node& node) const {
        for (const auto& tag : node.tags) {
            if (tag.starts_with("realm:")) {
                std::string node_realm = tag.substr(6);
                return is_ancestor_or_equal(current_realm_, node_realm);
            }
        }
        return true;  // No realm tag = visible everywhere
    }
};
```

**Use Cases:**
- Project isolation: `realm_set("project:cc-soul")`
- Team separation: `realm_set("team:backend")`
- Hierarchical: `project:cc-soul` inherits from `project:shared`

### Human Oversight (ReviewQueue)

AI-generated wisdom requires human approval:

```cpp
class ReviewQueue {
    struct ReviewItem {
        NodeId id;
        ReviewStatus status;  // Pending, Approved, Rejected
        Timestamp submitted;
        std::optional<std::string> reason;
    };

    std::vector<ReviewItem> queue_;

    void decide(NodeId id, ReviewDecision decision, std::string reason) {
        auto& node = mind_->get_mutable(id);

        switch (decision) {
            case Approve:
                node.kappa.update(true, 0.3f);  // Confidence boost
                node.tags.push_back("human:approved");
                break;
            case Reject:
                node.kappa.update(false, 0.5f);  // Strong penalty
                node.tags.push_back("human:rejected");
                break;
            case Edit:
                // Update content, then approve
                break;
            case Defer:
                // Keep in queue
                break;
        }
    }
};
```

**Workflow:**
1. Daemon synthesizes wisdom → auto-queued for review
2. Human reviews via `review_list`, `review_decide`
3. Approved nodes gain confidence; rejected nodes decay faster

### Quality Evaluation (EvalHarness)

Golden test suite validates recall quality:

```cpp
class EvalHarness {
    struct GoldenTestCase {
        std::string name;
        std::string query;
        std::vector<ExpectedResult> expected;  // ID + min_score + max_rank
    };

    std::vector<GoldenTestCase> tests_;

    EvalReport run_all(RecallFn recall_fn) {
        EvalReport report;
        for (const auto& test : tests_) {
            auto results = recall_fn(test.query, 10);
            report.add(test.name, evaluate(results, test.expected));
        }
        return report;
    }
};
```

**Metrics:**
- Recall@k: Expected nodes in top-k results
- Precision: Relevant nodes / total returned
- MRR: Mean reciprocal rank of first expected node

### Epiplexity Measurement

Measures compression quality (ε = reconstructability):

```cpp
class EpiplexityTest {
    // Can I reconstruct full content from just the seed?
    float measure(const std::string& full_content,
                  const std::string& seed,
                  EmbedFn embed_fn) {
        Vector full_vec = embed_fn(full_content);
        Vector seed_vec = embed_fn(seed);

        // High cosine similarity = high epiplexity
        return full_vec.cosine(seed_vec);
    }

    // Track drift over time
    void analyze_drift(std::vector<Measurement>& history) {
        // Alert if recent ε < historical average
    }
};
```

**Goal:** Maximize ε (>0.85 = excellent compression).

### 100M+ Scale Infrastructure

Built to handle production workloads:

| Component | Purpose | Scale |
|-----------|---------|-------|
| UnifiedIndex | Slot-based O(1) access | 100M+ nodes |
| QueryRouter | Smart index selection | Sub-ms routing |
| SynthesisQueue | Batched wisdom generation | Background processing |
| GapInquiry | Knowledge gap detection | Continuous analysis |
| AttractorDampener | Prevent runaway attractors | Stability |

---

## Future Directions

1. **Distributed Soul**: Multiple machines sharing WAL via network
2. **Hierarchical Memory**: Project-level → User-level → Universal
3. **Active Inference**: Soul predicts what you'll need next
4. **Dream Synthesis**: Offline processing generates novel insights
5. **Federated Learning**: Souls share patterns without raw data

---

*The architecture breathes. It grows. It remembers.*
