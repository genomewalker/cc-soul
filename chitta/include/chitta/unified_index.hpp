#pragma once
// Unified Index: Scalable mmap'd storage with Hilbert curve ordering
//
// Combines all storage components into a single coherent structure:
// - IndexedNode array (Hilbert-sorted for cache locality)
// - ConnectionPool (persistent HNSW graph)
// - Vectors and metadata (mmap'd arrays)
//
// Key features:
// - O(1) load time (no rebuild on startup)
// - Cache-friendly disk layout via Hilbert curve
// - Scales to 100M+ nodes with bounded memory
// - Copy-on-write snapshot support

#include "types.hpp"
#include "quantized.hpp"
#include "hilbert.hpp"
#include "mmap.hpp"  // For MappedRegion
#include "connection_pool.hpp"
#include "blob_store.hpp"
#include "tag_index.hpp"
#include "hnsw.hpp"
#include <cstdint>
#include <ctime>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <fstream>
#ifdef __linux__
#include <sys/ioctl.h>
#endif

namespace chitta {

// ═══════════════════════════════════════════════════════════════════════════
// Unified Index structures
// ═══════════════════════════════════════════════════════════════════════════

constexpr uint32_t UNIFIED_MAGIC = 0x554E4946;  // "UNIF"
constexpr uint32_t UNIFIED_VERSION = 2;  // v2: 64-bit offsets in NodeMeta (80 bytes, was 64)

// Slot-based node identifier (replaces pointer indirection)
struct SlotId {
    uint32_t value;

    SlotId() : value(UINT32_MAX) {}
    explicit SlotId(uint32_t v) : value(v) {}

    bool valid() const { return value != UINT32_MAX; }
    static SlotId invalid() { return SlotId(); }

    bool operator==(const SlotId& other) const { return value == other.value; }
    bool operator!=(const SlotId& other) const { return value != other.value; }
};

// Fixed-size node record (64 bytes, cache-line aligned)
struct alignas(64) IndexedNode {
    NodeId id;                    // 16B - UUID
    uint64_t hilbert_key;         // 8B  - Hilbert curve key
    uint32_t vector_offset;       // 4B  - Offset into vectors file
    uint32_t meta_offset;         // 4B  - Offset into metadata file
    uint64_t connection_offset;   // 8B  - Offset into connection pool
    uint8_t level;                // 1B  - HNSW level
    uint8_t flags;                // 1B  - 0x01=deleted, 0x02=frozen
    uint16_t connection_count;    // 2B  - Level 0 connection count
    uint32_t reserved[4];         // 16B - Future use / padding
};
static_assert(sizeof(IndexedNode) == 64, "IndexedNode must be 64 bytes");

// Index flags
constexpr uint8_t NODE_FLAG_DELETED = 0x01;
constexpr uint8_t NODE_FLAG_FROZEN = 0x02;

// Unified index header (4KB page-aligned)
struct alignas(4096) UnifiedIndexHeader {
    uint32_t magic;               // UNIFIED_MAGIC
    uint32_t version;             // UNIFIED_VERSION
    uint64_t node_count;          // Active nodes
    uint64_t capacity;            // Pre-allocated slots
    uint64_t deleted_count;       // Deleted nodes (for compaction)
    uint32_t entry_point_slot;    // HNSW entry point
    uint32_t max_level;           // Current max HNSW level
    uint32_t hnsw_m;              // HNSW M parameter
    uint32_t hnsw_ef_construction;
    uint64_t snapshot_id;         // For CoW versioning
    uint64_t checksum;
    uint64_t wal_sequence;        // Last applied WAL sequence (for crash recovery)
    uint8_t reserved[4016];       // Pad to 4KB
};
static_assert(sizeof(UnifiedIndexHeader) == 4096, "Header must be 4KB");

// ═══════════════════════════════════════════════════════════════════════════
// Unified Index class
// ═══════════════════════════════════════════════════════════════════════════

class UnifiedIndex {
public:
    // HNSW configuration
    static constexpr uint32_t DEFAULT_M = 16;
    static constexpr uint32_t DEFAULT_EF_CONSTRUCTION = 200;
    static constexpr uint32_t DEFAULT_EF_SEARCH = 50;
    static constexpr uint32_t MAX_LEVEL = 16;

    // Capacity defaults
    static constexpr size_t INITIAL_CAPACITY = 100000;
    static constexpr size_t GROWTH_FACTOR = 2;

    UnifiedIndex() = default;
    ~UnifiedIndex() { close(); }

    // Prevent copying
    UnifiedIndex(const UnifiedIndex&) = delete;
    UnifiedIndex& operator=(const UnifiedIndex&) = delete;

    // ═══════════════════════════════════════════════════════════════════════
    // Lifecycle
    // ═══════════════════════════════════════════════════════════════════════

    // Create new index at path
    bool create(const std::string& base_path, size_t initial_capacity = INITIAL_CAPACITY) {
        base_path_ = base_path;

        // Create directory if needed
        std::string dir = base_path_;
        if (dir.back() != '/') {
            size_t last_slash = dir.find_last_of('/');
            if (last_slash != std::string::npos) {
                dir = dir.substr(0, last_slash);
            }
        }

        // Create main index file
        std::string idx_path = base_path_ + ".unified";
        size_t idx_size = sizeof(UnifiedIndexHeader) + initial_capacity * sizeof(IndexedNode);

        if (!index_region_.create(idx_path, idx_size)) {
            std::cerr << "[UnifiedIndex] Failed to create index file\n";
            return false;
        }

        // Initialize header
        auto* header = index_region_.as<UnifiedIndexHeader>();
        header->magic = UNIFIED_MAGIC;
        header->version = UNIFIED_VERSION;
        header->node_count = 0;
        header->capacity = initial_capacity;
        header->deleted_count = 0;
        header->entry_point_slot = UINT32_MAX;
        header->max_level = 0;
        header->hnsw_m = DEFAULT_M;
        header->hnsw_ef_construction = DEFAULT_EF_CONSTRUCTION;
        header->snapshot_id = 0;
        header->checksum = 0;
        header->wal_sequence = 0;  // Initialize WAL sequence for crash recovery

        // Create vectors file
        std::string vec_path = base_path_ + ".vectors";
        size_t vec_size = initial_capacity * sizeof(QuantizedVector);
        if (!vectors_region_.create(vec_path, vec_size)) {
            std::cerr << "[UnifiedIndex] Failed to create vectors file\n";
            return false;
        }

        // Create binary vectors file (48 bytes each for fast first-pass)
        std::string bin_path = base_path_ + ".binary";
        size_t bin_size = initial_capacity * sizeof(BinaryVector);
        if (!binary_region_.create(bin_path, bin_size)) {
            std::cerr << "[UnifiedIndex] Failed to create binary vectors file\n";
            return false;
        }
        has_binary_ = true;

        // Create metadata file
        std::string meta_path = base_path_ + ".meta";
        size_t meta_size = initial_capacity * sizeof(NodeMeta);
        if (!meta_region_.create(meta_path, meta_size)) {
            std::cerr << "[UnifiedIndex] Failed to create metadata file\n";
            return false;
        }

        // Create connection pool
        if (!connections_.create(base_path_ + ".connections", initial_capacity)) {
            std::cerr << "[UnifiedIndex] Failed to create connection pool\n";
            return false;
        }

        // Create payload store
        if (!payloads_.create(base_path_ + ".payloads")) {
            std::cerr << "[UnifiedIndex] Failed to create payload store\n";
            return false;
        }

        // Create edge store
        if (!edges_.create(base_path_ + ".edges")) {
            std::cerr << "[UnifiedIndex] Failed to create edge store\n";
            return false;
        }

        // Create tag index
        if (!tags_.create(base_path_ + ".tags")) {
            std::cerr << "[UnifiedIndex] Failed to create tag index\n";
            return false;
        }

        // Initialize slot allocation
        next_slot_ = 0;
        capacity_ = initial_capacity;

        std::cerr << "[UnifiedIndex] Created with capacity " << initial_capacity << "\n";
        return true;
    }

