#pragma once
// ResonanceLearner: Self-tuning Bayesian bandit for memory retrieval
//
// Pure in-memory component — no database dependency.
// Uses Thompson sampling over Beta priors to optimize resonance parameters.
// Persists state via FieldStore domain events when available.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace chitta {

// Forward declaration for optional persistence
class FieldStore;

// ═══════════════════════════════════════════════════════════════════════════
// Resonance Configuration
// ═══════════════════════════════════════════════════════════════════════════

struct DuckDBResonanceConfig {
    // Spreading activation
    float spread_strength = 0.5f;
    float spread_decay = 0.5f;
    int max_hops = 3;
    float min_activation = 0.02f;

    // Hebbian learning
    float hebbian_strength = 0.03f;
    size_t hebbian_top_k = 5;

    // Attractor dynamics
    size_t max_attractors = 10;
    float attractor_min_connections = 3;
    float basin_boost = 1.15f;

    // Lateral inhibition (competition)
    bool enable_competition = true;
    float similarity_threshold = 0.85f;
    float inhibition_strength = 0.7f;

    // Epsilon (reconstructability) boost
    float epsilon_boost_alpha = 0.3f;

    // Relevance blend (semantic vs activation)
    float semantic_weight = 0.6f;
    float activation_weight = 0.4f;

    // Code intelligence integration
    float code_symbol_weight = 0.5f;
    size_t max_code_symbols = 5;

    // Surprise-gated encoding (Titans-style)
    bool enable_surprise_gating = true;
    float surprise_centroid_alpha = 0.1f;
    float surprise_low_threshold = 0.15f;
    float surprise_high_threshold = 0.5f;
    float surprise_confidence_boost = 0.3f;

    // Memory reconsolidation (Nader et al.)
    bool enable_reconsolidation = true;
    int64_t labile_window_ms = 1800000;
    float reconsolidation_threshold = 0.88f;
    size_t max_labile_tracked = 50;
};


// ═══════════════════════════════════════════════════════════════════════════
// Priors for Bayesian parameter estimation
// ═══════════════════════════════════════════════════════════════════════════

// Beta distribution prior for bounded parameters [0, 1]
struct BetaPrior {
    float alpha = 2.0f;
    float beta = 2.0f;

    float sample(std::mt19937& rng) const {
        if (alpha < 1.0f || beta < 1.0f) {
            return alpha / (alpha + beta);
        }
        std::gamma_distribution<float> gamma_a(alpha, 1.0f);
        std::gamma_distribution<float> gamma_b(beta, 1.0f);
        float x = gamma_a(rng);
        float y = gamma_b(rng);
        return x / (x + y);
    }

    float sample_range(std::mt19937& rng, float min, float max) const {
        return min + sample(rng) * (max - min);
    }

    void update(float reward) {
        if (reward > 0) {
            alpha += reward;
        } else {
            beta += (-reward);
        }
        constexpr float MAX_EVIDENCE = 100.0f;
        if (alpha + beta > MAX_EVIDENCE) {
            float scale = MAX_EVIDENCE / (alpha + beta);
            alpha *= scale;
            beta *= scale;
        }
    }

    float mean() const { return alpha / (alpha + beta); }

    float variance() const {
        float sum = alpha + beta;
        return (alpha * beta) / (sum * sum * (sum + 1));
    }
};

// Gaussian prior for unbounded parameters
struct GaussianPrior {
    float mu = 0.0f;
    float sigma = 1.0f;
    float n = 2.0f;

    float sample(std::mt19937& rng) const {
        std::normal_distribution<float> dist(mu, sigma);
        return dist(rng);
    }

    float sample_range(std::mt19937& rng, float min, float max) const {
        float raw = sample(rng);
        float sigmoid = 1.0f / (1.0f + std::exp(-raw));
        return min + sigmoid * (max - min);
    }

