#pragma once
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <stdexcept>
#include <cstring>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include "chitta_field.h"

// Explicit forward declarations for functions added in a later chitta-field version.
// These ensure the symbols are visible even if chitta_field.h was included earlier
// from a different path that predates these additions.
extern "C" {
int cf_backfill_embedding(struct CfHandle* h, uint64_t memory_id,
    const float* embedding_ptr, size_t embedding_len);
int cf_pending_embeddings(struct CfHandle* h,
    uint64_t* out_ids, size_t max_ids, size_t* out_count);
int cf_forget_triplet(struct CfHandle* h,
    const char* subject, const char* predicate, const char* object);
int cf_select_route(struct CfHandle* h, const char* query,
    uint64_t* out_episode_id, uint8_t* out_route);
int cf_route_feedback(struct CfHandle* h, uint64_t episode_id, float reward);
int cf_set_memory_status(struct CfHandle* h, uint64_t memory_id, uint8_t status);
}

namespace chitta {

/// A single recall result from FieldStore.
struct FieldRecallHit {
    uint64_t    memory_id;
    float       score;
    float       semantic_score;
    int64_t     ts_ms;
    float       strength;
    float       confidence;
    uint32_t    access_count;
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

    /// Set memory lifecycle status: 0=Active 1=Superseded 2=Contradicted 3=Archived
    void set_memory_status(uint64_t id, uint8_t status) {
        cf_set_memory_status(handle_, id, status);
    }

    /// Backfill embedding for a memory stored without one (embed_pending=true).
    void backfill_embedding(uint64_t id, const std::vector<float>& embedding) {
        int r = cf_backfill_embedding(handle_, id,
            embedding.data(), embedding.size());
        if (r != 0) throw std::runtime_error(last_error());
    }

