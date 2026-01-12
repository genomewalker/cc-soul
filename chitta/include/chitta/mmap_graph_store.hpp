#pragma once
// MmapGraphStore: Memory-mapped graph storage for 100M+ scale
//
// Design improvements over GraphStore:
// - Triplets stored in mmap'd file (not in-memory vector)
// - Object index stores indices into triplet array (not duplicate triplets)
// - Streaming builder for batch loading without O(N) RAM spike
// - Persistent dictionary with string table
//
// Memory at 100M triplets:
// - Triplets: 100M * 16B = 1.6GB (mmap'd, paged by OS)
// - Object index: 100M * 4B = 400MB (indices only)
// - CSR offsets: 2 * 100M * 8B = 1.6GB worst case
// - Total active: ~2GB vs ~4GB+ with duplication

#include "types.hpp"
#include "mmap.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <shared_mutex>
#include <mutex>
#include <string_view>

namespace chitta {

// Compact triplet representation (16 bytes)
struct MmapTriplet {
    uint32_t subject;
    uint32_t object;
    uint32_t predicate;
    float weight;
};

static_assert(sizeof(MmapTriplet) == 16, "MmapTriplet must be 16 bytes");

// File format constants
constexpr uint32_t MMAP_GRAPH_MAGIC = 0x4D475248;  // "MGRH"
constexpr uint32_t MMAP_GRAPH_VERSION = 2;

// File header (page-aligned)
struct alignas(4096) MmapGraphHeader {
    uint32_t magic;
    uint32_t version;
    uint64_t entity_count;
    uint64_t predicate_count;
    uint64_t triplet_count;
    uint64_t string_table_size;
    uint64_t checksum;
    uint8_t reserved[4048];
};

static_assert(sizeof(MmapGraphHeader) == 4096, "Header must be 4KB");

// String table: packed strings with null terminators
// Layout: [str0\0str1\0str2\0...]
// Index: [offset0, offset1, offset2, ...]
class StringTable {
public:
    void add(const std::string& s) {
        offsets_.push_back(data_.size());
        data_.insert(data_.end(), s.begin(), s.end());
        data_.push_back('\0');
    }

    std::string_view get(uint32_t idx) const {
        if (idx >= offsets_.size()) return {};
        size_t start = offsets_[idx];
        size_t end = (idx + 1 < offsets_.size()) ? offsets_[idx + 1] - 1 : data_.size() - 1;
        return std::string_view(data_.data() + start, end - start);
    }

    size_t size() const { return offsets_.size(); }

    size_t data_size() const { return data_.size(); }
    const char* data_ptr() const { return data_.data(); }
    const uint64_t* offsets_ptr() const { return offsets_.data(); }

    void load_from_mmap(const char* data, size_t data_size,
                        const uint64_t* offsets, size_t count) {
        data_.assign(data, data + data_size);
        offsets_.assign(offsets, offsets + count);
    }

    void clear() {
        data_.clear();
        offsets_.clear();
    }

private:
    std::vector<char> data_;
    std::vector<uint64_t> offsets_;
};

// Mmap-backed graph store
class MmapGraphStore {
public:
    MmapGraphStore() = default;
    ~MmapGraphStore() { close(); }

    // Create new graph store
    bool create(const std::string& base_path, size_t initial_capacity = 1000000) {
        base_path_ = base_path;

        // Create header file
        std::string header_path = base_path + ".graph";
        if (!header_region_.create(header_path, sizeof(MmapGraphHeader))) {
            return false;
        }

        auto* header = header_region_.as<MmapGraphHeader>();
        header->magic = MMAP_GRAPH_MAGIC;
        header->version = MMAP_GRAPH_VERSION;
        header->entity_count = 0;
        header->predicate_count = 0;
        header->triplet_count = 0;
        header->string_table_size = 0;

        // Create triplets file
        std::string triplets_path = base_path + ".triplets";
        if (!triplets_region_.create(triplets_path, initial_capacity * sizeof(MmapTriplet))) {
            return false;
        }

        // Create CSR offsets files
        std::string subj_path = base_path + ".subj_csr";
        std::string obj_path = base_path + ".obj_csr";
        if (!subject_csr_.create(subj_path, (initial_capacity + 1) * sizeof(uint64_t))) {
            return false;
        }
        if (!object_csr_.create(obj_path, (initial_capacity + 1) * sizeof(uint64_t))) {
            return false;
        }

        // Create object index (indices into triplet array, sorted by object)
        std::string obj_idx_path = base_path + ".obj_idx";
        if (!object_indices_.create(obj_idx_path, initial_capacity * sizeof(uint32_t))) {
            return false;
        }

        capacity_ = initial_capacity;
        return true;
    }

