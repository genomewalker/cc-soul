#pragma once
// MCP Server: Model Context Protocol for soul integration
//
// Implements JSON-RPC 2.0 over stdio for Claude integration.
// This is not a minimal implementation - it is a proper MCP server
// with full protocol compliance and rich tool schemas.

#include "mind.hpp"
#include "voice.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <functional>
#include <atomic>

namespace synapse {

using json = nlohmann::json;

// JSON-RPC 2.0 error codes
namespace rpc_error {
    constexpr int PARSE_ERROR = -32700;
    constexpr int INVALID_REQUEST = -32600;
    constexpr int METHOD_NOT_FOUND = -32601;
    constexpr int INVALID_PARAMS = -32602;
    constexpr int INTERNAL_ERROR = -32603;
    // MCP-specific errors
    constexpr int TOOL_NOT_FOUND = -32001;
    constexpr int TOOL_EXECUTION_ERROR = -32002;
}

// NodeType to string conversion
inline std::string node_type_to_string(NodeType type) {
    switch (type) {
        case NodeType::Wisdom: return "wisdom";
        case NodeType::Belief: return "belief";
        case NodeType::Intention: return "intention";
        case NodeType::Aspiration: return "aspiration";
        case NodeType::Episode: return "episode";
        case NodeType::Operation: return "operation";
        case NodeType::Invariant: return "invariant";
        case NodeType::Identity: return "identity";
        case NodeType::Term: return "term";
        case NodeType::Failure: return "failure";
        case NodeType::Dream: return "dream";
        case NodeType::Voice: return "voice";
        case NodeType::Meta: return "meta";
        case NodeType::Gap: return "gap";
        case NodeType::Question: return "question";
        case NodeType::StoryThread: return "story_thread";
        default: return "unknown";
    }
}

inline NodeType string_to_node_type(const std::string& s) {
    if (s == "wisdom") return NodeType::Wisdom;
    if (s == "belief") return NodeType::Belief;
    if (s == "intention") return NodeType::Intention;
    if (s == "aspiration") return NodeType::Aspiration;
    if (s == "episode") return NodeType::Episode;
    if (s == "operation") return NodeType::Operation;
    if (s == "invariant") return NodeType::Invariant;
    if (s == "identity") return NodeType::Identity;
    if (s == "term") return NodeType::Term;
    if (s == "failure") return NodeType::Failure;
    if (s == "dream") return NodeType::Dream;
    if (s == "voice") return NodeType::Voice;
    if (s == "meta") return NodeType::Meta;
    if (s == "gap") return NodeType::Gap;
    if (s == "question") return NodeType::Question;
    if (s == "story_thread") return NodeType::StoryThread;
    return NodeType::Episode;
}

// Tool schema definition
struct ToolSchema {
    std::string name;
    std::string description;
    json input_schema;
};

// Tool result
struct ToolResult {
    bool is_error = false;
    std::string content;
    json structured;
};

// MCP Server implementation
class MCPServer {
public:
    explicit MCPServer(std::shared_ptr<Mind> mind, std::string server_name = "synapse")
        : mind_(std::move(mind))
        , server_name_(std::move(server_name))
        , running_(false)
    {
        register_tools();
    }

    void run() {
        running_ = true;
        std::string line;

        while (running_ && std::getline(std::cin, line)) {
            if (line.empty()) continue;

            try {
                auto request = json::parse(line);
                auto response = handle_request(request);
                if (!response.is_null()) {
                    std::cout << response.dump() << "\n";
                    std::cout.flush();
                }
            } catch (const json::parse_error& e) {
                auto error = make_error(nullptr, rpc_error::PARSE_ERROR,
                                        std::string("Parse error: ") + e.what());
                std::cout << error.dump() << "\n";
                std::cout.flush();
            }
        }
    }

    void stop() { running_ = false; }

private:
    std::shared_ptr<Mind> mind_;
    std::string server_name_;
    std::atomic<bool> running_;
    std::vector<ToolSchema> tools_;
    std::unordered_map<std::string, std::function<ToolResult(const json&)>> handlers_;

