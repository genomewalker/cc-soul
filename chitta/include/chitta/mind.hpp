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
        // BM25: try loading from disk, fall back to lazy rebuild on first search
        bm25_path_ = storage_.base_path() + ".bm25";
        if (bm25_index_.load(bm25_path_)) {
            bm25_built_ = true;
            std::cerr << "[Mind] Loaded BM25 index (" << bm25_index_.size() << " docs)\n";
        } else {
            bm25_built_ = false;  // Will rebuild lazily on first search
        }

        if (!storage_.use_unified()) {
            // Only rebuild tag index for non-unified storage
            // For unified, SlotTagIndex is already loaded and authoritative
            rebuild_tag_index();
        }

        return true;
    }

    // Ensure BM25 index is built (lazy initialization)
    // Skipped for large datasets (>1M nodes) - use dense search only
    static constexpr size_t BM25_MAX_NODES = 1000000;

    void ensure_bm25_index() {
        if (bm25_built_) return;
        if (storage_.total_size() > BM25_MAX_NODES) {
            // Too large for in-memory BM25 - skip and use dense search only
            bm25_built_ = true;  // Mark as "built" to prevent retry
            return;
        }
        rebuild_bm25_index();
        bm25_built_ = true;
    }

    // Add to BM25 only if already built (otherwise rebuild will include it)
    void maybe_add_bm25(NodeId id, const std::string& text) {
        if (bm25_built_) {
            bm25_index_.add(id, text);
        }
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
                    maybe_add_bm25(node.id, *text);
                }
                // For unified storage, tags are already in SlotTagIndex
                if (!storage_.use_unified() && !node.tags.empty()) {
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
        // Save BM25 index if built
        if (bm25_built_ && !bm25_path_.empty() && bm25_index_.size() > 0) {
            bm25_index_.save(bm25_path_);
        }
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
        maybe_add_bm25(id, text);

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
        maybe_add_bm25(id, text);

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
        maybe_add_bm25(id, text);

        // Add to tag index (unified storage handles this in insert)
        if (!storage_.use_unified()) {
            tag_index_.add(id, tags);
        }

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
        maybe_add_bm25(id, text);

        // Add to tag index (unified storage handles this in insert)
        if (!storage_.use_unified()) {
            tag_index_.add(id, tags);
        }

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

        // Use SlotTagIndex for unified storage, in-memory TagIndex otherwise
        auto node_ids = storage_.use_unified()
            ? storage_.find_by_tag(tag)
            : tag_index_.find(tag);

        std::vector<Recall> results;
        for (const auto& id : node_ids) {
            if (Node* node = storage_.get(id)) {
                // Skip nodes without text content
                auto text = payload_to_text(node->payload);
                if (!text || text->size() < 3) continue;  // Skip empty/corrupted payloads

                Recall r;
                r.id = id;
                r.similarity = 1.0f;  // Exact match
                r.relevance = node->kappa.effective();
                r.type = node->node_type;
                r.confidence = node->kappa;
                r.created = node->tau_created;
                r.accessed = node->tau_accessed;
                r.payload = node->payload;
                r.text = *text;
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

        auto node_ids = storage_.use_unified()
            ? storage_.find_by_tags(tags)
            : tag_index_.find_all(tags);

        std::vector<Recall> results;
        for (const auto& id : node_ids) {
            if (Node* node = storage_.get(id)) {
                // Skip nodes without text content
                auto text = payload_to_text(node->payload);
                if (!text || text->size() < 3) continue;  // Skip empty/corrupted payloads

                Recall r;
                r.id = id;
                r.similarity = 1.0f;  // Exact match
                r.relevance = node->kappa.effective();
                r.type = node->node_type;
                r.confidence = node->kappa;
                r.created = node->tau_created;
                r.accessed = node->tau_accessed;
                r.payload = node->payload;
                r.text = *text;
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
        auto node_ids = storage_.use_unified()
            ? storage_.find_by_tag(tag)
            : tag_index_.find(tag);
        if (node_ids.empty()) return {};

        // Transform query for semantic scoring
        Artha artha = yantra_->transform(query);
        Timestamp current = now();

        std::vector<Recall> results;
        for (const auto& id : node_ids) {
            if (Node* node = storage_.get(id)) {
                // Skip nodes without text content
                auto text = payload_to_text(node->payload);
                if (!text || text->size() < 3) continue;  // Skip empty/corrupted payloads

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
                r.text = *text;
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
        if (storage_.use_unified()) {
            return storage_.tags_for_node(id);
        }
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
            maybe_add_bm25(id, texts[i]);
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
        if (!storage_.use_unified()) {
            tag_index_.add(id, tags_copy);
        }
        maybe_add_bm25(id, ledger_json);

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

        // Query by tags (use SlotTagIndex for unified storage)
        std::vector<NodeId> candidates;
        if (storage_.use_unified()) {
            candidates = required_tags.size() > 1
                ? storage_.find_by_tags(required_tags)
                : storage_.find_by_tag("ledger");
        } else {
            candidates = required_tags.size() > 1
                ? tag_index_.find_all(required_tags)
                : tag_index_.find("ledger");
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
        maybe_add_bm25(id, updates_json);

        return true;
    }

    // Get all ledgers (for history/debugging)
    // When project is specified, only ledgers from that project are listed.
    std::vector<std::pair<NodeId, Timestamp>> list_ledgers(
        size_t limit = 10,
        const std::string& project = "") {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<NodeId> candidates;
        if (storage_.use_unified()) {
            candidates = !project.empty()
                ? storage_.find_by_tags({"ledger", "project:" + project})
                : storage_.find_by_tag("ledger");
        } else {
            candidates = !project.empty()
                ? tag_index_.find_all({"ledger", "project:" + project})
                : tag_index_.find("ledger");
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
        // Save BM25 index if built
        if (bm25_built_ && !bm25_path_.empty() && bm25_index_.size() > 0) {
            bm25_index_.save(bm25_path_);
        }
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
    // Hebbian Learning: "neurons that fire together wire together"
    // ═══════════════════════════════════════════════════════════════════

    // Strengthen edge between two nodes (create if doesn't exist)
    // When nodes are co-activated (retrieved together, spread together),
    // the connection between them strengthens
    void hebbian_strengthen(NodeId a, NodeId b, float strength = 0.1f) {
        std::lock_guard<std::mutex> lock(mutex_);
        hebbian_strengthen_impl(a, b, strength);
    }

    // Batch update: strengthen edges between all pairs of co-activated nodes
    // Call this when multiple nodes are retrieved together in a search
    void hebbian_update(const std::vector<NodeId>& co_activated, float strength = 0.05f) {
        if (co_activated.size() < 2) return;

        std::lock_guard<std::mutex> lock(mutex_);

        // For all pairs (i, j) where i < j, strengthen both directions
        // Bidirectional strengthening: if A activates with B, both directions learn
        for (size_t i = 0; i < co_activated.size(); ++i) {
            for (size_t j = i + 1; j < co_activated.size(); ++j) {
                hebbian_strengthen_impl(co_activated[i], co_activated[j], strength);
                hebbian_strengthen_impl(co_activated[j], co_activated[i], strength);
            }
        }
    }

    // Recall with Hebbian learning: retrieve memories and strengthen co-retrieval
    // Top results become more connected over time, enabling emergent associations
    std::vector<Recall> recall_with_learning(const std::string& query, size_t k,
                                              float threshold = 0.0f,
                                              SearchMode mode = SearchMode::Hybrid,
                                              float hebbian_strength = 0.05f,
                                              size_t hebbian_top_k = 5) {
        auto results = recall(query, k, threshold, mode);

        // Apply Hebbian learning to top results
        if (results.size() >= 2 && hebbian_top_k > 0) {
            std::vector<NodeId> co_activated;
            size_t learn_count = std::min(results.size(), hebbian_top_k);
            co_activated.reserve(learn_count);
            for (size_t i = 0; i < learn_count; ++i) {
                co_activated.push_back(results[i].id);
            }
            hebbian_update(co_activated, hebbian_strength);
        }

        return results;
    }

    // Resonate with Hebbian learning: spreading activation + connection strengthening
    std::vector<Recall> resonate_with_learning(const std::string& query, size_t k = 10,
                                                float spread_strength = 0.5f,
                                                float hebbian_strength = 0.03f) {
        auto results = resonate(query, k, spread_strength);

        // Apply Hebbian learning to resonant results
        if (results.size() >= 2) {
            std::vector<NodeId> co_activated;
            co_activated.reserve(results.size());
            for (const auto& r : results) {
                co_activated.push_back(r.id);
            }
            hebbian_update(co_activated, hebbian_strength);
        }

        return results;
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

    // ═══════════════════════════════════════════════════════════════════
    // Resonance: Spreading Activation (Phase 1 of resonance architecture)
    // ═══════════════════════════════════════════════════════════════════

    // Activate a seed node and spread activation through graph edges
    // Returns nodes sorted by activation level (strongest first)
    std::vector<std::pair<NodeId, float>> spread_activation(
        const NodeId& seed,
        float initial_strength = 1.0f,
        float decay_factor = 0.5f,
        int max_hops = 3)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Activation map: node -> accumulated activation
        std::unordered_map<NodeId, float, NodeIdHash> activation;

        // BFS with decaying activation
        std::queue<std::tuple<NodeId, float, int>> frontier;
        frontier.push({seed, initial_strength, 0});
        activation[seed] = initial_strength;

        while (!frontier.empty()) {
            auto [current_id, strength, hop] = frontier.front();
            frontier.pop();

            if (hop >= max_hops || strength < 0.01f) continue;

            // Get neighbors through edges
            Node* node = storage_.get(current_id);
            if (!node) continue;

            for (const auto& edge : node->edges) {
                float propagated = strength * decay_factor * edge.weight;
                activation[edge.target] += propagated;

                // Only add to frontier if significant activation
                if (propagated >= 0.05f) {
                    frontier.push({edge.target, propagated, hop + 1});
                }
            }
        }

        // Sort by activation level
        std::vector<std::pair<NodeId, float>> result(activation.begin(), activation.end());
        std::sort(result.begin(), result.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        return result;
    }

    // Resonance query: combines semantic search with spreading activation
    // Seeds from top semantic matches, spreads activation, returns resonant nodes
    std::vector<Recall> resonate(const std::string& query, size_t k = 10,
                                  float spread_strength = 0.5f)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!yantra_ || !yantra_->ready()) {
            return {};
        }

        // Get semantic seeds
        auto artha = yantra_->transform(query);
        if (artha.nu.size() == 0) return {};

        auto seeds = recall_impl(artha.nu, query, 5, 0.0f, SearchMode::Hybrid);
        if (seeds.empty()) return {};

        // Spread activation from all seeds
        std::unordered_map<NodeId, float, NodeIdHash> total_activation;

        for (const auto& seed : seeds) {
            // Weight by semantic similarity
            float seed_strength = spread_strength * seed.relevance;

            std::queue<std::tuple<NodeId, float, int>> frontier;
            frontier.push({seed.id, seed_strength, 0});

            std::unordered_set<NodeId, NodeIdHash> visited;

            while (!frontier.empty()) {
                auto [current_id, strength, hop] = frontier.front();
                frontier.pop();

                if (hop >= 3 || strength < 0.01f) continue;
                if (visited.count(current_id)) continue;
                visited.insert(current_id);

                total_activation[current_id] += strength;

                Node* node = storage_.get(current_id);
                if (!node) continue;

                for (const auto& edge : node->edges) {
                    float propagated = strength * 0.5f * edge.weight;
                    if (propagated >= 0.01f) {
                        frontier.push({edge.target, propagated, hop + 1});
                    }
                }
            }
        }

        // Combine semantic scores with activation
        std::vector<Recall> results;
        for (const auto& [id, activation] : total_activation) {
            Node* node = storage_.get(id);
            if (!node) continue;

            auto text = payload_to_text(node->payload);

            // Find original semantic score if it was a seed
            float semantic_score = 0.0f;
            for (const auto& seed : seeds) {
                if (seed.id == id) {
                    semantic_score = seed.relevance;
                    break;
                }
            }

            // Resonance score combines semantic and activation
            float resonance_score = 0.6f * semantic_score + 0.4f * activation;

            results.push_back(Recall{
                id,
                resonance_score,       // similarity
                resonance_score,       // relevance
                node->node_type,
                node->kappa,
                node->tau_created,
                node->tau_accessed,
                node->payload,
                text.value_or("")
            });
        }

        // Sort by relevance
        std::sort(results.begin(), results.end(),
                  [](const auto& a, const auto& b) { return a.relevance > b.relevance; });

        if (results.size() > k) {
            results.resize(k);
        }

        return results;
    }

    // ═══════════════════════════════════════════════════════════════════
    // Attractor Dynamics (Phase 2 of resonance architecture)
    // ═══════════════════════════════════════════════════════════════════

    // An attractor is a high-confidence, well-connected node that
    // pulls similar nodes toward it (conceptual gravity well)
    struct Attractor {
        NodeId id;
        float strength;          // Attractor strength (confidence * connectivity)
        std::string label;       // First 50 chars of content for identification
        size_t basin_size = 0;   // Number of nodes in this attractor's basin
    };

    // Find natural attractors in the graph
    // Attractors are nodes with: high confidence + many connections + stable (old)
    std::vector<Attractor> find_attractors(size_t max_attractors = 10,
                                            float min_confidence = 0.6f,
                                            size_t min_edges = 2) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<Attractor> candidates;
        Timestamp current = now();

        storage_.for_each_hot([&](const NodeId& id, const Node& node) {
            // Skip low-confidence nodes
            if (node.kappa.effective() < min_confidence) return;

            // Skip poorly connected nodes
            if (node.edges.size() < min_edges) return;

            // Calculate attractor strength:
            // - confidence contributes (0.4)
            // - connectivity contributes (0.3)
            // - age/stability contributes (0.3)
            float confidence_score = node.kappa.effective();

            // Connectivity: log-scaled to avoid over-weighting highly connected nodes
            float connectivity_score = std::min(std::log2(1.0f + node.edges.size()) / 4.0f, 1.0f);

            // Age: older nodes are more stable attractors
            float age_days = static_cast<float>(current - node.tau_created) / 86400000.0f;
            float age_score = std::min(age_days / 30.0f, 1.0f);  // Max at 30 days

            float strength = 0.4f * confidence_score +
                            0.3f * connectivity_score +
                            0.3f * age_score;

            // Extract label from payload
            auto text = payload_to_text(node.payload);
            std::string label = text ? text->substr(0, 50) : "";

            candidates.push_back({id, strength, label, 0});
        });

        // Sort by strength (strongest first)
        std::sort(candidates.begin(), candidates.end(),
                  [](const Attractor& a, const Attractor& b) {
                      return a.strength > b.strength;
                  });

        // Limit to max_attractors
        if (candidates.size() > max_attractors) {
            candidates.resize(max_attractors);
        }

        return candidates;
    }

    // Compute which attractor a node is pulled toward
    // Returns the attractor ID and the pull strength (0-1)
    std::optional<std::pair<NodeId, float>> compute_attractor_pull(
        NodeId node_id,
        const std::vector<Attractor>& attractors)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return compute_attractor_pull_impl(node_id, attractors);
    }

    // Settle a set of nodes toward their attractors
    // This strengthens connections between nodes and their attractors
    // Returns number of nodes that settled
    size_t settle_toward_attractors(const std::vector<Attractor>& attractors,
                                     float settle_strength = 0.02f) {
        std::lock_guard<std::mutex> lock(mutex_);

        size_t settled = 0;

        storage_.for_each_hot([&](const NodeId& id, const Node& node) {
            // Skip nodes that are themselves attractors
            bool is_attractor = false;
            for (const auto& attr : attractors) {
                if (attr.id == id) {
                    is_attractor = true;
                    break;
                }
            }
            if (is_attractor) return;

            // Find which attractor this node is pulled toward
            auto pull = compute_attractor_pull_impl(id, attractors);
            if (!pull) return;

            auto [attractor_id, pull_strength] = *pull;

            // Strengthen connection toward attractor
            // Weight by pull strength (stronger pull = stronger connection)
            float actual_strength = settle_strength * pull_strength;
            if (actual_strength >= 0.01f) {
                hebbian_strengthen_impl(id, attractor_id, actual_strength);
                settled++;
            }
        });

        return settled;
    }

    // Assign nodes to attractor basins
    // Returns map of attractor_id -> list of node_ids in that basin
    std::unordered_map<NodeId, std::vector<NodeId>, NodeIdHash>
    compute_basins(const std::vector<Attractor>& attractors) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::unordered_map<NodeId, std::vector<NodeId>, NodeIdHash> basins;

        // Initialize empty basins for each attractor
        for (const auto& attr : attractors) {
            basins[attr.id] = {};
        }

        storage_.for_each_hot([&](const NodeId& id, const Node& node) {
            // Skip attractors themselves
            bool is_attractor = false;
            for (const auto& attr : attractors) {
                if (attr.id == id) {
                    is_attractor = true;
                    break;
                }
            }
            if (is_attractor) return;

            // Find which attractor this node is pulled toward
            auto pull = compute_attractor_pull_impl(id, attractors);
            if (pull) {
                basins[pull->first].push_back(id);
            }
        });

        return basins;
    }

    // Run one round of attractor dynamics:
    // 1. Find attractors
    // 2. Settle nodes toward attractors
    // 3. Compute basins
    // Returns a report of what happened
    struct AttractorReport {
        size_t attractor_count = 0;
        size_t nodes_settled = 0;
        std::vector<std::pair<std::string, size_t>> basin_sizes;  // label -> size
    };

    AttractorReport run_attractor_dynamics(size_t max_attractors = 10,
                                            float settle_strength = 0.02f) {
        AttractorReport report;

        // Step 1: Find attractors
        auto attractors = find_attractors(max_attractors);
        report.attractor_count = attractors.size();

        if (attractors.empty()) return report;

        // Step 2: Settle nodes toward attractors
        report.nodes_settled = settle_toward_attractors(attractors, settle_strength);

        // Step 3: Compute basins for reporting
        auto basins = compute_basins(attractors);
        for (const auto& attr : attractors) {
            report.basin_sizes.push_back({
                attr.label,
                basins[attr.id].size()
            });
        }

        return report;
    }

    // Enhanced resonate that uses attractor dynamics
    // First finds attractors, then resonates within attractor basins
    std::vector<Recall> resonate_with_attractors(const std::string& query,
                                                   size_t k = 10,
                                                   float spread_strength = 0.5f) {
        // First get regular resonance results
        auto results = resonate(query, k * 2, spread_strength);
        if (results.empty()) return results;

        std::lock_guard<std::mutex> lock(mutex_);

        // Find attractors
        auto attractors = find_attractors(5);
        if (attractors.empty()) {
            // No attractors found, return regular results
            if (results.size() > k) results.resize(k);
            return results;
        }

        // Compute basins
        auto basins = compute_basins(attractors);

        // Find which attractor the top result belongs to
        auto pull = compute_attractor_pull_impl(results[0].id, attractors);
        if (!pull) {
            if (results.size() > k) results.resize(k);
            return results;
        }

        NodeId primary_attractor = pull->first;

        // Boost results that are in the same basin
        for (auto& result : results) {
            auto result_pull = compute_attractor_pull_impl(result.id, attractors);
            if (result_pull && result_pull->first == primary_attractor) {
                // Same basin - boost relevance
                result.relevance *= 1.2f;
            }
        }

        // Re-sort by adjusted relevance
        std::sort(results.begin(), results.end(),
                  [](const Recall& a, const Recall& b) {
                      return a.relevance > b.relevance;
                  });

        if (results.size() > k) results.resize(k);
        return results;
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
                ensure_bm25_index();  // Lazy build on first use
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

                // Skip nodes without text content
                auto text = payload_to_text(node->payload);
                if (!text || text->size() < 3) continue;  // Skip empty/corrupted payloads

                Recall r;
                r.id = id;
                r.similarity = similarity;
                r.relevance = relevance;
                r.type = node->node_type;
                r.confidence = node->kappa;
                r.created = node->tau_created;
                r.accessed = node->tau_accessed;
                r.payload = node->payload;
                r.text = *text;
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

    // Hebbian strengthening implementation (caller must hold mutex_)
    // Finds existing edge and strengthens it, or creates new edge
    void hebbian_strengthen_impl(NodeId from, NodeId to, float strength) {
        Node* node = storage_.get(from);
        if (!node) return;

        // Find existing Similar edge to target
        for (auto& edge : node->edges) {
            if (edge.target == to && edge.type == EdgeType::Similar) {
                // Strengthen existing edge (cap at 1.0)
                edge.weight = std::min(edge.weight + strength, 1.0f);
                return;
            }
        }

        // No existing edge - create new one with initial strength
        // Use storage_.add_edge for WAL persistence
        storage_.add_edge(from, to, EdgeType::Similar, strength);
    }

    // Internal: compute attractor pull without taking lock
    std::optional<std::pair<NodeId, float>>
    compute_attractor_pull_impl(NodeId node_id, const std::vector<Attractor>& attractors) {
        Node* node = storage_.get(node_id);
        if (!node || attractors.empty()) return std::nullopt;

        // Find the attractor with strongest pull on this node
        // Pull = attractor_strength * semantic_similarity
        NodeId best_attractor;
        float best_pull = 0.0f;

        for (const auto& attr : attractors) {
            Node* attr_node = storage_.get(attr.id);
            if (!attr_node) continue;

            // Semantic similarity between node and attractor
            float similarity = node->nu.cosine(attr_node->nu);

            // Pull is attractor strength weighted by similarity
            float pull = attr.strength * similarity;

            if (pull > best_pull) {
                best_pull = pull;
                best_attractor = attr.id;
            }
        }

        if (best_pull < 0.1f) return std::nullopt;  // Too weak

        return std::make_pair(best_attractor, best_pull);
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
    std::string bm25_path_;  // Path for BM25 persistence
    bool bm25_built_ = false;  // Lazy build on first sparse/hybrid search
    CrossEncoder cross_encoder_;

    // Tag index for exact-match filtering (inter-agent communication)
    TagIndex tag_index_;

    // Autonomous dynamics and learning
    Daemon daemon_;
    FeedbackTracker feedback_;
};

} // namespace chitta
