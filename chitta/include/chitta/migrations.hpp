#pragma once
// Database migrations: explicit upgrade path between versions
//
// Principles:
// 1. Never auto-read old formats - fail fast with clear error
// 2. Explicit upgrade command with backup
// 3. Sequential migrations (v1→v2→v3, not v1→v3)
// 4. Each migration is idempotent and testable

#include "types.hpp"
#include "quantized.hpp"
#include "hnsw.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>

namespace chitta {
namespace migrations {

// Storage format constants
constexpr uint32_t STORAGE_MAGIC = 0x43485454;  // "CHTT"
constexpr uint32_t FOOTER_MAGIC = 0x454E4443;   // "CDNE" (end marker)
constexpr uint32_t CURRENT_VERSION = 3;

struct MigrationResult {
    bool success;
    uint32_t from_version;
    uint32_t to_version;
    std::string error;
    std::string backup_path;
};

// Detect database version without loading
inline uint32_t detect_version(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return 0;  // File doesn't exist

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

// Create backup of database file
inline std::string create_backup(const std::string& path, uint32_t version) {
    namespace fs = std::filesystem;

    std::string backup = path + ".bak.v" + std::to_string(version);

    // If backup exists, add timestamp
    if (fs::exists(backup)) {
        auto now = std::chrono::system_clock::now();
        auto ts = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        backup = path + ".bak.v" + std::to_string(version) + "." + std::to_string(ts);
    }

    fs::copy_file(path, backup);
    return backup;
}

// Migration: v1 → v2 (add tags field)
// v1 format: [count][nodes...][hnsw_index]
// v2 format: [magic][version][count][nodes+tags...][hnsw_index]
inline MigrationResult migrate_v1_to_v2(const std::string& path) {
    MigrationResult result{false, 1, 2, "", ""};

    // Read v1 format
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        result.error = "Cannot open file for reading";
        return result;
    }

    // v1 has no magic, starts with node count
    size_t count;
    in.read(reinterpret_cast<char*>(&count), sizeof(count));

    // Sanity check
    if (count > 1000000) {
        result.error = "Invalid node count (corrupt v1 file?)";
        return result;
    }

    // Read all nodes in v1 format (no tags)
    struct V1Node {
        NodeId id;
        NodeType node_type;
        Timestamp tau_created;
        Timestamp tau_accessed;
        float delta;
        float kappa_mu;
        float kappa_sigma_sq;
        uint32_t kappa_n;
        Vector nu;
        std::vector<uint8_t> payload;
        std::vector<Edge> edges;
    };

    std::vector<V1Node> nodes;
    nodes.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        V1Node node;

        in.read(reinterpret_cast<char*>(&node.id.high), sizeof(node.id.high));
        in.read(reinterpret_cast<char*>(&node.id.low), sizeof(node.id.low));
        in.read(reinterpret_cast<char*>(&node.node_type), sizeof(node.node_type));
        in.read(reinterpret_cast<char*>(&node.tau_created), sizeof(node.tau_created));
        in.read(reinterpret_cast<char*>(&node.tau_accessed), sizeof(node.tau_accessed));
        in.read(reinterpret_cast<char*>(&node.delta), sizeof(node.delta));
        in.read(reinterpret_cast<char*>(&node.kappa_mu), sizeof(node.kappa_mu));
        in.read(reinterpret_cast<char*>(&node.kappa_sigma_sq), sizeof(node.kappa_sigma_sq));
        in.read(reinterpret_cast<char*>(&node.kappa_n), sizeof(node.kappa_n));

        node.nu.data.resize(EMBED_DIM);
        in.read(reinterpret_cast<char*>(node.nu.data.data()), EMBED_DIM * sizeof(float));

        size_t payload_size;
        in.read(reinterpret_cast<char*>(&payload_size), sizeof(payload_size));
        if (payload_size > 0 && payload_size < 10000000) {
            node.payload.resize(payload_size);
            in.read(reinterpret_cast<char*>(node.payload.data()), payload_size);
        }

        size_t edge_count;
        in.read(reinterpret_cast<char*>(&edge_count), sizeof(edge_count));
        if (edge_count < 10000) {
            node.edges.reserve(edge_count);
            for (size_t e = 0; e < edge_count; ++e) {
                Edge edge;
                in.read(reinterpret_cast<char*>(&edge.target.high), sizeof(edge.target.high));
                in.read(reinterpret_cast<char*>(&edge.target.low), sizeof(edge.target.low));
                in.read(reinterpret_cast<char*>(&edge.type), sizeof(edge.type));
                in.read(reinterpret_cast<char*>(&edge.weight), sizeof(edge.weight));
                node.edges.push_back(edge);
            }
        }

        nodes.push_back(std::move(node));
    }