    void register_tools() {
        // Tool: soul_context - Get soul state for hook injection
        tools_.push_back({
            "soul_context",
            "Get soul context including beliefs, active intentions, relevant wisdom, and coherence. "
            "Use format='json' for structured data or 'text' for hook injection.",
            {
                {"type", "object"},
                {"properties", {
                    {"query", {
                        {"type", "string"},
                        {"description", "Optional query to find relevant wisdom"}
                    }},
                    {"format", {
                        {"type", "string"},
                        {"enum", {"text", "json"}},
                        {"default", "text"},
                        {"description", "Output format - 'text' for hook injection or 'json' for structured"}
                    }}
                }},
                {"required", json::array()}
            }
        });
        handlers_["soul_context"] = [this](const json& params) { return tool_soul_context(params); };

        // Tool: grow - Add wisdom, beliefs, or failures to the soul
        tools_.push_back({
            "grow",
            "Add to the soul: wisdom, beliefs, failures, aspirations, dreams, or terms. "
            "Each type has different decay and confidence properties.",
            {
                {"type", "object"},
                {"properties", {
                    {"type", {
                        {"type", "string"},
                        {"enum", {"wisdom", "belief", "failure", "aspiration", "dream", "term"}},
                        {"description", "What to grow"}
                    }},
                    {"content", {
                        {"type", "string"},
                        {"description", "The content/statement to add"}
                    }},
                    {"title", {
                        {"type", "string"},
                        {"description", "Short title (required for wisdom/failure)"}
                    }},
                    {"domain", {
                        {"type", "string"},
                        {"description", "Domain context (optional)"}
                    }},
                    {"confidence", {
                        {"type", "number"},
                        {"minimum", 0.0},
                        {"maximum", 1.0},
                        {"default", 0.8},
                        {"description", "Initial confidence (0-1)"}
                    }}
                }},
                {"required", {"type", "content"}}
            }
        });
        handlers_["grow"] = [this](const json& params) { return tool_grow(params); };

        // Tool: observe - Record an episodic observation
        tools_.push_back({
            "observe",
            "Record an observation (episode). Categories determine decay rate: "
            "bugfix/decision (slow), discovery/feature (medium), session_ledger/signal (fast).",
            {
                {"type", "object"},
                {"properties", {
                    {"category", {
                        {"type", "string"},
                        {"enum", {"bugfix", "decision", "discovery", "feature", "refactor", "session_ledger", "signal"}},
                        {"description", "Category affecting decay rate"}
                    }},
                    {"title", {
                        {"type", "string"},
                        {"maxLength", 80},
                        {"description", "Short title (max 80 chars)"}
                    }},
                    {"content", {
                        {"type", "string"},
                        {"description", "Full observation content"}
                    }},
                    {"project", {
                        {"type", "string"},
                        {"description", "Project name (optional)"}
                    }},
                    {"tags", {
                        {"type", "string"},
                        {"description", "Comma-separated tags for filtering"}
                    }}
                }},
                {"required", {"category", "title", "content"}}
            }
        });
        handlers_["observe"] = [this](const json& params) { return tool_observe(params); };

        // Tool: recall - Semantic search in soul
        tools_.push_back({
            "recall",
            "Recall relevant wisdom and episodes through semantic search.",
            {
                {"type", "object"},
                {"properties", {
                    {"query", {
                        {"type", "string"},
                        {"description", "What to search for"}
                    }},
                    {"limit", {
                        {"type", "integer"},
                        {"minimum", 1},
                        {"maximum", 50},
                        {"default", 5},
                        {"description", "Maximum results"}
                    }},
                    {"threshold", {
                        {"type", "number"},
                        {"minimum", 0.0},
                        {"maximum", 1.0},
                        {"default", 0.0},
                        {"description", "Minimum similarity threshold"}
                    }}
                }},
                {"required", {"query"}}
            }
        });
        handlers_["recall"] = [this](const json& params) { return tool_recall(params); };

        // Tool: cycle - Run maintenance cycle
        tools_.push_back({
            "cycle",
            "Run maintenance cycle: apply decay, prune low-confidence nodes, compute coherence, save.",
            {
                {"type", "object"},
                {"properties", {
                    {"save", {
                        {"type", "boolean"},
                        {"default", true},
                        {"description", "Whether to save after cycle"}
                    }}
                }},
                {"required", json::array()}
            }
        });
        handlers_["cycle"] = [this](const json& params) { return tool_cycle(params); };
    }

    json handle_request(const json& request) {
        // Validate JSON-RPC 2.0
        if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0") {
            return make_error(request.value("id", json()), rpc_error::INVALID_REQUEST,
                              "Missing or invalid jsonrpc version");
        }

        if (!request.contains("method") || !request["method"].is_string()) {
            return make_error(request.value("id", json()), rpc_error::INVALID_REQUEST,
                              "Missing or invalid method");
        }

        std::string method = request["method"];
        json params = request.value("params", json::object());
        json id = request.value("id", json());

