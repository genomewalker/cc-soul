# Phase 2 Core: Scalable Graph Algorithms

**Target**: 100M+ nodes, years of continuous operation, O(log n) or O(1) query time

---

## Design Principles

1. **No O(n²) operations** - ever, not even in preprocessing
2. **Incremental index updates** - O(affected) not O(total)
3. **Approximate with guarantees** - FORA, Monte Carlo, LSH
4. **Streaming/online** - handle updates as they arrive
5. **Memory-efficient** - sparse structures, probabilistic data structures

---

## 1. Multi-Hop Reasoning: FORA-Style Approximate PPR

### Algorithm: Forward Push + Monte Carlo Random Walk (FORA)

**Why FORA scales:**
- Query time: O(1/ε) not O(V)
- Answers top-500 PPR on billion-edge Twitter in <1s
- No full graph traversal required

### Data Structures

```cpp
// Reverse edge index - O(1) lookup of incoming edges
// Updated incrementally on edge add/remove
std::unordered_map<NodeId, std::vector<Edge>, NodeIdHash> reverse_edges_;

// Residual vectors for forward push (sparse)
// Only store non-zero entries
struct SparseVector {
    std::unordered_map<NodeId, float, NodeIdHash> entries;
    void add(NodeId id, float val) {
        entries[id] += val;
        if (std::abs(entries[id]) < 1e-10f) entries.erase(id);
    }
    float get(NodeId id) const {
        auto it = entries.find(id);
        return it != entries.end() ? it->second : 0.0f;
    }
};
```

### FORA Algorithm

```cpp
// Phase 1: Forward Push (deterministic, fast)
// Push residuals until all are below threshold
void forward_push(
    const NodeId& source,
    SparseVector& pi,      // PPR estimates
    SparseVector& residual,// Residuals to push
    float r_max,           // Push threshold
    float alpha)           // Restart probability
{
    std::queue<NodeId> active;
    residual.add(source, 1.0f);
    active.push(source);

    while (!active.empty()) {
        NodeId u = active.front();
        active.pop();

        float r_u = residual.get(u);
        if (std::abs(r_u) < r_max) continue;

        // Push to PPR estimate
        pi.add(u, alpha * r_u);

        // Push to neighbors via reverse edges
        float push_val = (1 - alpha) * r_u;
        residual.entries[u] = 0.0f;

        // Use reverse edges for incoming neighbors
        if (reverse_edges_.count(u)) {
            float out_deg = reverse_edges_[u].size();
            for (const auto& edge : reverse_edges_[u]) {
                float delta = push_val * edge.weight / out_deg;
                if (std::abs(delta) > r_max * 0.1f) {
                    residual.add(edge.target, delta);
                    active.push(edge.target);
                }
            }
        }
    }
}

// Phase 2: Monte Carlo Random Walks (probabilistic refinement)
void monte_carlo_refinement(
    SparseVector& pi,
    const SparseVector& residual,
    float alpha,
    size_t num_walks)    // O(1/ε²) walks for ε approximation
{
    // Sample walks from nodes with residual
    std::random_device rd;
    std::mt19937 gen(rd());

    for (const auto& [start, r] : residual.entries) {
        // Number of walks proportional to residual
        size_t walks = static_cast<size_t>(std::abs(r) * num_walks);

        for (size_t w = 0; w < walks; ++w) {
            NodeId current = start;
            float sign = r > 0 ? 1.0f : -1.0f;

            // Random walk with restart
            while (true) {
                std::uniform_real_distribution<float> dist(0.0f, 1.0f);
                if (dist(gen) < alpha) {
                    // Restart - add to PPR
                    pi.add(current, sign / num_walks);
                    break;
                }

                // Move to random neighbor
                Node* node = storage_.get(current);
                if (!node || node->edges.empty()) break;

                std::uniform_int_distribution<size_t> edge_dist(0, node->edges.size() - 1);
                current = node->edges[edge_dist(gen)].target;
            }
        }
    }
}
```