    void update(float observed_value, float reward) {
        float learning_rate = std::abs(reward) / (n + 1);
        if (reward > 0) {
            mu += learning_rate * (observed_value - mu);
        } else {
            mu -= learning_rate * (observed_value - mu) * 0.5f;
        }
        n += std::abs(reward);
        constexpr float MAX_N = 50.0f;
        if (n > MAX_N) n = MAX_N;
        sigma = std::max(0.1f, sigma * (1.0f - learning_rate * 0.1f));
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Query context for contextual bandit
// ═══════════════════════════════════════════════════════════════════════════

struct QueryContext {
    size_t query_length = 0;
    size_t term_count = 0;
    bool has_technical_terms = false;
    bool has_domain_prefix = false;
    float avg_term_frequency = 0.0f;

    std::vector<float> to_features() const {
        return {
            static_cast<float>(query_length) / 100.0f,
            static_cast<float>(term_count) / 10.0f,
            has_technical_terms ? 1.0f : 0.0f,
            has_domain_prefix ? 1.0f : 0.0f,
            avg_term_frequency
        };
    }
};

// Pending outcome for credit assignment
struct PendingResonanceOutcome {
    int64_t timestamp;
    std::vector<int64_t> result_ids;
    DuckDBResonanceConfig params_used;
    QueryContext context;
    std::unordered_map<int64_t, size_t> id_to_position;
};

// ═══════════════════════════════════════════════════════════════════════════
// ResonanceLearner: Bayesian bandit + contextual RL
// ═══════════════════════════════════════════════════════════════════════════

class ResonanceLearner {
public:
    ResonanceLearner() : rng_(std::random_device{}()) {}

    // Sample parameters using Thompson sampling
    DuckDBResonanceConfig sample_params(const QueryContext& context) {
        DuckDBResonanceConfig config;

        config.spread_strength = spread_strength_.sample_range(rng_, 0.2f, 0.8f);
        config.spread_decay = spread_decay_.sample_range(rng_, 0.3f, 0.7f);
        config.hebbian_strength = hebbian_strength_.sample_range(rng_, 0.01f, 0.1f);
        config.basin_boost = basin_boost_.sample_range(rng_, 1.0f, 1.5f);
        config.similarity_threshold = similarity_threshold_.sample_range(rng_, 0.7f, 0.95f);
        config.inhibition_strength = inhibition_strength_.sample_range(rng_, 0.3f, 0.9f);
        config.semantic_weight = semantic_weight_.sample_range(rng_, 0.4f, 0.8f);
        config.activation_weight = 1.0f - config.semantic_weight;

        if (context.has_technical_terms) {
            config.spread_strength *= 1.1f;
        }
        if (context.term_count > 5) {
            config.spread_decay *= 0.9f;
        }

        return config;
    }

    // Record a resonance call for later credit assignment
    void record_outcome(const std::vector<int64_t>& result_ids,
                        const DuckDBResonanceConfig& params,
                        const QueryContext& context) {
        PendingResonanceOutcome outcome;
        outcome.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        outcome.result_ids = result_ids;
        outcome.params_used = params;
        outcome.context = context;

        for (size_t i = 0; i < result_ids.size(); ++i) {
            outcome.id_to_position[result_ids[i]] = i;
        }

        pending_outcomes_.push_back(std::move(outcome));

        while (pending_outcomes_.size() > MAX_PENDING) {
            pending_outcomes_.pop_front();
        }
    }

    void on_strengthen(int64_t id, float amount = 1.0f) {
        process_feedback(id, amount);
    }

    void on_weaken(int64_t id, float amount = 1.0f) {
        process_feedback(id, -amount);
    }

    DuckDBResonanceConfig get_best_params() const {
        DuckDBResonanceConfig config;
        config.spread_strength = 0.2f + spread_strength_.mean() * 0.6f;
        config.spread_decay = 0.3f + spread_decay_.mean() * 0.4f;
        config.hebbian_strength = 0.01f + hebbian_strength_.mean() * 0.09f;
        config.basin_boost = 1.0f + basin_boost_.mean() * 0.5f;
        config.similarity_threshold = 0.7f + similarity_threshold_.mean() * 0.25f;
        config.inhibition_strength = 0.3f + inhibition_strength_.mean() * 0.6f;
        config.semantic_weight = 0.4f + semantic_weight_.mean() * 0.4f;
        config.activation_weight = 1.0f - config.semantic_weight;
        return config;
    }

    struct LearningStats {
        size_t total_feedback = 0;
        size_t positive_feedback = 0;
        size_t negative_feedback = 0;
        float avg_reward = 0.0f;
        std::map<std::string, float> param_means;
        std::map<std::string, float> param_uncertainties;
    };

    LearningStats get_stats() const {
        LearningStats stats;
        stats.total_feedback = total_feedback_;
        stats.positive_feedback = positive_feedback_;
        stats.negative_feedback = negative_feedback_;
        stats.avg_reward = total_feedback_ > 0 ?
            cumulative_reward_ / total_feedback_ : 0.0f;

        stats.param_means["spread_strength"] = spread_strength_.mean();
        stats.param_means["spread_decay"] = spread_decay_.mean();
        stats.param_means["hebbian_strength"] = hebbian_strength_.mean();
        stats.param_means["basin_boost"] = basin_boost_.mean();
        stats.param_means["semantic_weight"] = semantic_weight_.mean();

        stats.param_uncertainties["spread_strength"] = std::sqrt(spread_strength_.variance());
        stats.param_uncertainties["spread_decay"] = std::sqrt(spread_decay_.variance());
        stats.param_uncertainties["hebbian_strength"] = std::sqrt(hebbian_strength_.variance());

        return stats;
    }

    // Serialize/deserialize for persistence
    std::string serialize() const {
        std::ostringstream ss;
        ss << spread_strength_.alpha << " " << spread_strength_.beta << " ";
        ss << spread_decay_.alpha << " " << spread_decay_.beta << " ";
        ss << hebbian_strength_.alpha << " " << hebbian_strength_.beta << " ";
        ss << basin_boost_.alpha << " " << basin_boost_.beta << " ";
        ss << similarity_threshold_.alpha << " " << similarity_threshold_.beta << " ";
        ss << inhibition_strength_.alpha << " " << inhibition_strength_.beta << " ";
        ss << semantic_weight_.alpha << " " << semantic_weight_.beta << " ";
        ss << total_feedback_ << " " << positive_feedback_ << " ";
        ss << negative_feedback_ << " " << cumulative_reward_;
        return ss.str();
    }

    bool deserialize(const std::string& data) {
        std::istringstream ss(data);
        return static_cast<bool>(
            ss >> spread_strength_.alpha >> spread_strength_.beta
               >> spread_decay_.alpha >> spread_decay_.beta
               >> hebbian_strength_.alpha >> hebbian_strength_.beta
               >> basin_boost_.alpha >> basin_boost_.beta
               >> similarity_threshold_.alpha >> similarity_threshold_.beta
               >> inhibition_strength_.alpha >> inhibition_strength_.beta
               >> semantic_weight_.alpha >> semantic_weight_.beta
               >> total_feedback_ >> positive_feedback_
               >> negative_feedback_ >> cumulative_reward_
        );
    }

    // Persistence via FieldStore
    void persist(FieldStore* fs);
    void restore(FieldStore* fs);

private:
    static constexpr size_t MAX_PENDING = 100;

    BetaPrior spread_strength_;
    BetaPrior spread_decay_;
    BetaPrior hebbian_strength_;
    BetaPrior basin_boost_;
    BetaPrior similarity_threshold_;
    BetaPrior inhibition_strength_;
    BetaPrior semantic_weight_;

    std::deque<PendingResonanceOutcome> pending_outcomes_;

    size_t total_feedback_ = 0;
    size_t positive_feedback_ = 0;
    size_t negative_feedback_ = 0;
    float cumulative_reward_ = 0.0f;

    mutable std::mt19937 rng_;

    void process_feedback(int64_t id, float reward) {
        total_feedback_++;
        if (reward > 0) positive_feedback_++;
        else negative_feedback_++;
        cumulative_reward_ += reward;

        for (auto& outcome : pending_outcomes_) {
            auto it = outcome.id_to_position.find(id);
            if (it != outcome.id_to_position.end()) {
                size_t position = it->second;
                float position_weight = 1.0f / (1.0f + position * 0.2f);
                float weighted_reward = reward * position_weight;

                auto reinforce_update = [&](BetaPrior& prior, float sampled,
                                            float min_val, float max_val) {
                    float range = max_val - min_val;
                    float mean_mapped = min_val + prior.mean() * range;
                    float deviation = (range > 0.0f) ? (sampled - mean_mapped) / range : 0.0f;
                    float attributed_reward = weighted_reward * (1.0f + 0.5f * deviation);
                    prior.update(attributed_reward);
                };

                reinforce_update(spread_strength_,      outcome.params_used.spread_strength,      0.2f, 0.8f);
                reinforce_update(spread_decay_,         outcome.params_used.spread_decay,          0.3f, 0.7f);
                reinforce_update(hebbian_strength_,     outcome.params_used.hebbian_strength,      0.01f, 0.1f);
                reinforce_update(basin_boost_,          outcome.params_used.basin_boost,           1.0f, 1.5f);
                reinforce_update(similarity_threshold_, outcome.params_used.similarity_threshold,  0.7f, 0.95f);
                reinforce_update(inhibition_strength_,  outcome.params_used.inhibition_strength,   0.3f, 0.9f);
                reinforce_update(semantic_weight_,      outcome.params_used.semantic_weight,       0.4f, 0.8f);

                outcome.id_to_position.erase(it);
            }
        }
    }
};

// Inline implementations that depend on FieldStore (only when available)
#ifdef CHITTA_FIELD_AVAILABLE
#include "field_store.hpp"
inline void ResonanceLearner::persist(FieldStore* fs) {
    if (!fs) return;
    using namespace std::chrono;
    int64_t now_ms = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
    fs->user_model_upsert("learner_state_global", "resonance_state", serialize(), now_ms);
}

inline void ResonanceLearner::restore(FieldStore* fs) {
    if (!fs) return;
    auto payload = fs->get_latest_event("user_model", "resonance_state", "learner_state_global");
    if (payload) {
        deserialize(*payload);
    }
}
#else
inline void ResonanceLearner::persist(FieldStore*) {}
inline void ResonanceLearner::restore(FieldStore*) {}
#endif

} // namespace chitta
