#pragma once
// Feedback: the learning loop
//
// Memories that help should strengthen.
// Memories that mislead should weaken.
// The soul learns from outcomes.

#include "types.hpp"
#include <unordered_map>
#include <deque>
#include <mutex>

namespace synapse {

// Feedback types
enum class FeedbackType {
    Used,       // Memory was accessed/retrieved
    Helpful,    // Memory led to success
    Misleading, // Memory led to failure/correction
    Confirmed,  // External confirmation
    Challenged  // External challenge
};

// Single feedback event
struct FeedbackEvent {
    NodeId node_id;
    FeedbackType type;
    float magnitude;  // 0.0 - 1.0
    Timestamp timestamp;
    std::string context;
};

// Feedback configuration
struct FeedbackConfig {
    float used_delta = 0.01f;       // Small boost for access
    float helpful_delta = 0.1f;     // Significant boost for success
    float misleading_delta = -0.15f; // Penalty for misleading
    float confirmed_delta = 0.08f;  // Boost for confirmation
    float challenged_delta = -0.05f; // Small penalty for challenge

    size_t max_pending = 1000;      // Max pending feedback events
    int64_t batch_interval_ms = 5000; // Process batch every 5s
};

// Learning feedback tracker
class FeedbackTracker {
public:
    explicit FeedbackTracker(FeedbackConfig config = {})
        : config_(config) {}

    // Record feedback
    void record(NodeId id, FeedbackType type,
                float magnitude = 1.0f,
                const std::string& context = "") {
        std::lock_guard<std::mutex> lock(mutex_);

        FeedbackEvent event{
            id,
            type,
            std::clamp(magnitude, 0.0f, 1.0f),
            now(),
            context
        };

        pending_.push_back(event);

        // Track per-node stats
        auto& stats = node_stats_[id];
        stats.total_feedback++;
        switch (type) {
            case FeedbackType::Used:
                stats.access_count++;
                break;
            case FeedbackType::Helpful:
                stats.helpful_count++;
                break;
            case FeedbackType::Misleading:
                stats.misleading_count++;
                break;
            case FeedbackType::Confirmed:
                stats.confirmed_count++;
                break;
            case FeedbackType::Challenged:
                stats.challenged_count++;
                break;
        }

        // Limit pending queue
        while (pending_.size() > config_.max_pending) {
            pending_.pop_front();
        }
    }

    // Record that a memory was used (accessed)
    void used(NodeId id) {
        record(id, FeedbackType::Used);
    }

    // Record that a memory was helpful
    void helpful(NodeId id, const std::string& context = "") {
        record(id, FeedbackType::Helpful, 1.0f, context);
    }

    // Record that a memory was misleading
    void misleading(NodeId id, const std::string& context = "") {
        record(id, FeedbackType::Misleading, 1.0f, context);
    }

    // Record external confirmation
    void confirmed(NodeId id) {
        record(id, FeedbackType::Confirmed);
    }

    // Record external challenge
    void challenged(NodeId id) {
        record(id, FeedbackType::Challenged);
    }

    // Process pending feedback, returns deltas to apply
    std::vector<std::pair<NodeId, float>> process_pending() {
        std::lock_guard<std::mutex> lock(mutex_);

        std::unordered_map<NodeId, float, NodeIdHash> deltas;

        for (const auto& event : pending_) {
            float delta = 0.0f;
            switch (event.type) {
                case FeedbackType::Used:
                    delta = config_.used_delta;
                    break;
                case FeedbackType::Helpful:
                    delta = config_.helpful_delta;
                    break;
                case FeedbackType::Misleading:
                    delta = config_.misleading_delta;
                    break;
                case FeedbackType::Confirmed:
                    delta = config_.confirmed_delta;
                    break;
                case FeedbackType::Challenged:
                    delta = config_.challenged_delta;
                    break;
            }
            deltas[event.node_id] += delta * event.magnitude;
        }

        pending_.clear();

        std::vector<std::pair<NodeId, float>> result;
        for (const auto& [id, delta] : deltas) {
            result.emplace_back(id, delta);
        }
        return result;
    }

    // Get stats for a node
    struct NodeStats {
        size_t total_feedback = 0;
        size_t access_count = 0;
        size_t helpful_count = 0;
        size_t misleading_count = 0;
        size_t confirmed_count = 0;
        size_t challenged_count = 0;

        float helpfulness_ratio() const {
            size_t outcomes = helpful_count + misleading_count;
            if (outcomes == 0) return 0.5f;
            return static_cast<float>(helpful_count) / outcomes;
        }
    };

    std::optional<NodeStats> get_stats(NodeId id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = node_stats_.find(id);
        if (it != node_stats_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    size_t pending_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pending_.size();
    }

private:
    FeedbackConfig config_;
    mutable std::mutex mutex_;
    std::deque<FeedbackEvent> pending_;
    std::unordered_map<NodeId, NodeStats, NodeIdHash> node_stats_;
};

} // namespace synapse
