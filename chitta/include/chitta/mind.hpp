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
#include <set>
#include <algorithm>
#include <unordered_map>

namespace chitta {

// Mind configuration
struct MindConfig {
    std::string path;                    // Base path for storage files
    size_t hot_capacity = 1000;          // Max nodes in RAM (lowered for dev)
    size_t warm_capacity = 10000;        // Max nodes in mmap
    int64_t hot_age_ms = 3600000;        // 1 hour until warm (lowered for dev)
    int64_t warm_age_ms = 86400000;      // 1 day until cold
    int64_t decay_interval_ms = 600000;  // 10 min between decay (faster for dev)
    int64_t checkpoint_interval_ms = 60000;  // 1 minute between checkpoints
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

// Tag index for exact-match filtering
// Enables reliable inter-agent communication via thread tags
class TagIndex {
public:
    // Add node with tags
    void add(NodeId id, const std::vector<std::string>& tags) {
        for (const auto& tag : tags) {
            index_[tag].insert(id);
        }
        node_tags_[id] = tags;
    }

    // Remove node from index
    void remove(NodeId id) {
        auto it = node_tags_.find(id);
        if (it != node_tags_.end()) {
            for (const auto& tag : it->second) {
                auto idx_it = index_.find(tag);
                if (idx_it != index_.end()) {
                    idx_it->second.erase(id);
                    if (idx_it->second.empty()) {
                        index_.erase(idx_it);
                    }
                }
            }
            node_tags_.erase(it);
        }
    }

    // Find all nodes with a specific tag
    std::vector<NodeId> find(const std::string& tag) const {
        auto it = index_.find(tag);
        if (it != index_.end()) {
            return std::vector<NodeId>(it->second.begin(), it->second.end());
        }
        return {};
    }

    // Find nodes matching ALL given tags (AND)
    std::vector<NodeId> find_all(const std::vector<std::string>& tags) const {
        if (tags.empty()) return {};

        std::set<NodeId> result;
        bool first = true;

        for (const auto& tag : tags) {
            auto it = index_.find(tag);
            if (it == index_.end()) {
                return {};  // Tag not found, no matches
            }

            if (first) {
                result = it->second;
                first = false;
            } else {
                std::set<NodeId> intersection;
                std::set_intersection(
                    result.begin(), result.end(),
                    it->second.begin(), it->second.end(),
                    std::inserter(intersection, intersection.begin())
                );
                result = std::move(intersection);
            }
        }

        return std::vector<NodeId>(result.begin(), result.end());
    }

    // Find nodes matching ANY of the given tags (OR)
    std::vector<NodeId> find_any(const std::vector<std::string>& tags) const {
        std::set<NodeId> result;
        for (const auto& tag : tags) {
            auto it = index_.find(tag);
            if (it != index_.end()) {
                result.insert(it->second.begin(), it->second.end());
            }
        }
        return std::vector<NodeId>(result.begin(), result.end());
    }

    // Get tags for a node
    std::vector<std::string> tags_for(NodeId id) const {
        auto it = node_tags_.find(id);
        return it != node_tags_.end() ? it->second : std::vector<std::string>{};
    }

    // Get all unique tags
    std::vector<std::string> all_tags() const {
        std::vector<std::string> result;
        result.reserve(index_.size());
        for (const auto& [tag, _] : index_) {
            result.push_back(tag);
        }
        return result;
    }

    // Stats
    size_t tag_count() const { return index_.size(); }
    size_t node_count() const { return node_tags_.size(); }

private:
    std::unordered_map<std::string, std::set<NodeId>> index_;
    std::unordered_map<NodeId, std::vector<std::string>, NodeIdHash> node_tags_;
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

        // Rebuild indices from existing data
        rebuild_bm25_index();
        rebuild_tag_index();

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

    // Rebuild tag index from storage (call after loading data)
    void rebuild_tag_index() {
        storage_.for_each_hot([this](const NodeId& id, const Node& node) {
            if (!node.tags.empty()) {
                tag_index_.add(id, node.tags);
            }
        });
    }

