#include "chitta/soul_projection.hpp"
#ifdef CHITTA_FIELD_AVAILABLE

#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace chitta {

// Convert a JSON value that represents Vec<u8> (serde_json serializes as int array)
// back to a UTF-8 string for storage.
static std::string bytes_to_string(const nlohmann::json& j) {
    if (j.is_string()) {
        return j.get<std::string>();
    }
    if (j.is_array()) {
        std::string result;
        result.reserve(j.size());
        for (const auto& byte_val : j) {
            result.push_back(static_cast<char>(byte_val.get<uint8_t>()));
        }
        return result;
    }
    return "";
}

// Escape a string for safe SQL embedding (replace ' with '').
static std::string sql_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '\'') out += "''";
        else out += c;
    }
    return out;
}

SoulProjection::SoulProjection(duckdb::DuckDB& db, FieldStore* field)
    : db_(db), field_(field)
{
    conn_ = std::make_unique<duckdb::Connection>(db_);
    ensure_schema();
    // Load persisted watermark
    try {
        auto res = conn_->Query("SELECT last_seqno FROM cf_projection_watermark WHERE id=1");
        if (res && !res->HasError() && res->RowCount() == 1) {
            watermark_ = res->GetValue(0, 0).GetValue<uint64_t>();
        }
    } catch (...) {}
}

void SoulProjection::exec(const std::string& sql) {
    auto res = conn_->Query(sql);
    if (res->HasError()) {
        std::cerr << "[SoulProjection] SQL error: " << res->GetError()
                  << "\n  SQL: " << sql.substr(0, 200) << "\n";
    }
}

