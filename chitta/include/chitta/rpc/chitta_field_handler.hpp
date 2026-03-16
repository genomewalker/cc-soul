#pragma once
// ChittaFieldHandler: chitta-field primary RPC layer.
//
// Write ops: write only to FieldStore (chitta-field is the primary store).
// Read ops: served entirely from chitta-field (SPAF recall).
//
// NOTE: This file is included at the BOTTOM of duckdb_handler.hpp, after
// DuckDBRpcHandler and DuckDBToolResult are fully defined. Do not include
// duckdb_handler.hpp from here — just forward-declare what we need.

#include <chitta/field_store.hpp>
#include <chitta/soul_projection.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace chitta {

using json = nlohmann::json;

// DuckDBRpcHandler and DuckDBToolResult are defined in duckdb_handler.hpp,
// which includes this file. Both are fully defined by the time any method
// body here is instantiated by the compiler (they are all inline in this header,
// and this header is included at the bottom of the duckdb_handler.hpp
// translation unit after the class definition completes).
class DuckDBRpcHandler;

class ChittaFieldHandler {
public:
    explicit ChittaFieldHandler(FieldStore* field_store)
        : field_store_(field_store)
    {}

    void set_soul_projection(SoulProjection* sp) { soul_projection_ = sp; }

    FieldStore* field_store() const { return field_store_; }

    // Returns true when this handler should intercept the named tool.
    static bool is_field_routable(const std::string& method) {
        static const std::unordered_set<std::string> routable = {
            "remember",
            "recall",
            "strengthen",
            "weaken",
            "forget",
            "touch",
            "connect",
            "hybrid_recall",
            "smart_recall",
            "recall_temporal",
            "recall_keyword",
            "theme_list",
            "theme_get",
            "theme_stats",
            "theme_recall",
            "theme_maintain",
            "theme_assign_orphans",
        };
        return routable.count(method) > 0;
    }

    // Dispatch to the appropriate method. Returns DuckDBToolResult so callers
    // in DuckDBRpcHandler::handle() can use make_tool_response() directly.
    DuckDBToolResult dispatch(const std::string& method, const json& params) {
        if (method == "remember")        return handle_remember(params);
        if (method == "strengthen")      return handle_strengthen(params);
        if (method == "weaken")          return handle_weaken(params);
        if (method == "forget")          return handle_forget(params);
        if (method == "touch")           return handle_touch(params);
        if (method == "connect")         return handle_connect(params);
        if (method == "recall")          return handle_recall(params);
        if (method == "recall_temporal") return handle_recall_temporal(params);
        if (method == "recall_keyword")  return handle_recall_keyword(params);
        if (method == "hybrid_recall")   return handle_hybrid_recall(params);
        if (method == "smart_recall")    return handle_smart_recall(params);
        if (method == "sql_query")       return handle_sql_query(params);
        if (method == "theme_list")          return handle_theme_list(params);
        if (method == "theme_get")           return handle_theme_get(params);
        if (method == "theme_stats")         return handle_theme_stats(params);
        if (method == "theme_recall")        return handle_theme_recall(params);
        if (method == "theme_maintain")      return handle_theme_maintain(params);
        if (method == "theme_assign_orphans") return handle_theme_assign_orphans(params);
        return DuckDBToolResult::error("ChittaFieldHandler: unknown method: " + method);
    }

private:
    FieldStore*     field_store_;
    SoulProjection* soul_projection_ = nullptr;

    // ── Helpers ──────────────────────────────────────────────────────────────

    // Extract a uint64_t memory ID from "id" param (integer or numeric string).
    static uint64_t extract_id(const json& params) {
        if (!params.contains("id")) return 0;
        const auto& v = params["id"];
        if (v.is_number_integer()) return static_cast<uint64_t>(v.get<int64_t>());
        if (v.is_string()) {
            try { return std::stoull(v.get<std::string>()); } catch (...) {}
        }
        return 0;
    }

    // Extract float embedding from a JSON array under key.
    static std::vector<float> extract_embedding(const json& params,
                                                const std::string& key = "embedding") {
        std::vector<float> emb;
        if (!params.contains(key) || !params[key].is_array()) return emb;
        emb.reserve(params[key].size());
        for (const auto& v : params[key]) {
            if (v.is_number()) emb.push_back(v.get<float>());
        }
        return emb;
    }

