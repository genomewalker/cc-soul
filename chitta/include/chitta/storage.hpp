#pragma once
// Storage: tiered persistence for mind-scale graphs
//
// Hot  → RAM, float32 vectors, HNSW indexed (active nodes)
// Warm → mmap, int8 quantized, sparse index (recent nodes)
// Cold → disk metadata only, re-embed on access (old nodes)

#include "types.hpp"
#include "quantized.hpp"
#include "hnsw.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace chitta {

// File format magic and version
constexpr uint32_t STORAGE_MAGIC = 0x53594E41;  // "SYNA"
constexpr uint32_t STORAGE_VERSION = 1;

// Storage file header (64 bytes)
struct alignas(64) StorageHeader {
    uint32_t magic;
    uint32_t version;
    uint64_t node_count;
    uint64_t meta_offset;      // Offset to NodeMeta array
    uint64_t vector_offset;    // Offset to QuantizedVector array
    uint64_t payload_offset;   // Offset to payload data
    uint64_t edge_offset;      // Offset to edge data
    uint64_t index_offset;     // Offset to HNSW index
    uint64_t checksum;
};

static_assert(sizeof(StorageHeader) == 64, "StorageHeader must be 64 bytes");

// Memory-mapped region
class MappedRegion {
public:
    MappedRegion() = default;

    bool open(const std::string& path, bool readonly = true) {
        int flags = readonly ? O_RDONLY : O_RDWR;
        fd_ = ::open(path.c_str(), flags);
        if (fd_ < 0) return false;

        struct stat st;
        if (fstat(fd_, &st) < 0) {
            close();
            return false;
        }
        size_ = st.st_size;

        int prot = readonly ? PROT_READ : (PROT_READ | PROT_WRITE);
        data_ = mmap(nullptr, size_, prot, MAP_SHARED, fd_, 0);
        if (data_ == MAP_FAILED) {
            data_ = nullptr;
            close();
            return false;
        }

        // Advise sequential read for initial load
        madvise(data_, size_, MADV_SEQUENTIAL);
        return true;
    }

    bool create(const std::string& path, size_t size) {
        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd_ < 0) return false;

        if (ftruncate(fd_, size) < 0) {
            close();
            return false;
        }
        size_ = size;

        data_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (data_ == MAP_FAILED) {
            data_ = nullptr;
            close();
            return false;
        }

        return true;
    }

    void close() {
        if (data_) {
            munmap(data_, size_);
            data_ = nullptr;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        size_ = 0;
    }

    void sync() {
        if (data_) {
            msync(data_, size_, MS_SYNC);
        }
    }

    ~MappedRegion() { close(); }

    // Non-copyable
    MappedRegion(const MappedRegion&) = delete;
    MappedRegion& operator=(const MappedRegion&) = delete;

    // Movable
    MappedRegion(MappedRegion&& o) noexcept
        : data_(o.data_), size_(o.size_), fd_(o.fd_) {
        o.data_ = nullptr;
        o.size_ = 0;
        o.fd_ = -1;
    }

    MappedRegion& operator=(MappedRegion&& o) noexcept {
        if (this != &o) {
            close();
            data_ = o.data_;
            size_ = o.size_;
            fd_ = o.fd_;
            o.data_ = nullptr;
            o.size_ = 0;
            o.fd_ = -1;
        }
        return *this;
    }

    void* data() { return data_; }
    const void* data() const { return data_; }
    size_t size() const { return size_; }
    bool valid() const { return data_ != nullptr; }

    template<typename T>
    T* as() { return static_cast<T*>(data_); }

    template<typename T>
    const T* as() const { return static_cast<const T*>(data_); }

    template<typename T>
    T* at(size_t offset) {
        return reinterpret_cast<T*>(static_cast<char*>(data_) + offset);
    }

    template<typename T>
    const T* at(size_t offset) const {
        return reinterpret_cast<const T*>(static_cast<const char*>(data_) + offset);
    }

private:
    void* data_ = nullptr;
    size_t size_ = 0;
    int fd_ = -1;
};

