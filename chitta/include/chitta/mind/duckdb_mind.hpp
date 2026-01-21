#pragma once
// DuckDBMind: SimpleMind backed by DuckDB
//
// Same interface as SimpleMind but uses DuckDB for durable storage.
// Benefits: ACID transactions, crash recovery, vector search, graph queries.

#include "../duckdb_store.hpp"
#include "embedder.hpp"
#include "types.hpp"
#include "../types.hpp"
#include "../vak_onnx.hpp"
#include <memory>
#include <shared_mutex>
#include <optional>

namespace chitta {

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

class DuckDBMind {
public:
    explicit DuckDBMind(DuckDBMindConfig config)
        : config_(std::move(config))
        , running_(false) {}

    ~DuckDBMind() {
        if (running_) close();
    }

    // Lifecycle
    bool open() {
        std::unique_lock lock(mutex_);
        if (!store_.open(config_.path + ".duckdb")) {
            return false;
        }
        running_ = true;
        return true;
    }

    void close() {
        std::unique_lock lock(mutex_);
        if (!running_) return;
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

    // Update confidence
    bool strengthen(NodeId id, float amount = 0.1f) {
        std::unique_lock lock(mutex_);
        return store_.strengthen(nodeid_to_int64(id), amount);
    }

    bool weaken(NodeId id, float amount = 0.1f) {
        std::unique_lock lock(mutex_);
        return store_.weaken(nodeid_to_int64(id), amount);
    }

    // Remove
    bool remove(NodeId id) {
        std::unique_lock lock(mutex_);
        return store_.forget(nodeid_to_int64(id));
    }

    // Living memory operations
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

private:
    DuckDBMindConfig config_;
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