    // Safe create: uses O_EXCL to atomically create, fails if file exists
    // Returns: true if created new index, false if file exists or error
    // Caller should fall back to open() if this returns false
    bool create_safe(const std::string& base_path, size_t initial_capacity = INITIAL_CAPACITY) {
        base_path_ = base_path;

        // Create directory if needed
        std::string dir = base_path_;
        if (dir.back() != '/') {
            size_t last_slash = dir.find_last_of('/');
            if (last_slash != std::string::npos) {
                dir = dir.substr(0, last_slash);
            }
        }

        // Try atomic create of main index file (fails if exists)
        std::string idx_path = base_path_ + ".unified";
        size_t idx_size = sizeof(UnifiedIndexHeader) + initial_capacity * sizeof(IndexedNode);

        if (!index_region_.create_exclusive(idx_path, idx_size)) {
            // File exists or error - caller should try open()
            return false;
        }

        // We successfully created the primary file atomically
        // Now create supporting files (safe since we own the lock via primary file)

        // Initialize header
        auto* header = index_region_.as<UnifiedIndexHeader>();
        header->magic = UNIFIED_MAGIC;
        header->version = UNIFIED_VERSION;
        header->node_count = 0;
        header->capacity = initial_capacity;
        header->deleted_count = 0;
        header->entry_point_slot = UINT32_MAX;
        header->max_level = 0;
        header->hnsw_m = DEFAULT_M;
        header->hnsw_ef_construction = DEFAULT_EF_CONSTRUCTION;
        header->snapshot_id = 0;
        header->checksum = 0;
        header->wal_sequence = 0;

        // Create vectors file
        std::string vec_path = base_path_ + ".vectors";
        size_t vec_size = initial_capacity * sizeof(QuantizedVector);
        if (!vectors_region_.create(vec_path, vec_size)) {
            std::cerr << "[UnifiedIndex] Failed to create vectors file\n";
            cleanup_failed_create();
            return false;
        }

        // Create binary vectors file
        std::string bin_path = base_path_ + ".binary";
        size_t bin_size = initial_capacity * sizeof(BinaryVector);
        if (!binary_region_.create(bin_path, bin_size)) {
            std::cerr << "[UnifiedIndex] Failed to create binary vectors file\n";
            cleanup_failed_create();
            return false;
        }
        has_binary_ = true;

        // Create metadata file
        std::string meta_path = base_path_ + ".meta";
        size_t meta_size = initial_capacity * sizeof(NodeMeta);
        if (!meta_region_.create(meta_path, meta_size)) {
            std::cerr << "[UnifiedIndex] Failed to create metadata file\n";
            cleanup_failed_create();
            return false;
        }

        // Create connection pool
        if (!connections_.create(base_path_ + ".connections", initial_capacity)) {
            std::cerr << "[UnifiedIndex] Failed to create connection pool\n";
            cleanup_failed_create();
            return false;
        }

        // Create payload store
        if (!payloads_.create(base_path_ + ".payloads")) {
            std::cerr << "[UnifiedIndex] Failed to create payload store\n";
            cleanup_failed_create();
            return false;
        }

        // Create edge store
        if (!edges_.create(base_path_ + ".edges")) {
            std::cerr << "[UnifiedIndex] Failed to create edge store\n";
            cleanup_failed_create();
            return false;
        }

        // Create tag index
        if (!tags_.create(base_path_ + ".tags")) {
            std::cerr << "[UnifiedIndex] Failed to create tag index\n";
            cleanup_failed_create();
            return false;
        }

        // Initialize slot allocation
        next_slot_ = 0;
        capacity_ = initial_capacity;

        std::cerr << "[UnifiedIndex] Created safely with capacity " << initial_capacity << "\n";
        return true;
    }

