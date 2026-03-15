#pragma once
#include <string>
#include <vector>
#include <optional>
#include <stdexcept>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <limits>
#include "chitta_field.h"

namespace chitta {

/// A single recall result from FieldStore.
struct FieldRecallHit {
    uint64_t    memory_id;
    float       score;
    float       semantic_score;
    int64_t     ts_ms;
    float       strength;
    float       confidence;
    std::string kind;
    std::string realm;
    std::string content;
};

/// Thin RAII C++ wrapper around the chitta-field C FFI.
/// Manages a single CfHandle with automatic open/close lifetime.
class FieldStore {
public:
    explicit FieldStore(const std::string& data_dir, const std::string& lock_dir) {
        handle_ = cf_open(data_dir.c_str(), lock_dir.c_str());
        if (!handle_) {
            throw std::runtime_error(
                "cf_open failed: could not open chitta-field store at " + data_dir);
        }
    }

    ~FieldStore() {
        if (handle_) {
            cf_flush(handle_);
            cf_close(handle_);
            handle_ = nullptr;
        }
    }

    // Non-copyable, movable
    FieldStore(const FieldStore&) = delete;
    FieldStore& operator=(const FieldStore&) = delete;
    FieldStore(FieldStore&& o) noexcept : handle_(o.handle_) { o.handle_ = nullptr; }

    /// Store a new memory. Returns its stable MemoryId.
    uint64_t remember(
        const std::string&        kind,
        const std::string&        realm,
        const std::string&        content,
        const std::vector<float>& embedding,
        float                     confidence     = 1.0f,
        float                     decay_rate     = 0.001f,
        int64_t                   authored_at_ms = 0
    ) {
        uint64_t id = 0;
        int r = cf_put_memory(
            handle_,
            kind.c_str(), realm.c_str(),
            reinterpret_cast<const uint8_t*>(content.data()), content.size(),
            embedding.data(), embedding.size(),
            confidence, decay_rate, authored_at_ms,
            &id
        );
        if (r != 0) throw std::runtime_error(last_error());
        return id;
    }

    /// Strengthen a memory (positive feedback).
    void strengthen(uint64_t id, float amount = 0.1f) {
        cf_update_state(handle_, id,
            amount,
            std::numeric_limits<float>::quiet_NaN(),   // confidence_delta — no change
            std::numeric_limits<float>::quiet_NaN(),   // decay_rate — no change
            1,                                         // touch
            static_cast<int8_t>(-1)                    // pin — no change
        );
    }

    /// Weaken a memory (negative feedback).
    void weaken(uint64_t id, float amount = 0.1f) {
        cf_update_state(handle_, id,
            -amount,
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::quiet_NaN(),
            0,
            static_cast<int8_t>(-1)
        );
    }

    /// Touch (update access time without changing strength).
    void touch(uint64_t id) {
        cf_update_state(handle_, id,
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::quiet_NaN(),
            1,
            static_cast<int8_t>(-1)
        );
    }

    /// Soft-delete a memory.
    void forget(uint64_t id) {
        cf_forget(handle_, id);
    }

    /// Semantic recall — find k most similar memories to query embedding.
    std::vector<FieldRecallHit> recall(
        const std::vector<float>& query_embedding,
        size_t                    k,
        const std::string&        realm = ""
    ) {
        constexpr size_t MAX_HITS = 256;
        CfRecallHit buf[MAX_HITS];
        size_t written = 0;

        const char* realm_ptr = realm.empty() ? nullptr : realm.c_str();
        int r = cf_recall_semantic(
            handle_,
            query_embedding.data(), query_embedding.size(),
            realm_ptr, k,
            buf, MAX_HITS, &written
        );
        if (r != 0) throw std::runtime_error(last_error());
        return hits_to_results(buf, written);
    }

    /// Temporal recall — find memories in a time range.
    std::vector<FieldRecallHit> recall_temporal(
        int64_t            start_ms,
        int64_t            end_ms,
        size_t             limit,
        const std::string& realm = ""
    ) {
        constexpr size_t MAX_HITS = 256;
        CfRecallHit buf[MAX_HITS];
        size_t written = 0;

        const char* realm_ptr = realm.empty() ? nullptr : realm.c_str();
        int r = cf_recall_temporal(handle_, start_ms, end_ms, realm_ptr, limit,
                                   buf, MAX_HITS, &written);
        if (r != 0) throw std::runtime_error(last_error());
        return hits_to_results(buf, written);
    }