// Hot storage: in-memory with full vectors
class HotStorage {
public:
    void insert(NodeId id, Node node, QuantizedVector qvec) {
        nodes_[id] = std::move(node);
        vectors_[id] = std::move(qvec);
        index_.insert(id, vectors_[id]);
    }

    bool contains(NodeId id) const {
        return nodes_.find(id) != nodes_.end();
    }

    Node* get(NodeId id) {
        auto it = nodes_.find(id);
        return it != nodes_.end() ? &it->second : nullptr;
    }

    const Node* get(NodeId id) const {
        auto it = nodes_.find(id);
        return it != nodes_.end() ? &it->second : nullptr;
    }

    const QuantizedVector* vector(NodeId id) const {
        auto it = vectors_.find(id);
        return it != vectors_.end() ? &it->second : nullptr;
    }

    void remove(NodeId id) {
        nodes_.erase(id);
        vectors_.erase(id);
        index_.remove(id);
    }

    std::vector<std::pair<NodeId, float>> search(
        const QuantizedVector& query, size_t k) const
    {
        return index_.search(query, k);
    }

    size_t size() const { return nodes_.size(); }

    void for_each(std::function<void(const NodeId&, const Node&)> fn) const {
        for (const auto& [id, node] : nodes_) {
            fn(id, node);
        }
    }

    // Demote nodes to warm tier based on criteria
    std::vector<std::pair<NodeId, Node>> demote(
        std::function<bool(const Node&)> should_demote)
    {
        std::vector<std::pair<NodeId, Node>> demoted;
        std::vector<NodeId> to_remove;

        for (auto& [id, node] : nodes_) {
            if (should_demote(node)) {
                demoted.emplace_back(id, std::move(node));
                to_remove.push_back(id);
            }
        }

        for (const auto& id : to_remove) {
            remove(id);
        }

        return demoted;
    }

