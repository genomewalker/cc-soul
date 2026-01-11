#pragma once
// The Graph: where soul lives
//
// Nodes connected by edges. Vector-indexed for semantic search.
// The graph IS the soul - not a container, the thing itself.

#include "types.hpp"
#include "hnsw.hpp"  // For NodeIdHash
#include <algorithm>
#include <fstream>
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

// ═══════════════════════════════════════════════════════════════════════════
// Incremental Coherence Tracker
// Maintains running statistics for O(1) coherence computation
// ═══════════════════════════════════════════════════════════════════════════

class CoherenceTracker {
public:
    // Node type weights for global coherence
    static float type_weight(NodeType t) {
        switch (t) {
            case NodeType::Invariant: return 2.0f;
            case NodeType::Belief:    return 1.5f;
            case NodeType::Wisdom:    return 1.2f;
            case NodeType::Failure:   return 1.0f;
            case NodeType::Aspiration:return 0.8f;
            case NodeType::Dream:     return 0.7f;
            case NodeType::Term:      return 0.5f;
            case NodeType::Episode:   return 0.5f;
            default: return 1.0f;
        }
    }

    // Called when a node is inserted
    void on_insert(const Node& node) {
        float w = type_weight(node.node_type);
        float eff = node.kappa.effective();

        // Global coherence stats
        stats_.weighted_confidence_sum += w * eff;
        stats_.weight_sum += w;
        stats_.node_count++;

        if (w >= 1.0f) {
            stats_.important_confidence_sum += eff;
            stats_.important_count++;
        }

        // Structural coherence stats
        if (node.edges.empty()) {
            stats_.orphan_count++;
        } else {
            stats_.connected_count++;
            stats_.edge_count += node.edges.size();

            // Track contradiction edges
            for (const auto& edge : node.edges) {
                if (edge.type == EdgeType::Contradicts) {
                    stats_.contradiction_count++;
                }
            }
        }

        // Temporal coherence stats
        if (node.node_type == NodeType::Belief || node.node_type == NodeType::Wisdom) {
            stats_.belief_wisdom_count++;
        }

        // Type-specific counts for semantic tension
        if (node.node_type == NodeType::Belief) stats_.belief_count++;
        if (node.node_type == NodeType::Wisdom) stats_.wisdom_count++;

        dirty_ = true;
    }

    // Called when a node is removed
    void on_remove(const Node& node) {
        float w = type_weight(node.node_type);
        float eff = node.kappa.effective();

        // Global coherence stats
        stats_.weighted_confidence_sum -= w * eff;
        stats_.weight_sum -= w;
        stats_.node_count--;

        if (w >= 1.0f) {
            stats_.important_confidence_sum -= eff;
            stats_.important_count--;
        }

        // Structural coherence stats
        if (node.edges.empty()) {
            stats_.orphan_count--;
        } else {
            stats_.connected_count--;
            stats_.edge_count -= node.edges.size();

            for (const auto& edge : node.edges) {
                if (edge.type == EdgeType::Contradicts) {
                    stats_.contradiction_count--;
                }
            }
        }

        // Temporal coherence stats
        if (node.node_type == NodeType::Belief || node.node_type == NodeType::Wisdom) {
            stats_.belief_wisdom_count--;
        }

        if (node.node_type == NodeType::Belief) stats_.belief_count--;
        if (node.node_type == NodeType::Wisdom) stats_.wisdom_count--;

        dirty_ = true;
    }

    // Called when a node's confidence changes
    void on_confidence_change(const Node& node, float old_eff, float new_eff) {
        float w = type_weight(node.node_type);
        float delta = new_eff - old_eff;

        stats_.weighted_confidence_sum += w * delta;

        if (w >= 1.0f) {
            stats_.important_confidence_sum += delta;
        }

        dirty_ = true;
    }

    // Called when an edge is added
    void on_edge_add(const Node& from_node, EdgeType edge_type) {
        // Node was orphan, now connected
        if (from_node.edges.size() == 1) {
            stats_.orphan_count--;
            stats_.connected_count++;
        }

        stats_.edge_count++;

        if (edge_type == EdgeType::Contradicts) {
            stats_.contradiction_count++;
        }

        dirty_ = true;
    }