    // Convert a FieldRecallHit vector to the same JSON array format as tool_recall.
    static json hits_to_results_json(const std::vector<FieldRecallHit>& hits) {
        json arr = json::array();
        for (const auto& h : hits) {
            arr.push_back({
                {"id",         std::to_string(h.memory_id)},
                {"relevance",  h.score},
                {"similarity", h.semantic_score},
                {"type",       h.kind.empty() ? "episode" : h.kind},
                {"text",       h.content},
                {"realm",      h.realm},
                {"confidence", h.confidence},
            });
        }
        return arr;
    }

    // Merge two JSON result arrays, deduplicating by id (take max relevance score).
    static json merge_results(const json& a, const json& b) {
        std::unordered_map<std::string, json> seen;
        auto insert = [&](const json& arr) {
            for (const auto& entry : arr) {
                std::string id = entry.value("id", "");
                if (id.empty()) continue;
                auto it = seen.find(id);
                if (it == seen.end()) {
                    seen[id] = entry;
                } else {
                    float cur  = it->second.value("relevance", 0.0f);
                    float cand = entry.value("relevance", 0.0f);
                    if (cand > cur) it->second = entry;
                }
            }
        };
        insert(a);
        insert(b);

        json merged = json::array();
        for (auto& [id, entry] : seen) merged.push_back(entry);
        std::sort(merged.begin(), merged.end(), [](const json& x, const json& y) {
            return x.value("relevance", 0.0f) > y.value("relevance", 0.0f);
        });
        return merged;
    }

    // ── Write ops (FieldOnly) ─────────────────────────────────────────────────

    DuckDBToolResult handle_remember(const json& params) {
        std::string kind    = params.value("type", "episode");
        std::string realm   = params.value("realm", "brahman");
        std::string content = params.value("content", "");
        float confidence    = params.value("confidence", 1.0f);
        float decay_rate    = params.value("decay_rate", 0.001f);

        std::vector<float> embedding = extract_embedding(params);

        uint64_t field_id = 0;
        if (!content.empty() && field_store_) {
            try {
                field_id = field_store_->remember(kind, realm, content, embedding, confidence, decay_rate);
            } catch (...) {}
        }

        std::string id_str = std::to_string(field_id);
        return DuckDBToolResult::ok("Stored memory #" + id_str,
            {{"content", json::array({{{"type", "text"}, {"text", "Stored memory #" + id_str}}})}});
    }

    DuckDBToolResult handle_strengthen(const json& params) {
        uint64_t id  = extract_id(params);
        float amount = params.value("amount", 0.1f);

        if (id != 0 && field_store_) {
            try { field_store_->strengthen(id, amount); } catch (...) {}
        }

        return DuckDBToolResult::ok("OK");
    }

    DuckDBToolResult handle_weaken(const json& params) {
        uint64_t id  = extract_id(params);
        float amount = params.value("amount", 0.1f);

        if (id != 0 && field_store_) {
            try { field_store_->weaken(id, amount); } catch (...) {}
        }

        return DuckDBToolResult::ok("OK");
    }

    DuckDBToolResult handle_forget(const json& params) {
        uint64_t id = extract_id(params);

        if (id != 0 && field_store_) {
            try { field_store_->forget(id); } catch (...) {}
        }

        return DuckDBToolResult::ok("OK");
    }

    DuckDBToolResult handle_touch(const json& params) {
        uint64_t id = extract_id(params);

        if (id != 0 && field_store_) {
            try { field_store_->touch(id); } catch (...) {}
        }

        return DuckDBToolResult::ok("OK");
    }

    DuckDBToolResult handle_connect(const json& params) {
        std::string subject   = params.value("subject", "");
        std::string predicate = params.value("predicate", "");
        std::string object    = params.value("object", "");

        if (!subject.empty() && !predicate.empty() && !object.empty() && field_store_) {
            try {
                field_store_->add_triplet(subject, predicate, object);
            } catch (...) {}
        }

        if (params.contains("src_id") && params.contains("dst_id") && field_store_) {
            try {
                uint64_t src      = static_cast<uint64_t>(params["src_id"].get<int64_t>());
                uint64_t dst      = static_cast<uint64_t>(params["dst_id"].get<int64_t>());
                uint8_t edge_type = static_cast<uint8_t>(params.value("edge_type", 3));
                float weight      = params.value("weight", 0.5f);
                field_store_->add_edge(src, dst, edge_type, weight);
            } catch (...) {}
        }

        return DuckDBToolResult::ok("OK");
    }