    // Save hot tier to file
    bool save(const std::string& path) const {
        std::ofstream out(path, std::ios::binary);
        if (!out) return false;

        // Write node count
        size_t count = nodes_.size();
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));

        // Write each node
        for (const auto& [id, node] : nodes_) {
            // Node ID
            out.write(reinterpret_cast<const char*>(&id.high), sizeof(id.high));
            out.write(reinterpret_cast<const char*>(&id.low), sizeof(id.low));

            // Node type and metadata
            out.write(reinterpret_cast<const char*>(&node.node_type), sizeof(node.node_type));
            out.write(reinterpret_cast<const char*>(&node.tau_created), sizeof(node.tau_created));
            out.write(reinterpret_cast<const char*>(&node.tau_accessed), sizeof(node.tau_accessed));
            out.write(reinterpret_cast<const char*>(&node.delta), sizeof(node.delta));
            out.write(reinterpret_cast<const char*>(&node.kappa.mu), sizeof(node.kappa.mu));
            out.write(reinterpret_cast<const char*>(&node.kappa.sigma_sq), sizeof(node.kappa.sigma_sq));
            out.write(reinterpret_cast<const char*>(&node.kappa.n), sizeof(node.kappa.n));

            // Vector (full float32)
            out.write(reinterpret_cast<const char*>(node.nu.data.data()),
                      node.nu.data.size() * sizeof(float));

            // Payload
            size_t payload_size = node.payload.size();
            out.write(reinterpret_cast<const char*>(&payload_size), sizeof(payload_size));
            if (payload_size > 0) {
                out.write(reinterpret_cast<const char*>(node.payload.data()), payload_size);
            }

            // Edges
            size_t edge_count = node.edges.size();
            out.write(reinterpret_cast<const char*>(&edge_count), sizeof(edge_count));
            for (const auto& edge : node.edges) {
                out.write(reinterpret_cast<const char*>(&edge.target.high), sizeof(edge.target.high));
                out.write(reinterpret_cast<const char*>(&edge.target.low), sizeof(edge.target.low));
                out.write(reinterpret_cast<const char*>(&edge.type), sizeof(edge.type));
                out.write(reinterpret_cast<const char*>(&edge.weight), sizeof(edge.weight));
            }

            // Tags
            size_t tag_count = node.tags.size();
            out.write(reinterpret_cast<const char*>(&tag_count), sizeof(tag_count));
            for (const auto& tag : node.tags) {
                size_t tag_len = tag.size();
                out.write(reinterpret_cast<const char*>(&tag_len), sizeof(tag_len));
                out.write(tag.data(), tag_len);
            }
        }

        // Save HNSW index
        auto index_data = index_.serialize();
        size_t index_size = index_data.size();
        out.write(reinterpret_cast<const char*>(&index_size), sizeof(index_size));
        out.write(reinterpret_cast<const char*>(index_data.data()), index_size);

        return out.good();
    }

    // Load hot tier from file
    bool load(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;

        nodes_.clear();
        vectors_.clear();

        // Read node count
        size_t count;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));

        // Read each node
        for (size_t i = 0; i < count; ++i) {
            NodeId id;
            in.read(reinterpret_cast<char*>(&id.high), sizeof(id.high));
            in.read(reinterpret_cast<char*>(&id.low), sizeof(id.low));

            Node node;
            node.id = id;
            in.read(reinterpret_cast<char*>(&node.node_type), sizeof(node.node_type));
            in.read(reinterpret_cast<char*>(&node.tau_created), sizeof(node.tau_created));
            in.read(reinterpret_cast<char*>(&node.tau_accessed), sizeof(node.tau_accessed));
            in.read(reinterpret_cast<char*>(&node.delta), sizeof(node.delta));
            in.read(reinterpret_cast<char*>(&node.kappa.mu), sizeof(node.kappa.mu));
            in.read(reinterpret_cast<char*>(&node.kappa.sigma_sq), sizeof(node.kappa.sigma_sq));
            in.read(reinterpret_cast<char*>(&node.kappa.n), sizeof(node.kappa.n));

            // Vector
            node.nu.data.resize(EMBED_DIM);
            in.read(reinterpret_cast<char*>(node.nu.data.data()),
                    EMBED_DIM * sizeof(float));

            // Payload
            size_t payload_size;
            in.read(reinterpret_cast<char*>(&payload_size), sizeof(payload_size));
            if (payload_size > 0) {
                node.payload.resize(payload_size);
                in.read(reinterpret_cast<char*>(node.payload.data()), payload_size);
            }

            // Edges
            size_t edge_count;
            in.read(reinterpret_cast<char*>(&edge_count), sizeof(edge_count));
            node.edges.reserve(edge_count);
            for (size_t e = 0; e < edge_count; ++e) {
                Edge edge;
                in.read(reinterpret_cast<char*>(&edge.target.high), sizeof(edge.target.high));
                in.read(reinterpret_cast<char*>(&edge.target.low), sizeof(edge.target.low));
                in.read(reinterpret_cast<char*>(&edge.type), sizeof(edge.type));
                in.read(reinterpret_cast<char*>(&edge.weight), sizeof(edge.weight));
                node.edges.push_back(edge);
            }

            // Tags (backwards compatible - may not exist in old files)
            size_t tag_count = 0;
            if (in.peek() != EOF) {
                in.read(reinterpret_cast<char*>(&tag_count), sizeof(tag_count));
                node.tags.reserve(tag_count);
                for (size_t t = 0; t < tag_count; ++t) {
                    size_t tag_len;
                    in.read(reinterpret_cast<char*>(&tag_len), sizeof(tag_len));
                    std::string tag(tag_len, '\0');
                    in.read(&tag[0], tag_len);
                    node.tags.push_back(std::move(tag));
                }
            }

            // Store node and quantized vector
            vectors_[id] = QuantizedVector::from_float(node.nu);
            nodes_[id] = std::move(node);
        }

        // Load HNSW index
        size_t index_size;
        in.read(reinterpret_cast<char*>(&index_size), sizeof(index_size));
        std::vector<uint8_t> index_data(index_size);
        in.read(reinterpret_cast<char*>(index_data.data()), index_size);

        if (in.good() && index_size > 0) {
            index_ = HNSWIndex::deserialize(index_data);
        }

        return in.good();
    }

