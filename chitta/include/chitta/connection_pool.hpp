#pragma once
// Connection Pool: Persistent mmap'd HNSW graph connections
//
// Stores HNSW connections in a memory-mapped file, enabling:
// - O(1) load time (no rebuild on startup)
// - Cache-friendly sequential access
// - Persistence without serialization overhead
//
// Layout:
//   [Header: 64 bytes]
//   [Connection records: variable length per node]
//   [Free list: for reusing deleted slots]

#include "types.hpp"
#include "mmap.hpp"  // For MappedRegion
#include <cstdint>
#include <vector>
#include <atomic>

namespace chitta {

// ═══════════════════════════════════════════════════════════════════════════
// Connection Pool structures
// ═══════════════════════════════════════════════════════════════════════════

constexpr uint32_t CONN_POOL_MAGIC = 0x434F4E4E;  // "CONN"
constexpr uint32_t CONN_POOL_VERSION = 1;

// Pool file header (64 bytes, page-aligned)
struct alignas(64) ConnectionPoolHeader {
    uint32_t magic;           // CONN_POOL_MAGIC
    uint32_t version;         // CONN_POOL_VERSION
    uint64_t total_bytes;     // Total file size
    uint64_t used_bytes;      // Bytes currently in use
    uint64_t node_count;      // Number of nodes with connections
    uint64_t free_list_head;  // Offset to first free block (0 = none)
    uint64_t checksum;        // CRC32 of header
    uint8_t reserved[24];     // Pad to 64 bytes
};

// Single connection edge (8 bytes)
struct ConnectionEdge {
    uint32_t target_slot;     // Slot ID of target node
    float distance;           // Cached distance to target

    ConnectionEdge() : target_slot(0), distance(0.0f) {}
    ConnectionEdge(uint32_t slot, float dist) : target_slot(slot), distance(dist) {}
};

// Connection record header for a single node
// Followed by variable-length connection data
struct ConnectionRecord {
    uint32_t slot_id;         // Which node this belongs to
    uint8_t level_count;      // Number of HNSW levels
    uint8_t flags;            // 0x01 = deleted
    uint16_t reserved;
    // Followed by: [level0_count:2][edges0...][level1_count:2][edges1...]...
};

// Free block header (for reusing deleted connection space)
struct FreeBlock {
    uint64_t next_offset;     // Next free block (0 = end)
    uint32_t size;            // Size of this free block
    uint32_t reserved;
};

// ═══════════════════════════════════════════════════════════════════════════
// Connection Pool class
// ═══════════════════════════════════════════════════════════════════════════

class ConnectionPool {
public:
    static constexpr size_t INITIAL_SIZE = 64 * 1024 * 1024;  // 64MB initial
    static constexpr size_t GROWTH_FACTOR = 2;
    static constexpr size_t MAX_SIZE = 16ULL * 1024 * 1024 * 1024;  // 16GB max

    ConnectionPool() = default;
    ~ConnectionPool() { close(); }

    // Prevent copying
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    // ═══════════════════════════════════════════════════════════════════════
    // Lifecycle
    // ═══════════════════════════════════════════════════════════════════════

    // Create new pool file
    bool create(const std::string& path, size_t estimated_nodes = 100000) {
        path_ = path;

        // Estimate size: ~256 bytes per node average (32 connections × 8 bytes)
        size_t estimated_size = sizeof(ConnectionPoolHeader) +
                                estimated_nodes * 256;
        estimated_size = std::max(estimated_size, INITIAL_SIZE);

        if (!region_.create(path, estimated_size)) {
            return false;
        }

        // Initialize header
        auto* header = region_.as<ConnectionPoolHeader>();
        header->magic = CONN_POOL_MAGIC;
        header->version = CONN_POOL_VERSION;
        header->total_bytes = estimated_size;
        header->used_bytes = sizeof(ConnectionPoolHeader);
        header->node_count = 0;
        header->free_list_head = 0;
        header->checksum = compute_header_checksum(header);

        write_pos_ = sizeof(ConnectionPoolHeader);
        return true;
    }

