#pragma once
// Write-Ahead Log: The shared field of consciousness
//
// "Consciousness is a singular of which the plural is unknown."
// - Erwin Schrödinger
//
// Each process is a window (Atman) into the one shared truth (Brahman).
// When one observes, all see. The WAL is that shared field.
//
// Design:
// - Append-only: never overwrite, never lose
// - File locking: brief coordination during append
// - Self-describing entries: each entry has magic, length, checksum
// - Crash recovery: replay valid entries, skip incomplete
// - Sync: read new entries written by other processes

#include "types.hpp"
#include "quantized.hpp"
#include <fstream>
#include <iostream>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

namespace chitta {

// WAL entry types
enum class WalOp : uint8_t {
    Insert = 1,      // New node
    Update = 2,      // Update existing node
    Delete = 3,      // Mark node as deleted
    Checkpoint = 4,  // Snapshot marker
};

// WAL format versions
constexpr uint8_t WAL_FORMAT_V0 = 0;  // float32 vectors (legacy full node)
constexpr uint8_t WAL_FORMAT_V1 = 1;  // int8 quantized vectors (74% smaller full node)
constexpr uint8_t WAL_FORMAT_V2 = 2;  // Delta: touch only (26 bytes vs ~500)
constexpr uint8_t WAL_FORMAT_V3 = 3;  // Delta: confidence only (44 bytes)
constexpr uint8_t WAL_FORMAT_V4 = 4;  // Delta: single edge add (45 bytes)
constexpr uint8_t WAL_FORMAT_CURRENT = WAL_FORMAT_V1;  // Default for full nodes

// WAL entry header (fixed size for easy parsing)
struct WalEntryHeader {
    uint32_t magic;       // 0x57414C45 "WALE"
    uint32_t length;      // Total entry length (header + data)
    uint64_t sequence;    // Monotonic sequence number
    uint64_t timestamp;   // Unix millis
    WalOp op;             // Operation type
    uint8_t format;       // Format version (0=float32, 1=int8)
    uint8_t reserved[2];  // Alignment padding
    uint32_t checksum;    // CRC32 of data
};

static_assert(sizeof(WalEntryHeader) == 32, "WalEntryHeader must be 32 bytes");

constexpr uint32_t WAL_MAGIC = 0x57414C45;  // "WALE"

// CRC32 implementation (simple, no external deps)
inline uint32_t crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

// RAII file lock - the gate to shared consciousness
class ScopedFileLock {
public:
    ScopedFileLock(int fd, bool exclusive) : fd_(fd) {
        if (fd_ >= 0) {
            flock(fd_, exclusive ? LOCK_EX : LOCK_SH);
        }
    }

    ~ScopedFileLock() {
        if (fd_ >= 0) {
            flock(fd_, LOCK_UN);
        }
    }

