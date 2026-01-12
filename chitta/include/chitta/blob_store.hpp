#pragma once
// Blob Store: Append-only variable-length data storage
//
// Used for storing payloads and edges in UnifiedIndex.
// Simple design: [Header][Blob1][Blob2]...
// Each blob: [size:4][data:size]
//
// Offset 0 is reserved (indicates "no data").

#include "mmap.hpp"
#include <cstdint>
#include <cstring>
#include <vector>
#include <iostream>

namespace chitta {

constexpr uint32_t BLOB_STORE_MAGIC = 0x424C4F42;  // "BLOB"
constexpr uint32_t BLOB_STORE_VERSION = 1;

struct BlobStoreHeader {
    uint32_t magic;
    uint32_t version;
    uint64_t total_bytes;     // File size
    uint64_t used_bytes;      // Bytes in use (next write position)
    uint64_t blob_count;      // Number of blobs stored
    uint64_t checksum;
    uint8_t reserved[24];     // Pad to 64 bytes
};
static_assert(sizeof(BlobStoreHeader) == 64, "BlobStoreHeader must be 64 bytes");

class BlobStore {
public:
    static constexpr size_t INITIAL_SIZE = 16 * 1024 * 1024;  // 16MB
    static constexpr double GROWTH_FACTOR = 1.5;  // Less aggressive for large stores
    static constexpr size_t MAX_SIZE = 256ULL * 1024 * 1024 * 1024;  // 256GB (was 4GB)

    BlobStore() = default;
    ~BlobStore() { close(); }

    BlobStore(const BlobStore&) = delete;
    BlobStore& operator=(const BlobStore&) = delete;

    bool create(const std::string& path, size_t initial_size = INITIAL_SIZE) {
        path_ = path;
        initial_size = std::max(initial_size, sizeof(BlobStoreHeader) + 1024);

        if (!region_.create(path, initial_size)) {
            return false;
        }

        auto* header = region_.as<BlobStoreHeader>();
        header->magic = BLOB_STORE_MAGIC;
        header->version = BLOB_STORE_VERSION;
        header->total_bytes = initial_size;
        header->used_bytes = sizeof(BlobStoreHeader);
        header->blob_count = 0;
        header->checksum = compute_checksum(header);

        return true;
    }

    bool open(const std::string& path) {
        path_ = path;

        if (!region_.open(path, false)) {
            return false;
        }

        auto* header = region_.as<const BlobStoreHeader>();
        if (header->magic != BLOB_STORE_MAGIC) {
            std::cerr << "[BlobStore] Invalid magic\n";
            return false;
        }

        if (header->version > BLOB_STORE_VERSION) {
            std::cerr << "[BlobStore] Version too new: " << header->version << "\n";
            return false;
        }

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
        auto* header = region_.as<BlobStoreHeader>();
        header->checksum = compute_checksum(header);
        region_.sync();
    }

    bool valid() const { return region_.valid(); }

    // Store a blob, returns offset (0 on failure)
    uint64_t store(const uint8_t* data, uint32_t size) {
        if (!region_.valid() || size == 0) return 0;

        auto* header = region_.as<BlobStoreHeader>();
        size_t required = sizeof(uint32_t) + size;

        // Grow if needed
        while (header->used_bytes + required > header->total_bytes) {
            if (!grow()) return 0;
            header = region_.as<BlobStoreHeader>();
        }

        uint64_t offset = header->used_bytes;

        // Write size prefix
        uint32_t* size_ptr = region_.at<uint32_t>(offset);
        *size_ptr = size;

        // Write data
        uint8_t* data_ptr = region_.at<uint8_t>(offset + sizeof(uint32_t));
        std::memcpy(data_ptr, data, size);

        header->used_bytes += required;
        header->blob_count++;

        return offset;
    }

    // Store from vector
    uint64_t store(const std::vector<uint8_t>& data) {
        if (data.empty()) return 0;
        return store(data.data(), static_cast<uint32_t>(data.size()));
    }

    // Read a blob at offset
    std::vector<uint8_t> read(uint64_t offset) const {
        if (!region_.valid() || offset == 0) return {};

        auto* header = region_.as<const BlobStoreHeader>();
        if (offset >= header->used_bytes) return {};

        const uint32_t* size_ptr = region_.at<const uint32_t>(offset);
        uint32_t size = *size_ptr;

        if (offset + sizeof(uint32_t) + size > header->used_bytes) {
            return {};  // Corrupted
        }

        const uint8_t* data_ptr = region_.at<const uint8_t>(offset + sizeof(uint32_t));
        return std::vector<uint8_t>(data_ptr, data_ptr + size);
    }

    // Read into existing buffer, returns actual size (0 on failure)
    uint32_t read(uint64_t offset, uint8_t* buffer, uint32_t max_size) const {
        if (!region_.valid() || offset == 0) return 0;

        auto* header = region_.as<const BlobStoreHeader>();
        if (offset >= header->used_bytes) return 0;

        const uint32_t* size_ptr = region_.at<const uint32_t>(offset);
        uint32_t size = *size_ptr;

        if (offset + sizeof(uint32_t) + size > header->used_bytes) {
            return 0;
        }

        uint32_t copy_size = std::min(size, max_size);
        const uint8_t* data_ptr = region_.at<const uint8_t>(offset + sizeof(uint32_t));
        std::memcpy(buffer, data_ptr, copy_size);
        return copy_size;
    }

    // Get size of blob at offset without reading data
    uint32_t size_at(uint64_t offset) const {
        if (!region_.valid() || offset == 0) return 0;

        auto* header = region_.as<const BlobStoreHeader>();
        if (offset >= header->used_bytes) return 0;

        return *region_.at<const uint32_t>(offset);
    }

    size_t blob_count() const {
        if (!region_.valid()) return 0;
        return region_.as<const BlobStoreHeader>()->blob_count;
    }

    size_t used_bytes() const {
        if (!region_.valid()) return 0;
        return region_.as<const BlobStoreHeader>()->used_bytes;
    }

private:
    bool grow() {
        auto* header = region_.as<BlobStoreHeader>();
        size_t current = header->total_bytes;
        size_t new_size = static_cast<size_t>(current * GROWTH_FACTOR);

        // At least 25% growth
        new_size = std::max(new_size, current + current / 4);

        // Round up to 16MB boundary for better performance
        constexpr size_t ALIGN_SIZE = 16ULL * 1024 * 1024;
        new_size = (new_size + ALIGN_SIZE - 1) & ~(ALIGN_SIZE - 1);

        if (new_size > MAX_SIZE) {
            std::cerr << "[BlobStore] Cannot grow beyond " << MAX_SIZE << " bytes\n";
            return false;
        }

        // Use MappedRegion's resize (handles sync, unmap, truncate, remap)
        if (!region_.resize(new_size)) {
            std::cerr << "[BlobStore] Failed to resize to " << new_size << " bytes\n";
            return false;
        }

        header = region_.as<BlobStoreHeader>();
        header->total_bytes = new_size;

        std::cerr << "[BlobStore] Grew from " << current << " to " << new_size << " bytes\n";
        return true;
    }

    static uint64_t compute_checksum(const BlobStoreHeader* header) {
        return crc32(reinterpret_cast<const uint8_t*>(header), 40);
    }

    std::string path_;
    MappedRegion region_;
};

} // namespace chitta