    /// Artifact recall — find memories associated with a file path.
    std::vector<FieldRecallHit> recall_artifact(
        const std::string& path,
        size_t             limit
    ) {
        constexpr size_t MAX_HITS = 256;
        CfRecallHit buf[MAX_HITS];
        size_t written = 0;

        int r = cf_recall_artifact(handle_, path.c_str(), limit,
                                   buf, MAX_HITS, &written);
        if (r != 0) throw std::runtime_error(last_error());
        return hits_to_results(buf, written);
    }

    /// Expand associations from seed memory IDs (spreading activation).
    std::vector<FieldRecallHit> expand_associations(
        const std::vector<uint64_t>& seed_ids,
        size_t                       max_hops = 2,
        size_t                       limit    = 20
    ) {
        constexpr size_t MAX_HITS = 256;
        CfRecallHit buf[MAX_HITS];
        size_t written = 0;

        int r = cf_expand_associations(handle_,
            seed_ids.data(), seed_ids.size(),
            max_hops, limit,
            buf, MAX_HITS, &written);
        if (r != 0) throw std::runtime_error(last_error());
        return hits_to_results(buf, written);
    }

    /// Add an association edge between memories.
    /// edge_type: 0=DerivedFrom, 1=SameSession, 2=SameArtifact, 3=CoRetrieved
    void add_edge(uint64_t src, uint64_t dst, uint8_t edge_type = 3, float weight = 0.5f) {
        cf_add_assoc_edge(handle_, src, dst, edge_type, weight);
    }

    /// Register a file artifact, returns its artifact ID.
    uint64_t upsert_artifact(const std::string& path) {
        uint64_t id = 0;
        cf_upsert_artifact(handle_, path.c_str(), &id);
        return id;
    }

    /// Get content string for a memory.
    std::string get_content(uint64_t id) {
        char buf[65536];
        size_t written = 0;
        int r = cf_get_content(handle_, id,
                               reinterpret_cast<uint8_t*>(buf), sizeof(buf), &written);
        if (r != 0) return "";
        return std::string(buf, written);
    }

    /// Number of live memories.
    size_t memory_count() const {
        return cf_memory_count(handle_);
    }

    /// Flush manifest to disk.
    void flush() {
        cf_flush(handle_);
    }

    /// Health check — returns true if store is accessible.
    bool healthy() const {
        return handle_ != nullptr;
    }

private:
    CfHandle* handle_ = nullptr;

    std::string last_error() const {
        if (!handle_) return "handle is null";
        const char* e = cf_last_error(handle_);
        return e ? e : "unknown error";
    }

    /// Enrich CfRecallHit with content/kind/realm strings.
    std::vector<FieldRecallHit> hits_to_results(const CfRecallHit* buf, size_t n) {
        std::vector<FieldRecallHit> out;
        out.reserve(n);
        // cf_get_content needs a written out-param; cf_get_kind/realm do not
        char strbuf[65536];
        size_t written = 0;

        for (size_t i = 0; i < n; ++i) {
            FieldRecallHit h;
            h.memory_id      = buf[i].memory_id;
            h.score          = buf[i].score;
            h.semantic_score = buf[i].semantic_score;
            h.ts_ms          = buf[i].ts_ms;
            h.strength       = buf[i].strength;
            h.confidence     = buf[i].confidence;

            // Fetch content (has written out-param)
            written = 0;
            if (cf_get_content(handle_, h.memory_id,
                               reinterpret_cast<uint8_t*>(strbuf),
                               sizeof(strbuf), &written) == 0) {
                h.content.assign(strbuf, written);
            }

            // Fetch kind (null-terminated, no written out-param)
            strbuf[0] = '\0';
            if (cf_get_kind(handle_, h.memory_id,
                            reinterpret_cast<uint8_t*>(strbuf),
                            sizeof(strbuf)) == 0) {
                h.kind = strbuf;
            }

            // Fetch realm (null-terminated, no written out-param)
            strbuf[0] = '\0';
            if (cf_get_realm(handle_, h.memory_id,
                             reinterpret_cast<uint8_t*>(strbuf),
                             sizeof(strbuf)) == 0) {
                h.realm = strbuf;
            }

            out.push_back(std::move(h));
        }
        return out;
    }
};

} // namespace chitta
