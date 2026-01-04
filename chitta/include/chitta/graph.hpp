#pragma once
// The Graph: where soul lives
//
// Nodes connected by edges. Vector-indexed for semantic search.
// The graph IS the soul - not a container, the thing itself.

#include "types.hpp"
#include "hnsw.hpp"  // For NodeIdHash
#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <functional>

namespace chitta {

// A point-in-time snapshot
struct Snapshot {
    uint64_t id;
    Timestamp timestamp;
    std::unordered_map<NodeId, Node, NodeIdHash> nodes;
};

// The soul graph
class Graph {
public:
    Graph() = default;

    // Insert a node into the graph
    NodeId insert(Node node) {
        NodeId id = node.id;
        Vector nu = node.nu;

        {
            std::unique_lock lock(mutex_);
            nodes_.emplace(id, std::move(node));
            vectors_.emplace_back(id, std::move(nu));
        }

        return id;
    }

    // Get a node by ID
    std::optional<Node> get(NodeId id) const {
        std::shared_lock lock(mutex_);
        auto it = nodes_.find(id);
        if (it != nodes_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    // Get mutable access to a node
    template<typename F>
    bool with_node(NodeId id, F&& func) {
        std::unique_lock lock(mutex_);
        auto it = nodes_.find(id);
        if (it != nodes_.end()) {
            func(it->second);
            return true;
        }
        return false;
    }

    // Semantic search: find nodes similar to vector
    std::vector<std::pair<NodeId, float>> query(
        const Vector& vector, float threshold, size_t limit) const
    {
        std::shared_lock lock(mutex_);
        std::vector<std::pair<NodeId, float>> results;

        for (const auto& [id, v] : vectors_) {
            float sim = vector.cosine(v);
            if (sim >= threshold) {
                results.emplace_back(id, sim);
            }
        }

        std::sort(results.begin(), results.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        if (results.size() > limit) {
            results.resize(limit);
        }

        return results;
    }

    // Query by node type
    std::vector<Node> query_by_type(NodeType node_type) const {
        std::shared_lock lock(mutex_);
        std::vector<Node> results;

        for (const auto& [_, node] : nodes_) {
            if (node.node_type == node_type) {
                results.push_back(node);
            }
        }

        return results;
    }

    // Connect two nodes
    bool connect(NodeId from, NodeId to, EdgeType edge_type, float weight) {
        return with_node(from, [&](Node& node) {
            node.connect(to, edge_type, weight);
        });
    }

    // Apply decay to all nodes
    void apply_decay() {
        Timestamp current = now();
        std::unique_lock lock(mutex_);
        for (auto& [_, node] : nodes_) {
            node.apply_decay(current);
        }
    }

    // Prune dead nodes (confidence below threshold)
    size_t prune(float threshold) {
        std::unique_lock lock(mutex_);
        size_t before = nodes_.size();

        // Don't prune invariants or beliefs
        for (auto it = nodes_.begin(); it != nodes_.end();) {
            const Node& n = it->second;
            if (n.node_type != NodeType::Invariant &&
                n.node_type != NodeType::Belief &&
                !n.is_alive(threshold)) {
                it = nodes_.erase(it);
            } else {
                ++it;
            }
        }

        size_t removed = before - nodes_.size();

        // Update vector index
        if (removed > 0) {
            vectors_.erase(
                std::remove_if(vectors_.begin(), vectors_.end(),
                    [this](const auto& p) {
                        return nodes_.find(p.first) == nodes_.end();
                    }),
                vectors_.end());
        }

        return removed;
    }

    // Create a snapshot for rollback
    uint64_t snapshot() {
        std::unique_lock lock(mutex_);
        uint64_t id = snapshots_.size();
        snapshots_.push_back({id, now(), nodes_});
        return id;
    }

    // Rollback to a snapshot
    bool rollback(uint64_t snapshot_id) {
        std::unique_lock lock(mutex_);
        for (const auto& snap : snapshots_) {
            if (snap.id == snapshot_id) {
                nodes_ = snap.nodes;
                // Rebuild vector index
                vectors_.clear();
                for (const auto& [id, node] : nodes_) {
                    vectors_.emplace_back(id, node.nu);
                }
                return true;
            }
        }
        return false;
    }

    // Compute coherence of the graph
    Coherence compute_coherence() {
        std::shared_lock lock(mutex_);

        float local = compute_local_coherence();
        float global = compute_global_coherence();
        float temporal = compute_temporal_coherence();

        Coherence c;
        c.local = local;
        c.global = global;
        c.temporal = temporal;
        c.tau = now();

        coherence_ = c;
        return c;
    }

    // Get current coherence without recomputing
    Coherence coherence() const {
        std::shared_lock lock(mutex_);
        return coherence_;
    }

    // Insert just an ID reference (for tiered storage tracking)
    void insert_raw(NodeId id) {
        std::unique_lock lock(mutex_);
        node_ids_.insert(id);
    }

    // Current snapshot ID
    uint64_t current_snapshot() const {
        std::shared_lock lock(mutex_);
        return snapshots_.empty() ? 0 : snapshots_.back().id;
    }

    // Number of nodes
    size_t size() const {
        std::shared_lock lock(mutex_);
        return nodes_.size();
    }

    bool empty() const {
        std::shared_lock lock(mutex_);
        return nodes_.empty();
    }

    // Get all nodes (for serialization)
    std::vector<Node> all_nodes() const {
        std::shared_lock lock(mutex_);
        std::vector<Node> result;
        result.reserve(nodes_.size());
        for (const auto& [_, node] : nodes_) {
            result.push_back(node);
        }
        return result;
    }

private:
    float compute_local_coherence() const {
        if (nodes_.empty()) return 1.0f;

        size_t contradictions = 0;
        size_t total_edges = 0;

        for (const auto& [_, node] : nodes_) {
            for (const auto& edge : node.edges) {
                total_edges++;
                if (edge.type == EdgeType::Contradicts) {
                    contradictions++;
                }
            }
        }

        if (total_edges == 0) return 1.0f;
        return 1.0f - static_cast<float>(contradictions) / static_cast<float>(total_edges);
    }

    float compute_global_coherence() const {
        if (nodes_.empty()) return 1.0f;

        float sum = 0.0f;
        for (const auto& [_, node] : nodes_) {
            sum += node.kappa.effective();
        }
        float avg = sum / static_cast<float>(nodes_.size());

        float variance = 0.0f;
        for (const auto& [_, node] : nodes_) {
            float diff = node.kappa.effective() - avg;
            variance += diff * diff;
        }
        variance /= static_cast<float>(nodes_.size());

        return avg * (1.0f - std::sqrt(variance));
    }

    float compute_temporal_coherence() const {
        if (nodes_.empty()) return 1.0f;

        Timestamp current = now();
        size_t recent = 0;
        size_t old = 0;

        for (const auto& [_, node] : nodes_) {
            float age_days = static_cast<float>(current - node.tau_accessed) / 86400000.0f;
            if (age_days < 7.0f) {
                recent++;
            } else if (age_days > 30.0f) {
                old++;
            }
        }

        float total = static_cast<float>(nodes_.size());
        float recent_ratio = static_cast<float>(recent) / total;
        float old_ratio = static_cast<float>(old) / total;

        return 0.5f + 0.3f * recent_ratio - 0.2f * old_ratio;
    }

    mutable std::shared_mutex mutex_;
    std::unordered_map<NodeId, Node, NodeIdHash> nodes_;
    std::unordered_set<NodeId, NodeIdHash> node_ids_;  // For tiered storage tracking
    std::vector<std::pair<NodeId, Vector>> vectors_;
    Coherence coherence_;
    std::vector<Snapshot> snapshots_;
};

} // namespace chitta