    ScopedFileLock(const ScopedFileLock&) = delete;
    ScopedFileLock& operator=(const ScopedFileLock&) = delete;

private:
    int fd_;
};

// Serialize a node for WAL storage (V0: float32 vectors - legacy)
inline std::vector<uint8_t> serialize_node_v0(const Node& node) {
    std::vector<uint8_t> data;

    auto write = [&data](const void* ptr, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(ptr);
        data.insert(data.end(), bytes, bytes + size);
    };

    // Node ID
    write(&node.id.high, sizeof(node.id.high));
    write(&node.id.low, sizeof(node.id.low));

    // Type and timestamps
    write(&node.node_type, sizeof(node.node_type));
    write(&node.tau_created, sizeof(node.tau_created));
    write(&node.tau_accessed, sizeof(node.tau_accessed));
    write(&node.delta, sizeof(node.delta));

    // Confidence
    write(&node.kappa.mu, sizeof(node.kappa.mu));
    write(&node.kappa.sigma_sq, sizeof(node.kappa.sigma_sq));
    write(&node.kappa.n, sizeof(node.kappa.n));

    // Vector (full float32)
    write(node.nu.data.data(), node.nu.data.size() * sizeof(float));

    // Payload
    size_t payload_size = node.payload.size();
    write(&payload_size, sizeof(payload_size));
    if (payload_size > 0) {
        write(node.payload.data(), payload_size);
    }

    // Edges
    size_t edge_count = node.edges.size();
    write(&edge_count, sizeof(edge_count));
    for (const auto& edge : node.edges) {
        write(&edge.target.high, sizeof(edge.target.high));
        write(&edge.target.low, sizeof(edge.target.low));
        write(&edge.type, sizeof(edge.type));
        write(&edge.weight, sizeof(edge.weight));
    }

    // Tags
    size_t tag_count = node.tags.size();
    write(&tag_count, sizeof(tag_count));
    for (const auto& tag : node.tags) {
        size_t tag_len = tag.size();
        write(&tag_len, sizeof(tag_len));
        write(tag.data(), tag_len);
    }

    return data;
}

// Serialize a node for WAL storage (V1: int8 quantized vectors - 74% smaller)
inline std::vector<uint8_t> serialize_node(const Node& node) {
    std::vector<uint8_t> data;

    auto write = [&data](const void* ptr, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(ptr);
        data.insert(data.end(), bytes, bytes + size);
    };

    // Node ID
    write(&node.id.high, sizeof(node.id.high));
    write(&node.id.low, sizeof(node.id.low));

    // Type and timestamps
    write(&node.node_type, sizeof(node.node_type));
    write(&node.tau_created, sizeof(node.tau_created));
    write(&node.tau_accessed, sizeof(node.tau_accessed));
    write(&node.delta, sizeof(node.delta));

    // Confidence
    write(&node.kappa.mu, sizeof(node.kappa.mu));
    write(&node.kappa.sigma_sq, sizeof(node.kappa.sigma_sq));
    write(&node.kappa.n, sizeof(node.kappa.n));

    // Vector (int8 quantized: 392 bytes vs 1536 bytes)
    QuantizedVector qv = QuantizedVector::from_float(node.nu);
    write(qv.data, EMBED_DIM);
    write(&qv.scale, sizeof(qv.scale));
    write(&qv.offset, sizeof(qv.offset));

    // Payload
    size_t payload_size = node.payload.size();
    write(&payload_size, sizeof(payload_size));
    if (payload_size > 0) {
        write(node.payload.data(), payload_size);
    }

    // Edges
    size_t edge_count = node.edges.size();
    write(&edge_count, sizeof(edge_count));
    for (const auto& edge : node.edges) {
        write(&edge.target.high, sizeof(edge.target.high));
        write(&edge.target.low, sizeof(edge.target.low));
        write(&edge.type, sizeof(edge.type));
        write(&edge.weight, sizeof(edge.weight));
    }

    // Tags
    size_t tag_count = node.tags.size();
    write(&tag_count, sizeof(tag_count));
    for (const auto& tag : node.tags) {
        size_t tag_len = tag.size();
        write(&tag_len, sizeof(tag_len));
        write(tag.data(), tag_len);
    }

    return data;
}

// Deserialize a node from WAL data (V0: float32 vectors)
inline Node deserialize_node_v0(const uint8_t* data, size_t len) {
    Node node;
    size_t offset = 0;

    auto read = [&](void* ptr, size_t size) {
        if (offset + size <= len) {
            std::memcpy(ptr, data + offset, size);
            offset += size;
        }
    };

    // Node ID
    read(&node.id.high, sizeof(node.id.high));
    read(&node.id.low, sizeof(node.id.low));

    // Type and timestamps
    read(&node.node_type, sizeof(node.node_type));
    read(&node.tau_created, sizeof(node.tau_created));
    read(&node.tau_accessed, sizeof(node.tau_accessed));
    read(&node.delta, sizeof(node.delta));

    // Confidence
    read(&node.kappa.mu, sizeof(node.kappa.mu));
    read(&node.kappa.sigma_sq, sizeof(node.kappa.sigma_sq));
    read(&node.kappa.n, sizeof(node.kappa.n));

    // Vector (float32)
    node.nu.data.resize(EMBED_DIM);
    read(node.nu.data.data(), EMBED_DIM * sizeof(float));

    // Payload
    size_t payload_size;
    read(&payload_size, sizeof(payload_size));
    if (payload_size > 0 && payload_size < 10 * 1024 * 1024) {
        node.payload.resize(payload_size);
        read(node.payload.data(), payload_size);
    }

    // Edges
    size_t edge_count;
    read(&edge_count, sizeof(edge_count));
    if (edge_count < 10000) {
        node.edges.reserve(edge_count);
        for (size_t i = 0; i < edge_count; ++i) {
            Edge edge;
            read(&edge.target.high, sizeof(edge.target.high));
            read(&edge.target.low, sizeof(edge.target.low));
            read(&edge.type, sizeof(edge.type));
            read(&edge.weight, sizeof(edge.weight));
            node.edges.push_back(edge);
        }
    }

    // Tags
    size_t tag_count;
    read(&tag_count, sizeof(tag_count));
    if (tag_count < 1000) {
        node.tags.reserve(tag_count);
        for (size_t i = 0; i < tag_count; ++i) {
            size_t tag_len;
            read(&tag_len, sizeof(tag_len));
            if (tag_len < 1000 && offset + tag_len <= len) {
                std::string tag(reinterpret_cast<const char*>(data + offset), tag_len);
                offset += tag_len;
                node.tags.push_back(std::move(tag));
            }
        }
    }

    return node;
}

// Deserialize a node from WAL data (V1: int8 quantized vectors)
inline Node deserialize_node_v1(const uint8_t* data, size_t len) {
    Node node;
    size_t offset = 0;

    auto read = [&](void* ptr, size_t size) {
        if (offset + size <= len) {
            std::memcpy(ptr, data + offset, size);
            offset += size;
        }
    };

    // Node ID
    read(&node.id.high, sizeof(node.id.high));
    read(&node.id.low, sizeof(node.id.low));

    // Type and timestamps
    read(&node.node_type, sizeof(node.node_type));
    read(&node.tau_created, sizeof(node.tau_created));
    read(&node.tau_accessed, sizeof(node.tau_accessed));
    read(&node.delta, sizeof(node.delta));

    // Confidence
    read(&node.kappa.mu, sizeof(node.kappa.mu));
    read(&node.kappa.sigma_sq, sizeof(node.kappa.sigma_sq));
    read(&node.kappa.n, sizeof(node.kappa.n));

    // Vector (int8 quantized -> dequantize to float32)
    QuantizedVector qv;
    read(qv.data, EMBED_DIM);
    read(&qv.scale, sizeof(qv.scale));
    read(&qv.offset, sizeof(qv.offset));
    node.nu = qv.to_float();

    // Payload
    size_t payload_size;
    read(&payload_size, sizeof(payload_size));
    if (payload_size > 0 && payload_size < 10 * 1024 * 1024) {
        node.payload.resize(payload_size);
        read(node.payload.data(), payload_size);
    }

    // Edges
    size_t edge_count;
    read(&edge_count, sizeof(edge_count));
    if (edge_count < 10000) {
        node.edges.reserve(edge_count);
        for (size_t i = 0; i < edge_count; ++i) {
            Edge edge;
            read(&edge.target.high, sizeof(edge.target.high));
            read(&edge.target.low, sizeof(edge.target.low));
            read(&edge.type, sizeof(edge.type));
            read(&edge.weight, sizeof(edge.weight));
            node.edges.push_back(edge);
        }
    }

    // Tags
    size_t tag_count;
    read(&tag_count, sizeof(tag_count));
    if (tag_count < 1000) {
        node.tags.reserve(tag_count);
        for (size_t i = 0; i < tag_count; ++i) {
            size_t tag_len;
            read(&tag_len, sizeof(tag_len));
            if (tag_len < 1000 && offset + tag_len <= len) {
                std::string tag(reinterpret_cast<const char*>(data + offset), tag_len);
                offset += tag_len;
                node.tags.push_back(std::move(tag));
            }
        }
    }

    return node;
}

// Deserialize a node (format-aware, for backward compatibility)
inline Node deserialize_node(const uint8_t* data, size_t len, uint8_t format = WAL_FORMAT_V0) {
    if (format == WAL_FORMAT_V1) {
        return deserialize_node_v1(data, len);
    }
    return deserialize_node_v0(data, len);
}

// ============================================================================
// Delta serialization: Type-specific, minimal payloads
// ============================================================================

// V2: Touch delta - just node ID + timestamp (26 bytes total with header)
struct TouchDelta {
    NodeId id;              // 16 bytes
    Timestamp tau_accessed; // 8 bytes
};
static_assert(sizeof(TouchDelta) == 24, "TouchDelta should be 24 bytes");

inline std::vector<uint8_t> serialize_touch(NodeId id, Timestamp tau) {
    std::vector<uint8_t> data(sizeof(TouchDelta));
    TouchDelta* d = reinterpret_cast<TouchDelta*>(data.data());
    d->id = id;
    d->tau_accessed = tau;
    return data;
}

inline TouchDelta deserialize_touch(const uint8_t* data, size_t len) {
    TouchDelta d{};
    if (len >= sizeof(TouchDelta)) {
        std::memcpy(&d, data, sizeof(TouchDelta));
    }
    return d;
}

// V3: Confidence delta - node ID + full confidence (44 bytes total with header)
struct ConfidenceDelta {
    NodeId id;           // 16 bytes
    float mu;            // 4 bytes
    float sigma_sq;      // 4 bytes
    uint32_t n;          // 4 bytes
    Timestamp tau;       // 8 bytes (confidence.tau)
};
static_assert(sizeof(ConfidenceDelta) == 40, "ConfidenceDelta should be 40 bytes");

inline std::vector<uint8_t> serialize_confidence(NodeId id, const Confidence& kappa) {
    std::vector<uint8_t> data(sizeof(ConfidenceDelta));
    ConfidenceDelta* d = reinterpret_cast<ConfidenceDelta*>(data.data());
    d->id = id;
    d->mu = kappa.mu;
    d->sigma_sq = kappa.sigma_sq;
    d->n = kappa.n;
    d->tau = kappa.tau;
    return data;
}

inline ConfidenceDelta deserialize_confidence(const uint8_t* data, size_t len) {
    ConfidenceDelta d{};
    if (len >= sizeof(ConfidenceDelta)) {
        std::memcpy(&d, data, sizeof(ConfidenceDelta));
    }
    return d;
}

// V4: Edge add delta - from ID + edge (80 bytes total with header)
struct EdgeDelta {
    NodeId from_id;      // 16 bytes
    NodeId target;       // 16 bytes
    float weight;        // 4 bytes
    EdgeType type;       // 1 byte
    uint8_t padding[3];  // alignment to 8-byte boundary
};
static_assert(sizeof(EdgeDelta) == 40, "EdgeDelta should be 40 bytes");

inline std::vector<uint8_t> serialize_edge(NodeId from, const Edge& edge) {
    std::vector<uint8_t> data(sizeof(EdgeDelta));
    EdgeDelta* d = reinterpret_cast<EdgeDelta*>(data.data());
    d->from_id = from;
    d->target = edge.target;
    d->weight = edge.weight;
    d->type = edge.type;
    std::memset(d->padding, 0, sizeof(d->padding));
    return data;
}

inline EdgeDelta deserialize_edge(const uint8_t* data, size_t len) {
    EdgeDelta d{};
    if (len >= sizeof(EdgeDelta)) {
        std::memcpy(&d, data, sizeof(EdgeDelta));
    }
    return d;
}

// Delete delta - just node ID (16 bytes)
inline std::vector<uint8_t> serialize_delete(NodeId id) {
    std::vector<uint8_t> data(sizeof(NodeId));
    std::memcpy(data.data(), &id, sizeof(NodeId));
    return data;
}

inline NodeId deserialize_delete(const uint8_t* data, size_t len) {
    NodeId id{};
    if (len >= sizeof(NodeId)) {
        std::memcpy(&id, data, sizeof(NodeId));
    }
    return id;
}

// ============================================================================
// WalReplayEntry: Unified structure for replay callbacks
// Supports both full nodes and deltas
// ============================================================================
struct WalReplayEntry {
    WalOp op;
    uint8_t format;
    NodeId id;

