#pragma once
#ifdef CHITTA_FIELD_AVAILABLE

#include <string>
#include <memory>
#include <functional>
#include <duckdb.hpp>
#include <nlohmann/json.hpp>
#include "field_store.hpp"

namespace chitta {

// SoulProjection: Local DuckDB read model rebuilt from chitta-field log.
// - Replays domain events (Session/Transcript/Task/UserModel/Theme/Analytics)
//   into local DuckDB tables for fast SQL queries.
// - Watermark-based: tracks last replayed seqno, only replays new events.
// - Rebuildable: drop tables and replay from seqno=0 to rebuild from scratch.
class SoulProjection {
public:
    explicit SoulProjection(duckdb::DuckDB& db, FieldStore* field);

    // Replay all events since last watermark into DuckDB tables.
    // Returns number of events replayed.
    size_t replay_incremental();

    // Drop all projection tables and replay from seqno=0.
    void rebuild_full();

    // Get current watermark (last replayed seqno).
    uint64_t watermark() const { return watermark_; }

    // Execute a read-only SQL query against the projection DuckDB.
    // Returns a JSON-encoded result (same format as DuckDBStore::execute_sql_query).
    // {"success": bool, "columns": [...], "rows": [[...], ...], "error": "..."}
    std::string execute_sql(const std::string& sql);

private:
    duckdb::DuckDB& db_;
    FieldStore* field_;
    uint64_t watermark_ = 0;

    void ensure_schema();
    void apply_event(const std::string& op_json, uint64_t seqno);

    // Per-domain event handlers
    void apply_session_event(const nlohmann::json& op, uint64_t seqno);
    void apply_transcript_event(const nlohmann::json& op, uint64_t seqno);
    void apply_task_event(const nlohmann::json& op, uint64_t seqno);
    void apply_user_model_event(const nlohmann::json& op, uint64_t seqno);
    void apply_theme_event(const nlohmann::json& op, uint64_t seqno);
    void apply_analytics_event(const nlohmann::json& op, uint64_t seqno);

    void exec(const std::string& sql);
    std::unique_ptr<duckdb::Connection> conn_;
};

} // namespace chitta
#endif // CHITTA_FIELD_AVAILABLE