    // Called when an edge is removed
    void on_edge_remove(const Node& from_node, EdgeType edge_type) {
        stats_.edge_count--;

        if (edge_type == EdgeType::Contradicts) {
            stats_.contradiction_count--;
        }

        // Node becomes orphan
        if (from_node.edges.empty()) {
            stats_.orphan_count++;
            stats_.connected_count--;
        }

        dirty_ = true;
    }

    // Update temporal stats (call periodically with current time)
    void update_temporal(const std::unordered_map<NodeId, Node, NodeIdHash>& nodes, Timestamp current) {
        stats_.recent_access_count = 0;
        stats_.mature_confidence_sum = 0.0f;
        stats_.mature_count = 0;

        for (const auto& [_, node] : nodes) {
            float access_age_days = static_cast<float>(current - node.tau_accessed) / 86400000.0f;
            float creation_age_days = static_cast<float>(current - node.tau_created) / 86400000.0f;

            // Recent access (last 7 days)
            if (access_age_days < 7.0f) {
                stats_.recent_access_count++;
            } else if (access_age_days < 30.0f) {
                stats_.recent_access_count += 0.5f;  // Partial credit
            }

            // Mature wisdom/beliefs
            if ((node.node_type == NodeType::Wisdom || node.node_type == NodeType::Belief) &&
                creation_age_days > 7.0f) {
                stats_.mature_confidence_sum += node.kappa.effective();
                stats_.mature_count++;
            }
        }

        temporal_dirty_ = false;
    }

    // Compute coherence from stats - O(1) if stats are up to date
    Coherence compute(Timestamp current = 0) {
        if (stats_.node_count == 0) {
            Coherence empty;
            empty.local = 1.0f;
            empty.global = 1.0f;
            empty.temporal = 0.5f;
            empty.structural = 1.0f;
            return empty;
        }

        Coherence c;

        // Local coherence: contradiction ratio
        // (semantic tension requires sampling, done separately if needed)
        float total_edges = static_cast<float>(stats_.edge_count);
        float contradiction_ratio = total_edges > 0
            ? static_cast<float>(stats_.contradiction_count) / total_edges
            : 0.0f;
        c.local = std::max(0.0f, 1.0f - contradiction_ratio);

        // Global coherence: weighted confidence with variance penalty
        float weighted_avg = stats_.weight_sum > 0
            ? stats_.weighted_confidence_sum / stats_.weight_sum
            : 1.0f;

        // Estimate variance using Welford's approximation
        // For simplicity, use 1 - std_dev_proxy
        float important_avg = stats_.important_count > 0
            ? stats_.important_confidence_sum / stats_.important_count
            : weighted_avg;
        // Variance penalty estimated from deviation from mean
        float variance_penalty = std::abs(weighted_avg - important_avg) * 0.5f;
        c.global = weighted_avg * (1.0f - variance_penalty);

        // Temporal coherence: activity + maturity
        float total = static_cast<float>(stats_.node_count);
        float activity_ratio = stats_.recent_access_count / total;
        float maturity_ratio = stats_.mature_count > 0
            ? stats_.mature_confidence_sum / stats_.mature_count
            : 0.5f;
        c.temporal = std::clamp(0.3f + 0.4f * activity_ratio + 0.3f * maturity_ratio, 0.0f, 1.0f);

        // Structural coherence: orphan penalty + edge density
        float orphan_ratio = static_cast<float>(stats_.orphan_count) / total;
        float expected_edges = total * std::log2(std::max(total, 2.0f));
        float edge_density = std::min(static_cast<float>(stats_.edge_count) / expected_edges, 1.0f);
        c.structural = std::clamp((1.0f - 0.5f * orphan_ratio) * (0.5f + 0.5f * edge_density), 0.0f, 1.0f);

        dirty_ = false;
        return c;
    }

    // Check if recomputation needed
    bool is_dirty() const { return dirty_; }
    bool is_temporal_dirty() const { return temporal_dirty_; }

    // Reset all stats (call after major operations like rollback)
    void reset() {
        stats_ = {};
        dirty_ = true;
        temporal_dirty_ = true;
    }