private:
    std::unordered_map<NodeId, Node, NodeIdHash> nodes_;
    std::unordered_map<NodeId, QuantizedVector, NodeIdHash> vectors_;
    HNSWIndex index_;
};

// Warm storage: memory-mapped with quantized vectors
class WarmStorage {
public:
    bool open(const std::string& path) {
        path_ = path;
        return region_.open(path);
    }

    bool create(const std::string& path, size_t estimated_nodes) {
        path_ = path;

        size_t header_size = sizeof(StorageHeader);
        size_t meta_size = estimated_nodes * sizeof(NodeMeta);
        size_t vector_size = estimated_nodes * sizeof(QuantizedVector);
        size_t total = header_size + meta_size + vector_size;

        if (!region_.create(path, total)) return false;

        // Initialize header
        auto* header = region_.as<StorageHeader>();
        header->magic = STORAGE_MAGIC;
        header->version = STORAGE_VERSION;
        header->node_count = 0;
        header->meta_offset = header_size;
        header->vector_offset = header_size + meta_size;
        header->payload_offset = 0;
        header->edge_offset = 0;
        header->index_offset = 0;
        header->checksum = 0;

        capacity_ = estimated_nodes;
        return true;
    }

    void close() {
        region_.close();
        id_to_index_.clear();
    }

    bool valid() const { return region_.valid(); }

    size_t size() const {
        return valid() ? region_.as<const StorageHeader>()->node_count : 0;
    }

    bool insert(NodeId id, const NodeMeta& meta, const QuantizedVector& vec) {
        if (!valid()) return false;

        auto* header = region_.as<StorageHeader>();
        if (header->node_count >= capacity_) return false;

        size_t index = header->node_count++;

        auto* metas = region_.at<NodeMeta>(header->meta_offset);
        metas[index] = meta;

        auto* vectors = region_.at<QuantizedVector>(header->vector_offset);
        vectors[index] = vec;

        id_to_index_[id] = index;
        return true;
    }

    const NodeMeta* meta(NodeId id) const {
        auto it = id_to_index_.find(id);
        if (it == id_to_index_.end()) return nullptr;

        auto* header = region_.as<const StorageHeader>();
        return region_.at<const NodeMeta>(header->meta_offset) + it->second;
    }

    const QuantizedVector* vector(NodeId id) const {
        auto it = id_to_index_.find(id);
        if (it == id_to_index_.end()) return nullptr;

        auto* header = region_.as<const StorageHeader>();
        return region_.at<const QuantizedVector>(header->vector_offset) + it->second;
    }

    bool contains(NodeId id) const {
        return id_to_index_.find(id) != id_to_index_.end();
    }

    void sync() { region_.sync(); }

    // Iterate all nodes in warm storage
    void for_each(std::function<void(const NodeId&, const NodeMeta&)> fn) const {
        auto* header = region_.as<const StorageHeader>();
        if (!header) return;
        auto* metas = region_.at<const NodeMeta>(header->meta_offset);
        for (const auto& [id, idx] : id_to_index_) {
            fn(id, metas[idx]);
        }
    }

    // Remove node from warm storage (for demotion to cold)
    bool remove(NodeId id) {
        auto it = id_to_index_.find(id);
        if (it == id_to_index_.end()) return false;
        id_to_index_.erase(it);
        // Note: doesn't reclaim space in mmap, just removes from index
        return true;
    }

