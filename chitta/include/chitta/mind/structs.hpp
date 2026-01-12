#pragma once
// Mind structs: public result types and data structures
//
// Extracted from mind.hpp for modularity.
// These are return types and parameter types used by Mind API methods.

#include "../types.hpp"
#include <vector>
#include <utility>

namespace chitta {

// Result of confidence propagation through graph
struct PropagationResult {
    size_t nodes_affected;
    float total_delta_applied;
    std::vector<std::pair<NodeId, float>> changes;  // (id, delta)
};

// Report from recovery operations
struct RecoveryReport {
    bool decay_applied = false;
    bool integrity_repaired = false;
    bool index_rebuilt = false;
    size_t nodes_pruned = 0;
    float ojas_before = 0.0f;
    float ojas_after = 0.0f;
};

// An attractor is a high-confidence, well-connected node that
// pulls similar nodes toward it (conceptual gravity well)
struct Attractor {
    NodeId id;
    float strength;          // Attractor strength (confidence * connectivity)
    std::string label;       // First 50 chars of content for identification
    size_t basin_size = 0;   // Number of nodes in this attractor's basin
};

// Report from attractor dynamics
struct AttractorReport {
    size_t attractor_count = 0;
    size_t nodes_settled = 0;
    std::vector<std::pair<std::string, size_t>> basin_sizes;  // label -> size
};

// Statistics from epiplexity computation
struct EpiplexityStats {
    float mean = 0.0f;
    float median = 0.0f;
    float min = 1.0f;
    float max = 0.0f;
    size_t count = 0;
    std::vector<std::pair<NodeId, float>> top_nodes;  // Top 10 by epiplexity
};

// Reverse edge for incoming edge lookup
struct ReverseEdge {
    NodeId source;
    EdgeType type;
    float weight;
};

// Sparse vector for PPR computation
struct SparseVector {
    std::unordered_map<NodeId, float, NodeIdHash> entries;

    void add(const NodeId& id, float val) {
        entries[id] += val;
        if (std::abs(entries[id]) < 1e-10f) entries.erase(id);
    }

    float get(const NodeId& id) const {
        auto it = entries.find(id);
        return it != entries.end() ? it->second : 0.0f;
    }
};

// Causal chain for reasoning paths
struct CausalChain {
    std::vector<NodeId> nodes;
    std::vector<EdgeType> edges;
    float confidence;
};

} // namespace chitta
