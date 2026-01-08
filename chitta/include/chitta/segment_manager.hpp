#pragma once
// Segment Manager: Scalable multi-segment storage
//
// Divides the index into segments for:
// - Incremental compaction (one segment at a time)
// - Parallel operations (different segments)
// - Memory-efficient access (load only needed segments)
// - Better cache locality (nodes grouped by insertion time)
//
// Layout:
//   base_path.manifest      - Segment metadata and routing
//   base_path.seg0.unified  - Segment 0 index
//   base_path.seg0.vectors  - Segment 0 vectors
//   base_path.seg1.unified  - Segment 1 index
//   ...

#include "unified_index.hpp"
#include <map>
#include <memory>
#include <atomic>

namespace chitta {

// ═══════════════════════════════════════════════════════════════════════════
// Segment structures
// ═══════════════════════════════════════════════════════════════════════════

constexpr uint32_t MANIFEST_MAGIC = 0x5345474D;  // "SEGM"
constexpr uint32_t MANIFEST_VERSION = 1;
constexpr size_t DEFAULT_SEGMENT_CAPACITY = 100000;  // 100K nodes per segment

// Segment state
enum class SegmentState : uint8_t {
    Active = 0,     // Accepting new inserts
    Sealed = 1,     // Read-only, no more inserts
    Compacting = 2, // Being compacted
    Tombstone = 3   // Marked for deletion
};

// Per-segment metadata (64 bytes, no special alignment needed)
struct SegmentMeta {
    uint32_t segment_id;        // 4B  - Unique segment ID
    SegmentState state;         // 1B  - Current state
    uint8_t reserved1[3];       // 3B  - Padding
    uint64_t node_count;        // 8B  - Active nodes
    uint64_t deleted_count;     // 8B  - Deleted nodes
    uint64_t created_at;        // 8B  - Creation timestamp
    uint64_t sealed_at;         // 8B  - When sealed (0 if active)
    uint64_t min_hilbert;       // 8B  - Min Hilbert key
    uint64_t max_hilbert;       // 8B  - Max Hilbert key
    uint8_t reserved2[8];       // 8B  - Future use
};                              // Total: 64 bytes
static_assert(sizeof(SegmentMeta) == 64, "SegmentMeta must be 64 bytes");

// Manifest header (4KB)
struct alignas(4096) ManifestHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t segment_count;       // Total segments
    uint32_t active_segment_id;   // Current write target
    uint64_t total_nodes;         // Sum across all segments
    uint64_t next_segment_id;     // For creating new segments
    uint64_t checksum;
    uint8_t reserved[4056];
};
static_assert(sizeof(ManifestHeader) == 4096, "ManifestHeader must be 4KB");

// ═══════════════════════════════════════════════════════════════════════════
// Segment class - wrapper around UnifiedIndex
// ═══════════════════════════════════════════════════════════════════════════

class Segment {
public:
    Segment(uint32_t id, const std::string& base_path)
        : id_(id), base_path_(base_path) {}

    bool create(size_t capacity = DEFAULT_SEGMENT_CAPACITY) {
        return index_.create(segment_path(), capacity);
    }

    bool open() {
        return index_.open(segment_path());
    }

    void close() {
        index_.close();
    }

    bool valid() const { return index_.valid(); }

    // Forward to UnifiedIndex
    SlotId insert(const NodeId& id, const Node& node) {
        return index_.insert(id, node);
    }

    const IndexedNode* get(const NodeId& id) const {
        return index_.get(id);
    }

    SlotId lookup(const NodeId& id) const {
        return index_.lookup(id);
    }

    const QuantizedVector* vector(SlotId slot) const {
        return index_.vector(slot);
    }

    const NodeMeta* meta(SlotId slot) const {
        return index_.meta(slot);
    }

    std::vector<std::pair<SlotId, float>> search(
        const QuantizedVector& query, size_t k, size_t ef = 0) const
    {
        return index_.search(query, k, ef);
    }