    // ── sql_query: SoulProjection read model ─────────────────────────────────

    DuckDBToolResult handle_sql_query(const json& params);  // defined after SoulProjection is known

    // ── Read ops (chitta-field) ───────────────────────────────────────────────

    DuckDBToolResult handle_recall(const json& params) {
        if (!field_store_) return DuckDBToolResult::error("chitta-field store unavailable");

        std::vector<float> embedding = extract_embedding(params);
        if (embedding.empty()) {
            return DuckDBToolResult::error("recall requires embedding; chitta-field has no embedder");
        }

        size_t k          = static_cast<size_t>(params.value("limit", 10));
        std::string realm = params.value("realm", "");

        try {
            auto hits = field_store_->recall(embedding, k, realm);
            json results_json = hits_to_results_json(hits);

            std::ostringstream ss;
            ss << "Found " << hits.size() << " results";
            if (!realm.empty()) ss << " in realm '" << realm << "'";
            ss << ":\n";
            for (const auto& r : results_json) {
                int pct = static_cast<int>(r.value("relevance", 0.0f) * 100);
                ss << "[" << pct << "%] [" << r.value("type", "?") << "] "
                   << r.value("text", "").substr(0, 100) << "\n";
            }

            return DuckDBToolResult::ok(ss.str(), {{"results", results_json}, {"realm", realm}});
        } catch (const std::exception& e) {
            return DuckDBToolResult::error(std::string("chitta-field recall error: ") + e.what());
        }
    }

    DuckDBToolResult handle_recall_temporal(const json& params) {
        if (!field_store_) return DuckDBToolResult::error("chitta-field store unavailable");

        int64_t start_ms  = params.value("start_ms",  0LL);
        int64_t end_ms    = params.value("end_ms",    0LL);
        size_t  limit     = static_cast<size_t>(params.value("limit", 20));
        std::string realm = params.value("realm", "");

        try {
            auto hits = field_store_->recall_temporal(start_ms, end_ms, limit, realm);
            json results_json = hits_to_results_json(hits);

            std::ostringstream ss;
            ss << "Found " << hits.size() << " memories";
            if (!realm.empty()) ss << " in realm '" << realm << "'";
            ss << ":\n";
            for (const auto& r : results_json) {
                ss << "[" << r.value("type", "?") << "] "
                   << r.value("text", "").substr(0, 100) << "\n";
            }

            return DuckDBToolResult::ok(ss.str(), {
                {"results", results_json},
                {"count",   hits.size()},
                {"realm",   realm}
            });
        } catch (const std::exception& e) {
            return DuckDBToolResult::error(
                std::string("chitta-field temporal recall error: ") + e.what());
        }
    }

    DuckDBToolResult handle_recall_keyword(const json& params) {
        if (!field_store_) {
            return DuckDBToolResult::error("recall_keyword requires chitta-field store");
        }

        std::string query = params.value("query", "");
        if (query.empty()) return DuckDBToolResult::error("query parameter required");
        size_t k = static_cast<size_t>(params.value("limit", 10));

        try {
            auto hits = field_store_->recall_keyword(query, k);
            json results_json = hits_to_results_json(hits);

            std::ostringstream ss;
            ss << "Found " << hits.size() << " keyword results for '" << query << "':\n";
            for (const auto& r : results_json) {
                int pct = static_cast<int>(r.value("relevance", 0.0f) * 100);
                ss << "[" << pct << "%] " << r.value("text", "").substr(0, 100) << "\n";
            }

            return DuckDBToolResult::ok(ss.str(), {{"results", results_json}});
        } catch (const std::exception& e) {
            return DuckDBToolResult::error(
                std::string("chitta-field keyword recall error: ") + e.what());
        }
    }

