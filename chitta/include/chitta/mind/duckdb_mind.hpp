#pragma once
// DuckDBMind: Full resonance mind backed by DuckDB
//
// Same interface as Mind but uses DuckDB for durable storage.
// Benefits: ACID transactions, crash recovery, vector search, graph queries.
//
// Resonance Architecture:
// 1. Session Priming: Recent observations bias future recall
// 2. Spreading Activation: Activation flows through triplet graph
// 3. Attractor Dynamics: Results pulled toward conceptual gravity wells
// 4. Lateral Inhibition: Similar patterns compete
// 5. Hebbian Learning: Co-activated nodes strengthen connections

#include "../duckdb_store.hpp"
#include "../theme_manager.hpp"
#include "../narrative.hpp"
#include "../anticipator.hpp"
#include "embedder.hpp"
#include "types.hpp"
#include "../types.hpp"
#include "../rpc/types.hpp"
#ifdef CHITTA_WITH_ONNX
#include "../vak_onnx.hpp"
#endif
#include <memory>
#include <shared_mutex>
#include <optional>
#include <queue>
#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <chrono>
#include <map>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <random>
#include <filesystem>
#include <set>
#include <cmath>

namespace chitta {

// ═══════════════════════════════════════════════════════════════════════════
// Resonance Configuration
// ═══════════════════════════════════════════════════════════════════════════

struct DuckDBResonanceConfig {
    // Spreading activation
    float spread_strength = 0.5f;       // Initial activation strength
    float spread_decay = 0.5f;          // Decay per hop
    int max_hops = 3;                   // Maximum graph traversal depth
    float min_activation = 0.02f;       // Minimum activation to propagate

    // Hebbian learning
    float hebbian_strength = 0.03f;     // Connection strengthening per co-activation
    size_t hebbian_top_k = 5;           // Top-k results participate in Hebbian update

    // Attractor dynamics
    size_t max_attractors = 10;         // Maximum attractors to track
    float attractor_min_connections = 3; // Minimum triplet connections to be attractor
    float basin_boost = 1.15f;          // Relevance boost for same-basin results

    // Lateral inhibition (competition)
    bool enable_competition = true;
    float similarity_threshold = 0.85f; // Nodes more similar than this compete
    float inhibition_strength = 0.7f;   // How strongly winners suppress losers

    // Epsilon (reconstructability) boost
    float epsilon_boost_alpha = 0.3f;   // How much ε boosts relevance

    // Relevance blend (semantic vs activation)
    float semantic_weight = 0.6f;       // Weight for semantic similarity
    float activation_weight = 0.4f;     // Weight for spreading activation

    // Code intelligence integration
    float code_symbol_weight = 0.5f;    // Relevance weight for matching code symbols
    size_t max_code_symbols = 5;        // Maximum code symbols to include in results
};

// ═══════════════════════════════════════════════════════════════════════════
// Self-Tuning: Bayesian Bandit + Contextual RL
// ═══════════════════════════════════════════════════════════════════════════

// Beta distribution prior for bounded parameters [0, 1]
struct BetaPrior {
    float alpha = 2.0f;  // Successes + prior (start with weak prior)
    float beta = 2.0f;   // Failures + prior

    // Thompson sampling: sample from Beta(alpha, beta)
    // Uses approximation: Beta ~ Normal for large alpha, beta
    float sample(std::mt19937& rng) const {
        if (alpha < 1.0f || beta < 1.0f) {
            // Fallback for degenerate cases
            return alpha / (alpha + beta);
        }
        // Gamma-based exact sampling
        std::gamma_distribution<float> gamma_a(alpha, 1.0f);
        std::gamma_distribution<float> gamma_b(beta, 1.0f);
        float x = gamma_a(rng);
        float y = gamma_b(rng);
        return x / (x + y);
    }

    // Map sampled value [0,1] to parameter range [min, max]
    float sample_range(std::mt19937& rng, float min, float max) const {
        return min + sample(rng) * (max - min);
    }

    // Bayesian update with reward in [-1, 1]
    void update(float reward) {
        if (reward > 0) {
            alpha += reward;
        } else {
            beta += (-reward);
        }
        // Prevent priors from growing unboundedly (soft cap)
        constexpr float MAX_EVIDENCE = 100.0f;
        if (alpha + beta > MAX_EVIDENCE) {
            float scale = MAX_EVIDENCE / (alpha + beta);
            alpha *= scale;
            beta *= scale;
        }
    }

    // Mean of the distribution (exploitation value)
    float mean() const { return alpha / (alpha + beta); }

    // Variance (uncertainty)
    float variance() const {
        float sum = alpha + beta;
        return (alpha * beta) / (sum * sum * (sum + 1));
    }
};

// Gaussian prior for unbounded parameters
struct GaussianPrior {
    float mu = 0.0f;      // Mean
    float sigma = 1.0f;   // Standard deviation
    float n = 2.0f;       // Effective sample count

    float sample(std::mt19937& rng) const {
        std::normal_distribution<float> dist(mu, sigma);
        return dist(rng);
    }

    float sample_range(std::mt19937& rng, float min, float max) const {
        float raw = sample(rng);
        // Sigmoid mapping to [min, max]
        float sigmoid = 1.0f / (1.0f + std::exp(-raw));
        return min + sigmoid * (max - min);
    }

    // Bayesian update with observed value and reward
    void update(float observed_value, float reward) {
        // Reward-weighted update: move toward observed if reward > 0
        float learning_rate = std::abs(reward) / (n + 1);
        if (reward > 0) {
            mu += learning_rate * (observed_value - mu);
        } else {
            mu -= learning_rate * (observed_value - mu) * 0.5f;  // Move away slower
        }
        n += std::abs(reward);
        // Reduce variance as we learn
        sigma = std::max(0.1f, sigma * (1.0f - learning_rate * 0.1f));
    }
};

// Query context features for contextual bandit
struct QueryContext {
    size_t query_length = 0;
    size_t term_count = 0;
    bool has_technical_terms = false;
    bool has_domain_prefix = false;  // e.g., [cc-soul], [biology]
    float avg_term_frequency = 0.0f; // How common are query terms in triplets

    // Simple feature vector for context-dependent learning
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

    // Track position of each result for position-weighted credit
    std::unordered_map<int64_t, size_t> id_to_position;
};

// The learner: combines Bayesian bandits with contextual RL
class ResonanceLearner {
public:
    ResonanceLearner() : rng_(std::random_device{}()) {}

