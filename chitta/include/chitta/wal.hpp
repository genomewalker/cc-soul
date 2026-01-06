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

// WAL entry header (fixed size for easy parsing)
struct WalEntryHeader {
    uint32_t magic;       // 0x57414C45 "WALE"
    uint32_t length;      // Total entry length (header + data)
    uint64_t sequence;    // Monotonic sequence number
    uint64_t timestamp;   // Unix millis
    WalOp op;             // Operation type
    uint8_t reserved[3];  // Alignment padding
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

// Serialize a node for WAL storage
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

// Deserialize a node from WAL data
inline Node deserialize_node(const uint8_t* data, size_t len) {
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

    // Vector
    node.nu.data.resize(EMBED_DIM);
    read(node.nu.data.data(), EMBED_DIM * sizeof(float));

    // Payload
    size_t payload_size;
    read(&payload_size, sizeof(payload_size));
    if (payload_size > 0 && payload_size < 10 * 1024 * 1024) {  // Sanity check: <10MB
        node.payload.resize(payload_size);
        read(node.payload.data(), payload_size);
    }

    // Edges
    size_t edge_count;
    read(&edge_count, sizeof(edge_count));
    if (edge_count < 10000) {  // Sanity check
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
    if (tag_count < 1000) {  // Sanity check
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

        std::vector<uint8_t> node_data = serialize_node(node);

        WalEntryHeader header;
        header.magic = WAL_MAGIC;
        header.length = sizeof(WalEntryHeader) + node_data.size();
        header.sequence = ++next_seq_;
        header.timestamp = static_cast<uint64_t>(now());
        header.op = op;
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
                Node node = deserialize_node(data.data(), data.size());
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
                Node node = deserialize_node(data.data(), data.size());
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