    DuckDBToolResult handle_hybrid_recall(const json& params) {
        if (!field_store_) return DuckDBToolResult::error("chitta-field store unavailable");

        std::vector<float> embedding = extract_embedding(params);
        std::string query  = params.value("query", "");
        size_t k           = static_cast<size_t>(params.value("limit", 20));
        std::string realm  = params.value("realm", "");

        if (embedding.empty()) {
            return DuckDBToolResult::error("hybrid_recall requires embedding; chitta-field has no embedder");
        }

        try {
            json semantic = hits_to_results_json(field_store_->recall(embedding, k, realm));
            json merged   = semantic;

            if (!query.empty()) {
                json keyword = hits_to_results_json(field_store_->recall_keyword(query, k));
                merged = merge_results(semantic, keyword);
            }

            if (merged.size() > k) merged.erase(merged.begin() + static_cast<int>(k), merged.end());

            std::ostringstream ss;
            ss << "Hybrid recall: " << merged.size() << " results\n";
            for (const auto& r : merged) {
                int pct = static_cast<int>(r.value("relevance", 0.0f) * 100);
                ss << "[" << pct << "%] " << r.value("text", "").substr(0, 100) << "\n";
            }

            return DuckDBToolResult::ok(ss.str(), {{"results", merged}, {"realm", realm}});
        } catch (const std::exception& e) {
            return DuckDBToolResult::error(
                std::string("chitta-field hybrid recall error: ") + e.what());
        }
    }

    DuckDBToolResult handle_smart_recall(const json& params) {
        return handle_hybrid_recall(params);
    }

    // ── Theme ops (chitta-field) ──────────────────────────────────────────────

    DuckDBToolResult handle_theme_list(const json& params) {
        if (!field_store_) return DuckDBToolResult::error("chitta-field store unavailable");

        std::string raw = field_store_->theme_list();

        std::ostringstream ss;
        try {
            auto j = nlohmann::json::parse(raw);
            ss << "Themes (" << j.size() << "):\n";
            for (auto& t : j) {
                ss << "  [" << t.value("theme_id", 0ULL) << "] "
                   << t.value("name", "?")
                   << " (" << t.value("member_count", 0) << " memories"
                   << ", coherence=" << std::fixed << std::setprecision(2)
                   << t.value("coherence", 1.0) << ")\n";
            }
            return DuckDBToolResult::ok(ss.str(), {{"themes", j}});
        } catch (...) {
            return DuckDBToolResult::ok(raw);
        }
    }

    DuckDBToolResult handle_theme_get(const json& params) {
        if (!field_store_) return DuckDBToolResult::error("chitta-field store unavailable");

        uint64_t theme_id = 0;
        if (params.contains("theme_id") && params["theme_id"].is_number_integer())
            theme_id = static_cast<uint64_t>(params["theme_id"].get<int64_t>());
        else if (params.contains("id") && params["id"].is_number_integer())
            theme_id = static_cast<uint64_t>(params["id"].get<int64_t>());
        if (theme_id == 0) return DuckDBToolResult::error("theme_id required");

        std::string raw = field_store_->theme_get(theme_id);
        if (raw.empty()) return DuckDBToolResult::error("Theme not found: " + std::to_string(theme_id));

        try {
            auto j = nlohmann::json::parse(raw);
            return DuckDBToolResult::ok(j.dump(2), j);
        } catch (...) {
            return DuckDBToolResult::ok(raw);
        }
    }

    DuckDBToolResult handle_theme_stats(const json& params) {
        if (!field_store_) return DuckDBToolResult::error("chitta-field store unavailable");

        std::string realm = params.value("realm", "");
        std::string raw = field_store_->theme_stats(realm);

        try {
            auto j = nlohmann::json::parse(raw);
            std::ostringstream ss;
            ss << "Theme Organization Stats:\n"
               << "  Total themes: " << j.value("total_themes", 0) << "\n"
               << "  Total memberships: " << j.value("total_memberships", 0) << "\n"
               << "  Orphan memories: " << j.value("orphan_count", 0) << "\n"
               << "  Avg theme size: " << std::fixed << std::setprecision(1)
               << j.value("avg_size", 0.0) << "\n"
               << "  Avg coherence: " << std::setprecision(2)
               << j.value("avg_coherence", 1.0) << "\n";
            return DuckDBToolResult::ok(ss.str(), j);
        } catch (...) {
            return DuckDBToolResult::ok(raw);
        }
    }