    // Sample parameters using Thompson sampling
    DuckDBResonanceConfig sample_params(const QueryContext& context) {
        DuckDBResonanceConfig config;

        // Sample each parameter from its posterior
        config.spread_strength = spread_strength_.sample_range(rng_, 0.2f, 0.8f);
        config.spread_decay = spread_decay_.sample_range(rng_, 0.3f, 0.7f);
        config.hebbian_strength = hebbian_strength_.sample_range(rng_, 0.01f, 0.1f);
        config.basin_boost = basin_boost_.sample_range(rng_, 1.0f, 1.5f);
        config.similarity_threshold = similarity_threshold_.sample_range(rng_, 0.7f, 0.95f);
        config.inhibition_strength = inhibition_strength_.sample_range(rng_, 0.3f, 0.9f);
        config.semantic_weight = semantic_weight_.sample_range(rng_, 0.4f, 0.8f);
        config.activation_weight = 1.0f - config.semantic_weight;

        // Context-dependent adjustments (simple linear policy)
        if (context.has_technical_terms) {
            // Technical queries benefit from stronger graph activation
            config.spread_strength *= 1.1f;
        }
        if (context.term_count > 5) {
            // Long queries: reduce spread to avoid noise
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

        // Keep only recent outcomes (credit assignment window)
        while (pending_outcomes_.size() > MAX_PENDING) {
            pending_outcomes_.pop_front();
        }
    }

    // Process positive feedback (strengthen)
    void on_strengthen(int64_t id, float amount = 1.0f) {
        process_feedback(id, amount);
    }

    // Process negative feedback (weaken)
    void on_weaken(int64_t id, float amount = 1.0f) {
        process_feedback(id, -amount);
    }

    // Get current best parameters (exploitation mode)
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

    // Get learning statistics
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

private:
    static constexpr size_t MAX_PENDING = 100;

    // Parameter priors (Beta distributions)
    BetaPrior spread_strength_;
    BetaPrior spread_decay_;
    BetaPrior hebbian_strength_;
    BetaPrior basin_boost_;
    BetaPrior similarity_threshold_;
    BetaPrior inhibition_strength_;
    BetaPrior semantic_weight_;

    // Pending outcomes for credit assignment
    std::deque<PendingResonanceOutcome> pending_outcomes_;

    // Statistics
    size_t total_feedback_ = 0;
    size_t positive_feedback_ = 0;
    size_t negative_feedback_ = 0;
    float cumulative_reward_ = 0.0f;

    // RNG for Thompson sampling
    mutable std::mt19937 rng_;

    void process_feedback(int64_t id, float reward) {
        total_feedback_++;
        if (reward > 0) positive_feedback_++;
        else negative_feedback_++;
        cumulative_reward_ += reward;

        // Find which resonance call(s) returned this id
        for (auto& outcome : pending_outcomes_) {
            auto it = outcome.id_to_position.find(id);
            if (it != outcome.id_to_position.end()) {
                // Found! Apply credit assignment
                size_t position = it->second;

                // Position-weighted reward: top results get more credit/blame
                float position_weight = 1.0f / (1.0f + position * 0.2f);
                float weighted_reward = reward * position_weight;

                // Update all parameter priors
                spread_strength_.update(weighted_reward);
                spread_decay_.update(weighted_reward);
                hebbian_strength_.update(weighted_reward);
                basin_boost_.update(weighted_reward);
                similarity_threshold_.update(weighted_reward);
                inhibition_strength_.update(weighted_reward);
                semantic_weight_.update(weighted_reward);

                // Remove this id from the outcome (don't double-count)
                outcome.id_to_position.erase(it);
            }
        }
    }
};

// Session context for priming
struct DuckDBSessionContext {
    // Maintains insertion order via deque + O(1) lookup via set.
    // Evicts oldest (FIFO) when full, unlike unordered_set which removes
    // an arbitrary element.
    std::deque<int64_t> recent_observations_order;          // Insertion order
    std::unordered_set<int64_t> recent_observations_set;    // O(1) lookup
    std::unordered_set<std::string> active_topics;          // Current topic strings
    size_t max_observations = 50;                           // Limit recent observations

    float priming_boost = 0.3f;   // Boost for recently observed
    float topic_boost = 0.2f;     // Boost for topic-related

    void observe(int64_t id) {
        if (recent_observations_set.count(id)) return;  // Already tracked
        recent_observations_set.insert(id);
        recent_observations_order.push_back(id);
        while (recent_observations_order.size() > max_observations) {
            int64_t oldest = recent_observations_order.front();
            recent_observations_order.pop_front();
            recent_observations_set.erase(oldest);
        }
    }

    // O(1) membership check
    bool has_observation(int64_t id) const {
        return recent_observations_set.count(id) > 0;
    }

    void add_topic(const std::string& topic) {
        active_topics.insert(topic);
    }

    void clear() {
        recent_observations_order.clear();
        recent_observations_set.clear();
        active_topics.clear();
    }
};

// Attractor: conceptual gravity well
struct DuckDBAttractor {
    std::string entity;      // The triplet entity (subject or object)
    float strength;          // Attractor strength
    size_t connections;      // Number of triplet connections
};

struct DuckDBMindConfig {
    std::string path;

    // Quality gate
    bool enable_quality_gate = true;
    size_t min_content_length = 10;
    float min_signal_ratio = 0.3f;

    // Deduplication
    bool enable_deduplication = true;
    float dedup_threshold = 0.95f;

    // Decay and pruning
    float prune_threshold = 0.1f;
    float prune_min_age_days = 7.0f;
    float reinforce_amount = 0.15f;  // 3x previous - recalls should matter
};

// Health status compatible with SimpleMind
struct DuckDBHealth {
    size_t total_nodes = 0;
    size_t active_nodes = 0;
    size_t stale_nodes = 0;
    size_t weak_nodes = 0;
    float avg_confidence = 0.0f;

    std::string status() const {
        if (total_nodes == 0) return "empty";
        if (active_nodes < total_nodes / 2) return "degraded";
        if (total_nodes < 10) return "sparse";
        return "healthy";
    }
};

// Epiplexity: measures reconstruction quality from compressed seeds
// ε = (S · K · D · C)^0.25 where each component ∈ [0,1]
struct Epiplexity {
    float semantic_fidelity = 0.0f;     // S: embedding similarity between original and reconstructed
    float entity_preservation = 0.0f;   // K: key concepts retained (F1 score)
    float information_density = 0.0f;   // D: concepts per token (sigmoid normalized)
    float compression_utility = 0.0f;   // C: compression ratio benefit (saturating)
    float score = 0.0f;                 // Combined ε (geometric mean)

    // Compute combined score
    void compute_score() {
        // Geometric mean - if any component is 0, ε collapses
        score = std::pow(semantic_fidelity * entity_preservation *
                        information_density * compression_utility, 0.25f);
    }

    std::string to_string() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "ε=" << score << " (S=" << semantic_fidelity
           << " K=" << entity_preservation << " D=" << information_density
           << " C=" << compression_utility << ")";
        return ss.str();
    }
};

class DuckDBMind {
public:
    explicit DuckDBMind(DuckDBMindConfig config)
        : config_(std::move(config))
        , running_(false) {}

    explicit DuckDBMind(DuckDBMindConfig config, DuckDBResonanceConfig resonance_config)
        : config_(std::move(config))
        , resonance_config_(std::move(resonance_config))
        , running_(false) {}

    ~DuckDBMind() {
        if (running_) close();
    }

    // Lifecycle
    bool open() {
        std::unique_lock lock(mutex_);
        // Path convention: if path is a directory, use chitta.duckdb inside it
        std::string db_path = config_.path;
        if (std::filesystem::is_directory(config_.path)) {
            db_path = config_.path + "/chitta.duckdb";
        } else if (!config_.path.ends_with(".duckdb")) {
            db_path = config_.path + ".duckdb";
        }
        if (!store_.open(db_path)) {
            return false;
        }
        running_ = true;
        // Auto-load learner state on startup
        load_learner_state_unlocked();
        // Initialize theme manager
        theme_manager_ = std::make_unique<ThemeManager>(&store_, &embedder_, theme_config_);
        // Initialize narrative engine and anticipator
        narrative_engine_ = std::make_unique<NarrativeEngine>(store_);
        anticipator_ = std::make_unique<Anticipator>(&store_, narrative_engine_.get());
        return true;
    }

    void close() {
        std::unique_lock lock(mutex_);
        if (!running_) return;
        // Auto-save learner state on shutdown
        save_learner_state_unlocked();
        store_.close();
        running_ = false;
    }

    void sync() {
        // DuckDB handles sync internally via WAL
    }

    // Embedder
    void attach_yantra(std::shared_ptr<VakYantra> yantra) {
        embedder_.attach(std::move(yantra));
    }

    bool has_yantra() const {
        return embedder_.ready();
    }

    // Get the underlying yantra for status queries
    std::shared_ptr<VakYantra> embedder_yantra() const {
        return embedder_.yantra();
    }

    // Public accessors for diagnostics
    bool embedder_ready() const {
        return embedder_.ready();
    }

    bool passes_quality_gate_public(const std::string& text) const {
        return passes_quality_gate(text);
    }