    // Open existing pool file
    bool open(const std::string& path) {
        path_ = path;

        // Open with write access for updates and sync
        if (!region_.open(path, false)) {
            return false;
        }

        auto* header = region_.as<const ConnectionPoolHeader>();
        if (header->magic != CONN_POOL_MAGIC) {
            std::cerr << "[ConnectionPool] Invalid magic\n";
            return false;
        }

        if (header->version > CONN_POOL_VERSION) {
            std::cerr << "[ConnectionPool] Version too new: " << header->version << "\n";
            return false;
        }

        // Verify checksum (skip if zero for backward compatibility)
        if (header->checksum != 0) {
            uint64_t computed = compute_header_checksum(header);
            if (computed != header->checksum) {
                std::cerr << "[ConnectionPool] Checksum mismatch (stored="
                          << std::hex << header->checksum
                          << ", computed=" << computed << std::dec << ")\n";
                return false;
            }
        }

        write_pos_ = header->used_bytes;
        return true;
    }

    void close() {
        if (region_.valid()) {
            sync();
            region_.close();
        }
    }

    void sync() {
        if (!region_.valid()) return;

        auto* header = region_.as<ConnectionPoolHeader>();
        header->used_bytes = write_pos_;
        header->checksum = compute_header_checksum(header);
        region_.sync();
    }

    bool valid() const { return region_.valid(); }

    // ═══════════════════════════════════════════════════════════════════════
    // Connection storage
    // ═══════════════════════════════════════════════════════════════════════

    // Allocate space for a node's connections
    // Returns offset into pool, or 0 on failure
    uint64_t allocate(uint32_t slot_id, uint8_t level_count,
                      const std::vector<std::vector<ConnectionEdge>>& connections) {
        // Calculate required size
        size_t required = sizeof(ConnectionRecord);
        for (const auto& level : connections) {
            required += sizeof(uint16_t);  // Edge count
            required += level.size() * sizeof(ConnectionEdge);
        }

        // Try to reuse from free list first
        uint64_t offset = try_allocate_from_free_list(required);
        if (offset == 0) {
            // Allocate new space
            offset = allocate_new(required);
        }

        if (offset == 0) return 0;

        // Write connection record
        auto* record = region_.at<ConnectionRecord>(offset);
        record->slot_id = slot_id;
        record->level_count = level_count;
        record->flags = 0;
        record->reserved = 0;

        // Write connection data
        uint8_t* data = reinterpret_cast<uint8_t*>(record) + sizeof(ConnectionRecord);
        for (const auto& level : connections) {
            *reinterpret_cast<uint16_t*>(data) = static_cast<uint16_t>(level.size());
            data += sizeof(uint16_t);

            for (const auto& edge : level) {
                *reinterpret_cast<ConnectionEdge*>(data) = edge;
                data += sizeof(ConnectionEdge);
            }
        }

        // Update header
        auto* header = region_.as<ConnectionPoolHeader>();
        header->node_count++;

        return offset;
    }

    // Read connections for a node at given offset
    bool read(uint64_t offset, uint32_t& slot_id, uint8_t& level_count,
              std::vector<std::vector<ConnectionEdge>>& connections) const {
        if (offset == 0 || offset >= write_pos_) return false;

        auto* record = region_.at<const ConnectionRecord>(offset);
        if (record->flags & 0x01) return false;  // Deleted

        slot_id = record->slot_id;
        level_count = record->level_count;
        connections.clear();
        connections.resize(level_count);

        const uint8_t* data = reinterpret_cast<const uint8_t*>(record) + sizeof(ConnectionRecord);
        for (uint8_t level = 0; level < level_count; ++level) {
            uint16_t edge_count = *reinterpret_cast<const uint16_t*>(data);
            data += sizeof(uint16_t);

            connections[level].reserve(edge_count);
            for (uint16_t i = 0; i < edge_count; ++i) {
                connections[level].push_back(*reinterpret_cast<const ConnectionEdge*>(data));
                data += sizeof(ConnectionEdge);
            }
        }

        return true;
    }