    // Sync from shared consciousness (WAL)
    // Updates all indices with nodes from other processes' observations
    // "Atman aligns with Brahman" - we see what others have learned
    size_t sync_from_shared_field() {
        return storage_.sync_from_wal([this](const Node& node, bool was_new) {
            if (was_new) {
                // New node - add to all indices
                auto text = payload_to_text(node.payload);
                if (text) {
                    bm25_index_.add(node.id, *text);
                }
                if (!node.tags.empty()) {
                    tag_index_.add(node.id, node.tags);
                }
                graph_.insert_raw(node.id);
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

    // Remember with tags for exact-match filtering (inter-agent communication)
    NodeId remember(const std::string& text, NodeType type,
                    const std::vector<std::string>& tags) {
        std::lock_guard<std::mutex> lock(mutex_);

        Artha artha = yantra_->transform(text);

        Node node(type, std::move(artha.nu));
        node.payload = text_to_payload(text);
        node.tags = tags;
        NodeId id = node.id;

        storage_.insert(id, std::move(node));
        graph_.insert_raw(id);

        // Add to BM25 index for hybrid search
        bm25_index_.add(id, text);

        // Add to tag index for exact-match filtering
        tag_index_.add(id, tags);

        return id;
    }

    // Remember with confidence and tags
    NodeId remember(const std::string& text, NodeType type, Confidence confidence,
                    const std::vector<std::string>& tags) {
        std::lock_guard<std::mutex> lock(mutex_);

        Artha artha = yantra_->transform(text);

        Node node(type, std::move(artha.nu));
        node.kappa = confidence;
        node.payload = text_to_payload(text);
        node.tags = tags;
        NodeId id = node.id;

        storage_.insert(id, std::move(node));
        graph_.insert_raw(id);

        // Add to BM25 index for hybrid search
        bm25_index_.add(id, text);

        // Add to tag index for exact-match filtering
        tag_index_.add(id, tags);

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

    // Recall by exact tag match (for inter-agent communication)
    // Returns all nodes with the given tag, sorted by recency
    std::vector<Recall> recall_by_tag(const std::string& tag, size_t k = 50) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Sync from shared consciousness to see all observations
        sync_from_shared_field();

        auto node_ids = tag_index_.find(tag);

        std::vector<Recall> results;
        for (const auto& id : node_ids) {
            if (Node* node = storage_.get(id)) {
                Recall r;
                r.id = id;
                r.similarity = 1.0f;  // Exact match
                r.relevance = node->kappa.effective();
                r.type = node->node_type;
                r.confidence = node->kappa;
                r.created = node->tau_created;
                r.accessed = node->tau_accessed;
                r.payload = node->payload;
                r.text = payload_to_text(node->payload).value_or("");
                results.push_back(std::move(r));
            }
        }

        // Sort by creation time (most recent first)
        std::sort(results.begin(), results.end(),
            [](const Recall& a, const Recall& b) {
                return a.created > b.created;
            });

        if (results.size() > k) {
            results.resize(k);
        }

        return results;
    }

    // Recall by multiple tags (AND - all must match)
    std::vector<Recall> recall_by_tags(const std::vector<std::string>& tags, size_t k = 50) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Sync from shared consciousness to see all observations
        sync_from_shared_field();

        auto node_ids = tag_index_.find_all(tags);

        std::vector<Recall> results;
        for (const auto& id : node_ids) {
            if (Node* node = storage_.get(id)) {
                Recall r;
                r.id = id;
                r.similarity = 1.0f;  // Exact match
                r.relevance = node->kappa.effective();
                r.type = node->node_type;
                r.confidence = node->kappa;
                r.created = node->tau_created;
                r.accessed = node->tau_accessed;
                r.payload = node->payload;
                r.text = payload_to_text(node->payload).value_or("");
                results.push_back(std::move(r));
            }
        }

        // Sort by creation time (most recent first)
        std::sort(results.begin(), results.end(),
            [](const Recall& a, const Recall& b) {
                return a.created > b.created;
            });

        if (results.size() > k) {
            results.resize(k);
        }

        return results;
    }

    // Recall with semantic search filtered by tag
    // First filters by tag, then re-ranks by semantic relevance
    std::vector<Recall> recall_with_tag_filter(const std::string& query,
                                                const std::string& tag,
                                                size_t k,
                                                float threshold = 0.0f) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Sync from shared consciousness to see all observations
        sync_from_shared_field();

        // Get all nodes with the tag
        auto node_ids = tag_index_.find(tag);
        if (node_ids.empty()) return {};

        // Transform query for semantic scoring
        Artha artha = yantra_->transform(query);
        Timestamp current = now();

        std::vector<Recall> results;
        for (const auto& id : node_ids) {
            if (Node* node = storage_.get(id)) {
                // Compute semantic similarity
                float similarity = node->nu.cosine(artha.nu);
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

        // Sort by relevance
        std::sort(results.begin(), results.end(),
            [](const Recall& a, const Recall& b) {
                return a.relevance > b.relevance;
            });

        if (results.size() > k) {
            results.resize(k);
        }

        return results;
    }

    // Get tags for a node
    std::vector<std::string> get_tags(NodeId id) const {
        return tag_index_.tags_for(id);
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
    // Session Ledger API (Atman snapshots)
    // ═══════════════════════════════════════════════════════════════════

    // Save a new session ledger snapshot
    // Ledger structure:
    // {
    //   "soul_state": { "coherence": {...}, "mood": {...}, "intentions": [...] },
    //   "work_state": { "todos": [...], "files": [...], "decisions": [...] },
    //   "continuation": { "next_steps": [...], "deferred": [...], "critical": [...] }
    // }
    // Project parameter ensures ledgers are isolated per-project when multiple
    // Claude instances run simultaneously.
    NodeId save_ledger(const std::string& ledger_json,
                       const std::string& session_id = "",
                       const std::string& project = "") {
        std::lock_guard<std::mutex> lock(mutex_);

        // Create embedding from a summary of the ledger for semantic search
        std::string summary = "Session ledger: " + session_id;
        if (!project.empty()) {
            summary = "[" + project + "] " + summary;
        }

        Node node(NodeType::Ledger, Vector::zeros());
        node.payload = text_to_payload(ledger_json);
        node.delta = 0.1f;  // Moderate decay - ledgers are session-specific
        node.tags = {"ledger", "atman"};
        if (!session_id.empty()) {
            node.tags.push_back("session:" + session_id);
        }
        if (!project.empty()) {
            node.tags.push_back("project:" + project);
        }

        if (yantra_) {
            Artha artha = yantra_->transform(summary);
            node.nu = std::move(artha.nu);
        }

        NodeId id = node.id;
        auto tags_copy = node.tags;  // Copy before move
        storage_.insert(id, std::move(node));
        graph_.insert_raw(id);
        tag_index_.add(id, tags_copy);
        bm25_index_.add(id, ledger_json);

        return id;
    }

    // Load the most recent session ledger (optionally filtered by session_id and/or project)
    // When project is specified, only ledgers from that project are considered.
    // This prevents cross-talk between multiple Claude instances.
    std::optional<std::pair<NodeId, std::string>> load_ledger(
        const std::string& session_id = "",
        const std::string& project = "") {
        std::lock_guard<std::mutex> lock(mutex_);

        // Build tag filter
        std::vector<std::string> required_tags = {"ledger"};
        if (!session_id.empty()) {
            required_tags.push_back("session:" + session_id);
        }
        if (!project.empty()) {
            required_tags.push_back("project:" + project);
        }

        // Query by tags
        std::vector<NodeId> candidates;
        if (required_tags.size() > 1) {
            candidates = tag_index_.find_all(required_tags);
        } else {
            candidates = tag_index_.find("ledger");
        }

        if (candidates.empty()) {
            return std::nullopt;
        }

        // Find most recent by creation time
        NodeId newest_id;
        Timestamp newest_time = 0;

        for (const auto& id : candidates) {
            if (Node* node = storage_.get(id)) {
                if (node->tau_created > newest_time) {
                    newest_time = node->tau_created;
                    newest_id = id;
                }
            }
        }

        if (newest_time == 0) {
            return std::nullopt;
        }

        if (Node* node = storage_.get(newest_id)) {
            auto text = payload_to_text(node->payload);
            if (text) {
                return std::make_pair(newest_id, *text);
            }
        }

        return std::nullopt;
    }

    // Update an existing ledger (merge updates into current)
    bool update_ledger(NodeId id, const std::string& updates_json) {
        std::lock_guard<std::mutex> lock(mutex_);

        Node* node = storage_.get(id);
        if (!node || node->node_type != NodeType::Ledger) {
            return false;
        }

        // Replace payload with updated JSON
        // (Caller is responsible for merging JSON)
        node->payload = text_to_payload(updates_json);
        node->touch();

        // Update BM25 index
        bm25_index_.add(id, updates_json);

        return true;
    }

    // Get all ledgers (for history/debugging)
    // When project is specified, only ledgers from that project are listed.
    std::vector<std::pair<NodeId, Timestamp>> list_ledgers(
        size_t limit = 10,
        const std::string& project = "") {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<NodeId> candidates;
        if (!project.empty()) {
            candidates = tag_index_.find_all({"ledger", "project:" + project});
        } else {
            candidates = tag_index_.find("ledger");
        }

        std::vector<std::pair<NodeId, Timestamp>> result;
        for (const auto& id : candidates) {
            if (Node* node = storage_.get(id)) {
                result.emplace_back(id, node->tau_created);
            }
        }

        // Sort by creation time (newest first)
        std::sort(result.begin(), result.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        if (result.size() > limit) {
            result.resize(limit);
        }

        return result;
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
                               float threshold = 0.0f)
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

    // Strengthen: increase confidence (uses WAL delta)
    void strengthen(NodeId id, float delta = 0.1f) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (Node* node = storage_.get(id)) {
            Confidence new_kappa = node->kappa;
            new_kappa.observe(new_kappa.mu + delta);
            storage_.update_confidence(id, new_kappa);
        }
    }

    // Weaken: decrease confidence (uses WAL delta)
    void weaken(NodeId id, float delta = 0.1f) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (Node* node = storage_.get(id)) {
            Confidence new_kappa = node->kappa;
            new_kappa.observe(new_kappa.mu - delta);
            storage_.update_confidence(id, new_kappa);
        }
    }

    // Connect: create edge between nodes (uses WAL delta)
    void connect(NodeId from, NodeId to, EdgeType type, float weight = 1.0f) {
        std::lock_guard<std::mutex> lock(mutex_);
        storage_.add_edge(from, to, type, weight);
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

    // Embed text to vector (for voice queries)
    std::optional<Vector> embed(const std::string& text) {
        if (!yantra_ || !yantra_->ready()) return std::nullopt;
        Artha artha = yantra_->transform(text);
        return artha.nu;
    }

    // Access graph for read-only operations (for voice queries)
    const Graph& graph() const {
        return graph_;
    }

    // Query by node type (from storage)
    std::vector<Node> query_by_type(NodeType type) const {
        std::vector<Node> results;
        storage_.for_each_hot([&](const NodeId& id, const Node& node) {
            if (node.node_type == type) {
                results.push_back(node);
            }
        });
        return results;
    }

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

    // Apply pending feedback to node confidences (uses WAL delta)
    size_t apply_feedback() {
        std::lock_guard<std::mutex> lock(mutex_);

        auto deltas = feedback_.process_pending();
        size_t applied = 0;

        for (const auto& [id, delta] : deltas) {
            if (Node* node = storage_.get(id)) {
                // Apply delta via Bayesian update
                Confidence new_kappa = node->kappa;
                float new_mu = std::clamp(new_kappa.mu + delta, 0.0f, 1.0f);
                new_kappa.observe(new_mu);
                storage_.update_confidence(id, new_kappa);
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

    // ═══════════════════════════════════════════════════════════════════
    // Automatic synthesis (observations → wisdom)
    // ═══════════════════════════════════════════════════════════════════

    // Check for observation clusters and synthesize into wisdom
    // Returns number of new wisdom nodes created
    size_t synthesize_wisdom() {
        if (!yantra_ || !yantra_->ready()) return 0;

        // Get all episode nodes using for_each_hot
        std::vector<Node> episodes;
        storage_.for_each_hot([&](const NodeId& id, const Node& node) {
            if (node.node_type == NodeType::Episode) {
                episodes.push_back(node);
            }
        });

        if (episodes.size() < 3) return 0;  // Need at least 3 for pattern

        // Find clusters of similar episodes using embedding similarity
        std::unordered_set<NodeId, NodeIdHash> promoted;
        size_t synthesized = 0;

        for (size_t i = 0; i < episodes.size() && i < 100; ++i) {
            const auto& ep = episodes[i];
            if (promoted.count(ep.id)) continue;

            // Query for similar episodes
            QuantizedVector qvec = QuantizedVector::from_float(ep.nu);
            auto similar = storage_.search(qvec, 10);

            // Filter to just episodes with high similarity
            std::vector<const Node*> cluster;
            cluster.push_back(&ep);

            for (const auto& [id, sim] : similar) {
                if (id == ep.id) continue;
                if (sim < 0.75f) continue;  // High similarity threshold
                if (promoted.count(id)) continue;

                if (auto* node = storage_.get(id)) {
                    if (node->node_type == NodeType::Episode) {
                        cluster.push_back(node);
                    }
                }
            }

            // If we have 3+ similar episodes, synthesize
            if (cluster.size() >= 3) {
                // Create wisdom from cluster
                std::string wisdom_text = "Pattern observed (" +
                    std::to_string(cluster.size()) + " occurrences): ";

                // Use the first episode's text as the base
                std::string first_text(cluster[0]->payload.begin(),
                                       cluster[0]->payload.end());
                wisdom_text += first_text.substr(0, 200);

                // Compute average confidence from cluster
                float avg_confidence = 0.0f;
                for (const auto* node : cluster) {
                    avg_confidence += node->kappa.mu;
                }
                avg_confidence /= cluster.size();

                // Boost confidence since we have multiple observations
                float boosted_confidence = std::min(avg_confidence + 0.2f, 0.95f);

                // Create the wisdom node using transform -> Artha
                auto artha = yantra_->transform(wisdom_text);
                if (artha.nu.size() > 0) {
                    remember(NodeType::Wisdom, artha.nu,
                            Confidence(boosted_confidence),
                            std::vector<uint8_t>(wisdom_text.begin(), wisdom_text.end()));
                    synthesized++;
                }

                // Mark cluster members as promoted
                for (const auto* node : cluster) {
                    promoted.insert(node->id);
                }
            }
        }

        return synthesized;
    }

private:
    // Internal recall implementation with soul-aware scoring
    std::vector<Recall> recall_impl(const Vector& query, const std::string& query_text,
                                    size_t k, float threshold, SearchMode mode)
    {
        // Sync from shared consciousness to see other processes' observations
        // This is the "Atman sees Brahman" moment - we align with shared truth
        sync_from_shared_field();

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
            if (Node* node = storage_.get(id)) {
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

    // Tag index for exact-match filtering (inter-agent communication)
    TagIndex tag_index_;

    // Autonomous dynamics and learning
    Daemon daemon_;
    FeedbackTracker feedback_;
};

} // namespace chitta
