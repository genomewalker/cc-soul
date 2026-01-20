#pragma once
// SimpleRpcHandler: Minimal RPC handler for SimpleMind
//
// Core tools only: remember, recall, connect, soul_context

#include "../mind/simple_mind.hpp"
#include "../mind/payload.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <chrono>
#include <iomanip>

namespace chitta {

using json = nlohmann::json;

struct SimpleToolResult {
    bool is_error = false;
    std::string text;
    json structured;

    static SimpleToolResult ok(const std::string& t, const json& s = json()) {
        return {false, t, s};
    }
    static SimpleToolResult error(const std::string& msg) {
        return {true, msg, json()};
    }
};

class SimpleRpcHandler {
public:
    explicit SimpleRpcHandler(SimpleMind* mind) : mind_(mind) {
        register_tools();
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
    SimpleMind* mind_;
    std::vector<json> tools_;
    std::unordered_map<std::string, std::function<SimpleToolResult(const json&)>> handlers_;

    void register_tools() {
        // remember
        tools_.push_back({
            {"name", "remember"},
            {"description", "Store text in memory with optional tags"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"content", {{"type", "string"}, {"description", "Text to remember"}}},
                    {"type", {{"type", "string"}, {"description", "Node type (wisdom, insight, signal, episode)"}}},
                    {"tags", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Optional tags"}}}
                }},
                {"required", {"content"}}
            }}
        });
        handlers_["remember"] = [this](const json& p) { return tool_remember(p); };

