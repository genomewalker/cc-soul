# Phase 2 Core: Research-Grade Graph Algorithms

## Research Summary

Based on state-of-the-art literature (2024-2025), we implement three core capabilities using algorithms proven in knowledge graph research.

---

## 1. Multi-Hop Reasoning: Personalized PageRank + Path Extraction

### Algorithm: PPR with Random Walk with Restart (RWR)

**Why PPR over naive BFS:**
- O(1) query time after O(n) precomputation
- Naturally handles disconnected graphs
- Incorporates edge weights and graph structure
- Used by HippoRAG, GNN-RAG, and modern KG systems

**Mathematical formulation:**
```
π = α · e_s + (1-α) · P^T · π
```
Where:
- π = PPR vector (stationary distribution)
- α = restart probability (typically 0.15)
- e_s = seed vector (one-hot or weighted)
- P = transition matrix (row-normalized adjacency)

**Our enhancements:**

1. **Edge-type weighting**: Different edge types have different propagation strengths
   ```cpp
   float edge_weight(EdgeType type, const std::string& query) {
       // Semantic edges propagate more for semantic queries
       // Causal edges propagate more for "why" queries
       return base_weight * type_affinity(type, query);
   }
   ```

2. **Bidirectional propagation**: Compute PPR from both source and target, find intersection for path relevance

3. **Path extraction**: Backtrack through contribution matrix to find actual paths
   ```cpp
   // Track which node contributed most to each PPR score
   std::unordered_map<NodeId, NodeId> best_parent;
   ```

4. **Convergence acceleration**: Use power iteration with early stopping at ε = 1e-6

### API Design
```cpp
// Find nodes reachable via multi-hop reasoning
std::vector<Recall> ppr_query(
    const std::string& query,
    size_t k = 10,
    float alpha = 0.15,           // Restart probability
    size_t max_iterations = 50
);

// Find paths between concepts
std::vector<Path> find_reasoning_paths(
    const std::string& from_query,
    const std::string& to_query,
    size_t max_paths = 5
);
```

---

## 2. Memory Consolidation: Leiden Algorithm

### Algorithm: Modularity Optimization with Leiden

**Why Leiden over Louvain:**
- Guarantees well-connected communities (Louvain can produce disconnected)
- Faster convergence via refinement phase
- Better resolution handling

**Three-phase approach:**

1. **Local moving**: Greedily move nodes to maximize modularity
   ```cpp
   float delta_modularity(NodeId node, CommunityId new_comm) {
       // ΔQ = [Σ_in + k_i,in] / m - [(Σ_tot + k_i) / 2m]²
       //    - [Σ_in / m - (Σ_tot / 2m)² - (k_i / 2m)²]
   }
   ```

2. **Refinement**: Split poorly connected communities, re-optimize
   ```cpp
   // For each community, check internal connectivity
   // If min-cut < threshold, split and re-assign
   ```

3. **Aggregation**: Create super-nodes from communities
   ```cpp
   // Merge all nodes in community into single super-node
   // Sum edge weights between communities
   ```

**Our enhancements:**

1. **Type-aware communities**: Same-type nodes prefer same community
   ```cpp
   float type_affinity(NodeType a, NodeType b) {
       if (a == b) return 1.2f;  // 20% bonus for same type
       return 1.0f;
   }
   ```

2. **Semantic coherence constraint**: Merged nodes must have embedding similarity > 0.7

3. **Confidence preservation**: Bayesian combination of confidences
   ```cpp
   float merged_mu = (μ_a * n_a + μ_b * n_b) / (n_a + n_b);
   float merged_sigma = min(σ_a, σ_b) * 0.9;  // Tighter after merge
   ```

4. **Edge union with conflict resolution**:
   - Same target, same type: max weight
   - Same target, different type: keep both
   - Redirect all edges to merged node

### API Design
```cpp
// Find communities in the graph
std::vector<Community> find_communities(
    float resolution = 1.0,       // Higher = more communities
    size_t max_iterations = 100
);

// Consolidate similar nodes within communities
size_t consolidate_community(
    const Community& community,
    float min_similarity = 0.85
);

// Auto-consolidation based on Leiden communities
ConsolidationReport auto_consolidate(
    float min_similarity = 0.90,
    size_t max_merges = 20
);
```

---

## 3. Temporal Sequences: Hawkes Process Decay

### Algorithm: Self-Exciting Point Process with Time Decay

**Why Hawkes over simple exponential decay:**
- Models recency AND frequency (self-exciting)
- Recent bursts of activity amplify importance
- Captures causal influence between events
- Used in DynTKG and temporal KG research

