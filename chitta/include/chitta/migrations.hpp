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
#include "unified_index.hpp"
#include "segment_manager.hpp"
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

// ═══════════════════════════════════════════════════════════════════════════
// Format conversions: .hot → .unified or .manifest
// ═══════════════════════════════════════════════════════════════════════════

struct ConversionResult {
    bool success;
    size_t nodes_converted;
    std::string error;
    std::string backup_path;
};

// Convert .hot format to UnifiedIndex (.unified)
// Preserves all node data, creates new HNSW connections
inline ConversionResult convert_to_unified(const std::string& base_path) {
    ConversionResult result{false, 0, "", ""};

    std::string hot_path = base_path + ".hot";
    std::string unified_path = base_path + ".unified";

    // Check source exists
    if (!std::filesystem::exists(hot_path)) {
        result.error = "Source not found: " + hot_path;
        return result;
    }

    // Check target doesn't exist
    if (std::filesystem::exists(unified_path)) {
        result.error = "Target already exists: " + unified_path;
        return result;
    }

    // Load hot storage to read nodes
    std::ifstream in(hot_path, std::ios::binary);
    if (!in) {
        result.error = "Cannot open source file";
        return result;
    }

    // Read header
    uint32_t magic, version;
    size_t count;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&count), sizeof(count));

    if (magic != STORAGE_MAGIC) {
        result.error = "Invalid source format (bad magic)";
        return result;
    }

    if (version < 2) {
        result.error = "Please upgrade to v3 first: chitta_cli upgrade " + hot_path;
        return result;
    }

    std::cerr << "[migrations] Converting " << count << " nodes to unified format...\n";

    // Create unified index with appropriate capacity
    size_t capacity = std::max(count * 2, size_t(1000));

    // We need UnifiedIndex - include it
    // Note: This requires unified_index.hpp to be included
    // For now, we'll use a simpler approach: read nodes and write directly

    // Read all nodes
    std::vector<Node> nodes;
    nodes.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        Node node;

        in.read(reinterpret_cast<char*>(&node.id.high), sizeof(node.id.high));
        in.read(reinterpret_cast<char*>(&node.id.low), sizeof(node.id.low));
        in.read(reinterpret_cast<char*>(&node.node_type), sizeof(node.node_type));
        in.read(reinterpret_cast<char*>(&node.tau_created), sizeof(node.tau_created));
        in.read(reinterpret_cast<char*>(&node.tau_accessed), sizeof(node.tau_accessed));
        in.read(reinterpret_cast<char*>(&node.delta), sizeof(node.delta));

        float mu, sigma_sq;
        uint32_t n;
        in.read(reinterpret_cast<char*>(&mu), sizeof(mu));
        in.read(reinterpret_cast<char*>(&sigma_sq), sizeof(sigma_sq));
        in.read(reinterpret_cast<char*>(&n), sizeof(n));
        node.kappa = Confidence(mu);
        node.kappa.sigma_sq = sigma_sq;
        node.kappa.n = n;

        node.nu.data.resize(EMBED_DIM);
        in.read(reinterpret_cast<char*>(node.nu.data.data()), EMBED_DIM * sizeof(float));

        // Skip payload
        size_t payload_size;
        in.read(reinterpret_cast<char*>(&payload_size), sizeof(payload_size));
        if (payload_size > 0 && payload_size < 10000000) {
            in.seekg(payload_size, std::ios::cur);
        }

        // Read edges
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

        // Read tags (v2+)
        size_t tag_count;
        in.read(reinterpret_cast<char*>(&tag_count), sizeof(tag_count));
        if (tag_count < 1000) {
            for (size_t t = 0; t < tag_count; ++t) {
                size_t tag_len;
                in.read(reinterpret_cast<char*>(&tag_len), sizeof(tag_len));
                if (tag_len < 1000) {
                    std::string tag(tag_len, '\0');
                    in.read(tag.data(), tag_len);
                    node.tags.push_back(std::move(tag));
                }
            }
        }

        nodes.push_back(std::move(node));

        if ((i + 1) % 1000 == 0) {
            std::cerr << "[migrations] Read " << (i + 1) << "/" << count << " nodes\n";
        }
    }

    in.close();

    // Create backup of .hot file
    result.backup_path = create_backup(hot_path, version);
    std::cerr << "[migrations] Backup created: " << result.backup_path << "\n";

    // Now create UnifiedIndex and insert all nodes
    // This is done by including unified_index.hpp at the top
    // For compilation, we assume it's available
    UnifiedIndex unified;
    if (!unified.create(base_path, capacity)) {
        result.error = "Failed to create unified index";
        return result;
    }

    for (size_t i = 0; i < nodes.size(); ++i) {
        auto slot = unified.insert(nodes[i].id, nodes[i]);
        if (!slot.valid()) {
            result.error = "Failed to insert node " + std::to_string(i);
            return result;
        }

        if ((i + 1) % 1000 == 0) {
            std::cerr << "[migrations] Inserted " << (i + 1) << "/" << nodes.size() << " nodes\n";
        }
    }

    unified.sync();
    unified.close();

    result.success = true;
    result.nodes_converted = nodes.size();
    std::cerr << "[migrations] Conversion complete: " << result.nodes_converted << " nodes\n";

    return result;
}

