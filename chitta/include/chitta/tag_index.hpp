#pragma once
// Tag Index: Scalable tag storage with inverted index for 100M+ nodes
//
// Architecture (inspired by FalkorDB):
//   - String interning: each unique tag stored once, referenced by tag_id
//   - Inverted index: tag_id -> RoaringBitmap of slot_ids (O(1) lookup)
//   - Forward index: slot_id -> [tag_ids] (for reconstruction)
//
// Performance at 100M nodes:
//   - Memory: ~1 bit per node per dense tag, sorted array for sparse
//   - Lookup: O(1) via roaring bitmap
//   - Intersection: O(min(n,m)) for AND operations
//   - Serialization: portable roaring format

#include "types.hpp"
#include "mmap.hpp"
#include <roaring/roaring.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <shared_mutex>
#include <cstring>
#include <iostream>

namespace chitta {

constexpr uint32_t TAG_INDEX_MAGIC = 0x54414749;  // "TAGI"
constexpr uint32_t TAG_INDEX_VERSION = 1;

// Tag index header for persistence
struct TagIndexHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t tag_count;       // Number of unique tags
    uint32_t reserved;
    uint64_t string_table_offset;
    uint64_t posting_offset;
    uint64_t forward_offset;
    uint64_t checksum;
    uint8_t padding[16];      // Pad to 64 bytes
};
static_assert(sizeof(TagIndexHeader) == 64, "TagIndexHeader must be 64 bytes");

class SlotTagIndex {
public:
    SlotTagIndex() = default;
    ~SlotTagIndex() { close(); }

    SlotTagIndex(const SlotTagIndex&) = delete;
    SlotTagIndex& operator=(const SlotTagIndex&) = delete;

    // ═══════════════════════════════════════════════════════════════════════
    // Lifecycle
    // ═══════════════════════════════════════════════════════════════════════

    bool create(const std::string& path) {
        path_ = path;
        dirty_ = true;  // Force initial save to create file
        return save();  // Create empty file immediately
    }

    bool open(const std::string& path) {
        path_ = path;
        return load();
    }

    void close() {
        save();
        clear();
    }

    bool valid() const { return true; }

    // ═══════════════════════════════════════════════════════════════════════
    // String Interning
    // ═══════════════════════════════════════════════════════════════════════

    // Intern a tag string, returns tag_id
    uint32_t intern(const std::string& tag) {
        std::unique_lock lock(mutex_);

        auto it = string_to_id_.find(tag);
        if (it != string_to_id_.end()) {
            return it->second;
        }

        uint32_t id = static_cast<uint32_t>(id_to_string_.size());
        string_to_id_[tag] = id;
        id_to_string_.push_back(tag);
        postings_.push_back(roaring_bitmap_create());
        dirty_ = true;

        return id;
    }

    // Resolve tag_id to string
    const std::string& resolve(uint32_t tag_id) const {
        std::shared_lock lock(mutex_);
        static const std::string empty;
        if (tag_id >= id_to_string_.size()) return empty;
        return id_to_string_[tag_id];
    }