    /// Return IDs of memories waiting for an embedding (embed_pending=true).
    std::vector<uint64_t> pending_embeddings(size_t limit = 100) {
        std::vector<uint64_t> ids(limit);
        size_t written = 0;
        cf_pending_embeddings(handle_, ids.data(), limit, &written);
        ids.resize(written);
        return ids;
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

    /// Ingest new ops from foreign-instance segment files on shared storage.
    /// Returns number of ops applied, or -1 on error.
    int sync_foreign() {
        return cf_sync_foreign(handle_);
    }

    /// Apply outcome feedback for a retrieval episode (route learning).
    void feedback(uint64_t episode_id, float reward) {
        cf_feedback(handle_, episode_id, reward);
    }

    /// Get recommended working memory window size for a session type.
    size_t recommended_window(const std::string& session_type) const {
        return cf_recommended_window(handle_, session_type.c_str());
    }

    /// BM25 keyword recall.
    std::vector<FieldRecallHit> recall_keyword(const std::string& query, size_t k) {
        constexpr size_t MAX_HITS = 256;
        CfRecallHit buf[MAX_HITS];
        size_t written = 0;
        int r = cf_recall_keyword(handle_, query.c_str(), k, buf, MAX_HITS, &written);
        if (r != 0) throw std::runtime_error(last_error());
        return hits_to_results(buf, written);
    }

    /// Add an SPO triplet fact.
    uint64_t add_triplet(const std::string& subject, const std::string& predicate,
                         const std::string& object, float weight = 1.0f,
                         uint64_t source_memory_id = 0) {
        uint64_t id = 0;
        cf_add_triplet(handle_, subject.c_str(), predicate.c_str(), object.c_str(),
                       weight, source_memory_id, &id);
        return id;
    }

    /// Select retrieval route for query. Returns {episode_id, route} where
    /// route: 0=Semantic, 1=Keyword, 2=Temporal, 3=Artifact, 4=Hybrid, 5=Full
    struct RouteSelection { uint64_t episode_id; uint8_t route; };
    RouteSelection select_route(const std::string& query) {
        RouteSelection sel{0, 4};  // default Hybrid
        cf_select_route(handle_, query.c_str(), &sel.episode_id, &sel.route);
        return sel;
    }

    /// Record retrieval outcome for a route episode. reward in [-1, 1].
    void route_feedback(uint64_t episode_id, float reward) {
        cf_route_feedback(handle_, episode_id, reward);
    }

    /// Remove triplet by subject+predicate+object (invalidates matching entry).
    bool forget_triplet(const std::string& subject, const std::string& predicate,
                        const std::string& object) {
        return cf_forget_triplet(handle_, subject.c_str(), predicate.c_str(),
                                 object.c_str()) == 0;
    }

    /// Query triplets by subject, returns JSON string.
    std::string query_subject(const std::string& subject) {
        char buf[65536]; size_t written = 0;
        cf_query_subject(handle_, subject.c_str(), buf, sizeof(buf), &written);
        return std::string(buf, written);
    }

    /// Query triplets by object, returns JSON string.
    std::string query_object(const std::string& object) {
        char buf[65536]; size_t written = 0;
        cf_query_object(handle_, object.c_str(), buf, sizeof(buf), &written);
        return std::string(buf, written);
    }

    /// Health check — returns true if store is accessible.
    bool healthy() const {
        return handle_ != nullptr;
    }

    // ── Code Intelligence ────────────────────────────────────────────────────

    /// Upsert a symbol. Returns its SymbolId.
    uint64_t upsert_symbol(
        const std::string& kind,
        const std::string& name,
        const std::string& signature,
        const std::string& file_path,
        uint32_t line_start,
        uint32_t line_end,
        uint64_t repo_id,
        const std::vector<float>& embedding,
        const std::string& description = "",
        uint64_t memory_id = 0
    ) {
        uint64_t id = 0;
        const char* desc_ptr = description.empty() ? nullptr : description.c_str();
        int r = cf_upsert_symbol(handle_,
            kind.c_str(), name.c_str(), signature.c_str(), file_path.c_str(),
            line_start, line_end, repo_id,
            embedding.data(), embedding.size(),
            desc_ptr, memory_id, &id);
        if (r != 0) throw std::runtime_error(last_error());
        return id;
    }

    void remove_symbol(uint64_t symbol_id) {
        cf_remove_symbol(handle_, symbol_id);
    }

    std::vector<CfSymbolHit> search_symbols_by_name(const std::string& query, size_t limit) {
        constexpr size_t MAX = 256;
        CfSymbolHit buf[MAX];
        size_t written = 0;
        cf_search_symbols_by_name(handle_, query.c_str(), limit, buf, MAX, &written);
        return std::vector<CfSymbolHit>(buf, buf + written);
    }

    std::vector<CfSymbolHit> search_symbols_semantic(const std::vector<float>& query, size_t k) {
        constexpr size_t MAX = 256;
        CfSymbolHit buf[MAX];
        size_t written = 0;
        cf_search_symbols_semantic(handle_, query.data(), query.size(), k, buf, MAX, &written);
        return std::vector<CfSymbolHit>(buf, buf + written);
    }

    std::vector<CfSymbolHit> symbols_in_file(const std::string& file_path) {
        constexpr size_t MAX = 1024;
        CfSymbolHit buf[MAX];
        size_t written = 0;
        cf_symbols_in_file(handle_, file_path.c_str(), buf, MAX, &written);
        return std::vector<CfSymbolHit>(buf, buf + written);
    }

    void add_sym_call_edge(uint64_t caller_id, uint64_t callee_id) {
        cf_add_sym_call_edge(handle_, caller_id, callee_id);
    }

    std::vector<uint64_t> get_callees(uint64_t symbol_id) {
        constexpr size_t MAX = 1024;
        uint64_t buf[MAX];
        size_t written = 0;
        cf_get_callees(handle_, symbol_id, buf, MAX, &written);
        return std::vector<uint64_t>(buf, buf + written);
    }

    std::vector<uint64_t> get_callers(uint64_t symbol_id) {
        constexpr size_t MAX = 1024;
        uint64_t buf[MAX];
        size_t written = 0;
        cf_get_callers(handle_, symbol_id, buf, MAX, &written);
        return std::vector<uint64_t>(buf, buf + written);
    }

    uint64_t upsert_code_file(const std::string& path, const std::string& project, int64_t mtime) {
        uint64_t id = 0;
        int r = cf_upsert_code_file(handle_, path.c_str(), project.c_str(), mtime, &id);
        if (r != 0) throw std::runtime_error(last_error());
        return id;
    }

    size_t symbol_count() const {
        return cf_symbol_count(handle_);
    }

    size_t code_file_count() const {
        return cf_code_file_count(handle_);
    }

    /// Encode all unindexed memories into sparse codes. Returns count encoded.
    size_t encode_all() {
        return cf_encode_all(handle_);
    }

    /// Get cortical index size (how many memories have sparse codes).
    size_t cortical_count() const {
        return cf_cortical_count(handle_);
    }

    /// Get number of prototype clusters in the cortical index.
    size_t prototype_count() {
        return cf_prototype_count(handle_);
    }

    /// Save the cortical index to a binary snapshot file. Returns true on success.
    bool save_snapshot() const {
        return cf_save_snapshot(handle_);
    }

    /// Save the full in-memory state to a binary snapshot file. Returns true on success.
    bool save_full_snapshot() const {
        return cf_save_full_snapshot(handle_);
    }

    // ── Lite Encoder ─────────────────────────────────────────────────────────

    /// Check if the lite encoder is trained and ready.
    bool lite_encoder_ready() const {
        return cf_lite_encoder_ready(handle_) != 0;
    }

    /// Train the lite encoder from all memories with sparse codes.
    /// Returns true if at least one training example was used.
    bool train_lite_encoder() {
        return cf_train_lite_encoder(handle_) > 0;
    }

    /// Save the lite encoder to disk. Returns true on success.
    bool save_lite_encoder() {
        return cf_save_lite_encoder(handle_) == 0;
    }

    /// Encode text via lite encoder. Returns (atom_idx, weight) pairs.
    /// Returns empty vector if not trained or no words match vocab.
    std::vector<std::pair<uint32_t, float>> encode_lite(const std::string& text) {
        static constexpr size_t K_ACTIVE = 64;
        uint32_t atoms[K_ACTIVE] = {};
        float weights[K_ACTIVE] = {};
        int32_t n = cf_encode_lite(handle_,
            reinterpret_cast<const uint8_t*>(text.data()), text.size(),
            atoms, weights);
        if (n <= 0) return {};
        std::vector<std::pair<uint32_t, float>> result;
        result.reserve(static_cast<size_t>(n));
        for (int32_t i = 0; i < n; ++i) {
            result.emplace_back(atoms[i], weights[i]);
        }
        return result;
    }

    /// Run a tier demotion pass. Returns (demoted_count, deleted_count).
    std::pair<size_t, size_t> run_demotion(int64_t now_ms) const {
        uint64_t r = cf_run_demotion(handle_, now_ms);
        return {static_cast<size_t>(r & 0xFFFFFFFF), static_cast<size_t>(r >> 32)};
    }

    // ── Task / Sadhana organ ─────────────────────────────────────────────────

    /// Create a task entry in chitta-field. Returns 0 on success.
    int task_create(const std::string& task_id, const std::string& kind,
                    const std::string& payload_json, int64_t now_ms,
                    uint64_t fencing_token = 0) {
        return cf_task_create(handle_,
            task_id.c_str(), kind.c_str(),
            reinterpret_cast<const uint8_t*>(payload_json.data()), payload_json.size(),
            now_ms, fencing_token);
    }

    /// Transition a task status. new_status: "start"|"pause"|"resume"|"complete"|"fail".
    /// Returns true on success.
    bool task_transition(const std::string& task_id, const std::string& new_status,
                         int64_t now_ms, uint64_t fencing_token = 0) {
        return cf_task_transition(handle_,
            task_id.c_str(), new_status.c_str(),
            now_ms, fencing_token) == 0;
    }

    /// List tasks by kind. Returns JSON array. active_only=true filters non-terminal.
    std::string task_list(const std::string& kind = "", bool active_only = false) {
        std::vector<uint8_t> buf(262144);
        size_t written = 0;
        const char* kind_ptr = kind.empty() ? nullptr : kind.c_str();
        int r = cf_task_list(handle_, kind_ptr, active_only ? 1 : 0,
                             buf.data(), buf.size(), &written);
        if (r != 0 || written == 0) return "[]";
        return std::string(reinterpret_cast<char*>(buf.data()), written);
    }

    // ── Domain Event Log ─────────────────────────────────────────────────────

    /// Emit a domain event. Returns the assigned event_id.
    /// fencing_token=0 means intent/report tier; non-zero means authoritative (leader-only).
    uint64_t emit_event(const std::string& domain, const std::string& kind,
                        const std::string& entity_id, const std::string& payload_json,
                        uint64_t fencing_token = 0, const std::string& realm = "") {
        uint64_t event_id = 0;
        int r = cf_emit_event(handle_,
            domain.c_str(), kind.c_str(), entity_id.c_str(),
            reinterpret_cast<const uint8_t*>(payload_json.data()), payload_json.size(),
            realm.empty() ? nullptr : realm.c_str(),
            fencing_token, &event_id);
        if (r != 0) throw std::runtime_error(last_error());
        return event_id;
    }

    /// Iterate the event log from from_seqno, invoking cb(op_json, seqno) for each entry.
    void iterate_log(uint64_t from_seqno,
                     std::function<void(const std::string& op_json, uint64_t seqno)> cb) {
        struct Ctx { std::function<void(const std::string&, uint64_t)>* fn; };
        Ctx ctx{&cb};
        cf_iterate_log(handle_, from_seqno,
            [](const uint8_t* op_json, size_t op_len, uint64_t seqno, void* raw) {
                auto* c = static_cast<Ctx*>(raw);
                (*c->fn)(std::string(reinterpret_cast<const char*>(op_json), op_len), seqno);
            }, &ctx);
    }

    /// Upsert a user model entity (key: entity_id, tag: entity_type).
    /// Returns 0 on success, negative on error.
    int user_model_upsert(const std::string& entity_id, const std::string& entity_type,
                          const std::string& payload_json, int64_t now_ms) {
        return cf_user_model_upsert(handle_,
            entity_id.c_str(), entity_type.c_str(),
            reinterpret_cast<const uint8_t*>(payload_json.data()), payload_json.size(),
            now_ms);
    }

    // ── Theme Management ─────────────────────────────────────────────────────

    /// List all themes. Returns JSON string array.
    std::string theme_list() {
        std::vector<uint8_t> buf(65536);
        size_t written = 0;
        cf_theme_list(handle_, buf.data(), buf.size(), &written);
        return std::string(reinterpret_cast<char*>(buf.data()), written);
    }

    /// Get a single theme by ID. Returns JSON string or empty on not found.
    std::string theme_get(uint64_t theme_id) {
        std::vector<uint8_t> buf(65536);
        size_t written = 0;
        int r = cf_theme_get(handle_, theme_id, buf.data(), buf.size(), &written);
        if (r != 0 || written == 0) return "";
        return std::string(reinterpret_cast<char*>(buf.data()), written);
    }

    /// Get theme statistics. Returns JSON string.
    std::string theme_stats(const std::string& realm = "") {
        std::vector<uint8_t> buf(65536);
        size_t written = 0;
        const char* realm_ptr = realm.empty() ? nullptr : realm.c_str();
        cf_theme_stats(handle_, realm_ptr, buf.data(), buf.size(), &written);
        return std::string(reinterpret_cast<char*>(buf.data()), written);
    }

    /// Recall themes by embedding similarity. Returns JSON string array of {theme_id, score}.
    std::string theme_recall(const std::vector<float>& embedding,
                             size_t k,
                             const std::string& realm = "") {
        std::vector<uint8_t> buf(65536);
        size_t written = 0;
        const char* realm_ptr = realm.empty() ? nullptr : realm.c_str();
        cf_theme_recall(handle_,
            embedding.data(), embedding.size(),
            k, realm_ptr,
            buf.data(), buf.size(), &written);
        return std::string(reinterpret_cast<char*>(buf.data()), written);
    }

    /// Run theme maintenance (split/merge/reassign). Returns JSON result.
    std::string theme_maintain() {
        std::vector<uint8_t> buf(65536);
        size_t written = 0;
        cf_theme_maintain(handle_, buf.data(), buf.size(), &written);
        return std::string(reinterpret_cast<char*>(buf.data()), written);
    }

    /// Assign orphan memories to themes. Returns JSON with {assigned, remaining}.
    std::string theme_assign_orphans(size_t batch_size = 500,
                                     const std::string& realm = "") {
        std::vector<uint8_t> buf(65536);
        size_t written = 0;
        const char* realm_ptr = realm.empty() ? nullptr : realm.c_str();
        cf_theme_assign_orphans(handle_, batch_size, realm_ptr,
                                buf.data(), buf.size(), &written);
        return std::string(reinterpret_cast<char*>(buf.data()), written);
    }

    /// Get the payload of the most recent domain event matching domain+kind+entity_id.
    /// Currently supports domain="user_model"; kind matches entity_type.
    /// Returns the JSON payload string if found, or nullopt if not found or on error.
    std::optional<std::string> get_latest_event(
        const std::string& domain,
        const std::string& kind,
        const std::string& entity_id) {
        std::vector<uint8_t> buf(65536);
        size_t written = 0;
        int rc = cf_get_latest_event(handle_, domain.c_str(), kind.c_str(), entity_id.c_str(),
                                     buf.data(), buf.size(), &written);
        if (rc == 0 && written > 0) {
            return std::string(reinterpret_cast<char*>(buf.data()), written);
        }
        return std::nullopt;
    }

    // ── Phase 0: New query/management methods ───────────────────────────────

    /// 1. Filtered recall — returns JSON array of matching memories.
    std::string recall_filtered(const std::string& kind = "", const std::string& realm = "",
                                float min_confidence = 0.0f, float min_strength = 0.0f,
                                size_t limit = 50) {
        std::vector<uint8_t> buf(262144);
        size_t written = 0;
        const char* kind_ptr = kind.empty() ? nullptr : kind.c_str();
        const char* realm_ptr = realm.empty() ? nullptr : realm.c_str();
        int r = cf_recall_filtered(handle_, kind_ptr, realm_ptr,
                                   min_confidence, min_strength, limit,
                                   buf.data(), buf.size(), &written);
        if (r != 0 && r != -2) return "[]";
        return std::string(reinterpret_cast<char*>(buf.data()), written);
    }

    /// 2. Paginated memory listing sorted by strength/recency/confidence.
    std::string list_memories(const std::string& kind = "", const std::string& realm = "",
                              const std::string& sort_by = "recency",
                              size_t limit = 50, size_t offset = 0) {
        std::vector<uint8_t> buf(262144);
        size_t written = 0;
        const char* kind_ptr = kind.empty() ? nullptr : kind.c_str();
        const char* realm_ptr = realm.empty() ? nullptr : realm.c_str();
        int r = cf_list_memories(handle_, kind_ptr, realm_ptr,
                                 sort_by.c_str(), limit, offset,
                                 buf.data(), buf.size(), &written);
        if (r != 0 && r != -2) return "[]";
        return std::string(reinterpret_cast<char*>(buf.data()), written);
    }

    /// 3. Aggregate memory stats. Returns JSON with count_by_kind, avg_confidence, etc.
    std::string memory_stats(const std::string& realm = "") {
        std::vector<uint8_t> buf(65536);
        size_t written = 0;
        const char* realm_ptr = realm.empty() ? nullptr : realm.c_str();
        int r = cf_memory_stats(handle_, realm_ptr, buf.data(), buf.size(), &written);
        if (r != 0) return "{}";
        return std::string(reinterpret_cast<char*>(buf.data()), written);
    }

    /// 4. Get single task by ID. Returns JSON string, or empty on not found.
    std::string task_get(const std::string& task_id) {
        std::vector<uint8_t> buf(65536);
        size_t written = 0;
        int r = cf_task_get(handle_, task_id.c_str(), buf.data(), buf.size(), &written);
        if (r != 0 || written == 0) return "";
        return std::string(reinterpret_cast<char*>(buf.data()), written);
    }

    /// 5. Update task payload. Returns true on success.
    bool task_update_payload(const std::string& task_id,
                             const std::string& payload_json, int64_t now_ms) {
        return cf_task_update_payload(handle_, task_id.c_str(),
                                      payload_json.c_str(), now_ms) == 0;
    }

    /// 6. List sessions as JSON array. active_only=true filters by active status.
    std::string session_list(bool active_only = false) {
        std::vector<uint8_t> buf(65536);
        size_t written = 0;
        int r = cf_session_list(handle_, active_only ? 1 : 0,
                                buf.data(), buf.size(), &written);
        if (r != 0) return "[]";
        return std::string(reinterpret_cast<char*>(buf.data()), written);
    }

    /// 7. List transcripts as JSON array, most recent first.
    std::string transcript_list(size_t limit = 50) {
        std::vector<uint8_t> buf(65536);
        size_t written = 0;
        int r = cf_transcript_list(handle_, limit, buf.data(), buf.size(), &written);
        if (r != 0) return "[]";
        return std::string(reinterpret_cast<char*>(buf.data()), written);
    }

    /// 8. Get memory metadata by ID. Returns JSON string, or empty on not found.
    std::string get_memory_metadata(uint64_t memory_id) {
        std::vector<uint8_t> buf(4096);
        size_t written = 0;
        int r = cf_get_memory_metadata(handle_, memory_id, buf.data(), buf.size(), &written);
        if (r != 0 || written == 0) return "";
        return std::string(reinterpret_cast<char*>(buf.data()), written);
    }

    /// 9. Update memory kind field. Returns true on success.
    bool update_memory_kind(uint64_t memory_id, const std::string& new_kind) {
        return cf_update_memory_kind(handle_, memory_id, new_kind.c_str()) == 0;
    }

    /// 10. List all triplets where entity is subject OR object. Returns JSON string.
    std::string list_triplets_for_entity(const std::string& entity, size_t limit = 100) {
        std::vector<char> buf(65536);
        size_t written = 0;
        int r = cf_list_triplets_for_entity(handle_, entity.c_str(), limit,
                                            buf.data(), buf.size(), &written);
        if (r != 0) return "[]";
        return std::string(buf.data(), written);
    }

    // ── DuckDB removal: new query/management methods ────────────────────────

    std::string list_code_files(const std::string& project = "") {
        std::vector<uint8_t> buf(131072);
        size_t written = 0;
        const char* proj_ptr = project.empty() ? nullptr : project.c_str();
        int r = cf_list_code_files(handle_, proj_ptr, buf.data(), buf.size(), &written);
        if (r != 0 && r != -2) return "[]";
        return std::string(reinterpret_cast<char*>(buf.data()), written);
    }

    int clear_project(const std::string& project) {
        return cf_clear_project(handle_, project.c_str());
    }

    int set_symbol_description(uint64_t symbol_id, const std::string& desc) {
        return cf_set_symbol_description(handle_, symbol_id, desc.c_str(), desc.size());
    }

    int update_memory_content(uint64_t id, const std::string& content,
                              const std::vector<float>& embedding = {}) {
        const float* emb_ptr = embedding.empty() ? nullptr : embedding.data();
        return cf_update_memory_content(handle_, id,
            reinterpret_cast<const uint8_t*>(content.data()), content.size(),
            emb_ptr, embedding.size());
    }

    std::string realm_list() {
        std::vector<uint8_t> buf(32768);
        size_t written = 0;
        int r = cf_realm_list(handle_, buf.data(), buf.size(), &written);
        if (r != 0) return "[]";
        return std::string(reinterpret_cast<char*>(buf.data()), written);
    }

    std::vector<FieldRecallHit> recall_by_kind(const std::string& kind, size_t limit) {
        std::vector<uint8_t> buf(131072);
        size_t written = 0;
        int r = cf_recall_by_kind(handle_, kind.c_str(), limit, buf.data(), buf.size(), &written);
        if (r != 0 && r != -2) return {};
        auto json_str = std::string(reinterpret_cast<char*>(buf.data()), written);
        try {
            auto arr = nlohmann::json::parse(json_str, nullptr, false);
            if (arr.is_discarded() || !arr.is_array()) return {};
            std::vector<FieldRecallHit> hits;
            for (const auto& item : arr) {
                FieldRecallHit h;
                h.memory_id = item.value("id", uint64_t(0));
                h.confidence = item.value("confidence", 0.0f);
                h.content = item.value("content", std::string{});
                h.kind = kind;
                hits.push_back(h);
            }
            return hits;
        } catch (...) { return {}; }
    }

    /// Record co-retrieval for Hebbian association strengthening.
    void record_co_retrieval(const std::vector<uint64_t>& memory_ids,
                             float base_assoc_delta = 0.05f) {
        if (memory_ids.size() < 2) return;
        int64_t ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        cf_record_recall_batch(handle_,
            memory_ids.data(), memory_ids.size(),
            nullptr, 0, 1.0f, 0, ts_ms, base_assoc_delta);
    }

    /// Get association edges for a memory as JSON string.
    std::string get_assoc_edges(uint64_t memory_id, size_t limit = 20) {
        char buf[32768];
        size_t written = 0;
        int r = cf_get_assoc_edges(handle_, memory_id, limit,
                                    buf, sizeof(buf), &written);
        if (r != 0) return "[]";
        return std::string(buf, written);
    }

    /// Batch-fetch embeddings for multiple memories as JSON.
    std::string get_memory_embeddings_batch(const std::vector<uint64_t>& ids) {
        if (ids.empty()) return "{}";
        char buf[1 << 20]; // 1MB
        size_t written = 0;
        int r = cf_get_memory_embeddings_batch(handle_,
            ids.data(), ids.size(), buf, sizeof(buf), &written);
        if (r != 0) return "{}";
        return std::string(buf, written);
    }

    CfHandle* handle() const { return handle_; }

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
            h.access_count   = buf[i].access_count;

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