    DuckDBToolResult handle_theme_recall(const json& params) {
        if (!field_store_) return DuckDBToolResult::error("chitta-field store unavailable");

        std::vector<float> embedding = extract_embedding(params);
        if (embedding.empty())
            return DuckDBToolResult::error("theme_recall requires embedding; chitta-field has no embedder");

        size_t k = static_cast<size_t>(params.value("k", 5));
        std::string realm = params.value("realm", "");

        std::string raw = field_store_->theme_recall(embedding, k, realm);
        try {
            auto j = nlohmann::json::parse(raw);
            return DuckDBToolResult::ok(j.dump(2), {{"results", j}});
        } catch (...) {
            return DuckDBToolResult::ok(raw);
        }
    }

    DuckDBToolResult handle_theme_maintain(const json& params) {
        if (!field_store_) return DuckDBToolResult::error("chitta-field store unavailable");

        std::string raw = field_store_->theme_maintain();
        try {
            auto j = nlohmann::json::parse(raw);
            std::ostringstream ss;
            ss << "Theme Maintenance Complete:\n"
               << "  Themes split: " << j.value("themes_split", 0) << "\n"
               << "  Themes merged: " << j.value("themes_merged", 0) << "\n"
               << "  Memories reassigned: " << j.value("memories_reassigned", 0) << "\n";
            return DuckDBToolResult::ok(ss.str(), j);
        } catch (...) {
            return DuckDBToolResult::ok(raw);
        }
    }

    DuckDBToolResult handle_theme_assign_orphans(const json& params) {
        if (!field_store_) return DuckDBToolResult::error("chitta-field store unavailable");

        size_t batch_size = static_cast<size_t>(params.value("batch_size", 500));
        std::string realm = params.value("realm", "");

        std::string raw = field_store_->theme_assign_orphans(batch_size, realm);
        try {
            auto j = nlohmann::json::parse(raw);
            size_t assigned  = j.value("assigned", 0ULL);
            size_t remaining = j.value("remaining", 0ULL);
            std::ostringstream ss;
            ss << "Assigned " << assigned << " orphan memories to themes\n"
               << "Remaining orphans: " << remaining << "\n";
            return DuckDBToolResult::ok(ss.str(), j);
        } catch (...) {
            return DuckDBToolResult::ok(raw);
        }
    }
};

// ── handle_sql_query definition ───────────────────────────────────────────────
// Replay incremental then execute against SoulProjection.

inline DuckDBToolResult ChittaFieldHandler::handle_sql_query(const json& params) {
    if (!soul_projection_) {
        return DuckDBToolResult::error("sql_query requires SoulProjection (not initialized)");
    }

    soul_projection_->replay_incremental();

    std::string sql = params.value("query", "");
    if (sql.empty()) {
        return DuckDBToolResult::error("Query is required");
    }

    std::string result_json = soul_projection_->execute_sql(sql);

    try {
        json r = json::parse(result_json);
        if (!r.value("success", false)) {
            return DuckDBToolResult::error("SoulProjection SQL error: " + r.value("error", "unknown"));
        }

        const json& columns = r["columns"];
        const json& rows    = r["rows"];

        std::ostringstream ss;
        ss << "| ";
        for (const auto& col : columns) ss << col.get<std::string>() << " | ";
        ss << "\n|";
        for (size_t i = 0; i < columns.size(); ++i) ss << "---|";
        ss << "\n";

        json rows_json = json::array();
        for (const auto& row : rows) {
            json row_obj;
            ss << "| ";
            for (size_t i = 0; i < columns.size() && i < row.size(); ++i) {
                std::string v = row[i].get<std::string>();
                row_obj[columns[i].get<std::string>()] = v;
                ss << v << " | ";
            }
            ss << "\n";
            rows_json.push_back(row_obj);
        }

        return DuckDBToolResult::ok(ss.str(), {{"rows", rows_json}, {"count", rows_json.size()}});
    } catch (const std::exception& e) {
        return DuckDBToolResult::error(std::string("sql_query parse error: ") + e.what());
    }
}

} // namespace chitta