        // recall
        tools_.push_back({
            {"name", "recall"},
            {"description", "Search memory by semantic similarity"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Search query"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default 10)"}}},
                    {"tag", {{"type", "string"}, {"description", "Filter by tag"}}}
                }},
                {"required", {"query"}}
            }}
        });
        handlers_["recall"] = [this](const json& p) { return tool_recall(p); };

        // connect
        tools_.push_back({
            {"name", "connect"},
            {"description", "Create a triplet relationship"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"subject", {{"type", "string"}, {"description", "Subject entity"}}},
                    {"predicate", {{"type", "string"}, {"description", "Relationship type"}}},
                    {"object", {{"type", "string"}, {"description", "Object entity"}}}
                }},
                {"required", {"subject", "predicate", "object"}}
            }}
        });
        handlers_["connect"] = [this](const json& p) { return tool_connect(p); };

        // query_graph
        tools_.push_back({
            {"name", "query_graph"},
            {"description", "Query triplets by subject or object"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"subject", {{"type", "string"}, {"description", "Query by subject"}}},
                    {"object", {{"type", "string"}, {"description", "Query by object"}}}
                }}
            }}
        });
        handlers_["query_graph"] = [this](const json& p) { return tool_query_graph(p); };

        // soul_context
        tools_.push_back({
            {"name", "soul_context"},
            {"description", "Get current soul state and statistics"},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}
        });
        handlers_["soul_context"] = [this](const json& p) { return tool_soul_context(p); };

        // strengthen
        tools_.push_back({
            {"name", "strengthen"},
            {"description", "Increase confidence of a memory"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Node ID"}}},
                    {"amount", {{"type", "number"}, {"description", "Amount to strengthen (default 0.1)"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["strengthen"] = [this](const json& p) { return tool_strengthen(p); };

        // weaken
        tools_.push_back({
            {"name", "weaken"},
            {"description", "Decrease confidence of a memory"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Node ID"}}},
                    {"amount", {{"type", "number"}, {"description", "Amount to weaken (default 0.1)"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["weaken"] = [this](const json& p) { return tool_weaken(p); };

        // forget
        tools_.push_back({
            {"name", "forget"},
            {"description", "Remove a memory"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Node ID to forget"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["forget"] = [this](const json& p) { return tool_forget(p); };

        // observe - Critical for [LEARN] blocks from stop-hook
        tools_.push_back({
            {"name", "observe"},
            {"description", "Store an observation/learning (used by hooks for [LEARN] extraction)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"category", {{"type", "string"}, {"description", "Category: wisdom, insight, signal, episode"}}},
                    {"title", {{"type", "string"}, {"description", "Title/summary"}}},
                    {"content", {{"type", "string"}, {"description", "Full content"}}},
                    {"tags", {{"type", "string"}, {"description", "Comma-separated tags"}}}
                }},
                {"required", {"title", "content"}}
            }}
        });
        handlers_["observe"] = [this](const json& p) { return tool_observe(p); };

        // full_resonate - For hooks to get contextual recall
        tools_.push_back({
            {"name", "full_resonate"},
            {"description", "Semantic search with full context (for hooks)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Search query"}}},
                    {"k", {{"type", "integer"}, {"description", "Max results"}}}
                }},
                {"required", {"query"}}
            }}
        });
        handlers_["full_resonate"] = [this](const json& p) { return tool_full_resonate(p); };

        // checkpoint - Upsert continuation node for session continuity
        tools_.push_back({
            {"name", "checkpoint"},
            {"description", "Update the continuation node (session state across restarts/compacts)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"topic", {{"type", "string"}, {"description", "Current topic (one sentence)"}}},
                    {"state", {{"type", "string"}, {"description", "What we're doing now"}}},
                    {"decisions", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Key decisions made"}}},
                    {"next", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Next steps"}}}
                }},
                {"required", {"topic", "state"}}
            }}
        });
        handlers_["checkpoint"] = [this](const json& p) { return tool_checkpoint(p); };
    }

    // Tool implementations
    SimpleToolResult tool_remember(const json& params) {
        std::string content = params.value("content", "");
        if (content.empty()) {
            return SimpleToolResult::error("Content is required");
        }

        std::string type_str = params.value("type", "episode");
        NodeType type = NodeType::Episode;
        if (type_str == "wisdom") type = NodeType::Wisdom;
        else if (type_str == "belief") type = NodeType::Belief;
        else if (type_str == "intention") type = NodeType::Intention;

        NodeId id;
        if (params.contains("tags") && params["tags"].is_array()) {
            std::vector<std::string> tags;
            for (const auto& t : params["tags"]) {
                if (t.is_string()) tags.push_back(t.get<std::string>());
            }
            id = mind_->remember(content, type, tags);
        } else {
            id = mind_->remember(content, type);
        }

        // Check if ID is valid (empty UUID = failure)
        static const NodeId null_id{};
        if (id == null_id) {
            return SimpleToolResult::error("Failed to remember (quality gate or embedding failed)");
        }

        std::string preview = content.substr(0, 50);
        if (content.size() > 50) preview += "...";

        return SimpleToolResult::ok(
            "Remembered: " + preview,
            {{"id", id.to_string()}}
        );
    }

    SimpleToolResult tool_recall(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) {
            return SimpleToolResult::error("Query is required");
        }

        size_t limit = params.value("limit", 10);
        std::string tag = params.value("tag", "");

        std::vector<Recall> results;
        if (!tag.empty()) {
            // Tag-filtered recall not in search yet, do manual filter
            auto all = mind_->recall(query, limit * 2);
            for (const auto& r : all) {
                auto tags = mind_->get_tags(r.id);
                if (std::find(tags.begin(), tags.end(), tag) != tags.end()) {
                    results.push_back(r);
                    if (results.size() >= limit) break;
                }
            }
        } else {
            results = mind_->recall(query, limit);
        }

        std::ostringstream ss;
        ss << "Found " << results.size() << " results:\n";

        json results_json = json::array();
        for (const auto& r : results) {
            int pct = static_cast<int>(std::min(r.relevance, 1.0f) * 100);
            std::string type_name = node_type_name(r.type);

            ss << "[" << pct << "%] [" << type_name << "] "
               << r.text.substr(0, 100);
            if (r.text.size() > 100) ss << "...";
            ss << "\n";

            results_json.push_back({
                {"id", r.id.to_string()},
                {"relevance", r.relevance},
                {"similarity", r.similarity},
                {"type", type_name},
                {"text", r.text}
            });
        }

        return SimpleToolResult::ok(ss.str(), {{"results", results_json}});
    }

    SimpleToolResult tool_connect(const json& params) {
        std::string subject = params.value("subject", "");
        std::string predicate = params.value("predicate", "");
        std::string object = params.value("object", "");

        if (subject.empty() || predicate.empty() || object.empty()) {
            return SimpleToolResult::error("Subject, predicate, and object are required");
        }

        bool ok = mind_->connect(subject, predicate, object);
        if (!ok) {
            return SimpleToolResult::error("Failed to create triplet");
        }

        return SimpleToolResult::ok(
            "Connected: " + subject + " → " + predicate + " → " + object,
            {{"subject", subject}, {"predicate", predicate}, {"object", object}}
        );
    }

    SimpleToolResult tool_query_graph(const json& params) {
        std::string subject = params.value("subject", "");
        std::string object = params.value("object", "");

        json results_json = json::array();
        std::ostringstream ss;

        if (!subject.empty()) {
            auto results = mind_->query_subject(subject);
            ss << "Triplets with subject '" << subject << "':\n";
            for (const auto& [pred, obj, weight] : results) {
                ss << "  → " << pred << " → " << obj << "\n";
                results_json.push_back({
                    {"subject", subject},
                    {"predicate", pred},
                    {"object", obj},
                    {"weight", weight}
                });
            }
        }

        if (!object.empty()) {
            auto results = mind_->query_object(object);
            ss << "Triplets with object '" << object << "':\n";
            for (const auto& [subj, pred, weight] : results) {
                ss << "  " << subj << " → " << pred << " →\n";
                results_json.push_back({
                    {"subject", subj},
                    {"predicate", pred},
                    {"object", object},
                    {"weight", weight}
                });
            }
        }

        if (subject.empty() && object.empty()) {
            return SimpleToolResult::error("Either subject or object is required");
        }

        return SimpleToolResult::ok(ss.str(), {{"triplets", results_json}});
    }

    SimpleToolResult tool_soul_context(const json&) {
        SimpleHealth h = mind_->health();

        std::ostringstream ss;
        ss << "Soul State:\n";
        ss << "  Nodes: " << h.total_nodes << " total";
        if (h.total_nodes > 0) {
            ss << ", " << h.active_nodes << " active";
            if (h.weak_nodes > 0) ss << ", " << h.weak_nodes << " weak";
            if (h.stale_nodes > 0) ss << ", " << h.stale_nodes << " stale";
        }
        ss << "\n";
        ss << "  Confidence: " << std::fixed << std::setprecision(2) << h.avg_confidence << " avg\n";
        ss << "  Triplets: " << mind_->graph().triplet_count() << "\n";
        ss << "  Yantra: " << (mind_->has_yantra() ? "ready" : "not attached") << "\n";
        ss << "  Status: " << h.status() << "\n";

        return SimpleToolResult::ok(ss.str(), {
            {"total_nodes", h.total_nodes},
            {"active_nodes", h.active_nodes},
            {"weak_nodes", h.weak_nodes},
            {"stale_nodes", h.stale_nodes},
            {"avg_confidence", h.avg_confidence},
            {"triplet_count", mind_->graph().triplet_count()},
            {"yantra_ready", mind_->has_yantra()},
            {"status", h.status()}
        });
    }

    SimpleToolResult tool_strengthen(const json& params) {
        std::string id_str = params.value("id", "");
        if (id_str.empty()) {
            return SimpleToolResult::error("ID is required");
        }

        NodeId id = NodeId::from_string(id_str);
        float amount = params.value("amount", 0.1f);

        if (!mind_->strengthen(id, amount)) {
            return SimpleToolResult::error("Node not found");
        }

        return SimpleToolResult::ok("Strengthened node " + id_str);
    }

    SimpleToolResult tool_weaken(const json& params) {
        std::string id_str = params.value("id", "");
        if (id_str.empty()) {
            return SimpleToolResult::error("ID is required");
        }

        NodeId id = NodeId::from_string(id_str);
        float amount = params.value("amount", 0.1f);

        if (!mind_->weaken(id, amount)) {
            return SimpleToolResult::error("Node not found");
        }

        return SimpleToolResult::ok("Weakened node " + id_str);
    }

    SimpleToolResult tool_forget(const json& params) {
        std::string id_str = params.value("id", "");
        if (id_str.empty()) {
            return SimpleToolResult::error("ID is required");
        }

        NodeId id = NodeId::from_string(id_str);
        if (!mind_->remove(id)) {
            return SimpleToolResult::error("Node not found");
        }

        return SimpleToolResult::ok("Forgot node " + id_str);
    }

    // observe - stores [LEARN] blocks from hooks
    SimpleToolResult tool_observe(const json& params) {
        std::string title = params.value("title", "");
        std::string content = params.value("content", "");
        std::string category = params.value("category", "episode");
        std::string tags_str = params.value("tags", "");

        if (title.empty() || content.empty()) {
            return SimpleToolResult::error("Title and content are required");
        }

        // Map category to NodeType
        NodeType type = NodeType::Episode;
        if (category == "wisdom" || category == "insight") type = NodeType::Wisdom;
        else if (category == "belief") type = NodeType::Belief;
        else if (category == "decision") type = NodeType::Wisdom;
        else if (category == "signal") type = NodeType::Episode;

        // Parse tags
        std::vector<std::string> tags;
        if (!tags_str.empty()) {
            std::stringstream ss(tags_str);
            std::string tag;
            while (std::getline(ss, tag, ',')) {
                // Trim whitespace
                size_t start = tag.find_first_not_of(" \t");
                size_t end = tag.find_last_not_of(" \t");
                if (start != std::string::npos) {
                    tags.push_back(tag.substr(start, end - start + 1));
                }
            }
        }

        // Store: title + newline + content
        std::string full_text = title + "\n" + content;

        NodeId id;
        if (!tags.empty()) {
            id = mind_->remember(full_text, type, tags);
        } else {
            id = mind_->remember(full_text, type);
        }

        static const NodeId null_id{};
        if (id == null_id) {
            return SimpleToolResult::error("Failed to observe (quality gate or embedding failed)");
        }

        return SimpleToolResult::ok(
            "Observed: " + title.substr(0, 50),
            {{"id", id.to_string()}, {"category", category}}
        );
    }

    // full_resonate - contextual recall for hooks
    SimpleToolResult tool_full_resonate(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) {
            return SimpleToolResult::error("Query is required");
        }

        size_t k = params.value("k", 10);
        auto results = mind_->recall(query, k);

        if (results.empty()) {
            return SimpleToolResult::ok("No memories found matching query.", {{"results", json::array()}});
        }

        std::ostringstream ss;
        ss << "[I know]\n";
        ss << "Found " << results.size() << " results:\n\n";

        json results_json = json::array();
        for (const auto& r : results) {
            int pct = static_cast<int>(std::min(r.relevance, 1.0f) * 100);
            std::string type_name = node_type_name(r.type);

            ss << "[" << pct << "%] [" << type_name << "] "
               << r.text.substr(0, 200);
            if (r.text.size() > 200) ss << "...";
            ss << "\n\n";

            results_json.push_back({
                {"id", r.id.to_string()},
                {"relevance", r.relevance},
                {"type", type_name},
                {"text", r.text}
            });
        }

        return SimpleToolResult::ok(ss.str(), {{"results", results_json}});
    }

    // checkpoint - upserts the continuation node
    SimpleToolResult tool_checkpoint(const json& params) {
        std::string topic = params.value("topic", "");
        std::string state = params.value("state", "");

        if (topic.empty() || state.empty()) {
            return SimpleToolResult::error("Topic and state are required");
        }

        // Build continuation content
        std::ostringstream content;
        content << "[CONTINUATION]\n";
        content << "topic: " << topic << "\n";
        content << "state: " << state << "\n";

        content << "decisions:\n";
        if (params.contains("decisions") && params["decisions"].is_array()) {
            for (const auto& d : params["decisions"]) {
                if (d.is_string()) {
                    content << "- " << d.get<std::string>() << "\n";
                }
            }
        }

        content << "next:\n";
        if (params.contains("next") && params["next"].is_array()) {
            for (const auto& n : params["next"]) {
                if (n.is_string()) {
                    content << "- " << n.get<std::string>() << "\n";
                }
            }
        }

        // Add timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        content << "timestamp: " << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ") << "\n";

        // First, forget any existing continuation nodes
        auto existing = mind_->recall("continuity:current", 5);
        for (const auto& r : existing) {
            auto tags = mind_->get_tags(r.id);
            for (const auto& tag : tags) {
                if (tag == "continuity:current") {
                    mind_->remove(r.id);
                    break;
                }
            }
        }

        // Store new continuation node
        std::vector<std::string> tags = {"continuity:current", "role:continuation"};
        NodeId id = mind_->remember(content.str(), NodeType::Wisdom, tags);

        static const NodeId null_id{};
        if (id == null_id) {
            return SimpleToolResult::error("Failed to store checkpoint");
        }

        return SimpleToolResult::ok(
            "Checkpoint saved: " + topic.substr(0, 50),
            {{"id", id.to_string()}, {"topic", topic}}
        );
    }

    // Helper functions
    json tool_list() {
        return {{"tools", tools_}};
    }

    json make_response(const json& id, const json& result) {
        return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    }

    json make_error(const json& id, int code, const std::string& msg) {
        return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", msg}}}};
    }

    json make_tool_response(const json& id, const SimpleToolResult& result) {
        json content = json::array();
        content.push_back({{"type", "text"}, {"text", result.text}});

        return {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", {
                {"content", content},
                {"isError", result.is_error},
                {"structured", result.structured}
            }}
        };
    }

    static std::string node_type_name(NodeType t) {
        switch (t) {
            case NodeType::Wisdom: return "wisdom";
            case NodeType::Belief: return "belief";
            case NodeType::Intention: return "intention";
            case NodeType::Aspiration: return "aspiration";
            case NodeType::Episode: return "episode";
            case NodeType::Symbol: return "symbol";
            case NodeType::Dream: return "dream";
            case NodeType::Gap: return "gap";
            case NodeType::Question: return "question";
            default: return "unknown";
        }
    }
};

}  // namespace chitta