    // Read HNSW index
    size_t index_size;
    in.read(reinterpret_cast<char*>(&index_size), sizeof(index_size));
    std::vector<uint8_t> index_data;
    if (index_size > 0 && index_size < 100000000) {
        index_data.resize(index_size);
        in.read(reinterpret_cast<char*>(index_data.data()), index_size);
    }

    in.close();

    // Create backup
    result.backup_path = create_backup(path, 1);

    // Write v2 format
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        result.error = "Cannot open file for writing";
        return result;
    }

    // v2 header
    uint32_t magic = STORAGE_MAGIC;
    uint32_t version = 2;
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));

    // Write nodes with empty tags
    for (const auto& node : nodes) {
        out.write(reinterpret_cast<const char*>(&node.id.high), sizeof(node.id.high));
        out.write(reinterpret_cast<const char*>(&node.id.low), sizeof(node.id.low));
        out.write(reinterpret_cast<const char*>(&node.node_type), sizeof(node.node_type));
        out.write(reinterpret_cast<const char*>(&node.tau_created), sizeof(node.tau_created));
        out.write(reinterpret_cast<const char*>(&node.tau_accessed), sizeof(node.tau_accessed));
        out.write(reinterpret_cast<const char*>(&node.delta), sizeof(node.delta));
        out.write(reinterpret_cast<const char*>(&node.kappa_mu), sizeof(node.kappa_mu));
        out.write(reinterpret_cast<const char*>(&node.kappa_sigma_sq), sizeof(node.kappa_sigma_sq));
        out.write(reinterpret_cast<const char*>(&node.kappa_n), sizeof(node.kappa_n));

        out.write(reinterpret_cast<const char*>(node.nu.data.data()), EMBED_DIM * sizeof(float));

        size_t payload_size = node.payload.size();
        out.write(reinterpret_cast<const char*>(&payload_size), sizeof(payload_size));
        if (payload_size > 0) {
            out.write(reinterpret_cast<const char*>(node.payload.data()), payload_size);
        }

        size_t edge_count = node.edges.size();
        out.write(reinterpret_cast<const char*>(&edge_count), sizeof(edge_count));
        for (const auto& edge : node.edges) {
            out.write(reinterpret_cast<const char*>(&edge.target.high), sizeof(edge.target.high));
            out.write(reinterpret_cast<const char*>(&edge.target.low), sizeof(edge.target.low));
            out.write(reinterpret_cast<const char*>(&edge.type), sizeof(edge.type));
            out.write(reinterpret_cast<const char*>(&edge.weight), sizeof(edge.weight));
        }

        // Empty tags for v2
        size_t tag_count = 0;
        out.write(reinterpret_cast<const char*>(&tag_count), sizeof(tag_count));
    }

    // Write HNSW index
    out.write(reinterpret_cast<const char*>(&index_size), sizeof(index_size));
    if (index_size > 0) {
        out.write(reinterpret_cast<const char*>(index_data.data()), index_size);
    }

    result.success = out.good();
    if (!result.success) {
        result.error = "Write failed";
    }

    return result;
}

