#pragma once
// Mind: the unified API for soul storage
//
// High-level interface that:
// - Manages tiered storage transparently
// - Provides semantic search across all tiers
// - Handles decay and coherence autonomously
// - Supports checkpointing and recovery
// - Integrates with VakYantra for text→embedding

#include "types.hpp"
#include "graph.hpp"
#include "storage.hpp"
#include "dynamics.hpp"
#include "voice.hpp"
#include "vak.hpp"
#include "scoring.hpp"
#include "daemon.hpp"
#include "feedback.hpp"
#include <mutex>
#include <atomic>

namespace synapse {

// Mind configuration
struct MindConfig {
    std::string path;                    // Base path for storage files
    size_t hot_capacity = 10000;         // Max nodes in RAM
    size_t warm_capacity = 100000;       // Max nodes in mmap
    int64_t hot_age_ms = 86400000;       // 1 day until warm
    int64_t warm_age_ms = 604800000;     // 7 days until cold
    int64_t decay_interval_ms = 3600000; // 1 hour between decay
    int64_t checkpoint_interval_ms = 300000;  // 5 minutes between checkpoints
    float prune_threshold = 0.1f;        // Confidence below this = prune
};

// Search result with meaning
struct Recall {
    NodeId id;
    float similarity;      // Raw semantic similarity
    float relevance;       // Soul-aware relevance score
    NodeType type;
    Confidence confidence;
    Timestamp created;
    Timestamp accessed;
    std::vector<uint8_t> payload;
    std::string text;  // Original text if available
};

// Search mode for hybrid retrieval
enum class SearchMode {
    Dense,      // Semantic only (fast)
    Sparse,     // BM25 only (keyword)
    Hybrid      // Dense + Sparse with RRF fusion
};

// Mind state for persistence
struct MindState {
    uint64_t snapshot_id;
    Coherence coherence;
    Timestamp last_decay;
    Timestamp last_checkpoint;
    size_t total_nodes;
    size_t hot_nodes;
    size_t warm_nodes;
    size_t cold_nodes;
    bool yantra_ready;
};

// The Mind: unified interface to soul storage
class Mind {
public:
    explicit Mind(MindConfig config)
        : config_(std::move(config))
        , storage_({
            config_.path,
            config_.hot_capacity,
            config_.warm_capacity,
            config_.hot_age_ms,
            config_.warm_age_ms
          })
        , dynamics_()
        , yantra_(std::make_shared<ShantaYantra>())  // Silent by default
        , running_(false)
    {
        dynamics_.with_defaults();
    }

    // Attach a VakYantra for text→embedding transformation
    void attach_yantra(std::shared_ptr<VakYantra> yantra) {
        std::lock_guard<std::mutex> lock(mutex_);
        yantra_ = std::move(yantra);
    }

    // Check if yantra is ready for embeddings
    bool has_yantra() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return yantra_ && yantra_->ready();
    }