### Query Interface

```cpp
// O(1/ε) query time, not O(V)
std::vector<Recall> approximate_ppr_query(
    const std::string& query,
    size_t k = 10,
    float epsilon = 0.01f)  // Approximation error
{
    // Get semantic seeds (O(log n) with ANN index)
    auto seeds = approximate_knn(query, 5);

    SparseVector pi, residual;
    float r_max = epsilon / (2.0f * k);  // FORA threshold

    for (const auto& seed : seeds) {
        forward_push(seed.id, pi, residual, r_max, 0.15f);
    }

    // Monte Carlo refinement only if needed
    if (residual.entries.size() > k) {
        monte_carlo_refinement(pi, residual, 0.15f, 1000);
    }

    // Return top-k from sparse PPR vector
    return top_k_from_sparse(pi, k);
}
```

### Complexity

| Operation | Time | Space |
|-----------|------|-------|
| Query | O(1/ε) | O(1/ε) |
| Index update | O(affected edges) | O(V + E) |

---

## 2. Memory Consolidation: Incremental LSH Clustering

### Why Not Leiden for 100M Nodes?

- Even parallel Leiden is O(E) per iteration
- We need **incremental** clustering that updates on each insert

### Algorithm: Locality-Sensitive Hashing (LSH) Forest

**Concept**: Hash similar embeddings to same buckets, cluster incrementally.

```cpp
// LSH Forest for approximate nearest neighbor clustering
struct LSHForest {
    static constexpr size_t NUM_TREES = 10;
    static constexpr size_t HASH_SIZE = 16;  // bits

    // Random hyperplanes for each tree
    std::vector<std::vector<std::vector<float>>> hyperplanes;  // [tree][plane][dim]

    // Hash buckets: tree -> hash -> nodes
    std::vector<std::unordered_map<uint32_t, std::vector<NodeId>>> buckets;

    uint32_t compute_hash(const Embedding& emb, size_t tree) {
        uint32_t hash = 0;
        for (size_t i = 0; i < HASH_SIZE; ++i) {
            float dot = 0.0f;
            for (size_t d = 0; d < emb.size(); ++d) {
                dot += emb[d] * hyperplanes[tree][i][d];
            }
            if (dot > 0) hash |= (1u << i);
        }
        return hash;
    }

    void insert(const NodeId& id, const Embedding& emb) {
        for (size_t t = 0; t < NUM_TREES; ++t) {
            uint32_t hash = compute_hash(emb, t);
            buckets[t][hash].push_back(id);
        }
    }

    // Find candidates for consolidation: O(1) average
    std::vector<NodeId> find_similar(const Embedding& emb, size_t max_candidates = 50) {
        std::unordered_set<NodeId, NodeIdHash> candidates;

        for (size_t t = 0; t < NUM_TREES; ++t) {
            uint32_t hash = compute_hash(emb, t);
            if (buckets[t].count(hash)) {
                for (const auto& id : buckets[t][hash]) {
                    candidates.insert(id);
                    if (candidates.size() >= max_candidates) break;
                }
            }
        }

        return std::vector<NodeId>(candidates.begin(), candidates.end());
    }
};
```

### Incremental Consolidation

