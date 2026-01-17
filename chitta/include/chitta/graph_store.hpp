#pragma once
// GraphStore: Dictionary-encoded graph storage for 100M+ scale
//
// Design:
// - Entity dictionary: string → uint32 index
// - Predicate dictionary: string → uint16 index
// - Triplets: compact (subj_idx, pred_idx, obj_idx, weight)
// - CSR index: O(1) traversal by subject or object
//
// Storage format (graph.bin):
// ┌─────────────────────────────────────────┐
// │ Header: magic, version, counts          │
// │ Entity dictionary: [idx → string]       │
// │ Predicate dictionary: [idx → string]    │
// │ Triplets: packed (s, p, o, w)           │
// │ Subject offsets: CSR index              │
// │ Object offsets: reverse CSR index       │
// └─────────────────────────────────────────┘

#include "types.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <shared_mutex>

namespace chitta {

// Compact triplet representation (16 bytes)
// Field order: 4-4-4-4 = 16 bytes with no padding
struct CompactTriplet {
    uint32_t subject;     // Entity index (4 bytes) → 4B entities
    uint32_t object;      // Entity index (4 bytes) → 4B entities
    uint32_t predicate;   // Predicate index (4 bytes) → 4B predicates
    float weight;         // Full float precision (4 bytes)

};

static_assert(sizeof(CompactTriplet) == 16, "CompactTriplet must be 16 bytes");

// File format constants
constexpr uint32_t GRAPH_MAGIC = 0x47525048;  // "GRPH"
constexpr uint32_t GRAPH_VERSION = 2;  // v2: added EntityIndex

// File header
struct GraphHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t entity_count;
    uint32_t predicate_count;
    uint64_t triplet_count;
    uint64_t entity_dict_offset;
    uint64_t predicate_dict_offset;
    uint64_t triplets_offset;
    uint64_t subject_index_offset;
    uint64_t object_index_offset;
    uint64_t entity_index_offset;  // v2: EntityIndex (string → NodeId)
    uint64_t reserved[3];
};

static_assert(sizeof(GraphHeader) == 96, "GraphHeader should be 96 bytes");

// Dictionary: bidirectional string ↔ index mapping
class Dictionary {
public:
    // Get or create index for string
    uint32_t get_or_create(const std::string& s) {
        auto it = str_to_idx_.find(s);
        if (it != str_to_idx_.end()) return it->second;

        uint32_t idx = static_cast<uint32_t>(idx_to_str_.size());
        idx_to_str_.push_back(s);
        str_to_idx_[s] = idx;
        return idx;
    }

    // Get index (returns -1 if not found)
    int64_t get(const std::string& s) const {
        auto it = str_to_idx_.find(s);
        return it != str_to_idx_.end() ? it->second : -1;
    }

    // Get string by index
    const std::string& get(uint32_t idx) const {
        return idx_to_str_[idx];
    }

    size_t size() const { return idx_to_str_.size(); }
    bool empty() const { return idx_to_str_.empty(); }

    // Serialization
    void save(std::ostream& out) const {
        uint32_t count = static_cast<uint32_t>(idx_to_str_.size());
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));

        for (const auto& s : idx_to_str_) {
            uint32_t len = static_cast<uint32_t>(s.size());
            out.write(reinterpret_cast<const char*>(&len), sizeof(len));
            out.write(s.data(), len);
        }
    }

    bool load(std::istream& in) {
        uint32_t count;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));
        if (!in) return false;

        idx_to_str_.clear();
        str_to_idx_.clear();
        idx_to_str_.reserve(count);

        for (uint32_t i = 0; i < count; ++i) {
            uint32_t len;
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            if (!in || len > 10000) return false;

            std::string s(len, '\0');
            in.read(s.data(), len);
            if (!in) return false;

            str_to_idx_[s] = i;
            idx_to_str_.push_back(std::move(s));
        }
        return true;
    }

    void clear() {
        idx_to_str_.clear();
        str_to_idx_.clear();
    }

private:
    std::vector<std::string> idx_to_str_;
    std::unordered_map<std::string, uint32_t> str_to_idx_;
};

// CSR (Compressed Sparse Row) index for O(1) adjacency lookup
class CSRIndex {
public:
    // Build index from sorted triplets
    void build(const std::vector<CompactTriplet>& triplets,
               uint32_t num_entities, bool by_object = false) {
        offsets_.resize(num_entities + 1, 0);

        // Count edges per entity
        for (const auto& t : triplets) {
            uint32_t key = by_object ? t.object : t.subject;
            if (key < num_entities) offsets_[key + 1]++;
        }

        // Prefix sum to get offsets
        for (size_t i = 1; i <= num_entities; ++i) {
            offsets_[i] += offsets_[i - 1];
        }
    }

