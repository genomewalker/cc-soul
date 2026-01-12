#pragma once
// Contradiction Loom: Explicit conflict handling and truth maintenance
//
// Tracks contradictions between nodes:
// - Explicit Contradicts edges
// - Resolution nodes with rationale
// - Conflict surfacing at query time
//
// Prevents silent knowledge corruption by making conflicts visible.

#include "types.hpp"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>

namespace chitta {

// Contradiction status
enum class ContradictionStatus : uint8_t {
    Unresolved = 0,    // Conflict exists, no resolution
    Resolved = 1,      // Conflict resolved (one side chosen)
    Superseded = 2,    // Both superseded by new knowledge
    Coexisting = 3,    // Both valid in different contexts
};

// A contradiction between two nodes
struct Contradiction {
    NodeId node_a;                 // First conflicting node
    NodeId node_b;                 // Second conflicting node
    ContradictionStatus status = ContradictionStatus::Unresolved;
    std::string description;       // What the conflict is about
    NodeId resolution_node;        // Node containing resolution (if resolved)
    NodeId winner;                 // Which node "won" (if resolved)
    Timestamp detected_at = 0;     // When conflict was detected
    Timestamp resolved_at = 0;     // When resolved (0 if unresolved)
    float confidence = 0.5f;       // Confidence in the contradiction (0-1)
};

// Resolution rationale
struct Resolution {
    NodeId resolution_id;          // ID of resolution node
    NodeId winner;                 // Which node was chosen
    NodeId loser;                  // Which node was deprecated
    std::string rationale;         // Why winner was chosen
    Timestamp resolved_at = 0;
    std::string evidence;          // Supporting evidence
};

// Truth maintenance system
class TruthMaintenance {
public:
    TruthMaintenance() = default;

    // Register a contradiction between two nodes
    void add_contradiction(const NodeId& a, const NodeId& b,
                          const std::string& description,
                          float confidence = 0.5f,
                          Timestamp now = 0) {
        // Normalize order (smaller ID first)
        NodeId first = (a < b) ? a : b;
        NodeId second = (a < b) ? b : a;

        auto key = make_key(first, second);
        if (contradictions_.count(key)) {
            return;  // Already exists
        }

        Contradiction c;
        c.node_a = first;
        c.node_b = second;
        c.description = description;
        c.confidence = confidence;
        c.detected_at = now;
        c.status = ContradictionStatus::Unresolved;

        contradictions_[key] = c;

        // Track which nodes are involved in contradictions
        node_conflicts_[first].insert(second);
        node_conflicts_[second].insert(first);
    }

    // Resolve a contradiction
    void resolve(const NodeId& a, const NodeId& b,
                const NodeId& winner,
                const NodeId& resolution_node,
                const std::string& rationale,
                Timestamp now = 0) {
        NodeId first = (a < b) ? a : b;
        NodeId second = (a < b) ? b : a;

        auto key = make_key(first, second);
        auto it = contradictions_.find(key);
        if (it == contradictions_.end()) return;

        it->second.status = ContradictionStatus::Resolved;
        it->second.winner = winner;
        it->second.resolution_node = resolution_node;
        it->second.resolved_at = now;

        // Store resolution details
        Resolution r;
        r.resolution_id = resolution_node;
        r.winner = winner;
        r.loser = (winner == first) ? second : first;
        r.rationale = rationale;
        r.resolved_at = now;
        resolutions_[resolution_node] = r;
    }

    // Mark contradiction as coexisting (both valid in context)
    void mark_coexisting(const NodeId& a, const NodeId& b,
                        const std::string& context_description,
                        Timestamp now = 0) {
        NodeId first = (a < b) ? a : b;
        NodeId second = (a < b) ? b : a;

        auto key = make_key(first, second);
        auto it = contradictions_.find(key);
        if (it == contradictions_.end()) return;

        it->second.status = ContradictionStatus::Coexisting;
        it->second.description += " [Coexisting: " + context_description + "]";
        it->second.resolved_at = now;
    }