    // Full node (V0, V1 formats)
    bool has_full_node = false;
    Node full_node;

    // Touch delta (V2)
    bool has_touch = false;
    Timestamp touch_tau = 0;

    // Confidence delta (V3)
    bool has_confidence = false;
    Confidence confidence;

    // Edge delta (V4)
    bool has_edge = false;
    Edge edge;

    // Is this entry a delta (vs full node)?
    bool is_delta() const {
        return has_touch || has_confidence || has_edge;
    }
};

// Write-Ahead Log: the shared field
class WriteAheadLog {
public:
    explicit WriteAheadLog(const std::string& path)
        : path_(path), fd_(-1), next_seq_(0), last_read_pos_(0) {}

    ~WriteAheadLog() {
        close();
    }

    // Open or create WAL file
    bool open() {
        fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT, 0644);
        if (fd_ < 0) {
            std::cerr << "[WAL] Failed to open: " << path_ << "\n";
            return false;
        }

        // Find current end position and max sequence
        struct stat st;
        if (fstat(fd_, &st) == 0) {
            last_read_pos_ = 0;
            scan_for_sequence();
        }

        std::cerr << "[WAL] Opened: " << path_ << ", next_seq=" << next_seq_ << "\n";
        return true;
    }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    // Append a node operation (Insert/Update/Delete)
    // Returns sequence number, or 0 on failure
    uint64_t append(WalOp op, const Node& node) {
        if (fd_ < 0) return 0;

        std::vector<uint8_t> node_data = serialize_node(node);  // Uses V1 (int8)

        WalEntryHeader header;
        header.magic = WAL_MAGIC;
        header.length = sizeof(WalEntryHeader) + node_data.size();
        header.sequence = ++next_seq_;
        header.timestamp = static_cast<uint64_t>(now());
        header.op = op;
        header.format = WAL_FORMAT_CURRENT;  // V1: int8 quantized
        std::memset(header.reserved, 0, sizeof(header.reserved));
        header.checksum = crc32(node_data.data(), node_data.size());

        // Acquire exclusive lock, seek to end, write, sync, unlock
        {
            ScopedFileLock lock(fd_, true);

            off_t end = lseek(fd_, 0, SEEK_END);
            if (end < 0) return 0;

            // Write header
            if (write(fd_, &header, sizeof(header)) != sizeof(header)) {
                return 0;
            }

            // Write node data
            if (write(fd_, node_data.data(), node_data.size()) !=
                static_cast<ssize_t>(node_data.size())) {
                return 0;
            }

            // Sync to disk
            fsync(fd_);
        }

        return header.sequence;
    }