    // Get IndexedNode by slot (for converting search results)
    const IndexedNode* get_slot(SlotId slot) const {
        return index_.get_slot(slot);
    }

    size_t count() const { return index_.count(); }
    size_t capacity() const { return index_.capacity(); }
    size_t deleted_count() const { return index_.deleted_count(); }

    void sync() { index_.sync(); }

    uint32_t id() const { return id_; }

    // Iterate over all nodes in this segment
    void for_each(std::function<void(const NodeId&, const Node&)> fn) const {
        index_.for_each(fn);
    }

    // Check if segment should be sealed (e.g., at capacity)
    bool should_seal() const {
        return count() >= capacity() * 0.9;  // 90% full
    }

    // Compaction priority: higher = more urgent
    // Based on deleted ratio and age
    float compaction_priority() const {
        if (count() == 0) return 0.0f;
        float delete_ratio = static_cast<float>(deleted_count()) /
                            static_cast<float>(count() + deleted_count());
        return delete_ratio;  // Simple: prioritize by deletion ratio
    }

private:
    std::string segment_path() const {
        return base_path_ + ".seg" + std::to_string(id_);
    }

    uint32_t id_;
    std::string base_path_;
    UnifiedIndex index_;
};

// ═══════════════════════════════════════════════════════════════════════════
// Segment Manager
// ═══════════════════════════════════════════════════════════════════════════

class SegmentManager {
public:
    explicit SegmentManager(const std::string& base_path)
        : base_path_(base_path) {}

    ~SegmentManager() { close(); }

    // ═══════════════════════════════════════════════════════════════════════
    // Lifecycle
    // ═══════════════════════════════════════════════════════════════════════

    bool create() {
        // Create manifest file
        std::string manifest_path = base_path_ + ".manifest";
        if (!manifest_region_.create(manifest_path, sizeof(ManifestHeader))) {
            return false;
        }

        auto* header = manifest_region_.as<ManifestHeader>();
        header->magic = MANIFEST_MAGIC;
        header->version = MANIFEST_VERSION;
        header->segment_count = 0;
        header->active_segment_id = 0;
        header->total_nodes = 0;
        header->next_segment_id = 0;

        // Create first segment
        if (!create_segment()) {
            return false;
        }

        std::cerr << "[SegmentManager] Created at " << base_path_ << "\n";
        return true;
    }

    bool open() {
        std::string manifest_path = base_path_ + ".manifest";
        if (!manifest_region_.open(manifest_path, false)) {
            return false;
        }

        auto* header = manifest_region_.as<ManifestHeader>();
        if (header->magic != MANIFEST_MAGIC) {
            std::cerr << "[SegmentManager] Invalid manifest magic\n";
            return false;
        }

        // Open all segments
        for (uint32_t i = 0; i < header->next_segment_id; ++i) {
            auto seg = std::make_unique<Segment>(i, base_path_);
            if (seg->open()) {
                segments_[i] = std::move(seg);
            }
        }

        std::cerr << "[SegmentManager] Opened " << segments_.size()
                  << " segments, " << total_nodes() << " nodes\n";
        return true;
    }

    void close() {
        for (auto& [id, seg] : segments_) {
            seg->close();
        }
        segments_.clear();
        manifest_region_.close();
    }

    void sync() {
        for (auto& [id, seg] : segments_) {
            seg->sync();
        }
        manifest_region_.sync();
    }

