#pragma once
// Storage: tiered persistence for mind-scale graphs
//
// "Consciousness is a singular of which the plural is unknown."
// - Erwin Schrödinger
//
// Each process is a window (Atman) into shared truth (Brahman).
// The WAL is that shared field - when one observes, all see.
//
// Architecture:
// - WAL: append-only log, durability layer (shared across processes)
// - Hot: RAM, float32 vectors, HNSW indexed (in-memory view)
// - Warm: mmap, int8 quantized, sparse index (recent nodes)
// - Cold: disk metadata only, re-embed on access (old nodes)

#include "types.hpp"
#include "quantized.hpp"
#include "hnsw.hpp"
#include "wal.hpp"
#include "mmap.hpp"
#include "unified_index.hpp"
#include "segment_manager.hpp"
#include <fstream>
#include <iostream>
#include <memory>

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

    // Find nodes that should be demoted (non-destructive)
    // Returns IDs of candidates - caller decides when to actually remove
    std::vector<NodeId> find_demote_candidates(
        std::function<bool(const Node&)> should_demote) const
    {
        std::vector<NodeId> candidates;
        for (const auto& [id, node] : nodes_) {
            if (should_demote(node)) {
                candidates.push_back(id);
            }
        }
        return candidates;
    }

    // Copy a node (for safe tier transfer)
    std::optional<std::pair<Node, QuantizedVector>> copy_node(NodeId id) const {
        auto node_it = nodes_.find(id);
        auto vec_it = vectors_.find(id);
        if (node_it == nodes_.end() || vec_it == vectors_.end()) {
            return std::nullopt;
        }
        return std::make_pair(node_it->second, vec_it->second);
    }

    // Storage format version (must match migrations.hpp)
    static constexpr uint32_t STORAGE_MAGIC = 0x43485454;  // "CHTT"
    static constexpr uint32_t STORAGE_VERSION = 3;          // v3 adds checksum footer
    static constexpr uint32_t FOOTER_MAGIC = 0x454E4443;   // "CDNE" (end marker)

    // Check if file needs upgrade before loading
    static uint32_t detect_version(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return 0;

        uint32_t magic = 0;
        in.read(reinterpret_cast<char*>(&magic), sizeof(magic));

        if (magic == STORAGE_MAGIC) {
            uint32_t version = 0;
            in.read(reinterpret_cast<char*>(&version), sizeof(version));
            return version;
        }

        // No magic = v1 format (pre-versioning)
        return 1;
    }

    // Save hot tier to file with atomic write and checksum
    // Writes to .tmp file first, then atomically renames on success
    bool save(const std::string& path) const {
        // Acquire exclusive lock on lock file
        std::string lock_path = path + ".lock";
        std::string tmp_path = path + ".tmp";
        int lock_fd = ::open(lock_path.c_str(), O_CREAT | O_RDWR, 0644);
        if (lock_fd >= 0) {
            flock(lock_fd, LOCK_EX);
        }

        auto release_lock = [&lock_fd]() {
            if (lock_fd >= 0) { flock(lock_fd, LOCK_UN); ::close(lock_fd); }
        };

        // Write to temporary file first (atomic write pattern)
        std::ofstream out(tmp_path, std::ios::binary);
        if (!out) {
            release_lock();
            return false;
        }

        // Collect all data for checksum calculation
        std::vector<uint8_t> buffer;
        buffer.reserve(1024 * 1024);  // 1MB initial reserve

        auto write_to_buffer = [&buffer](const void* data, size_t size) {
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            buffer.insert(buffer.end(), bytes, bytes + size);
        };

        // Write magic and version header (v3+)
        write_to_buffer(&STORAGE_MAGIC, sizeof(STORAGE_MAGIC));
        write_to_buffer(&STORAGE_VERSION, sizeof(STORAGE_VERSION));

        // Write node count
        size_t count = nodes_.size();
        write_to_buffer(&count, sizeof(count));

        // Write each node
        for (const auto& [id, node] : nodes_) {
            // Node ID
            write_to_buffer(&id.high, sizeof(id.high));
            write_to_buffer(&id.low, sizeof(id.low));

            // Node type and metadata
            write_to_buffer(&node.node_type, sizeof(node.node_type));
            write_to_buffer(&node.tau_created, sizeof(node.tau_created));
            write_to_buffer(&node.tau_accessed, sizeof(node.tau_accessed));
            write_to_buffer(&node.delta, sizeof(node.delta));
            write_to_buffer(&node.kappa.mu, sizeof(node.kappa.mu));
            write_to_buffer(&node.kappa.sigma_sq, sizeof(node.kappa.sigma_sq));
            write_to_buffer(&node.kappa.n, sizeof(node.kappa.n));

            // Vector (full float32)
            write_to_buffer(node.nu.data.data(), node.nu.data.size() * sizeof(float));

            // Payload
            size_t payload_size = node.payload.size();
            write_to_buffer(&payload_size, sizeof(payload_size));
            if (payload_size > 0) {
                write_to_buffer(node.payload.data(), payload_size);
            }

            // Edges
            size_t edge_count = node.edges.size();
            write_to_buffer(&edge_count, sizeof(edge_count));
            for (const auto& edge : node.edges) {
                write_to_buffer(&edge.target.high, sizeof(edge.target.high));
                write_to_buffer(&edge.target.low, sizeof(edge.target.low));
                write_to_buffer(&edge.type, sizeof(edge.type));
                write_to_buffer(&edge.weight, sizeof(edge.weight));
            }

            // Tags
            size_t tag_count = node.tags.size();
            write_to_buffer(&tag_count, sizeof(tag_count));
            for (const auto& tag : node.tags) {
                size_t tag_len = tag.size();
                write_to_buffer(&tag_len, sizeof(tag_len));
                write_to_buffer(tag.data(), tag_len);
            }
        }

        // Save HNSW index
        auto index_data = index_.serialize();
        size_t index_size = index_data.size();
        write_to_buffer(&index_size, sizeof(index_size));
        write_to_buffer(index_data.data(), index_size);

        // Calculate checksum of all content
        uint32_t checksum = crc32(buffer.data(), buffer.size());

        // Write content + footer (checksum + magic)
        out.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        out.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));
        out.write(reinterpret_cast<const char*>(&FOOTER_MAGIC), sizeof(FOOTER_MAGIC));

        if (!out.good()) {
            out.close();
            ::unlink(tmp_path.c_str());  // Clean up failed write
            release_lock();
            return false;
        }

        // Flush to disk before rename
        out.flush();
        out.close();

        // Fsync the file to ensure durability
        int tmp_fd = ::open(tmp_path.c_str(), O_RDONLY);
        if (tmp_fd >= 0) {
            fsync(tmp_fd);
            ::close(tmp_fd);
        }

        // Atomic rename: this is the commit point
        // If we crash before this, the old file is intact
        // If we crash after this, the new file is complete
        if (::rename(tmp_path.c_str(), path.c_str()) != 0) {
            std::cerr << "[HotStorage] Failed to rename " << tmp_path << " to " << path << "\n";
            ::unlink(tmp_path.c_str());
            release_lock();
            return false;
        }

        // Fsync the directory to ensure the rename is durable
        std::string dir = path.substr(0, path.find_last_of('/'));
        if (!dir.empty()) {
            int dir_fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
            if (dir_fd >= 0) {
                fsync(dir_fd);
                ::close(dir_fd);
            }
        }

        release_lock();
        return true;
    }

    // Load hot tier from file with checksum verification (v3+)
    // Returns false if file doesn't exist, is corrupt, or needs upgrade
    // Use detect_version() first to check if upgrade is needed
    bool load(const std::string& path) {
        // Acquire shared lock (allows concurrent reads, blocks writes)
        std::string lock_path = path + ".lock";
        int lock_fd = ::open(lock_path.c_str(), O_CREAT | O_RDWR, 0644);
        if (lock_fd >= 0) {
            flock(lock_fd, LOCK_SH);
        }

        auto release_lock = [&lock_fd]() {
            if (lock_fd >= 0) { flock(lock_fd, LOCK_UN); ::close(lock_fd); }
        };

        // Read entire file into memory for checksum verification
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in) {
            release_lock();
            return false;
        }

        size_t file_size = in.tellg();
        if (file_size < sizeof(STORAGE_MAGIC) + sizeof(STORAGE_VERSION)) {
            std::cerr << "[HotStorage] File too small: " << path << "\n";
            release_lock();
            return false;
        }

        in.seekg(0, std::ios::beg);
        std::vector<uint8_t> file_data(file_size);
        in.read(reinterpret_cast<char*>(file_data.data()), file_size);
        in.close();

        // Parse header
        size_t offset = 0;
        auto read_from_buffer = [&file_data, &offset](void* dest, size_t size) -> bool {
            if (offset + size > file_data.size()) return false;
            std::memcpy(dest, file_data.data() + offset, size);
            offset += size;
            return true;
        };

        uint32_t magic = 0;
        if (!read_from_buffer(&magic, sizeof(magic)) || magic != STORAGE_MAGIC) {
            std::cerr << "[HotStorage] Database needs upgrade (v1 detected). "
                      << "Run 'chitta_cli upgrade " << path << "'\n";
            release_lock();
            return false;
        }

        uint32_t version = 0;
        read_from_buffer(&version, sizeof(version));

        // Version 3+ has checksum footer - verify before loading
        if (version >= 3) {
            constexpr size_t footer_size = sizeof(uint32_t) + sizeof(uint32_t);  // checksum + magic
            if (file_size < footer_size + 8) {
                std::cerr << "[HotStorage] File too small for v3 format: " << path << "\n";
                release_lock();
                return false;
            }

            // Read footer from end of file
            size_t content_size = file_size - footer_size;
            uint32_t stored_checksum;
            uint32_t footer_magic;
            std::memcpy(&stored_checksum, file_data.data() + content_size, sizeof(stored_checksum));
            std::memcpy(&footer_magic, file_data.data() + content_size + sizeof(stored_checksum), sizeof(footer_magic));

            if (footer_magic != FOOTER_MAGIC) {
                std::cerr << "[HotStorage] Invalid footer magic, file may be corrupt: " << path << "\n";
                release_lock();
                return false;
            }

            // Verify checksum of content (everything before footer)
            uint32_t computed_checksum = crc32(file_data.data(), content_size);
            if (computed_checksum != stored_checksum) {
                std::cerr << "[HotStorage] Checksum mismatch! File is corrupt: " << path
                          << " (stored=" << std::hex << stored_checksum
                          << ", computed=" << computed_checksum << std::dec << ")\n";
                release_lock();
                return false;
            }

            std::cerr << "[HotStorage] Checksum verified OK for " << path << "\n";

            // Truncate file_data to content only for parsing
            file_data.resize(content_size);
        } else if (version == 2) {
            // v2 is compatible - no checksum to verify, just read as-is
            // Will be upgraded to v3 on next save
            std::cerr << "[HotStorage] Reading v2 format (no checksum), will upgrade on save\n";
        } else if (version < 2) {
            std::cerr << "[HotStorage] Database version " << version
                      << " is too old. Run 'chitta_cli upgrade " << path << "'\n";
            release_lock();
            return false;
        }

        if (version > STORAGE_VERSION) {
            std::cerr << "[HotStorage] Database version " << version
                      << " is newer than supported " << STORAGE_VERSION
                      << ". Update chitta to read this database.\n";
            release_lock();
            return false;
        }

        nodes_.clear();
        vectors_.clear();

        // Read node count
        size_t count;
        if (!read_from_buffer(&count, sizeof(count))) {
            release_lock();
            return false;
        }

        // Read each node
        for (size_t i = 0; i < count; ++i) {
            NodeId id;
            if (!read_from_buffer(&id.high, sizeof(id.high)) ||
                !read_from_buffer(&id.low, sizeof(id.low))) {
                std::cerr << "[HotStorage] Truncated file at node " << i << "\n";
                release_lock();
                return false;
            }

            Node node;
            node.id = id;
            read_from_buffer(&node.node_type, sizeof(node.node_type));
            read_from_buffer(&node.tau_created, sizeof(node.tau_created));
            read_from_buffer(&node.tau_accessed, sizeof(node.tau_accessed));
            read_from_buffer(&node.delta, sizeof(node.delta));
            read_from_buffer(&node.kappa.mu, sizeof(node.kappa.mu));
            read_from_buffer(&node.kappa.sigma_sq, sizeof(node.kappa.sigma_sq));
            read_from_buffer(&node.kappa.n, sizeof(node.kappa.n));

            // Vector
            node.nu.data.resize(EMBED_DIM);
            if (!read_from_buffer(node.nu.data.data(), EMBED_DIM * sizeof(float))) {
                std::cerr << "[HotStorage] Truncated file at node " << i << " vector\n";
                release_lock();
                return false;
            }

            // Payload
            size_t payload_size;
            read_from_buffer(&payload_size, sizeof(payload_size));
            if (payload_size > 0 && payload_size < 100 * 1024 * 1024) {  // Sanity: <100MB
                node.payload.resize(payload_size);
                if (!read_from_buffer(node.payload.data(), payload_size)) {
                    std::cerr << "[HotStorage] Truncated file at node " << i << " payload\n";
                    release_lock();
                    return false;
                }
            }

            // Edges
            size_t edge_count;
            read_from_buffer(&edge_count, sizeof(edge_count));
            if (edge_count < 100000) {  // Sanity check
                node.edges.reserve(edge_count);
                for (size_t e = 0; e < edge_count; ++e) {
                    Edge edge;
                    read_from_buffer(&edge.target.high, sizeof(edge.target.high));
                    read_from_buffer(&edge.target.low, sizeof(edge.target.low));
                    read_from_buffer(&edge.type, sizeof(edge.type));
                    read_from_buffer(&edge.weight, sizeof(edge.weight));
                    node.edges.push_back(edge);
                }
            }

            // Tags (always present in v2+)
            size_t tag_count = 0;
            read_from_buffer(&tag_count, sizeof(tag_count));
            if (tag_count < 10000) {  // Sanity check
                node.tags.reserve(tag_count);
                for (size_t t = 0; t < tag_count; ++t) {
                    size_t tag_len;
                    read_from_buffer(&tag_len, sizeof(tag_len));
                    if (tag_len < 10000 && offset + tag_len <= file_data.size()) {
                        std::string tag(reinterpret_cast<const char*>(file_data.data() + offset), tag_len);
                        offset += tag_len;
                        node.tags.push_back(std::move(tag));
                    }
                }
            }

            // Store node and quantized vector
            vectors_[id] = QuantizedVector::from_float(node.nu);
            nodes_[id] = std::move(node);
        }

        // Load HNSW index
        size_t index_size = 0;
        if (read_from_buffer(&index_size, sizeof(index_size)) && index_size > 0 &&
            index_size < 100 * 1024 * 1024 && offset + index_size <= file_data.size()) {
            std::vector<uint8_t> index_data(file_data.data() + offset, file_data.data() + offset + index_size);
            index_ = HNSWIndex::deserialize(index_data);
        }

        release_lock();
        return true;
    }