    // Remember - store with embedding
    NodeId remember(const std::string& text, NodeType type = NodeType::Wisdom,
                    const std::string& realm = "brahman",
                    RealmVisibility visibility = RealmVisibility::Private,
                    float confidence = 0.8f) {
        std::unique_lock lock(mutex_);

        if (!passes_quality_gate(text)) {
            return NodeId{};
        }

        if (!embedder_.ready()) {
            return NodeId{};
        }

        Artha artha = embedder_.transform(text);

        // Deduplication check
        if (config_.enable_deduplication) {
            auto existing = find_duplicate(artha.nu, text);
            if (existing) {
                store_.strengthen(*existing, config_.reinforce_amount);
                store_.touch(*existing);
                return int64_to_nodeid(*existing);
            }
        }

        std::string kind = node_type_to_string(type);
        float decay_rate = default_decay_rate(type);

        int64_t id = store_.remember(text, kind, artha.nu.data, confidence, decay_rate, realm, visibility);
        if (id < 0) {
            return NodeId{};
        }

        return int64_to_nodeid(id);
    }

    NodeId remember(const std::string& text, NodeType type, const std::vector<std::string>& tags) {
        auto id = remember(text, type, "brahman", RealmVisibility::Private);
        if (id.low != 0) {
            for (const auto& tag : tags) {
                store_.add_tag(nodeid_to_int64(id), tag);
            }
        }
        return id;
    }

    // Recall - hybrid search with semantic + BM25 + tag matching
    // Formula: relevance = (w_sem * similarity + w_bm25 * bm25 + tag_boost) * (0.5 + 0.5 * confidence)
    //
    // Lock strategy: shared_lock for read phases (embedding, DB query, scoring),
    // then upgrade to unique_lock only for write operations (touch/strengthen).
    std::vector<Recall> recall(const std::string& query, size_t limit = 10) {
        std::vector<Recall> recalls;

        {
            // Read phase: shared lock allows concurrent reads
            std::shared_lock lock(mutex_);

            if (!embedder_.ready()) {
                return {};
            }

            // Hybrid scoring weights
            constexpr float w_sem = 0.6f;
            constexpr float w_bm25 = 0.4f;
            constexpr float tag_boost = 0.05f;

            // Phase 1: Semantic search (query mode — adds instruction prefix for BGE)
            Artha artha = embedder_.transform_query(query);
            auto sem_results = store_.recall(artha.nu.data, limit * 2);

            // Phase 2: BM25 keyword search (if FTS available)
            std::unordered_map<int64_t, float> bm25_scores;
            if (store_.has_fts() && !query.empty()) {
                auto bm25_results = store_.bm25_search_memory(query, limit * 3);
                // Normalize BM25 scores to [0,1]
                float bm25_max = 0.0f;
                for (const auto& [id, score] : bm25_results) {
                    bm25_max = std::max(bm25_max, score);
                }
                if (bm25_max > 0.0f) {
                    for (const auto& [id, score] : bm25_results) {
                        bm25_scores[id] = score / bm25_max;
                    }
                }
            }

            // Phase 3: Tag hits
            auto query_terms = extract_terms_unlocked(query);
            auto tag_hit_ids = store_.tag_hits(query_terms);

            // Phase 4: Merge scores
            std::unordered_map<int64_t, Recall> merged;
            for (const auto& r : sem_results) {
                Recall recall;
                recall.id = int64_to_nodeid(r.id);
                recall.text = r.content;
                recall.similarity = r.similarity;
                recall.type = string_to_node_type(r.kind);

                // Hybrid relevance formula
                float sem_score = r.similarity;
                float bm25_score = bm25_scores.count(r.id) ? bm25_scores[r.id] : 0.0f;
                float tag_bonus = tag_hit_ids.count(r.id) ? tag_boost : 0.0f;
                float confidence_factor = 0.5f + 0.5f * r.confidence;

                recall.relevance = (w_sem * sem_score + w_bm25 * bm25_score + tag_bonus) * confidence_factor;
                merged[r.id] = recall;
            }

            // Add BM25-only results (may have low semantic similarity but high keyword match)
            for (const auto& [id, bm25_score] : bm25_scores) {
                if (merged.count(id) == 0) {
                    auto mem = store_.get_memory(id);
                    if (mem) {
                        Recall recall;
                        recall.id = int64_to_nodeid(id);
                        recall.text = mem->content;
                        recall.similarity = 0.0f;  // No semantic match
                        recall.type = string_to_node_type(mem->kind);

                        float tag_bonus = tag_hit_ids.count(id) ? tag_boost : 0.0f;
                        float confidence_factor = 0.5f + 0.5f * mem->confidence;
                        recall.relevance = (w_bm25 * bm25_score + tag_bonus) * confidence_factor;
                        merged[id] = recall;
                    }
                }
            }

            // Convert to vector and sort by relevance
            recalls.reserve(merged.size());
            for (auto& [id, recall] : merged) {
                recalls.push_back(std::move(recall));
            }
            std::sort(recalls.begin(), recalls.end(),
                      [](const Recall& a, const Recall& b) { return a.relevance > b.relevance; });

            if (recalls.size() > limit) {
                recalls.resize(limit);
            }
        }  // shared_lock released

        // Write phase: unique lock for reinforcement (touch/strengthen)
        {
            std::unique_lock lock(mutex_);
            for (const auto& recall : recalls) {
                int64_t id = nodeid_to_int64(recall.id);
                store_.touch(id);
                store_.strengthen(id, config_.reinforce_amount);
            }
        }

        return recalls;
    }

    // Update confidence (with self-tuning feedback)
    bool strengthen(NodeId id, float amount = 0.1f) {
        std::unique_lock lock(mutex_);
        bool success = store_.strengthen(nodeid_to_int64(id), amount);
        if (success && enable_learning_) {
            // Positive feedback to learner for credit assignment
            learner_.on_strengthen(nodeid_to_int64(id), amount);
        }
        return success;
    }

    bool weaken(NodeId id, float amount = 0.1f) {
        std::unique_lock lock(mutex_);
        bool success = store_.weaken(nodeid_to_int64(id), amount);
        if (success && enable_learning_) {
            // Negative feedback to learner for credit assignment
            learner_.on_weaken(nodeid_to_int64(id), amount);
        }
        return success;
    }

    // Remove
    bool remove(NodeId id) {
        std::unique_lock lock(mutex_);
        return store_.forget(nodeid_to_int64(id));
    }

    // Living memory operations
    // IMPORTANT: DuckDB Connection is NOT thread-safe - must hold mutex_
    // MVCC handles transaction isolation, NOT connection thread-safety
    size_t tick() {
        std::unique_lock lock(mutex_);
        size_t decayed = store_.apply_decay();
        store_.prune(config_.prune_threshold, config_.prune_min_age_days);
        return decayed;
    }