    // Append checkpoint marker
    uint64_t checkpoint(const std::string& snapshot_path) {
        if (fd_ < 0) return 0;

        std::vector<uint8_t> path_data(snapshot_path.begin(), snapshot_path.end());

        WalEntryHeader header;
        header.magic = WAL_MAGIC;
        header.length = sizeof(WalEntryHeader) + path_data.size();
        header.sequence = ++next_seq_;
        header.timestamp = static_cast<uint64_t>(now());
        header.op = WalOp::Checkpoint;
        std::memset(header.reserved, 0, sizeof(header.reserved));
        header.checksum = crc32(path_data.data(), path_data.size());

        {
            ScopedFileLock lock(fd_, true);

            lseek(fd_, 0, SEEK_END);
            write(fd_, &header, sizeof(header));
            write(fd_, path_data.data(), path_data.size());
            fsync(fd_);
        }

        return header.sequence;
    }

    // ========================================================================
    // Delta append methods: Type-specific, minimal I/O
    // ========================================================================

    // Append touch delta (tau_accessed only) - 56 bytes vs ~500 for full node
    uint64_t append_touch(NodeId id, Timestamp tau_accessed) {
        if (fd_ < 0) return 0;

        std::vector<uint8_t> data = serialize_touch(id, tau_accessed);

        WalEntryHeader header;
        header.magic = WAL_MAGIC;
        header.length = sizeof(WalEntryHeader) + data.size();
        header.sequence = ++next_seq_;
        header.timestamp = static_cast<uint64_t>(now());
        header.op = WalOp::Update;
        header.format = WAL_FORMAT_V2;  // Touch delta
        std::memset(header.reserved, 0, sizeof(header.reserved));
        header.checksum = crc32(data.data(), data.size());

        {
            ScopedFileLock lock(fd_, true);
            lseek(fd_, 0, SEEK_END);
            if (::write(fd_, &header, sizeof(header)) != sizeof(header)) return 0;
            if (::write(fd_, data.data(), data.size()) != static_cast<ssize_t>(data.size())) return 0;
            fsync(fd_);
        }

        return header.sequence;
    }