private:
    std::unordered_map<NodeId, Node, NodeIdHash> nodes_;
    std::unordered_map<NodeId, QuantizedVector, NodeIdHash> vectors_;
    HNSWIndex index_;
};

// Warm storage: memory-mapped with quantized vectors and HNSW index
class WarmStorage {
public:
    bool open(const std::string& path) {
        path_ = path;
        if (!region_.open(path)) return false;

        // Rebuild id_to_index_ from mmap'd metadata
        auto* header = region_.as<const StorageHeader>();
        if (!header || header->magic != STORAGE_MAGIC) {
            region_.close();
            return false;
        }

        auto* metas = region_.at<const NodeMeta>(header->meta_offset);
        id_to_index_.clear();
        for (size_t i = 0; i < header->node_count; ++i) {
            id_to_index_[metas[i].id] = i;
        }
        capacity_ = header->node_count;  // Set to current count for existing file

        // Rebuild HNSW index from stored vectors
        rebuild_index();
        return true;
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

        // Add to HNSW index for O(log n) search
        index_.insert(id, vec);
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
        index_.remove(id);
        // Note: doesn't reclaim space in mmap, just removes from index
        return true;
    }

    // HNSW-based search for O(log n) performance
    std::vector<std::pair<NodeId, float>> search(
        const QuantizedVector& query, size_t k) const
    {
        // Use HNSW index for efficient approximate nearest neighbor search
        return index_.search(query, k);
    }

private:
    // Rebuild HNSW index from mmap'd vectors (called on open)
    void rebuild_index() {
        if (!valid()) return;

        auto* header = region_.as<const StorageHeader>();
        auto* vectors = region_.at<const QuantizedVector>(header->vector_offset);

        // Clear and rebuild
        index_ = HNSWIndex();

        // Iterate through stored nodes and add to HNSW
        // Note: id_to_index_ should already be populated from mmap
        for (const auto& [id, idx] : id_to_index_) {
            index_.insert(id, vectors[idx]);
        }

        std::cerr << "[WarmStorage] Rebuilt HNSW index with " << index_.size() << " nodes\n";
    }