    // Open existing index
    bool open(const std::string& base_path) {
        base_path_ = base_path;

        // Open main index file (with write access for updates)
        if (!index_region_.open(base_path_ + ".unified", false)) {
            return false;
        }

        auto* header = index_region_.as<const UnifiedIndexHeader>();
        if (header->magic != UNIFIED_MAGIC) {
            std::cerr << "[UnifiedIndex] Invalid magic\n";
            return false;
        }

        if (header->version > UNIFIED_VERSION) {
            std::cerr << "[UnifiedIndex] Version too new\n";
            return false;
        }

        // Open vectors (with write access)
        if (!vectors_region_.open(base_path_ + ".vectors", false)) {
            return false;
        }

        // Open binary vectors (optional - create if missing for scale optimization)
        std::string bin_path = base_path_ + ".binary";
        if (binary_region_.open(bin_path, false)) {
            has_binary_ = true;
        } else {
            // Create binary vectors from existing int8 vectors
            size_t bin_size = header->capacity * sizeof(BinaryVector);
            if (binary_region_.create(bin_path, bin_size)) {
                has_binary_ = true;
                rebuild_binary_vectors();
            }
        }

        // Open metadata (with write access)
        if (!meta_region_.open(base_path_ + ".meta", false)) {
            return false;
        }

        // Open connection pool
        if (!connections_.open(base_path_ + ".connections")) {
            return false;
        }

        // Open payload store (create if missing for backward compatibility)
        std::string payloads_path = base_path_ + ".payloads";
        if (!payloads_.open(payloads_path)) {
            if (!payloads_.create(payloads_path)) {
                std::cerr << "[UnifiedIndex] Failed to create payload store\n";
                return false;
            }
        }

        // Open edge store (create if missing for backward compatibility)
        std::string edges_path = base_path_ + ".edges";
        if (!edges_.open(edges_path)) {
            if (!edges_.create(edges_path)) {
                std::cerr << "[UnifiedIndex] Failed to create edge store\n";
                return false;
            }
        }

        // Open tag index (create if missing for backward compatibility)
        std::string tags_path = base_path_ + ".tags";
        if (!tags_.open(tags_path)) {
            if (!tags_.create(tags_path)) {
                std::cerr << "[UnifiedIndex] Failed to create tag index\n";
                return false;
            }
        }

        // Rebuild NodeId -> SlotId lookup
        rebuild_id_index();

        // Calculate required slots
        uint64_t used_slots = header->node_count + header->deleted_count;

        // Validate capacity - if corrupted (0 or less than used), fix it
        if (header->capacity < used_slots) {
            std::cerr << "[UnifiedIndex] Corrupted capacity (" << header->capacity
                      << ") < used slots (" << used_slots << "), repairing...\n";

            // Calculate new capacity with headroom
            size_t new_capacity = std::max(used_slots * 2, static_cast<uint64_t>(INITIAL_CAPACITY));

            // Update header
            auto* mutable_header = index_region_.as<UnifiedIndexHeader>();
            mutable_header->capacity = new_capacity;
            index_region_.sync();

            // Grow the backing files to match new capacity
            std::string vec_path = base_path_ + ".vectors";
            std::string meta_path = base_path_ + ".meta";
            size_t new_vec_size = new_capacity * sizeof(QuantizedVector);
            size_t new_meta_size = new_capacity * sizeof(NodeMeta);

            extend_file(vec_path, new_vec_size);
            extend_file(meta_path, new_meta_size);

            // Reopen vectors and meta with new size
            vectors_region_.close();
            meta_region_.close();
            vectors_region_.open(vec_path, false);
            meta_region_.open(meta_path, false);

            capacity_ = new_capacity;
            std::cerr << "[UnifiedIndex] Repaired capacity to " << new_capacity << "\n";
        } else {
            capacity_ = header->capacity;
        }

        next_slot_ = used_slots;

        std::cerr << "[UnifiedIndex] Opened " << header->node_count << " nodes (capacity "
                  << capacity_ << ")\n";
        return true;
    }

    void close() {
        sync();
        index_region_.close();
        vectors_region_.close();
        meta_region_.close();
        connections_.close();
        payloads_.close();
        edges_.close();
        tags_.close();
        binary_region_.close();
    }

    void sync() {
        // msync() is thread-safe, use shared_lock to allow concurrent reads
        std::shared_lock lock(mutex_);
        sync_unlocked();
    }