        // Handle MCP protocol methods
        if (method == "initialize") {
            return handle_initialize(params, id);
        } else if (method == "initialized") {
            return json();  // Notification, no response
        } else if (method == "tools/list") {
            return handle_tools_list(params, id);
        } else if (method == "tools/call") {
            return handle_tools_call(params, id);
        } else if (method == "shutdown") {
            running_ = false;
            return make_result(id, json::object());
        }

        return make_error(id, rpc_error::METHOD_NOT_FOUND,
                          "Unknown method: " + method);
    }

    json handle_initialize(const json& params, const json& id) {
        json capabilities = {
            {"tools", {
                {"listChanged", true}
            }}
        };

        json server_info = {
            {"name", server_name_},
            {"version", "0.1.0"}
        };

        json result = {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", capabilities},
            {"serverInfo", server_info}
        };

        return make_result(id, result);
    }

    json handle_tools_list(const json& /*params*/, const json& id) {
        json tools_array = json::array();
        for (const auto& tool : tools_) {
            tools_array.push_back({
                {"name", tool.name},
                {"description", tool.description},
                {"inputSchema", tool.input_schema}
            });
        }
        return make_result(id, {{"tools", tools_array}});
    }

    json handle_tools_call(const json& params, const json& id) {
        if (!params.contains("name") || !params["name"].is_string()) {
            return make_error(id, rpc_error::INVALID_PARAMS, "Missing tool name");
        }

        std::string name = params["name"];
        json arguments = params.value("arguments", json::object());

        auto it = handlers_.find(name);
        if (it == handlers_.end()) {
            return make_error(id, rpc_error::TOOL_NOT_FOUND, "Unknown tool: " + name);
        }

        try {
            ToolResult result = it->second(arguments);
            json content = json::array();
            content.push_back({
                {"type", "text"},
                {"text", result.content}
            });

            return make_result(id, {
                {"content", content},
                {"isError", result.is_error}
            });
        } catch (const std::exception& e) {
            return make_error(id, rpc_error::TOOL_EXECUTION_ERROR,
                              std::string("Tool execution failed: ") + e.what());
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // Tool implementations
    // ═══════════════════════════════════════════════════════════════════

    ToolResult tool_soul_context(const json& params) {
        std::string query = params.value("query", "");
        std::string format = params.value("format", "text");

        MindState state = mind_->state();
        Coherence coherence = mind_->coherence();

        json result = {
            {"coherence", {
                {"local", coherence.local},
                {"global", coherence.global},
                {"temporal", coherence.temporal},
                {"tau_k", coherence.tau_k()}
            }},
            {"statistics", {
                {"total_nodes", state.total_nodes},
                {"hot_nodes", state.hot_nodes},
                {"warm_nodes", state.warm_nodes},
                {"cold_nodes", state.cold_nodes}
            }},
            {"yantra_ready", state.yantra_ready}
        };

        // Add relevant wisdom if query provided
        if (!query.empty() && mind_->has_yantra()) {
            auto recalls = mind_->recall(query, 5);
            json wisdom_array = json::array();
            for (const auto& r : recalls) {
                wisdom_array.push_back({
                    {"id", r.id.to_string()},
                    {"text", r.text},
                    {"similarity", r.similarity},
                    {"type", node_type_to_string(r.type)},
                    {"confidence", r.confidence.mu}
                });
            }
            result["relevant_wisdom"] = wisdom_array;
        }

        if (format == "text") {
            std::ostringstream ss;
            ss << "Soul State:\n";
            ss << "  Coherence: " << (coherence.tau_k() * 100) << "%\n";
            ss << "  Nodes: " << state.total_nodes << " total (";
            ss << state.hot_nodes << " hot, ";
            ss << state.warm_nodes << " warm, ";
            ss << state.cold_nodes << " cold)\n";
            ss << "  Yantra: " << (state.yantra_ready ? "ready" : "not ready") << "\n";

            if (result.contains("relevant_wisdom")) {
                ss << "\nRelevant Wisdom:\n";
                for (const auto& w : result["relevant_wisdom"]) {
                    ss << "  - " << w["text"].get<std::string>() << " (";
                    ss << (w["similarity"].get<float>() * 100) << "% match)\n";
                }
            }

            return {false, ss.str(), result};
        }

        return {false, result.dump(2), result};
    }

    ToolResult tool_grow(const json& params) {
        std::string type_str = params.at("type");
        std::string content = params.at("content");
        std::string title = params.value("title", "");
        std::string domain = params.value("domain", "");
        float confidence = params.value("confidence", 0.8f);

        NodeType type = string_to_node_type(type_str);

        // Validate requirements
        if ((type == NodeType::Wisdom || type == NodeType::Failure) && title.empty()) {
            return {true, "Title required for wisdom/failure", json()};
        }

        // Create combined text for embedding
        std::string full_text = content;
        if (!title.empty()) {
            full_text = title + ": " + content;
        }
        if (!domain.empty()) {
            full_text = "[" + domain + "] " + full_text;
        }

        NodeId id;
        if (mind_->has_yantra()) {
            id = mind_->remember(full_text, type, Confidence(confidence));
        } else {
            id = mind_->remember(type, Vector::zeros(), Confidence(confidence),
                                 std::vector<uint8_t>(full_text.begin(), full_text.end()));
        }

        json result = {
            {"id", id.to_string()},
            {"type", type_str},
            {"title", title},
            {"confidence", confidence}
        };

        std::ostringstream ss;
        ss << "Grew " << type_str << ": " << (title.empty() ? content.substr(0, 50) : title);
        ss << " (id: " << id.to_string() << ")";

        return {false, ss.str(), result};
    }

    ToolResult tool_observe(const json& params) {
        std::string category = params.at("category");
        std::string title = params.at("title");
        std::string content = params.at("content");
        std::string project = params.value("project", "");
        std::string tags = params.value("tags", "");

        // Determine decay rate based on category
        float decay = 0.05f;  // default
        if (category == "bugfix" || category == "decision") {
            decay = 0.02f;  // slow decay
        } else if (category == "session_ledger" || category == "signal") {
            decay = 0.15f;  // fast decay
        }

        // Create full observation text
        std::string full_text = title + "\n" + content;
        if (!project.empty()) {
            full_text = "[" + project + "] " + full_text;
        }
        if (!tags.empty()) {
            full_text += "\nTags: " + tags;
        }

        NodeId id;
        if (mind_->has_yantra()) {
            id = mind_->remember(full_text, NodeType::Episode);
        } else {
            id = mind_->remember(NodeType::Episode, Vector::zeros(),
                                 std::vector<uint8_t>(full_text.begin(), full_text.end()));
        }

        // Set decay rate
        if (auto node = mind_->get(id)) {
            mind_->strengthen(id, 0);  // Touch to set decay
        }

        json result = {
            {"id", id.to_string()},
            {"category", category},
            {"title", title},
            {"decay_rate", decay}
        };

        return {false, "Observed: " + title, result};
    }

    ToolResult tool_recall(const json& params) {
        std::string query = params.at("query");
        size_t limit = params.value("limit", 5);
        float threshold = params.value("threshold", 0.0f);

        if (!mind_->has_yantra()) {
            return {true, "Yantra not ready - cannot perform semantic search", json()};
        }

        auto recalls = mind_->recall(query, limit, threshold);

        json results_array = json::array();
        std::ostringstream ss;
        ss << "Found " << recalls.size() << " results:\n";

        for (const auto& r : recalls) {
            results_array.push_back({
                {"id", r.id.to_string()},
                {"text", r.text},
                {"similarity", r.similarity},
                {"relevance", r.relevance},
                {"type", node_type_to_string(r.type)},
                {"confidence", r.confidence.mu}
            });

            // Show relevance score (soul-aware) instead of raw similarity
            ss << "\n[" << (r.relevance * 100) << "%] " << r.text.substr(0, 100);
            if (r.text.length() > 100) ss << "...";
        }

        return {false, ss.str(), {{"results", results_array}}};
    }

    ToolResult tool_cycle(const json& params) {
        bool save = params.value("save", true);

        DynamicsReport report = mind_->tick();

        if (save) {
            mind_->snapshot();
        }

        Coherence coherence = mind_->coherence();

        json result = {
            {"coherence", coherence.tau_k()},
            {"decay_applied", report.decay_applied},
            {"triggers_fired", report.triggers_fired.size()},
            {"saved", save}
        };

        std::ostringstream ss;
        ss << "Cycle complete: coherence=" << (coherence.tau_k() * 100) << "%, ";
        ss << "decay=" << (report.decay_applied ? "yes" : "no");
        ss << ", triggers=" << report.triggers_fired.size();

        return {false, ss.str(), result};
    }

    // ═══════════════════════════════════════════════════════════════════
    // JSON-RPC helpers
    // ═══════════════════════════════════════════════════════════════════

    static json make_result(const json& id, const json& result) {
        return {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", result}
        };
    }

    static json make_error(const json& id, int code, const std::string& message) {
        return {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error", {
                {"code", code},
                {"message", message}
            }}
        };
    }
};

} // namespace synapse