    // Check if tag exists
    bool has_tag(const std::string& tag) const {
        std::shared_lock lock(mutex_);
        return string_to_id_.find(tag) != string_to_id_.end();
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Inverted Index Operations
    // ═══════════════════════════════════════════════════════════════════════

    // Add tag to slot
    void add(uint32_t slot, const std::string& tag) {
        uint32_t tag_id = intern(tag);

        std::unique_lock lock(mutex_);
        roaring_bitmap_add(postings_[tag_id], slot);

        // Update forward index
        if (slot >= forward_.size()) {
            forward_.resize(slot + 1);
        }
        forward_[slot].push_back(tag_id);
        dirty_ = true;
    }

    // Add multiple tags to slot
    void add(uint32_t slot, const std::vector<std::string>& tags) {
        for (const auto& tag : tags) {
            add(slot, tag);
        }
    }

    // Remove tag from slot
    void remove(uint32_t slot, const std::string& tag) {
        std::unique_lock lock(mutex_);

        auto it = string_to_id_.find(tag);
        if (it == string_to_id_.end()) return;

        uint32_t tag_id = it->second;
        roaring_bitmap_remove(postings_[tag_id], slot);

        // Update forward index
        if (slot < forward_.size()) {
            auto& tags = forward_[slot];
            tags.erase(std::remove(tags.begin(), tags.end(), tag_id), tags.end());
        }
        dirty_ = true;
    }

    // Remove all tags from slot
    void remove_all(uint32_t slot) {
        std::unique_lock lock(mutex_);

        if (slot >= forward_.size()) return;

        for (uint32_t tag_id : forward_[slot]) {
            roaring_bitmap_remove(postings_[tag_id], slot);
        }
        forward_[slot].clear();
        dirty_ = true;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Query Operations
    // ═══════════════════════════════════════════════════════════════════════

    // Get all slots with a specific tag
    std::vector<uint32_t> slots_with_tag(const std::string& tag) const {
        std::shared_lock lock(mutex_);

        auto it = string_to_id_.find(tag);
        if (it == string_to_id_.end()) return {};

        return bitmap_to_vector(postings_[it->second]);
    }

    // Check if slot has tag (O(1))
    bool slot_has_tag(uint32_t slot, const std::string& tag) const {
        std::shared_lock lock(mutex_);

        auto it = string_to_id_.find(tag);
        if (it == string_to_id_.end()) return false;

        return roaring_bitmap_contains(postings_[it->second], slot);
    }

    // Get all tags for a slot
    std::vector<std::string> tags_for_slot(uint32_t slot) const {
        std::shared_lock lock(mutex_);

        std::vector<std::string> result;
        if (slot >= forward_.size()) return result;

        for (uint32_t tag_id : forward_[slot]) {
            if (tag_id < id_to_string_.size()) {
                result.push_back(id_to_string_[tag_id]);
            }
        }
        return result;
    }

    // Get raw roaring bitmap for a tag (for external intersection)
    const roaring_bitmap_t* get_posting(const std::string& tag) const {
        std::shared_lock lock(mutex_);

        auto it = string_to_id_.find(tag);
        if (it == string_to_id_.end()) return nullptr;

        return postings_[it->second];
    }

    // Intersect search results with tag filter
    // Returns filtered slots in sorted order
    std::vector<uint32_t> filter_by_tag(
        const std::vector<uint32_t>& slots,
        const std::string& tag
    ) const {
        std::shared_lock lock(mutex_);

        auto it = string_to_id_.find(tag);
        if (it == string_to_id_.end()) return {};

        const roaring_bitmap_t* posting = postings_[it->second];
        std::vector<uint32_t> result;
        result.reserve(slots.size());

        for (uint32_t slot : slots) {
            if (roaring_bitmap_contains(posting, slot)) {
                result.push_back(slot);
            }
        }
        return result;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Statistics
    // ═══════════════════════════════════════════════════════════════════════

    size_t tag_count() const {
        std::shared_lock lock(mutex_);
        return id_to_string_.size();
    }

    size_t total_taggings() const {
        std::shared_lock lock(mutex_);
        size_t total = 0;
        for (const auto* bitmap : postings_) {
            total += roaring_bitmap_get_cardinality(bitmap);
        }
        return total;
    }

    size_t memory_usage() const {
        std::shared_lock lock(mutex_);
        size_t bytes = 0;

        // String table
        for (const auto& s : id_to_string_) {
            bytes += s.size() + sizeof(std::string);
        }

        // Hash table overhead
        bytes += string_to_id_.size() * (sizeof(std::string) + sizeof(uint32_t) + 32);

        // Roaring bitmaps
        for (const auto* bitmap : postings_) {
            bytes += roaring_bitmap_size_in_bytes(bitmap);
        }

        // Forward index
        for (const auto& tags : forward_) {
            bytes += tags.capacity() * sizeof(uint32_t);
        }

        return bytes;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Persistence
    // ═══════════════════════════════════════════════════════════════════════

    bool save() const {
        if (path_.empty() || !dirty_) return true;

        std::shared_lock lock(mutex_);

        FILE* f = fopen(path_.c_str(), "wb");
        if (!f) return false;

        // Write header (placeholder, update at end)
        TagIndexHeader header{};
        header.magic = TAG_INDEX_MAGIC;
        header.version = TAG_INDEX_VERSION;
        header.tag_count = static_cast<uint32_t>(id_to_string_.size());
        fwrite(&header, sizeof(header), 1, f);

        // Write string table
        header.string_table_offset = ftell(f);
        for (const auto& s : id_to_string_) {
            uint16_t len = static_cast<uint16_t>(s.size());
            fwrite(&len, sizeof(len), 1, f);
            fwrite(s.data(), 1, len, f);
        }

        // Write posting lists (serialized roaring bitmaps)
        header.posting_offset = ftell(f);
        for (const auto* bitmap : postings_) {
            size_t size = roaring_bitmap_size_in_bytes(bitmap);
            uint32_t size32 = static_cast<uint32_t>(size);
            fwrite(&size32, sizeof(size32), 1, f);

            std::vector<char> buf(size);
            roaring_bitmap_serialize(bitmap, buf.data());
            fwrite(buf.data(), 1, size, f);
        }

        // Write forward index
        header.forward_offset = ftell(f);
        uint32_t forward_count = static_cast<uint32_t>(forward_.size());
        fwrite(&forward_count, sizeof(forward_count), 1, f);
        for (const auto& tags : forward_) {
            uint16_t count = static_cast<uint16_t>(tags.size());
            fwrite(&count, sizeof(count), 1, f);
            fwrite(tags.data(), sizeof(uint32_t), tags.size(), f);
        }

        // Update header with offsets
        header.checksum = crc32(reinterpret_cast<const uint8_t*>(&header), 48);
        fseek(f, 0, SEEK_SET);
        fwrite(&header, sizeof(header), 1, f);

        fclose(f);
        dirty_ = false;
        return true;
    }

    bool load() {
        FILE* f = fopen(path_.c_str(), "rb");
        if (!f) return false;

        // Read header
        TagIndexHeader header{};
        if (fread(&header, sizeof(header), 1, f) != 1) {
            fclose(f);
            return false;
        }

        if (header.magic != TAG_INDEX_MAGIC) {
            fclose(f);
            return false;
        }

        clear();

        // Read string table
        fseek(f, header.string_table_offset, SEEK_SET);
        id_to_string_.reserve(header.tag_count);
        for (uint32_t i = 0; i < header.tag_count; ++i) {
            uint16_t len;
            fread(&len, sizeof(len), 1, f);
            std::string s(len, '\0');
            fread(s.data(), 1, len, f);
            string_to_id_[s] = i;
            id_to_string_.push_back(std::move(s));
        }

        // Read posting lists
        fseek(f, header.posting_offset, SEEK_SET);
        postings_.reserve(header.tag_count);
        for (uint32_t i = 0; i < header.tag_count; ++i) {
            uint32_t size;
            fread(&size, sizeof(size), 1, f);
            std::vector<char> buf(size);
            fread(buf.data(), 1, size, f);
            postings_.push_back(roaring_bitmap_deserialize(buf.data()));
        }

        // Read forward index
        fseek(f, header.forward_offset, SEEK_SET);
        uint32_t forward_count;
        fread(&forward_count, sizeof(forward_count), 1, f);
        forward_.resize(forward_count);
        for (uint32_t i = 0; i < forward_count; ++i) {
            uint16_t count;
            fread(&count, sizeof(count), 1, f);
            forward_[i].resize(count);
            fread(forward_[i].data(), sizeof(uint32_t), count, f);
        }

        fclose(f);
        dirty_ = false;
        return true;
    }

private:
    void clear() {
        for (auto* bitmap : postings_) {
            roaring_bitmap_free(bitmap);
        }
        postings_.clear();
        string_to_id_.clear();
        id_to_string_.clear();
        forward_.clear();
    }

    static std::vector<uint32_t> bitmap_to_vector(const roaring_bitmap_t* bitmap) {
        uint64_t card = roaring_bitmap_get_cardinality(bitmap);
        std::vector<uint32_t> result(card);
        roaring_bitmap_to_uint32_array(bitmap, result.data());
        return result;
    }

    std::string path_;
    mutable bool dirty_ = false;
    mutable std::shared_mutex mutex_;

    // String interning
    std::unordered_map<std::string, uint32_t> string_to_id_;
    std::vector<std::string> id_to_string_;

    // Inverted index: tag_id -> slots
    std::vector<roaring_bitmap_t*> postings_;

    // Forward index: slot -> tag_ids
    std::vector<std::vector<uint32_t>> forward_;
};

} // namespace chitta
