#pragma once
// Epiplexity Self-Test: Validate Oracle compression quality
//
// Tests LLM reconstruction after compression.
// Tracks epsilon (ε) drift over time.
// Alerts when compression quality degrades.
//
// Epiplexity = how well the LLM can reconstruct full meaning from seeds.

#include "types.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cmath>

namespace chitta {

// Reconstruction test result
struct ReconstructionResult {
    NodeId id;
    std::string seed;             // Compressed seed
    std::string original;         // Original full content
    std::string reconstructed;    // LLM reconstruction
    float epsilon;                // Reconstruction quality (0-1)
    float semantic_similarity;    // Embedding similarity
    bool passed;                  // Above threshold
    std::string failure_reason;
};

// Epsilon measurement over time
struct EpsilonMeasurement {
    Timestamp timestamp;
    NodeId id;
    float epsilon;
    std::string seed_type;        // e.g., "wisdom", "pattern", "triplet"
};

// Epiplexity configuration
struct EpiplexityConfig {
    float pass_threshold = 0.7f;      // Minimum epsilon to pass
    float alert_threshold = 0.5f;     // Alert if below this
    float drift_threshold = 0.1f;     // Alert if epsilon drops by this much
    size_t sample_size = 10;          // Nodes to sample per test run
    uint64_t test_interval_ms = 86400000;  // 1 day between tests
};

// Epiplexity test harness
class EpiplexityTest {
public:
    // Reconstruction function type
    // Takes seed, returns reconstructed full content
    using ReconstructFn = std::function<std::string(const std::string& seed)>;

    // Similarity function type
    // Compares two texts, returns similarity (0-1)
    using SimilarityFn = std::function<float(const std::string& a, const std::string& b)>;

    explicit EpiplexityTest(EpiplexityConfig config = {})
        : config_(config) {}

    // Test a single node's compression quality
    ReconstructionResult test_node(
        const NodeId& id,
        const std::string& seed,
        const std::string& original,
        const ReconstructFn& reconstruct,
        const SimilarityFn& similarity,
        Timestamp now = 0) const
    {
        ReconstructionResult result;
        result.id = id;
        result.seed = seed;
        result.original = original;

        // Reconstruct from seed
        result.reconstructed = reconstruct(seed);

        // Calculate semantic similarity
        result.semantic_similarity = similarity(original, result.reconstructed);

        // Calculate epsilon (reconstruction quality)
        // Combines semantic similarity with length preservation
        float length_ratio = std::min(
            static_cast<float>(result.reconstructed.size()) / original.size(),
            static_cast<float>(original.size()) / result.reconstructed.size());
        length_ratio = std::max(0.5f, length_ratio);  // Don't penalize too much

        result.epsilon = result.semantic_similarity * length_ratio;

        // Check pass/fail
        result.passed = result.epsilon >= config_.pass_threshold;
        if (!result.passed) {
            result.failure_reason = "Epsilon " + std::to_string(result.epsilon) +
                " below threshold " + std::to_string(config_.pass_threshold);
        }

        // Record measurement
        if (now > 0) {
            EpsilonMeasurement m;
            m.timestamp = now;
            m.id = id;
            m.epsilon = result.epsilon;
            measurements_.push_back(m);
        }

        return result;
    }

    // Test a batch of nodes
    std::vector<ReconstructionResult> test_batch(
        const std::vector<std::tuple<NodeId, std::string, std::string>>& nodes,
        const ReconstructFn& reconstruct,
        const SimilarityFn& similarity,
        Timestamp now = 0) const
    {
        std::vector<ReconstructionResult> results;
        for (const auto& [id, seed, original] : nodes) {
            results.push_back(test_node(id, seed, original, reconstruct, similarity, now));
        }
        return results;
    }

    // Calculate aggregate statistics
    struct BatchStats {
        size_t total;
        size_t passed;
        size_t failed;
        size_t alerts;       // Below alert threshold
        float avg_epsilon;
        float min_epsilon;
        float max_epsilon;
    };

    BatchStats get_stats(const std::vector<ReconstructionResult>& results) const {
        BatchStats stats{};
        stats.total = results.size();
        if (results.empty()) return stats;

        stats.min_epsilon = 1.0f;
        stats.max_epsilon = 0.0f;

        for (const auto& r : results) {
            if (r.passed) stats.passed++;
            else stats.failed++;

            if (r.epsilon < config_.alert_threshold) stats.alerts++;

            stats.avg_epsilon += r.epsilon;
            stats.min_epsilon = std::min(stats.min_epsilon, r.epsilon);
            stats.max_epsilon = std::max(stats.max_epsilon, r.epsilon);
        }

        stats.avg_epsilon /= results.size();
        return stats;
    }