    // Linear scan for warm tier (no HNSW, use brute force)
    std::vector<std::pair<NodeId, float>> search(
        const QuantizedVector& query, size_t k) const
    {
        std::vector<std::pair<NodeId, float>> results;
        results.reserve(id_to_index_.size());

        for (const auto& [id, idx] : id_to_index_) {
            auto* header = region_.as<const StorageHeader>();
            auto* vec = region_.at<const QuantizedVector>(header->vector_offset) + idx;
            float sim = query.cosine_approx(*vec);
            results.emplace_back(id, sim);
        }

        // Partial sort for top-k
        if (results.size() > k) {
            std::partial_sort(results.begin(), results.begin() + k, results.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });
            results.resize(k);
        } else {
            std::sort(results.begin(), results.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });
        }

        return results;
    }

private:
    std::string path_;
    MappedRegion region_;
    std::unordered_map<NodeId, size_t, NodeIdHash> id_to_index_;
    size_t capacity_ = 0;
};

// Cold storage: metadata only, requires re-embedding
class ColdStorage {
public:
    bool open(const std::string& path) {
        path_ = path;
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;

        size_t count;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));

        for (size_t i = 0; i < count; ++i) {
            NodeId id;
            NodeMeta meta;
            in.read(reinterpret_cast<char*>(&id), sizeof(id));
            in.read(reinterpret_cast<char*>(&meta), sizeof(meta));

            size_t payload_len;
            in.read(reinterpret_cast<char*>(&payload_len), sizeof(payload_len));
            std::vector<uint8_t> payload(payload_len);
            in.read(reinterpret_cast<char*>(payload.data()), payload_len);

            metas_[id] = meta;
            payloads_[id] = std::move(payload);
        }

        return true;
    }

    bool save(const std::string& path) const {
        std::ofstream out(path, std::ios::binary);
        if (!out) return false;

        size_t count = metas_.size();
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));

        for (const auto& [id, meta] : metas_) {
            out.write(reinterpret_cast<const char*>(&id), sizeof(id));
            out.write(reinterpret_cast<const char*>(&meta), sizeof(meta));

            auto pit = payloads_.find(id);
            size_t payload_len = (pit != payloads_.end()) ? pit->second.size() : 0;
            out.write(reinterpret_cast<const char*>(&payload_len), sizeof(payload_len));
            if (payload_len > 0) {
                out.write(reinterpret_cast<const char*>(pit->second.data()), payload_len);
            }
        }

        return true;
    }

    void insert(NodeId id, NodeMeta meta, std::vector<uint8_t> payload) {
        metas_[id] = meta;
        payloads_[id] = std::move(payload);
    }

    bool contains(NodeId id) const {
        return metas_.find(id) != metas_.end();
    }

    const NodeMeta* meta(NodeId id) const {
        auto it = metas_.find(id);
        return it != metas_.end() ? &it->second : nullptr;
    }

    const std::vector<uint8_t>* payload(NodeId id) const {
        auto it = payloads_.find(id);
        return it != payloads_.end() ? &it->second : nullptr;
    }

    size_t size() const { return metas_.size(); }

    // Promote to warm tier (need to re-embed)
    std::vector<NodeId> candidates_for_promotion(size_t max_count) const {
        std::vector<std::pair<NodeId, Timestamp>> by_access;
        for (const auto& [id, meta] : metas_) {
            by_access.emplace_back(id, meta.tau_accessed);
        }

        std::partial_sort(by_access.begin(),
            by_access.begin() + std::min(max_count, by_access.size()),
            by_access.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        std::vector<NodeId> result;
        for (size_t i = 0; i < std::min(max_count, by_access.size()); ++i) {
            result.push_back(by_access[i].first);
        }
        return result;
    }

private:
    std::string path_;
    std::unordered_map<NodeId, NodeMeta, NodeIdHash> metas_;
    std::unordered_map<NodeId, std::vector<uint8_t>, NodeIdHash> payloads_;
};