**Mathematical formulation:**
```
λ(t) = μ + Σ_{t_i < t} α · exp(-β · (t - t_i))
```
Where:
- λ(t) = intensity at time t
- μ = background rate
- α = excitation amplitude (how much each event excites)
- β = decay rate
- t_i = times of previous events

**For memory retrieval:**
```cpp
float temporal_relevance(Timestamp t_event, Timestamp t_query) {
    float delta = (t_query - t_event) / 86400000.0f;  // days
    float base_decay = exp(-beta * delta);

    // Add excitation from related events
    float excitation = 0.0f;
    for (auto& related : get_related_events(t_event)) {
        float t_rel = (t_query - related.time) / 86400000.0f;
        excitation += alpha * exp(-beta * t_rel);
    }

    return base_decay + excitation;
}
```

**Our enhancements:**

1. **Type-dependent decay rates**:
   ```cpp
   float beta_for_type(NodeType type) {
       switch (type) {
           case Belief:    return 0.01f;   // Slow decay (100 days half-life)
           case Wisdom:    return 0.02f;   // Moderate (35 days)
           case Episode:   return 0.10f;   // Fast (7 days)
           case Signal:    return 0.20f;   // Very fast (3.5 days)
       }
   }
   ```

2. **Edge-mediated excitation**: Events connected by edges excite each other
   ```cpp
   for (auto& edge : node.edges) {
       if (edge.type == EdgeType::TriggeredBy ||
           edge.type == EdgeType::Continues) {
           excitation += edge.weight * alpha * decay(t);
       }
   }
   ```

3. **Session-aware bursting**: Events in same session have reduced decay between them

4. **Causal chain detection**: Identify sequences where A → B → C with temporal ordering + edges
   ```cpp
   struct CausalChain {
       std::vector<NodeId> nodes;
       std::vector<EdgeType> edges;
       float confidence;  // Product of edge weights * temporal coherence
   };
   ```

### API Design
```cpp
// Get temporally-weighted results
std::vector<Recall> temporal_query(
    const std::string& query,
    size_t k = 10,
    float alpha = 0.3,    // Excitation amplitude
    float beta = 0.05     // Base decay rate
);

// Find causal chains
std::vector<CausalChain> find_causal_chains(
    const NodeId& effect,     // What are we explaining?
    size_t max_depth = 5,
    float min_confidence = 0.3
);

// Timeline with Hawkes-weighted importance
std::vector<Recall> temporal_timeline(
    Timestamp from,
    Timestamp to,
    size_t limit = 20
);
```

---

## Implementation Strategy

### Phase 1: Core Algorithms (mind.hpp)

1. **PPR Engine**
   - Sparse matrix representation for transition matrix
   - Power iteration with convergence tracking
   - Path extraction via parent tracking

2. **Leiden Engine**
   - Community data structure with O(1) membership lookup
   - Modularity delta calculation
   - Refinement with min-cut detection

3. **Hawkes Engine**
   - Time kernel functions
   - Excitation computation
   - Causal chain traversal

### Phase 2: Integration Tools (rpc/tools/)

1. `multi_hop` - PPR-based reasoning
2. `consolidate` - Leiden-based merging
3. `timeline` - Hawkes-weighted temporal view
4. `causal_chain` - Find what led to what

### Phase 3: Daemon Integration

- Background consolidation during idle
- Periodic community structure update
- Hawkes parameters learned from feedback

---

## Complexity Analysis

| Operation | Time | Space |
|-----------|------|-------|
| PPR query | O(k·E/V) | O(V) |
| Path extraction | O(k·d) | O(k·d) |
| Leiden iteration | O(E) | O(V) |
| Consolidation | O(C·E_c) | O(V) |
| Hawkes temporal | O(n·log n) | O(n) |
| Causal chain | O(d^k) | O(d·k) |

Where:
- V = nodes, E = edges
- k = result size, d = average degree
- C = communities, E_c = intra-community edges
- n = nodes in time range

---

## References

1. [GNN-RAG: Graph Neural Retrieval for LLM Reasoning](https://aclanthology.org/2025.findings-acl.856.pdf)
2. [Leiden Algorithm](https://github.com/vtraag/leidenalg)
3. [Hawkes Process for Temporal KG](https://ieeexplore.ieee.org/document/10649972)
4. [Personalized PageRank Survey](https://arxiv.org/html/2403.05198v1)
5. [Dynamic Graph Hawkes Process](https://pmc.ncbi.nlm.nih.gov/articles/PMC10280484/)