    // Auto-distill repeated episode patterns into wisdom
    // Returns number of wisdom nodes created
    size_t auto_distill_episodes(size_t max_distillations = 5,
                                  float similarity_threshold = 0.85f,
                                  size_t min_occurrences = 3) {
        std::unique_lock lock(mutex_);

        if (!embedder_.ready()) {
            return 0;
        }

        auto candidates = store_.find_distill_candidates(
            similarity_threshold, min_occurrences, max_distillations);

        size_t created = 0;
        for (const auto& c : candidates) {
            if (c.episode_ids.empty()) continue;

            // Create wisdom node with distilled content
            // Format: "[distilled] Pattern from N episodes: <content>"
            std::ostringstream wisdom_content;
            wisdom_content << "[distilled] Pattern from " << c.episode_ids.size()
                          << " episodes (avg sim=" << std::fixed << std::setprecision(2)
                          << c.avg_similarity << "): " << c.pattern_content;

            std::string content = wisdom_content.str();
            Artha artha = embedder_.transform(content);

            int64_t wisdom_id = store_.remember(
                content,
                "wisdom",
                artha.nu.data,
                c.avg_confidence,  // Use average confidence from episodes
                0.005f,            // Wisdom decays very slowly
                "brahman",
                RealmVisibility::Private
            );

            if (wisdom_id < 0) continue;

            // Set provenance: derived from first episode
            store_.set_provenance(wisdom_id, "auto_distill", "auto_distill_episodes",
                                   c.avg_confidence, c.episode_ids[0]);

            // Assign to theme if theme_manager is available
            if (theme_manager_) {
                auto themes = store_.themes_by_relevance(artha.nu.data, 1);
                if (!themes.empty()) {
                    store_.theme_assign(wisdom_id, themes[0].id, 0.8f);
                }
            }

            // Create "EvolvedFrom" triplets linking wisdom to source episodes
            for (int64_t ep_id : c.episode_ids) {
                std::string wisdom_node = "wisdom:" + std::to_string(wisdom_id);
                std::string episode_node = "episode:" + std::to_string(ep_id);
                store_.connect(wisdom_node, "EvolvedFrom", episode_node);
            }

            // Weaken source episodes (don't delete, preserve provenance)
            for (int64_t ep_id : c.episode_ids) {
                store_.weaken(ep_id, 0.3f);
            }

            created++;
        }

        return created;
    }

    void touch(NodeId id) {
        std::unique_lock lock(mutex_);
        store_.touch(nodeid_to_int64(id));
        store_.strengthen(nodeid_to_int64(id), config_.reinforce_amount);
    }

    // Graph operations
    bool connect(const std::string& subject, const std::string& predicate, const std::string& object) {
        std::unique_lock lock(mutex_);
        return store_.connect(subject, predicate, object);
    }

    std::vector<std::tuple<std::string, std::string, float>>
    query_subject(const std::string& subject) const {
        std::shared_lock lock(mutex_);
        auto triplets = store_.query_subject(subject);
        std::vector<std::tuple<std::string, std::string, float>> results;
        for (const auto& t : triplets) {
            results.emplace_back(t.predicate, t.object, t.weight);
        }
        return results;
    }

    std::vector<std::tuple<std::string, std::string, float>>
    query_object(const std::string& object) const {
        std::shared_lock lock(mutex_);
        auto triplets = store_.query_object(object);
        std::vector<std::tuple<std::string, std::string, float>> results;
        for (const auto& t : triplets) {
            results.emplace_back(t.subject, t.predicate, t.weight);
        }
        return results;
    }

    // Health
    DuckDBHealth health() const {
        std::shared_lock lock(mutex_);
        auto h = store_.health();

        DuckDBHealth dh;
        dh.total_nodes = h.total_memories;
        dh.avg_confidence = h.avg_confidence;
        dh.active_nodes = h.total_memories;  // TODO: count by confidence threshold
        return dh;
    }

    size_t size() const {
        std::shared_lock lock(mutex_);
        return store_.memory_count();
    }

    // Access to store for advanced operations
    DuckDBStore& store() { return store_; }
    const DuckDBStore& store() const { return store_; }

    // Access to embedder for direct embedding
    Embedder& embedder() { return embedder_; }
    const Embedder& embedder() const { return embedder_; }

    // Tags
    std::vector<std::string> get_tags(NodeId id) const {
        std::shared_lock lock(mutex_);
        return store_.get_tags(nodeid_to_int64(id));
    }

    // Triplet count
    size_t triplet_count() const {
        std::shared_lock lock(mutex_);
        return store_.triplet_count();
    }

    // ═══════════════════════════════════════════════════════════════════════
    // THREAD-SAFE TRANSCRIPT OPERATIONS (for distillation thread)
    // IMPORTANT: Always use these instead of store() for transcript operations
    // ═══════════════════════════════════════════════════════════════════════

    std::vector<TranscriptState> get_pending_transcripts() {
        std::shared_lock lock(mutex_);
        return store_.get_pending_transcripts();
    }

    bool update_transcript_progress(const std::string& session_id, int64_t last_line) {
        std::unique_lock lock(mutex_);
        return store_.update_transcript_progress(session_id, last_line);
    }