// Tiered storage manager
class TieredStorage {
public:
    struct Config {
        std::string base_path;
        size_t hot_max_nodes = 1000;       // Max nodes in hot tier (lowered for dev)
        size_t warm_max_nodes = 10000;     // Max nodes in warm tier
        Timestamp hot_threshold_ms = 3600000;    // 1 hour (lowered for dev)
        Timestamp warm_threshold_ms = 86400000;  // 1 day
    };

    explicit TieredStorage(Config config) : config_(std::move(config)), loaded_successfully_(false) {}

    bool initialize() {
        std::string hot_path = config_.base_path + ".hot";
        std::string warm_path = config_.base_path + ".warm";
        std::string cold_path = config_.base_path + ".cold";

        std::cerr << "[TieredStorage] Loading from: " << hot_path << "\n";

        // Try to load existing hot tier
        loaded_successfully_ = hot_.load(hot_path);
        std::cerr << "[TieredStorage] Load result: " << (loaded_successfully_ ? "success" : "failed")
                  << ", nodes: " << hot_.size() << "\n";

        // Try to open existing warm/cold files
        warm_.open(warm_path);
        cold_.open(cold_path);

        return true;
    }

    bool insert(NodeId id, Node node) {
        QuantizedVector qvec = QuantizedVector::from_float(node.nu);
        hot_.insert(id, std::move(node), std::move(qvec));
        return true;
    }

    Node* get(NodeId id) {
        // Check hot first
        if (auto* node = hot_.get(id)) {
            node->touch();
            return node;
        }

        // Check warm - promote to hot on access
        if (warm_.contains(id)) {
            return promote_from_warm(id);
        }

        // Cold requires re-embedding
        return nullptr;
    }

    bool contains(NodeId id) const {
        return hot_.contains(id) || warm_.contains(id) || cold_.contains(id);
    }

    StorageTier tier(NodeId id) const {
        if (hot_.contains(id)) return StorageTier::Hot;
        if (warm_.contains(id)) return StorageTier::Warm;
        if (cold_.contains(id)) return StorageTier::Cold;
        return StorageTier::Cold;  // Default
    }