    std::string path_;
    MappedRegion region_;
    std::unordered_map<NodeId, size_t, NodeIdHash> id_to_index_;
    size_t capacity_ = 0;
    HNSWIndex index_;  // HNSW index for O(log n) search
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

// Tiered storage manager with WAL for concurrent access
class TieredStorage {
public:
    struct Config {
        std::string base_path;
        // Hot tier: in-memory with HNSW index, ~4KB per node
        // 10K nodes ≈ 40MB RAM - reasonable for most systems
        size_t hot_max_nodes = 10000;
        // Warm tier: mmap'd, ~2KB per node (no full payload)
        // 50K nodes ≈ 100MB disk, fast access via mmap
        size_t warm_max_nodes = 50000;
        // Age thresholds for demotion consideration
        Timestamp hot_threshold_ms = 604800000;   // 7 days before consider demoting
        Timestamp warm_threshold_ms = 2592000000; // 30 days before cold
        bool use_wal = true;               // Enable WAL for concurrency
        size_t wal_compact_threshold = 1000; // Compact WAL after this many entries
        bool use_unified_index = false;    // Phase 3: Use UnifiedIndex backend
        bool use_segments = false;         // Phase 3.4: Use SegmentManager backend
    };

    explicit TieredStorage(Config config)
        : config_(std::move(config))
        , wal_(config_.base_path + ".wal")
        , loaded_successfully_(false)
        , last_wal_seq_(0) {}