    bool mark_transcript_distilled(const std::string& session_id) {
        std::unique_lock lock(mutex_);
        return store_.mark_transcript_distilled(session_id);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // EPIPLEXITY: Reconstruction quality metric
    // ═══════════════════════════════════════════════════════════════════════

    // Compute epiplexity for a seed given original and reconstructed text
    // Parameters tuned for SSL format seeds
    Epiplexity compute_epiplexity(const std::string& original,
                                   const std::string& seed,
                                   const std::string& reconstructed) {
        Epiplexity e;

        if (!embedder_.ready()) {
            return e;  // All zeros if no embedder
        }

        // S: Semantic fidelity - cosine similarity mapped to [0,1]
        Vector orig_emb = embedder_.embed(original);
        Vector recon_emb = embedder_.embed(reconstructed);
        if (orig_emb.size() > 0 && recon_emb.size() > 0) {
            float cosine = orig_emb.cosine(recon_emb);
            e.semantic_fidelity = (1.0f + cosine) / 2.0f;  // Map [-1,1] to [0,1]
        }

        // K: Entity preservation - count key terms preserved in seed
        auto count_terms = [](const std::string& text) {
            std::set<std::string> terms;
            std::string word;
            for (char c : text) {
                if (std::isalnum(c) || c == '_') {
                    word += std::tolower(c);
                } else if (!word.empty()) {
                    if (word.length() >= 3) terms.insert(word);
                    word.clear();
                }
            }
            if (!word.empty() && word.length() >= 3) terms.insert(word);
            return terms;
        };
        auto orig_terms = count_terms(original);
        auto seed_terms = count_terms(seed);
        if (!orig_terms.empty()) {
            size_t preserved = 0;
            for (const auto& t : seed_terms) {
                if (orig_terms.count(t)) preserved++;
            }
            float precision = seed_terms.empty() ? 0.0f :
                             static_cast<float>(preserved) / seed_terms.size();
            float recall = static_cast<float>(preserved) / orig_terms.size();
            e.entity_preservation = (precision + recall > 0) ?
                                   2.0f * precision * recall / (precision + recall) : 0.0f;
        }

        // D: Information density - concepts per token (sigmoid normalized)
        // Tuned for SSL: τ=0.4 (40% concept density target), α=6 (sharper transition)
        auto count_tokens = [](const std::string& text) {
            size_t count = 0;
            bool in_word = false;
            for (char c : text) {
                if (std::isalnum(c)) {
                    if (!in_word) { count++; in_word = true; }
                } else {
                    in_word = false;
                }
            }
            return count;
        };
        size_t seed_tokens = count_tokens(seed);
        float density = seed_tokens > 0 ?
                       static_cast<float>(seed_terms.size()) / seed_tokens : 0.0f;
        e.information_density = 1.0f / (1.0f + std::exp(-6.0f * (density - 0.4f)));

        // C: Compression utility - reward compression with saturation
        // Tuned: β=0.7 rewards compression more aggressively
        // 2x compression → 0.50, 3x → 0.65, 5x → 0.77
        size_t orig_tokens = count_tokens(original);
        if (orig_tokens > 0 && seed_tokens > 0 && orig_tokens > seed_tokens) {
            float log_ratio = std::log(static_cast<float>(orig_tokens) / seed_tokens);
            e.compression_utility = 1.0f - std::exp(-0.7f * log_ratio);
        } else if (orig_tokens <= seed_tokens) {
            e.compression_utility = 0.1f;  // Penalty for expansion
        }

        e.compute_score();
        return e;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // RESONANCE ARCHITECTURE
    // ═══════════════════════════════════════════════════════════════════════

    // Full Resonate: All mechanisms working together
    // 1. Session Priming: Context modulates which patterns activate
    // 2. Spreading Activation: Activation spreads through triplet graph
    // 3. Attractor Dynamics: Results pulled toward conceptual gravity wells
    // 4. Lateral Inhibition: Similar patterns compete
    // 5. Hebbian Learning: Co-activated nodes strengthen connections
    // 6. Self-Tuning: Bayesian bandits adapt parameters from feedback
    // Lock strategy: shared_lock for read phases (embedding, DB queries, scoring,
    // lateral inhibition), then briefly upgrade to unique_lock for write phases
    // (Hebbian update, bandit state, session context, learner state).
    std::vector<Recall> full_resonate(const std::string& query, size_t k = 10,
                                        const std::vector<std::string>& exclude_kinds = {}) {
        std::vector<Recall> results;
        DuckDBResonanceConfig active_config;
        QueryContext context;
        std::vector<int64_t> seed_ids_for_priming;

        {
            // Read phase: shared lock allows concurrent reads
            std::shared_lock lock(mutex_);

            if (!embedder_.ready()) {
                return {};
            }

            // Extract query context for contextual bandit
            context = extract_query_context_unlocked(query);

            // Self-tuning: sample parameters using Thompson sampling
            active_config = resonance_config_;
            if (enable_learning_) {
                active_config = learner_.sample_params(context);
            }

            // Transform query to embedding (query mode — adds instruction prefix for BGE)
            Artha artha = embedder_.transform_query(query);
            if (artha.nu.size() == 0) return {};

            // Phase 1a: Get semantic seeds (initial candidates)
            auto seeds = store_.recall(artha.nu.data, k * 2, "", true, exclude_kinds);
            if (seeds.empty()) return {};

            // Phase 1b: BM25 keyword search for hybrid scoring
            constexpr float w_bm25 = 0.3f;
            constexpr float tag_boost = 0.05f;
            std::unordered_map<int64_t, float> bm25_scores;
            if (store_.has_fts() && !query.empty()) {
                auto bm25_results = store_.bm25_search_memory(query, k * 3, "", true, exclude_kinds);
                float bm25_max = 0.0f;
                for (const auto& [id, score] : bm25_results) {
                    bm25_max = std::max(bm25_max, score);
                }
                if (bm25_max > 0.0f) {
                    for (const auto& [id, score] : bm25_results) {
                        bm25_scores[id] = score / bm25_max;
                    }
                }
            }

            // Phase 1c: Tag hits for query terms
            auto query_terms = extract_terms_unlocked(query);
            auto tag_hit_ids = store_.tag_hits(query_terms);

            // Phase 2: Find attractors (conceptual gravity wells)
            auto attractors = find_attractors_unlocked();

            // Phase 3: Spread activation through triplet graph
            std::unordered_map<int64_t, float> activation;

            for (const auto& seed : seeds) {
                activation[seed.id] = seed.similarity * active_config.spread_strength;
            }

            spread_activation_unlocked(query_terms, activation, active_config);

            // Phase 4: Build results with activation-boosted relevance
            std::unordered_set<int64_t> seen;

            for (const auto& seed : seeds) {
                Recall recall;
                recall.id = int64_to_nodeid(seed.id);
                recall.text = seed.content;
                recall.similarity = seed.similarity;
                recall.type = string_to_node_type(seed.kind);
                recall.confidence = Confidence(seed.confidence);
                recall.created = seed.created_at;
                recall.accessed = seed.accessed_at;

                float act = activation.count(seed.id) ? activation[seed.id] : 0.0f;
                float bm25 = bm25_scores.count(seed.id) ? bm25_scores[seed.id] : 0.0f;
                float tag_bonus = tag_hit_ids.count(seed.id) ? tag_boost : 0.0f;

                float confidence_factor = 0.5f + 0.5f * seed.confidence;
                recall.relevance = (seed.similarity * active_config.semantic_weight
                                 + bm25 * w_bm25
                                 + act * active_config.activation_weight
                                 + tag_bonus) * confidence_factor;

                // Session priming boost
                if (session_context_.has_observation(seed.id)) {
                    recall.relevance *= (1.0f + session_context_.priming_boost);
                }

                results.push_back(std::move(recall));
                seen.insert(seed.id);

                // Collect seed IDs for priming update (deferred to write phase)
                seed_ids_for_priming.push_back(seed.id);
            }

            // Add activated nodes not in seeds (discovered via graph)
            for (const auto& [id, act] : activation) {
                if (seen.count(id) || act < 0.1f) continue;

                auto mem_opt = store_.get_memory(id);
                if (!mem_opt) continue;
                const auto& mem = *mem_opt;

                Recall recall;
                recall.id = int64_to_nodeid(id);
                recall.text = mem.content;
                recall.type = string_to_node_type(mem.kind);
                recall.confidence = Confidence(mem.confidence);
                recall.relevance = act;

                results.push_back(std::move(recall));
                seen.insert(id);
            }

            // Phase 4b: Code intelligence - hybrid BM25 + term search
            bool skip_code_intel = std::find(exclude_kinds.begin(), exclude_kinds.end(), "symbol") != exclude_kinds.end();
            if (!skip_code_intel) {
                std::vector<Symbol> code_symbols;
                if (store_.has_fts()) {
                    code_symbols = store_.bm25_search_symbols(query, active_config.max_code_symbols);
                }
                if (code_symbols.size() < active_config.max_code_symbols) {
                    auto term_symbols = find_code_symbols_unlocked(query_terms,
                        active_config.max_code_symbols - code_symbols.size());
                    std::unordered_set<int64_t> seen_symbols;
                    for (const auto& s : code_symbols) seen_symbols.insert(s.id);
                    for (auto& s : term_symbols) {
                        if (!seen_symbols.count(s.id)) {
                            code_symbols.push_back(std::move(s));
                        }
                    }
                }

                for (const auto& sym : code_symbols) {
                    Recall recall;
                    recall.id = NodeId{static_cast<uint64_t>(-sym.id), 0};
                    recall.text = "[CODE] " + sym.kind + " " + sym.name +
                                 " @ " + sym.file_path + ":" + std::to_string(sym.line_start);
                    recall.type = NodeType::Operation;
                    recall.relevance = active_config.code_symbol_weight;
                    recall.confidence = Confidence(1.0f);
                    results.push_back(std::move(recall));
                }
            }

            // Phase 5: Attractor dynamics - boost results in same basin
            if (!attractors.empty() && !results.empty()) {
                apply_attractor_boost_unlocked(results, attractors, query_terms);
            }

            // Sort by relevance
            std::sort(results.begin(), results.end(),
                      [](const Recall& a, const Recall& b) {
                          return a.relevance > b.relevance;
                      });

            // Phase 6: Lateral inhibition (competition) - read-only scoring
            if (active_config.enable_competition && results.size() >= 2) {
                apply_lateral_inhibition_unlocked(results, active_config);
            }

            // Limit to k results
            if (results.size() > k) {
                results.resize(k);
            }
        }  // shared_lock released

        // Write phase: unique lock for Hebbian update, session context, learner state
        {
            std::unique_lock lock(mutex_);

            // Record observations for future priming
            for (int64_t id : seed_ids_for_priming) {
                session_context_.observe(id);
            }

            // Phase 7: Hebbian learning - strengthen triplet connections
            if (results.size() >= 2) {
                hebbian_update_unlocked(results, active_config);
            }

            // Phase 8: Record outcome for self-tuning credit assignment
            if (enable_learning_ && !results.empty()) {
                std::vector<int64_t> result_ids;
                result_ids.reserve(results.size());
                for (const auto& r : results) {
                    result_ids.push_back(static_cast<int64_t>(r.id.low));
                }
                learner_.record_outcome(result_ids, active_config, context);
            }
        }

        return results;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // xMemory THEME-BASED RETRIEVAL
    // ═══════════════════════════════════════════════════════════════════════

    // Two-stage theme recall (xMemory architecture)
    // Stage 1: Find relevant themes, select representatives
    // Stage 2: Adaptive expansion from high-relevance themes
    std::vector<Recall> theme_recall(const std::string& query, size_t k = 10,
                                      const std::string& realm = "",
                                      const std::vector<std::string>& exclude_kinds = {}) {
        std::unique_lock lock(mutex_);

        if (!theme_manager_ || !embedder_.ready()) {
            // Fall back to full_resonate if theme manager not initialized
            return {};
        }

        // Transform query to embedding (query mode — adds instruction prefix for BGE)
        Artha artha = embedder_.transform_query(query);
        if (artha.nu.size() == 0) return {};

        // Two-stage retrieval
        auto memory_results = theme_manager_->two_stage_recall(artha.nu.data, k, realm);

        // Convert to Recall format
        std::vector<Recall> recalls;
        recalls.reserve(memory_results.size());

        for (const auto& mr : memory_results) {
            Recall recall;
            recall.id = int64_to_nodeid(mr.id);
            recall.text = mr.content;
            recall.similarity = mr.similarity;
            recall.type = string_to_node_type(mr.kind);
            recall.confidence = Confidence(mr.confidence);
            recall.relevance = mr.similarity;  // Theme-based relevance
            recall.created = mr.created_at;
            recall.accessed = mr.accessed_at;
            recalls.push_back(std::move(recall));
        }

        // Touch retrieved memories
        for (const auto& recall : recalls) {
            int64_t id = nodeid_to_int64(recall.id);
            store_.touch(id);
            store_.strengthen(id, config_.reinforce_amount);
        }

        return recalls;
    }

    // Get theme retrieval results (full detail with theme info)
    std::vector<ThemeResult> theme_recall_detailed(const std::string& query,
                                                    size_t max_themes = 5,
                                                    const std::string& realm = "") {
        std::shared_lock lock(mutex_);

        if (!theme_manager_ || !embedder_.ready()) {
            return {};
        }

        Artha artha = embedder_.transform_query(query);
        if (artha.nu.size() == 0) return {};

        return theme_manager_->retrieve_representatives(artha.nu.data, max_themes, realm);
    }

    // Assign a memory to a theme (manual override)
    bool assign_to_theme(NodeId memory_id, int64_t theme_id) {
        std::unique_lock lock(mutex_);
        if (!theme_manager_) return false;

        return store_.theme_assign(nodeid_to_int64(memory_id), theme_id);
    }

    // Create a new theme
    int64_t create_theme(const std::string& name, const std::string& realm = "brahman") {
        std::unique_lock lock(mutex_);
        return store_.theme_create(name, {}, realm);
    }

    // Get theme statistics
    ThemeStats get_theme_stats(const std::string& realm = "") {
        std::shared_lock lock(mutex_);
        return store_.theme_stats(realm);
    }

    // Run theme maintenance
    DuckDBStore::ThemeMaintenanceResult run_theme_maintenance(const std::string& realm = "") {
        std::unique_lock lock(mutex_);
        if (!theme_manager_) {
            return {};
        }
        return theme_manager_->run_maintenance(realm);
    }

    // Access to theme manager
    ThemeManager* theme_manager() { return theme_manager_.get(); }
    const ThemeManager* theme_manager() const { return theme_manager_.get(); }

    // Access to narrative engine
    NarrativeEngine* narrative() { return narrative_engine_.get(); }
    const NarrativeEngine* narrative() const { return narrative_engine_.get(); }

    // Access to anticipator
    Anticipator* anticipator() { return anticipator_.get(); }
    const Anticipator* anticipator() const { return anticipator_.get(); }

    // Access to theme config
    ThemeConfig& theme_config() { return theme_config_; }
    const ThemeConfig& theme_config() const { return theme_config_; }

    // Enable/disable theme-based recall
    void set_theme_recall_enabled(bool enabled) { enable_theme_recall_ = enabled; }
    bool is_theme_recall_enabled() const { return enable_theme_recall_; }

    // Access to session context
    DuckDBSessionContext& session_context() { return session_context_; }
    const DuckDBSessionContext& session_context() const { return session_context_; }

    // Access to resonance config
    DuckDBResonanceConfig& resonance_config() { return resonance_config_; }
    const DuckDBResonanceConfig& resonance_config() const { return resonance_config_; }

    // Access to learner for stats and control
    ResonanceLearner& learner() { return learner_; }
    const ResonanceLearner& learner() const { return learner_; }

    // Enable/disable self-tuning
    void set_learning_enabled(bool enabled) { enable_learning_ = enabled; }
    bool is_learning_enabled() const { return enable_learning_; }

    // Get learning statistics
    ResonanceLearner::LearningStats get_learning_stats() const {
        return learner_.get_stats();
    }

    // Persist learner state to a memory node (call periodically or on shutdown)
    bool save_learner_state() {
        std::unique_lock lock(mutex_);
        return save_learner_state_unlocked();
    }

    // Load learner state from storage (call on startup)
    bool load_learner_state() {
        std::unique_lock lock(mutex_);
        return load_learner_state_unlocked();
    }

    // Find attractors (public interface)
    std::vector<DuckDBAttractor> find_attractors() {
        std::shared_lock lock(mutex_);
        return find_attractors_unlocked();
    }

    // Hebbian strengthen a specific connection
    void hebbian_strengthen(const std::string& subject, const std::string& object,
                            float strength = 0.1f) {
        std::unique_lock lock(mutex_);
        // Find existing triplet and strengthen, or create new one
        auto triplets = store_.query_subject(to_lower(subject));
        for (const auto& t : triplets) {
            if (t.object == to_lower(object)) {
                // Strengthen existing connection
                float new_weight = std::min(t.weight + strength, 1.0f);
                store_.connect(t.subject, t.predicate, t.object, new_weight);
                return;
            }
        }
        // No existing connection - create with "related_to" predicate
        store_.connect(to_lower(subject), "related_to", to_lower(object), strength);
    }

private:
    // ═══════════════════════════════════════════════════════════════════════
    // Resonance Helper Methods (unlocked - caller must hold mutex)
    // ═══════════════════════════════════════════════════════════════════════

    // Extract significant terms from query for graph traversal
    std::vector<std::string> extract_terms_unlocked(const std::string& query) const {
        // Thread-safe: const after static initialization
        static const std::unordered_set<std::string> stopwords = {
            "the", "a", "an", "is", "are", "was", "were", "be", "been", "being",
            "have", "has", "had", "do", "does", "did", "will", "would", "could",
            "should", "may", "might", "must", "shall", "can", "need", "dare",
            "to", "of", "in", "for", "on", "with", "at", "by", "from", "as",
            "into", "through", "during", "before", "after", "above", "below",
            "between", "under", "again", "further", "then", "once", "here",
            "there", "when", "where", "why", "how", "all", "each", "few",
            "more", "most", "other", "some", "such", "no", "nor", "not",
            "only", "own", "same", "so", "than", "too", "very", "just",
            "and", "but", "if", "or", "because", "until", "while", "about",
            "what", "which", "who", "this", "that", "these", "those", "it"
        };

        std::vector<std::string> terms;
        std::string current;

        for (char c : query) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
                current += std::tolower(static_cast<unsigned char>(c));
            } else if (!current.empty()) {
                if (current.length() >= 3 && !stopwords.count(current)) {
                    terms.push_back(current);
                }
                current.clear();
            }
        }
        if (!current.empty() && current.length() >= 3 && !stopwords.count(current)) {
            terms.push_back(current);
        }

        return terms;
    }

    // Find attractors: entities with high triplet connectivity
    // OPTIMIZED: Uses cache with 5-minute TTL to avoid expensive query
    std::vector<DuckDBAttractor> find_attractors_unlocked() const {
        auto now = std::chrono::steady_clock::now();

        // Return cached attractors if still valid
        if (!attractor_cache_.empty() &&
            (now - attractor_cache_time_) < ATTRACTOR_CACHE_TTL) {
            return attractor_cache_;
        }

        // Cache miss or expired - compute fresh
        auto top_entities = store_.get_top_connected_entities(resonance_config_.max_attractors);

        attractor_cache_.clear();
        attractor_cache_.reserve(top_entities.size());

        for (const auto& [entity, count] : top_entities) {
            float strength = std::min(std::log2(1.0f + count) / 4.0f, 1.0f);
            attractor_cache_.push_back({entity, strength, count});
        }
        attractor_cache_time_ = now;

        return attractor_cache_;
    }

    // Extract query context for contextual bandit
    // OPTIMIZED: Removed expensive triplet queries - uses heuristics instead
    QueryContext extract_query_context_unlocked(const std::string& query) const {
        QueryContext ctx;
        ctx.query_length = query.length();

        auto terms = extract_terms_unlocked(query);
        ctx.term_count = terms.size();

        // Check for domain prefix like [cc-soul], [biology]
        ctx.has_domain_prefix = query.find('[') != std::string::npos &&
                                query.find(']') != std::string::npos;

        // Check for technical terms (contains underscores, camelCase, or code patterns)
        for (const auto& term : terms) {
            if (term.find('_') != std::string::npos ||
                term.find("impl") != std::string::npos ||
                term.find("func") != std::string::npos ||
                term.find("class") != std::string::npos) {
                ctx.has_technical_terms = true;
                break;
            }
        }

        // OPTIMIZATION: Skip expensive triplet frequency calculation
        // Use heuristic based on term count instead
        ctx.avg_term_frequency = static_cast<float>(terms.size() * 10);

        return ctx;
    }

    // Spread activation through triplet graph (with config parameter)
    // OPTIMIZED: Limited iterations to prevent runaway on large graphs
    void spread_activation_unlocked(const std::vector<std::string>& seed_terms,
                                     std::unordered_map<int64_t, float>& activation,
                                     const DuckDBResonanceConfig& config) {
        std::unordered_set<std::string> visited;
        std::queue<std::tuple<std::string, float, int>> frontier;

        // Initialize frontier with seed terms
        for (const auto& term : seed_terms) {
            frontier.push({term, config.spread_strength, 0});
        }

        // CRITICAL: Limit total iterations to prevent runaway on large graphs
        // With 945K triplets, unbounded BFS causes hangs
        constexpr size_t MAX_ITERATIONS = 50;
        constexpr size_t MAX_TRIPLETS_PER_ENTITY = 100;
        size_t iterations = 0;

        while (!frontier.empty() && iterations < MAX_ITERATIONS) {
            auto [entity, strength, hop] = frontier.front();
            frontier.pop();

            if (hop >= config.max_hops) continue;
            if (strength < config.min_activation) continue;
            if (visited.count(entity)) continue;
            visited.insert(entity);
            iterations++;

            // Get connected entities via triplets (limited to prevent explosion)
            auto subject_triplets = store_.query_subject(entity);
            auto object_triplets = store_.query_object(entity);

            // Limit triplets processed per entity
            size_t subj_limit = std::min(subject_triplets.size(), MAX_TRIPLETS_PER_ENTITY);
            size_t obj_limit = std::min(object_triplets.size(), MAX_TRIPLETS_PER_ENTITY);

            // Propagate to connected objects
            for (size_t i = 0; i < subj_limit; ++i) {
                const auto& t = subject_triplets[i];
                float propagated = strength * config.spread_decay * t.weight;
                if (propagated >= config.min_activation) {
                    frontier.push({t.object, propagated, hop + 1});
                    // NOTE: Removed boost_memories_by_term_unlocked - too expensive O(n*m)
                }
            }

            // Propagate to connected subjects
            for (size_t i = 0; i < obj_limit; ++i) {
                const auto& t = object_triplets[i];
                float propagated = strength * config.spread_decay * t.weight;
                if (propagated >= config.min_activation) {
                    frontier.push({t.subject, propagated, hop + 1});
                }
            }
        }
    }

    // Boost activation for memories containing a term
    void boost_memories_by_term_unlocked(const std::string& term, float boost,
                                          std::unordered_map<int64_t, float>& activation) {
        // This is a simplification - in production would use BM25 index
        // For now, just add the boost to activation map entries whose content contains the term
        for (auto& [id, act] : activation) {
            auto mem = store_.get_memory(id);
            if (mem && mem->content.find(term) != std::string::npos) {
                act += boost;
            }
        }
    }

    // Find code symbols matching query terms (for hybrid search)
    std::vector<Symbol> find_code_symbols_unlocked(const std::vector<std::string>& terms,
                                                    size_t max_results) {
        std::vector<Symbol> symbols;
        std::unordered_set<int64_t> seen_ids;

        for (const auto& term : terms) {
            if (term.length() < 3) continue;  // Skip very short terms

            // Search for symbols matching this term
            auto matches = store_.find_symbol(term);

            for (auto& sym : matches) {
                if (seen_ids.count(sym.id)) continue;
                seen_ids.insert(sym.id);
                symbols.push_back(std::move(sym));

                if (symbols.size() >= max_results) {
                    return symbols;
                }
            }
        }
        return symbols;
    }

    // Apply attractor boost to results in same conceptual basin
    void apply_attractor_boost_unlocked(std::vector<Recall>& results,
                                         const std::vector<DuckDBAttractor>& attractors,
                                         const std::vector<std::string>& query_terms) {
        if (attractors.empty()) return;

        // Find which attractor the query is closest to
        std::string primary_attractor;
        float best_match = 0.0f;

        for (const auto& attr : attractors) {
            for (const auto& term : query_terms) {
                if (attr.entity == term || attr.entity.find(term) != std::string::npos) {
                    if (attr.strength > best_match) {
                        best_match = attr.strength;
                        primary_attractor = attr.entity;
                    }
                }
            }
        }

        if (primary_attractor.empty()) return;

        // Boost results that are in the same basin (mention the attractor entity)
        for (auto& result : results) {
            std::string lower_text = to_lower(result.text);
            if (lower_text.find(primary_attractor) != std::string::npos) {
                result.relevance *= resonance_config_.basin_boost;
            }
        }
    }

    // Apply lateral inhibition (winner-take-all competition)
    // Optimized: pre-extracts term sets for all results once (O(n)),
    // then uses indices in the pairwise loop (avoids repeated extract_terms calls).
    void apply_lateral_inhibition_unlocked(std::vector<Recall>& results,
                                            const DuckDBResonanceConfig& config) {
        if (results.size() < 2) return;

        // Pre-extract term sets for all results ONCE
        std::vector<std::unordered_set<std::string>> term_sets;
        term_sets.reserve(results.size());
        for (const auto& r : results) {
            auto terms = extract_terms_unlocked(r.text);
            term_sets.emplace_back(terms.begin(), terms.end());
        }

        std::vector<bool> suppressed(results.size(), false);

        // Process in relevance order (winners first)
        for (size_t i = 0; i < results.size(); ++i) {
            if (suppressed[i]) continue;

            // Winner inhibits similar nodes below it
            for (size_t j = i + 1; j < results.size(); ++j) {
                if (suppressed[j]) continue;

                // Jaccard similarity using pre-extracted term sets
                float text_sim = compute_jaccard_similarity(term_sets[i], term_sets[j]);

                if (text_sim > config.similarity_threshold) {
                    // Lateral inhibition: winner suppresses this similar loser
                    suppressed[j] = true;
                    float penalty = config.inhibition_strength * text_sim;
                    results[j].relevance *= (1.0f - penalty);
                }
            }
        }

        // Re-sort after applying penalties
        std::sort(results.begin(), results.end(),
                  [](const Recall& a, const Recall& b) {
                      return a.relevance > b.relevance;
                  });
    }

    // Jaccard similarity between pre-computed term sets
    static float compute_jaccard_similarity(const std::unordered_set<std::string>& set_a,
                                            const std::unordered_set<std::string>& set_b) {
        if (set_a.empty() || set_b.empty()) return 0.0f;

        size_t intersection = 0;
        // Iterate over the smaller set for efficiency
        const auto& smaller = (set_a.size() <= set_b.size()) ? set_a : set_b;
        const auto& larger = (set_a.size() <= set_b.size()) ? set_b : set_a;
        for (const auto& term : smaller) {
            if (larger.count(term)) intersection++;
        }

        size_t union_size = set_a.size() + set_b.size() - intersection;
        return union_size > 0 ? static_cast<float>(intersection) / union_size : 0.0f;
    }

    // Simple text similarity (Jaccard on words) - kept for other callers
    float compute_text_similarity_unlocked(const std::string& a, const std::string& b) const {
        auto terms_a = extract_terms_unlocked(a);
        auto terms_b = extract_terms_unlocked(b);

        std::unordered_set<std::string> set_a(terms_a.begin(), terms_a.end());
        std::unordered_set<std::string> set_b(terms_b.begin(), terms_b.end());

        return compute_jaccard_similarity(set_a, set_b);
    }

    // Hebbian learning: strengthen connections between co-activated results
    void hebbian_update_unlocked(const std::vector<Recall>& results,
                                  const DuckDBResonanceConfig& config) {
        size_t n = std::min(results.size(), config.hebbian_top_k);

        // Extract key terms from top results
        std::vector<std::vector<std::string>> result_terms;
        for (size_t i = 0; i < n; ++i) {
            result_terms.push_back(extract_terms_unlocked(results[i].text));
        }

        // Strengthen connections between terms from different results
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = i + 1; j < n; ++j) {
                // Connect first significant term from each
                if (!result_terms[i].empty() && !result_terms[j].empty()) {
                    const std::string& term_i = result_terms[i][0];
                    const std::string& term_j = result_terms[j][0];

                    if (term_i != term_j) {
                        // Bidirectional strengthening
                        strengthen_triplet_unlocked(term_i, term_j,
                                                     config.hebbian_strength);
                        strengthen_triplet_unlocked(term_j, term_i,
                                                     config.hebbian_strength);
                    }
                }
            }
        }
    }