    // Initialize or load existing mind
    bool open() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!storage_.initialize()) return false;
        running_ = true;

        // Rebuild BM25 index from existing data
        rebuild_bm25_index();

        return true;
    }

    // Rebuild BM25 index from storage (call after loading data)
    void rebuild_bm25_index() {
        storage_.for_each_hot([this](const NodeId& id, const Node& node) {
            auto text = payload_to_text(node.payload);
            if (text) {
                bm25_index_.add(id, *text);
            }
        });
    }

    // Close and persist
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        storage_.sync();
    }

    // ═══════════════════════════════════════════════════════════════════
    // Text-based API (requires VakYantra)
    // ═══════════════════════════════════════════════════════════════════

    // Remember text: transform to embedding and store
    NodeId remember(const std::string& text, NodeType type = NodeType::Wisdom) {
        std::lock_guard<std::mutex> lock(mutex_);

        Artha artha = yantra_->transform(text);

        Node node(type, std::move(artha.nu));
        node.payload = text_to_payload(text);
        NodeId id = node.id;

        storage_.insert(id, std::move(node));
        graph_.insert_raw(id);

        // Add to BM25 index for hybrid search
        bm25_index_.add(id, text);

        return id;
    }

    // Remember with explicit confidence
    NodeId remember(const std::string& text, NodeType type, Confidence confidence) {
        std::lock_guard<std::mutex> lock(mutex_);

        Artha artha = yantra_->transform(text);

        Node node(type, std::move(artha.nu));
        node.kappa = confidence;
        node.payload = text_to_payload(text);
        NodeId id = node.id;

        storage_.insert(id, std::move(node));
        graph_.insert_raw(id);

        // Add to BM25 index for hybrid search
        bm25_index_.add(id, text);

        return id;
    }

    // Recall by text query with soul-aware scoring
    std::vector<Recall> recall(const std::string& query, size_t k,
                               float threshold = 0.0f,
                               SearchMode mode = SearchMode::Hybrid) {
        std::lock_guard<std::mutex> lock(mutex_);

        Artha artha = yantra_->transform(query);
        return recall_impl(artha.nu, query, k, threshold, mode);
    }

    // Remember batch (more efficient)
    std::vector<NodeId> remember_batch(const std::vector<std::string>& texts,
                                       NodeType type = NodeType::Wisdom) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto arthas = yantra_->transform_batch(texts);

        std::vector<NodeId> ids;
        ids.reserve(texts.size());

        for (size_t i = 0; i < texts.size(); ++i) {
            Node node(type, std::move(arthas[i].nu));
            node.payload = text_to_payload(texts[i]);
            NodeId id = node.id;
            ids.push_back(id);

            storage_.insert(id, std::move(node));
            graph_.insert_raw(id);

            // Add to BM25 index for hybrid search
            bm25_index_.add(id, texts[i]);
        }

        return ids;
    }

    // ═══════════════════════════════════════════════════════════════════
    // Vector-based API (direct embeddings)
    // ═══════════════════════════════════════════════════════════════════

    // Remember: store a node with pre-computed embedding
    NodeId remember(NodeType type, Vector embedding,
                    std::vector<uint8_t> payload = {})
    {
        std::lock_guard<std::mutex> lock(mutex_);

        Node node(type, std::move(embedding));
        node.payload = std::move(payload);
        NodeId id = node.id;

        storage_.insert(id, std::move(node));
        graph_.insert_raw(id);

        return id;
    }

    // Remember with explicit confidence
    NodeId remember(NodeType type, Vector embedding, Confidence confidence,
                    std::vector<uint8_t> payload = {})
    {
        std::lock_guard<std::mutex> lock(mutex_);

        Node node(type, std::move(embedding));
        node.kappa = confidence;
        node.payload = std::move(payload);
        NodeId id = node.id;

        storage_.insert(id, std::move(node));
        graph_.insert_raw(id);

        return id;
    }

    // Recall: semantic search with pre-computed query vector
    std::vector<Recall> recall(const Vector& query, size_t k,
                               float threshold = 0.0f) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return recall_impl(query, "", k, threshold, SearchMode::Dense);
    }

    // ═══════════════════════════════════════════════════════════════════
    // Node operations
    // ═══════════════════════════════════════════════════════════════════

    // Get a specific node
    std::optional<Node> get(NodeId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (Node* node = storage_.get(id)) {
            return *node;
        }
        return std::nullopt;
    }

    // Get text from a node (if stored as payload)
    std::optional<std::string> text(NodeId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (Node* node = storage_.get(id)) {
            return payload_to_text(node->payload);
        }
        return std::nullopt;
    }

    // Strengthen: increase confidence
    void strengthen(NodeId id, float delta = 0.1f) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (Node* node = storage_.get(id)) {
            node->kappa.observe(node->kappa.mu + delta);
        }
    }

    // Weaken: decrease confidence
    void weaken(NodeId id, float delta = 0.1f) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (Node* node = storage_.get(id)) {
            node->kappa.observe(node->kappa.mu - delta);
        }
    }

    // Connect: create edge between nodes
    void connect(NodeId from, NodeId to, EdgeType type, float weight = 1.0f) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (Node* node = storage_.get(from)) {
            node->connect(to, type, weight);
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // Dynamics and lifecycle
    // ═══════════════════════════════════════════════════════════════════

    // Tick: run one cycle of dynamics
    DynamicsReport tick() {
        std::lock_guard<std::mutex> lock(mutex_);

        // Run dynamics on graph
        DynamicsReport report = dynamics_.tick(graph_);

        // Manage storage tiers
        storage_.manage_tiers();

        // Checkpoint if needed
        Timestamp current = now();
        if (current - last_checkpoint_ > config_.checkpoint_interval_ms) {
            storage_.sync();
            last_checkpoint_ = current;
        }

        return report;
    }

    // Query by type
    std::vector<NodeId> by_type(NodeType type, size_t limit = 100) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<NodeId> results;
        storage_.for_each_hot([&](const NodeId& id, const Node& node) {
            if (node.node_type == type && results.size() < limit) {
                results.push_back(id);
            }
        });
        return results;
    }

    // Compute coherence
    Coherence coherence() {
        std::lock_guard<std::mutex> lock(mutex_);
        return graph_.compute_coherence();
    }

    // Snapshot for recovery
    uint64_t snapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        storage_.sync();
        return graph_.snapshot();
    }

    // Get current state
    MindState state() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return MindState{
            graph_.current_snapshot(),
            graph_.coherence(),
            last_decay_,
            last_checkpoint_,
            storage_.total_size(),
            storage_.hot_size(),
            storage_.warm_size(),
            storage_.cold_size(),
            yantra_ && yantra_->ready()
        };
    }

    // Access chorus for multi-voice reasoning
    HarmonyReport harmonize(const std::vector<Voice>& voices) {
        std::lock_guard<std::mutex> lock(mutex_);
        Chorus chorus(voices);
        return chorus.harmonize(graph_);
    }

    // Statistics
    size_t size() const { return storage_.total_size(); }
    size_t hot_size() const { return storage_.hot_size(); }
    size_t warm_size() const { return storage_.warm_size(); }
    size_t cold_size() const { return storage_.cold_size(); }

    // ═══════════════════════════════════════════════════════════════════
    // Autonomous dynamics (daemon)
    // ═══════════════════════════════════════════════════════════════════

    // Start the background daemon
    void start_daemon(DaemonConfig config = {}) {
        daemon_ = Daemon(config);
        daemon_.attach(&graph_);
        daemon_.on_save([this]() {
            std::lock_guard<std::mutex> lock(mutex_);
            storage_.sync();
        });
        daemon_.start();
    }

    // Stop the background daemon
    void stop_daemon() {
        daemon_.stop();
    }

    // Check if daemon is running
    bool daemon_running() const {
        return daemon_.is_running();
    }

    // Get daemon stats
    Daemon::Stats daemon_stats() const {
        return daemon_.stats();
    }

    // ═══════════════════════════════════════════════════════════════════
    // Learning feedback
    // ═══════════════════════════════════════════════════════════════════

    // Record that a memory was accessed
    void feedback_used(NodeId id) {
        feedback_.used(id);
    }

    // Record that a memory was helpful (led to success)
    void feedback_helpful(NodeId id, const std::string& context = "") {
        feedback_.helpful(id, context);
    }

    // Record that a memory was misleading (led to correction)
    void feedback_misleading(NodeId id, const std::string& context = "") {
        feedback_.misleading(id, context);
    }

    // Apply pending feedback to node confidences
    size_t apply_feedback() {
        std::lock_guard<std::mutex> lock(mutex_);

        auto deltas = feedback_.process_pending();
        size_t applied = 0;

        for (const auto& [id, delta] : deltas) {
            if (Node* node = storage_.get(id)) {
                // Apply delta via Bayesian update
                float new_mu = std::clamp(node->kappa.mu + delta, 0.0f, 1.0f);
                node->kappa.observe(new_mu);
                applied++;
            }
        }

        return applied;
    }

    // Get feedback stats for a node
    std::optional<FeedbackTracker::NodeStats> feedback_stats(NodeId id) const {
        return feedback_.get_stats(id);
    }

    // Get count of pending feedback
    size_t pending_feedback() const {
        return feedback_.pending_count();
    }