    // Append confidence delta - 72 bytes vs ~500 for full node
    uint64_t append_confidence(NodeId id, const Confidence& kappa) {
        if (fd_ < 0) return 0;

        std::vector<uint8_t> data = serialize_confidence(id, kappa);

        WalEntryHeader header;
        header.magic = WAL_MAGIC;
        header.length = sizeof(WalEntryHeader) + data.size();
        header.sequence = ++next_seq_;
        header.timestamp = static_cast<uint64_t>(now());
        header.op = WalOp::Update;
        header.format = WAL_FORMAT_V3;  // Confidence delta
        std::memset(header.reserved, 0, sizeof(header.reserved));
        header.checksum = crc32(data.data(), data.size());

        {
            ScopedFileLock lock(fd_, true);
            lseek(fd_, 0, SEEK_END);
            if (::write(fd_, &header, sizeof(header)) != sizeof(header)) return 0;
            if (::write(fd_, data.data(), data.size()) != static_cast<ssize_t>(data.size())) return 0;
            fsync(fd_);
        }

        return header.sequence;
    }

    // Append edge delta - 72 bytes vs ~500 for full node
    uint64_t append_edge(NodeId from, const Edge& edge) {
        if (fd_ < 0) return 0;

        std::vector<uint8_t> data = serialize_edge(from, edge);

        WalEntryHeader header;
        header.magic = WAL_MAGIC;
        header.length = sizeof(WalEntryHeader) + data.size();
        header.sequence = ++next_seq_;
        header.timestamp = static_cast<uint64_t>(now());
        header.op = WalOp::Update;
        header.format = WAL_FORMAT_V4;  // Edge delta
        std::memset(header.reserved, 0, sizeof(header.reserved));
        header.checksum = crc32(data.data(), data.size());

        {
            ScopedFileLock lock(fd_, true);
            lseek(fd_, 0, SEEK_END);
            if (::write(fd_, &header, sizeof(header)) != sizeof(header)) return 0;
            if (::write(fd_, data.data(), data.size()) != static_cast<ssize_t>(data.size())) return 0;
            fsync(fd_);
        }

        return header.sequence;
    }