    // Strengthen a triplet connection
    void strengthen_triplet_unlocked(const std::string& subject, const std::string& object,
                                      float strength) {
        auto triplets = store_.query_subject(subject);
        for (const auto& t : triplets) {
            if (t.object == object) {
                // Strengthen existing
                float new_weight = std::min(t.weight + strength, 1.0f);
                store_.connect(t.subject, t.predicate, t.object, new_weight);
                return;
            }
        }
        // Create new "related_to" connection
        store_.connect(subject, "related_to", object, strength);
    }

    // Convert string to lowercase (in-place transform, single allocation)
    static std::string to_lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    // Persistence helpers (unlocked - caller must hold mutex)
    bool save_learner_state_unlocked() {
        std::string state = learner_.serialize();
        // Delete old learner state first
        // (In production, would use UPDATE or dedicated metadata table)
        int64_t id = store_.remember(
            "[LEARNER_STATE] " + state,
            "system",
            {},  // No embedding needed
            1.0f,  // Max confidence
            0.0f,  // No decay
            "brahman",
            RealmVisibility::Private
        );
        return id > 0;
    }

    bool load_learner_state_unlocked() {
        // Search for most recent learner state by scanning system nodes
        // This is a simplified approach - production would use dedicated table
        auto results = store_.recall({}, 50, "", true);
        for (const auto& r : results) {
            if (r.content.find("[LEARNER_STATE]") == 0) {
                std::string state = r.content.substr(16);  // Skip "[LEARNER_STATE] "
                if (learner_.deserialize(state)) {
                    return true;
                }
            }
        }
        return false;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Member Variables
    // ═══════════════════════════════════════════════════════════════════════

    DuckDBMindConfig config_;
    DuckDBResonanceConfig resonance_config_;
    DuckDBSessionContext session_context_;
    ResonanceLearner learner_;
    bool enable_learning_ = true;
    mutable DuckDBStore store_;
    Embedder embedder_;
    mutable std::shared_mutex mutex_;
    bool running_;

    // xMemory Theme System
    ThemeConfig theme_config_;
    mutable std::unique_ptr<ThemeManager> theme_manager_;
    bool enable_theme_recall_ = true;  // Use theme-based retrieval

    // Narrative and Anticipation
    std::unique_ptr<NarrativeEngine> narrative_engine_;
    std::unique_ptr<Anticipator> anticipator_;

    // Attractor cache (expensive to compute, changes slowly)
    mutable std::vector<DuckDBAttractor> attractor_cache_;
    mutable std::chrono::steady_clock::time_point attractor_cache_time_;
    static constexpr auto ATTRACTOR_CACHE_TTL = std::chrono::minutes(5);

    bool passes_quality_gate(const std::string& text) const {
        if (!config_.enable_quality_gate) return true;
        if (text.size() < config_.min_content_length) return false;

        size_t signal = 0;
        for (char c : text) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') signal++;
        }
        float ratio = static_cast<float>(signal) / text.size();
        return ratio >= config_.min_signal_ratio;
    }

