#pragma once
// Utility-Calibrated Decay: Usage-driven memory retention
//
// Nodes that are frequently recalled decay slower (survival advantage).
// Nodes that receive positive feedback strengthen further.
// Replaces fixed delta with adaptive rates based on actual utility.
//
// Formula: effective_delta = base_delta * (1 / (1 + log(1 + recall_count)))
// More recalls = slower decay = longer retention

#include "types.hpp"
#include <unordered_map>
#include <cmath>
#include <algorithm>

namespace chitta {

// Usage statistics for a single node
struct UsageStats {
    uint32_t recall_count = 0;      // Times this node was retrieved
    uint32_t positive_feedback = 0; // Times marked helpful
    uint32_t negative_feedback = 0; // Times marked unhelpful
    Timestamp first_recall = 0;     // When first recalled
    Timestamp last_recall = 0;      // When last recalled
    float cumulative_relevance = 0; // Sum of relevance scores when recalled

    // MemRL-inspired utility tracking (Q-value analog)
    float utility = 0.5f;           // Learned effectiveness [0,1], starts neutral
    uint32_t outcome_count = 0;     // Number of task outcomes recorded

    // Net feedback score (-1 to +1)
    float feedback_score() const {
        uint32_t total = positive_feedback + negative_feedback;
        if (total == 0) return 0.0f;
        return static_cast<float>(positive_feedback - negative_feedback) / total;
    }

    // Average relevance when recalled
    float avg_relevance() const {
        if (recall_count == 0) return 0.0f;
        return cumulative_relevance / recall_count;
    }

    // Update utility with task outcome (Monte Carlo style from MemRL)
    // outcome: 0.0 = task failed, 1.0 = task succeeded
    void update_utility(float outcome, float learning_rate = 0.1f) {
        outcome_count++;
        // Exponential moving average: Q ← Q + α(outcome - Q)
        utility += learning_rate * (outcome - utility);
        utility = std::clamp(utility, 0.0f, 1.0f);
    }

    // Get utility score (neutral 0.5 if no outcomes recorded)
    float utility_score() const {
        if (outcome_count == 0) return 0.5f;
        return utility;
    }
};

// Decay configuration
struct DecayConfig {
    // Base decay rates by type
    float wisdom_base_delta = 0.02f;
    float belief_base_delta = 0.01f;
    float episode_base_delta = 0.10f;
    float signal_base_delta = 0.15f;
    float default_base_delta = 0.05f;

    // Utility modifiers
    float recall_decay_factor = 0.3f;     // How much recalls reduce decay (0-1)
    float feedback_decay_factor = 0.2f;   // How much positive feedback reduces decay
    float relevance_decay_factor = 0.1f;  // How much high relevance reduces decay

    // Minimum decay (never completely stop decaying)
    float min_delta = 0.001f;

    // Maximum decay boost from negative feedback
    float max_decay_multiplier = 2.0f;
};

// Utility-calibrated decay manager
class UtilityDecay {
public:
    explicit UtilityDecay(DecayConfig config = {}) : config_(config) {}

    // Record a recall event
    void record_recall(const NodeId& id, float relevance_score, Timestamp now) {
        auto& stats = usage_[id];
        stats.recall_count++;
        stats.cumulative_relevance += relevance_score;
        if (stats.first_recall == 0) {
            stats.first_recall = now;
        }
        stats.last_recall = now;
    }

    // Record feedback
    void record_feedback(const NodeId& id, bool positive) {
        auto& stats = usage_[id];
        if (positive) {
            stats.positive_feedback++;
        } else {
            stats.negative_feedback++;
        }
    }

    // Record task outcome (MemRL-inspired utility update)
    void record_outcome(const NodeId& id, float success, float learning_rate = 0.1f) {
        usage_[id].update_utility(success, learning_rate);
    }

    // Get utility score for a node (returns 0.5 if unknown)
    float get_utility(const NodeId& id) const {
        auto it = usage_.find(id);
        return (it != usage_.end()) ? it->second.utility_score() : 0.5f;
    }

    // Get usage stats for a node
    const UsageStats* get_stats(const NodeId& id) const {
        auto it = usage_.find(id);
        return (it != usage_.end()) ? &it->second : nullptr;
    }

    // Calculate effective decay rate for a node
    float effective_delta(const Node& node) const {
        float base = base_delta(node.node_type);

        auto it = usage_.find(node.id);
        if (it == usage_.end()) {
            return base;  // No usage data, use base rate
        }

        const auto& stats = it->second;

        // 1. Recall count modifier: more recalls = slower decay
        // Formula: 1 / (1 + factor * log(1 + count))
        float recall_modifier = 1.0f / (1.0f + config_.recall_decay_factor *
            std::log(1.0f + stats.recall_count));

        // 2. Feedback modifier: positive = slower, negative = faster
        float feedback = stats.feedback_score();
        float feedback_modifier = 1.0f - config_.feedback_decay_factor * feedback;

        // 3. Relevance modifier: high avg relevance = slower decay
        float relevance_modifier = 1.0f - config_.relevance_decay_factor *
            std::min(1.0f, stats.avg_relevance());

        // Combined modifier
        float modifier = recall_modifier * feedback_modifier * relevance_modifier;

        // Clamp to valid range
        modifier = std::clamp(modifier, 1.0f / config_.max_decay_multiplier,
                             config_.max_decay_multiplier);

        float effective = base * modifier;
        return std::max(effective, config_.min_delta);
    }

