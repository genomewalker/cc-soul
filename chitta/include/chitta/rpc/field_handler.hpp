#pragma once
// FieldRpcHandler: RPC handler backed by FieldStore + VakYantra.

#include "../field_store.hpp"
#include "../vak.hpp"
#include "../code_intel.hpp"
#include "../mind/subconscious.hpp"
#include "../sadhana/sadhana_manager.hpp"
#include "../version.hpp"
#include "../daemon_config.hpp"
#include "../ingester.hpp"
#include "../wiki_export.hpp"
#include "../embedding_export.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <optional>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <regex>
#include <array>
#include <numeric>
#include <unistd.h>
#include <glob.h>
#include <iostream>

namespace chitta {

using json = nlohmann::json;

struct ToolResult {
    bool is_error = false;
    std::string text;
    json structured;
    static ToolResult ok(const std::string& t, const json& s = json()) { return {false, t, s}; }
    static ToolResult error(const std::string& msg) { return {true, msg, json()}; }
};

inline std::string display_path(const std::string& file_path) {
    size_t last_slash = file_path.rfind('/');
    if (last_slash == std::string::npos) return file_path;
    std::string basename = file_path.substr(last_slash + 1);
    if (last_slash > 0) {
        size_t prev_slash = file_path.rfind('/', last_slash - 1);
        if (prev_slash != std::string::npos) return file_path.substr(prev_slash + 1);
    }
    return basename;
}

class FieldRpcHandler {
public:
    explicit FieldRpcHandler(FieldStore* fs, VakYantra* yantra)
        : field_store_(fs), yantra_(yantra) {
        register_tools();
    }

    void set_subconscious(Subconscious* s) { subconscious_ = s; }
    void set_sadhana_manager(SadhanaManager* sm) { sadhana_manager_ = sm; }
    void set_queue_stats(std::atomic<size_t>* count, std::atomic<size_t>* fails,
                         const std::string& failed_path) {
        queue_count_ = count; queue_fail_count_ = fails; failed_queue_path_ = failed_path;
    }
    using RecallCallback = std::function<void(const std::vector<uint64_t>&, int)>;
    void set_recall_callback(RecallCallback cb) { recall_callback_ = std::move(cb); }
    FieldStore* get_field_store() const { return field_store_; }
    VakYantra* get_yantra() const { return yantra_; }

    std::string get_distill_model() const {
        std::lock_guard<std::mutex> lk(distill_mutex_);
        return distill_model_;
    }
    void set_distill_model(const std::string& m) {
        std::lock_guard<std::mutex> lk(distill_mutex_);
        distill_model_ = m;
    }
    bool get_distill_enabled() const { return distill_enabled_.load(); }
    void set_distill_enabled(bool e) { distill_enabled_.store(e); }

    void run_belief_maintenance(float stale_strength_threshold = 0.1f,
                                int stale_days = 30,
                                float dup_threshold = 0.97f,
                                size_t max_dups = 5) {
        size_t demoted = 0, contradictions_archived = 0, dups_merged = 0;

        // 1. Stale belief demotion: archive Active memories with decayed strength below threshold
        {
            std::string raw = field_store_->list_memories("", "", "recency", 2000, 0);
            try {
                auto arr = json::parse(raw);
                auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                for (const auto& m : arr) {
                    uint64_t id = m.value("id", uint64_t(0));
                    if (id == 0) continue;
                    std::string meta_json = field_store_->get_memory_metadata(id);
                    if (meta_json.empty()) continue;
                    auto meta = json::parse(meta_json, nullptr, false);
                    if (meta.is_discarded()) continue;
                    std::string status = meta.value("status", "Active");
                    if (status != "Active") continue;
                    float strength = meta.value("strength", 1.0f);
                    float decay_rate = meta.value("decay_rate", 0.001f);
                    int64_t last_ms = meta.value("last_strengthened_ms", int64_t(0));
                    if (last_ms == 0) last_ms = meta.value("created_at_ms", int64_t(0));
                    if (last_ms == 0) continue;
                    double age_days = static_cast<double>(now_ms - last_ms) / 86400000.0;
                    if (age_days < stale_days) continue;
                    double effective = strength * std::exp(-decay_rate * age_days);
                    if (effective < stale_strength_threshold) {
                        field_store_->set_memory_status(id, 3); // Archived
                        ++demoted;
                    }
                }
            } catch (...) {}
        }

        // 2. Contradiction resolution: archive Contradicted memories whose contradictors are gone
        {
            std::string raw = field_store_->list_memories("", "", "recency", 2000, 0);
            try {
                auto arr = json::parse(raw);
                for (const auto& m : arr) {
                    uint64_t id = m.value("id", uint64_t(0));
                    if (id == 0) continue;
                    std::string meta_json = field_store_->get_memory_metadata(id);
                    if (meta_json.empty()) continue;
                    auto meta = json::parse(meta_json, nullptr, false);
                    if (meta.is_discarded()) continue;
                    std::string status = meta.value("status", "Active");
                    if (status != "Contradicted") continue;
                    auto conflicts = field_store_->get_conflicts(id);
                    bool any_active = false;
                    for (uint64_t cid : conflicts) {
                        std::string cmeta_json = field_store_->get_memory_metadata(cid);
                        if (cmeta_json.empty()) continue;
                        auto cmeta = json::parse(cmeta_json, nullptr, false);
                        if (cmeta.is_discarded()) continue;
                        std::string cs = cmeta.value("status", "Active");
                        if (cs == "Active" || cs == "Verified") { any_active = true; break; }
                    }
                    if (!any_active) {
                        field_store_->set_memory_status(id, 3); // Archived
                        ++contradictions_archived;
                    }
                }
            } catch (...) {}
        }

        // 3. Duplicate consolidation: merge highest-similarity pairs
        {
            auto pairs = find_dup_pairs("", 200, dup_threshold);
            if (pairs.size() > max_dups) pairs.resize(max_dups);
            std::unordered_set<uint64_t> processed;
            for (const auto& p : pairs) {
                if (processed.count(p.a_id) || processed.count(p.b_id)) continue;
                uint64_t weaker_id = (p.a_score >= p.b_score) ? p.b_id : p.a_id;
                try {
                    field_store_->forget(weaker_id);
                    processed.insert(weaker_id);
                    ++dups_merged;
                } catch (...) {}
            }
        }

        std::cerr << "[belief_maintenance] demoted=" << demoted
                  << " contradictions_archived=" << contradictions_archived
                  << " dups_merged=" << dups_merged << "\n";
    }

    json handle(const json& request) {
        std::string method = request.value("method", "");
        json params = request.value("params", json::object());
        auto id = request.value("id", json());

        if (method == "tools/list") {
            return make_response(id, tool_list());
        }
        if (method == "tools/call") {
            std::string name = params.value("name", "");
            json args = params.value("arguments", json::object());
            auto it = handlers_.find(name);
            if (it == handlers_.end()) {
                return make_error(id, -32601, "Unknown tool: " + name);
            }
            auto result = it->second(args);
            return make_tool_response(id, result);
        }
        return make_error(id, -32601, "Unknown method: " + method);
    }

private:
    FieldStore* field_store_;
    VakYantra* yantra_;
    Subconscious* subconscious_ = nullptr;
    SadhanaManager* sadhana_manager_ = nullptr;
    std::atomic<size_t>* queue_count_ = nullptr;
    std::atomic<size_t>* queue_fail_count_ = nullptr;
    std::string failed_queue_path_;
    RecallCallback recall_callback_;

    mutable std::mutex distill_mutex_;
    std::string distill_model_ = "github-copilot/gpt-5-mini";
    std::atomic<bool> distill_enabled_{true};

    std::vector<json> tools_;
    std::unordered_map<std::string, std::function<ToolResult(const json&)>> handlers_;
    std::unordered_map<std::string, std::string> tool_visibility_;

    // ── Embedding helpers ───────────────────────────────────────────────────

    std::vector<float> embed_text(const std::string& text) {
        if (!yantra_ || text.empty()) return {};
        Artha a = yantra_->transform(text);
        return a.nu.data;
    }

    std::vector<float> embed_query(const std::string& query) {
        if (!yantra_ || query.empty()) return {};
        Artha a = yantra_->transform(query, EmbedMode::Query);
        return a.nu.data;
    }

    // ── ID extraction helpers ───────────────────────────────────────────────

    static uint64_t extract_id(const json& params, const std::string& key = "id") {
        if (!params.contains(key)) return 0;
        const auto& v = params[key];
        if (v.is_number_integer()) return static_cast<uint64_t>(v.get<int64_t>());
        if (v.is_string()) {
            try { return std::stoull(v.get<std::string>()); } catch (...) {}
        }
        return 0;
    }

    static std::pair<int64_t, std::string> parse_id(const json& params, const std::string& key = "id") {
        int64_t db_id = 0;
        std::string id_str;
        if (params.contains(key)) {
            const auto& val = params[key];
            if (val.is_number_integer()) {
                db_id = val.get<int64_t>();
                id_str = std::to_string(db_id);
            } else if (val.is_string()) {
                id_str = val.get<std::string>();
                try { db_id = std::stoll(id_str); } catch (...) { db_id = 0; }
            }
        }
        return {db_id, id_str};
    }

    // ── Result conversion helpers ───────────────────────────────────────────

    static json hits_to_results_json(const std::vector<FieldRecallHit>& hits, bool explain = false) {
        json arr = json::array();
        for (const auto& h : hits) {
            json entry = {
                {"id",         std::to_string(h.memory_id)},
                {"relevance",  h.score},
                {"similarity", h.semantic_score},
                {"type",       h.kind.empty() ? "episode" : h.kind},
                {"text",       h.content},
                {"realm",      h.realm},
                {"confidence", h.confidence},
            };
            if (explain) {
                entry["explain"] = {
                    {"semantic_weight",  h.semantic_weight},
                    {"status_mul",       h.status_mul},
                    {"epistemic_mul",    h.epistemic_mul},
                    {"strength_factor",  h.strength_factor},
                };
            }
            arr.push_back(std::move(entry));
        }
        return arr;
    }

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

    // ── Query helpers ───────────────────────────────────────────────────────

    static float category_to_confidence(const std::string& category) {
        if (category == "correction") return 0.95f;
        if (category == "preference") return 0.90f;
        if (category == "solution")   return 0.90f;
        if (category == "milestone")  return 0.90f;
        if (category == "decision")   return 0.85f;
        if (category == "failure")    return 0.85f;
        if (category == "gotcha")     return 0.85f;
        if (category == "episode")    return 0.70f;
        return 0.80f;
    }

    static std::string_view trim_view(std::string_view s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
        return s;
    }