    // Append delete (just node ID) - 48 bytes
    uint64_t append_delete(NodeId id) {
        if (fd_ < 0) return 0;

        std::vector<uint8_t> data = serialize_delete(id);

        WalEntryHeader header;
        header.magic = WAL_MAGIC;
        header.length = sizeof(WalEntryHeader) + data.size();
        header.sequence = ++next_seq_;
        header.timestamp = static_cast<uint64_t>(now());
        header.op = WalOp::Delete;
        header.format = WAL_FORMAT_V1;  // Simple delete
        std::memset(header.reserved, 0, sizeof(header.reserved));
        header.checksum = crc32(data.data(), data.size());

        {
            ScopedFileLock lock(fd_, true);
            lseek(fd_, 0, SEEK_END);
            if (::write(fd_, &header, sizeof(header)) != sizeof(header)) return 0;
            if (::write(fd_, data.data(), data.size()) != static_cast<ssize_t>(data.size())) return 0;
            fsync(fd_);
        }

        return header.sequence;
    }

    // ========================================================================

    // Replay entries since a sequence number
    // Callback receives (op, node, sequence)
    // Returns number of entries replayed
    size_t replay_since(uint64_t since_seq,
                        std::function<void(WalOp, const Node&, uint64_t)> callback) {
        if (fd_ < 0) return 0;

        ScopedFileLock lock(fd_, false);  // Shared lock for reading

        // Seek to start
        lseek(fd_, 0, SEEK_SET);

        size_t count = 0;
        WalEntryHeader header;

        while (::read(fd_, &header, sizeof(header)) == sizeof(header)) {
            // Validate magic
            if (header.magic != WAL_MAGIC) {
                std::cerr << "[WAL] Invalid magic at offset, stopping replay\n";
                break;
            }

            // Calculate data size
            size_t data_size = header.length - sizeof(header);
            if (data_size > 100 * 1024 * 1024) {  // Sanity: <100MB per entry
                std::cerr << "[WAL] Entry too large, stopping replay\n";
                break;
            }

            // Read data
            std::vector<uint8_t> data(data_size);
            if (::read(fd_, data.data(), data_size) != static_cast<ssize_t>(data_size)) {
                std::cerr << "[WAL] Incomplete entry, stopping replay\n";
                break;
            }

            // Verify checksum
            if (crc32(data.data(), data.size()) != header.checksum) {
                std::cerr << "[WAL] Checksum mismatch at seq " << header.sequence << "\n";
                continue;  // Skip corrupted entry
            }

            // Process if newer than requested
            if (header.sequence > since_seq && header.op != WalOp::Checkpoint) {
                Node node = deserialize_node(data.data(), data.size(), header.format);
                callback(header.op, node, header.sequence);
                count++;
            }

            // Track max sequence
            if (header.sequence >= next_seq_) {
                next_seq_ = header.sequence;
            }
        }

        // Remember position for incremental sync
        last_read_pos_ = lseek(fd_, 0, SEEK_CUR);

        return count;
    }