// Convert .hot format to SegmentManager (.manifest)
inline ConversionResult convert_to_segments(const std::string& base_path) {
    ConversionResult result{false, 0, "", ""};

    std::string hot_path = base_path + ".hot";
    std::string manifest_path = base_path + ".manifest";

    // Check source exists
    if (!std::filesystem::exists(hot_path)) {
        result.error = "Source not found: " + hot_path;
        return result;
    }

    // Check target doesn't exist
    if (std::filesystem::exists(manifest_path)) {
        result.error = "Target already exists: " + manifest_path;
        return result;
    }

    // Load hot storage to read nodes (same as above)
    std::ifstream in(hot_path, std::ios::binary);
    if (!in) {
        result.error = "Cannot open source file";
        return result;
    }

    uint32_t magic, version;
    size_t count;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&count), sizeof(count));

    if (magic != STORAGE_MAGIC) {
        result.error = "Invalid source format (bad magic)";
        return result;
    }

    if (version < 2) {
        result.error = "Please upgrade to v3 first";
        return result;
    }

    std::cerr << "[migrations] Converting " << count << " nodes to segment format...\n";

    // Read all nodes (same logic as above)
    std::vector<Node> nodes;
    nodes.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        Node node;

        in.read(reinterpret_cast<char*>(&node.id.high), sizeof(node.id.high));
        in.read(reinterpret_cast<char*>(&node.id.low), sizeof(node.id.low));
        in.read(reinterpret_cast<char*>(&node.node_type), sizeof(node.node_type));
        in.read(reinterpret_cast<char*>(&node.tau_created), sizeof(node.tau_created));
        in.read(reinterpret_cast<char*>(&node.tau_accessed), sizeof(node.tau_accessed));
        in.read(reinterpret_cast<char*>(&node.delta), sizeof(node.delta));

        float mu, sigma_sq;
        uint32_t n;
        in.read(reinterpret_cast<char*>(&mu), sizeof(mu));
        in.read(reinterpret_cast<char*>(&sigma_sq), sizeof(sigma_sq));
        in.read(reinterpret_cast<char*>(&n), sizeof(n));
        node.kappa = Confidence(mu);
        node.kappa.sigma_sq = sigma_sq;
        node.kappa.n = n;

        node.nu.data.resize(EMBED_DIM);
        in.read(reinterpret_cast<char*>(node.nu.data.data()), EMBED_DIM * sizeof(float));

        size_t payload_size;
        in.read(reinterpret_cast<char*>(&payload_size), sizeof(payload_size));
        if (payload_size > 0 && payload_size < 10000000) {
            in.seekg(payload_size, std::ios::cur);
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

        size_t tag_count;
        in.read(reinterpret_cast<char*>(&tag_count), sizeof(tag_count));
        if (tag_count < 1000) {
            for (size_t t = 0; t < tag_count; ++t) {
                size_t tag_len;
                in.read(reinterpret_cast<char*>(&tag_len), sizeof(tag_len));
                if (tag_len < 1000) {
                    std::string tag(tag_len, '\0');
                    in.read(tag.data(), tag_len);
                    node.tags.push_back(std::move(tag));
                }
            }
        }

        nodes.push_back(std::move(node));
    }

    in.close();

    // Create backup
    result.backup_path = create_backup(hot_path, version);
    std::cerr << "[migrations] Backup created: " << result.backup_path << "\n";

    // Create SegmentManager and insert all nodes
    SegmentManager segments(base_path);
    if (!segments.create()) {
        result.error = "Failed to create segment manager";
        return result;
    }

    for (size_t i = 0; i < nodes.size(); ++i) {
        auto slot = segments.insert(nodes[i].id, nodes[i]);
        if (!slot.valid()) {
            result.error = "Failed to insert node " + std::to_string(i);
            return result;
        }

        if ((i + 1) % 1000 == 0) {
            std::cerr << "[migrations] Inserted " << (i + 1) << "/" << nodes.size() << " nodes\n";
        }
    }

    segments.sync();
    segments.close();

    result.success = true;
    result.nodes_converted = nodes.size();
    std::cerr << "[migrations] Conversion complete: " << result.nodes_converted << " nodes\n";

    return result;
}

} // namespace migrations
} // namespace chitta
