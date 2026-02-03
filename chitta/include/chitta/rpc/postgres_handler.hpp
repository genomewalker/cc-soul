#pragma once
// PostgresRpcHandler: RPC handler for PostgresMind
//
// Same interface as DuckDBRpcHandler but uses PostgresMind backend.

#include "../mind/postgres_mind.hpp"
#include "../mind/payload.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <iomanip>

namespace chitta {

using json = nlohmann::json;

class PostgresRpcHandler {
public:
    explicit PostgresRpcHandler(PostgresMind* mind) : mind_(mind) {
        register_tools();
    }

    json handle(const json& request) {
        std::string method = request.value("method", "");
        json params = request.value("params", json::object());
        auto id = request.value("id", json());

        if (method == "tools/list") {
            return make_response(id, {{"tools", tools_}});
        }

        if (method == "tools/call") {
            std::string name = params.value("name", "");
            json args = params.value("arguments", json::object());

            auto it = handlers_.find(name);
            if (it == handlers_.end()) {
                return make_error(id, -32601, "Unknown tool: " + name);
            }

            auto [is_error, text, structured] = it->second(args);
            json content = json::array();
            content.push_back({{"type", "text"}, {"text", text}});
            return {
                {"jsonrpc", "2.0"},
                {"id", id},
                {"result", {{"content", content}, {"isError", is_error}, {"structured", structured}}}
            };
        }

        return make_error(id, -32601, "Unknown method: " + method);
    }

private:
    PostgresMind* mind_;
    std::vector<json> tools_;
    std::unordered_map<std::string, std::function<std::tuple<bool, std::string, json>(const json&)>> handlers_;

    // Category to confidence mapping for high-value learnings
    static float category_to_confidence(const std::string& category) {
        if (category == "correction") return 0.95f;
        if (category == "preference") return 0.90f;
        if (category == "solution")   return 0.90f;
        if (category == "decision")   return 0.85f;
        if (category == "failure")    return 0.85f;
        if (category == "episode")    return 0.70f;
        return 0.80f;  // wisdom, insight, belief, etc.
    }