    // Get all contradictions involving a node
    std::vector<Contradiction> get_conflicts(const NodeId& id) const {
        std::vector<Contradiction> result;

        auto it = node_conflicts_.find(id);
        if (it == node_conflicts_.end()) return result;

        for (const auto& other : it->second) {
            NodeId first = (id < other) ? id : other;
            NodeId second = (id < other) ? other : id;
            auto key = make_key(first, second);

            auto cit = contradictions_.find(key);
            if (cit != contradictions_.end()) {
                result.push_back(cit->second);
            }
        }

        return result;
    }

    // Get all unresolved contradictions
    std::vector<Contradiction> get_unresolved() const {
        std::vector<Contradiction> result;
        for (const auto& [_, c] : contradictions_) {
            if (c.status == ContradictionStatus::Unresolved) {
                result.push_back(c);
            }
        }
        return result;
    }

    // Check if a node has unresolved conflicts
    bool has_unresolved_conflicts(const NodeId& id) const {
        for (const auto& c : get_conflicts(id)) {
            if (c.status == ContradictionStatus::Unresolved) {
                return true;
            }
        }
        return false;
    }

    // Check if two nodes contradict each other
    bool contradicts(const NodeId& a, const NodeId& b) const {
        NodeId first = (a < b) ? a : b;
        NodeId second = (a < b) ? b : a;
        auto key = make_key(first, second);
        return contradictions_.count(key) > 0;
    }

    // Get the resolution for a node (if it lost a contradiction)
    const Resolution* get_resolution(const NodeId& resolution_node) const {
        auto it = resolutions_.find(resolution_node);
        return (it != resolutions_.end()) ? &it->second : nullptr;
    }

    // Filter recall results to surface conflicts
    // Returns nodes with conflicts flagged
    struct RecallWithConflicts {
        NodeId id;
        float score;
        bool has_conflict;
        std::vector<NodeId> conflicting_nodes;
    };

    std::vector<RecallWithConflicts> annotate_conflicts(
        const std::vector<std::pair<NodeId, float>>& results) const
    {
        std::vector<RecallWithConflicts> annotated;
        annotated.reserve(results.size());

        // Build set of result IDs for quick lookup
        std::unordered_set<NodeId, NodeIdHash> result_ids;
        for (const auto& [id, _] : results) {
            result_ids.insert(id);
        }

        for (const auto& [id, score] : results) {
            RecallWithConflicts r;
            r.id = id;
            r.score = score;
            r.has_conflict = false;

            // Check if this node conflicts with any other result
            auto it = node_conflicts_.find(id);
            if (it != node_conflicts_.end()) {
                for (const auto& other : it->second) {
                    if (result_ids.count(other)) {
                        r.has_conflict = true;
                        r.conflicting_nodes.push_back(other);
                    }
                }
            }

            annotated.push_back(r);
        }

        return annotated;
    }

    // Remove contradictions involving a deleted node
    void remove_node(const NodeId& id) {
        auto it = node_conflicts_.find(id);
        if (it == node_conflicts_.end()) return;

        // Remove all contradictions involving this node
        for (const auto& other : it->second) {
            NodeId first = (id < other) ? id : other;
            NodeId second = (id < other) ? other : id;
            contradictions_.erase(make_key(first, second));

            // Clean up other node's conflict set
            auto oit = node_conflicts_.find(other);
            if (oit != node_conflicts_.end()) {
                oit->second.erase(id);
            }
        }

        node_conflicts_.erase(it);
        resolutions_.erase(id);
    }

    // Statistics
    size_t total_contradictions() const { return contradictions_.size(); }

    size_t unresolved_count() const {
        size_t count = 0;
        for (const auto& [_, c] : contradictions_) {
            if (c.status == ContradictionStatus::Unresolved) count++;
        }
        return count;
    }

    // Persistence
    bool save(const std::string& path) const {
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) return false;

        uint32_t magic = 0x54525554;  // "TRUT"
        uint32_t version = 1;
        uint64_t count = contradictions_.size();

        fwrite(&magic, sizeof(magic), 1, f);
        fwrite(&version, sizeof(version), 1, f);
        fwrite(&count, sizeof(count), 1, f);

