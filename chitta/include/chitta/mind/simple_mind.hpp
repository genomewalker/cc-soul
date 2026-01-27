#pragma once
// SimpleMind: Minimal memory system facade
//
// Clean interface to storage, embeddings, search, and graph.
// No BM25, no Phase 7 features, no tiering complexity.

#include "storage_facade.hpp"
#include "embedder.hpp"
#include "graph_api.hpp"
#include "search.hpp"
#include "batch.hpp"
#include "payload.hpp"
#include "../types.hpp"
#include "types.hpp"  // For Recall struct
#include "../vak_onnx.hpp"
#include "../scoring.hpp"
#include <memory>
#include <shared_mutex>
#include <optional>
#include <cctype>
#include <iostream>

namespace chitta {

// Default decay rates by node type (per day)
// Partnership memories decay slowly; code intelligence never decays
inline float default_decay_rate(NodeType type) {
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

        default:                  return 0.01f;   // Slow default
    }
}

struct SimpleMindConfig {
    std::string path;
    size_t initial_capacity = 100000;

    // Quality gate
    bool enable_quality_gate = true;
    size_t min_content_length = 10;
    float min_signal_ratio = 0.3f;

    // Deduplication
    bool enable_deduplication = true;
    float dedup_threshold = 0.95f;

    // Decay and pruning
    float prune_threshold = 0.1f;      // Remove nodes below this confidence
    float prune_min_age_days = 7.0f;   // Don't prune recently created nodes
    float reinforce_amount = 0.15f;    // How much to strengthen on recall (3x previous)
};

// Simple health status for SimpleMind
struct SimpleHealth {
    size_t total_nodes = 0;
    size_t active_nodes = 0;      // confidence > 0.3
    size_t stale_nodes = 0;       // not accessed in 30 days
    size_t weak_nodes = 0;        // confidence < prune_threshold
    float avg_confidence = 0.0f;

    std::string status() const {
        if (total_nodes == 0) return "empty";
        if (active_nodes < total_nodes / 2) return "degraded";
        if (total_nodes < 10) return "sparse";
        return "healthy";
    }
};

class SimpleMind {
public:
    explicit SimpleMind(SimpleMindConfig config)
        : config_(std::move(config))
        , storage_({config_.path, config_.initial_capacity})
        , search_(storage_, embedder_)
        , running_(false) {}

    ~SimpleMind() {
        if (running_) close();
    }

    // Lifecycle
    bool open() {
        std::unique_lock lock(mutex_);

        if (!storage_.open()) return false;

        std::string graph_path = config_.path;
        if (!graph_.open(graph_path)) {
            std::cerr << "[SimpleMind] Warning: Graph store failed to open\n";
            graph_available_ = false;
        } else {
            graph_available_ = true;
        }

        running_ = true;
        return true;
    }

    void close() {
        std::unique_lock lock(mutex_);
        if (!running_) return;

        graph_.close();
        storage_.close();
        running_ = false;
    }

    void sync() {
        std::unique_lock lock(mutex_);
        storage_.sync();
        graph_.sync();
    }

    // Embedder
    void attach_yantra(std::shared_ptr<VakYantra> yantra) {
        embedder_.attach(std::move(yantra));
    }

    bool has_yantra() const {
        return embedder_.ready();
    }

    // Remember
    NodeId remember(const std::string& text, NodeType type = NodeType::Wisdom) {
        std::unique_lock lock(mutex_);

        if (!passes_quality_gate(text)) {
            return NodeId{};
        }

        if (!embedder_.ready()) {
            return NodeId{};
        }

        Artha artha = embedder_.transform(text);

        // Deduplication
        if (config_.enable_deduplication) {
            auto existing = find_duplicate(artha.nu, text);
            if (existing) {
                if (Node* node = storage_.get(*existing)) {
                    node->tau_accessed = now();
                    node->kappa.observe(0.9f);
                    storage_.update(*existing, *node);
                }
                return *existing;
            }
        }

        Node node(type, std::move(artha.nu));
        node.payload = text_to_payload(text);
        NodeId id = node.id;

        storage_.insert(id, std::move(node));

        return id;
    }