// Migration: v2 → v3 (add checksum footer for integrity)
// v2 format: [magic][version][count][nodes+tags...][hnsw_index]
// v3 format: [magic][version][count][nodes+tags...][hnsw_index][checksum][footer_magic]
inline MigrationResult migrate_v2_to_v3(const std::string& path) {
    MigrationResult result{false, 2, 3, "", ""};

    // Read entire v2 file
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        result.error = "Cannot open file for reading";
        return result;
    }

    size_t file_size = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(file_size);
    in.read(reinterpret_cast<char*>(data.data()), file_size);
    in.close();

    // Verify it's v2
    if (file_size < 8) {
        result.error = "File too small";
        return result;
    }

    uint32_t magic, version;
    std::memcpy(&magic, data.data(), sizeof(magic));
    std::memcpy(&version, data.data() + 4, sizeof(version));

    if (magic != STORAGE_MAGIC || version != 2) {
        result.error = "Not a v2 database";
        return result;
    }

    // Create backup
    result.backup_path = create_backup(path, 2);

    // Update version in data
    uint32_t new_version = 3;
    std::memcpy(data.data() + 4, &new_version, sizeof(new_version));

    // Calculate checksum of content
    uint32_t checksum = crc32(data.data(), data.size());

    // Write v3 format (content + checksum footer)
    std::string tmp_path = path + ".tmp";
    std::ofstream out(tmp_path, std::ios::binary);
    if (!out) {
        result.error = "Cannot open temp file for writing";
        return result;
    }

    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    out.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));
    out.write(reinterpret_cast<const char*>(&FOOTER_MAGIC), sizeof(FOOTER_MAGIC));

    if (!out.good()) {
        out.close();
        std::filesystem::remove(tmp_path);
        result.error = "Write failed";
        return result;
    }

    out.flush();
    out.close();

    // Fsync before rename
    int tmp_fd = ::open(tmp_path.c_str(), O_RDONLY);
    if (tmp_fd >= 0) {
        fsync(tmp_fd);
        ::close(tmp_fd);
    }

    // Atomic rename
    if (::rename(tmp_path.c_str(), path.c_str()) != 0) {
        std::filesystem::remove(tmp_path);
        result.error = "Rename failed";
        return result;
    }

    result.success = true;
    return result;
}

// Run all necessary migrations to reach current version
inline MigrationResult upgrade(const std::string& path) {
    namespace fs = std::filesystem;

    MigrationResult result{false, 0, CURRENT_VERSION, "", ""};

    if (!fs::exists(path)) {
        result.error = "Database file not found: " + path;
        return result;
    }

    uint32_t version = detect_version(path);
    result.from_version = version;

    if (version == 0) {
        result.error = "Cannot detect database version";
        return result;
    }

    if (version == CURRENT_VERSION) {
        result.success = true;
        result.error = "Already at current version";
        return result;
    }

    if (version > CURRENT_VERSION) {
        result.error = "Database version " + std::to_string(version) +
                      " is newer than supported " + std::to_string(CURRENT_VERSION);
        return result;
    }

    // Run migrations sequentially
    std::cerr << "[migrations] Upgrading from v" << version << " to v" << CURRENT_VERSION << "\n";

    // v1 → v2
    if (version == 1) {
        std::cerr << "[migrations] Running v1 → v2 migration (adding tags)...\n";
        auto r = migrate_v1_to_v2(path);
        if (!r.success) {
            result.error = "v1→v2 migration failed: " + r.error;
            return result;
        }
        result.backup_path = r.backup_path;
        version = 2;
        std::cerr << "[migrations] v1 → v2 complete. Backup: " << r.backup_path << "\n";
    }

    // v2 → v3
    if (version == 2) {
        std::cerr << "[migrations] Running v2 → v3 migration (adding checksum)...\n";
        auto r = migrate_v2_to_v3(path);
        if (!r.success) {
            result.error = "v2→v3 migration failed: " + r.error;
            return result;
        }
        if (result.backup_path.empty()) {
            result.backup_path = r.backup_path;
        }
        version = 3;
        std::cerr << "[migrations] v2 → v3 complete. Backup: " << r.backup_path << "\n";
    }

    // Clear any existing WAL after migration (fresh start)
    // The migrated snapshot is now the source of truth
    std::string wal_path = path;
    // Remove .hot suffix if present to get base path
    if (wal_path.size() > 4 && wal_path.substr(wal_path.size() - 4) == ".hot") {
        wal_path = wal_path.substr(0, wal_path.size() - 4);
    }
    wal_path += ".wal";
    if (fs::exists(wal_path)) {
        std::cerr << "[migrations] Clearing WAL after migration: " << wal_path << "\n";
        fs::remove(wal_path);
    }

    result.success = (version == CURRENT_VERSION);
    return result;
}

// Check if upgrade is needed
inline bool needs_upgrade(const std::string& path) {
    return detect_version(path) < CURRENT_VERSION;
}

} // namespace migrations
} // namespace chitta