    // Replay entries since a sequence number with delta support (Phase 2)
    // Callback receives WalReplayEntry which can be full node or delta
    // Returns number of entries replayed
    size_t replay_v2(uint64_t since_seq,
                     std::function<void(const WalReplayEntry&, uint64_t)> callback) {
        if (fd_ < 0) return 0;

        ScopedFileLock lock(fd_, false);

        lseek(fd_, 0, SEEK_SET);

        size_t count = 0;
        WalEntryHeader header;

        while (::read(fd_, &header, sizeof(header)) == sizeof(header)) {
            if (header.magic != WAL_MAGIC) {
                std::cerr << "[WAL] Invalid magic at offset, stopping replay_v2\n";
                break;
            }

            size_t data_size = header.length - sizeof(header);
            if (data_size > 100 * 1024 * 1024) break;

            std::vector<uint8_t> data(data_size);
            if (::read(fd_, data.data(), data_size) != static_cast<ssize_t>(data_size)) {
                break;
            }

            if (crc32(data.data(), data.size()) != header.checksum) {
                std::cerr << "[WAL] Checksum mismatch at seq " << header.sequence << "\n";
                continue;
            }

            if (header.sequence > since_seq && header.op != WalOp::Checkpoint) {
                WalReplayEntry entry;
                entry.op = header.op;
                entry.format = header.format;

                switch (header.format) {
                    case WAL_FORMAT_V0:
                    case WAL_FORMAT_V1: {
                        entry.full_node = deserialize_node(data.data(), data.size(), header.format);
                        entry.id = entry.full_node.id;
                        entry.has_full_node = true;
                        break;
                    }
                    case WAL_FORMAT_V2: {
                        TouchDelta td = deserialize_touch(data.data(), data.size());
                        entry.id = td.id;
                        entry.has_touch = true;
                        entry.touch_tau = td.tau_accessed;
                        break;
                    }
                    case WAL_FORMAT_V3: {
                        ConfidenceDelta cd = deserialize_confidence(data.data(), data.size());
                        entry.id = cd.id;
                        entry.has_confidence = true;
                        entry.confidence.mu = cd.mu;
                        entry.confidence.sigma_sq = cd.sigma_sq;
                        entry.confidence.n = cd.n;
                        entry.confidence.tau = cd.tau;
                        break;
                    }
                    case WAL_FORMAT_V4: {
                        EdgeDelta ed = deserialize_edge(data.data(), data.size());
                        entry.id = ed.from_id;
                        entry.has_edge = true;
                        entry.edge.target = ed.target;
                        entry.edge.type = ed.type;
                        entry.edge.weight = ed.weight;
                        break;
                    }
                    default:
                        continue;
                }

                callback(entry, header.sequence);
                count++;
            }

            if (header.sequence >= next_seq_) {
                next_seq_ = header.sequence;
            }
        }

        last_read_pos_ = lseek(fd_, 0, SEEK_CUR);
        return count;
    }

    // Sync: read only NEW entries since last sync
    // More efficient than replay_since for frequent syncs
    size_t sync(std::function<void(WalOp, const Node&, uint64_t)> callback) {
        if (fd_ < 0) return 0;

        ScopedFileLock lock(fd_, false);

        // Get current file size
        struct stat st;
        if (fstat(fd_, &st) < 0) return 0;

        // Nothing new?
        if (static_cast<size_t>(st.st_size) <= last_read_pos_) return 0;

        // Seek to where we left off
        lseek(fd_, last_read_pos_, SEEK_SET);

        size_t count = 0;
        WalEntryHeader header;

        while (::read(fd_, &header, sizeof(header)) == sizeof(header)) {
            if (header.magic != WAL_MAGIC) break;

            size_t data_size = header.length - sizeof(header);
            if (data_size > 100 * 1024 * 1024) break;

            std::vector<uint8_t> data(data_size);
            if (::read(fd_, data.data(), data_size) != static_cast<ssize_t>(data_size)) {
                break;
            }

            if (crc32(data.data(), data.size()) != header.checksum) {
                continue;
            }

            if (header.op != WalOp::Checkpoint) {
                Node node = deserialize_node(data.data(), data.size(), header.format);
                callback(header.op, node, header.sequence);
                count++;
            }

            if (header.sequence >= next_seq_) {
                next_seq_ = header.sequence;
            }
        }

        last_read_pos_ = lseek(fd_, 0, SEEK_CUR);
        return count;
    }