    NodeId remember(const std::string& text, NodeType type, const Confidence& confidence) {
        std::unique_lock lock(mutex_);

        if (!passes_quality_gate(text)) {
            return NodeId{};
        }

        if (!embedder_.ready()) {
            return NodeId{};
        }

        Artha artha = embedder_.transform(text);

        if (config_.enable_deduplication) {
            auto existing = find_duplicate(artha.nu, text);
            if (existing) {
                if (Node* node = storage_.get(*existing)) {
                    node->tau_accessed = now();
                    node->kappa.observe(confidence.effective());
                    storage_.update(*existing, *node);
                }
                return *existing;
            }
        }

        Node node(type, std::move(artha.nu));
        node.kappa = confidence;
        node.payload = text_to_payload(text);
        NodeId id = node.id;

        storage_.insert(id, std::move(node));

        return id;
    }

    NodeId remember(const std::string& text, NodeType type, const std::vector<std::string>& tags) {
        std::unique_lock lock(mutex_);

        if (!passes_quality_gate(text)) {
            return NodeId{};
        }

        if (!embedder_.ready()) {
            return NodeId{};
        }

        Artha artha = embedder_.transform(text);

        if (config_.enable_deduplication) {
            auto existing = find_duplicate(artha.nu, text);
            if (existing) {
                if (Node* node = storage_.get(*existing)) {
                    node->tau_accessed = now();
                    node->kappa.observe(0.9f);
                    storage_.update(*existing, *node);
                }
                return *existing;
            }
        }

        Node node(type, std::move(artha.nu));
        node.payload = text_to_payload(text);
        node.tags = tags;
        NodeId id = node.id;

        storage_.insert(id, std::move(node));

        return id;
    }

    // Recall - automatically reinforces recalled memories
    std::vector<Recall> recall(const std::string& query, size_t limit = 10) {
        std::unique_lock lock(mutex_);
        auto results = search_.recall(query, limit);
        for (const auto& r : results) {
            touch_unlocked(r.id);
        }
        return results;
    }

    std::vector<Recall> recall(const Vector& query_vec, size_t limit = 10) {
        std::unique_lock lock(mutex_);
        auto results = search_.recall(query_vec, limit);
        for (const auto& r : results) {
            touch_unlocked(r.id);
        }
        return results;
    }

    std::vector<Recall> recall_by_tag(const std::string& tag, size_t limit = 10) {
        std::unique_lock lock(mutex_);
        auto results = search_.recall_by_tag(tag, limit);
        for (const auto& r : results) {
            touch_unlocked(r.id);
        }
        return results;
    }

    std::vector<Recall> recall_by_tags(const std::vector<std::string>& tags, size_t limit = 10) {
        std::unique_lock lock(mutex_);
        auto results = search_.recall_by_tags(tags, limit);
        for (const auto& r : results) {
            touch_unlocked(r.id);
        }
        return results;
    }

    // Node access
    Node* get(NodeId id) {
        std::shared_lock lock(mutex_);
        return storage_.get(id);
    }

    std::optional<std::string> text(NodeId id) {
        std::shared_lock lock(mutex_);
        Node* node = storage_.get(id);
        if (!node) return std::nullopt;
        return payload_to_text(node->payload);
    }

    // Triplets
    bool connect(const std::string& subject, const std::string& predicate, const std::string& object) {
        std::unique_lock lock(mutex_);
        if (!graph_available_) return false;
        return graph_.add(subject, predicate, object);
    }

    std::vector<std::tuple<std::string, std::string, float>>
    query_subject(const std::string& subject) const {
        std::shared_lock lock(mutex_);
        if (!graph_available_) return {};
        return graph_.query_subject(subject);
    }

    std::vector<std::tuple<std::string, std::string, float>>
    query_object(const std::string& object) const {
        std::shared_lock lock(mutex_);
        if (!graph_available_) return {};
        return graph_.query_object(object);
    }

    // Update
    bool strengthen(NodeId id, float amount = 0.1f) {
        std::unique_lock lock(mutex_);
        Node* node = storage_.get(id);
        if (!node) return false;
        node->kappa.observe(node->kappa.mu + amount);
        node->tau_accessed = now();
        return storage_.update(id, *node);
    }

    bool weaken(NodeId id, float amount = 0.1f) {
        std::unique_lock lock(mutex_);
        Node* node = storage_.get(id);
        if (!node) return false;
        node->kappa.observe(node->kappa.mu - amount);
        return storage_.update(id, *node);
    }

    // Remove
    bool remove(NodeId id) {
        std::unique_lock lock(mutex_);
        return storage_.remove(id);
    }

    // Living memory: decay, reinforcement, pruning