    // Get range of triplets for entity
    std::pair<uint64_t, uint64_t> range(uint32_t entity_idx) const {
        if (entity_idx >= offsets_.size() - 1) return {0, 0};
        return {offsets_[entity_idx], offsets_[entity_idx + 1]};
    }

    size_t size() const { return offsets_.empty() ? 0 : offsets_.size() - 1; }

    // Serialization
    void save(std::ostream& out) const {
        uint64_t count = offsets_.size();
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));
        out.write(reinterpret_cast<const char*>(offsets_.data()),
                  offsets_.size() * sizeof(uint64_t));
    }

    bool load(std::istream& in, uint64_t expected_count) {
        uint64_t count;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));
        if (!in || count != expected_count) return false;

        offsets_.resize(count);
        in.read(reinterpret_cast<char*>(offsets_.data()),
                count * sizeof(uint64_t));
        return in.good();
    }

    void clear() { offsets_.clear(); }

private:
    std::vector<uint64_t> offsets_;
};

// EntityIndex: Links triplet entity indices to soul NodeIds
// Uses dictionary indices (uint32_t) instead of strings for 100M+ scale:
//   entity_idx → NodeId (compact, O(1))
//   node_id → [entity_idx...] (reverse lookup)
// Strings only stored once in the parent Dictionary.
class EntityIndex {
public:
    // Link an entity index to a NodeId
    void link(uint32_t entity_idx, NodeId node_id) {
        idx_to_node_[entity_idx] = node_id;
        node_to_indices_[node_id].push_back(entity_idx);
    }

    // Resolve entity index to NodeId
    std::optional<NodeId> resolve(uint32_t entity_idx) const {
        auto it = idx_to_node_.find(entity_idx);
        if (it != idx_to_node_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    // Get all entity indices linked to a NodeId
    std::vector<uint32_t> indices_for_node(NodeId node_id) const {
        auto it = node_to_indices_.find(node_id);
        if (it != node_to_indices_.end()) {
            return it->second;
        }
        return {};
    }

    // Check if entity index is linked
    bool has(uint32_t entity_idx) const {
        return idx_to_node_.find(entity_idx) != idx_to_node_.end();
    }

    // Remove a link
    void unlink(uint32_t entity_idx) {
        auto it = idx_to_node_.find(entity_idx);
        if (it != idx_to_node_.end()) {
            NodeId node_id = it->second;
            idx_to_node_.erase(it);
            auto& indices = node_to_indices_[node_id];
            indices.erase(std::remove(indices.begin(), indices.end(), entity_idx), indices.end());
            if (indices.empty()) {
                node_to_indices_.erase(node_id);
            }
        }
    }

    // Get all linked entity indices
    std::vector<std::pair<uint32_t, NodeId>> all() const {
        std::vector<std::pair<uint32_t, NodeId>> result;
        result.reserve(idx_to_node_.size());
        for (const auto& [idx, node_id] : idx_to_node_) {
            result.emplace_back(idx, node_id);
        }
        return result;
    }

    size_t size() const { return idx_to_node_.size(); }
    bool empty() const { return idx_to_node_.empty(); }
    void clear() { idx_to_node_.clear(); node_to_indices_.clear(); }

    // Serialization (compact: pairs of uint32_t + NodeId)
    void save(std::ostream& out) const {
        uint32_t count = static_cast<uint32_t>(idx_to_node_.size());
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));

        for (const auto& [entity_idx, node_id] : idx_to_node_) {
            out.write(reinterpret_cast<const char*>(&entity_idx), sizeof(entity_idx));
            out.write(reinterpret_cast<const char*>(&node_id), sizeof(node_id));
        }
    }

    bool load(std::istream& in) {
        clear();

        uint32_t count;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));
        if (!in) return false;

        for (uint32_t i = 0; i < count; ++i) {
            uint32_t entity_idx;
            in.read(reinterpret_cast<char*>(&entity_idx), sizeof(entity_idx));
            if (!in) return false;

            NodeId node_id;
            in.read(reinterpret_cast<char*>(&node_id), sizeof(node_id));
            if (!in) return false;

            link(entity_idx, node_id);
        }
        return true;
    }