```cpp
// Called on every node insert - O(1) average
std::optional<NodeId> try_consolidate_on_insert(
    const NodeId& new_id,
    const Node& new_node,
    float min_similarity = 0.92f)
{
    // Find candidates via LSH - O(1)
    auto candidates = lsh_forest_.find_similar(new_node.nu, 20);

    // Check actual similarity only for candidates - O(candidates)
    for (const auto& cand_id : candidates) {
        Node* cand = storage_.get(cand_id);
        if (!cand) continue;
        if (cand->node_type != new_node.node_type) continue;

        float sim = cand->nu.cosine(new_node.nu);
        if (sim >= min_similarity) {
            // Merge into existing node
            return merge_nodes(cand_id, new_id);
        }
    }

    // No match - add to LSH index
    lsh_forest_.insert(new_id, new_node.nu);
    return std::nullopt;
}

// Background consolidation: process nodes in batches
size_t background_consolidate(size_t batch_size = 100) {
    // Use reservoir sampling to pick random nodes - O(1)
    auto sample = reservoir_sample(batch_size);

    size_t merged = 0;
    for (const auto& id : sample) {
        Node* node = storage_.get(id);
        if (!node) continue;

        auto candidates = lsh_forest_.find_similar(node->nu, 10);
        for (const auto& cand_id : candidates) {
            if (cand_id == id) continue;
            // Try merge...
        }
    }
    return merged;
}
```

### Complexity

| Operation | Time | Space |
|-----------|------|-------|
| Insert + check | O(1) average | O(V) |
| Find similar | O(1) average | - |
| Background batch | O(batch_size) | - |

---

## 3. Temporal Sequences: Time-Partitioned Index

### Data Structure: Time-Bucketed Skip List

```cpp
// Temporal index with O(log n) range queries
struct TemporalIndex {
    static constexpr size_t BUCKET_SIZE_MS = 3600000;  // 1 hour buckets

    // Bucket ID -> sorted vector of (timestamp, node_id)
    std::map<uint64_t, std::vector<std::pair<Timestamp, NodeId>>> buckets;

    // Total count for reservoir sampling
    size_t total_nodes = 0;

    uint64_t bucket_id(Timestamp ts) {
        return ts / BUCKET_SIZE_MS;
    }

    void insert(Timestamp ts, const NodeId& id) {
        uint64_t bid = bucket_id(ts);
        auto& bucket = buckets[bid];
        // Insert sorted
        auto it = std::lower_bound(bucket.begin(), bucket.end(),
            std::make_pair(ts, id));
        bucket.insert(it, {ts, id});
        total_nodes++;
    }

    // Range query: O(log B + k) where B = buckets, k = results
    std::vector<NodeId> range_query(
        Timestamp from,
        Timestamp to,
        size_t limit = 100)
    {
        std::vector<NodeId> results;

        uint64_t from_bid = bucket_id(from);
        uint64_t to_bid = bucket_id(to);

        // Use map's O(log n) lower_bound
        auto it = buckets.lower_bound(from_bid);

        while (it != buckets.end() && it->first <= to_bid) {
            for (const auto& [ts, id] : it->second) {
                if (ts >= from && ts <= to) {
                    results.push_back(id);
                    if (results.size() >= limit) return results;
                }
            }
            ++it;
        }

        return results;
    }
};
```

### Hawkes with Pre-computed Intensity

```cpp
// Maintain running Hawkes intensity per node
// Updated incrementally on access
struct HawkesState {
    float base_intensity;     // μ + base_decay
    float excitation;         // Sum of excitations
    Timestamp last_update;    // When intensity was computed

    // Lazy update on query
    float get_intensity(Timestamp now, float beta) {
        float delta_days = (now - last_update) / 86400000.0f;
        // Decay excitation
        excitation *= std::exp(-beta * delta_days);
        last_update = now;
        return base_intensity + excitation;
    }

    void add_excitation(float amount) {
        excitation += amount;
    }
};

// Map NodeId -> HawkesState (only for recently accessed nodes)
// Use LRU eviction to bound memory
LRUCache<NodeId, HawkesState> hawkes_cache_;
```

### Causal Chain with Reverse Edge Index