    // Update a node's delta based on utility (call periodically)
    float update_delta(Node& node) const {
        node.delta = effective_delta(node);
        return node.delta;
    }

    // Batch update all nodes' decay rates
    void update_all(std::vector<Node>& nodes) const {
        for (auto& node : nodes) {
            update_delta(node);
        }
    }

    // Get survival probability after time t
    // P(survive) = exp(-delta * t)
    static float survival_probability(float delta, float time_ms) {
        float time_days = time_ms / 86400000.0f;
        return std::exp(-delta * time_days);
    }

    // Expected lifetime (days until 50% survival)
    static float expected_lifetime(float delta) {
        if (delta <= 0) return std::numeric_limits<float>::infinity();
        return 0.693f / delta;  // ln(2) / delta
    }

    // Clear usage data for removed nodes
    void remove(const NodeId& id) {
        usage_.erase(id);
    }

    // Clear all usage data
    void clear() {
        usage_.clear();
    }

    // Statistics
    size_t tracked_nodes() const { return usage_.size(); }

    // Persistence (atomic: write temp → fsync → rename)
    // Version 2: Added utility and outcome_count fields
    bool save(const std::string& path) const {
        return safe_save(path, [this](FILE* f) {
            uint32_t magic = 0x55544443;  // "UTDC"
            uint32_t version = 2;         // v2: utility tracking
            uint64_t count = usage_.size();

            if (fwrite(&magic, sizeof(magic), 1, f) != 1) return false;
            if (fwrite(&version, sizeof(version), 1, f) != 1) return false;
            if (fwrite(&count, sizeof(count), 1, f) != 1) return false;

            for (const auto& [id, stats] : usage_) {
                if (fwrite(&id.high, sizeof(id.high), 1, f) != 1) return false;
                if (fwrite(&id.low, sizeof(id.low), 1, f) != 1) return false;
                if (fwrite(&stats.recall_count, sizeof(stats.recall_count), 1, f) != 1) return false;
                if (fwrite(&stats.positive_feedback, sizeof(stats.positive_feedback), 1, f) != 1) return false;
                if (fwrite(&stats.negative_feedback, sizeof(stats.negative_feedback), 1, f) != 1) return false;
                if (fwrite(&stats.first_recall, sizeof(stats.first_recall), 1, f) != 1) return false;
                if (fwrite(&stats.last_recall, sizeof(stats.last_recall), 1, f) != 1) return false;
                if (fwrite(&stats.cumulative_relevance, sizeof(stats.cumulative_relevance), 1, f) != 1) return false;
                // v2 fields
                if (fwrite(&stats.utility, sizeof(stats.utility), 1, f) != 1) return false;
                if (fwrite(&stats.outcome_count, sizeof(stats.outcome_count), 1, f) != 1) return false;
            }
            return true;
        });
    }

    bool load(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;

        uint32_t magic, version;
        uint64_t count;

        if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x55544443 ||
            fread(&version, sizeof(version), 1, f) != 1 || (version != 1 && version != 2) ||
            fread(&count, sizeof(count), 1, f) != 1 || count > 100000000) {
            fclose(f);
            return false;
        }

        usage_.clear();
        for (uint64_t i = 0; i < count; ++i) {
            NodeId id;
            UsageStats stats;

            if (fread(&id.high, sizeof(id.high), 1, f) != 1 ||
                fread(&id.low, sizeof(id.low), 1, f) != 1 ||
                fread(&stats.recall_count, sizeof(stats.recall_count), 1, f) != 1 ||
                fread(&stats.positive_feedback, sizeof(stats.positive_feedback), 1, f) != 1 ||
                fread(&stats.negative_feedback, sizeof(stats.negative_feedback), 1, f) != 1 ||
                fread(&stats.first_recall, sizeof(stats.first_recall), 1, f) != 1 ||
                fread(&stats.last_recall, sizeof(stats.last_recall), 1, f) != 1 ||
                fread(&stats.cumulative_relevance, sizeof(stats.cumulative_relevance), 1, f) != 1) {
                fclose(f);
                return false;
            }

            // v2 fields (default to neutral if v1)
            if (version >= 2) {
                if (fread(&stats.utility, sizeof(stats.utility), 1, f) != 1 ||
                    fread(&stats.outcome_count, sizeof(stats.outcome_count), 1, f) != 1) {
                    fclose(f);
                    return false;
                }
            }

            usage_[id] = stats;
        }

        fclose(f);
        return true;
    }

private:
    float base_delta(NodeType type) const {
        switch (type) {
            case NodeType::Wisdom:    return config_.wisdom_base_delta;
            case NodeType::Belief:    return config_.belief_base_delta;
            case NodeType::Invariant: return config_.belief_base_delta;
            case NodeType::Episode:   return config_.episode_base_delta;
            default:                  return config_.default_base_delta;
        }
    }

    DecayConfig config_;
    std::unordered_map<NodeId, UsageStats, NodeIdHash> usage_;
};

} // namespace chitta