void SoulProjection::ensure_schema() {
    exec(R"(
        CREATE TABLE IF NOT EXISTS cf_sessions (
            session_id TEXT PRIMARY KEY,
            kind TEXT,
            realm TEXT,
            started_at_ms BIGINT,
            last_heartbeat_ms BIGINT,
            status TEXT DEFAULT 'active',
            seqno BIGINT
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS cf_transcripts (
            transcript_id TEXT PRIMARY KEY,
            session_id TEXT,
            progress_pct REAL DEFAULT 0,
            turn_count INTEGER DEFAULT 0,
            seqno BIGINT
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS cf_transcript_turns (
            id BIGINT,
            transcript_id TEXT,
            role TEXT,
            content TEXT,
            ts_ms BIGINT,
            seqno BIGINT
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS cf_tasks (
            task_id TEXT PRIMARY KEY,
            kind TEXT,
            status TEXT DEFAULT 'pending',
            payload_json TEXT,
            created_at_ms BIGINT,
            updated_at_ms BIGINT,
            fencing_token BIGINT DEFAULT 0,
            seqno BIGINT
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS cf_user_model (
            entity_id TEXT PRIMARY KEY,
            kind TEXT,
            payload_json TEXT,
            updated_at_ms BIGINT,
            seqno BIGINT
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS cf_themes (
            theme_id TEXT PRIMARY KEY,
            name TEXT,
            centroid_json TEXT,
            member_count INTEGER DEFAULT 0,
            seqno BIGINT
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS cf_analytics (
            id BIGINT,
            kind TEXT,
            entity_id TEXT,
            payload_json TEXT,
            ts_ms BIGINT,
            seqno BIGINT
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS cf_projection_watermark (
            id INTEGER PRIMARY KEY,
            last_seqno BIGINT DEFAULT 0
        )
    )");

    exec("INSERT OR IGNORE INTO cf_projection_watermark (id, last_seqno) VALUES (1, 0)");
}

void SoulProjection::apply_session_event(const nlohmann::json& op, uint64_t seqno) {
    try {
        const std::string session_id = sql_escape(op.value("session_id", ""));
        const std::string kind       = op.value("kind", "");
        const std::string realm      = sql_escape(op.value("realm", ""));
        const int64_t     ts_ms      = op.value("ts_ms", int64_t(0));

        if (kind == "register") {
            std::ostringstream sql;
            sql << "INSERT OR REPLACE INTO cf_sessions "
                << "(session_id, kind, realm, started_at_ms, last_heartbeat_ms, status, seqno) VALUES ("
                << "'" << session_id << "',"
                << "'" << sql_escape(kind) << "',"
                << "'" << realm << "',"
                << ts_ms << ","
                << ts_ms << ","
                << "'active',"
                << seqno
                << ")";
            exec(sql.str());
        } else if (kind == "heartbeat") {
            std::ostringstream sql;
            sql << "UPDATE cf_sessions SET last_heartbeat_ms=" << ts_ms
                << ", seqno=" << seqno
                << " WHERE session_id='" << session_id << "'";
            exec(sql.str());
        } else if (kind == "deregister") {
            std::ostringstream sql;
            sql << "UPDATE cf_sessions SET status='closed', seqno=" << seqno
                << " WHERE session_id='" << session_id << "'";
            exec(sql.str());
        }
    } catch (const std::exception& e) {
        std::cerr << "[SoulProjection] apply_session_event error: " << e.what() << "\n";
    }
}

void SoulProjection::apply_transcript_event(const nlohmann::json& op, uint64_t seqno) {
    try {
        const std::string session_id    = sql_escape(op.value("session_id", ""));
        const std::string kind          = op.value("kind", "");
        const int64_t     ts_ms         = op.value("ts_ms", int64_t(0));
        const std::string payload_str   = bytes_to_string(op.value("payload_json", nlohmann::json::array()));

        if (kind == "register") {
            // payload carries transcript_id
            std::string transcript_id;
            try {
                auto payload = nlohmann::json::parse(payload_str);
                transcript_id = sql_escape(payload.value("transcript_id", ""));
            } catch (...) {
                transcript_id = sql_escape(payload_str);
            }
            if (transcript_id.empty()) transcript_id = session_id;

            std::ostringstream sql;
            sql << "INSERT OR REPLACE INTO cf_transcripts "
                << "(transcript_id, session_id, progress_pct, turn_count, seqno) VALUES ("
                << "'" << transcript_id << "',"
                << "'" << session_id << "',"
                << "0, 0, " << seqno
                << ")";
            exec(sql.str());
        } else if (kind == "update_progress") {
            float progress = 0.0f;
            try {
                auto payload = nlohmann::json::parse(payload_str);
                progress = payload.value("progress_pct", 0.0f);
            } catch (...) {}

            std::ostringstream sql;
            sql << "UPDATE cf_transcripts SET progress_pct=" << progress
                << ", seqno=" << seqno
                << " WHERE session_id='" << session_id << "'";
            exec(sql.str());
        } else if (kind == "add_turn") {
            std::string transcript_id;
            std::string role;
            std::string content;
            int64_t turn_id = static_cast<int64_t>(seqno);

            try {
                auto payload = nlohmann::json::parse(payload_str);
                transcript_id = sql_escape(payload.value("transcript_id", ""));
                role          = sql_escape(payload.value("role", ""));
                content       = sql_escape(payload.value("content", ""));
                turn_id       = payload.value("id", turn_id);
            } catch (...) {}

            if (transcript_id.empty()) transcript_id = session_id;

            std::ostringstream sql_turn;
            sql_turn << "INSERT INTO cf_transcript_turns "
                     << "(id, transcript_id, role, content, ts_ms, seqno) VALUES ("
                     << turn_id << ","
                     << "'" << transcript_id << "',"
                     << "'" << role << "',"
                     << "'" << content << "',"
                     << ts_ms << ","
                     << seqno
                     << ")";
            exec(sql_turn.str());

            std::ostringstream sql_count;
            sql_count << "UPDATE cf_transcripts SET turn_count=turn_count+1, seqno=" << seqno
                      << " WHERE transcript_id='" << transcript_id << "'";
            exec(sql_count.str());
        }
        // kind == "create_episode" → no-op for now
    } catch (const std::exception& e) {
        std::cerr << "[SoulProjection] apply_transcript_event error: " << e.what() << "\n";
    }
}

void SoulProjection::apply_task_event(const nlohmann::json& op, uint64_t seqno) {
    try {
        const std::string task_id      = sql_escape(op.value("task_id", ""));
        const std::string task_type    = sql_escape(op.value("task_type", ""));
        const std::string kind         = op.value("kind", "");
        const int64_t     ts_ms        = op.value("ts_ms", int64_t(0));
        const uint64_t    fencing      = op.value("fencing_token", uint64_t(0));
        const std::string payload_str  = sql_escape(bytes_to_string(
            op.value("payload_json", nlohmann::json::array())));

        if (kind == "create") {
            std::ostringstream sql;
            sql << "INSERT OR REPLACE INTO cf_tasks "
                << "(task_id, kind, status, payload_json, created_at_ms, updated_at_ms, fencing_token, seqno) VALUES ("
                << "'" << task_id << "',"
                << "'" << task_type << "',"
                << "'pending',"
                << "'" << payload_str << "',"
                << ts_ms << ","
                << ts_ms << ","
                << fencing << ","
                << seqno
                << ")";
            exec(sql.str());
        } else if (kind == "start" || kind == "pause" || kind == "resume"
                || kind == "complete" || kind == "fail") {
            std::ostringstream sql;
            sql << "UPDATE cf_tasks SET status='" << sql_escape(kind) << "'"
                << ", updated_at_ms=" << ts_ms
                << ", fencing_token=" << fencing
                << ", seqno=" << seqno
                << " WHERE task_id='" << task_id << "'";
            exec(sql.str());
        }
    } catch (const std::exception& e) {
        std::cerr << "[SoulProjection] apply_task_event error: " << e.what() << "\n";
    }
}

void SoulProjection::apply_user_model_event(const nlohmann::json& op, uint64_t seqno) {
    try {
        const std::string entity_id   = sql_escape(op.value("entity_id", ""));
        const std::string entity_type = sql_escape(op.value("entity_type", ""));
        const std::string kind        = op.value("kind", "");
        const int64_t     ts_ms       = op.value("ts_ms", int64_t(0));
        const std::string payload_str = sql_escape(bytes_to_string(
            op.value("payload_json", nlohmann::json::array())));

        // All kinds (upsert/observe/progress/complete) → upsert
        std::ostringstream sql;
        sql << "INSERT OR REPLACE INTO cf_user_model "
            << "(entity_id, kind, payload_json, updated_at_ms, seqno) VALUES ("
            << "'" << entity_id << "',"
            << "'" << entity_type << "',"
            << "'" << payload_str << "',"
            << ts_ms << ","
            << seqno
            << ")";
        exec(sql.str());
    } catch (const std::exception& e) {
        std::cerr << "[SoulProjection] apply_user_model_event error: " << e.what() << "\n";
    }
}

void SoulProjection::apply_theme_event(const nlohmann::json& op, uint64_t seqno) {
    try {
        const uint64_t    theme_id_int = op.value("theme_id", uint64_t(0));
        const std::string theme_id     = std::to_string(theme_id_int);
        const std::string kind         = op.value("kind", "");
        const int64_t     ts_ms        = op.value("ts_ms", int64_t(0));
        const std::string payload_str  = bytes_to_string(
            op.value("payload_json", nlohmann::json::array()));

        if (kind == "create") {
            std::string name;
            std::string centroid_json;
            try {
                auto payload = nlohmann::json::parse(payload_str);
                name         = sql_escape(payload.value("name", ""));
                centroid_json = sql_escape(payload.value("centroid_json", ""));
            } catch (...) {}

            std::ostringstream sql;
            sql << "INSERT OR REPLACE INTO cf_themes "
                << "(theme_id, name, centroid_json, member_count, seqno) VALUES ("
                << "'" << theme_id << "',"
                << "'" << name << "',"
                << "'" << centroid_json << "',"
                << "0,"
                << seqno
                << ")";
            exec(sql.str());
        } else if (kind == "update_centroid") {
            const std::string centroid = sql_escape(payload_str);
            std::ostringstream sql;
            sql << "UPDATE cf_themes SET centroid_json='" << centroid << "'"
                << ", seqno=" << seqno
                << " WHERE theme_id='" << theme_id << "'";
            exec(sql.str());
        } else if (kind == "assign_member") {
            std::ostringstream sql;
            sql << "UPDATE cf_themes SET member_count=member_count+1, seqno=" << seqno
                << " WHERE theme_id='" << theme_id << "'";
            exec(sql.str());
        } else if (kind == "remove_member") {
            std::ostringstream sql;
            sql << "UPDATE cf_themes SET member_count=MAX(0, member_count-1), seqno=" << seqno
                << " WHERE theme_id='" << theme_id << "'";
            exec(sql.str());
        }
    } catch (const std::exception& e) {
        std::cerr << "[SoulProjection] apply_theme_event error: " << e.what() << "\n";
    }
}

void SoulProjection::apply_analytics_event(const nlohmann::json& op, uint64_t seqno) {
    try {
        const uint64_t    event_id    = op.value("event_id", uint64_t(0));
        const std::string kind        = sql_escape(op.value("kind", ""));
        const std::string session_id  = sql_escape(op.value("session_id", ""));
        const int64_t     ts_ms       = op.value("ts_ms", int64_t(0));
        const std::string payload_str = sql_escape(bytes_to_string(
            op.value("payload_json", nlohmann::json::array())));

        std::ostringstream sql;
        sql << "INSERT INTO cf_analytics "
            << "(id, kind, entity_id, payload_json, ts_ms, seqno) VALUES ("
            << static_cast<int64_t>(event_id) << ","
            << "'" << kind << "',"
            << "'" << session_id << "',"
            << "'" << payload_str << "',"
            << ts_ms << ","
            << seqno
            << ")";
        exec(sql.str());
    } catch (const std::exception& e) {
        std::cerr << "[SoulProjection] apply_analytics_event error: " << e.what() << "\n";
    }
}

void SoulProjection::apply_event(const std::string& op_json, uint64_t seqno) {
    try {
        auto root = nlohmann::json::parse(op_json);

        if (root.contains("SessionEvent")) {
            apply_session_event(root["SessionEvent"], seqno);
        } else if (root.contains("TranscriptEvent")) {
            apply_transcript_event(root["TranscriptEvent"], seqno);
        } else if (root.contains("TaskEvent")) {
            apply_task_event(root["TaskEvent"], seqno);
        } else if (root.contains("UserModelEvent")) {
            apply_user_model_event(root["UserModelEvent"], seqno);
        } else if (root.contains("ThemeEvent")) {
            apply_theme_event(root["ThemeEvent"], seqno);
        } else if (root.contains("AnalyticsEvent")) {
            apply_analytics_event(root["AnalyticsEvent"], seqno);
        }
        // Other op variants (PutPayload, AddTriplet, etc.) are not projection events.

        watermark_ = seqno;
        std::ostringstream sql;
        sql << "UPDATE cf_projection_watermark SET last_seqno=" << seqno << " WHERE id=1";
        exec(sql.str());
    } catch (const std::exception& e) {
        std::cerr << "[SoulProjection] apply_event parse error at seqno=" << seqno
                  << ": " << e.what() << "\n";
    }
}

size_t SoulProjection::replay_incremental() {
    size_t replayed = 0;
    try {
        field_->iterate_log(watermark_, [&](const std::string& op_json, uint64_t seqno) {
            apply_event(op_json, seqno);
            replayed++;
        });
    } catch (const std::exception& e) {
        std::cerr << "[SoulProjection] replay_incremental error: " << e.what() << "\n";
    }
    return replayed;
}

void SoulProjection::rebuild_full() {
    try {
        // Drop all projection tables
        for (const char* tbl : {
            "cf_sessions", "cf_transcripts", "cf_transcript_turns",
            "cf_tasks", "cf_user_model", "cf_themes", "cf_analytics",
            "cf_projection_watermark"
        }) {
            exec(std::string("DROP TABLE IF EXISTS ") + tbl);
        }
        watermark_ = 0;
        ensure_schema();
        replay_incremental();
    } catch (const std::exception& e) {
        std::cerr << "[SoulProjection] rebuild_full error: " << e.what() << "\n";
    }
}

std::string SoulProjection::execute_sql(const std::string& sql) {
    nlohmann::json result;
    try {
        auto res = conn_->Query(sql);
        if (!res || res->HasError()) {
            result = {{"success", false}, {"error", res ? res->GetError() : "null result"}};
            return result.dump();
        }

        nlohmann::json columns = nlohmann::json::array();
        for (size_t i = 0; i < res->ColumnCount(); ++i) {
            columns.push_back(res->ColumnName(i));
        }

        nlohmann::json rows = nlohmann::json::array();
        for (size_t row = 0; row < res->RowCount(); ++row) {
            nlohmann::json row_arr = nlohmann::json::array();
            for (size_t col = 0; col < res->ColumnCount(); ++col) {
                auto val = res->GetValue(col, row);
                row_arr.push_back(val.ToString());
            }
            rows.push_back(row_arr);
        }

        result = {{"success", true}, {"columns", columns}, {"rows", rows}, {"error", ""}};
    } catch (const std::exception& e) {
        result = {{"success", false}, {"error", std::string(e.what())}};
    }
    return result.dump();
}

} // namespace chitta
#endif // CHITTA_FIELD_AVAILABLE