```cpp
// Find causal chains using reverse edge index - O(depth * avg_in_degree)
std::vector<CausalChain> find_causal_chains_scalable(
    const NodeId& effect,
    size_t max_depth = 5)
{
    // BFS backward through reverse edges
    std::queue<std::tuple<NodeId, CausalChain, size_t>> frontier;

    CausalChain initial;
    initial.nodes.push_back(effect);
    frontier.push({effect, initial, 0});

    std::vector<CausalChain> chains;

    while (!frontier.empty() && chains.size() < 10) {
        auto [current, chain, depth] = std::move(frontier.front());
        frontier.pop();

        if (depth >= max_depth) {
            chains.push_back(chain);
            continue;
        }

        // Use reverse edge index - O(in_degree) not O(V)
        if (reverse_edges_.count(current)) {
            for (const auto& edge : reverse_edges_[current]) {
                if (!is_causal_edge(edge.type)) continue;

                Node* cause = storage_.get(edge.target);
                if (!cause) continue;

                // Temporal constraint
                Node* effect_node = storage_.get(current);
                if (cause->tau_created >= effect_node->tau_created) continue;

                CausalChain new_chain = chain;
                new_chain.nodes.push_back(edge.target);
                new_chain.edges.push_back(edge.type);
                new_chain.confidence *= edge.weight;

                frontier.push({edge.target, new_chain, depth + 1});
            }
        }
    }

    return chains;
}
```

### Complexity

| Operation | Time | Space |
|-----------|------|-------|
| Time range query | O(log B + k) | O(V) for index |
| Hawkes intensity | O(1) with cache | O(cache_size) |
| Causal chain | O(depth × avg_in_degree) | O(depth) |

---

## Index Maintenance

### Incremental Updates

```cpp
// Called on every node insert
void on_node_insert(const NodeId& id, Node& node) {
    // 1. Add to reverse edge index - O(out_degree)
    for (const auto& edge : node.edges) {
        reverse_edges_[edge.target].push_back({id, edge.type, edge.weight});
    }

    // 2. Add to temporal index - O(log B)
    temporal_index_.insert(node.tau_created, id);

    // 3. Add to LSH forest - O(num_trees)
    lsh_forest_.insert(id, node.nu);

    // 4. Try consolidation - O(1) average
    auto merged = try_consolidate_on_insert(id, node);
}

// Called on edge add
void on_edge_add(const NodeId& from, const Edge& edge) {
    // O(1) - just append to reverse index
    reverse_edges_[edge.target].push_back({from, edge.type, edge.weight});
}

// Called on node touch (access)
void on_node_touch(const NodeId& id, Node& node) {
    // Update Hawkes excitation - O(1)
    if (hawkes_cache_.contains(id)) {
        hawkes_cache_.get(id).add_excitation(0.3f);
    }
}
```

### Background Maintenance

```cpp
// Run periodically in daemon cycle
void background_maintenance() {
    // 1. Prune LSH buckets with deleted nodes
    lsh_forest_.prune_deleted(storage_);

    // 2. Run batch consolidation on sample
    background_consolidate(100);

    // 3. Evict cold entries from Hawkes cache
    hawkes_cache_.evict_oldest(1000);

    // 4. Compact temporal index buckets
    temporal_index_.compact_old_buckets();
}
```

---

## Memory Budget (100M nodes)

| Structure | Size | Notes |
|-----------|------|-------|
| Reverse edges | ~8 bytes × E | E ≈ 10 × V typically |
| Temporal index | ~16 bytes × V | timestamp + id per node |
| LSH forest | ~4 bytes × V × trees | hash bucket membership |
| Hawkes cache | ~32 bytes × cache_size | LRU bounded |
| **Total overhead** | **~200 bytes/node** | 20GB for 100M nodes |

---

## References

1. [FORA: Simple and Effective Approximate Single-Source Personalized PageRank](https://dl.acm.org/doi/10.1145/3097983.3098072)
2. [Fast Incremental and Personalized PageRank](https://www.vldb.org/pvldb/vol4/p173-bahmani.pdf)
3. [LSH Forest: Self-Tuning Indexes for Similarity Search](https://dl.acm.org/doi/10.1145/985692.985705)
4. [GVE-Leiden: Fast Parallel Community Detection](https://arxiv.org/html/2312.13936v2)