    bool initialize() {
        std::string hot_path = config_.base_path + ".hot";
        std::string warm_path = config_.base_path + ".warm";
        std::string cold_path = config_.base_path + ".cold";
        std::string unified_path = config_.base_path + ".unified";
        std::string manifest_path = config_.base_path + ".manifest";

        // Phase 3.4: Check for segment manager (auto-detect or config flag)
        std::ifstream manifest_check(manifest_path, std::ios::binary);
        bool segments_exist = manifest_check.good();
        manifest_check.close();

        if (segments_exist || config_.use_segments) {
            segments_ = std::make_unique<SegmentManager>(config_.base_path);
            if (segments_exist) {
                std::cerr << "[TieredStorage] Opening segment manager (Phase 3.4)\n";
                if (segments_->open()) {
                    std::cerr << "[TieredStorage] Segments: " << segments_->segment_count()
                              << " segments, " << segments_->total_nodes() << " nodes\n";
                    loaded_successfully_ = true;
                    return true;
                }
                std::cerr << "[TieredStorage] Failed to open segments, falling back\n";
                segments_.reset();
            } else if (config_.use_segments) {
                std::cerr << "[TieredStorage] Creating segment manager (Phase 3.4)\n";
                if (segments_->create()) {
                    std::cerr << "[TieredStorage] Segment manager created\n";
                    loaded_successfully_ = true;
                    return true;
                }
                std::cerr << "[TieredStorage] Failed to create segments\n";
                segments_.reset();
            }
        }

        // Phase 3: Check for unified index (auto-detect or config flag)
        std::ifstream unified_check(unified_path, std::ios::binary);
        bool unified_exists = unified_check.good();
        unified_check.close();

        if (unified_exists || config_.use_unified_index) {
            if (unified_exists) {
                std::cerr << "[TieredStorage] Opening unified index (Phase 3)\n";
                if (unified_.open(config_.base_path)) {
                    std::cerr << "[TieredStorage] Unified index: " << unified_.count()
                              << " nodes, O(1) load\n";
                    loaded_successfully_ = true;
                    return true;
                }
                std::cerr << "[TieredStorage] Failed to open unified index, falling back\n";
            } else if (config_.use_unified_index) {
                std::cerr << "[TieredStorage] Creating unified index (Phase 3)\n";
                if (unified_.create(config_.base_path)) {
                    std::cerr << "[TieredStorage] Unified index created\n";
                    loaded_successfully_ = true;
                    return true;
                }
                std::cerr << "[TieredStorage] Failed to create unified index\n";
            }
        }

        std::cerr << "[TieredStorage] Loading from: " << hot_path << "\n";

        // Check if database exists and needs upgrade
        std::ifstream check(hot_path, std::ios::binary);
        if (check.good()) {
            check.close();
            uint32_t version = HotStorage::detect_version(hot_path);
            if (version > 0 && version < HotStorage::STORAGE_VERSION) {
                std::cerr << "[TieredStorage] Database needs upgrade (v" << version
                          << " → v" << HotStorage::STORAGE_VERSION << "). "
                          << "Run 'chitta_cli upgrade'\n";
                return false;  // Fail initialization
            }
        }

        // Try to load existing hot tier (snapshot)
        loaded_successfully_ = hot_.load(hot_path);
        std::cerr << "[TieredStorage] Load result: " << (loaded_successfully_ ? "success" : "failed")
                  << ", nodes: " << hot_.size() << "\n";

        // Open WAL and replay entries since snapshot
        if (config_.use_wal) {
            if (!wal_.open()) {
                std::cerr << "[TieredStorage] Warning: WAL open failed, using snapshot only\n";
            } else {
                // Replay WAL to catch up with other processes' writes
                size_t replayed = replay_wal();
                std::cerr << "[TieredStorage] Replayed " << replayed << " WAL entries\n";
            }
        }

        // Initialize warm storage: open existing or create new
        if (!warm_.open(warm_path) || !warm_.valid()) {
            // Warm file missing or invalid - create with configured capacity
            if (warm_.create(warm_path, config_.warm_max_nodes)) {
                std::cerr << "[TieredStorage] Created warm storage with capacity " << config_.warm_max_nodes << "\n";
            } else {
                std::cerr << "[TieredStorage] Warning: Could not create warm storage\n";
            }
        }

        // Initialize cold storage
        cold_.open(cold_path);

        return true;
    }