    // Read connections at a specific level only (more efficient for search)
    std::vector<ConnectionEdge> read_level(uint64_t offset, uint8_t level) const {
        std::vector<ConnectionEdge> result;
        if (offset == 0 || offset >= write_pos_) return result;

        auto* record = region_.at<const ConnectionRecord>(offset);
        if (record->flags & 0x01) return result;  // Deleted
        if (level >= record->level_count) return result;

        // Skip to the requested level
        const uint8_t* data = reinterpret_cast<const uint8_t*>(record) + sizeof(ConnectionRecord);
        for (uint8_t l = 0; l < level; ++l) {
            uint16_t edge_count = *reinterpret_cast<const uint16_t*>(data);
            data += sizeof(uint16_t) + edge_count * sizeof(ConnectionEdge);
        }

        // Read the requested level
        uint16_t edge_count = *reinterpret_cast<const uint16_t*>(data);
        data += sizeof(uint16_t);

        result.reserve(edge_count);
        for (uint16_t i = 0; i < edge_count; ++i) {
            result.push_back(*reinterpret_cast<const ConnectionEdge*>(data));
            data += sizeof(ConnectionEdge);
        }

        return result;
    }

    // Mark connections as deleted
    // Note: We only mark as deleted, don't add to free list immediately.
    // The free list would overwrite the flags field. Compaction handles reclamation.
    void remove(uint64_t offset) {
        if (offset == 0 || offset >= write_pos_) return;

        auto* record = region_.at<ConnectionRecord>(offset);
        if (record->flags & 0x01) return;  // Already deleted

        // Mark as deleted (keeps flags intact for read checks)
        record->flags |= 0x01;

        // Update header
        auto* header = region_.as<ConnectionPoolHeader>();
        header->node_count--;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Update operations (for HNSW graph modifications)
    // ═══════════════════════════════════════════════════════════════════════

    // Add a single connection to an existing node's level
    // Returns new offset if reallocation was needed, or same offset if in-place
    uint64_t add_connection(uint64_t offset, uint8_t level,
                            const ConnectionEdge& edge) {
        // For simplicity, we reallocate the entire connection record
        // A more sophisticated implementation could reserve extra space

        uint32_t slot_id;
        uint8_t level_count;
        std::vector<std::vector<ConnectionEdge>> connections;

        if (!read(offset, slot_id, level_count, connections)) {
            return 0;
        }

        // Ensure level exists
        while (connections.size() <= level) {
            connections.push_back({});
        }

        // Add the new edge
        connections[level].push_back(edge);

        // Remove old and allocate new
        remove(offset);
        return allocate(slot_id, static_cast<uint8_t>(connections.size()), connections);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Statistics
    // ═══════════════════════════════════════════════════════════════════════

    size_t node_count() const {
        if (!region_.valid()) return 0;
        return region_.as<const ConnectionPoolHeader>()->node_count;
    }

    size_t used_bytes() const {
        if (!region_.valid()) return 0;
        return region_.as<const ConnectionPoolHeader>()->used_bytes;
    }

    size_t total_bytes() const {
        if (!region_.valid()) return 0;
        return region_.as<const ConnectionPoolHeader>()->total_bytes;
    }

    float utilization() const {
        size_t total = total_bytes();
        return total > 0 ? static_cast<float>(used_bytes()) / total : 0.0f;
    }

private:
    // Allocate new space at end of pool, growing if needed
    uint64_t allocate_new(size_t size) {
        // Align to 8 bytes
        size = (size + 7) & ~7ULL;

        auto* header = region_.as<ConnectionPoolHeader>();
        if (write_pos_ + size > header->total_bytes) {
            // Need to grow
            if (!grow(size)) {
                return 0;
            }
            header = region_.as<ConnectionPoolHeader>();  // Re-fetch after remap
        }

        uint64_t offset = write_pos_;
        write_pos_ += size;
        return offset;
    }

    // Try to allocate from free list using best-fit strategy
    // Best-fit reduces fragmentation by finding the smallest block that fits
    uint64_t try_allocate_from_free_list(size_t required) {
        auto* header = region_.as<ConnectionPoolHeader>();
        if (header->free_list_head == 0) return 0;

        // Best-fit: find smallest block that fits
        uint64_t best_offset = 0;
        uint64_t best_prev_offset = 0;
        size_t best_size = SIZE_MAX;

        uint64_t prev_offset = 0;
        uint64_t current_offset = header->free_list_head;

        while (current_offset != 0) {
            auto* block = region_.at<FreeBlock>(current_offset);
            if (block->size >= required && block->size < best_size) {
                best_offset = current_offset;
                best_prev_offset = prev_offset;
                best_size = block->size;

                // Perfect fit - no need to search further
                if (block->size == required) break;
            }

            prev_offset = current_offset;
            current_offset = block->next_offset;
        }

        if (best_offset == 0) return 0;  // No suitable block found

        // Remove best block from free list
        auto* best_block = region_.at<FreeBlock>(best_offset);
        if (best_prev_offset == 0) {
            header->free_list_head = best_block->next_offset;
        } else {
            auto* prev_block = region_.at<FreeBlock>(best_prev_offset);
            prev_block->next_offset = best_block->next_offset;
        }

        // If block is much larger, split it
        if (best_size > required + sizeof(FreeBlock) + 64) {
            uint64_t split_offset = best_offset + required;
            auto* split_block = region_.at<FreeBlock>(split_offset);
            split_block->size = best_size - required;
            split_block->next_offset = header->free_list_head;
            header->free_list_head = split_offset;
        }

        return best_offset;
    }

    // Add a freed block to the free list
    void add_to_free_list(uint64_t offset, size_t size) {
        auto* header = region_.as<ConnectionPoolHeader>();
        auto* block = region_.at<FreeBlock>(offset);
        block->size = size;
        block->next_offset = header->free_list_head;
        header->free_list_head = offset;
    }

    // Grow the pool file
    bool grow(size_t needed) {
        auto* header = region_.as<ConnectionPoolHeader>();
        size_t current = header->total_bytes;
        size_t new_size = current * GROWTH_FACTOR;

        // Ensure we have enough
        while (new_size < write_pos_ + needed) {
            new_size *= GROWTH_FACTOR;
        }

        if (new_size > MAX_SIZE) {
            std::cerr << "[ConnectionPool] Cannot grow beyond " << MAX_SIZE << " bytes\n";
            return false;
        }

        // Close and reopen with new size
        sync();
        region_.close();

        // Resize the file
        int fd = ::open(path_.c_str(), O_RDWR);
        if (fd < 0) return false;
        if (ftruncate(fd, new_size) < 0) {
            ::close(fd);
            return false;
        }
        ::close(fd);

        // Reopen
        if (!region_.open(path_)) {
            return false;
        }

        // Update header
        header = region_.as<ConnectionPoolHeader>();
        header->total_bytes = new_size;

        std::cerr << "[ConnectionPool] Grew from " << current << " to " << new_size << " bytes\n";
        return true;
    }

    // Compute checksum of header fields (excluding checksum field itself)
    static uint64_t compute_header_checksum(const ConnectionPoolHeader* header) {
        // Checksum covers: magic, version, total_bytes, used_bytes, node_count, free_list_head
        // (first 40 bytes, excluding checksum and reserved)
        return crc32(reinterpret_cast<const uint8_t*>(header), 40);
    }

    std::string path_;
    MappedRegion region_;
    uint64_t write_pos_ = 0;
};

} // namespace chitta