    bool valid() const {
        return manifest_region_.valid() && !segments_.empty();
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Operations
    // ═══════════════════════════════════════════════════════════════════════

    // Insert into active segment
    SlotId insert(const NodeId& id, const Node& node) {
        auto* active = active_segment();
        if (!active) return SlotId::invalid();

        // Check if need to roll to new segment
        if (active->should_seal()) {
            seal_active_segment();
            if (!create_segment()) {
                return SlotId::invalid();
            }
            active = active_segment();
        }

        auto slot = active->insert(id, node);
        if (slot.valid()) {
            // Track in routing table
            routing_[id] = active->id();

            auto* header = manifest_region_.as<ManifestHeader>();
            header->total_nodes++;
        }

        return slot;
    }

    // Lookup which segment contains a node
    Segment* find_segment(const NodeId& id) {
        auto it = routing_.find(id);
        if (it != routing_.end()) {
            auto seg_it = segments_.find(it->second);
            if (seg_it != segments_.end()) {
                return seg_it->second.get();
            }
        }

        // Fallback: search all segments
        for (auto& [seg_id, seg] : segments_) {
            if (seg->lookup(id).valid()) {
                routing_[id] = seg_id;  // Cache for future
                return seg.get();
            }
        }

        return nullptr;
    }

    // Get node by ID
    const IndexedNode* get(const NodeId& id) {
        auto* seg = find_segment(id);
        return seg ? seg->get(id) : nullptr;
    }

    // Search across all segments
    std::vector<std::pair<NodeId, float>> search(
        const QuantizedVector& query, size_t k, size_t ef = 0) const
    {
        // Search each segment
        std::vector<std::pair<NodeId, float>> all_results;

        for (const auto& [seg_id, seg] : segments_) {
            auto seg_results = seg->search(query, k, ef);

            // Convert SlotId to NodeId and add to results
            for (const auto& [slot, dist] : seg_results) {
                auto* indexed = seg->get_slot(slot);
                if (indexed) {
                    all_results.emplace_back(indexed->id, 1.0f - dist);
                }
            }
        }

        // Sort by similarity (descending) and take top k
        std::partial_sort(
            all_results.begin(),
            all_results.begin() + std::min(k, all_results.size()),
            all_results.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; }
        );

        if (all_results.size() > k) {
            all_results.resize(k);
        }

        return all_results;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Segment management
    // ═══════════════════════════════════════════════════════════════════════

    // Create new segment
    bool create_segment() {
        auto* header = manifest_region_.as<ManifestHeader>();
        uint32_t new_id = header->next_segment_id++;

        auto seg = std::make_unique<Segment>(new_id, base_path_);
        if (!seg->create()) {
            return false;
        }

        header->segment_count++;
        header->active_segment_id = new_id;
        segments_[new_id] = std::move(seg);

        std::cerr << "[SegmentManager] Created segment " << new_id << "\n";
        return true;
    }

    // Seal active segment (make read-only)
    void seal_active_segment() {
        auto* active = active_segment();
        if (active) {
            active->sync();
            std::cerr << "[SegmentManager] Sealed segment " << active->id()
                      << " with " << active->count() << " nodes\n";
        }
    }

    // Get segment with highest compaction priority
    Segment* segment_for_compaction() {
        Segment* best = nullptr;
        float best_priority = 0.0f;

        for (auto& [id, seg] : segments_) {
            float priority = seg->compaction_priority();
            if (priority > best_priority && priority > 0.3f) {  // >30% deleted
                best = seg.get();
                best_priority = priority;
            }
        }

        return best;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Statistics
    // ═══════════════════════════════════════════════════════════════════════

    size_t segment_count() const { return segments_.size(); }

    size_t total_nodes() const {
        size_t total = 0;
        for (const auto& [id, seg] : segments_) {
            total += seg->count();
        }
        return total;
    }

    // Iterate over all nodes across all segments
    void for_each(std::function<void(const NodeId&, const Node&)> fn) const {
        for (const auto& [seg_id, seg] : segments_) {
            seg->for_each(fn);
        }
    }

    Segment* active_segment() {
        auto* header = manifest_region_.as<ManifestHeader>();
        auto it = segments_.find(header->active_segment_id);
        return (it != segments_.end()) ? it->second.get() : nullptr;
    }

private:
    std::string base_path_;
    MappedRegion manifest_region_;
    std::map<uint32_t, std::unique_ptr<Segment>> segments_;
    std::unordered_map<NodeId, uint32_t, NodeIdHash> routing_;  // NodeId -> segment_id
};

} // namespace chitta