    // Open existing graph store
    bool open(const std::string& base_path) {
        base_path_ = base_path;

        std::string header_path = base_path + ".graph";
        if (!header_region_.open(header_path)) {
            return false;
        }

        auto* header = header_region_.as<const MmapGraphHeader>();
        if (header->magic != MMAP_GRAPH_MAGIC || header->version != MMAP_GRAPH_VERSION) {
            return false;
        }

        // Open data files
        if (!triplets_region_.open(base_path + ".triplets")) return false;
        if (!subject_csr_.open(base_path + ".subj_csr")) return false;
        if (!object_csr_.open(base_path + ".obj_csr")) return false;
        if (!object_indices_.open(base_path + ".obj_idx")) return false;

        // Load string tables
        if (!load_string_tables()) return false;

        // Build lookup maps from string tables
        rebuild_lookup_maps();

        triplet_count_ = header->triplet_count;
        capacity_ = triplets_region_.size() / sizeof(MmapTriplet);

        return true;
    }

    void close() {
        sync();
        header_region_.close();
        triplets_region_.close();
        subject_csr_.close();
        object_csr_.close();
        object_indices_.close();
    }

    void sync() {
        header_region_.sync();
        triplets_region_.sync();
        subject_csr_.sync();
        object_csr_.sync();
        object_indices_.sync();
        save_string_tables();
    }

    // Add triplet (for incremental updates)
    bool add(const std::string& subject, const std::string& predicate,
             const std::string& object, float weight = 1.0f) {
        std::unique_lock lock(mutex_);

        // Get or create indices
        uint32_t subj_idx = get_or_create_entity(subject);
        uint32_t pred_idx = get_or_create_predicate(predicate);
        uint32_t obj_idx = get_or_create_entity(object);

        // Ensure capacity
        if (triplet_count_ >= capacity_) {
            if (!grow_triplets()) return false;
        }

        // Add triplet
        auto* triplets = triplets_region_.as<MmapTriplet>();
        triplets[triplet_count_] = MmapTriplet{subj_idx, obj_idx, pred_idx, weight};
        triplet_count_++;

        // Update header
        auto* header = header_region_.as<MmapGraphHeader>();
        header->triplet_count = triplet_count_;

        indices_dirty_ = true;
        return true;
    }

    // Bulk add (more efficient for batch loading)
    size_t add_batch(const std::vector<std::tuple<std::string, std::string, std::string, float>>& triplets) {
        std::unique_lock lock(mutex_);

        size_t added = 0;
        for (const auto& [subj, pred, obj, weight] : triplets) {
            uint32_t subj_idx = get_or_create_entity(subj);
            uint32_t pred_idx = get_or_create_predicate(pred);
            uint32_t obj_idx = get_or_create_entity(obj);

            if (triplet_count_ >= capacity_) {
                if (!grow_triplets()) break;
            }

            auto* t = triplets_region_.as<MmapTriplet>();
            t[triplet_count_++] = MmapTriplet{subj_idx, obj_idx, pred_idx, weight};
            added++;
        }

        auto* header = header_region_.as<MmapGraphHeader>();
        header->triplet_count = triplet_count_;
        indices_dirty_ = true;

        return added;
    }

