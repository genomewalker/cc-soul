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
#include "../narrative.hpp"
#include "../anticipator.hpp"
#include "../resonance_learner.hpp"
#include "embedder.hpp"
#include "types.hpp"
#include "../types.hpp"
#include "../rpc/types.hpp"
#ifdef CHITTA_WITH_ONNX
#include "../vak_onnx.hpp"
#endif
#ifdef CHITTA_FIELD_AVAILABLE
#include "../field_store.hpp"
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

// ResonanceConfig, BetaPrior, GaussianPrior, QueryContext,
// PendingResonanceOutcome, and ResonanceLearner are now in
// ../resonance_learner.hpp (standalone, no DuckDB dependency).

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
        labile_memories.clear();
        centroid_initialized = false;
        centroid_observations = 0;
    }

    // Reconsolidation: recently retrieved memories in labile state
    struct LabileMemory {
        int64_t memory_id;
        int64_t retrieved_at_ms;
        Vector embedding;
    };
    std::deque<LabileMemory> labile_memories;

    void mark_labile(int64_t id, int64_t now_ms, const Vector& emb) {
        // Avoid duplicates
        for (const auto& lm : labile_memories) {
            if (lm.memory_id == id) return;
        }
        labile_memories.push_back({id, now_ms, emb});
    }

    void prune_labile(int64_t now_ms, int64_t window_ms) {
        while (!labile_memories.empty() &&
               now_ms - labile_memories.front().retrieved_at_ms > window_ms)
            labile_memories.pop_front();
    }

    Vector context_centroid;
    size_t centroid_observations = 0;
    bool centroid_initialized = false;

    void update_centroid(const Vector& emb, float alpha = 0.1f) {
        if (!centroid_initialized) {
            context_centroid = emb;
            centroid_initialized = true;
        } else {
            for (size_t i = 0; i < emb.size(); i++)
                context_centroid[i] = alpha * emb[i] + (1.0f - alpha) * context_centroid[i];
        }
        centroid_observations++;
    }

    float compute_surprise(const Vector& emb) const {
        if (!centroid_initialized || centroid_observations < 3) return 0.5f;
        float cosine = context_centroid.cosine(emb);
        return 1.0f - std::max(-1.0f, std::min(1.0f, cosine));
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
        if (config_.path == ":memory:") {
            // DuckDB in-memory — no path modification
        } else if (std::filesystem::is_directory(config_.path)) {
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

        // Surprise-gated encoding
        if (resonance_config_.enable_surprise_gating) {
            float surprise = session_context_.compute_surprise(artha.nu);
            float t = std::clamp((surprise - resonance_config_.surprise_low_threshold) /
                                 (resonance_config_.surprise_high_threshold - resonance_config_.surprise_low_threshold),
                                 0.0f, 1.0f);
            confidence = std::min(1.0f, confidence + t * resonance_config_.surprise_confidence_boost);
            session_context_.update_centroid(artha.nu, resonance_config_.surprise_centroid_alpha);
        }

        // Reconsolidation: if a labile memory is similar enough, update it
        if (resonance_config_.enable_reconsolidation && !artha.nu.is_zero()) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            session_context_.prune_labile(now, resonance_config_.labile_window_ms);
            for (const auto& lm : session_context_.labile_memories) {
                float sim = lm.embedding.cosine(artha.nu);
                if (sim >= resonance_config_.reconsolidation_threshold) {
                    store_.strengthen(lm.memory_id, config_.reinforce_amount);
                    store_.touch(lm.memory_id);
                    return int64_to_nodeid(lm.memory_id);
                }
            }
        }

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

        // Generate and store SDR for pattern matching
        if (!artha.nu.data.empty()) {
            auto sdr = SparseVector::from_dense(artha.nu.data);
            store_.store_sdr(id, sdr.serialize());
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
    std::vector<Recall> recall(const std::string& query, size_t limit = 10, bool separation_mode = false) {
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

        // Write phase: use batched operations for MVCC efficiency
        // Updates are coalesced and flushed periodically (every 30s or 100 updates)
        {
            std::unique_lock lock(mutex_);
            for (const auto& recall : recalls) {
                int64_t id = nodeid_to_int64(recall.id);
                store_.touch_batched(id);
                store_.strengthen_batched(id, config_.reinforce_amount);
            }
            // Auto-flush if threshold reached
            if (store_.should_flush()) {
                store_.flush_pending_updates();
            }
        }

        // Pattern separation: MMR reranking for maximally diverse results
        if (separation_mode && recalls.size() > 1) {
            recalls = mmr_rerank(std::move(recalls), limit, 0.3f);
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

            // Assign to theme if themes exist
            {
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
    // MMR (Maximal Marginal Relevance) reranking for pattern separation.
    // Selects results that are relevant to the query but maximally diverse from each other.
    // Uses SDR IoU for inter-result similarity (orthogonal to cosine).
    // lambda: 0=max diversity, 1=max relevance. 0.3 = separation mode default.
    std::vector<Recall> mmr_rerank(std::vector<Recall> candidates,
                                    size_t k,
                                    float lambda = 0.3f) {
        if (candidates.size() <= 1) return candidates;

        // Load SDRs for all candidates
        std::vector<SparseVector> sdrs;
        sdrs.reserve(candidates.size());
        for (const auto& c : candidates) {
            int64_t mem_id = static_cast<int64_t>(c.id.low);
            std::string sdr_str = store_.get_sdr(mem_id);
            sdrs.push_back(SparseVector::deserialize(sdr_str));
        }

        std::vector<Recall> selected;
        std::vector<bool> chosen(candidates.size(), false);
        selected.reserve(std::min(k, candidates.size()));

        while (selected.size() < k && selected.size() < candidates.size()) {
            float best_score = -1e9f;
            size_t best_idx = 0;
            bool found = false;

            for (size_t j = 0; j < candidates.size(); j++) {
                if (chosen[j]) continue;

                float relevance = candidates[j].relevance;
                float max_sim = 0.0f;

                for (size_t si = 0; si < selected.size(); si++) {
                    // Use SDR IoU for inter-result similarity
                    size_t sel_orig_idx = 0;
                    for (size_t idx = 0; idx < candidates.size(); idx++) {
                        if (candidates[idx].id == selected[si].id) {
                            sel_orig_idx = idx;
                            break;
                        }
                    }
                    float sim = sdrs[j].iou(sdrs[sel_orig_idx]);
                    max_sim = std::max(max_sim, sim);
                }

                float score = lambda * relevance - (1.0f - lambda) * max_sim;
                if (!found || score > best_score) {
                    best_score = score;
                    best_idx = j;
                    found = true;
                }
            }

            if (!found) break;
            chosen[best_idx] = true;
            selected.push_back(std::move(candidates[best_idx]));
        }
        return selected;
    }

    std::vector<Recall> full_resonate(const std::string& query, size_t k = 10,
                                        const std::vector<std::string>& exclude_kinds = {},
                                        bool separation_mode = false) {
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

            // Phase 6b: Mark recalled memories as labile for reconsolidation
            if (active_config.enable_reconsolidation && !results.empty()) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                session_context_.prune_labile(now, active_config.labile_window_ms);
                size_t to_mark = std::min(results.size(), active_config.max_labile_tracked);
                for (size_t i = 0; i < to_mark; ++i) {
                    int64_t mem_id = static_cast<int64_t>(results[i].id.low);
                    auto emb_opt = store_.get_memory_embedding(mem_id);
                    if (emb_opt && emb_opt->size() == EMBED_DIM) {
                        session_context_.mark_labile(mem_id, now, Vector(std::move(*emb_opt)));
                    }
                }
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

        // Pattern separation: MMR reranking for maximally diverse results
        if (separation_mode && results.size() > 1) {
            results = mmr_rerank(std::move(results), k, 0.3f);
        }

        return results;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // GLOBAL WORKSPACE THEORY (GWT) RETRIEVAL
    // ═══════════════════════════════════════════════════════════════════════

    // Two-phase GWT recall: broad retrieval → salience competition → focused expansion
    // Phase 1: Broad candidate retrieval via full_resonate (2x k)
    // Phase 2: Salience scoring (relevance × priority × recency) → winner-take-all
    // Phase 3: One-hop graph expansion from winner for focused context
    std::vector<Recall> gwt_recall(const std::string& query, size_t k = 10,
                                    const std::string& realm = "",
                                    bool separation_mode = false) {
        // Phase 1: Broad retrieval (2x candidates)
        auto candidates = full_resonate(query, k * 2, {}, separation_mode);
        if (candidates.empty()) return {};

        // Phase 2: Salience competition — score each candidate
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        for (auto& c : candidates) {
            int64_t mem_id = static_cast<int64_t>(c.id.low);
            float priority_weight = 1.0f;
            float recency = 0.5f;

            auto mem = store_.get_memory(mem_id);
            if (mem) {
                // Priority boost: Background=1.0, Notable=1.3, Critical=1.6
                priority_weight = 1.0f + 0.3f * static_cast<float>(static_cast<uint8_t>(mem->priority_tier));
                // Recency decay: half-life of 7 days
                if (mem->accessed_at > 0) {
                    float age_days = static_cast<float>(now - mem->accessed_at) / (86400.0f * 1000.0f);
                    recency = std::exp(-age_days / 7.0f);
                }
            }

            c.salience = c.relevance * priority_weight * (0.5f + 0.5f * recency);
        }

        // Winner-take-all: highest salience wins the broadcast
        auto winner_it = std::max_element(candidates.begin(), candidates.end(),
                                          [](const Recall& a, const Recall& b) {
                                              return a.salience < b.salience;
                                          });
        winner_it->broadcast = true;

        // Phase 3: Focused one-hop expansion from winner via spreading activation
        {
            std::shared_lock lock(mutex_);

            // Extract terms from winner's text for graph traversal
            auto winner_terms = extract_terms_unlocked(winner_it->text);

            // Build activation map seeded from winner
            std::unordered_map<int64_t, float> expansion_activation;
            int64_t winner_mem_id = static_cast<int64_t>(winner_it->id.low);
            expansion_activation[winner_mem_id] = 1.0f;

            // One-hop config: single hop, strong signal
            DuckDBResonanceConfig one_hop_config = resonance_config_;
            one_hop_config.max_hops = 1;
            one_hop_config.spread_strength = 0.8f;
            one_hop_config.spread_decay = 0.6f;

            spread_activation_unlocked(winner_terms, expansion_activation, one_hop_config);

            // Collect existing candidate IDs for dedup
            std::unordered_set<int64_t> seen;
            for (const auto& c : candidates) {
                seen.insert(static_cast<int64_t>(c.id.low));
            }

            // Add activated neighbors not already in candidates
            for (const auto& [id, act] : expansion_activation) {
                if (seen.count(id) || act < 0.1f) continue;

                auto mem_opt = store_.get_memory(id);
                if (!mem_opt) continue;

                Recall recall;
                recall.id = int64_to_nodeid(id);
                recall.text = mem_opt->content;
                recall.type = string_to_node_type(mem_opt->kind);
                recall.confidence = Confidence(mem_opt->confidence);
                recall.relevance = act * 0.5f;  // Expansion results get dampened relevance
                recall.salience = recall.relevance;
                candidates.push_back(std::move(recall));
                seen.insert(id);
            }
        }

        // Sort: broadcast winner first, then by salience
        std::sort(candidates.begin(), candidates.end(),
                  [](const Recall& a, const Recall& b) {
                      if (a.broadcast != b.broadcast) return a.broadcast > b.broadcast;
                      return a.salience > b.salience;
                  });

        // Return top k
        if (candidates.size() > k) candidates.resize(k);
        return candidates;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // xMemory THEME-BASED RETRIEVAL
    // ═══════════════════════════════════════════════════════════════════════

    // Two-stage theme recall (xMemory architecture)
    // ThemeManager removed; theme_recall is now routed through ChittaFieldHandler.
    std::vector<Recall> theme_recall(const std::string& query, size_t k = 10,
                                      const std::string& realm = "",
                                      const std::vector<std::string>& exclude_kinds = {}) {
        return {};
    }

    // Get theme retrieval results (full detail with theme info)
    // ThemeManager removed; returns empty. Use ChittaFieldHandler::theme_recall instead.
    std::vector<ThemeResult> theme_recall_detailed(const std::string& query,
                                                    size_t max_themes = 5,
                                                    const std::string& realm = "") {
        return {};
    }

    // Assign a memory to a theme (manual override)
    bool assign_to_theme(NodeId memory_id, int64_t theme_id) {
        std::unique_lock lock(mutex_);
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
    // ThemeManager removed; returns empty result. Theme maintenance is now via
    // ChittaFieldHandler::handle_theme_maintain (FieldStore::theme_maintain).
    DuckDBStore::ThemeMaintenanceResult run_theme_maintenance(const std::string& realm = "") {
        return {};
    }

    // Access to narrative engine
    NarrativeEngine* narrative() { return narrative_engine_.get(); }
    const NarrativeEngine* narrative() const { return narrative_engine_.get(); }

    // Access to anticipator
    Anticipator* anticipator() { return anticipator_.get(); }
    const Anticipator* anticipator() const { return anticipator_.get(); }

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

#ifdef CHITTA_FIELD_AVAILABLE
    // Wire up a FieldStore so learner state is persisted via chitta-field events.
    void set_field_store(FieldStore* fs) {
        field_store_ = fs;
        if (anticipator_) anticipator_->set_field_store(fs);
    }
#endif

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
#ifdef CHITTA_FIELD_AVAILABLE
        if (field_store_) {
            using namespace std::chrono;
            int64_t now_ms = duration_cast<milliseconds>(
                system_clock::now().time_since_epoch()).count();
            return field_store_->user_model_upsert(
                "learner_state_global", "resonance_state", state, now_ms) == 0;
        }
#endif
        // Fallback: persist via DuckDB memory node
        int64_t id = store_.remember(
            "[LEARNER_STATE] " + state,
            "system",
            {},
            1.0f,
            0.0f,
            "brahman",
            RealmVisibility::Private
        );
        return id > 0;
    }

    bool load_learner_state_unlocked() {
#ifdef CHITTA_FIELD_AVAILABLE
        if (field_store_) {
            auto payload = field_store_->get_latest_event(
                "user_model", "resonance_state", "learner_state_global");
            if (payload) {
                return learner_.deserialize(*payload);
            }
            return false;
        }
#endif
        // Fallback: scan DuckDB system nodes for [LEARNER_STATE] tag
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
#ifdef CHITTA_FIELD_AVAILABLE
    FieldStore* field_store_ = nullptr;
#endif

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