    // Check for epsilon drift
    struct DriftAnalysis {
        bool drift_detected;
        float current_avg;
        float previous_avg;
        float change;
        std::string message;
    };

    DriftAnalysis check_drift(Timestamp now, uint64_t lookback_ms = 604800000) const {
        DriftAnalysis analysis;
        analysis.drift_detected = false;

        // Split measurements into recent and previous periods
        Timestamp midpoint = now - lookback_ms;
        Timestamp start = now - lookback_ms * 2;

        std::vector<float> recent, previous;
        for (const auto& m : measurements_) {
            if (m.timestamp >= midpoint && m.timestamp <= now) {
                recent.push_back(m.epsilon);
            } else if (m.timestamp >= start && m.timestamp < midpoint) {
                previous.push_back(m.epsilon);
            }
        }

        if (recent.empty() || previous.empty()) {
            analysis.message = "Insufficient data for drift analysis";
            return analysis;
        }

        analysis.current_avg = 0.0f;
        for (float e : recent) analysis.current_avg += e;
        analysis.current_avg /= recent.size();

        analysis.previous_avg = 0.0f;
        for (float e : previous) analysis.previous_avg += e;
        analysis.previous_avg /= previous.size();

        analysis.change = analysis.current_avg - analysis.previous_avg;

        if (analysis.change < -config_.drift_threshold) {
            analysis.drift_detected = true;
            analysis.message = "Epsilon drift detected: " +
                std::to_string(analysis.previous_avg) + " -> " +
                std::to_string(analysis.current_avg);
        } else {
            analysis.message = "No significant drift";
        }

        return analysis;
    }

    // Get measurements for a node
    std::vector<EpsilonMeasurement> get_node_history(const NodeId& id) const {
        std::vector<EpsilonMeasurement> history;
        for (const auto& m : measurements_) {
            if (m.id == id) {
                history.push_back(m);
            }
        }
        return history;
    }

    // Clear old measurements
    void prune_measurements(Timestamp cutoff) {
        measurements_.erase(
            std::remove_if(measurements_.begin(), measurements_.end(),
                [cutoff](const EpsilonMeasurement& m) { return m.timestamp < cutoff; }),
            measurements_.end());
    }

    // Configuration
    const EpiplexityConfig& config() const { return config_; }
    void set_config(const EpiplexityConfig& c) { config_ = c; }

    // Statistics
    size_t measurement_count() const { return measurements_.size(); }

    // Persistence
    bool save(const std::string& path) const {
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) return false;

        uint32_t magic = 0x45504958;  // "EPIX"
        uint32_t version = 1;
        uint64_t count = measurements_.size();

        fwrite(&magic, sizeof(magic), 1, f);
        fwrite(&version, sizeof(version), 1, f);
        fwrite(&count, sizeof(count), 1, f);

        for (const auto& m : measurements_) {
            fwrite(&m.timestamp, sizeof(m.timestamp), 1, f);
            fwrite(&m.id.high, sizeof(m.id.high), 1, f);
            fwrite(&m.id.low, sizeof(m.id.low), 1, f);
            fwrite(&m.epsilon, sizeof(m.epsilon), 1, f);

            uint16_t type_len = static_cast<uint16_t>(m.seed_type.size());
            fwrite(&type_len, sizeof(type_len), 1, f);
            fwrite(m.seed_type.data(), 1, type_len, f);
        }

        fclose(f);
        return true;
    }

    bool load(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;

        uint32_t magic, version;
        uint64_t count;

        if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x45504958 ||
            fread(&version, sizeof(version), 1, f) != 1 || version != 1 ||
            fread(&count, sizeof(count), 1, f) != 1 || count > 10000000) {
            fclose(f);
            return false;
        }

        measurements_.clear();
        for (uint64_t i = 0; i < count; ++i) {
            EpsilonMeasurement m;

            if (fread(&m.timestamp, sizeof(m.timestamp), 1, f) != 1 ||
                fread(&m.id.high, sizeof(m.id.high), 1, f) != 1 ||
                fread(&m.id.low, sizeof(m.id.low), 1, f) != 1 ||
                fread(&m.epsilon, sizeof(m.epsilon), 1, f) != 1) {
                fclose(f);
                return false;
            }

            uint16_t type_len;
            if (fread(&type_len, sizeof(type_len), 1, f) != 1 || type_len > 1000) {
                fclose(f);
                return false;
            }
            m.seed_type.resize(type_len);
            if (fread(&m.seed_type[0], 1, type_len, f) != type_len) {
                fclose(f);
                return false;
            }

            measurements_.push_back(m);
        }

        fclose(f);
        return true;
    }

private:
    EpiplexityConfig config_;
    mutable std::vector<EpsilonMeasurement> measurements_;
};

} // namespace chitta
