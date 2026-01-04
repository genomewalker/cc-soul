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
        float structural = compute_structural_coherence();

        Coherence c;
        c.local = local;
        c.global = global;
        c.temporal = temporal;
        c.structural = structural;
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
    // Node type weights for importance-weighted calculations
    static float node_weight(NodeType type) {
        switch (type) {
            case NodeType::Invariant: return 2.0f;  // Core identity - highest
            case NodeType::Belief: return 1.5f;     // Guiding principles
            case NodeType::Wisdom: return 1.2f;     // Accumulated patterns
            case NodeType::Failure: return 1.0f;    // Lessons learned
            case NodeType::Intention: return 0.8f;  // Active goals
            case NodeType::Episode: return 0.5f;    // Observations
            case NodeType::Term: return 0.3f;       // Vocabulary
            default: return 0.5f;
        }
    }

    // Local coherence: explicit contradictions + semantic tension
    float compute_local_coherence() const {
        if (nodes_.empty()) return 1.0f;

        size_t contradictions = 0;
        size_t total_edges = 0;
        size_t semantic_tensions = 0;
        size_t belief_wisdom_pairs = 0;

        // Collect beliefs and wisdom for semantic tension check
        std::vector<const Node*> beliefs, wisdom;
        for (const auto& [_, node] : nodes_) {
            for (const auto& edge : node.edges) {
                total_edges++;
                if (edge.type == EdgeType::Contradicts) {
                    contradictions++;
                }
            }
            if (node.node_type == NodeType::Belief) beliefs.push_back(&node);
            if (node.node_type == NodeType::Wisdom) wisdom.push_back(&node);
        }

        // Check for semantic tension: similar embeddings without support edges
        // (Sample to avoid O(n²) for large graphs)
        size_t max_checks = std::min(beliefs.size() * wisdom.size(), size_t(100));
        for (size_t i = 0; i < std::min(beliefs.size(), size_t(10)); ++i) {
            for (size_t j = 0; j < std::min(wisdom.size(), size_t(10)); ++j) {
                belief_wisdom_pairs++;
                float sim = beliefs[i]->nu.cosine(wisdom[j]->nu);
                // High similarity but no explicit connection = potential tension
                if (sim > 0.7f) {
                    bool has_support = false;
                    for (const auto& edge : beliefs[i]->edges) {
                        if (edge.target == wisdom[j]->id &&
                            (edge.type == EdgeType::Supports || edge.type == EdgeType::Similar)) {
                            has_support = true;
                            break;
                        }
                    }
                    if (!has_support) semantic_tensions++;
                }
            }
        }

        float contradiction_ratio = total_edges > 0
            ? static_cast<float>(contradictions) / static_cast<float>(total_edges)
            : 0.0f;

        float tension_ratio = belief_wisdom_pairs > 0
            ? static_cast<float>(semantic_tensions) / static_cast<float>(belief_wisdom_pairs)
            : 0.0f;

        // Contradictions matter more than semantic tension
        return std::max(0.0f, 1.0f - contradiction_ratio - 0.3f * tension_ratio);
    }

    // Global coherence: importance-weighted confidence with variance penalty
    float compute_global_coherence() const {
        if (nodes_.empty()) return 1.0f;

        float weighted_sum = 0.0f;
        float weight_total = 0.0f;
        float important_sum = 0.0f;
        float important_count = 0.0f;

        for (const auto& [_, node] : nodes_) {
            float w = node_weight(node.node_type);
            float eff = node.kappa.effective();
            weighted_sum += eff * w;
            weight_total += w;

            // Track variance only for important nodes
            if (w >= 1.0f) {
                important_sum += eff;
                important_count += 1.0f;
            }
        }

        if (weight_total == 0.0f) return 1.0f;
        float weighted_avg = weighted_sum / weight_total;

        // Variance among important nodes only
        float variance = 0.0f;
        if (important_count > 1.0f) {
            float important_avg = important_sum / important_count;
            for (const auto& [_, node] : nodes_) {
                if (node_weight(node.node_type) >= 1.0f) {
                    float diff = node.kappa.effective() - important_avg;
                    variance += diff * diff;
                }
            }
            variance /= important_count;
        }

        // Penalize variance but not too harshly
        return weighted_avg * (1.0f - 0.5f * std::sqrt(variance));
    }

    // Temporal coherence: activity + maturity balance
    float compute_temporal_coherence() const {
        if (nodes_.empty()) return 0.5f;

        Timestamp current = now();
        float activity_score = 0.0f;
        float maturity_score = 0.0f;
        float maturity_count = 0.0f;

        for (const auto& [_, node] : nodes_) {
            float access_age_days = static_cast<float>(current - node.tau_accessed) / 86400000.0f;
            float creation_age_days = static_cast<float>(current - node.tau_created) / 86400000.0f;

            // Activity: recently accessed nodes
            if (access_age_days < 7.0f) {
                activity_score += 1.0f;
            } else if (access_age_days < 30.0f) {
                activity_score += 0.5f;
            }

            // Maturity: wisdom/beliefs that have survived are valuable
            if ((node.node_type == NodeType::Wisdom || node.node_type == NodeType::Belief) &&
                creation_age_days > 7.0f) {
                maturity_score += node.kappa.effective();
                maturity_count += 1.0f;
            }
        }

        float total = static_cast<float>(nodes_.size());
        float activity_ratio = activity_score / total;
        float maturity_ratio = maturity_count > 0.0f
            ? maturity_score / maturity_count
            : 0.5f;  // Neutral if no mature wisdom yet

        // Balance: active AND mature is best
        // Range: 0.3 (dead graph) to 1.0 (active + mature wisdom)
        return std::clamp(0.3f + 0.4f * activity_ratio + 0.3f * maturity_ratio, 0.0f, 1.0f);
    }

    // Structural coherence: connectivity health
    float compute_structural_coherence() const {
        if (nodes_.empty()) return 1.0f;

        size_t connected_nodes = 0;
        size_t orphan_nodes = 0;
        size_t total_edges = 0;

        for (const auto& [_, node] : nodes_) {
            if (node.edges.empty()) {
                orphan_nodes++;
            } else {
                connected_nodes++;
                total_edges += node.edges.size();
            }
        }

        float total = static_cast<float>(nodes_.size());

        // Orphan penalty: isolated knowledge is less coherent
        float orphan_ratio = static_cast<float>(orphan_nodes) / total;

        // Edge density: more connections = more integrated
        // Use log scale to avoid penalizing large graphs
        float expected_edges = total * std::log2(std::max(total, 2.0f));
        float edge_density = std::min(static_cast<float>(total_edges) / expected_edges, 1.0f);

        // Structural score: penalize orphans, reward connectivity
        return std::clamp((1.0f - 0.5f * orphan_ratio) * (0.5f + 0.5f * edge_density), 0.0f, 1.0f);
    }

    mutable std::shared_mutex mutex_;
    std::unordered_map<NodeId, Node, NodeIdHash> nodes_;
    std::unordered_set<NodeId, NodeIdHash> node_ids_;  // For tiered storage tracking
    std::vector<std::pair<NodeId, Vector>> vectors_;
    Coherence coherence_;
    std::vector<Snapshot> snapshots_;
};

} // namespace chitta