    // Delta-aware sync: handles both full nodes and deltas
    // Use this for Phase 2+ WAL with delta encoding
    size_t sync_v2(std::function<void(const WalReplayEntry&, uint64_t)> callback) {
        if (fd_ < 0) return 0;

        ScopedFileLock lock(fd_, false);

        struct stat st;
        if (fstat(fd_, &st) < 0) return 0;
        if (static_cast<size_t>(st.st_size) <= last_read_pos_) return 0;

        lseek(fd_, last_read_pos_, SEEK_SET);

        size_t count = 0;
        WalEntryHeader header;

        while (::read(fd_, &header, sizeof(header)) == sizeof(header)) {
            if (header.magic != WAL_MAGIC) break;

            size_t data_size = header.length - sizeof(header);
            if (data_size > 100 * 1024 * 1024) break;

            std::vector<uint8_t> data(data_size);
            if (::read(fd_, data.data(), data_size) != static_cast<ssize_t>(data_size)) {
                break;
            }

            if (crc32(data.data(), data.size()) != header.checksum) {
                continue;
            }

            if (header.op != WalOp::Checkpoint) {
                WalReplayEntry entry;
                entry.op = header.op;
                entry.format = header.format;

                switch (header.format) {
                    case WAL_FORMAT_V0:
                    case WAL_FORMAT_V1: {
                        // Full node
                        entry.full_node = deserialize_node(data.data(), data.size(), header.format);
                        entry.id = entry.full_node.id;
                        entry.has_full_node = true;
                        break;
                    }
                    case WAL_FORMAT_V2: {
                        // Touch delta
                        TouchDelta td = deserialize_touch(data.data(), data.size());
                        entry.id = td.id;
                        entry.has_touch = true;
                        entry.touch_tau = td.tau_accessed;
                        break;
                    }
                    case WAL_FORMAT_V3: {
                        // Confidence delta
                        ConfidenceDelta cd = deserialize_confidence(data.data(), data.size());
                        entry.id = cd.id;
                        entry.has_confidence = true;
                        entry.confidence.mu = cd.mu;
                        entry.confidence.sigma_sq = cd.sigma_sq;
                        entry.confidence.n = cd.n;
                        entry.confidence.tau = cd.tau;
                        break;
                    }
                    case WAL_FORMAT_V4: {
                        // Edge delta
                        EdgeDelta ed = deserialize_edge(data.data(), data.size());
                        entry.id = ed.from_id;
                        entry.has_edge = true;
                        entry.edge.target = ed.target;
                        entry.edge.type = ed.type;
                        entry.edge.weight = ed.weight;
                        break;
                    }
                    default:
                        // Unknown format, skip
                        continue;
                }

                callback(entry, header.sequence);
                count++;
            }

            if (header.sequence >= next_seq_) {
                next_seq_ = header.sequence;
            }
        }

        last_read_pos_ = lseek(fd_, 0, SEEK_CUR);
        return count;
    }

    // Truncate WAL (after successful snapshot)
    bool truncate() {
        if (fd_ < 0) return false;

        ScopedFileLock lock(fd_, true);

        if (ftruncate(fd_, 0) < 0) return false;
        lseek(fd_, 0, SEEK_SET);
        last_read_pos_ = 0;
        // Keep sequence number to avoid reuse

        std::cerr << "[WAL] Truncated, next_seq=" << next_seq_ << "\n";
        return true;
    }

    uint64_t next_sequence() const { return next_seq_; }
    const std::string& path() const { return path_; }

private:
    void scan_for_sequence() {
        // Scan entire WAL to find max sequence
        lseek(fd_, 0, SEEK_SET);

        WalEntryHeader header;
        while (::read(fd_, &header, sizeof(header)) == sizeof(header)) {
            if (header.magic != WAL_MAGIC) break;

            size_t data_size = header.length - sizeof(header);
            if (data_size > 100 * 1024 * 1024) break;

            // Skip data
            lseek(fd_, data_size, SEEK_CUR);

            if (header.sequence > next_seq_) {
                next_seq_ = header.sequence;
            }
        }

        last_read_pos_ = lseek(fd_, 0, SEEK_CUR);
    }

    std::string path_;
    int fd_;
    uint64_t next_seq_;
    size_t last_read_pos_;
};

} // namespace chitta