    bool valid() const {
        return index_region_.valid() && vectors_region_.valid() &&
               meta_region_.valid() && connections_.valid();
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Node operations
    // ═══════════════════════════════════════════════════════════════════════

    // Insert a node with automatic Hilbert ordering
    SlotId insert(const NodeId& id, const Node& node) {
        std::unique_lock lock(mutex_);

        // Check if already exists
        auto it = id_to_slot_.find(id);
        if (it != id_to_slot_.end()) {
            return it->second;  // Already exists
        }

        // Ensure capacity
        if (next_slot_ >= capacity_) {
            if (!grow()) {
                return SlotId::invalid();
            }
        }

        // Allocate slot
        SlotId slot(next_slot_++);

        // Compute quantized vector and Hilbert key
        QuantizedVector qvec = QuantizedVector::from_float(node.nu);
        uint64_t hkey = hilbert_key(qvec);

        // Assign HNSW level
        uint8_t level = assign_level();

        // Write vector
        auto* vectors = vectors_region_.as<QuantizedVector>();
        vectors[slot.value] = qvec;

        // Write binary vector for fast first-pass search
        if (has_binary_) {
            auto* binvecs = binary_region_.as<BinaryVector>();
            binvecs[slot.value] = BinaryVector::from_quantized(qvec);
        }

        // Store payload if present
        uint32_t payload_offset = 0;
        uint32_t payload_size = 0;
        if (!node.payload.empty()) {
            payload_offset = static_cast<uint32_t>(payloads_.store(node.payload));
            payload_size = static_cast<uint32_t>(node.payload.size());
        }

        // Store edges if present
        uint32_t edge_offset = 0;
        if (!node.edges.empty()) {
            edge_offset = static_cast<uint32_t>(store_edges(node.edges));
        }

        // Store tags in inverted index
        if (!node.tags.empty()) {
            tags_.add(slot.value, node.tags);
            tags_.save();  // Persist immediately for crash safety
        }

        // Write metadata (v2: 64-bit offsets)
        auto* metas = meta_region_.as<NodeMeta>();
        metas[slot.value] = NodeMeta{
            id,
            node.tau_created,
            node.tau_accessed,
            static_cast<uint64_t>(slot.value),  // vector_offset
            static_cast<uint64_t>(payload_offset),
            static_cast<uint64_t>(edge_offset),
            node.kappa.mu,
            node.kappa.sigma_sq,
            node.delta,
            payload_size,
            node.node_type,
            StorageTier::Hot,
            0,                // flags
            0                 // reserved
        };

        // Create empty connections (will be populated during HNSW insert)
        std::vector<std::vector<ConnectionEdge>> empty_connections(level + 1);
        uint64_t conn_offset = connections_.allocate(slot.value, level + 1, empty_connections);

        // Write indexed node
        auto* nodes = node_array();
        nodes[slot.value] = IndexedNode{
            id,
            hkey,
            slot.value,   // vector_offset
            slot.value,   // meta_offset
            conn_offset,
            level,
            0,            // flags
            0,            // connection_count (updated during HNSW insert)
            {0, 0, 0, 0}  // reserved
        };

        // Update lookup
        id_to_slot_[id] = slot;

        // Update header
        auto* header = index_region_.as<UnifiedIndexHeader>();
        header->node_count++;

        // Update max level
        if (level > header->max_level) {
            header->max_level = level;
            header->entry_point_slot = slot.value;
        }

        // If first node, set as entry point
        if (header->entry_point_slot == UINT32_MAX) {
            header->entry_point_slot = slot.value;
        }

        // Insert into HNSW graph
        insert_hnsw(slot);

        return slot;
    }

    // Update an existing node's payload and metadata (for ε-yajna compression)
    bool update(const NodeId& id, const Node& node) {
        std::unique_lock lock(mutex_);

        auto it = id_to_slot_.find(id);
        if (it == id_to_slot_.end()) {
            return false;  // Node doesn't exist
        }

        SlotId slot = it->second;

        // Update vector if changed
        QuantizedVector qvec = QuantizedVector::from_float(node.nu);
        auto* vectors = vectors_region_.as<QuantizedVector>();
        vectors[slot.value] = qvec;

        if (has_binary_) {
            auto* binvecs = binary_region_.as<BinaryVector>();
            binvecs[slot.value] = BinaryVector::from_quantized(qvec);
        }

        // Store new payload (old payload space is orphaned but reclaimed on rebuild)
        uint32_t payload_offset = 0;
        uint32_t payload_size = 0;
        if (!node.payload.empty()) {
            payload_offset = static_cast<uint32_t>(payloads_.store(node.payload));
            payload_size = static_cast<uint32_t>(node.payload.size());
        }

        // Store new edges (old edge space is orphaned but reclaimed on rebuild)
        uint32_t edge_offset = 0;
        if (!node.edges.empty()) {
            edge_offset = static_cast<uint32_t>(store_edges(node.edges));
        }

        // Update metadata
        auto* metas = meta_region_.as<NodeMeta>();
        metas[slot.value].tau_accessed = node.tau_accessed;
        metas[slot.value].tau_created = node.tau_created;
        metas[slot.value].node_type = node.node_type;
        metas[slot.value].confidence_mu = node.kappa.mu;
        metas[slot.value].confidence_sigma = node.kappa.sigma_sq;
        metas[slot.value].decay_rate = node.delta;
        metas[slot.value].payload_offset = payload_offset;
        metas[slot.value].payload_size = payload_size;
        metas[slot.value].edge_offset = edge_offset;

        // Update tags: clear old tags and add new ones
        tags_.remove_all(slot.value);
        if (!node.tags.empty()) {
            tags_.add(slot.value, node.tags);
        }
        tags_.save();

        // Sync to persist changes
        edges_.sync();
        payloads_.sync();
        meta_region_.sync();
        vectors_region_.sync();

        return true;
    }

    // Get node by ID
    const IndexedNode* get(const NodeId& id) const {
        std::shared_lock lock(mutex_);

        auto it = id_to_slot_.find(id);
        if (it == id_to_slot_.end()) {
            return nullptr;
        }

        const auto* node = &node_array()[it->second.value];
        if (node->flags & NODE_FLAG_DELETED) {
            return nullptr;
        }

        return node;
    }

    // Get node by slot
    const IndexedNode* get_slot(SlotId slot) const {
        if (!slot.valid() || slot.value >= next_slot_) {
            return nullptr;
        }

        const auto* node = &node_array()[slot.value];
        if (node->flags & NODE_FLAG_DELETED) {
            return nullptr;
        }

        return node;
    }

    // Get quantized vector for a slot
    const QuantizedVector* vector(SlotId slot) const {
        if (!slot.valid()) return nullptr;
        return &vectors_region_.as<const QuantizedVector>()[slot.value];
    }

    // Get metadata for a slot
    const NodeMeta* meta(SlotId slot) const {
        if (!slot.valid()) return nullptr;
        return &meta_region_.as<const NodeMeta>()[slot.value];
    }

    // Update access timestamp for a slot
    void touch(SlotId slot) {
        if (!slot.valid()) return;
        auto* metas = meta_region_.as<NodeMeta>();
        metas[slot.value].tau_accessed = now();
    }

    // Update confidence for a slot
    bool update_confidence(SlotId slot, const Confidence& kappa) {
        if (!slot.valid()) return false;
        auto* metas = meta_region_.as<NodeMeta>();
        metas[slot.value].confidence_mu = kappa.mu;
        metas[slot.value].confidence_sigma = kappa.sigma_sq;
        return true;
    }

    // Mark node as deleted (soft delete)
    bool remove(const NodeId& id) {
        std::unique_lock lock(mutex_);
        auto it = id_to_slot_.find(id);
        if (it == id_to_slot_.end()) return false;

        SlotId slot = it->second;
        auto* nodes = node_array();
        nodes[slot.value].flags |= NODE_FLAG_DELETED;

        // Update header
        auto* header = index_region_.as<UnifiedIndexHeader>();
        if (header->node_count > 0) header->node_count--;
        header->deleted_count++;

        // Remove from lookup
        id_to_slot_.erase(it);
        return true;
    }

    // Get payload for a slot
    std::vector<uint8_t> payload(SlotId slot) const {
        if (!slot.valid()) return {};
        auto* m = meta(slot);
        if (!m || m->payload_offset == 0 || m->payload_size == 0) return {};
        return payloads_.read(m->payload_offset);
    }

    // Lookup slot by NodeId
    SlotId lookup(const NodeId& id) const {
        std::shared_lock lock(mutex_);
        auto it = id_to_slot_.find(id);
        return (it != id_to_slot_.end()) ? it->second : SlotId::invalid();
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Search (HNSW)
    // ═══════════════════════════════════════════════════════════════════════

    // Search for k nearest neighbors
    std::vector<std::pair<SlotId, float>> search(
        const QuantizedVector& query, size_t k, size_t ef = 0) const
    {
        std::shared_lock lock(mutex_);

        auto* header = index_region_.as<const UnifiedIndexHeader>();
        if (header->node_count == 0) {
            return {};
        }

        if (ef == 0) ef = DEFAULT_EF_SEARCH;

        // Start from entry point
        SlotId entry_point(header->entry_point_slot);
        if (!entry_point.valid()) {
            return {};
        }

        // Search from top to bottom layers
        SlotId current = entry_point;
        for (int level = static_cast<int>(header->max_level); level > 0; --level) {
            current = search_layer_greedy(query, current, level);
        }

        // Search layer 0 with ef width
        auto candidates = search_layer(query, current, 0, ef);

        // Return top k
        std::vector<std::pair<SlotId, float>> results;
        results.reserve(std::min(k, candidates.size()));

        for (size_t i = 0; i < k && i < candidates.size(); ++i) {
            results.push_back(candidates[i]);
        }

        return results;
    }

    // Two-stage search: HNSW first-pass with larger ef, then int8 rerank
    // O(log N) complexity via HNSW instead of O(N) binary scan
    // For 100M+ scale: first pass retrieves candidates, second pass refines
    std::vector<std::pair<SlotId, float>> search_two_stage(
        const QuantizedVector& query, size_t k, size_t first_pass_k = 0) const
    {
        // For small datasets, regular search is sufficient
        if (count() < 1000) {
            return search(query, k);
        }

        std::shared_lock lock(mutex_);

        auto* header = index_region_.as<const UnifiedIndexHeader>();
        if (header->node_count == 0) return {};

        // First pass: HNSW with larger ef_search for more candidates
        // Rule of thumb: retrieve 10x candidates for reranking
        if (first_pass_k == 0) first_pass_k = std::max(k * 10, size_t(100));

        // Use HNSW search with higher ef for better recall in first pass
        size_t ef_first_pass = std::max(first_pass_k * 2, size_t(200));

        // Get entry point and search
        SlotId entry_point(header->entry_point_slot);
        if (!entry_point.valid()) return {};

        // Search from top to bottom layers
        SlotId current = entry_point;
        for (int level = static_cast<int>(header->max_level); level > 0; --level) {
            current = search_layer_greedy(query, current, level);
        }

        // Search layer 0 with expanded ef for first pass
        auto candidates = search_layer(query, current, 0, ef_first_pass);

        // Limit to first_pass_k candidates
        if (candidates.size() > first_pass_k) {
            candidates.resize(first_pass_k);
        }

        // Second pass: rerank with full int8 cosine similarity
        auto* vectors = vectors_region_.as<const QuantizedVector>();
        for (auto& [slot, score] : candidates) {
            // Use exact int8 cosine for better precision
            score = vectors[slot.value].cosine_approx(query);
        }

        // Sort by reranked score
        std::sort(candidates.begin(), candidates.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        // Return top k
        if (candidates.size() > k) {
            candidates.resize(k);
        }

        return candidates;
    }

    // Binary brute-force search (Hamming distance)
    // DEPRECATED for large scale - use HNSW-based search_two_stage instead
    // Kept for small datasets (<10K) where O(N) is acceptable
    // TODO: Replace with IVF-PQ for very large scale (>10M nodes)
    std::vector<std::pair<SlotId, float>> search_binary_brute(
        const BinaryVector& query, size_t k) const
    {
        if (!has_binary_) return {};

        auto* header = index_region_.as<const UnifiedIndexHeader>();
        size_t total = header->node_count + header->deleted_count;

        auto* binvecs = binary_region_.as<const BinaryVector>();
        const auto* nodes = node_array();

        // Compute all Hamming distances
        std::vector<std::pair<SlotId, uint32_t>> dists;
        dists.reserve(total);

        for (size_t i = 0; i < total; ++i) {
            if (nodes[i].flags & NODE_FLAG_DELETED) continue;
            uint32_t dist = query.hamming_fast(binvecs[i]);
            dists.emplace_back(SlotId(i), dist);
        }

        // Partial sort for top k
        if (dists.size() > k) {
            std::partial_sort(dists.begin(), dists.begin() + k, dists.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });
            dists.resize(k);
        } else {
            std::sort(dists.begin(), dists.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });
        }

        // Convert to similarity scores
        std::vector<std::pair<SlotId, float>> results;
        results.reserve(dists.size());
        for (const auto& [slot, dist] : dists) {
            float sim = 1.0f - static_cast<float>(dist) / EMBED_DIM;
            results.emplace_back(slot, sim);
        }

        return results;
    }

    bool has_binary_vectors() const { return has_binary_; }

    // ═══════════════════════════════════════════════════════════════════════
    // Statistics
    // ═══════════════════════════════════════════════════════════════════════

    size_t count() const {
        if (!index_region_.valid()) return 0;
        return index_region_.as<const UnifiedIndexHeader>()->node_count;
    }

    size_t capacity() const { return capacity_; }

    size_t deleted_count() const {
        if (!index_region_.valid()) return 0;
        return index_region_.as<const UnifiedIndexHeader>()->deleted_count;
    }

    uint32_t max_level() const {
        if (!index_region_.valid()) return 0;
        return index_region_.as<const UnifiedIndexHeader>()->max_level;
    }

    uint64_t snapshot_id() const {
        if (!index_region_.valid()) return 0;
        return index_region_.as<const UnifiedIndexHeader>()->snapshot_id;
    }

    // WAL sequence tracking for crash recovery
    uint64_t wal_sequence() const {
        if (!index_region_.valid()) return 0;
        return index_region_.as<const UnifiedIndexHeader>()->wal_sequence;
    }

    void set_wal_sequence(uint64_t seq) {
        if (!index_region_.valid()) return;
        std::unique_lock lock(mutex_);
        auto* header = index_region_.as<UnifiedIndexHeader>();
        // Monotonic: only update if new seq is greater (prevents regression)
        if (seq > header->wal_sequence) {
            header->wal_sequence = seq;
            // Don't sync on every call - sync is expensive and will be done periodically
        }
    }

    // Access tag index for filtered queries
    const SlotTagIndex& slot_tag_index() const { return tags_; }
    SlotTagIndex& slot_tag_index() { return tags_; }

    // ═══════════════════════════════════════════════════════════════════════
    // Iteration
    // ═══════════════════════════════════════════════════════════════════════

    // Iterate over all active nodes, reconstructing Node from stored components
    void for_each(std::function<void(const NodeId&, const Node&)> fn) const {
        if (!valid()) return;

        std::shared_lock lock(mutex_);
        auto* header = index_region_.as<const UnifiedIndexHeader>();
        const auto* nodes = node_array();

        for (size_t i = 0; i < header->node_count + header->deleted_count; ++i) {
            if (nodes[i].flags & NODE_FLAG_DELETED) continue;

            SlotId slot(static_cast<uint32_t>(i));
            auto* meta = this->meta(slot);
            auto* qvec = this->vector(slot);
            if (!meta || !qvec) continue;

            Node node;
            node.id = meta->id;  // Use meta->id (correct) instead of nodes[i].id (may be zero after restore)
            node.node_type = meta->node_type;
            node.nu = qvec->to_float();
            node.tau_created = meta->tau_created;
            node.tau_accessed = meta->tau_accessed;
            node.delta = meta->decay_rate;
            node.kappa.mu = meta->confidence_mu;
            node.kappa.sigma_sq = meta->confidence_sigma;

            // Load payload if present
            if (meta->payload_offset != 0 && meta->payload_size != 0) {
                node.payload = payloads_.read(meta->payload_offset);
            }

            // Load edges if present
            if (meta->edge_offset != 0) {
                node.edges = load_edges(meta->edge_offset);
            }

            // Tags not loaded inline to save memory during iteration
            // Use tags_.tags_for_slot(slot) or mind->get_tags(id) if needed

            fn(meta->id, node);  // Use meta->id for correct ID
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Copy-on-Write Snapshots
    // ═══════════════════════════════════════════════════════════════════════

    // Snapshot metadata
    struct SnapshotInfo {
        uint64_t snapshot_id;
        uint64_t timestamp;
        uint64_t node_count;
        std::string base_path;
    };

    // Create a consistent point-in-time snapshot
    // Uses filesystem reflinks (CoW) when available for instant, space-efficient snapshots
    // Falls back to regular copy if reflinks not supported
    bool create_snapshot(const std::string& snapshot_path) {
        std::unique_lock lock(mutex_);

        if (!valid()) {
            std::cerr << "[UnifiedIndex] Cannot snapshot: index not valid\n";
            return false;
        }

        // Sync all pending writes to disk (already hold lock, use unlocked version)
        sync_unlocked();

        // Increment snapshot ID
        auto* header = index_region_.as<UnifiedIndexHeader>();
        header->snapshot_id++;
        uint64_t snap_id = header->snapshot_id;

        // Sync header update
        index_region_.sync();

        // Copy each file using CoW if available
        bool success = true;
        success &= copy_file_cow(base_path_ + ".unified", snapshot_path + ".unified");
        success &= copy_file_cow(base_path_ + ".vectors", snapshot_path + ".vectors");
        success &= copy_file_cow(base_path_ + ".meta", snapshot_path + ".meta");
        success &= copy_file_cow(base_path_ + ".connections", snapshot_path + ".connections");
        success &= copy_file_cow(base_path_ + ".payloads", snapshot_path + ".payloads");
        success &= copy_file_cow(base_path_ + ".edges", snapshot_path + ".edges");
        success &= copy_file_cow(base_path_ + ".tags", snapshot_path + ".tags");

        if (success) {
            std::cerr << "[UnifiedIndex] Snapshot " << snap_id << " created at "
                      << snapshot_path << "\n";
        } else {
            std::cerr << "[UnifiedIndex] Snapshot failed\n";
        }

        return success;
    }

    // Get info about current state (for snapshot metadata)
    SnapshotInfo info() const {
        if (!valid()) return {};

        auto* header = index_region_.as<const UnifiedIndexHeader>();
        return SnapshotInfo{
            header->snapshot_id,
            static_cast<uint64_t>(std::time(nullptr)),
            header->node_count,
            base_path_
        };
    }

    // Public accessor for loading edges (used by TieredStorage)
    std::vector<Edge> get_edges(uint64_t offset) const {
        return load_edges(offset);
    }

private:
    // Sync without taking lock (caller must hold mutex_)
    void sync_unlocked() {
        connections_.sync();
        payloads_.sync();
        edges_.sync();
        tags_.save();
        index_region_.sync();
        vectors_region_.sync();
        meta_region_.sync();
        if (has_binary_) binary_region_.sync();
    }

    // Copy file using CoW (reflink) if available, fall back to regular copy
    static bool copy_file_cow(const std::string& src, const std::string& dst) {
        // Try reflink first (instant CoW copy)
        #ifdef __linux__
        int src_fd = ::open(src.c_str(), O_RDONLY);
        if (src_fd < 0) return false;

        int dst_fd = ::open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (dst_fd < 0) {
            ::close(src_fd);
            return false;
        }

        // Try FICLONE (btrfs, xfs reflink)
        #ifndef FICLONE
        #define FICLONE _IOW(0x94, 9, int)
        #endif

        int ret = ioctl(dst_fd, FICLONE, src_fd);
        ::close(src_fd);
        ::close(dst_fd);

        if (ret == 0) {
            return true;  // Reflink succeeded
        }
        #endif

        // Fall back to regular copy
        return copy_file_regular(src, dst);
    }

    static bool copy_file_regular(const std::string& src, const std::string& dst) {
        std::ifstream in(src, std::ios::binary);
        if (!in) return false;

        std::ofstream out(dst, std::ios::binary);
        if (!out) return false;

        // Use larger buffer for efficiency
        constexpr size_t BUFFER_SIZE = 64 * 1024;  // 64KB
        std::vector<char> buffer(BUFFER_SIZE);

        while (in) {
            in.read(buffer.data(), BUFFER_SIZE);
            std::streamsize bytes_read = in.gcount();
            if (bytes_read > 0) {
                out.write(buffer.data(), bytes_read);
            }
        }

        return out.good();
    }

private:
    // Cleanup after failed create_safe - remove partially created files
    void cleanup_failed_create() {
        index_region_.close();
        vectors_region_.close();
        binary_region_.close();
        meta_region_.close();
        // Remove all files we may have created
        std::remove((base_path_ + ".unified").c_str());
        std::remove((base_path_ + ".vectors").c_str());
        std::remove((base_path_ + ".binary").c_str());
        std::remove((base_path_ + ".meta").c_str());
        std::remove((base_path_ + ".connections").c_str());
        std::remove((base_path_ + ".payloads").c_str());
        std::remove((base_path_ + ".edges").c_str());
        std::remove((base_path_ + ".tags").c_str());
    }

    // Get node array from index region
    IndexedNode* node_array() {
        return reinterpret_cast<IndexedNode*>(
            reinterpret_cast<uint8_t*>(index_region_.as<UnifiedIndexHeader>()) +
            sizeof(UnifiedIndexHeader));
    }

    const IndexedNode* node_array() const {
        return reinterpret_cast<const IndexedNode*>(
            reinterpret_cast<const uint8_t*>(index_region_.as<const UnifiedIndexHeader>()) +
            sizeof(UnifiedIndexHeader));
    }

    // Rebuild NodeId -> SlotId mapping on open
    void rebuild_id_index() {
        auto* header = index_region_.as<const UnifiedIndexHeader>();
        const auto* nodes = node_array();

        id_to_slot_.clear();
        id_to_slot_.reserve(header->node_count);

        for (size_t i = 0; i < header->node_count + header->deleted_count; ++i) {
            if (!(nodes[i].flags & NODE_FLAG_DELETED)) {
                // Use meta->id (correct) instead of nodes[i].id (may be zero after restore)
                auto* m = meta(SlotId(static_cast<uint32_t>(i)));
                if (m) {
                    id_to_slot_[m->id] = SlotId(i);
                }
            }
        }
    }

    // Rebuild binary vectors from int8 vectors (migration)
    void rebuild_binary_vectors() {
        if (!has_binary_ || !vectors_region_.valid()) return;

        auto* header = index_region_.as<const UnifiedIndexHeader>();
        size_t total = header->node_count + header->deleted_count;

        auto* vectors = vectors_region_.as<const QuantizedVector>();
        auto* binvecs = binary_region_.as<BinaryVector>();
        const auto* nodes = node_array();

        for (size_t i = 0; i < total; ++i) {
            if (!(nodes[i].flags & NODE_FLAG_DELETED)) {
                binvecs[i] = BinaryVector::from_quantized(vectors[i]);
            }
        }

        std::cerr << "[UnifiedIndex] Rebuilt " << header->node_count
                  << " binary vectors for two-stage search\n";
    }

    // Grow capacity (atomic two-phase approach)
    // Phase 1: Extend all files without touching existing mappings
    // Phase 2: Create new mappings, swap in atomically, update header last
    bool grow() {
        size_t new_capacity = capacity_ * GROWTH_FACTOR;

        // Safety: ensure we never grow to 0 or smaller than needed
        if (new_capacity < INITIAL_CAPACITY) {
            new_capacity = INITIAL_CAPACITY;
        }
        if (new_capacity <= next_slot_) {
            new_capacity = next_slot_ * GROWTH_FACTOR;
        }

        std::cerr << "[UnifiedIndex] Growing from " << capacity_ << " to " << new_capacity << "\n";

        // Acquire cross-process lock for grow operation
        GrowLock lock(base_path_);
        if (!lock.lock_exclusive()) {
            std::cerr << "[UnifiedIndex] Could not acquire grow lock (another process growing?)\n";
            return false;
        }

        std::string idx_path = base_path_ + ".unified";
        std::string vec_path = base_path_ + ".vectors";
        std::string meta_path = base_path_ + ".meta";

        size_t new_idx_size = sizeof(UnifiedIndexHeader) + new_capacity * sizeof(IndexedNode);
        size_t new_vec_size = new_capacity * sizeof(QuantizedVector);
        size_t new_meta_size = new_capacity * sizeof(NodeMeta);

        // Phase 1: Extend files WITHOUT closing existing mappings
        // If any fails, old mappings remain valid
        if (!extend_file(idx_path, new_idx_size)) {
            std::cerr << "[UnifiedIndex] Failed to extend index file\n";
            return false;
        }
        if (!extend_file(vec_path, new_vec_size)) {
            std::cerr << "[UnifiedIndex] Failed to extend vectors file\n";
            return false;
        }
        if (!extend_file(meta_path, new_meta_size)) {
            std::cerr << "[UnifiedIndex] Failed to extend meta file\n";
            return false;
        }

        // Phase 2: Open new mappings into temporaries
        MappedRegion new_index, new_vectors, new_meta;
        if (!new_index.open(idx_path, false)) {
            std::cerr << "[UnifiedIndex] Failed to remap index\n";
            return false;
        }
        if (!new_vectors.open(vec_path, false)) {
            std::cerr << "[UnifiedIndex] Failed to remap vectors\n";
            return false;
        }
        if (!new_meta.open(meta_path, false)) {
            std::cerr << "[UnifiedIndex] Failed to remap meta\n";
            return false;
        }

        // Phase 3: Swap mappings atomically (old mappings closed by move assignment)
        index_region_ = std::move(new_index);
        vectors_region_ = std::move(new_vectors);
        meta_region_ = std::move(new_meta);

        // Phase 4: Update header LAST and sync
        auto* header = index_region_.as<UnifiedIndexHeader>();
        header->capacity = new_capacity;
        index_region_.sync();
        capacity_ = new_capacity;

        std::cerr << "[UnifiedIndex] Grow complete: capacity=" << new_capacity << "\n";
        return true;
    }

    // Assign HNSW level for new node (exponential distribution)
    uint8_t assign_level() {
        static thread_local std::mt19937 rng(std::random_device{}());
        float ml = 1.0f / std::log(static_cast<float>(DEFAULT_M));
        float r = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
        uint8_t level = static_cast<uint8_t>(-std::log(r) * ml);
        return std::min(level, static_cast<uint8_t>(MAX_LEVEL - 1));
    }

    // Insert node into HNSW graph
    void insert_hnsw(SlotId new_slot) {
        auto* header = index_region_.as<UnifiedIndexHeader>();
        auto* nodes = node_array();
        auto& new_node = nodes[new_slot.value];

        const auto* query_vec = vector(new_slot);
        if (!query_vec) return;

        // If only node, nothing to connect
        if (header->node_count == 1) {
            return;
        }

        // Find entry point
        SlotId entry_point(header->entry_point_slot);
        if (!entry_point.valid()) return;

        // Descend from top level to new_node's level + 1
        SlotId current = entry_point;
        for (int level = static_cast<int>(header->max_level);
             level > static_cast<int>(new_node.level); --level) {
            current = search_layer_greedy(*query_vec, current, level);
        }

        // Insert at each level from new_node.level down to 0
        for (int level = static_cast<int>(new_node.level); level >= 0; --level) {
            auto neighbors = search_layer(*query_vec, current, level, header->hnsw_ef_construction);

            // Select best neighbors (up to M)
            size_t M = (level == 0) ? header->hnsw_m * 2 : header->hnsw_m;
            std::vector<ConnectionEdge> selected;
            for (size_t i = 0; i < M && i < neighbors.size(); ++i) {
                selected.emplace_back(neighbors[i].first.value, neighbors[i].second);
            }

            // Add connections from new node to neighbors
            update_connections(new_slot, level, selected);

            // Add reverse connections from neighbors to new node
            float dist_to_new = 0.0f;  // Will compute per neighbor
            for (const auto& edge : selected) {
                SlotId neighbor(edge.target_slot);
                const auto* neighbor_vec = vector(neighbor);
                if (neighbor_vec) {
                    float dist = query_vec->cosine_approx(*neighbor_vec);
                    add_reverse_connection(neighbor, level, new_slot, 1.0f - dist);
                }
            }

            // Update current for next level
            if (!neighbors.empty()) {
                current = neighbors[0].first;
            }
        }
    }

    // Greedy search at a single level (for descending)
    SlotId search_layer_greedy(const QuantizedVector& query, SlotId entry, int level) const {
        SlotId current = entry;
        float current_dist = distance_to(query, current);

        while (true) {
            SlotId best = current;
            float best_dist = current_dist;

            // Check all neighbors at this level
            auto neighbors = connections_.read_level(
                node_array()[current.value].connection_offset, level);

            for (const auto& edge : neighbors) {
                SlotId neighbor(edge.target_slot);
                if (node_array()[neighbor.value].flags & NODE_FLAG_DELETED) continue;

                float dist = distance_to(query, neighbor);
                if (dist < best_dist) {
                    best = neighbor;
                    best_dist = dist;
                }
            }

            if (best.value == current.value) {
                return current;  // No improvement found
            }

            current = best;
            current_dist = best_dist;
        }
    }

    // Search at a single level with ef candidates
    std::vector<std::pair<SlotId, float>> search_layer(
        const QuantizedVector& query, SlotId entry, int level, size_t ef) const
    {
        std::vector<std::pair<SlotId, float>> candidates;
        std::unordered_set<uint32_t> visited;

        // Priority queue: closest first
        auto cmp = [](const auto& a, const auto& b) { return a.second > b.second; };
        std::priority_queue<std::pair<SlotId, float>,
                           std::vector<std::pair<SlotId, float>>,
                           decltype(cmp)> frontier(cmp);

        float entry_dist = distance_to(query, entry);
        frontier.emplace(entry, entry_dist);
        visited.insert(entry.value);

        while (!frontier.empty() && candidates.size() < ef) {
            auto [current, current_dist] = frontier.top();
            frontier.pop();

            candidates.emplace_back(current, current_dist);

            // Explore neighbors
            auto neighbors = connections_.read_level(
                node_array()[current.value].connection_offset, level);

            for (const auto& edge : neighbors) {
                if (visited.count(edge.target_slot)) continue;
                visited.insert(edge.target_slot);

                SlotId neighbor(edge.target_slot);
                if (node_array()[neighbor.value].flags & NODE_FLAG_DELETED) continue;

                float dist = distance_to(query, neighbor);
                frontier.emplace(neighbor, dist);
            }
        }

        // Sort by distance
        std::sort(candidates.begin(), candidates.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });

        return candidates;
    }

    // Compute distance (1 - cosine similarity)
    float distance_to(const QuantizedVector& query, SlotId slot) const {
        const auto* vec = vector(slot);
        if (!vec) return 1.0f;
        return 1.0f - query.cosine_approx(*vec);
    }

    // Update connections for a node at a level
    void update_connections(SlotId slot, int level,
                           const std::vector<ConnectionEdge>& new_connections) {
        auto* nodes = node_array();
        auto& node = nodes[slot.value];

        // Read existing connections
        uint32_t slot_id;
        uint8_t level_count;
        std::vector<std::vector<ConnectionEdge>> all_connections;
        connections_.read(node.connection_offset, slot_id, level_count, all_connections);

        // Ensure level exists
        while (all_connections.size() <= static_cast<size_t>(level)) {
            all_connections.push_back({});
        }

        // Update connections at this level
        all_connections[level] = new_connections;

        // Reallocate
        connections_.remove(node.connection_offset);
        node.connection_offset = connections_.allocate(
            slot.value, static_cast<uint8_t>(all_connections.size()), all_connections);

        // Update connection count for level 0
        if (level == 0) {
            node.connection_count = static_cast<uint16_t>(new_connections.size());
        }
    }

    // Add a reverse connection from neighbor to new_node
    void add_reverse_connection(SlotId neighbor, int level, SlotId target, float dist) {
        auto* nodes = node_array();
        auto& node = nodes[neighbor.value];

        uint32_t slot_id;
        uint8_t level_count;
        std::vector<std::vector<ConnectionEdge>> all_connections;
        connections_.read(node.connection_offset, slot_id, level_count, all_connections);

        // Ensure level exists
        while (all_connections.size() <= static_cast<size_t>(level)) {
            all_connections.push_back({});
        }

        auto* header = index_region_.as<UnifiedIndexHeader>();
        size_t M = (level == 0) ? header->hnsw_m * 2 : header->hnsw_m;

        // Add new connection
        all_connections[level].emplace_back(target.value, dist);

        // Prune if over M
        if (all_connections[level].size() > M) {
            std::sort(all_connections[level].begin(), all_connections[level].end(),
                     [](const auto& a, const auto& b) { return a.distance < b.distance; });
            all_connections[level].resize(M);
        }

        // Reallocate
        connections_.remove(node.connection_offset);
        node.connection_offset = connections_.allocate(
            neighbor.value, static_cast<uint8_t>(all_connections.size()), all_connections);
    }

    // Serialize edges to blob storage
    // Format: [count:2][edge1:24][edge2:24]...
    // Each edge: [target.high:8][target.low:8][type:1][padding:3][weight:4] = 24 bytes
    uint64_t store_edges(const std::vector<Edge>& edges) {
        if (edges.empty()) return 0;

        std::vector<uint8_t> data;
        data.resize(sizeof(uint16_t) + edges.size() * 24);

        uint8_t* ptr = data.data();

        // Write count
        uint16_t count = static_cast<uint16_t>(edges.size());
        std::memcpy(ptr, &count, sizeof(count));
        ptr += sizeof(count);

        // Write each edge
        for (const auto& edge : edges) {
            std::memcpy(ptr, &edge.target.high, sizeof(uint64_t));
            ptr += sizeof(uint64_t);
            std::memcpy(ptr, &edge.target.low, sizeof(uint64_t));
            ptr += sizeof(uint64_t);
            *ptr++ = static_cast<uint8_t>(edge.type);
            *ptr++ = 0; *ptr++ = 0; *ptr++ = 0;  // padding
            std::memcpy(ptr, &edge.weight, sizeof(float));
            ptr += sizeof(float);
        }

        return edges_.store(data);
    }

    // Deserialize edges from blob storage
    std::vector<Edge> load_edges(uint64_t offset) const {
        if (offset == 0) return {};

        auto data = edges_.read(offset);
        if (data.size() < sizeof(uint16_t)) return {};

        const uint8_t* ptr = data.data();

        uint16_t count;
        std::memcpy(&count, ptr, sizeof(count));
        ptr += sizeof(count);

        if (data.size() < sizeof(uint16_t) + count * 24) return {};

        std::vector<Edge> result;
        result.reserve(count);

        for (uint16_t i = 0; i < count; ++i) {
            Edge edge;
            std::memcpy(&edge.target.high, ptr, sizeof(uint64_t));
            ptr += sizeof(uint64_t);
            std::memcpy(&edge.target.low, ptr, sizeof(uint64_t));
            ptr += sizeof(uint64_t);
            edge.type = static_cast<EdgeType>(*ptr++);
            ptr += 3;  // skip padding
            std::memcpy(&edge.weight, ptr, sizeof(float));
            ptr += sizeof(float);
            result.push_back(edge);
        }

        return result;
    }

    std::string base_path_;
    MappedRegion index_region_;
    MappedRegion vectors_region_;
    MappedRegion binary_region_;   // Binary vectors for fast first-pass (48 bytes each)
    MappedRegion meta_region_;
    ConnectionPool connections_;
    BlobStore payloads_;       // Variable-length payload storage
    BlobStore edges_;          // Variable-length edge storage
    SlotTagIndex tags_;            // Inverted index for tags (roaring bitmaps)

    mutable std::shared_mutex mutex_;
    std::unordered_map<NodeId, SlotId, NodeIdHash> id_to_slot_;
    size_t capacity_ = 0;
    size_t next_slot_ = 0;
    bool has_binary_ = false;  // Binary vectors available
};

} // namespace chitta