    bool insert(NodeId id, Node node) {
        // Phase 3.4: Delegate to segment manager if active
        if (use_segments()) {
            auto slot = segments_->insert(id, node);
            return slot.valid();
        }

        // Phase 3: Delegate to unified index if active
        if (use_unified()) {
            auto slot = unified_.insert(id, node);
            return slot.valid();
        }

        // WAL first (durability), then in-memory (visibility)
        // This ensures crash safety: if we crash after WAL append,
        // the node will be recovered on next startup
        if (config_.use_wal) {
            uint64_t seq = wal_.append(WalOp::Insert, node);
            if (seq == 0) {
                std::cerr << "[TieredStorage] WAL append failed for node " << id.to_string() << "\n";
                // Continue anyway - at least it will be in memory
            } else {
                last_wal_seq_ = seq;
            }
        }

        QuantizedVector qvec = QuantizedVector::from_float(node.nu);
        hot_.insert(id, std::move(node), std::move(qvec));
        return true;
    }

    // Update node confidence with WAL delta (Phase 2: 72 bytes vs ~500 for full node)
    bool update_confidence(NodeId id, const Confidence& kappa) {
        Node* node = hot_.get(id);
        if (!node) return false;

        node->kappa = kappa;

        if (config_.use_wal) {
            wal_.append_confidence(id, kappa);
        }

        return true;
    }

    // Add edge to node with WAL delta (Phase 2: 72 bytes vs ~500 for full node)
    bool add_edge(NodeId from, NodeId to, EdgeType type, float weight) {
        Node* node = hot_.get(from);
        if (!node) return false;

        Edge edge{to, type, weight};
        node->edges.push_back(edge);

        if (config_.use_wal) {
            wal_.append_edge(from, edge);
        }

        return true;
    }

    // Sync from WAL: see other processes' writes
    // Call this before reads to ensure we see the shared truth
    size_t sync_from_wal() {
        return sync_from_wal(nullptr);
    }

    // Sync from WAL with callback for each synced node
    // The callback receives (node, was_inserted) - use this to update indices
    // Phase 2: Uses sync_v2 to handle both full nodes and deltas
    size_t sync_from_wal(std::function<void(const Node&, bool)> on_sync) {
        if (!config_.use_wal) return 0;

        size_t applied = wal_.sync_v2([this, &on_sync](const WalReplayEntry& entry, uint64_t seq) {
            bool was_new = !hot_.contains(entry.id);
            bool needs_index_update = apply_wal_entry_v2(entry);

            if (seq > last_wal_seq_) {
                last_wal_seq_ = seq;
            }

            // Notify caller about synced full nodes (for index rebuilds)
            // Only call for full node inserts/updates, not deltas
            if (on_sync && needs_index_update && entry.has_full_node) {
                on_sync(entry.full_node, was_new);
            }
        });

        if (applied > 0) {
            std::cerr << "[TieredStorage] Synced " << applied << " entries from WAL (v2)\n";
        }

        return applied;
    }

