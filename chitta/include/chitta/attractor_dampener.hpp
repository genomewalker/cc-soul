#pragma once
// Attractor Dampener: Prevent confirmation bias
//
// Prevents over-retrieved nodes from dominating recall:
// - Limits Hebbian updates per query
// - Decay boost for over-retrieved nodes
// - Diversity injection in recall results
//
// Without this, popular nodes become attractors that suppress alternatives.

#include "types.hpp"
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cmath>
#include <random>

namespace chitta {

// Dampening configuration
struct DampenerConfig {
    // Hebbian limits
    uint32_t max_hebbian_updates_per_query = 5;  // Cap strengthening per query
    float hebbian_decay_per_update = 0.95f;      // Each update slightly weaker

    // Retrieval dampening
    uint32_t over_retrieval_threshold = 10;  // Retrievals in window to trigger
    float over_retrieval_penalty = 0.1f;     // Score penalty per threshold excess
    uint64_t retrieval_window_ms = 3600000;  // 1 hour window

    // Diversity injection
    float diversity_fraction = 0.2f;  // Fraction of results to diversify
    float min_diversity_distance = 0.3f;  // Minimum distance for diversity picks
};

// Retrieval history for a node
struct RetrievalHistory {
    std::vector<Timestamp> timestamps;  // Recent retrieval times
    uint32_t total_count = 0;           // All-time count
    float cumulative_score = 0.0f;      // Sum of retrieval scores

    // Count retrievals in time window
    uint32_t count_in_window(Timestamp now, uint64_t window_ms) const {
        uint32_t count = 0;
        Timestamp cutoff = now - window_ms;
        for (const auto& ts : timestamps) {
            if (ts >= cutoff) count++;
        }
        return count;
    }

    // Prune old timestamps
    void prune(Timestamp cutoff) {
        timestamps.erase(
            std::remove_if(timestamps.begin(), timestamps.end(),
                [cutoff](Timestamp ts) { return ts < cutoff; }),
            timestamps.end());
    }
};

// Attractor dampener
class AttractorDampener {
public:
    explicit AttractorDampener(DampenerConfig config = {})
        : config_(config), rng_(std::random_device{}()) {}

    // Record a retrieval
    void record_retrieval(const NodeId& id, float score, Timestamp now) {
        auto& hist = history_[id];
        hist.timestamps.push_back(now);
        hist.total_count++;
        hist.cumulative_score += score;

        // Prune old entries periodically
        if (hist.timestamps.size() > 100) {
            hist.prune(now - config_.retrieval_window_ms * 2);
        }
    }

    // Calculate dampening factor for a node (0-1, lower = more dampened)
    float dampening_factor(const NodeId& id, Timestamp now) const {
        auto it = history_.find(id);
        if (it == history_.end()) return 1.0f;

        uint32_t recent = it->second.count_in_window(now, config_.retrieval_window_ms);
        if (recent <= config_.over_retrieval_threshold) return 1.0f;

        // Exponential dampening for over-retrieved nodes
        uint32_t excess = recent - config_.over_retrieval_threshold;
        float penalty = config_.over_retrieval_penalty * excess;
        return std::max(0.1f, 1.0f - penalty);
    }