private:
    std::unordered_map<uint32_t, NodeId> idx_to_node_;
    std::unordered_map<NodeId, std::vector<uint32_t>, NodeIdHash> node_to_indices_;
};

// Main graph store
class GraphStore {
public:
    GraphStore() = default;

    // Add a triplet (thread-safe, deduplicates)
    void add(const std::string& subject, const std::string& predicate,
             const std::string& object, float weight = 1.0f) {
        std::unique_lock lock(mutex_);

        CompactTriplet t;
        t.subject = entities_.get_or_create(subject);
        t.predicate = static_cast<uint32_t>(predicates_.get_or_create(predicate));
        t.object = entities_.get_or_create(object);
        t.weight = weight;

        // Check for duplicate triplet
        for (const auto& existing : triplets_) {
            if (existing.subject == t.subject &&
                existing.predicate == t.predicate &&
                existing.object == t.object) {
                return;  // Already exists, skip
            }
        }

        triplets_.push_back(t);
        dirty_ = true;

        // Append to WAL for crash recovery
        if (wal_stream_.is_open()) {
            wal_stream_.write(reinterpret_cast<const char*>(&t), sizeof(t));
            wal_stream_.flush();
        }
    }

    // Batch add with incremental global dedupe
    // Uses persistent hash set built lazily, O(batch_size) per call after first build
    void add_batch(const std::vector<std::tuple<std::string, std::string, std::string, float>>& triplets) {
        if (triplets.empty()) return;

        std::unique_lock lock(mutex_);

        // Build global hash set on first batch call (lazy initialization)
        // After that, it's maintained incrementally - O(1) per insert
        if (triplet_hashes_.empty() && !triplets_.empty()) {
            triplet_hashes_.reserve(triplets_.size() * 2);
            for (const auto& t : triplets_) {
                triplet_hashes_.insert(make_triplet_key(t.subject, t.predicate, t.object));
            }
        }

        std::vector<CompactTriplet> new_triplets;
        new_triplets.reserve(triplets.size());

        for (const auto& [subject, predicate, object, weight] : triplets) {
            uint32_t subj_idx = entities_.get_or_create(subject);
            uint32_t pred_idx = static_cast<uint32_t>(predicates_.get_or_create(predicate));
            uint32_t obj_idx = entities_.get_or_create(object);

            uint64_t key = make_triplet_key(subj_idx, pred_idx, obj_idx);
            if (triplet_hashes_.find(key) != triplet_hashes_.end()) continue;
            triplet_hashes_.insert(key);

            CompactTriplet t;
            t.subject = subj_idx;
            t.predicate = pred_idx;
            t.object = obj_idx;
            t.weight = weight;

            triplets_.push_back(t);
            new_triplets.push_back(t);
        }

        if (!new_triplets.empty()) {
            dirty_ = true;

            // Batch WAL write with single flush
            if (wal_stream_.is_open()) {
                for (const auto& t : new_triplets) {
                    wal_stream_.write(reinterpret_cast<const char*>(&t), sizeof(t));
                }
                wal_stream_.flush();
            }
        }
    }

    // Compact: remove duplicate triplets (one-time cleanup)
    size_t compact() {
        std::unique_lock lock(mutex_);

        size_t before = triplets_.size();
        std::unordered_set<uint64_t> seen;
        seen.reserve(triplets_.size());

        std::vector<CompactTriplet> unique;
        unique.reserve(triplets_.size());

        for (const auto& t : triplets_) {
            uint64_t key = make_triplet_key(t.subject, t.predicate, t.object);
            if (seen.insert(key).second) {
                unique.push_back(t);
            }
        }

        size_t removed = before - unique.size();
        if (removed > 0) {
            triplets_ = std::move(unique);
            triplet_hashes_ = std::move(seen);
            dirty_ = true;
        }

        return removed;
    }

private:
    // FNV-1a hash combining for triplet key
    static uint64_t make_triplet_key(uint32_t subj, uint32_t pred, uint32_t obj) {
        uint64_t h = 14695981039346656037ULL;
        h ^= subj; h *= 1099511628211ULL;
        h ^= pred; h *= 1099511628211ULL;
        h ^= obj;  h *= 1099511628211ULL;
        return h;
    }

    // Persistent hash set for O(1) dedupe (built lazily, maintained incrementally)
    std::unordered_set<uint64_t> triplet_hashes_;

public:

    // Query by subject
    std::vector<std::tuple<std::string, std::string, float>>
    query_subject(const std::string& subject) const {
        std::shared_lock lock(mutex_);

        int64_t subj_idx = entities_.get(subject);
        if (subj_idx < 0) return {};

        std::vector<std::tuple<std::string, std::string, float>> results;

        // Use CSR index if built, otherwise linear scan
        if (!subject_index_.size()) {
            // Linear scan (for small graphs or before index build)
            for (const auto& t : triplets_) {
                if (t.subject == static_cast<uint32_t>(subj_idx)) {
                    results.emplace_back(
                        predicates_.get(t.predicate),
                        entities_.get(t.object),
                        t.weight
                    );
                }
            }
        } else {
            auto [start, end] = subject_index_.range(subj_idx);
            for (uint64_t i = start; i < end && i < triplets_.size(); ++i) {
                const auto& t = triplets_[i];
                if (t.subject == static_cast<uint32_t>(subj_idx)) {
                    results.emplace_back(
                        predicates_.get(t.predicate),
                        entities_.get(t.object),
                        t.weight
                    );
                }
            }
        }

        return results;
    }

    // Query by predicate (requires scan, consider predicate index for frequent use)
    std::vector<std::tuple<std::string, std::string, float>>
    query_predicate(const std::string& predicate) const {
        std::shared_lock lock(mutex_);

        int64_t pred_idx = predicates_.get(predicate);
        if (pred_idx < 0) return {};

        std::vector<std::tuple<std::string, std::string, float>> results;
        for (const auto& t : triplets_) {
            if (t.predicate == static_cast<uint32_t>(pred_idx)) {
                results.emplace_back(
                    entities_.get(t.subject),
                    entities_.get(t.object),
                    t.weight
                );
            }
        }
        return results;
    }

    // Query by object (reverse lookup)
    std::vector<std::tuple<std::string, std::string, float>>
    query_object(const std::string& object) const {
        std::shared_lock lock(mutex_);

        int64_t obj_idx = entities_.get(object);
        if (obj_idx < 0) return {};

        std::vector<std::tuple<std::string, std::string, float>> results;

        if (!object_index_.size()) {
            for (const auto& t : triplets_) {
                if (t.object == static_cast<uint32_t>(obj_idx)) {
                    results.emplace_back(
                        entities_.get(t.subject),
                        predicates_.get(t.predicate),
                        t.weight
                    );
                }
            }
        } else {
            auto [start, end] = object_index_.range(obj_idx);
            for (uint64_t i = start; i < end && i < triplets_by_object_.size(); ++i) {
                const auto& t = triplets_by_object_[i];
                if (t.object == static_cast<uint32_t>(obj_idx)) {
                    results.emplace_back(
                        entities_.get(t.subject),
                        predicates_.get(t.predicate),
                        t.weight
                    );
                }
            }
        }

        return results;
    }

    // General query (subject?, predicate?, object?)
    std::vector<std::tuple<std::string, std::string, std::string, float>>
    query(const std::string& subject = "", const std::string& predicate = "",
          const std::string& object = "") const {
        std::shared_lock lock(mutex_);

        int64_t subj_idx = subject.empty() ? -1 : entities_.get(subject);
        int64_t pred_idx = predicate.empty() ? -1 : predicates_.get(predicate);
        int64_t obj_idx = object.empty() ? -1 : entities_.get(object);

        // If specific entity not found, return empty
        if (!subject.empty() && subj_idx < 0) return {};
        if (!predicate.empty() && pred_idx < 0) return {};
        if (!object.empty() && obj_idx < 0) return {};

        std::vector<std::tuple<std::string, std::string, std::string, float>> results;

        for (const auto& t : triplets_) {
            if (subj_idx >= 0 && t.subject != static_cast<uint32_t>(subj_idx)) continue;
            if (pred_idx >= 0 && t.predicate != static_cast<uint32_t>(pred_idx)) continue;
            if (obj_idx >= 0 && t.object != static_cast<uint32_t>(obj_idx)) continue;

            results.emplace_back(
                entities_.get(t.subject),
                predicates_.get(t.predicate),
                entities_.get(t.object),
                t.weight
            );
        }

        return results;
    }

    // Build CSR indices (call after bulk loading, before queries)
    void build_indices() {
        std::unique_lock lock(mutex_);

        if (triplets_.empty()) return;

        // Sort by subject for subject index
        std::sort(triplets_.begin(), triplets_.end(),
            [](const CompactTriplet& a, const CompactTriplet& b) {
                return a.subject < b.subject;
            });

        subject_index_.build(triplets_, entities_.size(), false);

        // Create object-sorted copy for reverse index
        triplets_by_object_ = triplets_;
        std::sort(triplets_by_object_.begin(), triplets_by_object_.end(),
            [](const CompactTriplet& a, const CompactTriplet& b) {
                return a.object < b.object;
            });

        object_index_.build(triplets_by_object_, entities_.size(), true);

        dirty_ = false;
    }