    // Build CSR indices (call after bulk loading)
    void build_indices() {
        std::unique_lock lock(mutex_);
        if (!indices_dirty_ || triplet_count_ == 0) return;

        auto* triplets = triplets_region_.as<MmapTriplet>();

        // Create temporary arrays for sorting
        std::vector<uint32_t> subj_order(triplet_count_);
        std::vector<uint32_t> obj_order(triplet_count_);
        for (uint32_t i = 0; i < triplet_count_; ++i) {
            subj_order[i] = i;
            obj_order[i] = i;
        }

        // Sort by subject
        std::sort(subj_order.begin(), subj_order.end(),
            [triplets](uint32_t a, uint32_t b) {
                return triplets[a].subject < triplets[b].subject;
            });

        // Sort by object
        std::sort(obj_order.begin(), obj_order.end(),
            [triplets](uint32_t a, uint32_t b) {
                return triplets[a].object < triplets[b].object;
            });

        // Reorder triplets by subject (in-place via temp buffer if needed)
        std::vector<MmapTriplet> temp(triplet_count_);
        for (size_t i = 0; i < triplet_count_; ++i) {
            temp[i] = triplets[subj_order[i]];
        }
        std::memcpy(triplets, temp.data(), triplet_count_ * sizeof(MmapTriplet));

        // Store object indices (now pointing to subject-sorted array)
        // We need to rebuild obj_order after reordering
        for (uint32_t i = 0; i < triplet_count_; ++i) {
            obj_order[i] = i;
        }
        std::sort(obj_order.begin(), obj_order.end(),
            [triplets](uint32_t a, uint32_t b) {
                return triplets[a].object < triplets[b].object;
            });

        auto* obj_indices = object_indices_.as<uint32_t>();
        std::memcpy(obj_indices, obj_order.data(), triplet_count_ * sizeof(uint32_t));

        // Build subject CSR offsets
        size_t entity_count = entities_.size();
        auto* subj_csr = subject_csr_.as<uint64_t>();
        std::memset(subj_csr, 0, (entity_count + 1) * sizeof(uint64_t));

        for (size_t i = 0; i < triplet_count_; ++i) {
            uint32_t subj = triplets[i].subject;
            if (subj < entity_count) subj_csr[subj + 1]++;
        }
        for (size_t i = 1; i <= entity_count; ++i) {
            subj_csr[i] += subj_csr[i - 1];
        }

        // Build object CSR offsets
        auto* obj_csr = object_csr_.as<uint64_t>();
        std::memset(obj_csr, 0, (entity_count + 1) * sizeof(uint64_t));

        for (size_t i = 0; i < triplet_count_; ++i) {
            uint32_t obj = triplets[obj_indices[i]].object;
            if (obj < entity_count) obj_csr[obj + 1]++;
        }
        for (size_t i = 1; i <= entity_count; ++i) {
            obj_csr[i] += obj_csr[i - 1];
        }

        indices_dirty_ = false;
    }

    // Query by subject
    std::vector<std::tuple<std::string, std::string, float>>
    query_subject(const std::string& subject) const {
        std::shared_lock lock(mutex_);

        auto it = entity_to_idx_.find(subject);
        if (it == entity_to_idx_.end()) return {};

        uint32_t subj_idx = it->second;
        auto* triplets = triplets_region_.as<const MmapTriplet>();
        auto* subj_csr = subject_csr_.as<const uint64_t>();

        uint64_t start = subj_csr[subj_idx];
        uint64_t end = subj_csr[subj_idx + 1];

        std::vector<std::tuple<std::string, std::string, float>> results;
        results.reserve(end - start);

        for (uint64_t i = start; i < end && i < triplet_count_; ++i) {
            const auto& t = triplets[i];
            results.emplace_back(
                std::string(predicates_.get(t.predicate)),
                std::string(entities_.get(t.object)),
                t.weight
            );
        }

        return results;
    }

    // Query by object
    std::vector<std::tuple<std::string, std::string, float>>
    query_object(const std::string& object) const {
        std::shared_lock lock(mutex_);

        auto it = entity_to_idx_.find(object);
        if (it == entity_to_idx_.end()) return {};

        uint32_t obj_idx = it->second;
        auto* triplets = triplets_region_.as<const MmapTriplet>();
        auto* obj_csr = object_csr_.as<const uint64_t>();
        auto* obj_indices = object_indices_.as<const uint32_t>();

        uint64_t start = obj_csr[obj_idx];
        uint64_t end = obj_csr[obj_idx + 1];

        std::vector<std::tuple<std::string, std::string, float>> results;
        results.reserve(end - start);

        for (uint64_t i = start; i < end && i < triplet_count_; ++i) {
            uint32_t triplet_idx = obj_indices[i];
            if (triplet_idx >= triplet_count_) continue;
            const auto& t = triplets[triplet_idx];
            results.emplace_back(
                std::string(entities_.get(t.subject)),
                std::string(predicates_.get(t.predicate)),
                t.weight
            );
        }

        return results;
    }