    std::optional<int64_t> find_duplicate(const Vector& embedding, const std::string& text) {
        if (!config_.enable_deduplication) return std::nullopt;

        auto candidates = store_.recall(embedding.data, 5);
        for (const auto& c : candidates) {
            if (c.similarity >= config_.dedup_threshold) {
                if (c.content == text || c.similarity >= 0.98f) {
                    return c.id;
                }
            }
        }
        return std::nullopt;
    }

    static float default_decay_rate(NodeType type) {
        switch (type) {
            // Partnership memories - slow decay preserves context
            case NodeType::Wisdom:    return 0.005f;  // Insights last months
            case NodeType::Episode:   return 0.03f;   // Context fades slowly

            // Immutable - never decay
            case NodeType::Belief:    return 0.0f;
            case NodeType::Invariant: return 0.0f;

            // Code intelligence - never decay (structural knowledge)
            case NodeType::Symbol:         return 0.0f;
            case NodeType::ProjectEssence: return 0.0f;
            case NodeType::ModuleState:    return 0.0f;
            case NodeType::PatternState:   return 0.0f;

            default: return 0.01f;  // Slow default
        }
    }

    // Delegates to the canonical implementation in rpc/types.hpp
    static std::string node_type_to_string(NodeType type) {
        return chitta::rpc::node_type_to_string(type);
    }

    static NodeType string_to_node_type(const std::string& s) {
        if (s == "wisdom") return NodeType::Wisdom;
        if (s == "belief") return NodeType::Belief;
        if (s == "intention") return NodeType::Intention;
        if (s == "episode") return NodeType::Episode;
        if (s == "symbol") return NodeType::Symbol;
        if (s == "dream") return NodeType::Dream;
        if (s == "projectessence") return NodeType::ProjectEssence;
        if (s == "modulestate") return NodeType::ModuleState;
        if (s == "patternstate") return NodeType::PatternState;
        return NodeType::Episode;
    }

    // Convert between NodeId and int64_t
    static NodeId int64_to_nodeid(int64_t id) {
        NodeId nid;
        nid.high = 0;
        nid.low = static_cast<uint64_t>(id);
        return nid;
    }

    static int64_t nodeid_to_int64(const NodeId& id) {
        return static_cast<int64_t>(id.low);
    }
};

}  // namespace chitta