    static bool starts_with_ci(std::string_view s, std::string_view prefix) {
        if (s.size() < prefix.size()) return false;
        for (size_t i = 0; i < prefix.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(s[i])) !=
                std::tolower(static_cast<unsigned char>(prefix[i]))) return false;
        }
        return true;
    }

    static bool looks_like_code_query(const std::string& q) {
        std::string_view s = trim_view(q);
        if (s.empty()) return false;
        if (starts_with_ci(s, "how ") || starts_with_ci(s, "why ") || starts_with_ci(s, "what ") ||
            starts_with_ci(s, "where ") || starts_with_ci(s, "explain ") || starts_with_ci(s, "describe ") ||
            starts_with_ci(s, "find ") || starts_with_ci(s, "show ")) return false;
        if (s.find("::") != std::string_view::npos || s.find("->") != std::string_view::npos ||
            s.find('(') != std::string_view::npos || s.find(')') != std::string_view::npos ||
            s.find('_') != std::string_view::npos || s.find('/') != std::string_view::npos ||
            s.find('\\') != std::string_view::npos || s.find('#') != std::string_view::npos ||
            s.find('.') != std::string_view::npos) return true;
        bool has_space = s.find_first_of(" \t\n") != std::string_view::npos;
        if (!has_space && s.size() <= 80) {
            size_t ok = 0;
            for (char c : s) {
                unsigned char uc = static_cast<unsigned char>(c);
                if (std::isalnum(uc) || c == ':' || c == '.' || c == '-') ok++;
            }
            if (ok == s.size()) return true;
        }
        return false;
    }

    struct ExpandedQuery {
        std::string lex;
        std::string vec;
        std::string hyde;
    };

    static ExpandedQuery expand_query(const std::string& query) {
        ExpandedQuery eq;
        eq.vec = query;
        eq.hyde = "[memory about] " + query;

        static const std::unordered_set<std::string> STOP_WORDS = {
            "a","an","the","is","are","was","were","be","been","being","have","has","had",
            "do","does","did","will","would","could","should","may","might","shall","can",
            "to","of","in","for","on","with","at","by","from","as","into","through",
            "i","me","my","we","our","you","your","he","him","his","she","her","it","its",
            "they","them","their","what","which","who","this","that","these","those",
            "not","only","just","about","so","than","too","very","all","both","each",
            "more","most","other","some","such","no","own","same","here","there",
            "when","where","why","how","then","once","am","out","off","over","under"
        };

        std::ostringstream lex_ss;
        std::istringstream iss(query);
        std::string word;
        bool first = true;
        while (iss >> word) {
            std::string lower;
            lower.reserve(word.size());
            for (char c : word) {
                unsigned char uc = static_cast<unsigned char>(c);
                if (std::isalnum(uc)) lower += static_cast<char>(std::tolower(uc));
            }
            if (!lower.empty() && STOP_WORDS.find(lower) == STOP_WORDS.end()) {
                if (!first) lex_ss << " ";
                lex_ss << lower;
                first = false;
            }
        }
        eq.lex = lex_ss.str().empty() ? query : lex_ss.str();
        return eq;
    }

    // ── SSL helpers ─────────────────────────────────────────────────────────

    static bool is_ssl_format(const std::string& content) {
        if (content.empty()) return false;
        if (content[0] == '[' && content.find(']') != std::string::npos) return true;
        if (content.rfind("[LEARN]", 0) == 0) return true;
        if (content.rfind("[ε]", 0) == 0) return true;
        if (content.find("\xe2\x86\x92") != std::string::npos) return true;  // UTF-8 →
        return false;
    }

    static std::string to_ssl_format(const std::string& content,
                                      const std::string& domain = "note",
                                      const std::string& location = "") {
        if (is_ssl_format(content)) return content;
        std::string result = "[" + domain + "] ";
        size_t newline = content.find('\n');
        if (newline != std::string::npos && newline < 80) {
            result += content.substr(0, newline);
            if (!location.empty()) result += " @" + location;
            result += "\n" + content.substr(newline + 1);
        } else if (content.size() > 80) {
            result += content.substr(0, 80) + "...";
            if (!location.empty()) result += " @" + location;
            result += "\n" + content;
        } else {
            result += content;
            if (!location.empty()) result += " @" + location;
        }
        return result;
    }

    // ── Timestamp parsing ───────────────────────────────────────────────────

    std::optional<int64_t> parse_timestamp_str(const std::string& ts) {
        if (ts.empty()) return std::nullopt;
        std::tm tm = {};
        int year, month, day, hour = 0, min = 0, sec = 0;
        if (std::sscanf(ts.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &min, &sec) >= 3 ||
            std::sscanf(ts.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec) >= 3 ||
            std::sscanf(ts.c_str(), "%d-%d-%d", &year, &month, &day) == 3) {
            tm.tm_year = year - 1900;
            tm.tm_mon = month - 1;
            tm.tm_mday = day;
            tm.tm_hour = hour;
            tm.tm_min = min;
            tm.tm_sec = sec;
            tm.tm_isdst = -1;
            std::time_t time = std::mktime(&tm);
            if (time == -1) return std::nullopt;
            return static_cast<int64_t>(time) * 1000;
        }
        try {
            int64_t val = std::stoll(ts);
            if (val < 946684800000LL) val *= 1000;
            return val;
        } catch (...) {
            return std::nullopt;
        }
    }

    // ── Misc helpers ────────────────────────────────────────────────────────

    static std::vector<std::string> extract_terms(const std::string& query) {
        std::vector<std::string> terms;
        std::istringstream iss(query);
        std::string word;
        while (iss >> word) {
            std::string clean;
            for (char c : word) {
                if (std::isalnum(c)) clean += std::tolower(c);
            }
            if (clean.length() >= 3 &&
                clean != "the" && clean != "and" && clean != "for" &&
                clean != "that" && clean != "with" && clean != "how" &&
                clean != "what" && clean != "does" && clean != "can") {
                terms.push_back(clean);
            }
        }
        return terms;
    }

    static std::string get_session_id() {
        const char* env = std::getenv("CLAUDE_SESSION_ID");
        return env ? env : "";
    }

    static std::string get_session_id(const json& params) {
        if (params.contains("session_id") && params["session_id"].is_string()) {
            std::string sid = params["session_id"].get<std::string>();
            if (!sid.empty()) return sid;
        }
        return get_session_id();
    }

    // Aspect to kind mapping
    static inline const std::unordered_map<std::string, std::vector<std::string>> ASPECT_KINDS = {
        {"preferences", {"preference"}},
        {"corrections", {"correction"}},
        {"insights", {"insight", "wisdom"}},
        {"failures", {"failure"}},
        {"decisions", {"decision"}},
        {"approaches", {"approach"}},
        {"milestones", {"milestone"}},
        {"goals", {"goal"}},
        {"habits", {"habit"}},
        {"beliefs", {"belief", "invariant"}},
        {"wisdom", {"wisdom", "insight"}},
        {"code", {"symbol", "function", "class", "file", "dependency"}},
        {"gaps", {"gap", "question"}},
    };

    // ── RPC response helpers ────────────────────────────────────────────────

    json tool_list() {
        json filtered = json::array();
        for (const auto& tool : tools_) {
            auto name = tool["name"].get<std::string>();
            auto it = tool_visibility_.find(name);
            std::string vis = (it != tool_visibility_.end()) ? it->second : "default";
            if (vis != "internal") filtered.push_back(tool);
        }
        return {{"tools", filtered}};
    }

    json make_response(const json& id, const json& result) {
        return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    }

    json make_error(const json& id, int code, const std::string& msg) {
        return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", msg}}}};
    }

    json make_tool_response(const json& id, const ToolResult& result) {
        json content = json::array();
        content.push_back({{"type", "text"}, {"text", result.text}});
        return {{"jsonrpc", "2.0"}, {"id", id}, {"result", {
            {"content", content}, {"isError", result.is_error}, {"structured", result.structured}
        }}};
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Handler file includes — tool implementations (must be before register_tools)
    // ═══════════════════════════════════════════════════════════════════════

    #include "handlers/field_memory_recall.hpp"
    #include "handlers/field_memory_ops.hpp"
    #include "handlers/field_memory_structured.hpp"
    #include "handlers/field_code_intel.hpp"
    #include "handlers/field_system.hpp"
    #include "handlers/field_session.hpp"
    #include "handlers/field_distill.hpp"
    #include "handlers/field_misc.hpp"
    #include "handlers/field_contradiction.hpp"
    #include "handlers/field_operator.hpp"
    #include "handlers/ledger.hpp"
    #include "handlers/long_task.hpp"
    #include "handlers/compact.hpp"
    #include "handlers/drift_recon.hpp"
    #include "handlers/drift_5w.hpp"
    #include "handlers/drift_consolidation.hpp"
    #include "handlers/drift_recall.hpp"
    #include "handlers/drift_probe.hpp"

    // ═══════════════════════════════════════════════════════════════════════
    // register_tools() — all tool schemas and handler bindings
    // ═══════════════════════════════════════════════════════════════════════

    void register_tools() {
        // ── Memory tools ────────────────────────────────────────────────────
        tools_.push_back({
            {"name", "remember"},
            {"description", "Store text in memory with optional tags and realm"},
            {"inputSchema", {{"type", "object"},
                {"properties", {
                    {"content", {{"type", "string"}, {"description", "Text to remember"}}},
                    {"type", {{"type", "string"}, {"description", "Node type (wisdom, insight, signal, episode)"}}},
                    {"confidence", {{"type", "number"}, {"description", "Initial confidence 0-1 (default: 0.8)"}}},
                    {"tags", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Optional tags"}}},
                    {"realm", {{"type", "string"}, {"description", "Primary realm (default: brahman)"}}},
                    {"visibility", {{"type", "integer"}, {"description", "0=Private, 1=Shared, 2=Global (default: 0)"}}},
                    {"shared_realms", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Additional realms"}}}
                }}, {"required", {"content"}}
            }}
        });
        handlers_["remember"] = [this](const json& p) { return tool_remember(p); };

        tools_.push_back({
            {"name", "recall"},
            {"description", "Search memory by semantic similarity with realm filtering"},
            {"inputSchema", {{"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Search query"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default 10)"}}},
                    {"min_confidence", {{"type", "number"}, {"description", "Minimum confidence threshold"}}},
                    {"tag", {{"type", "string"}, {"description", "Filter by tag"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}},
                    {"include_global", {{"type", "boolean"}, {"description", "Include global memories (default: true)"}}},
                    {"separation_mode", {{"type", "boolean"}, {"description", "Diverse results via MMR (default: false)"}}},
                    {"gwt_mode", {{"type", "boolean"}, {"description", "Global Workspace Theory mode (default: false)"}}},
                    {"explain", {{"type", "boolean"}, {"description", "Include score decomposition per hit (default: false)"}}}
                }}, {"required", {"query"}}
            }}
        });
        handlers_["recall"] = [this](const json& p) { return tool_recall(p); };

        tools_.push_back({
            {"name", "recall_temporal"},
            {"description", "Search memories within a time window (defaults to last 7 days)"},
            {"inputSchema", {{"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Optional semantic search query"}}},
                    {"start", {{"type", "string"}, {"description", "Start date (ISO8601 or YYYY-MM-DD)"}}},
                    {"end", {{"type", "string"}, {"description", "End date"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default 20)"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}},
                    {"include_global", {{"type", "boolean"}, {"description", "Include global memories"}}}
                }}
            }}
        });
        handlers_["recall_temporal"] = [this](const json& p) { return tool_recall_temporal(p); };

        tools_.push_back({{"name","recall_keyword"},{"description","BM25 keyword search"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"query",{{"type","string"},{"description","Search query"}}},
                {"limit",{{"type","integer"},{"description","Max results (default 10)"}}},
                {"explain",{{"type","boolean"},{"description","Include score decomposition per hit (default: false)"}}}
            }},{"required",{"query"}}}}
        });
        handlers_["recall_keyword"] = [this](const json& p) { return tool_recall_keyword(p); };

        // Exploration primitives
        tools_.push_back({{"name","explore_recall"},{"description","Lightweight recall - titles/scores only"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"query",{{"type","string"},{"description","Search query"}}},
                {"limit",{{"type","integer"},{"description","Max results (default 10)"}}}
            }},{"required",{"query"}}}}
        });
        handlers_["explore_recall"] = [this](const json& p) { return tool_explore_recall(p); };

        tools_.push_back({{"name","explore_peek"},{"description","Get summary of a memory (first 200 chars)"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"},{"description","Memory ID"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["explore_peek"] = [this](const json& p) { return tool_explore_peek(p); };

        tools_.push_back({{"name","explore_expand"},{"description","Get full content of a memory"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"},{"description","Memory ID"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["explore_expand"] = [this](const json& p) { return tool_explore_expand(p); };

        tools_.push_back({{"name","explore_neighbors"},{"description","Get nodes connected via triplets"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"node",{{"type","string"},{"description","Node name"}}},
                {"direction",{{"type","string"},{"description","outgoing, incoming, or both"}}}
            }},{"required",{"node"}}}}
        });
        handlers_["explore_neighbors"] = [this](const json& p) { return tool_explore_neighbors(p); };

        // Graph tools
        tools_.push_back({{"name","connect"},{"description","Create a triplet relationship"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"subject",{{"type","string"},{"description","Subject entity"}}},
                {"predicate",{{"type","string"},{"description","Relationship type"}}},
                {"object",{{"type","string"},{"description","Object entity"}}}
            }},{"required",{"subject","predicate","object"}}}}
        });
        handlers_["connect"] = [this](const json& p) { return tool_connect(p); };

        tools_.push_back({{"name","query_graph"},{"description","Query triplets by subject or object"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"subject",{{"type","string"},{"description","Query by subject"}}},
                {"object",{{"type","string"},{"description","Query by object"}}}
            }}}}
        });
        handlers_["query_graph"] = [this](const json& p) { return tool_query(p); };

        tools_.push_back({{"name","query_triplets_temporal"},{"description","Query triplets at a point in time"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"subject",{{"type","string"}}},{"predicate",{{"type","string"}}},
                {"object",{{"type","string"}}},
                {"at_date",{{"type","string"},{"description","YYYY-MM-DD"}}},
                {"limit",{{"type","integer"}}}
            }}}}
        });
        handlers_["query_triplets_temporal"] = [this](const json& p) { return tool_query_triplets_temporal(p); };

        tools_.push_back({{"name","triplet_history"},{"description","Get history of a subject-predicate relationship"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"subject",{{"type","string"}}},{"predicate",{{"type","string"}}},
                {"limit",{{"type","integer"}}}
            }},{"required",{"subject","predicate"}}}}
        });
        handlers_["triplet_history"] = [this](const json& p) { return tool_triplet_history(p); };

        tools_.push_back({{"name","connect_temporal"},{"description","Create triplet with temporal validity"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"subject",{{"type","string"}}},{"predicate",{{"type","string"}}},{"object",{{"type","string"}}},
                {"valid_from",{{"type","string"}}},{"valid_to",{{"type","string"}}},
                {"context_date",{{"type","string"}}}
            }},{"required",{"subject","predicate","object"}}}}
        });
        handlers_["connect_temporal"] = [this](const json& p) { return tool_connect_temporal(p); };

        // Strength/forget
        tools_.push_back({{"name","strengthen"},{"description","Increase confidence of a memory"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"},{"description","Node ID"}}},
                {"amount",{{"type","number"},{"description","Amount (default 0.1)"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["strengthen"] = [this](const json& p) { return tool_strengthen(p); };

        tools_.push_back({{"name","weaken"},{"description","Decrease confidence of a memory"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"}}},{"amount",{{"type","number"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["weaken"] = [this](const json& p) { return tool_weaken(p); };

        tools_.push_back({{"name","forget"},{"description","Remove a memory"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"},{"description","Node ID to forget"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["forget"] = [this](const json& p) { return tool_forget(p); };

        tools_.push_back({{"name","batch_forget"},{"description","Delete multiple nodes by ID"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"ids",{{"type","array"},{"items",{{"type","string"}}}}},
                {"pattern",{{"type","string"}}}
            }}}}
        });
        handlers_["batch_forget"] = [this](const json& p) { return tool_batch_forget(p); };

        // Observe/Grow
        tools_.push_back({{"name","observe"},{"description","Store an observation/learning"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"category",{{"type","string"}}},{"title",{{"type","string"}}},
                {"content",{{"type","string"}}},{"tags",{{"type","string"}}},
                {"confidence",{{"type","number"}}}
            }},{"required",{"title","content"}}}}
        });
        handlers_["observe"] = [this](const json& p) { return tool_observe(p); };

        tools_.push_back({{"name","full_resonate"},{"description","Semantic search with full context"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"query",{{"type","string"}}},{"k",{{"type","integer"}}},
                {"realm",{{"type","string"}}},{"include_global",{{"type","boolean"}}},
                {"exclude_kinds",{{"type","array"},{"items",{{"type","string"}}}}},
                {"partnership_only",{{"type","boolean"}}},{"separation_mode",{{"type","boolean"}}}
            }},{"required",{"query"}}}}
        });
        handlers_["full_resonate"] = [this](const json& p) { return tool_full_resonate(p); };

        tools_.push_back({{"name","grow"},{"description","Add wisdom, belief, failure, aspiration, or dream"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"type",{{"type","string"}}},{"content",{{"type","string"}}},
                {"title",{{"type","string"}}},{"tags",{{"type","string"}}}
            }},{"required",{"type","content"}}}}
        });
        handlers_["grow"] = [this](const json& p) { return tool_grow(p); };

        tools_.push_back({{"name","get"},{"description","Get a node by ID"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["get"] = [this](const json& p) { return tool_get(p); };

        tools_.push_back({{"name","expand_memory"},{"description","Expand a memory to full hierarchical context"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"}}},{"depth",{{"type","integer"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["expand_memory"] = [this](const json& p) { return tool_expand_memory(p); };

        tools_.push_back({{"name","update"},{"description","Update node content"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"}}},{"content",{{"type","string"}}}
            }},{"required",{"id","content"}}}}
        });
        handlers_["update"] = [this](const json& p) { return tool_update(p); };

        tools_.push_back({{"name","query"},{"description","Query triplets with flexible filters"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"subject",{{"type","string"}}},{"predicate",{{"type","string"}}},
                {"object",{{"type","string"}}}
            }}}}
        });
        handlers_["query"] = [this](const json& p) { return tool_query(p); };

        tools_.push_back({{"name","tag"},{"description","Add or remove tags from a node"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"}}},{"add",{{"type","string"}}},{"remove",{{"type","string"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["tag"] = [this](const json& p) { return tool_tag(p); };

        // ── Code Intelligence tools ─────────────────────────────────────────
        tools_.push_back({{"name","extract_symbols"},{"description","Extract symbols from source file using tree-sitter"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"path",{{"type","string"},{"description","File path to analyze"}}}
            }},{"required",{"path"}}}}
        });
        handlers_["extract_symbols"] = [this](const json& p) { return tool_extract_symbols(p); };

        tools_.push_back({{"name","learn_codebase"},{"description","Learn codebase by extracting symbols. path can be a local directory or a remote git URL (https://github.com/..., git@github.com:...). Remote repos are shallow-cloned into a temp dir, indexed, then deleted."},
            {"inputSchema",{{"type","object"},{"properties",{
                {"path",{{"type","string"},{"description","Local path or remote git URL"}}},
                {"project",{{"type","string"},{"description","Project name (defaults to repo/dir name)"}}},
                {"branch",{{"type","string"},{"description","Branch, tag, or commit to clone (remote only)"}}},
                {"max_files",{{"type","integer"}}},{"exclude",{{"type","string"}}},
                {"incremental",{{"type","boolean"}}},{"force",{{"type","boolean"}}}
            }},{"required",{"path"}}}}
        });
        handlers_["learn_codebase"] = [this](const json& p) { return tool_learn_codebase(p); };

        tools_.push_back({{"name","find_symbol"},{"description","Search for symbols by name"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"name",{{"type","string"}}},{"kind",{{"type","string"}}}
            }},{"required",{"name"}}}}
        });
        handlers_["find_symbol"] = [this](const json& p) { return tool_find_symbol(p); };

        tools_.push_back({{"name","symbol_callers"},{"description","Find all symbols that call the given symbol"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"name",{{"type","string"}}},{"id",{{"type","integer"}}},
                {"kind",{{"type","string"}}},{"project",{{"type","string"}}}
            }}}}
        });
        handlers_["symbol_callers"] = [this](const json& p) { return tool_symbol_callers(p); };

        tools_.push_back({{"name","symbol_callees"},{"description","Find all symbols that the given symbol calls"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"name",{{"type","string"}}},{"id",{{"type","integer"}}},
                {"kind",{{"type","string"}}},{"project",{{"type","string"}}}
            }}}}
        });
        handlers_["symbol_callees"] = [this](const json& p) { return tool_symbol_callees(p); };

        tools_.push_back({{"name","read_symbol"},{"description","Read actual source code for a symbol"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"name",{{"type","string"}}},{"id",{{"type","integer"}}},
                {"kind",{{"type","string"}}},{"project",{{"type","string"}}}
            }}}}
        });
        handlers_["read_symbol"] = [this](const json& p) { return tool_read_symbol(p); };

        tools_.push_back({{"name","read_function"},{"description","Read source of a function/method by name"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"name",{{"type","string"}}},{"project",{{"type","string"}}}
            }},{"required",{"name"}}}}
        });
        handlers_["read_function"] = [this](const json& p) { return tool_read_function(p); };

        tools_.push_back({{"name","search_symbols"},{"description","Semantic search for code symbols"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"query",{{"type","string"}}},{"kind",{{"type","string"}}},
                {"limit",{{"type","integer"}}},{"project",{{"type","string"}}}
            }},{"required",{"query"}}}}
        });
        handlers_["search_symbols"] = [this](const json& p) { return tool_search_symbols(p); };

        tools_.push_back({{"name","code_context"},{"description","Get code context summary"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"path",{{"type","string"}}}
            }}}}
        });
        handlers_["code_context"] = [this](const json& p) { return tool_code_context(p); };

        tools_.push_back({{"name","smart_context"},{"description","Build intelligent context combining memories, code, and graph"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"task",{{"type","string"}}},{"mode",{{"type","string"}}},
                {"limit",{{"type","integer"}}},{"memories",{{"type","boolean"}}},
                {"code",{{"type","boolean"}}},{"neighbors",{{"type","boolean"}}},
                {"realm",{{"type","string"}}}
            }},{"required",{"task"}}}}
        });
        handlers_["smart_context"] = [this](const json& p) { return tool_smart_context(p); };

        tools_.push_back({{"name","codebase_overview"},{"description","Get full indexed codebase structure"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"project",{{"type","string"}}},{"format",{{"type","string"}}},
                {"include_callsites",{{"type","boolean"}}}
            }}}}
        });
        handlers_["codebase_overview"] = [this](const json& p) { return tool_codebase_overview(p); };

        tools_.push_back({{"name","clear_codebase"},{"description","Remove all code intel data for a project"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"project",{{"type","string"}}},{"dry_run",{{"type","boolean"}}}
            }},{"required",{"project"}}}}
        });
        handlers_["clear_codebase"] = [this](const json& p) { return tool_clear_codebase(p); };

        tools_.push_back({{"name","clear_triplets"},{"description","Delete triplets by subject pattern"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"pattern",{{"type","string"}}},{"dry_run",{{"type","boolean"}}}
            }},{"required",{"pattern"}}}}
        });
        handlers_["clear_triplets"] = [this](const json& p) { return tool_clear_triplets(p); };

        tools_.push_back({{"name","resolve_callsites"},{"description","Resolve callsites to symbols for call graph"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"project",{{"type","string"}}}
            }}}}
        });
        handlers_["resolve_callsites"] = [this](const json& p) { return tool_resolve_callsites(p); };

        tools_.push_back({{"name","type_hierarchy"},{"description","Get type hierarchy for a type"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"name",{{"type","string"}}},{"direction",{{"type","string"}}}
            }},{"required",{"name"}}}}
        });
        handlers_["type_hierarchy"] = [this](const json& p) { return tool_type_hierarchy(p); };

        tools_.push_back({{"name","file_imports"},{"description","Get imports for a file"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"path",{{"type","string"}}}
            }},{"required",{"path"}}}}
        });
        handlers_["file_imports"] = [this](const json& p) { return tool_file_imports(p); };

        tools_.push_back({{"name","file_dependents"},{"description","Get files that import a module/file"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"module",{{"type","string"}}}
            }},{"required",{"module"}}}}
        });
        handlers_["file_dependents"] = [this](const json& p) { return tool_file_dependents(p); };

        tools_.push_back({{"name","embed_symbols"},{"description","Fast embed symbol metadata"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"batch_size",{{"type","integer"}}},{"reset",{{"type","boolean"}}}
            }}}}
        });
        handlers_["embed_symbols"] = [this](const json& p) { return tool_embed_symbols(p); };

        tools_.push_back({{"name","dedupe_symbols"},{"description","Remove duplicate symbols"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["dedupe_symbols"] = [this](const json& p) { return tool_dedupe_symbols(p); };

        tools_.push_back({{"name","describe_symbol"},{"description","Set description for a code symbol"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"symbol_id",{{"type","integer"}}},{"description",{{"type","string"}}}
            }},{"required",{"symbol_id","description"}}}}
        });
        handlers_["describe_symbol"] = [this](const json& p) { return tool_describe_symbol(p); };

        tools_.push_back({{"name","enrichment_status"},{"description","Get code enrichment progress"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["enrichment_status"] = [this](const json& p) { return tool_enrichment_status(p); };

        // ── System tools ────────────────────────────────────────────────────
        tools_.push_back({{"name","memory_status"},{"description","Get effective status of a memory: active, superseded, or contradicted — checks incoming supersedes triplets"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"}}},{"memory_id",{{"type","integer"}}}
            }},{"required",json::array()}}}
        });
        handlers_["memory_status"] = [this](const json& p) { return tool_memory_status(p); };

        tools_.push_back({{"name","memory_provenance"},{"description","Show why a memory exists: source, evidence, superseded_by, supersedes relations"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"},{"description","Memory ID to inspect"}}},
                {"memory_id",{{"type","integer"},{"description","Memory ID (numeric)"}}}
            }},{"required",json::array()}}}
        });
        handlers_["memory_provenance"] = [this](const json& p) { return tool_memory_provenance(p); };

        tools_.push_back({{"name","list_by_status"},{"description","List memories filtered by lifecycle status (active/superseded/contradicted/archived)"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"status",{{"type","string"},{"description","Filter: active, superseded, contradicted, archived, or all"},{"default","superseded"}}},
                {"limit",{{"type","integer"},{"description","Max results"},{"default",50}}},
                {"realm",{{"type","string"},{"description","Filter by realm"}}}
            }},{"required",json::array()}}}
        });
        handlers_["list_by_status"] = [this](const json& p) { return tool_list_by_status(p); };

        tools_.push_back({{"name","compact_wal"},{"description","Compact WAL: save full snapshot then delete covered segments"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["compact_wal"] = [this](const json& p) { return tool_compact_wal(p); };

        tools_.push_back({{"name","queue_status"},{"description","Show async queue stats: processed, failed, dead-letter path"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["queue_status"] = [this](const json& p) { return tool_queue_status(p); };

        tools_.push_back({{"name","health_check"},{"description","Check daemon health"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["health_check"] = [this](const json& p) { return tool_health_check(p); };

        tools_.push_back({{"name","version_check"},{"description","Get version info"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["version_check"] = [this](const json&) { return tool_version_check(); };

        tools_.push_back({{"name","cycle"},{"description","Run maintenance cycle"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"force",{{"type","boolean"}}}
            }}}}
        });
        handlers_["cycle"] = [this](const json& p) { return tool_cycle(p); };

        tools_.push_back({{"name","cleanup"},{"description","Remove garbage nodes"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"dry_run",{{"type","boolean"}}}
            }}}}
        });
        handlers_["cleanup"] = [this](const json& p) { return tool_cleanup(p); };

        tools_.push_back({{"name","soul_context"},{"description","Get current soul state and statistics"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["soul_context"] = [this](const json& p) { return tool_soul_context(p); };

        tools_.push_back({{"name","resonance_stats"},{"description","Show ResonanceLearner Bayesian bandit stats"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["resonance_stats"] = [this](const json& p) { return tool_resonance_stats(p); };

        tools_.push_back({{"name","subconscious_stats"},{"description","Get subconscious background processor stats"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["subconscious_stats"] = [this](const json& p) { return tool_subconscious_stats(p); };

        tools_.push_back({{"name","reembed_memories"},{"description","Re-embed memories with proper embeddings"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"all",{{"type","boolean"}}},{"kind",{{"type","string"}}},
                {"min_confidence",{{"type","number"}}},{"limit",{{"type","integer"}}},
                {"dry_run",{{"type","boolean"}}}
            }}}}
        });
        handlers_["reembed_memories"] = [this](const json& p) { return tool_reembed_memories(p); };

        tools_.push_back({{"name","rebuild_fts_index"},{"description","Rebuild FTS index for BM25 search"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["rebuild_fts_index"] = [this](const json& p) { return tool_rebuild_fts_index(p); };

        tools_.push_back({{"name","hygiene_stats"},{"description","Get memory hygiene statistics"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["hygiene_stats"] = [this](const json& p) { return tool_hygiene_stats(p); };

        tools_.push_back({{"name","hygiene_run"},{"description","Run memory hygiene: decay, prune, consolidate"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"prune_threshold",{{"type","number"}}},{"min_age_days",{{"type","number"}}},
                {"consolidation_threshold",{{"type","number"}}},{"max_consolidations",{{"type","integer"}}}
            }}}}
        });
        handlers_["hygiene_run"] = [this](const json& p) { return tool_hygiene_run(p); };

        tools_.push_back({{"name","import_soul"},{"description","Import .soul file (SSL format)"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"file",{{"type","string"}}},{"content",{{"type","string"}}}
            }}}}
        });
        handlers_["import_soul"] = [this](const json& p) { return tool_import_soul(p); };

        tools_.push_back({{"name","export_soul"},{"description","Export memories to SSL format"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"file",{{"type","string"}}},{"tag",{{"type","string"}}},
                {"limit",{{"type","integer"}}}
            }}}}
        });
        handlers_["export_soul"] = [this](const json& p) { return tool_export_soul(p); };

        tools_.push_back({{"name","chitta_health"},{"description","Report feedback loop health diagnostics"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["chitta_health"] = [this](const json& p) { return tool_chitta_health(p); };

        tools_.push_back({{"name","restore_code_intel_confidence"},{"description","Restore confidence for code intel memories"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"confidence",{{"type","number"}}},{"dry_run",{{"type","boolean"}}}
            }}}}
        });
        handlers_["restore_code_intel_confidence"] = [this](const json& p) { return tool_restore_code_intel_confidence(p); };

        // ── Theme tools ─────────────────────────────────────────────────────
        tools_.push_back({{"name","theme_list"},{"description","List all themes with statistics"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"realm",{{"type","string"}}},{"limit",{{"type","integer"}}}
            }}}}
        });
        handlers_["theme_list"] = [this](const json& p) { return tool_theme_list(p); };

        tools_.push_back({{"name","theme_get"},{"description","Get theme details"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["theme_get"] = [this](const json& p) { return tool_theme_get(p); };

        tools_.push_back({{"name","theme_recall"},{"description","Two-stage theme-based retrieval"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"query",{{"type","string"}}},{"limit",{{"type","integer"}}},{"realm",{{"type","string"}}}
            }},{"required",{"query"}}}}
        });
        handlers_["theme_recall"] = [this](const json& p) { return tool_theme_recall(p); };

        tools_.push_back({{"name","theme_stats"},{"description","Get theme organization statistics"},
            {"inputSchema",{{"type","object"},{"properties",{{"realm",{{"type","string"}}}}}}}
        });
        handlers_["theme_stats"] = [this](const json& p) { return tool_theme_stats(p); };

        // theme_maintain and theme_assign_orphans removed — no theme engine backend

        // ── Realm tools ─────────────────────────────────────────────────────
        tools_.push_back({{"name","realm_list"},{"description","List all known realms"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["realm_list"] = [this](const json&) { return tool_realm_list(); };

        tools_.push_back({{"name","realm_get"},{"description","Get all realms a memory belongs to"},
            {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","string"}}}}},{"required",{"id"}}}}
        });
        handlers_["realm_get"] = [this](const json& p) { return tool_realm_get(p); };

        tools_.push_back({{"name","realm_set"},{"description","Set primary realm for a memory"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"}}},{"realm",{{"type","string"}}}
            }},{"required",{"id","realm"}}}}
        });
        handlers_["realm_set"] = [this](const json& p) { return tool_realm_set(p); };

        tools_.push_back({{"name","realm_add"},{"description","Add memory to a shared realm (stub)"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"}}},{"realm",{{"type","string"}}}
            }},{"required",{"id","realm"}}}}
        });
        handlers_["realm_add"] = [this](const json& p) { return tool_realm_add(p); };

        tools_.push_back({{"name","realm_remove"},{"description","Remove memory from a shared realm (stub)"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"}}},{"realm",{{"type","string"}}}
            }},{"required",{"id","realm"}}}}
        });
        handlers_["realm_remove"] = [this](const json& p) { return tool_realm_remove(p); };

        tools_.push_back({{"name","realm_visibility"},{"description","Set visibility level (stub)"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"}}},{"visibility",{{"type","integer"}}}
            }},{"required",{"id","visibility"}}}}
        });
        handlers_["realm_visibility"] = [this](const json& p) { return tool_realm_visibility(p); };

        tools_.push_back({{"name","realm_detect"},{"description","Detect current realm from environment"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["realm_detect"] = [this](const json&) { return tool_realm_detect(); };

        // ── Ledger + Long Task tools ────────────────────────────────────────
        tools_.push_back({{"name","ledger_save"},{"description","Save session checkpoint"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"session_id",{{"type","string"}}},{"project",{{"type","string"}}},
                {"transcript_path",{{"type","string"}}},{"mood",{{"type","string"}}},
                {"coherence",{{"type","number"}}},{"confidence",{{"type","number"}}},
                {"todos",{{"type","array"}}},{"active_files",{{"type","array"},{"items",{{"type","string"}}}}},
                {"decisions",{{"type","array"},{"items",{{"type","string"}}}}},
                {"next_steps",{{"type","array"},{"items",{{"type","string"}}}}},
                {"blockers",{{"type","array"},{"items",{{"type","string"}}}}},
                {"discoveries",{{"type","array"},{"items",{{"type","string"}}}}},
                {"snapshot",{{"type","string"}}}
            }},{"required",json::array()}}}
        });
        handlers_["ledger_save"] = [this](const json& p) { return tool_ledger_save(p); };

        tools_.push_back({{"name","ledger_load"},{"description","Load most recent checkpoint"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"session_id",{{"type","string"}}},{"project",{{"type","string"}}}
            }}}}
        });
        handlers_["ledger_load"] = [this](const json& p) { return tool_ledger_load(p); };

        tools_.push_back({{"name","ledger_list"},{"description","List recent checkpoints"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"project",{{"type","string"}}},{"limit",{{"type","integer"}}}
            }}}}
        });
        handlers_["ledger_list"] = [this](const json& p) { return tool_ledger_list(p); };

        tools_.push_back({{"name","ledger_get"},{"description","Get checkpoint by ID"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["ledger_get"] = [this](const json& p) { return tool_ledger_get(p); };

        tools_.push_back({{"name","ledger_delete"},{"description","Delete checkpoint"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["ledger_delete"] = [this](const json& p) { return tool_ledger_delete(p); };

        tools_.push_back({{"name","long_task_start"},{"description","Start a long-running task"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"task_id",{{"type","string"}}},{"goal",{{"type","string"}}},{"realm",{{"type","string"}}},
                {"hard_checks",{{"type","array"},{"items",{{"type","string"}}}}},
                {"soft_checks",{{"type","array"},{"items",{{"type","string"}}}}},
                {"work_items",{{"type","array"},{"items",{{"type","string"}}}}}
            }},{"required",{"task_id","goal"}}}}
        });
        handlers_["long_task_start"] = [this](const json& p) { return tool_long_task_start(p); };

        tools_.push_back({{"name","long_task_get"},{"description","Get a long-running task by ID"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"task_id",{{"type","string"}}}
            }},{"required",{"task_id"}}}}
        });
        handlers_["long_task_get"] = [this](const json& p) { return tool_long_task_get(p); };

        tools_.push_back({{"name","long_task_active"},{"description","Get active long-running task"},
            {"inputSchema",{{"type","object"},{"properties",{{"realm",{{"type","string"}}}}}}}
        });
        handlers_["long_task_active"] = [this](const json& p) { return tool_long_task_active(p); };

        tools_.push_back({{"name","long_task_update"},{"description","Update long-running task progress"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"task_id",{{"type","string"}}},{"completed_summary",{{"type","string"}}},
                {"work_items",{{"type","array"},{"items",{{"type","string"}}}}},
                {"blockers",{{"type","array"},{"items",{{"type","string"}}}}}
            }},{"required",{"task_id"}}}}
        });
        handlers_["long_task_update"] = [this](const json& p) { return tool_long_task_update(p); };

        tools_.push_back({{"name","long_task_complete"},{"description","Mark task as completed"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"task_id",{{"type","string"}}},{"outcome",{{"type","string"}}}
            }},{"required",{"task_id","outcome"}}}}
        });
        handlers_["long_task_complete"] = [this](const json& p) { return tool_long_task_complete(p); };

        tools_.push_back({{"name","long_task_event"},{"description","Append event to task log"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"task_id",{{"type","string"}}},{"kind",{{"type","string"}}},
                {"payload",{{"type","string"}}},{"tags",{{"type","array"},{"items",{{"type","string"}}}}},
                {"related_entities",{{"type","array"},{"items",{{"type","string"}}}}}
            }},{"required",{"task_id","kind"}}}}
        });
        handlers_["long_task_event"] = [this](const json& p) { return tool_long_task_event(p); };

        tools_.push_back({{"name","checkpoint"},{"description","Save session state"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"realm",{{"type","string"}}},{"mood",{{"type","string"}}},
                {"summary",{{"type","string"}}},{"next_steps",{{"type","array"},{"items",{{"type","string"}}}}},
                {"active_files",{{"type","array"},{"items",{{"type","string"}}}}},
                {"discoveries",{{"type","array"},{"items",{{"type","string"}}}}}
            }}}}
        });
        handlers_["checkpoint"] = [this](const json& p) { return tool_unified_checkpoint(p); };

        tools_.push_back({{"name","long_task_snapshot"},{"description","Get synthesized task context"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"task_id",{{"type","string"}}},{"mode",{{"type","string"}}},{"max_tokens",{{"type","integer"}}}
            }},{"required",{"task_id"}}}}
        });
        handlers_["long_task_snapshot"] = [this](const json& p) { return tool_long_task_snapshot(p); };

        tools_.push_back({{"name","long_task_evaluate"},{"description","Evaluate task completion"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"task_id",{{"type","string"}}}
            }},{"required",{"task_id"}}}}
        });
        handlers_["long_task_evaluate"] = [this](const json& p) { return tool_long_task_evaluate(p); };

        // ── Session/Transcript tools ────────────────────────────────────────
        tools_.push_back({{"name","transcript_register"},{"description","Register transcript for distillation"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"session_id",{{"type","string"}}},{"transcript_path",{{"type","string"}}},
                {"realm",{{"type","string"}}}
            }},{"required",{"session_id","transcript_path"}}}}
        });
        handlers_["transcript_register"] = [this](const json& p) { return tool_transcript_register(p); };

        tools_.push_back({{"name","transcript_get"},{"description","Get transcript state"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"session_id",{{"type","string"}}}
            }},{"required",{"session_id"}}}}
        });
        handlers_["transcript_get"] = [this](const json& p) { return tool_transcript_get(p); };

        tools_.push_back({{"name","transcript_list"},{"description","List registered transcripts"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["transcript_list"] = [this](const json& p) { return tool_transcript_list(p); };

        tools_.push_back({{"name","transcript_update"},{"description","Update transcript progress"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"session_id",{{"type","string"}}},{"last_line",{{"type","integer"}}}
            }},{"required",{"session_id","last_line"}}}}
        });
        handlers_["transcript_update"] = [this](const json& p) { return tool_transcript_update(p); };

        tools_.push_back({{"name","transcript_remove"},{"description","Remove transcript from tracking"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"session_id",{{"type","string"}}}
            }},{"required",{"session_id"}}}}
        });
        handlers_["transcript_remove"] = [this](const json& p) { return tool_transcript_remove(p); };

        tools_.push_back({{"name","transcript_parse"},{"description","Parse new turns from transcript"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"session_id",{{"type","string"}}},{"min_turns",{{"type","integer"}}}
            }},{"required",{"session_id"}}}}
        });
        handlers_["transcript_parse"] = [this](const json& p) { return tool_transcript_parse(p); };

        tools_.push_back({{"name","transcript_search"},{"description","Semantic search across transcript content"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"query",{{"type","string"}}},{"session_id",{{"type","string"}}},
                {"limit",{{"type","integer"}}},{"min_similarity",{{"type","number"}}},
                {"keyword_only",{{"type","boolean"}}}
            }},{"required",{"query"}}}}
        });
        handlers_["transcript_search"] = [this](const json& p) { return tool_transcript_search(p); };

        tools_.push_back({{"name","read_transcript"},{"description","Read JSONL transcript with pagination"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"path",{{"type","string"}}},{"session_id",{{"type","string"}}},
                {"start_turn",{{"type","integer"}}},{"limit",{{"type","integer"}}},
                {"max_chars_per_turn",{{"type","integer"}}},{"role_filter",{{"type","string"}}},
                {"keyword",{{"type","string"}}},{"metadata_only",{{"type","boolean"}}}
            }}}}
        });
        handlers_["read_transcript"] = [this](const json& p) { return tool_read_transcript(p); };

        tools_.push_back({{"name","get_turns"},{"description","Get conversation turns for a session"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"session_id",{{"type","string"}}},{"start_index",{{"type","integer"}}},
                {"limit",{{"type","integer"}}}
            }}}}
        });
        handlers_["get_turns"] = [this](const json& p) { return tool_get_turns(p); };

        tools_.push_back({{"name","create_episode"},{"description","Create dialogue episode for conversation tracking"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"session_id",{{"type","string"}}},{"title",{{"type","string"}}},
                {"start_turn",{{"type","integer"}}},{"end_turn",{{"type","integer"}}},
                {"episode_type",{{"type","string"}}},{"realm",{{"type","string"}}}
            }},{"required",{"session_id","title","start_turn"}}}}
        });
        handlers_["create_episode"] = [this](const json& p) { return tool_create_episode(p); };

        // Messaging
        tools_.push_back({{"name","msg_send"},{"description","Send message to another session"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"target",{{"type","string"}}},{"content",{{"type","string"}}},
                {"target_type",{{"type","string"}}},{"priority",{{"type","integer"}}},
                {"content_type",{{"type","string"}}},{"ttl",{{"type","integer"}}},
                {"session_id",{{"type","string"}}}
            }},{"required",{"target","content"}}}}
        });
        handlers_["msg_send"] = [this](const json& p) { return tool_msg_send(p); };

        tools_.push_back({{"name","msg_inbox"},{"description","Check unread messages"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"session_id",{{"type","string"}}},{"limit",{{"type","integer"}}},
                {"min_priority",{{"type","integer"}}},{"auto_ack",{{"type","boolean"}}}
            }}}}
        });
        handlers_["msg_inbox"] = [this](const json& p) { return tool_msg_inbox(p); };

        tools_.push_back({{"name","msg_ack"},{"description","Acknowledge a message"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"message_id",{{"type","integer"}}},{"session_id",{{"type","string"}}}
            }},{"required",{"message_id"}}}}
        });
        handlers_["msg_ack"] = [this](const json& p) { return tool_msg_ack(p); };

        tools_.push_back({{"name","msg_ack_all"},{"description","Acknowledge all messages"},
            {"inputSchema",{{"type","object"},{"properties",{{"session_id",{{"type","string"}}}}}}}
        });
        handlers_["msg_ack_all"] = [this](const json& p) { return tool_msg_ack_all(p); };

        tools_.push_back({{"name","msg_history"},{"description","Get message history"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"session_id",{{"type","string"}}},{"limit",{{"type","integer"}}}
            }}}}
        });
        handlers_["msg_history"] = [this](const json& p) { return tool_msg_history(p); };

        tools_.push_back({{"name","session_register"},{"description","Register session"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"session_id",{{"type","string"}}},{"realm",{{"type","string"}}},
                {"pid",{{"type","integer"}}},{"transcript_path",{{"type","string"}}},
                {"project_dir",{{"type","string"}}},{"metadata",{{"type","string"}}}
            }}}}
        });
        handlers_["session_register"] = [this](const json& p) { return tool_session_register(p); };

        tools_.push_back({{"name","session_heartbeat"},{"description","Send heartbeat"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"session_id",{{"type","string"}}},{"metadata",{{"type","string"}}}
            }}}}
        });
        handlers_["session_heartbeat"] = [this](const json& p) { return tool_session_heartbeat(p); };

        tools_.push_back({{"name","session_list"},{"description","List active sessions"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"realm",{{"type","string"}}},{"status",{{"type","string"}}}
            }}}}
        });
        handlers_["session_list"] = [this](const json& p) { return tool_session_list(p); };

        tools_.push_back({{"name","session_deregister"},{"description","Deregister session"},
            {"inputSchema",{{"type","object"},{"properties",{{"session_id",{{"type","string"}}}}}}}
        });
        handlers_["session_deregister"] = [this](const json& p) { return tool_session_deregister(p); };

        tools_.push_back({{"name","session_sync"},{"description","Sync session registry"},
            {"inputSchema",{{"type","object"},{"properties",{{"projects_dir",{{"type","string"}}}}}}}
        });
        handlers_["session_sync"] = [this](const json& p) { return tool_session_sync(p); };

        // ── Distill tools ───────────────────────────────────────────────────
        tools_.push_back({{"name","distill_status"},{"description","Get distillation status"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["distill_status"] = [this](const json& p) { return tool_distill_status(p); };

        tools_.push_back({{"name","distill_set_model"},{"description","Change distillation model"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"model",{{"type","string"}}},{"enabled",{{"type","boolean"}}}
            }},{"required",{"model"}}}}
        });
        handlers_["distill_set_model"] = [this](const json& p) { return tool_distill_set_model(p); };

        tools_.push_back({{"name","suggestion_track"},{"description","Track a suggestion"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"content",{{"type","string"}}},{"context",{{"type","string"}}},{"realm",{{"type","string"}}}
            }},{"required",{"content"}}}}
        });
        handlers_["suggestion_track"] = [this](const json& p) { return tool_suggestion_track(p); };

        tools_.push_back({{"name","suggestion_pending"},{"description","List pending suggestions"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"realm",{{"type","string"}}},{"limit",{{"type","integer"}}}
            }}}}
        });
        handlers_["suggestion_pending"] = [this](const json& p) { return tool_suggestion_pending(p); };

        tools_.push_back({{"name","suggestion_resolve"},{"description","Record suggestion outcome"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"helped",{{"type","boolean"}}},{"details",{{"type","string"}}}
            }},{"required",{"id","helped"}}}}
        });
        handlers_["suggestion_resolve"] = [this](const json& p) { return tool_suggestion_resolve(p); };

        tools_.push_back({{"name","suggestion_count"},{"description","Count pending suggestions"},
            {"inputSchema",{{"type","object"},{"properties",{{"realm",{{"type","string"}}}}}}}
        });
        handlers_["suggestion_count"] = [this](const json& p) { return tool_suggestion_count(p); };

        tools_.push_back({{"name","consolidation_scan"},{"description","Find similar memory pairs"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"similarity_threshold",{{"type","number"}}},{"limit",{{"type","integer"}}},
                {"realm",{{"type","string"}}}
            }}}}
        });
        handlers_["consolidation_scan"] = [this](const json& p) { return tool_consolidation_scan(p); };

        tools_.push_back({{"name","consolidation_merge"},{"description","Merge two similar memories"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"primary_id",{{"type","integer"}}},{"secondary_id",{{"type","integer"}}},
                {"merged_content",{{"type","string"}}}
            }},{"required",{"primary_id","secondary_id"}}}}
        });
        handlers_["consolidation_merge"] = [this](const json& p) { return tool_consolidation_merge(p); };

        tools_.push_back({{"name","consolidation_auto"},{"description","Auto-merge highly similar memories"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"similarity_threshold",{{"type","number"}}},{"max_merges",{{"type","integer"}}}
            }}}}
        });
        handlers_["consolidation_auto"] = [this](const json& p) { return tool_consolidation_auto(p); };

        tools_.push_back({{"name","metacognition_corrections"},{"description","Analyze patterns in corrections"},
            {"inputSchema",{{"type","object"},{"properties",{{"limit",{{"type","integer"}}}}}}}
        });
        handlers_["metacognition_corrections"] = [this](const json& p) { return tool_metacognition_corrections(p); };

        tools_.push_back({{"name","metacognition_outcomes"},{"description","Analyze suggestion outcomes"},
            {"inputSchema",{{"type","object"},{"properties",{{"limit",{{"type","integer"}}}}}}}
        });
        handlers_["metacognition_outcomes"] = [this](const json& p) { return tool_metacognition_outcomes(p); };

        tools_.push_back({{"name","metacognition_evaluate"},{"description","Self-evaluate learning effectiveness"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["metacognition_evaluate"] = [this](const json& p) { return tool_metacognition_evaluate(p); };

        tools_.push_back({{"name","epiplexity_check"},{"description","Compute epiplexity score"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"original",{{"type","string"}}},{"seed",{{"type","string"}}},{"reconstructed",{{"type","string"}}}
            }},{"required",{"original","seed","reconstructed"}}}}
        });
        handlers_["epiplexity_check"] = [this](const json& p) { return tool_epiplexity_check(p); };

        tools_.push_back({{"name","ssl_convert"},{"description","Convert raw text to SSL format"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"content",{{"type","string"}}},{"domain",{{"type","string"}}},{"location",{{"type","string"}}}
            }},{"required",{"content"}}}}
        });
        handlers_["ssl_convert"] = [this](const json& p) { return tool_ssl_convert(p); };

        tools_.push_back({{"name","curiosity_note_gap"},{"description","Record a knowledge gap"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"gap",{{"type","string"}}},{"context",{{"type","string"}}},{"realm",{{"type","string"}}}
            }},{"required",{"gap"}}}}
        });
        handlers_["curiosity_note_gap"] = [this](const json& p) { return tool_curiosity_note_gap(p); };

        tools_.push_back({{"name","curiosity_gaps"},{"description","List knowledge gaps"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"limit",{{"type","integer"}}},{"realm",{{"type","string"}}}
            }}}}
        });
        handlers_["curiosity_gaps"] = [this](const json& p) { return tool_curiosity_gaps(p); };

        tools_.push_back({{"name","curiosity_resolve"},{"description","Mark gap as resolved"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"learned",{{"type","string"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["curiosity_resolve"] = [this](const json& p) { return tool_curiosity_resolve(p); };

        // ── Misc tools ──────────────────────────────────────────────────────

        // Memory management
        tools_.push_back({{"name","list_memories_brief"},{"description","Fast memory index"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"limit",{{"type","integer"}}},{"realm",{{"type","string"}}},
                {"kind",{{"type","string"}}},{"priority_tier",{{"type","integer"}}}
            }}}}
        });
        handlers_["list_memories_brief"] = [this](const json& p) { return tool_list_memories_brief(p); };

        tools_.push_back({{"name","set_priority_tier"},{"description","Set memory priority tier"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"memory_id",{{"type","integer"}}},{"tier",{{"type","integer"}}}
            }},{"required",{"memory_id","tier"}}}}
        });
        handlers_["set_priority_tier"] = [this](const json& p) { return tool_set_priority_tier(p); };

        tools_.push_back({{"name","recall_by_priority"},{"description","Budget-aware recall"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"query",{{"type","string"}}},{"budget_tokens",{{"type","integer"}}},
                {"realm",{{"type","string"}}},{"include_global",{{"type","boolean"}}}
            }}}}
        });
        handlers_["recall_by_priority"] = [this](const json& p) { return tool_recall_by_priority(p); };

        tools_.push_back({{"name","set_memory_type"},{"description","Set memory semantic type"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"memory_id",{{"type","integer"}}},{"type",{{"type","string"}}}
            }},{"required",{"memory_id","type"}}}}
        });
        handlers_["set_memory_type"] = [this](const json& p) { return tool_set_memory_type(p); };

        tools_.push_back({{"name","memory_type_stats"},{"description","Get memory type statistics"},
            {"inputSchema",{{"type","object"},{"properties",{{"realm",{{"type","string"}}}}}}}
        });
        handlers_["memory_type_stats"] = [this](const json& p) { return tool_memory_type_stats(p); };

        tools_.push_back({{"name","forget_kind"},{"description","Bulk-delete all memories of a given kind (e.g. 'habit'). Optionally filter by realm."},
            {"inputSchema",{{"type","object"},{"properties",{
                {"kind",{{"type","string"},{"description","Memory kind to delete (e.g. habit, unknown)"}}},
                {"realm",{{"type","string"},{"description","Optional realm filter"}}},
                {"limit",{{"type","integer"},{"description","Max to delete (default 5000)"}}}
            }},{"required",{"kind"}}}}});
        handlers_["forget_kind"] = [this](const json& p) { return tool_forget_kind(p); };

        tools_.push_back({{"name","smart_recall"},{"description","Intelligent memory recall with hierarchical expansion"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"query",{{"type","string"}}},{"limit",{{"type","integer"}}},
                {"expand_top",{{"type","integer"}}},{"realm",{{"type","string"}}},
                {"include_global",{{"type","boolean"}}},
                {"separation_mode",{{"type","boolean"}}},{"gwt_mode",{{"type","boolean"}}}
            }},{"required",{"query"}}}}
        });
        handlers_["smart_recall"] = [this](const json& p) { return tool_smart_recall(p); };

        tools_.push_back({{"name","hybrid_recall"},{"description","Combined vector + BM25 + graph recall"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"query",{{"type","string"}}},{"limit",{{"type","integer"}}},
                {"tag",{{"type","string"}}},{"realm",{{"type","string"}}},
                {"vector_weight",{{"type","number"}}},{"bm25_weight",{{"type","number"}}},
                {"graph_weight",{{"type","number"}}},{"recency_weight",{{"type","number"}}},
                {"explain",{{"type","boolean"},{"description","Include score decomposition per hit (default: false)"}}}
            }},{"required",{"query"}}}}
        });
        handlers_["hybrid_recall"] = [this](const json& p) { return tool_hybrid_recall(p); };

        tools_.push_back({{"name","structured_recall"},{"description","Three-lens recall: facts, context, and temporal agents merged for high-fidelity retrieval"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"query",{{"type","string"}}},{"limit",{{"type","integer"}}},
                {"realm",{{"type","string"}}}
            }},{"required",{"query"}}}}
        });
        handlers_["structured_recall"] = [this](const json& p) { return tool_structured_recall(p); };

        tools_.push_back({{"name","route_stats"},{"description","Show route learner status and arm configuration for smart_recall"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["route_stats"] = [this](const json& p) { return tool_route_stats(p); };

        tools_.push_back({{"name","ask"},{"description","Natural language insight query: retrieves and synthesizes memories to answer a question about the user, session, or project"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"question",{{"type","string"}}},{"limit",{{"type","integer"}}},
                {"realm",{{"type","string"}}}
            }},{"required",{"question"}}}}
        });
        handlers_["ask"] = [this](const json& p) { return tool_ask(p); };

        tools_.push_back({{"name","expand_query"},{"description","Expand query into typed variants"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"query",{{"type","string"}}}
            }},{"required",{"query"}}}}
        });
        handlers_["expand_query"] = [this](const json& p) { return tool_expand_query(p); };

        // Anticipation/Habit/Profile/Goal/Calibration
        tools_.push_back({{"name","anticipation_observe"},{"description","Record context->action pattern"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"context",{{"type","string"}}},{"action",{{"type","string"}}},{"realm",{{"type","string"}}}
            }},{"required",{"context","action"}}}}
        });
        handlers_["anticipation_observe"] = [this](const json& p) { return tool_anticipation_observe(p); };

        tools_.push_back({{"name","anticipation_predict"},{"description","Predict likely actions"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"context",{{"type","string"}}},{"limit",{{"type","integer"}}},
                {"min_confidence",{{"type","number"}}},{"realm",{{"type","string"}}}
            }},{"required",{"context"}}}}
        });
        handlers_["anticipation_predict"] = [this](const json& p) { return tool_anticipation_predict(p); };

        tools_.push_back({{"name","anticipation_success"},{"description","Mark prediction as successful"},
            {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","integer"}}}}},{"required",{"id"}}}}
        });
        handlers_["anticipation_success"] = [this](const json& p) { return tool_anticipation_success(p); };

        tools_.push_back({{"name","anticipation_list"},{"description","List learned anticipation patterns"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"realm",{{"type","string"}}},{"limit",{{"type","integer"}}},{"sort_by",{{"type","string"}}}
            }}}}
        });
        handlers_["anticipation_list"] = [this](const json& p) { return tool_anticipation_list(p); };

        tools_.push_back({{"name","anticipation_filter"},{"description","Get predictions passing annoyance gate"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"session_id",{{"type","string"}}},{"max",{{"type","integer"}}}
            }}}}
        });
        handlers_["anticipation_filter"] = [this](const json& p) { return tool_anticipation_filter(p); };

        tools_.push_back({{"name","anticipation_gate_status"},{"description","Show annoyance gate state"},
            {"inputSchema",{{"type","object"},{"properties",{{"session_id",{{"type","string"}}}}}}}
        });
        handlers_["anticipation_gate_status"] = [this](const json& p) { return tool_anticipation_gate_status(p); };

        tools_.push_back({{"name","anticipation_record_outcome"},{"description","Record prediction outcome"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"candidate_id",{{"type","integer"}}},{"correct",{{"type","boolean"}}}
            }},{"required",{"candidate_id","correct"}}}}
        });
        handlers_["anticipation_record_outcome"] = [this](const json& p) { return tool_anticipation_record_outcome(p); };

        tools_.push_back({{"name","habit_observe"},{"description","Record trigger->response pattern"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"trigger",{{"type","string"}}},{"response",{{"type","string"}}},{"realm",{{"type","string"}}}
            }},{"required",{"trigger","response"}}}}
        });
        handlers_["habit_observe"] = [this](const json& p) { return tool_habit_observe(p); };

        tools_.push_back({{"name","habit_match"},{"description","Find matching habits"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"context",{{"type","string"}}},{"min_strength",{{"type","number"}}},{"realm",{{"type","string"}}}
            }},{"required",{"context"}}}}
        });
        handlers_["habit_match"] = [this](const json& p) { return tool_habit_match(p); };

        tools_.push_back({{"name","habit_strengthen"},{"description","Strengthen a habit"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"amount",{{"type","number"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["habit_strengthen"] = [this](const json& p) { return tool_habit_strengthen(p); };

        tools_.push_back({{"name","habit_weaken"},{"description","Weaken a habit"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"amount",{{"type","number"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["habit_weaken"] = [this](const json& p) { return tool_habit_weaken(p); };

        tools_.push_back({{"name","habit_list"},{"description","List formed habits"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"realm",{{"type","string"}}},{"min_strength",{{"type","number"}}},
                {"limit",{{"type","integer"}}},{"sort_by",{{"type","string"}}}
            }}}}
        });
        handlers_["habit_list"] = [this](const json& p) { return tool_habit_list(p); };

        tools_.push_back({{"name","profile_get"},{"description","Get user profile"},
            {"inputSchema",{{"type","object"},{"properties",{{"user_id",{{"type","string"}}}}}}}
        });
        handlers_["profile_get"] = [this](const json& p) { return tool_profile_get(p); };

        tools_.push_back({{"name","profile_update"},{"description","Update profile field"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"user_id",{{"type","string"}}},{"field",{{"type","string"}}},{"value",{{"type","string"}}}
            }},{"required",{"field","value"}}}}
        });
        handlers_["profile_update"] = [this](const json& p) { return tool_profile_update(p); };

        tools_.push_back({{"name","profile_observe"},{"description","Record user observation"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"observation_type",{{"type","string"}}},{"value",{{"type","string"}}},{"user_id",{{"type","string"}}}
            }},{"required",{"observation_type","value"}}}}
        });
        handlers_["profile_observe"] = [this](const json& p) { return tool_profile_observe(p); };

        tools_.push_back({{"name","goal_set"},{"description","Define a long-term goal"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"title",{{"type","string"}}},{"description",{{"type","string"}}},
                {"milestones",{{"type","string"}}},{"deadline",{{"type","integer"}}},{"realm",{{"type","string"}}}
            }},{"required",{"title"}}}}
        });
        handlers_["goal_set"] = [this](const json& p) { return tool_goal_set(p); };

        tools_.push_back({{"name","goal_get"},{"description","Get goal by ID"},
            {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","integer"}}}}},{"required",{"id"}}}}
        });
        handlers_["goal_get"] = [this](const json& p) { return tool_goal_get(p); };

        tools_.push_back({{"name","goal_list"},{"description","List goals"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"status",{{"type","string"}}},{"realm",{{"type","string"}}},
                {"limit",{{"type","integer"}}},{"sort_by",{{"type","string"}}}
            }}}}
        });
        handlers_["goal_list"] = [this](const json& p) { return tool_goal_list(p); };

        tools_.push_back({{"name","goal_progress"},{"description","Update goal progress"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"progress",{{"type","number"}}},{"milestone",{{"type","string"}}}
            }},{"required",{"id","progress"}}}}
        });
        handlers_["goal_progress"] = [this](const json& p) { return tool_goal_progress(p); };

        tools_.push_back({{"name","goal_complete"},{"description","Mark goal completed"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"outcome",{{"type","string"}}}
            }},{"required",{"id","outcome"}}}}
        });
        handlers_["goal_complete"] = [this](const json& p) { return tool_goal_complete(p); };

        tools_.push_back({{"name","calibration_record"},{"description","Record prediction outcome"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"domain",{{"type","string"}}},{"success",{{"type","boolean"}}}
            }},{"required",{"domain","success"}}}}
        });
        handlers_["calibration_record"] = [this](const json& p) { return tool_calibration_record(p); };

        tools_.push_back({{"name","calibration_score"},{"description","Get accuracy score"},
            {"inputSchema",{{"type","object"},{"properties",{{"domain",{{"type","string"}}}}}}}
        });
        handlers_["calibration_score"] = [this](const json& p) { return tool_calibration_score(p); };

        // Narrative
        tools_.push_back({{"name","narrative_status"},{"description","Get work mode and segment summary"},
            {"inputSchema",{{"type","object"},{"properties",{{"session_id",{{"type","string"}}}}}}}
        });
        handlers_["narrative_status"] = [this](const json& p) { return tool_narrative_status(p); };

        tools_.push_back({{"name","narrative_log"},{"description","Append event to session log"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"session_id",{{"type","string"}}},{"kind",{{"type","string"}}},{"summary",{{"type","string"}}},
                {"tool_name",{{"type","string"}}},{"success",{{"type","boolean"}}},
                {"payload",{{"type","string"}}},{"files_mentioned",{{"type","string"}}}
            }},{"required",{"kind","summary"}}}}
        });
        handlers_["narrative_log"] = [this](const json& p) { return tool_narrative_log(p); };

        tools_.push_back({{"name","narrative_history"},{"description","Get work mode segment history"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"session_id",{{"type","string"}}},{"limit",{{"type","integer"}}}
            }}}}
        });
        handlers_["narrative_history"] = [this](const json& p) { return tool_narrative_history(p); };

        // Sadhana
        tools_.push_back({{"name","sadhana_start"},{"description","Create and start an autonomous agent"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"goal",{{"type","string"}}},{"brain_provider",{{"type","string"}}},
                {"brain_model",{{"type","string"}}},{"interval_seconds",{{"type","integer"}}},
                {"max_turns",{{"type","integer"}}},{"realm",{{"type","string"}}},
                {"goal_dsl",{{"type","object"}}}
            }},{"required",{"goal"}}}}
        });
        handlers_["sadhana_start"] = [this](const json& p) { return tool_sadhana_start(p); };

        tools_.push_back({{"name","sadhana_pause"},{"description","Pause a sadhana"},
            {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","integer"}}}}},{"required",{"id"}}}}
        });
        handlers_["sadhana_pause"] = [this](const json& p) { return tool_sadhana_pause(p); };

        tools_.push_back({{"name","sadhana_resume"},{"description","Resume a paused sadhana"},
            {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","integer"}}}}},{"required",{"id"}}}}
        });
        handlers_["sadhana_resume"] = [this](const json& p) { return tool_sadhana_resume(p); };

        tools_.push_back({{"name","sadhana_stop"},{"description","Stop a sadhana"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"success",{{"type","boolean"}}},{"reason",{{"type","string"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["sadhana_stop"] = [this](const json& p) { return tool_sadhana_stop(p); };

        tools_.push_back({{"name","sadhana_status"},{"description","Get sadhana status"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"history_limit",{{"type","integer"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["sadhana_status"] = [this](const json& p) { return tool_sadhana_status(p); };

        tools_.push_back({{"name","sadhana_list"},{"description","List sadhanas"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"state",{{"type","string"}}},{"realm",{{"type","string"}}},{"limit",{{"type","integer"}}}
            }}}}
        });
        handlers_["sadhana_list"] = [this](const json& p) { return tool_sadhana_list(p); };

        tools_.push_back({{"name","sadhana_set_model"},{"description","Change brain model"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"model",{{"type","string"}}}
            }},{"required",{"id","model"}}}}
        });
        handlers_["sadhana_set_model"] = [this](const json& p) { return tool_sadhana_set_model(p); };

        tools_.push_back({{"name","sadhana_set_goal"},{"description","Change sadhana goal"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"goal",{{"type","string"}}}
            }},{"required",{"id","goal"}}}}
        });
        handlers_["sadhana_set_goal"] = [this](const json& p) { return tool_sadhana_set_goal(p); };

        tools_.push_back({{"name","sadhana_set_interval"},{"description","Change tick interval"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"interval",{{"type","integer"}}}
            }},{"required",{"id","interval"}}}}
        });
        handlers_["sadhana_set_interval"] = [this](const json& p) { return tool_sadhana_set_interval(p); };

        tools_.push_back({{"name","sadhana_set_max_turns"},{"description","Set max turns per cycle"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"max_turns",{{"type","integer"}}}
            }},{"required",{"id","max_turns"}}}}
        });
        handlers_["sadhana_set_max_turns"] = [this](const json& p) { return tool_sadhana_set_max_turns(p); };

        tools_.push_back({{"name","sadhana_checkpoint"},{"description","Report mid-cycle checkpoint"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},
                {"status",{{"type","string"},{"enum",{"progressed","achieved","blocked"}}}},
                {"summary",{{"type","string"}}}
            }},{"required",{"id","status","summary"}}}}
        });
        handlers_["sadhana_checkpoint"] = [this](const json& p) { return tool_sadhana_checkpoint(p); };

        // Dream
        tools_.push_back({{"name","dream_start"},{"description","Start an autonomous dream"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"topic",{{"type","string"}}},{"realm",{{"type","string"}}},{"publish_path",{{"type","string"}}}
            }},{"required",{"topic"}}}}
        });
        handlers_["dream_start"] = [this](const json& p) { return tool_dream_start(p); };

        tools_.push_back({{"name","dream_wander"},{"description","Auto-select topic and dream"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"realm",{{"type","string"}}},{"publish_path",{{"type","string"}}}
            }}}}
        });
        handlers_["dream_wander"] = [this](const json& p) { return tool_dream_wander(p); };

        tools_.push_back({{"name","dream_cancel"},{"description","Cancel a dream"},
            {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","integer"}}}}},{"required",{"id"}}}}
        });
        handlers_["dream_cancel"] = [this](const json& p) { return tool_dream_cancel(p); };

        tools_.push_back({{"name","dream_force_woke"},{"description","Force stuck dream to woke"},
            {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","integer"}}}}},{"required",{"id"}}}}
        });
        handlers_["dream_force_woke"] = [this](const json& p) { return tool_dream_force_woke(p); };

        tools_.push_back({{"name","dream_list"},{"description","List recent dreams"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"limit",{{"type","integer"}}},{"realm",{{"type","string"}}}
            }}}}
        });
        handlers_["dream_list"] = [this](const json& p) { return tool_dream_list(p); };

        tools_.push_back({{"name","dream_status"},{"description","Get dream details"},
            {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","integer"}}}}},{"required",{"id"}}}}
        });
        handlers_["dream_status"] = [this](const json& p) { return tool_dream_status(p); };

        tools_.push_back({{"name","think_wander"},{"description","Trigger internal memory synthesis"},
            {"inputSchema",{{"type","object"},{"properties",{{"realm",{{"type","string"}}}}}}}
        });
        handlers_["think_wander"] = [this](const json& p) { return tool_think_wander(p); };

        tools_.push_back({{"name","impl_start"},{"description","Start self-improvement implementation sadhana"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"repo",{{"type","string"}}},{"interval_seconds",{{"type","integer"}}},
                {"max_turns",{{"type","integer"}}},{"realm",{{"type","string"}}}
            }}}}
        });
        handlers_["impl_start"] = [this](const json& p) { return tool_impl_start(p); };

        // Context Repository
        tools_.push_back({{"name","memory_history"},{"description","View memory version history"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"limit",{{"type","integer"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["memory_history"] = [this](const json& p) { return tool_memory_history(p); };

        tools_.push_back({{"name","memory_revert"},{"description","Revert memory to previous version"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"version",{{"type","integer"}}},{"reason",{{"type","string"}}}
            }},{"required",{"id","version"}}}}
        });
        handlers_["memory_revert"] = [this](const json& p) { return tool_memory_revert(p); };

        tools_.push_back({{"name","pin_memory"},{"description","Pin memory to keep hot"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"reason",{{"type","string"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["pin_memory"] = [this](const json& p) { return tool_pin_memory(p); };

        tools_.push_back({{"name","unpin_memory"},{"description","Unpin a memory"},
            {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","integer"}}}}},{"required",{"id"}}}}
        });
        handlers_["unpin_memory"] = [this](const json& p) { return tool_unpin_memory(p); };

        tools_.push_back({{"name","list_pinned"},{"description","List pinned memories"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"realm",{{"type","string"}}},{"limit",{{"type","integer"}}}
            }}}}
        });
        handlers_["list_pinned"] = [this](const json& p) { return tool_list_pinned(p); };

        tools_.push_back({{"name","memory_lock"},{"description","Acquire memory lock"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"holder_id",{{"type","string"}}},
                {"holder_type",{{"type","string"}}},{"duration",{{"type","integer"}}}
            }},{"required",{"id","holder_id"}}}}
        });
        handlers_["memory_lock"] = [this](const json& p) { return tool_memory_lock(p); };

        tools_.push_back({{"name","memory_unlock"},{"description","Release memory lock"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"holder_id",{{"type","string"}}}
            }},{"required",{"id","holder_id"}}}}
        });
        handlers_["memory_unlock"] = [this](const json& p) { return tool_memory_unlock(p); };

        tools_.push_back({{"name","memory_lock_status"},{"description","Check lock status"},
            {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","integer"}}}}},{"required",{"id"}}}}
        });
        handlers_["memory_lock_status"] = [this](const json& p) { return tool_memory_lock_status(p); };

        tools_.push_back({{"name","propose_change"},{"description","Propose change to memory"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"content",{{"type","string"}}},{"proposed_by",{{"type","string"}}}
            }},{"required",{"id","content","proposed_by"}}}}
        });
        handlers_["propose_change"] = [this](const json& p) { return tool_propose_change(p); };

        tools_.push_back({{"name","list_merge_queue"},{"description","List pending merge proposals"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"status",{{"type","string"}}},{"limit",{{"type","integer"}}}
            }}}}
        });
        handlers_["list_merge_queue"] = [this](const json& p) { return tool_list_merge_queue(p); };

        tools_.push_back({{"name","resolve_merge"},{"description","Resolve merge proposal"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"merge_id",{{"type","integer"}}},{"resolution",{{"type","string"}}},{"status",{{"type","string"}}}
            }},{"required",{"merge_id","status"}}}}
        });
        handlers_["resolve_merge"] = [this](const json& p) { return tool_resolve_merge(p); };

        // File Time Machine
        tools_.push_back({{"name","file_timeline"},{"description","Show files modified in time range"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"query",{{"type","string"}}},{"session_id",{{"type","string"}}},
                {"path",{{"type","string"}}},{"file_pattern",{{"type","string"}}},
                {"limit",{{"type","integer"}}},{"cross_session",{{"type","boolean"}}}
            }}}}
        });
        handlers_["file_timeline"] = [this](const json& p) { return tool_file_timeline(p); };

        tools_.push_back({{"name","file_at_time"},{"description","Get file content at time (stub)"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"file_path",{{"type","string"}}},{"time",{{"type","string"}}},
                {"session_id",{{"type","string"}}},{"show_diff",{{"type","boolean"}}}
            }},{"required",{"file_path"}}}}
        });
        handlers_["file_at_time"] = [this](const json& p) { return tool_file_at_time(p); };

        tools_.push_back({{"name","file_restore"},{"description","Restore file version (stub)"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"file_path",{{"type","string"}}},{"version_id",{{"type","integer"}}},{"preview",{{"type","boolean"}}}
            }},{"required",{"file_path"}}}}
        });
        handlers_["file_restore"] = [this](const json& p) { return tool_file_restore(p); };

        tools_.push_back({{"name","file_index_session"},{"description","Index file-history from session"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"session_id",{{"type","string"}}},{"force",{{"type","boolean"}}}
            }},{"required",{"session_id"}}}}
        });
        handlers_["file_index_session"] = [this](const json& p) { return tool_file_index_session(p); };

        tools_.push_back({{"name","file_index_all"},{"description","Index all sessions for cross-session timeline"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"force",{{"type","boolean"}}},{"limit",{{"type","integer"}}}
            }}}}
        });
        handlers_["file_index_all"] = [this](const json& p) { return tool_file_index_all(p); };

        // Misc
        tools_.push_back({{"name","learn_outcome"},{"description","Record memory usage outcome"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"memory_id",{{"type","string"}}},
                {"outcome",{{"type","string"},{"enum",{"positive","negative","neutral"}}}},
                {"context",{{"type","string"}}}
            }},{"required",{"memory_id","outcome"}}}}
        });
        handlers_["learn_outcome"] = [this](const json& p) { return tool_learn_outcome(p); };

        tools_.push_back({{"name","log_exposure"},{"description","Log memory exposure (SUS)"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"session_id",{{"type","string"}}},{"turn_id",{{"type","integer"}}},
                {"hook_type",{{"type","string"}}},{"memory_ids",{{"type","array"},{"items",{{"type","integer"}}}}},
                {"ranks",{{"type","array"},{"items",{{"type","integer"}}}}},
                {"resonance_scores",{{"type","array"},{"items",{{"type","number"}}}}}
            }},{"required",{"session_id","turn_id","hook_type","memory_ids"}}}}
        });
        handlers_["log_exposure"] = [this](const json& p) { return tool_log_exposure(p); };

        tools_.push_back({{"name","get_sus_metrics"},{"description","Get Soul Utility Score metrics"},
            {"inputSchema",{{"type","object"},{"properties",{{"days",{{"type","integer"}}}}}}}
        });
        handlers_["get_sus_metrics"] = [this](const json& p) { return tool_get_sus_metrics(p); };

        tools_.push_back({{"name","episode_cluster_status"},{"description","Find similar episode clusters"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"similarity_threshold",{{"type","number"}}},{"min_occurrences",{{"type","integer"}}}
            }}}}
        });
        handlers_["episode_cluster_status"] = [this](const json& p) { return tool_episode_cluster_status(p); };

        tools_.push_back({{"name","insight_promote"},{"description","Promote memory to global"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","integer"}}},{"reason",{{"type","string"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["insight_promote"] = [this](const json& p) { return tool_insight_promote(p); };

        tools_.push_back({{"name","insight_global"},{"description","List global insights"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"limit",{{"type","integer"}}},{"tag",{{"type","string"}}}
            }}}}
        });
        handlers_["insight_global"] = [this](const json& p) { return tool_insight_global(p); };

        tools_.push_back({{"name","list_by_aspect"},{"description","List memories by semantic aspect"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"aspect",{{"type","string"}}},{"limit",{{"type","integer"}}},{"min_confidence",{{"type","number"}}}
            }},{"required",{"aspect"}}}}
        });
        handlers_["list_by_aspect"] = [this](const json& p) { return tool_list_by_aspect(p); };

        tools_.push_back({{"name","list_aspects"},{"description","List available semantic aspects"},
            {"inputSchema",{{"type","object"},{"properties",json::object()}}}
        });
        handlers_["list_aspects"] = [this](const json& p) { return tool_list_aspects(p); };

        tools_.push_back({{"name","query_claims"},{"description","Query semantic claims"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"subject",{{"type","string"}}},{"predicate",{{"type","string"}}},
                {"scope",{{"type","string"}}},{"active_only",{{"type","boolean"}}},{"limit",{{"type","integer"}}}
            }}}}
        });
        handlers_["query_claims"] = [this](const json& p) { return tool_query_claims(p); };

        tools_.push_back({{"name","get_policies"},{"description","Get active policies"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"scope",{{"type","string"}}},{"type",{{"type","string"}}},{"limit",{{"type","integer"}}}
            }}}}
        });
        handlers_["get_policies"] = [this](const json& p) { return tool_get_policies(p); };

        tools_.push_back({{"name","get_entities"},{"description","Get tracked entities"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"type",{{"type","string"}}},{"limit",{{"type","integer"}}}
            }}}}
        });
        handlers_["get_entities"] = [this](const json& p) { return tool_get_entities(p); };

        tools_.push_back({{"name","get_relationship_events"},{"description","Get relationship events"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"event_type",{{"type","string"}}},{"session_id",{{"type","string"}}},{"limit",{{"type","integer"}}}
            }}}}
        });
        handlers_["get_relationship_events"] = [this](const json& p) { return tool_get_relationship_events(p); };

        // ── Context compaction ─────────────────────────────────────────────
        tools_.push_back({
            {"name", "compact_context"},
            {"description", "Memory-aware context compaction. Scores conversation messages by recency, "
                "semantic relevance to query, and memory coverage (content already in memory is safer to drop). "
                "Returns a subset of messages fitting the target token budget."},
            {"inputSchema", {{"type", "object"},
                {"properties", {
                    {"messages", {{"type", "array"}, {"description", "Conversation messages [{role,content}]"},
                        {"items", {{"type", "object"}}}}},
                    {"query", {{"type", "string"}, {"description", "Upcoming task hint for semantic scoring"}}},
                    {"target_ratio", {{"type", "number"}, {"description", "Fraction of tokens to KEEP (default 0.4)"}}},
                    {"distill_novel", {{"type", "boolean"}, {"description", "Reserved for future use"}}}
                }}, {"required", {"messages"}}
            }}
        });
        handlers_["compact_context"] = [this](const json& p) { return tool_compact_context(p); };

        // ── Drift-memory tools ───────────────────────────────────────────────
        tools_.push_back({{"name","set_evidence_type"},{"description","Tag a memory with its epistemological evidence class (observation/inference/hearsay/authoritative/prediction)"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"},{"description","Memory ID"}}},
                {"evidence_type",{{"type","string"},{"description","One of: observation, inference, hearsay, authoritative, prediction"}}}
            }},{"required",{"id","evidence_type"}}}}});
        handlers_["set_evidence_type"] = [this](const json& p) { return tool_set_evidence_type(p); };

        tools_.push_back({{"name","get_evidence_type"},{"description","Retrieve the evidence type tag of a memory"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"},{"description","Memory ID"}}}
            }},{"required",{"id"}}}}});
        handlers_["get_evidence_type"] = [this](const json& p) { return tool_get_evidence_type(p); };

        tools_.push_back({{"name","labile_memories"},{"description","List memories recalled multiple times recently — candidates for reconsolidation (excludes freshly-written hook memories)"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"realm",{{"type","string"},{"description","Filter by realm"}}},
                {"limit",{{"type","integer"},{"description","Max results (default 20)"}}},
                {"window_hours",{{"type","number"},{"description","Recency window in hours (default 48)"}}},
                {"min_access",{{"type","integer"},{"description","Min recall count to qualify (default 2)"}}}
            }}}}});
        handlers_["labile_memories"] = [this](const json& p) { return tool_labile_memories(p); };

        tools_.push_back({{"name","reconsolidate"},{"description","Update content of a memory during its labile window (reconsolidation)"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"},{"description","Memory ID to update"}}},
                {"content",{{"type","string"},{"description","New/corrected content"}}},
                {"reason",{{"type","string"},{"description","Optional reason for reconsolidation"}}}
            }},{"required",{"id","content"}}}}});
        handlers_["reconsolidate"] = [this](const json& p) { return tool_reconsolidate(p); };

        tools_.push_back({{"name","5w_search"},{"description","Multi-dimensional semantic search across who/what/when/where/why axes"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"who",{{"type","string"},{"description","Who is involved"}}},
                {"what",{{"type","string"},{"description","What is happening/topic"}}},
                {"when",{{"type","string"},{"description","Temporal description"}}},
                {"where",{{"type","string"},{"description","Location or context"}}},
                {"why",{{"type","string"},{"description","Motivation or reason"}}},
                {"realm",{{"type","string"},{"description","Filter by realm"}}},
                {"limit",{{"type","integer"},{"description","Max results (default 10)"}}}
            }}}}});
        handlers_["5w_search"] = [this](const json& p) { return tool_5w_search(p); };

        tools_.push_back({{"name","recall_ucb1"},{"description","Recall with UCB1 exploration bonus — surfaces novel under-accessed memories alongside relevant ones"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"query",{{"type","string"},{"description","Search query"}}},
                {"realm",{{"type","string"},{"description","Filter by realm"}}},
                {"limit",{{"type","integer"},{"description","Max results (default 10)"}}},
                {"exploration",{{"type","number"},{"description","Exploration weight sqrt(2)≈1.414 (default)"}}},
                {"fetch_k",{{"type","integer"},{"description","Candidate pool size before re-ranking (default 40)"}}}
            }},{"required",{"query"}}}}});
        handlers_["recall_ucb1"] = [this](const json& p) { return tool_recall_ucb1(p); };

        tools_.push_back({{"name","find_near_duplicates"},{"description","Find memory pairs with high semantic similarity (near-duplicates)"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"realm",{{"type","string"},{"description","Filter by realm"}}},
                {"limit",{{"type","integer"},{"description","Max pairs to return (default 20)"}}},
                {"threshold",{{"type","number"},{"description","Cosine similarity threshold (default 0.90)"}}}
            }}}}});
        handlers_["find_near_duplicates"] = [this](const json& p) { return tool_find_near_duplicates(p); };

        tools_.push_back({{"name","consolidate_similar"},{"description","Merge near-duplicate memories — keeps stronger, soft-deletes weaker"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"realm",{{"type","string"},{"description","Filter by realm"}}},
                {"threshold",{{"type","number"},{"description","Similarity threshold (default 0.92)"}}},
                {"dry_run",{{"type","boolean"},{"description","Preview without deleting (default true)"}}},
                {"limit",{{"type","integer"},{"description","Max pairs to merge (default 10)"}}}
            }}}}});
        handlers_["consolidate_similar"] = [this](const json& p) { return tool_consolidate_similar(p); };

        tools_.push_back({{"name","cooccurrence_graph"},{"description","Show top co-activated memory associations for a given memory"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"},{"description","Memory ID"}}},
                {"limit",{{"type","integer"},{"description","Max edges to return (default 10)"}}}
            }},{"required",{"id"}}}}});
        handlers_["cooccurrence_graph"] = [this](const json& p) { return tool_cooccurrence_graph(p); };

        tools_.push_back({{"name","labile_memories_top"},{"description","List the most-accessed (most labile) memories — candidates for reconsolidation"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"realm",{{"type","string"},{"description","Filter by realm"}}},
                {"limit",{{"type","integer"},{"description","Max results (default 20)"}}}
            }}}}});
        handlers_["labile_memories_top"] = [this](const json& p) { return tool_labile_memories_top(p); };

        // ── Behavioral Probe ──────────────────────────────────────────────────
        tools_.push_back({{"name","probe_seed"},{"description","Store an exemplar text as a centroid for a behavioral class (sycophantic/hedging/shallow/direct). Bootstrap the probe with representative examples."},
            {"inputSchema",{{"type","object"},{"properties",{
                {"class",{{"type","string"},{"description","Behavioral class: sycophantic | hedging | shallow | direct"}}},
                {"text",{{"type","string"},{"description","Exemplar text for this class"}}},
                {"note",{{"type","string"},{"description","Optional annotation"}}}
            }},{"required",{"class","text"}}}}});
        handlers_["probe_seed"] = [this](const json& p) { return tool_probe_seed(p); };

        tools_.push_back({{"name","behavioral_probe"},{"description","Score text against behavioral centroid clusters. Returns per-class similarity scores (sycophantic/hedging/shallow/direct) and overall quality estimate. Requires prior probe_seed calls."},
            {"inputSchema",{{"type","object"},{"properties",{
                {"text",{{"type","string"},{"description","Text to probe (e.g. a Claude response)"}}}
            }},{"required",{"text"}}}}});
        handlers_["behavioral_probe"] = [this](const json& p) { return tool_behavioral_probe(p); };

        tools_.push_back({{"name","probe_calibrate"},{"description","Add a confirmed exemplar to a behavioral class to refine its centroid. Use when you have a clear example of the behavior."},
            {"inputSchema",{{"type","object"},{"properties",{
                {"class",{{"type","string"},{"description","Behavioral class to update"}}},
                {"text",{{"type","string"},{"description","Confirmed exemplar text"}}}
            }},{"required",{"class","text"}}}}});
        handlers_["probe_calibrate"] = [this](const json& p) { return tool_probe_calibrate(p); };

        tools_.push_back({{"name","probe_status"},{"description","Show how many exemplars exist per behavioral class. Use to verify the probe is seeded before running behavioral_probe."},
            {"inputSchema",{{"type","object"}}}});
        handlers_["probe_status"] = [this](const json& p) { return tool_probe_status(p); };

        // Contradiction engine
        tools_.push_back({{"name","why_active"},{"description","Explain why a memory is active: status, epistemic source, confirmations, contradictions"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"},{"description","Memory ID to inspect"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["why_active"] = [this](const json& p) { return tool_why_active(p); };

        tools_.push_back({{"name","what_superseded"},{"description","Show the full supersession chain for a memory"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"},{"description","Memory ID to trace"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["what_superseded"] = [this](const json& p) { return tool_what_superseded(p); };

        tools_.push_back({{"name","show_conflicts"},{"description","Semantic search + show contradiction pairs for matching memories"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"query",{{"type","string"},{"description","Search query"}}},
                {"limit",{{"type","integer"},{"description","Max memories to scan (default 20)"}}},
                {"realm",{{"type","string"},{"description","Filter by realm"}}}
            }},{"required",{"query"}}}}
        });
        handlers_["show_conflicts"] = [this](const json& p) { return tool_show_conflicts(p); };

        // Operator controls
        tools_.push_back({{"name","approve_memory"},{"description","Approve a Proposed memory, promoting it to Active"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"},{"description","Memory ID to approve"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["approve_memory"] = [this](const json& p) { return tool_approve_memory(p); };

        tools_.push_back({{"name","reject_memory"},{"description","Reject a Proposed memory, archiving it"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"},{"description","Memory ID to reject"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["reject_memory"] = [this](const json& p) { return tool_reject_memory(p); };

        tools_.push_back({{"name","promote_memory"},{"description","Promote a memory one tier: Proposed→Observed→Verified→Active"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"id",{{"type","string"},{"description","Memory ID"}}}
            }},{"required",{"id"}}}}
        });
        handlers_["promote_memory"] = [this](const json& p) { return tool_promote_memory(p); };

        tools_.push_back({{"name","conflict_inspector"},{"description","Semantic search + show status and contradiction partners for each hit"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"query",{{"type","string"},{"description","Search query"}}},
                {"limit",{{"type","integer"},{"description","Max memories to scan (default 10)"}}},
                {"realm",{{"type","string"},{"description","Filter by realm"}}}
            }},{"required",{"query"}}}}
        });
        handlers_["conflict_inspector"] = [this](const json& p) { return tool_conflict_inspector(p); };

        tools_.push_back({{"name","disable_source"},{"description","Add a source to the deny-list via triplet"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"source",{{"type","string"},{"description","Source identifier to deny"}}}
            }},{"required",{"source"}}}}
        });
        handlers_["disable_source"] = [this](const json& p) { return tool_disable_source(p); };

        // Override memory_history handler with richer operator version
        handlers_["memory_history"] = [this](const json& p) { return tool_operator_memory_history(p); };

        // ── Tier 1: Ingest source ──────────────────────────────────────────
        tools_.push_back({{"name","ingest_source"},
            {"description","Ingest external content (URL, file, directory) into memory via SSL distillation"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"source",{{"type","string"},{"description","URL, file path, or directory path"}}},
                {"realm",{{"type","string"},{"description","Target realm (default: brahman)"}}},
                {"type",{{"type","string"},{"description","Source type: auto|url|file|directory (default: auto)"}}},
                {"model",{{"type","string"},{"description","LLM model (default: gemma4:26b)"}}},
                {"endpoint",{{"type","string"},{"description","OpenAI-compatible endpoint (auto-discovered if empty)"}}},
                {"max_chunks",{{"type","integer"},{"description","Max chunks to process (default: 30)"}}}
            }},{"required",{"source"}}}}
        });
        handlers_["ingest_source"] = [this](const json& p) { return tool_ingest_source(p); };

        // ── Tier 2: Wiki export ────────────────────────────────────────────
        tools_.push_back({{"name","wiki_export"},
            {"description","Export memories as Obsidian-compatible .md wiki with backlinks"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"output_dir",{{"type","string"},{"description","Output directory (default: ~/.claude/wiki/)"}}},
                {"realm",{{"type","string"},{"description","Filter to specific realm (default: all)"}}},
                {"max_memories",{{"type","integer"},{"description","Max memories per realm (default: 5000)"}}}
            }}}}
        });
        handlers_["wiki_export"] = [this](const json& p) { return tool_wiki_export(p); };

        // ── Tier 3: Health-check sadhana ───────────────────────────────────
        tools_.push_back({{"name","health_check_start"},
            {"description","Start autonomous health-check sadhana that monitors memory quality, dedup ratio, and embedding coverage"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"interval_seconds",{{"type","integer"},{"description","Check interval in seconds (default: 3600)"}}},
                {"realm",{{"type","string"},{"description","Realm to monitor (default: brahman)"}}},
                {"max_turns",{{"type","integer"},{"description","Max check cycles (default: 0 = unlimited)"}}}
            }}}}
        });
        handlers_["health_check_start"] = [this](const json& p) { return tool_health_check_start(p); };

        // ── Tier 4: Export training pairs ──────────────────────────────────
        tools_.push_back({{"name","export_training_pairs"},
            {"description","Export query-passage pairs as JSONL for BGE embedding fine-tuning"},
            {"inputSchema",{{"type","object"},{"properties",{
                {"output_path",{{"type","string"},{"description","Output JSONL path (default: ~/.claude/training/pairs.jsonl)"}}},
                {"realm",{{"type","string"},{"description","Filter to specific realm (default: all)"}}},
                {"max_pairs",{{"type","integer"},{"description","Max pairs to export (default: 10000)"}}},
                {"min_confidence",{{"type","number"},{"description","Min confidence threshold (default: 0.5)"}}},
                {"include_negatives",{{"type","boolean"},{"description","Generate hard negatives (default: true)"}}}
            }}}}
        });
        handlers_["export_training_pairs"] = [this](const json& p) { return tool_export_training_pairs(p); };

        classify_tools();
    }

    void classify_tools() {
        static const std::vector<std::string> internal_tools = {
            "cleanup", "hygiene_run",
            "consolidation_scan", "consolidation_merge", "consolidation_auto",
            "batch_forget", "reembed_memories",
            "dedupe_symbols",
            "metacognition_corrections", "metacognition_outcomes",
            "distill_status", "enrichment_status", "epiplexity_check",
            "clear_codebase", "clear_triplets", "describe_symbol", "extract_symbols",
            "file_dependents", "file_imports", "resolve_callsites",
            "restore_code_intel_confidence", "ssl_convert", "subconscious_stats",
            "suggestion_count", "suggestion_pending", "suggestion_resolve", "suggestion_track",
            "transcript_get", "transcript_list", "transcript_parse", "transcript_register",
            "transcript_remove", "transcript_search", "transcript_update",
            "type_hierarchy", "version_check",
            "export_soul", "import_soul",
            "cycle", "anticipation_gate_status", "anticipation_record_outcome",
            "session_register", "session_heartbeat", "session_deregister", "msg_ack",
            "file_index_session", "file_index_all",
            "chitta_health",
            "ingest_source", "wiki_export", "health_check_start", "export_training_pairs"
        };

        static const std::vector<std::string> advanced_tools = {
            "strengthen", "weaken", "tag", "update", "get", "query_graph",
            "realm_add", "realm_detect", "realm_get", "realm_list", "realm_remove", "realm_set", "realm_visibility",
            "goal_set", "goal_get", "goal_list", "goal_complete", "goal_progress",
            "habit_observe", "habit_match", "habit_list", "habit_strengthen", "habit_weaken",
            "anticipation_predict", "anticipation_observe", "anticipation_list", "anticipation_success",
            "calibration_record", "calibration_score",
            "profile_get", "profile_observe", "profile_update",
            "curiosity_gaps", "curiosity_note_gap", "curiosity_resolve",
            "narrative_history",
            "memory_history", "memory_revert", "pin_memory", "unpin_memory", "list_pinned",
            "memory_lock", "memory_unlock", "memory_lock_status",
            "propose_change", "list_merge_queue", "resolve_merge",
            "file_timeline", "file_at_time", "file_restore"
        };

        for (const auto& name : internal_tools) tool_visibility_[name] = "internal";
        for (const auto& name : advanced_tools) tool_visibility_[name] = "advanced";
    }

};

} // namespace chitta