    Node* get(NodeId id) {
        // Phase 3.4: Delegate to segment manager if active
        if (use_segments()) {
            return get_from_segments(id);
        }

        // Phase 3: Delegate to unified index if active
        if (use_unified()) {
            return get_from_unified(id);
        }

        // Note: caller should sync_from_wal() before get if needed
        // This keeps state mutation explicit

        // Check hot first
        if (auto* node = hot_.get(id)) {
            node->touch();
            // Record touch delta to WAL (Phase 2: 56 bytes vs ~500 for full node)
            if (config_.use_wal) {
                wal_.append_touch(id, node->tau_accessed);
            }
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
        if (use_segments()) {
            return segments_->find_segment(id) != nullptr;
        }
        if (use_unified()) {
            return unified_.lookup(id).valid();
        }
        return hot_.contains(id) || warm_.contains(id) || cold_.contains(id);
    }

    StorageTier tier(NodeId id) const {
        if (use_segments()) {
            return segments_->find_segment(id) ? StorageTier::Hot : StorageTier::Cold;
        }
        if (use_unified()) {
            return unified_.lookup(id).valid() ? StorageTier::Hot : StorageTier::Cold;
        }
        if (hot_.contains(id)) return StorageTier::Hot;
        if (warm_.contains(id)) return StorageTier::Warm;
        if (cold_.contains(id)) return StorageTier::Cold;
        return StorageTier::Cold;  // Default
    }

    std::vector<std::pair<NodeId, float>> search(
        const QuantizedVector& query, size_t k) const
    {
        // Phase 3.4: Delegate to segment manager if active
        if (use_segments()) {
            return segments_->search(query, k);
        }

        // Phase 3: Delegate to unified index if active
        if (use_unified()) {
            auto slot_results = unified_.search(query, k);
            std::vector<std::pair<NodeId, float>> results;
            results.reserve(slot_results.size());
            for (const auto& [slot, score] : slot_results) {
                auto* indexed = unified_.get_slot(slot);
                if (indexed) {
                    results.emplace_back(indexed->id, 1.0f - score);  // Convert distance to similarity
                }
            }
            return results;
        }

        // Note: caller should sync_from_wal() before search if needed
        // This keeps search() const and explicit about state mutation

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
    // Uses safe copy-then-delete pattern: only removes from source after
    // confirming destination accepted the node.
    void manage_tiers() {
        Timestamp current = now();

        // Skip demotion if warm storage isn't ready
        if (!warm_.valid()) {
            return;
        }

        // Only demote if we're at capacity
        if (hot_.size() <= config_.hot_max_nodes) {
            return;
        }

        // Find demotion candidates (non-destructive)
        auto candidates = hot_.find_demote_candidates([this, current](const Node& node) {
            float value = compute_value(node, current);
            bool low_value = value < 0.3f;
            bool very_old = (current - node.tau_accessed) > config_.warm_threshold_ms;
            return low_value || very_old;
        });

        // Safe copy-then-delete for each candidate
        size_t demoted_count = 0;
        for (const auto& id : candidates) {
            // Stop if we're back under capacity
            if (hot_.size() <= config_.hot_max_nodes) break;

            // Copy node data (don't remove yet)
            auto node_data = hot_.copy_node(id);
            if (!node_data) continue;

            const auto& [node, qvec] = *node_data;

            // Prepare metadata for warm tier
            NodeMeta meta;
            meta.id = id;
            meta.node_type = node.node_type;
            meta.tier = StorageTier::Warm;
            meta.tau_created = node.tau_created;
            meta.tau_accessed = node.tau_accessed;
            meta.confidence_mu = node.kappa.mu;
            meta.confidence_sigma = node.kappa.sigma_sq;
            meta.decay_rate = node.delta;

            // Try to insert into warm storage
            if (warm_.insert(id, meta, qvec)) {
                // Success! Now safe to remove from hot
                hot_.remove(id);
                demoted_count++;
            }
            // If insert failed, node stays in hot (safe)
        }

        if (demoted_count > 0) {
            std::cerr << "[TieredStorage] Demoted " << demoted_count << " nodes to warm tier\n";
        }

        // Warm→Cold demotion (similar safe pattern)
        std::vector<NodeId> cold_candidates;
        warm_.for_each([&](const NodeId& id, const NodeMeta& meta) {
            float age_days = static_cast<float>(current - meta.tau_accessed) / 86400000.0f;
            bool low_conf_old = (meta.confidence_mu < 0.2f && age_days > 7.0f);
            bool very_old = (age_days > 30.0f);
            if (low_conf_old || very_old) {
                cold_candidates.push_back(id);
            }
        });

        for (const auto& id : cold_candidates) {
            auto* meta = warm_.meta(id);
            if (!meta) continue;
            // Copy to cold, then remove from warm
            cold_.insert(id, *meta, {});
            warm_.remove(id);
        }
    }

    void sync() {
        // Phase 3.4: Segment manager handles its own sync
        if (use_segments()) {
            segments_->sync();
            std::cerr << "[TieredStorage] Segments synced\n";
            return;
        }

        // Phase 3: Unified index handles its own sync
        if (use_unified()) {
            unified_.sync();
            std::cerr << "[TieredStorage] Unified index synced\n";
            return;
        }

        std::cerr << "[TieredStorage] sync() called: hot_size=" << hot_.size()
                  << ", loaded_successfully=" << loaded_successfully_ << "\n";

        // First, sync from WAL to get any pending writes from other processes
        if (config_.use_wal) {
            sync_from_wal();
        }

        // Only save if we have data (prevents overwriting on failed load)
        if (hot_.size() > 0 || loaded_successfully_) {
            std::cerr << "[TieredStorage] Saving hot tier (snapshot)\n";
            hot_.save(config_.base_path + ".hot");

            // After successful snapshot, we can truncate WAL
            // This is safe because snapshot contains all WAL entries
            if (config_.use_wal && wal_.next_sequence() > config_.wal_compact_threshold) {
                std::cerr << "[TieredStorage] Compacting WAL (seq=" << wal_.next_sequence() << ")\n";
                wal_.truncate();
            }
        } else {
            std::cerr << "[TieredStorage] SKIPPING save (no data, load failed)\n";
        }

        warm_.sync();
        if (cold_.size() > 0 || loaded_successfully_) {
            cold_.save(config_.base_path + ".cold");
        }
    }

    // Force WAL compaction (call after major operations)
    void compact_wal() {
        if (!config_.use_wal) return;

        // Sync all pending entries
        sync_from_wal();

        // Save snapshot
        hot_.save(config_.base_path + ".hot");

        // Truncate WAL
        wal_.truncate();
        std::cerr << "[TieredStorage] WAL compacted\n";
    }

    size_t hot_size() const {
        if (use_segments()) return segments_->total_nodes();
        if (use_unified()) return unified_.count();
        return hot_.size();
    }
    size_t warm_size() const {
        if (use_segments()) return 0;
        if (use_unified()) return 0;
        return warm_.size();
    }
    size_t cold_size() const {
        if (use_segments()) return 0;
        if (use_unified()) return 0;
        return cold_.size();
    }
    size_t total_size() const {
        if (use_segments()) return segments_->total_nodes();
        if (use_unified()) return unified_.count();
        return hot_size() + warm_size() + cold_size();
    }

    void for_each_hot(std::function<void(const NodeId&, const Node&)> fn) const {
        if (use_segments()) {
            // TODO: Iterate all segments - for now skip
            return;
        }
        if (use_unified()) {
            // Iterate all nodes in unified index (all are "hot" in unified mode)
            for (size_t i = 0; i < unified_.count() + unified_.deleted_count(); ++i) {
                SlotId slot(static_cast<uint32_t>(i));
                auto* indexed = unified_.get_slot(slot);
                if (!indexed) continue;

                auto* meta = unified_.meta(slot);
                auto* qvec = unified_.vector(slot);
                if (!meta || !qvec) continue;

                // Reconstruct node for callback
                Node node;
                node.id = indexed->id;
                node.node_type = meta->node_type;
                node.nu = qvec->to_float();
                node.tau_created = meta->tau_created;
                node.tau_accessed = meta->tau_accessed;
                node.delta = meta->decay_rate;
                node.kappa.mu = meta->confidence_mu;
                node.kappa.sigma_sq = meta->confidence_sigma;

                fn(indexed->id, node);
            }
            return;
        }
        hot_.for_each(fn);
    }

private:
    // Replay all WAL entries (called on startup)
    // Phase 2: Handles both full nodes and deltas via replay_v2
    size_t replay_wal() {
        if (!config_.use_wal) return 0;

        // Use replay_v2 to handle all WAL formats including deltas
        size_t count = wal_.replay_v2(0, [this](const WalReplayEntry& entry, uint64_t seq) {
            apply_wal_entry_v2(entry);
            if (seq > last_wal_seq_) {
                last_wal_seq_ = seq;
            }
        });

        return count;
    }

    // Apply a single WAL entry to in-memory state (legacy, V0/V1 full nodes only)
    void apply_wal_entry(WalOp op, const Node& node) {
        switch (op) {
            case WalOp::Insert:
            case WalOp::Update: {
                // Check if we already have this node (from snapshot)
                // If so, update only if WAL entry is newer
                if (auto* existing = hot_.get(node.id)) {
                    if (node.tau_accessed > existing->tau_accessed) {
                        // WAL entry is newer, update
                        QuantizedVector qvec = QuantizedVector::from_float(node.nu);
                        hot_.insert(node.id, node, std::move(qvec));
                    }
                } else {
                    // New node, insert
                    QuantizedVector qvec = QuantizedVector::from_float(node.nu);
                    hot_.insert(node.id, node, std::move(qvec));
                }
                break;
            }
            case WalOp::Delete:
                hot_.remove(node.id);
                break;
            case WalOp::Checkpoint:
                // Checkpoint entries are informational, no action needed
                break;
        }
    }

    // Apply a WAL replay entry to in-memory state (Phase 2: supports deltas)
    // Returns true if a full node was inserted/updated (for index rebuild callbacks)
    bool apply_wal_entry_v2(const WalReplayEntry& entry) {
        if (entry.op == WalOp::Delete) {
            hot_.remove(entry.id);
            return false;
        }

        if (entry.op == WalOp::Checkpoint) {
            return false;
        }

        // Full node entry (V0, V1)
        if (entry.has_full_node) {
            if (auto* existing = hot_.get(entry.id)) {
                if (entry.full_node.tau_accessed > existing->tau_accessed) {
                    QuantizedVector qvec = QuantizedVector::from_float(entry.full_node.nu);
                    hot_.insert(entry.id, entry.full_node, std::move(qvec));
                }
            } else {
                QuantizedVector qvec = QuantizedVector::from_float(entry.full_node.nu);
                hot_.insert(entry.id, entry.full_node, std::move(qvec));
            }
            return true;
        }

        // Delta entries (V2, V3, V4) - only update existing nodes
        auto* existing = hot_.get(entry.id);
        if (!existing) {
            // Node not in hot storage - delta can't apply
            // (This is expected if node is in warm/cold tier)
            return false;
        }

        // Touch delta (V2)
        if (entry.has_touch) {
            if (entry.touch_tau > existing->tau_accessed) {
                existing->tau_accessed = entry.touch_tau;
            }
            return false;
        }

        // Confidence delta (V3)
        if (entry.has_confidence) {
            // Apply if newer (use confidence.tau for ordering)
            if (entry.confidence.tau > existing->kappa.tau) {
                existing->kappa = entry.confidence;
            }
            return false;
        }

        // Edge delta (V4)
        if (entry.has_edge) {
            // Add edge if not already present
            bool found = false;
            for (const auto& e : existing->edges) {
                if (e.target == entry.edge.target && e.type == entry.edge.type) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                existing->edges.push_back(entry.edge);
            }
            return false;
        }

        return false;
    }

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

        // Record touch delta to WAL (promotion counts as access)
        if (config_.use_wal) {
            wal_.append_touch(id, node.tau_accessed);
        }

        QuantizedVector vec = *qvec;
        hot_.insert(id, std::move(node), std::move(vec));
        return hot_.get(id);
    }

    bool use_unified() const { return unified_.valid(); }
    bool use_segments() const { return segments_ && segments_->valid(); }

    // Reconstruct Node from SegmentManager (for API compatibility)
    Node* get_from_segments(NodeId id) {
        auto* seg = segments_->find_segment(id);
        if (!seg) return nullptr;

        auto slot = seg->lookup(id);
        if (!slot.valid()) return nullptr;

        auto* indexed = seg->get_slot(slot);
        auto* meta = seg->meta(slot);
        auto* qvec = seg->vector(slot);
        if (!indexed || !meta || !qvec) return nullptr;

        // Reconstruct in cache
        Node& node = unified_cache_[id];
        node.id = id;
        node.node_type = meta->node_type;
        node.nu = qvec->to_float();
        node.tau_created = meta->tau_created;
        node.tau_accessed = meta->tau_accessed;
        node.delta = meta->decay_rate;
        node.kappa.mu = meta->confidence_mu;
        node.kappa.sigma_sq = meta->confidence_sigma;
        node.touch();

        if (unified_cache_.size() > 1000) {
            unified_cache_.clear();
            unified_cache_[id] = node;
        }

        return &unified_cache_[id];
    }

    // Reconstruct Node from UnifiedIndex (for API compatibility)
    Node* get_from_unified(NodeId id) {
        auto slot = unified_.lookup(id);
        if (!slot.valid()) return nullptr;

        auto* indexed = unified_.get_slot(slot);
        auto* meta = unified_.meta(slot);
        auto* qvec = unified_.vector(slot);
        if (!indexed || !meta || !qvec) return nullptr;

        // Reconstruct in cache (allows returning Node*)
        Node& node = unified_cache_[id];
        node.id = id;
        node.node_type = meta->node_type;
        node.nu = qvec->to_float();
        node.tau_created = meta->tau_created;
        node.tau_accessed = meta->tau_accessed;
        node.delta = meta->decay_rate;
        node.kappa.mu = meta->confidence_mu;
        node.kappa.sigma_sq = meta->confidence_sigma;

        // Update access time
        node.touch();

        // Limit cache size
        if (unified_cache_.size() > 1000) {
            unified_cache_.clear();
            unified_cache_[id] = node;
        }

        return &unified_cache_[id];
    }

    Config config_;
    WriteAheadLog wal_;
    HotStorage hot_;
    WarmStorage warm_;
    ColdStorage cold_;
    UnifiedIndex unified_;
    std::unique_ptr<SegmentManager> segments_;
    std::unordered_map<NodeId, Node, NodeIdHash> unified_cache_;  // Reconstructed nodes cache
    bool loaded_successfully_;
    uint64_t last_wal_seq_;
};

} // namespace chitta