    std::vector<std::pair<NodeId, float>> search(
        const QuantizedVector& query, size_t k) const
    {
        // Search hot tier with HNSW
        auto hot_results = hot_.search(query, k);

        // If we have enough results from hot tier, return them
        if (hot_results.size() >= k) {
            return hot_results;
        }

        // Also search warm tier
        auto warm_results = warm_.search(query, k);

        // Merge results
        std::vector<std::pair<NodeId, float>> merged;
        merged.reserve(hot_results.size() + warm_results.size());
        merged.insert(merged.end(), hot_results.begin(), hot_results.end());
        merged.insert(merged.end(), warm_results.begin(), warm_results.end());

        // Sort and take top k
        std::partial_sort(merged.begin(),
            merged.begin() + std::min(k, merged.size()),
            merged.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        if (merged.size() > k) merged.resize(k);
        return merged;
    }

    // Compute value score for tiering decisions
    // High value = keep hot, low value = demote
    float compute_value(const Node& node, Timestamp current) const {
        // Age in days (with minimum of 1 hour = 0.04 days to avoid division issues)
        float age_ms = static_cast<float>(current - node.tau_accessed);
        float age_days = std::max(age_ms / 86400000.0f, 0.04f);

        // Recency factor: exponential decay over time
        // Half-life of ~3 days for hot tier consideration
        float recency = std::exp(-0.23f * age_days);

        // Confidence contributes to value
        float confidence = node.kappa.mu;

        // Value = weighted combination
        // High confidence + recent = high value
        // Low confidence + old = low value
        return confidence * 0.4f + recency * 0.6f;
    }

    // Run tier management (call periodically)
    void manage_tiers() {
        Timestamp current = now();

        // Demote from hot to warm: value-based
        // Demote if value < threshold AND we're at capacity
        auto demoted = hot_.demote([this, current](const Node& node) {
            float value = compute_value(node, current);

            // Value threshold for hot tier: 0.3
            // Also demote if very old regardless of confidence (>7 days and at capacity)
            bool low_value = value < 0.3f;
            bool very_old = (current - node.tau_accessed) > config_.warm_threshold_ms;
            bool at_capacity = hot_.size() > config_.hot_max_nodes;

            return (low_value || very_old) && at_capacity;
        });

        for (auto& [id, node] : demoted) {
            NodeMeta meta;
            meta.id = id;
            meta.node_type = node.node_type;
            meta.tier = StorageTier::Warm;
            meta.tau_created = node.tau_created;
            meta.tau_accessed = node.tau_accessed;
            meta.confidence_mu = node.kappa.mu;
            meta.confidence_sigma = node.kappa.sigma_sq;
            meta.decay_rate = node.delta;

            QuantizedVector qvec = QuantizedVector::from_float(node.nu);
            warm_.insert(id, meta, qvec);
        }

        // Demote from warm to cold: very low confidence or very old
        std::vector<NodeId> to_demote;
        warm_.for_each([&](const NodeId& id, const NodeMeta& meta) {
            float age_days = static_cast<float>(current - meta.tau_accessed) / 86400000.0f;

            // Demote to cold if: low confidence AND old, OR very old regardless
            bool low_conf_old = (meta.confidence_mu < 0.2f && age_days > 7.0f);
            bool very_old = (age_days > 30.0f);

            if (low_conf_old || very_old) {
                to_demote.push_back(id);
            }
        });

        for (const auto& id : to_demote) {
            auto* meta = warm_.meta(id);
            if (!meta) continue;

            // Move to cold storage (stores metadata + payload, loses vector)
            cold_.insert(id, *meta, {});  // Empty payload for now
            warm_.remove(id);
        }
    }

    void sync() {
        std::cerr << "[TieredStorage] sync() called: hot_size=" << hot_.size()
                  << ", loaded_successfully=" << loaded_successfully_ << "\n";
        // Only save if we have data (prevents overwriting on failed load)
        if (hot_.size() > 0 || loaded_successfully_) {
            std::cerr << "[TieredStorage] Saving hot tier\n";
            hot_.save(config_.base_path + ".hot");
        } else {
            std::cerr << "[TieredStorage] SKIPPING save (no data, load failed)\n";
        }
        warm_.sync();
        if (cold_.size() > 0 || loaded_successfully_) {
            cold_.save(config_.base_path + ".cold");
        }
    }

    size_t hot_size() const { return hot_.size(); }
    size_t warm_size() const { return warm_.size(); }
    size_t cold_size() const { return cold_.size(); }
    size_t total_size() const {
        return hot_size() + warm_size() + cold_size();
    }

    void for_each_hot(std::function<void(const NodeId&, const Node&)> fn) const {
        hot_.for_each(fn);
    }

private:
    Node* promote_from_warm(NodeId id) {
        auto* meta = warm_.meta(id);
        auto* qvec = warm_.vector(id);
        if (!meta || !qvec) return nullptr;

        // Reconstruct node from warm storage
        Node node(meta->node_type, qvec->to_float());
        node.id = id;
        node.tau_created = meta->tau_created;
        node.tau_accessed = now();
        node.delta = meta->decay_rate;
        node.kappa.mu = meta->confidence_mu;
        node.kappa.sigma_sq = meta->confidence_sigma;

        QuantizedVector vec = *qvec;
        hot_.insert(id, std::move(node), std::move(vec));
        return hot_.get(id);
    }

    Config config_;
    HotStorage hot_;
    WarmStorage warm_;
    ColdStorage cold_;
    bool loaded_successfully_;
};

} // namespace chitta