    // Query by predicate (linear scan - consider predicate index if frequent)
    std::vector<std::tuple<std::string, std::string, float>>
    query_predicate(const std::string& predicate) const {
        std::shared_lock lock(mutex_);

        auto it = predicate_to_idx_.find(predicate);
        if (it == predicate_to_idx_.end()) return {};

        uint32_t pred_idx = it->second;
        auto* triplets = triplets_region_.as<const MmapTriplet>();

        std::vector<std::tuple<std::string, std::string, float>> results;

        for (size_t i = 0; i < triplet_count_; ++i) {
            const auto& t = triplets[i];
            if (t.predicate == pred_idx) {
                results.emplace_back(
                    std::string(entities_.get(t.subject)),
                    std::string(entities_.get(t.object)),
                    t.weight
                );
            }
        }

        return results;
    }

    // General query
    std::vector<std::tuple<std::string, std::string, std::string, float>>
    query(const std::string& subject = "", const std::string& predicate = "",
          const std::string& object = "") const {
        std::shared_lock lock(mutex_);

        int64_t subj_idx = -1, pred_idx = -1, obj_idx = -1;

        if (!subject.empty()) {
            auto it = entity_to_idx_.find(subject);
            if (it == entity_to_idx_.end()) return {};
            subj_idx = it->second;
        }
        if (!predicate.empty()) {
            auto it = predicate_to_idx_.find(predicate);
            if (it == predicate_to_idx_.end()) return {};
            pred_idx = it->second;
        }
        if (!object.empty()) {
            auto it = entity_to_idx_.find(object);
            if (it == entity_to_idx_.end()) return {};
            obj_idx = it->second;
        }

        auto* triplets = triplets_region_.as<const MmapTriplet>();
        std::vector<std::tuple<std::string, std::string, std::string, float>> results;

        // Optimize: use CSR if only subject or object specified
        if (subj_idx >= 0 && pred_idx < 0 && obj_idx < 0) {
            auto* subj_csr = subject_csr_.as<const uint64_t>();
            uint64_t start = subj_csr[subj_idx];
            uint64_t end = subj_csr[subj_idx + 1];
            for (uint64_t i = start; i < end && i < triplet_count_; ++i) {
                const auto& t = triplets[i];
                results.emplace_back(
                    std::string(entities_.get(t.subject)),
                    std::string(predicates_.get(t.predicate)),
                    std::string(entities_.get(t.object)),
                    t.weight
                );
            }
            return results;
        }

        // Full scan for complex queries
        for (size_t i = 0; i < triplet_count_; ++i) {
            const auto& t = triplets[i];
            if (subj_idx >= 0 && t.subject != static_cast<uint32_t>(subj_idx)) continue;
            if (pred_idx >= 0 && t.predicate != static_cast<uint32_t>(pred_idx)) continue;
            if (obj_idx >= 0 && t.object != static_cast<uint32_t>(obj_idx)) continue;

            results.emplace_back(
                std::string(entities_.get(t.subject)),
                std::string(predicates_.get(t.predicate)),
                std::string(entities_.get(t.object)),
                t.weight
            );
        }

        return results;
    }

    // Stats
    size_t entity_count() const { return entities_.size(); }
    size_t predicate_count() const { return predicates_.size(); }
    size_t triplet_count() const { return triplet_count_; }

    // Memory estimate (active RAM, not mmap'd)
    size_t memory_bytes() const {
        size_t bytes = 0;
        bytes += entity_to_idx_.size() * 64;  // hash map overhead
        bytes += predicate_to_idx_.size() * 48;
        bytes += entities_.data_size();
        bytes += predicates_.data_size();
        return bytes;
    }

private:
    uint32_t get_or_create_entity(const std::string& s) {
        auto it = entity_to_idx_.find(s);
        if (it != entity_to_idx_.end()) return it->second;

        uint32_t idx = static_cast<uint32_t>(entities_.size());
        entities_.add(s);
        entity_to_idx_[s] = idx;

        auto* header = header_region_.as<MmapGraphHeader>();
        header->entity_count = entities_.size();

        return idx;
    }