private:
    // Internal recall implementation with soul-aware scoring
    std::vector<Recall> recall_impl(const Vector& query, const std::string& query_text,
                                    size_t k, float threshold, SearchMode mode) const
    {
        Timestamp current = now();
        std::vector<std::pair<NodeId, float>> candidates;

        // Get candidates based on search mode
        if (mode == SearchMode::Dense || mode == SearchMode::Hybrid) {
            QuantizedVector qquery = QuantizedVector::from_float(query);
            auto dense = storage_.search(qquery, k * 4);  // Get extra for fusion/filtering
            candidates = dense;
        }

        if (mode == SearchMode::Sparse || mode == SearchMode::Hybrid) {
            if (!query_text.empty()) {
                auto sparse = bm25_index_.search(query_text, k * 4);

                if (mode == SearchMode::Hybrid && !candidates.empty()) {
                    // RRF fusion of dense and sparse results
                    candidates = rrf_fusion(candidates, sparse, 60.0f, 0.7f);
                } else if (mode == SearchMode::Sparse) {
                    candidates = sparse;
                }
            }
        }

        // Score candidates with soul-aware relevance
        std::vector<Recall> results;
        for (const auto& [id, base_score] : candidates) {
            if (Node* node = const_cast<TieredStorage&>(storage_).get(id)) {
                // Get semantic similarity (for dense) or use base score (for sparse)
                float similarity = base_score;
                if (mode == SearchMode::Hybrid) {
                    // For hybrid, base_score is RRF score, not similarity
                    // Compute actual similarity if we have the embedding
                    QuantizedVector qquery = QuantizedVector::from_float(query);
                    QuantizedVector qnode = QuantizedVector::from_float(node->nu);
                    similarity = qquery.cosine_approx(qnode);
                }

                if (similarity < threshold) continue;

                // Soul-aware relevance scoring
                float relevance = soul_relevance(similarity, *node, current, scoring_config_);

                Recall r;
                r.id = id;
                r.similarity = similarity;
                r.relevance = relevance;
                r.type = node->node_type;
                r.confidence = node->kappa;
                r.created = node->tau_created;
                r.accessed = node->tau_accessed;
                r.payload = node->payload;
                r.text = payload_to_text(node->payload).value_or("");
                results.push_back(std::move(r));
            }
        }

        // Sort by relevance (soul-aware), not raw similarity
        std::sort(results.begin(), results.end(),
            [](const Recall& a, const Recall& b) {
                return a.relevance > b.relevance;
            });

        // Limit to k results
        if (results.size() > k) {
            results.resize(k);
        }

        return results;
    }

    // Convert text to payload (simple UTF-8 encoding)
    static std::vector<uint8_t> text_to_payload(const std::string& text) {
        return std::vector<uint8_t>(text.begin(), text.end());
    }

    // Convert payload back to text
    static std::optional<std::string> payload_to_text(const std::vector<uint8_t>& payload) {
        if (payload.empty()) return std::nullopt;
        return std::string(payload.begin(), payload.end());
    }

    MindConfig config_;
    mutable std::mutex mutex_;
    TieredStorage storage_;
    Graph graph_;
    Dynamics dynamics_;
    std::shared_ptr<VakYantra> yantra_;
    std::atomic<bool> running_;
    Timestamp last_decay_ = 0;
    Timestamp last_checkpoint_ = 0;

    // Soul-aware scoring and hybrid retrieval
    ScoringConfig scoring_config_;
    BM25Index bm25_index_;
    CrossEncoder cross_encoder_;

    // Autonomous dynamics and learning
    Daemon daemon_;
    FeedbackTracker feedback_;
};

} // namespace synapse