    // Apply dampening to recall results
    std::vector<std::pair<NodeId, float>> dampen_results(
        const std::vector<std::pair<NodeId, float>>& results,
        Timestamp now) const
    {
        std::vector<std::pair<NodeId, float>> dampened;
        dampened.reserve(results.size());

        for (const auto& [id, score] : results) {
            float factor = dampening_factor(id, now);
            dampened.push_back({id, score * factor});
        }

        // Re-sort by dampened score
        std::sort(dampened.begin(), dampened.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        return dampened;
    }

    // Get Hebbian update factor (diminishing returns within query)
    float hebbian_factor(uint32_t update_index) const {
        if (update_index >= config_.max_hebbian_updates_per_query) {
            return 0.0f;  // No more updates allowed
        }
        return std::pow(config_.hebbian_decay_per_update, update_index);
    }

    // Inject diversity into results
    // Replaces some similar results with more diverse alternatives
    std::vector<std::pair<NodeId, float>> inject_diversity(
        const std::vector<std::pair<NodeId, float>>& results,
        const std::vector<std::pair<NodeId, float>>& alternatives,
        const std::function<float(const NodeId&, const NodeId&)>& similarity_fn) const
    {
        if (results.empty() || alternatives.empty()) return results;

        size_t diversity_count = static_cast<size_t>(
            results.size() * config_.diversity_fraction);
        if (diversity_count == 0) return results;

        std::vector<std::pair<NodeId, float>> diversified = results;

        // Find positions to replace (skip top results)
        size_t start_pos = results.size() / 2;
        size_t replaced = 0;

        for (const auto& [alt_id, alt_score] : alternatives) {
            if (replaced >= diversity_count) break;

            // Check if alternative is sufficiently different from all current results
            bool is_diverse = true;
            for (const auto& [res_id, _] : diversified) {
                if (similarity_fn(alt_id, res_id) > (1.0f - config_.min_diversity_distance)) {
                    is_diverse = false;
                    break;
                }
            }

            if (is_diverse && start_pos + replaced < diversified.size()) {
                diversified[start_pos + replaced] = {alt_id, alt_score * 0.9f};
                replaced++;
            }
        }

        // Re-sort
        std::sort(diversified.begin(), diversified.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        return diversified;
    }

    // Get retrieval statistics for a node
    const RetrievalHistory* get_history(const NodeId& id) const {
        auto it = history_.find(id);
        return (it != history_.end()) ? &it->second : nullptr;
    }

    // Identify potential attractors (over-retrieved nodes)
    std::vector<NodeId> get_attractors(Timestamp now, uint32_t threshold = 0) const {
        if (threshold == 0) threshold = config_.over_retrieval_threshold * 2;

        std::vector<NodeId> attractors;
        for (const auto& [id, hist] : history_) {
            if (hist.count_in_window(now, config_.retrieval_window_ms) >= threshold) {
                attractors.push_back(id);
            }
        }
        return attractors;
    }

    // Remove history for deleted node
    void remove(const NodeId& id) {
        history_.erase(id);
    }

    // Prune old history entries
    void prune_all(Timestamp now) {
        Timestamp cutoff = now - config_.retrieval_window_ms * 2;
        for (auto& [_, hist] : history_) {
            hist.prune(cutoff);
        }
    }

    // Statistics
    size_t tracked_count() const { return history_.size(); }

    // Configuration
    const DampenerConfig& config() const { return config_; }
    void set_config(const DampenerConfig& c) { config_ = c; }

    // Persistence
    bool save(const std::string& path) const {
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) return false;

        uint32_t magic = 0x41545244;  // "ATRD"
        uint32_t version = 1;
        uint64_t count = history_.size();

        fwrite(&magic, sizeof(magic), 1, f);
        fwrite(&version, sizeof(version), 1, f);
        fwrite(&count, sizeof(count), 1, f);

        for (const auto& [id, hist] : history_) {
            fwrite(&id.high, sizeof(id.high), 1, f);
            fwrite(&id.low, sizeof(id.low), 1, f);
            fwrite(&hist.total_count, sizeof(hist.total_count), 1, f);
            fwrite(&hist.cumulative_score, sizeof(hist.cumulative_score), 1, f);

            // Save recent timestamps (limit to 100 most recent)
            size_t ts_count = std::min(hist.timestamps.size(), size_t(100));
            uint16_t ts_count_16 = static_cast<uint16_t>(ts_count);
            fwrite(&ts_count_16, sizeof(ts_count_16), 1, f);

            // Write most recent timestamps
            size_t start = hist.timestamps.size() > 100 ? hist.timestamps.size() - 100 : 0;
            for (size_t i = start; i < hist.timestamps.size(); ++i) {
                fwrite(&hist.timestamps[i], sizeof(Timestamp), 1, f);
            }
        }

        fclose(f);
        return true;
    }

    bool load(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;

        uint32_t magic, version;
        uint64_t count;

        if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x41545244 ||
            fread(&version, sizeof(version), 1, f) != 1 || version != 1 ||
            fread(&count, sizeof(count), 1, f) != 1 || count > 100000000) {
            fclose(f);
            return false;
        }

        history_.clear();
        for (uint64_t i = 0; i < count; ++i) {
            NodeId id;
            RetrievalHistory hist;

            if (fread(&id.high, sizeof(id.high), 1, f) != 1 ||
                fread(&id.low, sizeof(id.low), 1, f) != 1 ||
                fread(&hist.total_count, sizeof(hist.total_count), 1, f) != 1 ||
                fread(&hist.cumulative_score, sizeof(hist.cumulative_score), 1, f) != 1) {
                fclose(f);
                return false;
            }

            uint16_t ts_count;
            if (fread(&ts_count, sizeof(ts_count), 1, f) != 1 || ts_count > 100) {
                fclose(f);
                return false;
            }

            hist.timestamps.resize(ts_count);
            for (uint16_t j = 0; j < ts_count; ++j) {
                if (fread(&hist.timestamps[j], sizeof(Timestamp), 1, f) != 1) {
                    fclose(f);
                    return false;
                }
            }

            history_[id] = hist;
        }

        fclose(f);
        return true;
    }

private:
    DampenerConfig config_;
    std::unordered_map<NodeId, RetrievalHistory, NodeIdHash> history_;
    mutable std::mt19937 rng_;
};

} // namespace chitta