    uint32_t get_or_create_predicate(const std::string& s) {
        auto it = predicate_to_idx_.find(s);
        if (it != predicate_to_idx_.end()) return it->second;

        uint32_t idx = static_cast<uint32_t>(predicates_.size());
        predicates_.add(s);
        predicate_to_idx_[s] = idx;

        auto* header = header_region_.as<MmapGraphHeader>();
        header->predicate_count = predicates_.size();

        return idx;
    }

    bool grow_triplets() {
        size_t new_capacity = capacity_ * 2;
        if (!triplets_region_.resize(new_capacity * sizeof(MmapTriplet))) return false;
        if (!object_indices_.resize(new_capacity * sizeof(uint32_t))) return false;
        capacity_ = new_capacity;
        return true;
    }

    void rebuild_lookup_maps() {
        entity_to_idx_.clear();
        predicate_to_idx_.clear();

        for (size_t i = 0; i < entities_.size(); ++i) {
            entity_to_idx_[std::string(entities_.get(i))] = i;
        }
        for (size_t i = 0; i < predicates_.size(); ++i) {
            predicate_to_idx_[std::string(predicates_.get(i))] = i;
        }
    }

    bool load_string_tables() {
        std::string entity_path = base_path_ + ".entities";
        std::string pred_path = base_path_ + ".predicates";

        // Load entities
        std::ifstream ent_in(entity_path, std::ios::binary);
        if (ent_in) {
            uint64_t count, data_size;
            ent_in.read(reinterpret_cast<char*>(&count), sizeof(count));
            ent_in.read(reinterpret_cast<char*>(&data_size), sizeof(data_size));

            std::vector<uint64_t> offsets(count);
            std::vector<char> data(data_size);
            ent_in.read(reinterpret_cast<char*>(offsets.data()), count * sizeof(uint64_t));
            ent_in.read(data.data(), data_size);

            entities_.load_from_mmap(data.data(), data_size, offsets.data(), count);
        }

        // Load predicates
        std::ifstream pred_in(pred_path, std::ios::binary);
        if (pred_in) {
            uint64_t count, data_size;
            pred_in.read(reinterpret_cast<char*>(&count), sizeof(count));
            pred_in.read(reinterpret_cast<char*>(&data_size), sizeof(data_size));

            std::vector<uint64_t> offsets(count);
            std::vector<char> data(data_size);
            pred_in.read(reinterpret_cast<char*>(offsets.data()), count * sizeof(uint64_t));
            pred_in.read(data.data(), data_size);

            predicates_.load_from_mmap(data.data(), data_size, offsets.data(), count);
        }

        return true;
    }

    void save_string_tables() {
        // Save entities
        std::string entity_path = base_path_ + ".entities";
        std::ofstream ent_out(entity_path, std::ios::binary);
        if (ent_out) {
            uint64_t count = entities_.size();
            uint64_t data_size = entities_.data_size();
            ent_out.write(reinterpret_cast<const char*>(&count), sizeof(count));
            ent_out.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));
            ent_out.write(reinterpret_cast<const char*>(entities_.offsets_ptr()), count * sizeof(uint64_t));
            ent_out.write(entities_.data_ptr(), data_size);
        }

        // Save predicates
        std::string pred_path = base_path_ + ".predicates";
        std::ofstream pred_out(pred_path, std::ios::binary);
        if (pred_out) {
            uint64_t count = predicates_.size();
            uint64_t data_size = predicates_.data_size();
            pred_out.write(reinterpret_cast<const char*>(&count), sizeof(count));
            pred_out.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));
            pred_out.write(reinterpret_cast<const char*>(predicates_.offsets_ptr()), count * sizeof(uint64_t));
            pred_out.write(predicates_.data_ptr(), data_size);
        }
    }

    mutable std::shared_mutex mutex_;
    std::string base_path_;

    // Mmap'd regions
    MappedRegion header_region_;
    MappedRegion triplets_region_;
    MappedRegion subject_csr_;
    MappedRegion object_csr_;
    MappedRegion object_indices_;  // Indices into triplet array, sorted by object

    // String tables (loaded into RAM for fast lookup)
    StringTable entities_;
    StringTable predicates_;

    // Reverse lookup (string → index)
    std::unordered_map<std::string, uint32_t> entity_to_idx_;
    std::unordered_map<std::string, uint32_t> predicate_to_idx_;

    size_t triplet_count_ = 0;
    size_t capacity_ = 0;
    bool indices_dirty_ = false;
};

} // namespace chitta