    // Rebuild stats from scratch (call after rollback or load)
    void rebuild(const std::unordered_map<NodeId, Node, NodeIdHash>& nodes) {
        reset();
        for (const auto& [_, node] : nodes) {
            on_insert(node);
        }
        dirty_ = false;
    }

private:
    struct Stats {
        // Global coherence
        float weighted_confidence_sum = 0.0f;
        float weight_sum = 0.0f;
        float important_confidence_sum = 0.0f;
        size_t important_count = 0;
        size_t node_count = 0;

        // Structural coherence
        size_t orphan_count = 0;
        size_t connected_count = 0;
        size_t edge_count = 0;
        size_t contradiction_count = 0;

        // Temporal coherence (needs periodic refresh)
        float recent_access_count = 0.0f;
        float mature_confidence_sum = 0.0f;
        size_t mature_count = 0;

        // Type counts for semantic tension
        size_t belief_count = 0;
        size_t wisdom_count = 0;
        size_t belief_wisdom_count = 0;
    };

    Stats stats_;
    bool dirty_ = true;
    bool temporal_dirty_ = true;
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
            coherence_tracker_.on_insert(node);
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
        std::unique_lock lock(mutex_);
        auto it = nodes_.find(from);
        if (it == nodes_.end()) return false;

        it->second.connect(to, edge_type, weight);
        coherence_tracker_.on_edge_add(it->second, edge_type);
        return true;
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
                coherence_tracker_.on_remove(n);
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
                // Rebuild coherence tracker
                coherence_tracker_.rebuild(nodes_);
                return true;
            }
        }
        return false;
    }

    // Compute coherence of the graph - O(1) using incremental tracker
    // Falls back to full computation for semantic tension if needed
    Coherence compute_coherence() {
        std::shared_lock lock(mutex_);

        // Update temporal stats periodically (requires full scan)
        if (coherence_tracker_.is_temporal_dirty()) {
            coherence_tracker_.update_temporal(nodes_, now());
        }

        // Get incremental coherence (O(1) for most components)
        Coherence c = coherence_tracker_.compute(now());

        // Adjust local coherence with semantic tension (sampled, O(100))
        // Only do this if there are enough beliefs/wisdom nodes
        float tension_penalty = compute_semantic_tension_sampled();
        c.local = std::max(0.0f, c.local - 0.3f * tension_penalty);

        c.tau = now();
        coherence_ = c;
        return c;
    }

    // Fast coherence query - uses cached value, O(1)
    Coherence coherence() const {
        std::shared_lock lock(mutex_);
        return coherence_;
    }

    // Force full coherence recomputation (for accuracy verification)
    Coherence compute_coherence_full() {
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

    // Sampled semantic tension check - O(100) max
    // Returns tension ratio for local coherence penalty
    float compute_semantic_tension_sampled() const {
        // Collect beliefs and wisdom nodes
        std::vector<const Node*> beliefs, wisdom;
        for (const auto& [_, node] : nodes_) {
            if (node.node_type == NodeType::Belief) beliefs.push_back(&node);
            if (node.node_type == NodeType::Wisdom) wisdom.push_back(&node);
        }

        if (beliefs.empty() || wisdom.empty()) return 0.0f;

        size_t semantic_tensions = 0;
        size_t pairs_checked = 0;

        // Sample up to 10x10 = 100 pairs
        for (size_t i = 0; i < std::min(beliefs.size(), size_t(10)); ++i) {
            for (size_t j = 0; j < std::min(wisdom.size(), size_t(10)); ++j) {
                pairs_checked++;
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

        return pairs_checked > 0
            ? static_cast<float>(semantic_tensions) / static_cast<float>(pairs_checked)
            : 0.0f;
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
    mutable CoherenceTracker coherence_tracker_;  // Incremental coherence computation
    std::vector<Snapshot> snapshots_;

    // ═══════════════════════════════════════════════════════════════════
    // Entity-centric layer: structured knowledge on top of semantic graph
    // ═══════════════════════════════════════════════════════════════════

    // Entity index: canonical_name → Entity
    std::unordered_map<std::string, Entity> entities_;

    // Triplet storage: subject_id → list of triplets
    std::unordered_map<NodeId, std::vector<Triplet>, NodeIdHash> triplets_by_subject_;

    // Reverse index: object_id → subject_ids that reference it
    std::unordered_map<NodeId, std::vector<NodeId>, NodeIdHash> triplets_by_object_;

    // Mention index: entity_id → episode/wisdom ids that mention it
    std::unordered_map<NodeId, std::vector<NodeId>, NodeIdHash> mentions_;

public:
    // ═══════════════════════════════════════════════════════════════════
    // Entity management
    // ═══════════════════════════════════════════════════════════════════

    // Find entity by name (case-insensitive)
    std::optional<Entity> find_entity(const std::string& name) const {
        std::shared_lock lock(mutex_);
        // Normalize name for lookup
        std::string lower_name;
        for (char c : name) {
            if (c == ' ' && (lower_name.empty() || lower_name.back() == ' '))
                continue;
            lower_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        while (!lower_name.empty() && lower_name.back() == ' ')
            lower_name.pop_back();

        auto it = entities_.find(lower_name);
        if (it != entities_.end()) return it->second;

        // Check aliases
        for (const auto& [_, entity] : entities_) {
            if (entity.matches(name)) return entity;
        }
        return std::nullopt;
    }

    // Find or create entity
    Entity& find_or_create_entity(const std::string& name, EntityType type = EntityType::Unknown) {
        std::unique_lock lock(mutex_);
        // Normalize name
        std::string lower_name;
        for (char c : name) {
            if (c == ' ' && (lower_name.empty() || lower_name.back() == ' '))
                continue;
            lower_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        while (!lower_name.empty() && lower_name.back() == ' ')
            lower_name.pop_back();

        auto it = entities_.find(lower_name);
        if (it != entities_.end()) {
            // Update type if upgrading from Unknown
            if (it->second.entity_type == EntityType::Unknown && type != EntityType::Unknown) {
                it->second.entity_type = type;
            }
            return it->second;
        }

        // Create new entity
        Entity entity(name, type);
        entities_.emplace(lower_name, entity);
        return entities_[lower_name];
    }

    // Add alias to existing entity
    bool add_entity_alias(const std::string& canonical, const std::string& alias) {
        std::unique_lock lock(mutex_);
        std::string lower_name;
        for (char c : canonical) {
            lower_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        auto it = entities_.find(lower_name);
        if (it != entities_.end()) {
            it->second.add_alias(alias);
            return true;
        }
        return false;
    }

    // Merge two entities (keep first, absorb second)
    bool merge_entities(const std::string& keep_name, const std::string& absorb_name) {
        std::unique_lock lock(mutex_);
        std::string keep_lower, absorb_lower;
        for (char c : keep_name) keep_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char c : absorb_name) absorb_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        auto keep_it = entities_.find(keep_lower);
        auto absorb_it = entities_.find(absorb_lower);
        if (keep_it == entities_.end() || absorb_it == entities_.end()) return false;

        Entity& keep = keep_it->second;
        Entity& absorb = absorb_it->second;

        // Transfer aliases
        keep.add_alias(absorb.canonical_name);
        for (const auto& alias : absorb.aliases) {
            keep.add_alias(alias);
        }

        // Transfer triplets
        NodeId absorb_id = absorb.id;
        NodeId keep_id = keep.id;

        // Update triplets where absorb is subject
        auto subj_it = triplets_by_subject_.find(absorb_id);
        if (subj_it != triplets_by_subject_.end()) {
            for (auto& triplet : subj_it->second) {
                triplet.subject = keep_id;
                triplets_by_subject_[keep_id].push_back(triplet);
            }
            triplets_by_subject_.erase(subj_it);
        }

        // Update triplets where absorb is object
        auto obj_it = triplets_by_object_.find(absorb_id);
        if (obj_it != triplets_by_object_.end()) {
            for (const auto& subj_id : obj_it->second) {
                auto& triplets = triplets_by_subject_[subj_id];
                for (auto& t : triplets) {
                    if (t.object == absorb_id) t.object = keep_id;
                }
                triplets_by_object_[keep_id].push_back(subj_id);
            }
            triplets_by_object_.erase(obj_it);
        }

        // Transfer mentions
        auto mention_it = mentions_.find(absorb_id);
        if (mention_it != mentions_.end()) {
            auto& keep_mentions = mentions_[keep_id];
            keep_mentions.insert(keep_mentions.end(),
                                 mention_it->second.begin(), mention_it->second.end());
            mentions_.erase(mention_it);
        }

        // Merge counts
        keep.mention_count += absorb.mention_count;
        if (absorb.last_mentioned > keep.last_mentioned) {
            keep.last_mentioned = absorb.last_mentioned;
        }

        // Remove absorbed entity
        entities_.erase(absorb_it);
        return true;
    }

    // Get all entities
    std::vector<Entity> all_entities() const {
        std::shared_lock lock(mutex_);
        std::vector<Entity> result;
        result.reserve(entities_.size());
        for (const auto& [_, entity] : entities_) {
            result.push_back(entity);
        }
        return result;
    }

    size_t entity_count() const {
        std::shared_lock lock(mutex_);
        return entities_.size();
    }

    // ═══════════════════════════════════════════════════════════════════
    // Triplet management
    // ═══════════════════════════════════════════════════════════════════

    // Add a triplet
    void add_triplet(const Triplet& triplet) {
        std::unique_lock lock(mutex_);
        triplets_by_subject_[triplet.subject].push_back(triplet);
        triplets_by_object_[triplet.object].push_back(triplet.subject);
    }

    // Add triplet with source tracking
    void add_triplet(NodeId subject, const std::string& predicate, NodeId object,
                     float weight = 1.0f, NodeId source = NodeId{}) {
        Triplet t(subject, predicate, object, weight);
        if (source.high != 0 || source.low != 0) {
            t.source = source;
        }
        add_triplet(t);
    }

    // Query triplets by subject
    std::vector<Triplet> triplets_for_subject(NodeId subject) const {
        std::shared_lock lock(mutex_);
        auto it = triplets_by_subject_.find(subject);
        if (it != triplets_by_subject_.end()) return it->second;
        return {};
    }

    // Query triplets by predicate (scans all, use sparingly)
    std::vector<Triplet> triplets_by_predicate(const std::string& predicate) const {
        std::shared_lock lock(mutex_);
        std::vector<Triplet> results;
        for (const auto& [_, triplets] : triplets_by_subject_) {
            for (const auto& t : triplets) {
                if (t.predicate == predicate) results.push_back(t);
            }
        }
        return results;
    }

    // Query triplets by object (reverse lookup)
    std::vector<Triplet> triplets_for_object(NodeId object) const {
        std::shared_lock lock(mutex_);
        std::vector<Triplet> results;
        auto it = triplets_by_object_.find(object);
        if (it == triplets_by_object_.end()) return results;

        for (const auto& subj_id : it->second) {
            auto subj_it = triplets_by_subject_.find(subj_id);
            if (subj_it != triplets_by_subject_.end()) {
                for (const auto& t : subj_it->second) {
                    if (t.object == object) results.push_back(t);
                }
            }
        }
        return results;
    }

    // Pattern query: (subject?, predicate?, object?)
    std::vector<Triplet> query_triplets(
        std::optional<NodeId> subject = std::nullopt,
        std::optional<std::string> predicate = std::nullopt,
        std::optional<NodeId> object = std::nullopt) const
    {
        std::shared_lock lock(mutex_);
        std::vector<Triplet> results;

        if (subject) {
            // Start from subject index
            auto it = triplets_by_subject_.find(*subject);
            if (it == triplets_by_subject_.end()) return results;
            for (const auto& t : it->second) {
                if (predicate && t.predicate != *predicate) continue;
                if (object && t.object != *object) continue;
                results.push_back(t);
            }
        } else if (object) {
            // Start from object index
            results = triplets_for_object(*object);
            if (predicate) {
                results.erase(std::remove_if(results.begin(), results.end(),
                    [&](const Triplet& t) { return t.predicate != *predicate; }),
                    results.end());
            }
        } else if (predicate) {
            // Full scan by predicate
            results = triplets_by_predicate(*predicate);
        } else {
            // Return all triplets
            for (const auto& [_, triplets] : triplets_by_subject_) {
                results.insert(results.end(), triplets.begin(), triplets.end());
            }
        }
        return results;
    }

    size_t triplet_count() const {
        std::shared_lock lock(mutex_);
        size_t count = 0;
        for (const auto& [_, triplets] : triplets_by_subject_) {
            count += triplets.size();
        }
        return count;
    }

    // ═══════════════════════════════════════════════════════════════════
    // Mention tracking
    // ═══════════════════════════════════════════════════════════════════

    // Record that an episode/wisdom mentions an entity
    void add_mention(NodeId entity_id, NodeId episode_id) {
        std::unique_lock lock(mutex_);
        mentions_[entity_id].push_back(episode_id);
    }

    // Get all episodes that mention an entity
    std::vector<NodeId> mentions_of(NodeId entity_id) const {
        std::shared_lock lock(mutex_);
        auto it = mentions_.find(entity_id);
        if (it != mentions_.end()) return it->second;
        return {};
    }

    // ═══════════════════════════════════════════════════════════════════
    // Triplet snapshot persistence
    // ═══════════════════════════════════════════════════════════════════

    // Save triplets to binary file
    bool save_triplets(const std::string& path) const {
        std::shared_lock lock(mutex_);

        std::ofstream out(path, std::ios::binary);
        if (!out) return false;

        // Header: magic + version + count
        uint32_t magic = 0x54524950;  // "TRIP"
        uint32_t version = 1;
        uint64_t count = 0;
        for (const auto& [_, triplets] : triplets_by_subject_) {
            count += triplets.size();
        }

        out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        out.write(reinterpret_cast<const char*>(&version), sizeof(version));
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));

        // Write each triplet
        for (const auto& [_, triplets] : triplets_by_subject_) {
            for (const auto& t : triplets) {
                // Subject + Object (16 bytes each)
                out.write(reinterpret_cast<const char*>(&t.subject.high), sizeof(t.subject.high));
                out.write(reinterpret_cast<const char*>(&t.subject.low), sizeof(t.subject.low));
                out.write(reinterpret_cast<const char*>(&t.object.high), sizeof(t.object.high));
                out.write(reinterpret_cast<const char*>(&t.object.low), sizeof(t.object.low));
                // Weight
                out.write(reinterpret_cast<const char*>(&t.weight), sizeof(t.weight));
                // Predicate (length + string)
                uint32_t pred_len = static_cast<uint32_t>(t.predicate.size());
                out.write(reinterpret_cast<const char*>(&pred_len), sizeof(pred_len));
                out.write(t.predicate.data(), pred_len);
            }
        }

        out.close();
        return true;
    }

    // Load triplets from binary file
    bool load_triplets(const std::string& path) {
        std::unique_lock lock(mutex_);

        std::ifstream in(path, std::ios::binary);
        if (!in) return false;

        // Read header
        uint32_t magic, version;
        uint64_t count;
        in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        in.read(reinterpret_cast<char*>(&version), sizeof(version));
        in.read(reinterpret_cast<char*>(&count), sizeof(count));

        if (magic != 0x54524950 || version != 1) {
            return false;
        }

        // Clear existing triplets
        triplets_by_subject_.clear();
        triplets_by_object_.clear();

        // Read triplets
        for (uint64_t i = 0; i < count; ++i) {
            Triplet t;

            // Subject + Object
            in.read(reinterpret_cast<char*>(&t.subject.high), sizeof(t.subject.high));
            in.read(reinterpret_cast<char*>(&t.subject.low), sizeof(t.subject.low));
            in.read(reinterpret_cast<char*>(&t.object.high), sizeof(t.object.high));
            in.read(reinterpret_cast<char*>(&t.object.low), sizeof(t.object.low));
            // Weight
            in.read(reinterpret_cast<char*>(&t.weight), sizeof(t.weight));
            // Predicate
            uint32_t pred_len;
            in.read(reinterpret_cast<char*>(&pred_len), sizeof(pred_len));
            t.predicate.resize(pred_len);
            in.read(t.predicate.data(), pred_len);

            // Add to indices (without lock since we hold unique_lock)
            triplets_by_subject_[t.subject].push_back(t);
            triplets_by_object_[t.object].push_back(t.subject);
        }

        in.close();
        return true;
    }

    // Get all triplets (for WAL batch persist)
    std::vector<Triplet> all_triplets() const {
        std::shared_lock lock(mutex_);
        std::vector<Triplet> result;
        for (const auto& [_, triplets] : triplets_by_subject_) {
            result.insert(result.end(), triplets.begin(), triplets.end());
        }
        return result;
    }
};

} // namespace chitta