        for (const auto& [key, c] : contradictions_) {
            fwrite(&c.node_a.high, sizeof(c.node_a.high), 1, f);
            fwrite(&c.node_a.low, sizeof(c.node_a.low), 1, f);
            fwrite(&c.node_b.high, sizeof(c.node_b.high), 1, f);
            fwrite(&c.node_b.low, sizeof(c.node_b.low), 1, f);
            fwrite(&c.status, sizeof(c.status), 1, f);
            fwrite(&c.winner.high, sizeof(c.winner.high), 1, f);
            fwrite(&c.winner.low, sizeof(c.winner.low), 1, f);
            fwrite(&c.resolution_node.high, sizeof(c.resolution_node.high), 1, f);
            fwrite(&c.resolution_node.low, sizeof(c.resolution_node.low), 1, f);
            fwrite(&c.detected_at, sizeof(c.detected_at), 1, f);
            fwrite(&c.resolved_at, sizeof(c.resolved_at), 1, f);
            fwrite(&c.confidence, sizeof(c.confidence), 1, f);

            uint16_t desc_len = static_cast<uint16_t>(std::min(c.description.size(), size_t(65535)));
            fwrite(&desc_len, sizeof(desc_len), 1, f);
            fwrite(c.description.data(), 1, desc_len, f);
        }

        fclose(f);
        return true;
    }

    bool load(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;

        uint32_t magic, version;
        uint64_t count;

        if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x54525554 ||
            fread(&version, sizeof(version), 1, f) != 1 || version != 1 ||
            fread(&count, sizeof(count), 1, f) != 1 || count > 10000000) {
            fclose(f);
            return false;
        }

        contradictions_.clear();
        node_conflicts_.clear();

        for (uint64_t i = 0; i < count; ++i) {
            Contradiction c;

            if (fread(&c.node_a.high, sizeof(c.node_a.high), 1, f) != 1 ||
                fread(&c.node_a.low, sizeof(c.node_a.low), 1, f) != 1 ||
                fread(&c.node_b.high, sizeof(c.node_b.high), 1, f) != 1 ||
                fread(&c.node_b.low, sizeof(c.node_b.low), 1, f) != 1 ||
                fread(&c.status, sizeof(c.status), 1, f) != 1 ||
                fread(&c.winner.high, sizeof(c.winner.high), 1, f) != 1 ||
                fread(&c.winner.low, sizeof(c.winner.low), 1, f) != 1 ||
                fread(&c.resolution_node.high, sizeof(c.resolution_node.high), 1, f) != 1 ||
                fread(&c.resolution_node.low, sizeof(c.resolution_node.low), 1, f) != 1 ||
                fread(&c.detected_at, sizeof(c.detected_at), 1, f) != 1 ||
                fread(&c.resolved_at, sizeof(c.resolved_at), 1, f) != 1 ||
                fread(&c.confidence, sizeof(c.confidence), 1, f) != 1) {
                fclose(f);
                return false;
            }

            uint16_t desc_len;
            if (fread(&desc_len, sizeof(desc_len), 1, f) != 1) {
                fclose(f);
                return false;
            }

            c.description.resize(desc_len);
            if (fread(&c.description[0], 1, desc_len, f) != desc_len) {
                fclose(f);
                return false;
            }

            auto key = make_key(c.node_a, c.node_b);
            contradictions_[key] = c;
            node_conflicts_[c.node_a].insert(c.node_b);
            node_conflicts_[c.node_b].insert(c.node_a);
        }

        fclose(f);
        return true;
    }

private:
    // Create unique key for node pair
    static std::pair<NodeId, NodeId> make_key(const NodeId& a, const NodeId& b) {
        return {a, b};
    }

    // Hash for pair of NodeIds
    struct PairHash {
        size_t operator()(const std::pair<NodeId, NodeId>& p) const {
            NodeIdHash h;
            return h(p.first) ^ (h(p.second) << 1);
        }
    };

    std::unordered_map<std::pair<NodeId, NodeId>, Contradiction, PairHash> contradictions_;
    std::unordered_map<NodeId, std::unordered_set<NodeId, NodeIdHash>, NodeIdHash> node_conflicts_;
    std::unordered_map<NodeId, Resolution, NodeIdHash> resolutions_;
};

} // namespace chitta