    void register_tools() {
        // remember
        tools_.push_back({
            {"name", "remember"},
            {"description", "Store text in memory"},
            {"inputSchema", {{"type", "object"}, {"properties", {
                {"content", {{"type", "string"}}},
                {"type", {{"type", "string"}}}
            }}, {"required", {"content"}}}}
        });
        handlers_["remember"] = [this](const json& p) {
            std::string content = p.value("content", "");
            if (content.empty()) return std::make_tuple(true, std::string("Content required"), json());

            std::string type_str = p.value("type", "episode");
            NodeType type = NodeType::Episode;
            if (type_str == "wisdom") type = NodeType::Wisdom;
            else if (type_str == "belief") type = NodeType::Belief;

            NodeId id = mind_->remember(content, type);
            if (!id.valid()) return std::make_tuple(true, std::string("Failed"), json());

            return std::make_tuple(false, "Remembered: " + content.substr(0, 50), json{{"id", id.to_string()}});
        };

        // recall
        tools_.push_back({
            {"name", "recall"},
            {"description", "Search memory"},
            {"inputSchema", {{"type", "object"}, {"properties", {
                {"query", {{"type", "string"}}},
                {"limit", {{"type", "integer"}}}
            }}, {"required", {"query"}}}}
        });
        handlers_["recall"] = [this](const json& p) {
            std::string query = p.value("query", "");
            if (query.empty()) return std::make_tuple(true, std::string("Query required"), json());

            size_t limit = p.value("limit", 10);
            auto results = mind_->recall(query, limit);

            std::ostringstream ss;
            json arr = json::array();
            for (const auto& r : results) {
                int pct = static_cast<int>(r.relevance * 100);
                ss << "[" << pct << "%] " << r.text.substr(0, 80) << "\n";
                arr.push_back({{"id", r.id.to_string()}, {"relevance", r.relevance}, {"text", r.text}});
            }
            return std::make_tuple(false, ss.str(), json{{"results", arr}});
        };

        // connect
        tools_.push_back({
            {"name", "connect"},
            {"description", "Create triplet"},
            {"inputSchema", {{"type", "object"}, {"properties", {
                {"subject", {{"type", "string"}}},
                {"predicate", {{"type", "string"}}},
                {"object", {{"type", "string"}}}
            }}, {"required", {"subject", "predicate", "object"}}}}
        });
        handlers_["connect"] = [this](const json& p) {
            std::string s = p.value("subject", "");
            std::string pr = p.value("predicate", "");
            std::string o = p.value("object", "");
            if (s.empty() || pr.empty() || o.empty())
                return std::make_tuple(true, std::string("Subject, predicate, object required"), json());

            if (!mind_->connect(s, pr, o))
                return std::make_tuple(true, std::string("Failed"), json());

            return std::make_tuple(false, "Connected: " + s + " → " + pr + " → " + o, json());
        };

        // soul_context
        tools_.push_back({
            {"name", "soul_context"},
            {"description", "Get soul state"},
            {"inputSchema", {{"type", "object"}}}
        });
        handlers_["soul_context"] = [this](const json&) {
            auto h = mind_->health();
            std::ostringstream ss;
            ss << "Soul (PostgreSQL):\n"
               << "  Nodes: " << h.total_nodes << "\n"
               << "  Triplets: " << mind_->triplet_count() << "\n"
               << "  Yantra: " << (mind_->has_yantra() ? "ready" : "no") << "\n";
            return std::make_tuple(false, ss.str(), json{
                {"total_nodes", h.total_nodes},
                {"triplet_count", mind_->triplet_count()},
                {"yantra_ready", mind_->has_yantra()}
            });
        };

        // observe (for hooks)
        tools_.push_back({
            {"name", "observe"},
            {"description", "Store observation"},
            {"inputSchema", {{"type", "object"}, {"properties", {
                {"title", {{"type", "string"}}},
                {"content", {{"type", "string"}}},
                {"category", {{"type", "string"}, {"description", "Category: correction, preference, solution, decision, failure, wisdom, episode"}}},
                {"confidence", {{"type", "number"}, {"description", "Optional confidence override (0.0-1.0)"}}}
            }}, {"required", {"title", "content"}}}}
        });
        handlers_["observe"] = [this](const json& p) {
            std::string title = p.value("title", "");
            std::string content = p.value("content", "");
            std::string cat = p.value("category", "wisdom");
            if (title.empty() || content.empty())
                return std::make_tuple(true, std::string("Title and content required"), json());

            // Derive confidence from category (or use explicit override)
            float confidence = p.contains("confidence")
                ? p.value("confidence", 0.8f)
                : category_to_confidence(cat);

            // Map category to NodeType
            NodeType type = NodeType::Wisdom;
            if (cat == "episode") type = NodeType::Episode;
            else if (cat == "belief") type = NodeType::Belief;

            NodeId id = mind_->remember(title + "\n" + content, type, confidence);
            if (!id.valid()) return std::make_tuple(true, std::string("Failed"), json());

            return std::make_tuple(false, "Observed: " + title.substr(0, 50),
                json{{"id", id.to_string()}, {"category", cat}, {"confidence", confidence}});
        };

        // full_resonate (for hooks)
        tools_.push_back({
            {"name", "full_resonate"},
            {"description", "Semantic search for hooks"},
            {"inputSchema", {{"type", "object"}, {"properties", {
                {"query", {{"type", "string"}}},
                {"k", {{"type", "integer"}}}
            }}, {"required", {"query"}}}}
        });
        handlers_["full_resonate"] = [this](const json& p) {
            std::string query = p.value("query", "");
            if (query.empty()) return std::make_tuple(true, std::string("Query required"), json());

            size_t k = p.value("k", 10);
            auto results = mind_->recall(query, k);

            std::ostringstream ss;
            ss << "[I know]\n";
            json arr = json::array();
            for (const auto& r : results) {
                int pct = static_cast<int>(r.relevance * 100);
                ss << "[" << pct << "%] " << r.text.substr(0, 150) << "\n\n";
                arr.push_back({{"id", r.id.to_string()}, {"relevance", r.relevance}, {"text", r.text}});
            }
            return std::make_tuple(false, ss.str(), json{{"results", arr}});
        };
    }

    json make_response(const json& id, const json& result) {
        return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    }

    json make_error(const json& id, int code, const std::string& msg) {
        return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", msg}}}};
    }
};

}  // namespace chitta