    // Save to binary file
    bool save(const std::string& path) {
        std::unique_lock lock(mutex_);

        // Build indices if dirty
        if (dirty_ && !triplets_.empty()) {
            lock.unlock();
            build_indices();
            lock.lock();
        }

        std::ofstream out(path, std::ios::binary);
        if (!out) return false;

        // Write header (placeholder, update at end)
        GraphHeader header{};
        header.magic = GRAPH_MAGIC;
        header.version = GRAPH_VERSION;
        header.entity_count = static_cast<uint32_t>(entities_.size());
        header.predicate_count = static_cast<uint32_t>(predicates_.size());
        header.triplet_count = triplets_.size();

        out.write(reinterpret_cast<const char*>(&header), sizeof(header));

        // Entity dictionary
        header.entity_dict_offset = out.tellp();
        entities_.save(out);

        // Predicate dictionary
        header.predicate_dict_offset = out.tellp();
        predicates_.save(out);

        // Triplets (subject-sorted)
        header.triplets_offset = out.tellp();
        out.write(reinterpret_cast<const char*>(triplets_.data()),
                  triplets_.size() * sizeof(CompactTriplet));

        // Subject CSR index
        header.subject_index_offset = out.tellp();
        subject_index_.save(out);

        // Object CSR index
        header.object_index_offset = out.tellp();
        object_index_.save(out);

        // EntityIndex (v2)
        header.entity_index_offset = out.tellp();
        entity_index_.save(out);

        // Update header with offsets
        out.seekp(0);
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));

        out.close();

        // Clear WAL after successful save
        if (!wal_path_.empty()) {
            std::ofstream(wal_path_, std::ios::trunc).close();
        }

        return true;
    }

    // Load from binary file
    bool load(const std::string& path) {
        std::unique_lock lock(mutex_);

        std::ifstream in(path, std::ios::binary);
        if (!in) return false;

        // Read header
        GraphHeader header;
        in.read(reinterpret_cast<char*>(&header), sizeof(header));
        // Accept v1 (original) and v2 (with EntityIndex)
        if (!in || header.magic != GRAPH_MAGIC ||
            (header.version != 1 && header.version != GRAPH_VERSION)) {
            return false;
        }

        // Clear existing data
        entities_.clear();
        predicates_.clear();
        triplets_.clear();
        triplets_by_object_.clear();
        subject_index_.clear();
        object_index_.clear();
        entity_index_.clear();

        // Load entity dictionary
        in.seekg(header.entity_dict_offset);
        if (!entities_.load(in)) return false;

        // Load predicate dictionary
        in.seekg(header.predicate_dict_offset);
        if (!predicates_.load(in)) return false;

        // Load triplets
        in.seekg(header.triplets_offset);
        triplets_.resize(header.triplet_count);
        in.read(reinterpret_cast<char*>(triplets_.data()),
                header.triplet_count * sizeof(CompactTriplet));
        if (!in) return false;

        // Load subject index
        in.seekg(header.subject_index_offset);
        if (!subject_index_.load(in, header.entity_count + 1)) return false;

        // Load object index
        in.seekg(header.object_index_offset);
        if (!object_index_.load(in, header.entity_count + 1)) return false;

        // Load EntityIndex (v2+)
        if (header.version >= 2 && header.entity_index_offset > 0) {
            in.seekg(header.entity_index_offset);
            if (!entity_index_.load(in)) {
                // Non-fatal: continue without EntityIndex
                entity_index_.clear();
            }
        }

        // Create object-sorted triplets
        triplets_by_object_ = triplets_;
        std::sort(triplets_by_object_.begin(), triplets_by_object_.end(),
            [](const CompactTriplet& a, const CompactTriplet& b) {
                return a.object < b.object;
            });

        dirty_ = false;

        in.close();

        // Replay WAL if exists
        replay_wal();

        return true;
    }

    // Open WAL for incremental persistence
    void open_wal(const std::string& path) {
        wal_path_ = path;
        wal_stream_.open(path, std::ios::binary | std::ios::app);
    }

    // Replay WAL entries
    size_t replay_wal() {
        if (wal_path_.empty()) return 0;

        std::ifstream in(wal_path_, std::ios::binary);
        if (!in) return 0;

        size_t count = 0;
        CompactTriplet t;
        while (in.read(reinterpret_cast<char*>(&t), sizeof(t))) {
            // Ensure dictionaries have the indices
            // (WAL stores indices, so we need existing dictionaries)
            if (t.subject < entities_.size() && t.object < entities_.size() &&
                t.predicate < predicates_.size()) {
                triplets_.push_back(t);
                count++;
            }
        }

        if (count > 0) dirty_ = true;

        return count;
    }

    // Stats
    size_t entity_count() const { return entities_.size(); }
    size_t predicate_count() const { return predicates_.size(); }
    size_t triplet_count() const { return triplets_.size(); }

    // Decoded triplet for export
    struct DecodedTriplet {
        std::string subject;
        std::string predicate;
        std::string object;
        float weight;
    };

    // Get all triplets as decoded strings (for export)
    std::vector<DecodedTriplet> all_triplets() const {
        std::shared_lock lock(mutex_);
        std::vector<DecodedTriplet> result;
        result.reserve(triplets_.size());
        for (const auto& t : triplets_) {
            result.push_back({
                entities_.get(t.subject),
                predicates_.get(t.predicate),
                entities_.get(t.object),
                t.weight
            });
        }
        return result;
    }

    // Memory usage estimate
    size_t memory_bytes() const {
        size_t bytes = 0;
        bytes += triplets_.size() * sizeof(CompactTriplet);
        bytes += triplets_by_object_.size() * sizeof(CompactTriplet);
        // Rough estimate for dictionaries and indices
        bytes += entities_.size() * 50;  // Avg string + overhead
        bytes += predicates_.size() * 30;
        return bytes;
    }

    // === EntityIndex methods (string API backed by dictionary indices) ===

    // Link an entity name to a NodeId in the soul
    void link_entity(const std::string& entity, NodeId node_id) {
        std::unique_lock lock(mutex_);
        uint32_t idx = entities_.get_or_create(entity);
        entity_index_.link(idx, node_id);
    }

    // Resolve entity name to NodeId
    std::optional<NodeId> resolve_entity(const std::string& entity) const {
        std::shared_lock lock(mutex_);
        int64_t idx = entities_.get(entity);
        if (idx < 0) return std::nullopt;
        return entity_index_.resolve(static_cast<uint32_t>(idx));
    }

    // Get all entity names linked to a NodeId
    std::vector<std::string> entities_for_node(NodeId node_id) const {
        std::shared_lock lock(mutex_);
        auto indices = entity_index_.indices_for_node(node_id);
        std::vector<std::string> result;
        result.reserve(indices.size());
        for (uint32_t idx : indices) {
            result.push_back(entities_.get(idx));
        }
        return result;
    }

    // Check if entity is linked to a node
    bool entity_has_node(const std::string& entity) const {
        std::shared_lock lock(mutex_);
        int64_t idx = entities_.get(entity);
        if (idx < 0) return false;
        return entity_index_.has(static_cast<uint32_t>(idx));
    }

    // Unlink an entity from its node
    void unlink_entity(const std::string& entity) {
        std::unique_lock lock(mutex_);
        int64_t idx = entities_.get(entity);
        if (idx >= 0) {
            entity_index_.unlink(static_cast<uint32_t>(idx));
        }
    }

    // Get all linked entities (for export/debug)
    std::vector<std::pair<std::string, NodeId>> linked_entities() const {
        std::shared_lock lock(mutex_);
        auto pairs = entity_index_.all();
        std::vector<std::pair<std::string, NodeId>> result;
        result.reserve(pairs.size());
        for (const auto& [idx, node_id] : pairs) {
            result.emplace_back(entities_.get(idx), node_id);
        }
        return result;
    }

    // Count of linked entities
    size_t linked_entity_count() const {
        std::shared_lock lock(mutex_);
        return entity_index_.size();
    }

private:
    mutable std::shared_mutex mutex_;

    Dictionary entities_;       // String → compact index (for triplets)
    Dictionary predicates_;     // String → compact index (for triplets)
    EntityIndex entity_index_;  // String → NodeId (links to soul nodes)

    std::vector<CompactTriplet> triplets_;          // Sorted by subject
    std::vector<CompactTriplet> triplets_by_object_; // Sorted by object

    CSRIndex subject_index_;
    CSRIndex object_index_;

    bool dirty_ = false;
    std::string wal_path_;
    std::ofstream wal_stream_;
};

} // namespace chitta
