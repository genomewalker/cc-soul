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
#include "embedder.hpp"
#include "types.hpp"
#include "../types.hpp"
#include "../vak_onnx.hpp"
#include <memory>
#include <shared_mutex>
#include <optional>
#include <queue>
#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <random>
#include <filesystem>
#include <set>
#include <chrono>
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
    std::unordered_set<int64_t> recent_observations;  // Memory IDs accessed this session
    std::unordered_set<std::string> active_topics;    // Current topic strings
    size_t max_observations = 50;                     // Limit recent observations

    float priming_boost = 0.3f;   // Boost for recently observed
    float topic_boost = 0.2f;     // Boost for topic-related

    void observe(int64_t id) {
        recent_observations.insert(id);
        if (recent_observations.size() > max_observations) {
            recent_observations.erase(recent_observations.begin());
        }
    }

    void add_topic(const std::string& topic) {
        active_topics.insert(topic);
    }

    void clear() {
        recent_observations.clear();
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
    float reinforce_amount = 0.05f;
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

    // Remember - store with embedding
    NodeId remember(const std::string& text, NodeType type = NodeType::Wisdom,
                    const std::string& realm = "brahman",
                    RealmVisibility visibility = RealmVisibility::Private) {
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

        int64_t id = store_.remember(text, kind, artha.nu.data, 0.8f, decay_rate, realm, visibility);
        if (id < 0) {
            return NodeId{};
        }

        return int64_to_nodeid(id);
    }

    NodeId remember(const std::string& text, NodeType type, const std::vector<std::string>& tags) {
        // For now, tags are stored in the content (DuckDB doesn't have native tag support yet)
        return remember(text, type, "brahman", RealmVisibility::Private);
    }

    // Recall - search with auto-reinforcement
    std::vector<Recall> recall(const std::string& query, size_t limit = 10) {
        std::unique_lock lock(mutex_);

        if (!embedder_.ready()) {
            return {};
        }

        Artha artha = embedder_.transform(query);
        auto results = store_.recall(artha.nu.data, limit);

        std::vector<Recall> recalls;
        for (const auto& r : results) {
            // Touch each recalled memory
            store_.touch(r.id);
            store_.strengthen(r.id, config_.reinforce_amount);

            Recall recall;
            recall.id = int64_to_nodeid(r.id);
            recall.text = r.content;
            recall.similarity = r.similarity;
            recall.relevance = r.similarity * r.confidence;
            recall.type = string_to_node_type(r.kind);
            recalls.push_back(recall);
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

    // Tags (stub for compatibility)
    std::vector<std::string> get_tags(NodeId) const { return {}; }

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
    std::vector<Recall> full_resonate(const std::string& query, size_t k = 10) {
        std::unique_lock lock(mutex_);

        if (!embedder_.ready()) {
            return {};
        }

        // Extract query context for contextual bandit
        QueryContext context = extract_query_context_unlocked(query);

        // Self-tuning: sample parameters using Thompson sampling
        DuckDBResonanceConfig active_config = resonance_config_;
        if (enable_learning_) {
            active_config = learner_.sample_params(context);
        }

        // Transform query to embedding
        Artha artha = embedder_.transform(query);
        if (artha.nu.size() == 0) return {};

        // Phase 1: Get semantic seeds (initial candidates)
        auto seeds = store_.recall(artha.nu.data, k * 2);
        if (seeds.empty()) return {};

        // Phase 2: Find attractors (conceptual gravity wells)
        auto attractors = find_attractors_unlocked();

        // Phase 3: Spread activation through triplet graph
        // Extract terms from query for graph traversal
        auto query_terms = extract_terms_unlocked(query);
        std::unordered_map<int64_t, float> activation;

        // Initialize activation from seeds (using sampled params)
        for (const auto& seed : seeds) {
            activation[seed.id] = seed.similarity * active_config.spread_strength;
        }

        // Spread through triplet graph (using sampled params)
        spread_activation_unlocked(query_terms, activation, active_config);

        // Phase 4: Build results with activation-boosted relevance
        std::vector<Recall> results;
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

            // Combine semantic relevance with activation (using sampled weights)
            float act = activation.count(seed.id) ? activation[seed.id] : 0.0f;
            recall.relevance = seed.similarity * seed.confidence * active_config.semantic_weight
                             + act * active_config.activation_weight;

            // Session priming boost
            if (session_context_.recent_observations.count(seed.id)) {
                recall.relevance *= (1.0f + session_context_.priming_boost);
            }

            results.push_back(std::move(recall));
            seen.insert(seed.id);

            // Record observation for future priming
            session_context_.observe(seed.id);
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
        std::vector<Symbol> code_symbols;
        if (store_.has_fts()) {
            // Use BM25 full-text search (ranked by relevance)
            code_symbols = store_.bm25_search_symbols(query, active_config.max_code_symbols);
        }
        // Fallback or supplement with term-based search
        if (code_symbols.size() < active_config.max_code_symbols) {
            auto term_symbols = find_code_symbols_unlocked(query_terms,
                active_config.max_code_symbols - code_symbols.size());
            // Merge, avoiding duplicates
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
            // Use negative ID range for code symbols to avoid collision
            recall.id = NodeId{static_cast<uint64_t>(-sym.id), 0};
            // Format: "kind name @ file:line"
            recall.text = "[CODE] " + sym.kind + " " + sym.name +
                         " @ " + sym.file_path + ":" + std::to_string(sym.line_start);
            recall.type = NodeType::Operation;  // Code symbols are operations
            recall.relevance = active_config.code_symbol_weight;
            recall.confidence = Confidence(1.0f);  // Code symbols are certain
            results.push_back(std::move(recall));
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

        // Phase 6: Lateral inhibition (competition) - using sampled config
        if (active_config.enable_competition && results.size() >= 2) {
            apply_lateral_inhibition_unlocked(results, active_config);
        }

        // Limit to k results
        if (results.size() > k) {
            results.resize(k);
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

        return results;
    }

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
    // Uses efficient SQL query that excludes code intel triplets
    std::vector<DuckDBAttractor> find_attractors_unlocked() const {
        // Use efficient SQL-based counting (excludes code intel predicates)
        auto top_entities = store_.get_top_connected_entities(resonance_config_.max_attractors);

        std::vector<DuckDBAttractor> attractors;
        attractors.reserve(top_entities.size());

        for (const auto& [entity, count] : top_entities) {
            float strength = std::min(std::log2(1.0f + count) / 4.0f, 1.0f);
            attractors.push_back({entity, strength, count});
        }

        return attractors;
    }

    // Extract query context for contextual bandit
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

        // Calculate average term frequency in triplets
        float total_freq = 0.0f;
        for (const auto& term : terms) {
            auto subj = store_.query_subject(term);
            auto obj = store_.query_object(term);
            total_freq += subj.size() + obj.size();
        }
        ctx.avg_term_frequency = terms.empty() ? 0.0f : total_freq / terms.size();

        return ctx;
    }

    // Spread activation through triplet graph (with config parameter)
    void spread_activation_unlocked(const std::vector<std::string>& seed_terms,
                                     std::unordered_map<int64_t, float>& activation,
                                     const DuckDBResonanceConfig& config) {
        std::unordered_set<std::string> visited;
        std::queue<std::tuple<std::string, float, int>> frontier;

        // Initialize frontier with seed terms
        for (const auto& term : seed_terms) {
            frontier.push({term, config.spread_strength, 0});
        }

        while (!frontier.empty()) {
            auto [entity, strength, hop] = frontier.front();
            frontier.pop();

            if (hop >= config.max_hops) continue;
            if (strength < config.min_activation) continue;
            if (visited.count(entity)) continue;
            visited.insert(entity);

            // Get connected entities via triplets
            auto subject_triplets = store_.query_subject(entity);
            auto object_triplets = store_.query_object(entity);

            // Propagate to connected objects
            for (const auto& t : subject_triplets) {
                float propagated = strength * config.spread_decay * t.weight;
                if (propagated >= config.min_activation) {
                    frontier.push({t.object, propagated, hop + 1});

                    // Find memories mentioning this object and boost their activation
                    boost_memories_by_term_unlocked(t.object, propagated, activation);
                }
            }

            // Propagate to connected subjects
            for (const auto& t : object_triplets) {
                float propagated = strength * config.spread_decay * t.weight;
                if (propagated >= config.min_activation) {
                    frontier.push({t.subject, propagated, hop + 1});

                    boost_memories_by_term_unlocked(t.subject, propagated, activation);
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
    void apply_lateral_inhibition_unlocked(std::vector<Recall>& results,
                                            const DuckDBResonanceConfig& config) {
        if (results.size() < 2) return;

        std::vector<bool> suppressed(results.size(), false);

        // Process in relevance order (winners first)
        for (size_t i = 0; i < results.size(); ++i) {
            if (suppressed[i]) continue;

            // Winner inhibits similar nodes below it
            for (size_t j = i + 1; j < results.size(); ++j) {
                if (suppressed[j]) continue;

                // Check similarity based on text overlap (simplified)
                float text_sim = compute_text_similarity_unlocked(results[i].text, results[j].text);

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

    // Simple text similarity (Jaccard on words)
    float compute_text_similarity_unlocked(const std::string& a, const std::string& b) const {
        auto terms_a = extract_terms_unlocked(a);
        auto terms_b = extract_terms_unlocked(b);

        if (terms_a.empty() || terms_b.empty()) return 0.0f;

        std::unordered_set<std::string> set_a(terms_a.begin(), terms_a.end());
        std::unordered_set<std::string> set_b(terms_b.begin(), terms_b.end());

        size_t intersection = 0;
        for (const auto& term : set_a) {
            if (set_b.count(term)) intersection++;
        }

        size_t union_size = set_a.size() + set_b.size() - intersection;
        return union_size > 0 ? static_cast<float>(intersection) / union_size : 0.0f;
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

    // Convert string to lowercase
    static std::string to_lower(const std::string& s) {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            result += std::tolower(static_cast<unsigned char>(c));
        }
        return result;
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
            case NodeType::Wisdom: return 0.02f;
            case NodeType::Belief: return 0.0f;
            case NodeType::Invariant: return 0.0f;
            case NodeType::Episode: return 0.10f;
            default: return 0.05f;
        }
    }

    static std::string node_type_to_string(NodeType type) {
        switch (type) {
            case NodeType::Wisdom: return "wisdom";
            case NodeType::Belief: return "belief";
            case NodeType::Intention: return "intention";
            case NodeType::Episode: return "episode";
            case NodeType::Symbol: return "symbol";
            case NodeType::Dream: return "dream";
            default: return "unknown";
        }
    }

    static NodeType string_to_node_type(const std::string& s) {
        if (s == "wisdom") return NodeType::Wisdom;
        if (s == "belief") return NodeType::Belief;
        if (s == "intention") return NodeType::Intention;
        if (s == "episode") return NodeType::Episode;
        if (s == "symbol") return NodeType::Symbol;
        if (s == "dream") return NodeType::Dream;
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