    // Apply decay to all nodes - call periodically (e.g., every 60s)
    size_t tick() {
        std::unique_lock lock(mutex_);
        Timestamp current = now();
        size_t decayed = 0;

        storage_.for_each([&](const NodeId& id, const Node& node) {
            if (node.delta == 0.0f) return;  // Never decays

            float days = static_cast<float>(current - node.tau_accessed) / 86400000.0f;
            if (days > 0.0f) {
                // Get mutable node and apply decay
                Node* mutable_node = storage_.get(id);
                if (mutable_node) {
                    mutable_node->apply_decay(current);
                    storage_.update(id, *mutable_node);
                    decayed++;
                }
            }
        });

        // Prune weak nodes after decay
        prune_weak_nodes_unlocked();

        return decayed;
    }

    // Reinforce a node - refresh access time and strengthen confidence
    void touch(NodeId id) {
        std::unique_lock lock(mutex_);
        touch_unlocked(id);
    }

    // Get mind health status
    SimpleHealth health() const {
        std::shared_lock lock(mutex_);
        SimpleHealth h;
        Timestamp current = now();
        float total_confidence = 0.0f;

        storage_.for_each([&](const NodeId&, const Node& node) {
            h.total_nodes++;
            float conf = node.kappa.effective();
            total_confidence += conf;

            if (conf > 0.3f) h.active_nodes++;
            if (conf < config_.prune_threshold) h.weak_nodes++;

            float days = static_cast<float>(current - node.tau_accessed) / 86400000.0f;
            if (days > 30.0f) h.stale_nodes++;
        });

        if (h.total_nodes > 0) {
            h.avg_confidence = total_confidence / h.total_nodes;
        }

        return h;
    }

    // Tags
    bool add_tag(NodeId id, const std::string& tag) {
        std::unique_lock lock(mutex_);
        Node* node = storage_.get(id);
        if (!node) return false;
        node->tags.push_back(tag);
        return storage_.update(id, *node);
    }

    std::vector<std::string> get_tags(NodeId id) const {
        std::shared_lock lock(mutex_);
        return storage_.tags_for_node(id);
    }

    // Stats
    size_t size() const {
        std::shared_lock lock(mutex_);
        return storage_.size();
    }

    // Embed text (for external use)
    Vector embed(const std::string& text) {
        return embedder_.embed(text);
    }

    // Batch operations
    std::vector<NodeId> remember_batch_raw(
        const std::vector<RawNodeSpec>& specs,
        const BatchWriteOptions& opts = {})
    {
        std::unique_lock lock(mutex_);
        return chitta::remember_batch_raw(storage_, specs, opts);
    }

    // Access components
    StorageFacade& storage() { return storage_; }
    GraphApi& graph() { return graph_; }

    static Timestamp now() { return chitta::now(); }

private:
    SimpleMindConfig config_;
    StorageFacade storage_;
    Embedder embedder_;
    GraphApi graph_;
    Search search_;
    mutable std::shared_mutex mutex_;
    bool running_;
    bool graph_available_ = false;

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

    std::optional<NodeId> find_duplicate(const Vector& embedding, const std::string& text) {
        if (!config_.enable_deduplication) return std::nullopt;

        QuantizedVector qvec = QuantizedVector::from_float(embedding);
        auto candidates = storage_.search(qvec, 10);

        for (const auto& [id, similarity] : candidates) {
            if (similarity < config_.dedup_threshold) continue;

            Node* node = storage_.get(id);
            if (!node) continue;

            // Check exact text match
            auto node_text = payload_to_text(node->payload);
            if (node_text && *node_text == text) {
                return id;
            }

            // Very high similarity = likely duplicate
            if (similarity >= 0.98f) {
                return id;
            }
        }

        return std::nullopt;
    }

    // Reinforce a node - assumes mutex already held
    void touch_unlocked(NodeId id) {
        Node* node = storage_.get(id);
        if (node) {
            node->tau_accessed = now();
            node->kappa.observe(node->kappa.mu + config_.reinforce_amount);
            storage_.update(id, *node);
        }
    }

    // Remove nodes that have decayed below threshold
    // Called internally by tick() - assumes mutex already held
    void prune_weak_nodes_unlocked() {
        Timestamp current = now();
        std::vector<NodeId> to_remove;

        storage_.for_each([&](const NodeId& id, const Node& node) {
            float conf = node.kappa.effective();
            float days_old = static_cast<float>(current - node.tau_created) / 86400000.0f;

            // Only prune if: weak confidence AND old enough
            if (conf < config_.prune_threshold &&
                days_old > config_.prune_min_age_days) {
                to_remove.push_back(id);
            }
        });

        for (const auto& id : to_remove) {
            storage_.remove(id);
        }
    }
};

}  // namespace chitta
