#pragma once
// RPC Handler: Central request handler for all RPC tools
//
// This handler can be used by both:
// - The socket server (daemon mode)
// - The RPC stdio server (thin client mode, though it forwards to daemon)

#include "protocol.hpp"
#include "types.hpp"
#include "tools/memory.hpp"
#include "tools/learning.hpp"
#include "../mind.hpp"
#include "../version.hpp"
#include <unordered_map>
#include <string>
#include <sstream>
#include <ctime>

namespace chitta::rpc {

using json = nlohmann::json;

// Helper to validate required parameters before accessing
// Returns empty string if all required params present, otherwise error message
inline std::string validate_required(const json& params, std::initializer_list<const char*> required) {
    for (const char* key : required) {
        if (!params.contains(key)) {
            return std::string("Missing required parameter: ") + key;
        }
    }
    return "";
}

// Safe parameter access with default
template<typename T>
inline T get_param(const json& params, const char* key, T default_val) {
    if (params.contains(key)) {
        try {
            return params[key].get<T>();
        } catch (...) {
            return default_val;
        }
    }
    return default_val;
}

class Handler {
public:
    explicit Handler(Mind* mind) : mind_(mind) {
        register_all_tools();
    }

    // Process a JSON-RPC request string, return response string
    std::string handle(const std::string& request_str) {
        try {
            auto request = json::parse(request_str);
            auto response = handle_request(request);
            try {
                return response.dump();
            } catch (const json::type_error& e) {
                // UTF-8 encoding issue - try with replacement
                return response.dump(-1, ' ', false, json::error_handler_t::replace);
            }
        } catch (const json::parse_error& e) {
            return make_error(json(), error::PARSE_ERROR,
                            std::string("JSON parse error: ") + e.what()).dump();
        } catch (const std::exception& e) {
            return make_error(json(), error::INTERNAL_ERROR,
                            std::string("Internal error: ") + e.what()).dump();
        }
    }

    // Get list of available tools (for tools/list)
    const std::vector<ToolSchema>& tools() const { return tools_; }

private:
    Mind* mind_;
    std::vector<ToolSchema> tools_;
    std::unordered_map<std::string, ToolHandler> handlers_;

    void register_all_tools() {
        // Memory tools (recall, resonate, etc.)
        tools::memory::register_schemas(tools_);
        tools::memory::register_handlers(mind_, handlers_);

        // Learning tools (grow, observe, feedback)
        tools::learning::register_schemas(tools_);
        tools::learning::register_handlers(mind_, handlers_);

        // Context tools (soul_context, attractors, lens, lens_harmony)
        register_context_tools();

        // Intention tools (intend, wonder, answer)
        register_intention_tools();

        // Narrative tools (narrate, ledger)
        register_narrative_tools();

        // Maintenance tools (cycle)
        register_maintenance_tools();

        // Analysis tools (epistemic state, bias detection, confidence propagation)
        register_analysis_tools();
    }

    // ═══════════════════════════════════════════════════════════════════
    // JSON-RPC dispatch
    // ═══════════════════════════════════════════════════════════════════

    json handle_request(const json& request) {
        std::string error_msg;
        if (!validate_request(request, error_msg)) {
            return make_error(request.value("id", json()), error::INVALID_REQUEST, error_msg);
        }

        auto info = parse_request(request);

        if (info.method == "initialize") {
            return handle_initialize(info.params, info.id);
        } else if (info.method == "tools/list") {
            return handle_tools_list(info.params, info.id);
        } else if (info.method == "tools/call") {
            return handle_tools_call(info.params, info.id);
        } else if (info.method == "shutdown") {
            return handle_shutdown(info.params, info.id);
        } else {
            return make_error(info.id, error::METHOD_NOT_FOUND,
                            "Unknown method: " + info.method);
        }
    }

    json handle_initialize(const json& params, const json& id) {
        (void)params;
        json capabilities = {
            {"tools", {
                {"listChanged", true}
            }}
        };
        return make_result(id, {
            {"protocolVersion", "2024-11-05"},
            {"serverInfo", {
                {"name", "chitta"},
                {"version", CHITTA_VERSION}
            }},
            {"capabilities", capabilities}
        });
    }

    json handle_tools_list(const json& params, const json& id) {
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
            return make_error(id, error::INVALID_PARAMS, "Missing tool name");
        }

        std::string name = params["name"];
        json arguments = params.value("arguments", json::object());

        auto it = handlers_.find(name);
        if (it == handlers_.end()) {
            return make_error(id, error::TOOL_NOT_FOUND, "Unknown tool: " + name);
        }

        try {
            ToolResult result = it->second(arguments);
            return make_result(id, make_tool_response(result.content, result.is_error, result.structured));
        } catch (const std::exception& e) {
            return make_error(id, error::TOOL_EXECUTION_ERROR,
                            std::string("Tool execution failed: ") + e.what());
        }
    }

    json handle_shutdown(const json& params, const json& id) {
        mind_->snapshot();
        return make_result(id, {{"status", "ok"}});
    }

    // ═══════════════════════════════════════════════════════════════════
    // Context tools (inline for simplicity)
    // ═══════════════════════════════════════════════════════════════════

    void register_context_tools() {
        tools_.push_back({
            "soul_context",
            "Get soul context including coherence, ojas, statistics, and session state.",
            {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Query to find relevant wisdom"}}},
                    {"format", {{"type", "string"}, {"enum", {"text", "json"}}, {"default", "text"}}},
                    {"include_ledger", {{"type", "boolean"}, {"default", true}}}
                }},
                {"required", json::array()}
            }
        });
        handlers_["soul_context"] = [this](const json& p) { return tool_soul_context(p); };

        tools_.push_back({
            "attractors",
            "Find natural attractors in the soul graph. Attractors are high-confidence, "
            "well-connected nodes that act as conceptual gravity wells.",
            {
                {"type", "object"},
                {"properties", {
                    {"max_attractors", {{"type", "integer"}, {"minimum", 1}, {"maximum", 20}, {"default", 10}}},
                    {"settle", {{"type", "boolean"}, {"default", false},
                               {"description", "Also run settling dynamics"}}},
                    {"settle_strength", {{"type", "number"}, {"minimum", 0.01}, {"maximum", 0.1}, {"default", 0.02}}}
                }},
                {"required", json::array()}
            }
        });
        handlers_["attractors"] = [this](const json& p) { return tool_attractors(p); };

        tools_.push_back({
            "lens",
            "Search through a cognitive perspective (manas, buddhi, ahamkara, chitta, vikalpa, sakshi).",
            {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "What to search for"}}},
                    {"lens", {{"type", "string"},
                             {"enum", {"manas", "buddhi", "ahamkara", "chitta", "vikalpa", "sakshi", "all"}},
                             {"default", "all"}}},
                    {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 20}, {"default", 5}}}
                }},
                {"required", {"query"}}
            }
        });
        handlers_["lens"] = [this](const json& p) { return tool_lens(p); };

        tools_.push_back({
            "lens_harmony",
            "Check if different cognitive lenses agree on the same query.",
            {
                {"type", "object"},
                {"properties", {}},
                {"required", json::array()}
            }
        });
        handlers_["lens_harmony"] = [this](const json& p) { return tool_lens_harmony(p); };
    }

    ToolResult tool_soul_context(const json& params) {
        std::string query = params.value("query", "");
        std::string format = params.value("format", "text");
        bool include_ledger = params.value("include_ledger", true);

        MindState state = mind_->state();
        Coherence coherence = mind_->coherence();
        MindHealth health = mind_->health();

        json result = {
            {"samarasya", {
                {"local", coherence.local},
                {"global", coherence.global},
                {"temporal", coherence.temporal},
                {"structural", coherence.structural},
                {"tau", coherence.tau_k()}
            }},
            {"ojas", {
                {"structural", health.structural},
                {"semantic", health.semantic},
                {"temporal", health.temporal},
                {"capacity", health.capacity},
                {"vitality", health.ojas()},
                {"psi", health.psi()},
                {"status", health.status_string()}
            }},
            {"statistics", {
                {"total_nodes", state.total_nodes},
                {"hot_nodes", state.hot_nodes},
                {"warm_nodes", state.warm_nodes},
                {"cold_nodes", state.cold_nodes}
            }},
            {"yantra_ready", state.yantra_ready}
        };

        // Session context
        const auto& session = mind_->session_context();
        result["session_context"] = {
            {"recent_observations", session.recent_observations.size()},
            {"active_intentions", session.active_intentions.size()},
            {"goal_basin", session.goal_basin.size()},
            {"priming_active", !session.empty()}
        };

        // Competition config
        const auto& competition = mind_->competition_config();
        result["competition"] = {
            {"enabled", competition.enabled},
            {"similarity_threshold", competition.similarity_threshold},
            {"inhibition_strength", competition.inhibition_strength},
            {"hard_suppression", competition.hard_suppression}
        };

        // Ledger
        if (include_ledger) {
            auto ledger = mind_->load_ledger();
            if (ledger) {
                try {
                    result["ledger"] = {
                        {"id", ledger->first.to_string()},
                        {"content", json::parse(ledger->second)}
                    };
                } catch (...) {
                    result["ledger"] = {
                        {"id", ledger->first.to_string()},
                        {"content", {{"raw", ledger->second}}}
                    };
                }
            }
        }

        // Relevant wisdom
        if (!query.empty() && mind_->has_yantra()) {
            auto recalls = mind_->recall(query, 5);
            json wisdom_array = json::array();
            for (const auto& r : recalls) {
                json item;
                item["id"] = r.id.to_string();
                item["text"] = r.text;
                item["similarity"] = r.similarity;
                item["type"] = node_type_to_string(r.type);
                item["confidence"] = r.confidence.mu;
                wisdom_array.push_back(item);
            }
            result["relevant_wisdom"] = wisdom_array;
        }

        if (format == "text") {
            std::ostringstream ss;
            ss << "Soul State:\n";
            ss << "  Sāmarasya (τ): " << int(coherence.tau_k() * 100) << "%\n";
            ss << "  Ojas (ψ): " << int(health.psi() * 100) << "% [" << health.status_string() << "]\n";
            ss << "  Nodes: " << state.total_nodes << " total\n";
            return ToolResult::ok(ss.str(), result);
        }

        return ToolResult::ok(result.dump(2), result);
    }

    ToolResult tool_attractors(const json& params) {
        size_t max_attractors = params.value("max_attractors", 10);
        bool settle = params.value("settle", false);
        float settle_strength = params.value("settle_strength", 0.02f);

        auto attractors = mind_->find_attractors(max_attractors);

        if (attractors.empty()) {
            return ToolResult::ok("No attractors found (need nodes with high confidence, connections, and age)", json());
        }

        Mind::AttractorReport report;
        if (settle) {
            report = mind_->run_attractor_dynamics(max_attractors, settle_strength);
        }

        json attractors_array = json::array();
        std::ostringstream ss;
        ss << "Found " << attractors.size() << " attractors:\n";

        for (const auto& a : attractors) {
            attractors_array.push_back({
                {"id", a.id.to_string()},
                {"strength", a.strength},
                {"label", a.label},
                {"basin_size", a.basin_size}
            });
            ss << "\n  [" << int(a.strength * 100) << "%] " << a.label;
            ss << " (basin: " << a.basin_size << ")";
        }

        json result = {
            {"attractors", attractors_array},
            {"count", attractors.size()}
        };

        if (settle) {
            result["nodes_settled"] = report.nodes_settled;
            ss << "\n\nSettled " << report.nodes_settled << " nodes toward attractors";
        }

        return ToolResult::ok(ss.str(), result);
    }

    ToolResult tool_lens(const json& params) {
        auto err = validate_required(params, {"query"});
        if (!err.empty()) return ToolResult::error(err);

        std::string query = params["query"];
        std::string lens = params.value("lens", "all");
        size_t limit = params.value("limit", 5);

        if (!mind_->has_yantra()) {
            return ToolResult::error("Yantra not ready");
        }

        json result = json::object();
        std::ostringstream ss;
        ss << "Lens search for: " << query << "\n";

        // Simplified lens: do semantic search and categorize by type
        auto recalls = mind_->recall(query, limit * 5);  // Get more to filter

        std::map<std::string, std::vector<const Recall*>> by_type;
        for (const auto& r : recalls) {
            std::string type_name = node_type_to_string(r.type);
            if (type_name == "episode") by_type["manas"].push_back(&r);
            else if (type_name == "wisdom") by_type["buddhi"].push_back(&r);
            else if (type_name == "belief") by_type["ahamkara"].push_back(&r);
            else if (type_name == "dream") by_type["vikalpa"].push_back(&r);
            else if (type_name == "failure") by_type["sakshi"].push_back(&r);
            else by_type["chitta"].push_back(&r);  // Default bucket
        }

        auto fill_result = [&](const std::string& name) {
            json arr = json::array();
            auto it = by_type.find(name);
            if (it != by_type.end()) {
                size_t count = 0;
                for (const auto* rp : it->second) {
                    if (count++ >= limit) break;
                    json item;
                    item["id"] = rp->id.to_string();
                    item["text"] = rp->text;
                    item["score"] = rp->relevance;
                    item["type"] = node_type_to_string(rp->type);
                    arr.push_back(item);
                }
            }
            result[name] = arr;
            ss << "\n" << name << ": " << arr.size() << " results";
        };

        if (lens == "all" || lens == "manas") fill_result("manas");
        if (lens == "all" || lens == "buddhi") fill_result("buddhi");
        if (lens == "all" || lens == "ahamkara") fill_result("ahamkara");
        if (lens == "all" || lens == "chitta") fill_result("chitta");
        if (lens == "all" || lens == "vikalpa") fill_result("vikalpa");
        if (lens == "all" || lens == "sakshi") fill_result("sakshi");

        return ToolResult::ok(ss.str(), result);
    }

    ToolResult tool_lens_harmony(const json& params) {
        Coherence coherence = mind_->coherence();

        json result = {
            {"mean_coherence", coherence.tau_k()},
            {"variance", 0.0f},  // TODO: compute actual variance
            {"voices_agree", coherence.tau_k() > 0.7f},
            {"perspectives", json::array({
                {{"voice", "manas"}, {"coherence", coherence.local}},
                {{"voice", "buddhi"}, {"coherence", coherence.global}},
                {{"voice", "chitta"}, {"coherence", coherence.temporal}},
                {{"voice", "ahamkara"}, {"coherence", coherence.structural}}
            })}
        };

        return ToolResult::ok("Lens harmony check", result);
    }

    // ═══════════════════════════════════════════════════════════════════
    // Intention tools
    // ═══════════════════════════════════════════════════════════════════

    void register_intention_tools() {
        tools_.push_back({
            "intend",
            "Set an active intention. Intentions prime memory retrieval.",
            {
                {"type", "object"},
                {"properties", {
                    {"want", {{"type", "string"}, {"description", "What you want to achieve"}}},
                    {"because", {{"type", "string"}, {"description", "Why this matters"}}}
                }},
                {"required", {"want"}}
            }
        });
        handlers_["intend"] = [this](const json& p) { return tool_intend(p); };

        tools_.push_back({
            "wonder",
            "Register a question or knowledge gap. Creates a gap node.",
            {
                {"type", "object"},
                {"properties", {
                    {"question", {{"type", "string"}, {"description", "The question"}}},
                    {"context", {{"type", "string"}, {"description", "Why this matters"}}}
                }},
                {"required", {"question"}}
            }
        });
        handlers_["wonder"] = [this](const json& p) { return tool_wonder(p); };

        tools_.push_back({
            "answer",
            "Resolve a knowledge gap with an answer.",
            {
                {"type", "object"},
                {"properties", {
                    {"question_id", {{"type", "string"}, {"description", "ID of the gap node"}}},
                    {"resolution", {{"type", "string"}, {"description", "The answer"}}}
                }},
                {"required", {"question_id", "resolution"}}
            }
        });
        handlers_["answer"] = [this](const json& p) { return tool_answer(p); };
    }

    ToolResult tool_intend(const json& params) {
        auto err = validate_required(params, {"want"});
        if (!err.empty()) return ToolResult::error(err);

        std::string want = params["want"];
        std::string because = params.value("because", "");

        std::string text = "INTENTION: " + want;
        if (!because.empty()) {
            text += "\nBecause: " + because;
        }

        NodeId id;
        if (mind_->has_yantra()) {
            id = mind_->remember(text, NodeType::Intention);
        } else {
            id = mind_->remember(NodeType::Intention, Vector::zeros(),
                                std::vector<uint8_t>(text.begin(), text.end()));
        }

        // Note: Session context priming happens automatically via recall_primed
        return ToolResult::ok("Intention set: " + want, {{"id", id.to_string()}});
    }

    ToolResult tool_wonder(const json& params) {
        auto err = validate_required(params, {"question"});
        if (!err.empty()) return ToolResult::error(err);

        std::string question = params["question"];
        std::string context = params.value("context", "");

        std::string text = "QUESTION: " + question;
        if (!context.empty()) {
            text += "\nContext: " + context;
        }

        NodeId id;
        if (mind_->has_yantra()) {
            id = mind_->remember(text, NodeType::Gap);
        } else {
            id = mind_->remember(NodeType::Gap, Vector::zeros(),
                                std::vector<uint8_t>(text.begin(), text.end()));
        }

        return ToolResult::ok("Wondering: " + question, {{"id", id.to_string()}});
    }

    ToolResult tool_answer(const json& params) {
        auto err = validate_required(params, {"question_id", "resolution"});
        if (!err.empty()) return ToolResult::error(err);

        std::string question_id_str = params["question_id"];
        std::string resolution = params["resolution"];

        NodeId question_id = NodeId::from_string(question_id_str);
        auto question = mind_->get(question_id);

        if (!question) {
            return ToolResult::error("Question not found: " + question_id_str);
        }

        // Create answer as wisdom linked to question
        std::string text = "ANSWER: " + resolution;
        NodeId answer_id;
        if (mind_->has_yantra()) {
            answer_id = mind_->remember(text, NodeType::Wisdom);
        } else {
            answer_id = mind_->remember(NodeType::Wisdom, Vector::zeros(),
                                       std::vector<uint8_t>(text.begin(), text.end()));
        }

        // Link question to answer via Hebbian strengthening
        mind_->hebbian_strengthen(question_id, answer_id, 0.5f);

        return ToolResult::ok("Answered: " + resolution, {
            {"question_id", question_id_str},
            {"answer_id", answer_id.to_string()}
        });
    }

    // ═══════════════════════════════════════════════════════════════════
    // Narrative tools
    // ═══════════════════════════════════════════════════════════════════

    void register_narrative_tools() {
        tools_.push_back({
            "narrate",
            "Start or end a narrative thread for tracking complex workflows.",
            {
                {"type", "object"},
                {"properties", {
                    {"action", {{"type", "string"}, {"enum", {"start", "end"}}}},
                    {"title", {{"type", "string"}, {"description", "Thread title (for start)"}}},
                    {"episode_id", {{"type", "string"}, {"description", "Thread ID (for end)"}}},
                    {"content", {{"type", "string"}, {"description", "Summary (for end)"}}},
                    {"emotion", {{"type", "string"}, {"description", "Emotional tone (for end)"}}}
                }},
                {"required", {"action"}}
            }
        });
        handlers_["narrate"] = [this](const json& p) { return tool_narrate(p); };

        tools_.push_back({
            "ledger",
            "Save, load, or list session state as natural language (high-ε).",
            {
                {"type", "object"},
                {"properties", {
                    {"action", {{"type", "string"}, {"enum", {"save", "load", "list"}}}},
                    {"content", {{"type", "string"}, {"description", "Session summary in natural language (e.g., 'Working on X → Next: Y')"}}},
                    {"project", {{"type", "string"}, {"description", "Project name for filtering"}}},
                    {"id", {{"type", "string"}, {"description", "Ledger ID for loading specific snapshot"}}},
                    {"limit", {{"type", "integer"}, {"default", 10}, {"description", "Max ledgers to list"}}}
                }},
                {"required", {"action"}}
            }
        });
        handlers_["ledger"] = [this](const json& p) { return tool_ledger(p); };
    }

    ToolResult tool_narrate(const json& params) {
        auto err = validate_required(params, {"action"});
        if (!err.empty()) return ToolResult::error(err);

        std::string action = params["action"];

        if (action == "start") {
            std::string title = params.value("title", "untitled thread");
            std::string text = "THREAD_START: " + title;

            NodeId id;
            if (mind_->has_yantra()) {
                id = mind_->remember(text, NodeType::StoryThread);
            } else {
                id = mind_->remember(NodeType::StoryThread, Vector::zeros(),
                                    std::vector<uint8_t>(text.begin(), text.end()));
            }

            return ToolResult::ok("Thread started: " + title, {
                {"thread_id", id.to_string()},
                {"title", title}
            });

        } else if (action == "end") {
            if (!params.contains("episode_id")) {
                return ToolResult::error("Missing required parameter: episode_id");
            }
            std::string episode_id = params["episode_id"];
            std::string content = params.value("content", "");
            std::string emotion = params.value("emotion", "neutral");

            NodeId thread_id = NodeId::from_string(episode_id);
            auto thread = mind_->get(thread_id);

            if (!thread) {
                return ToolResult::error("Thread not found: " + episode_id);
            }

            // Update thread with summary
            std::string summary = "THREAD_END: " + content + " [" + emotion + "]";
            if (mind_->has_yantra()) {
                NodeId summary_id = mind_->remember(summary, NodeType::Episode);
                mind_->hebbian_strengthen(thread_id, summary_id, 0.5f);
            }

            return ToolResult::ok("Thread ended", {
                {"thread_id", episode_id},
                {"emotion", emotion}
            });
        }

        return ToolResult::error("Unknown action: " + action);
    }

    static std::string format_timestamp(Timestamp ts) {
        time_t time = static_cast<time_t>(ts / 1000);  // Convert from millis to seconds
        struct tm tm_info;
        localtime_r(&time, &tm_info);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm_info);
        return buf;
    }

    ToolResult tool_ledger(const json& params) {
        auto err = validate_required(params, {"action"});
        if (!err.empty()) return ToolResult::error(err);

        std::string action = params["action"];
        std::string project = params.value("project", "");

        if (action == "save") {
            std::string content = params.value("content", "");
            if (content.empty()) {
                return ToolResult::error("Ledger content required (natural language summary)");
            }
            NodeId id = mind_->save_ledger(content, "", project);
            json result;
            result["status"] = "saved";
            result["id"] = id.to_string();
            if (!project.empty()) result["project"] = project;
            return ToolResult::ok("Ledger saved: " + content.substr(0, 50), result);

        } else if (action == "load") {
            std::string ledger_id = params.value("id", "");

            // Load by ID if specified, otherwise load most recent
            if (!ledger_id.empty()) {
                NodeId id = NodeId::from_string(ledger_id);
                auto node_opt = mind_->get(id);
                if (!node_opt) {
                    return ToolResult::error("Ledger not found: " + ledger_id);
                }
                auto text_opt = mind_->text(id);
                if (!text_opt) {
                    return ToolResult::error("Ledger has no content: " + ledger_id);
                }
                std::string date = format_timestamp(node_opt->tau_created);
                json result;
                result["id"] = ledger_id;
                result["date"] = date;
                result["content"] = *text_opt;
                return ToolResult::ok(*text_opt, result);
            }

            auto ledger = mind_->load_ledger("", project);
            if (ledger) {
                json result;
                result["id"] = ledger->first.to_string();
                result["content"] = ledger->second;
                return ToolResult::ok(ledger->second, result);
            }
            return ToolResult::ok("No ledger found", {{"status", "empty"}});

        } else if (action == "list") {
            size_t limit = params.value("limit", 10);
            auto ledgers = mind_->list_ledgers(limit, project);

            if (ledgers.empty()) {
                return ToolResult::ok("No ledgers found", {{"ledgers", json::array()}});
            }

            json ledgers_array = json::array();
            std::ostringstream ss;
            ss << "Found " << ledgers.size() << " ledger(s)";
            if (!project.empty()) ss << " for project: " << project;
            ss << "\n";

            for (const auto& [id, ts] : ledgers) {
                std::string date = format_timestamp(ts);
                ledgers_array.push_back({
                    {"id", id.to_string()},
                    {"timestamp", ts},
                    {"date", date}
                });
                ss << "  " << date << "  " << id.to_string().substr(0, 8) << "...\n";
            }

            return ToolResult::ok(ss.str(), {{"ledgers", ledgers_array}});
        }

        return ToolResult::error("Unknown action: " + action);
    }

    // ═══════════════════════════════════════════════════════════════════
    // Maintenance tools
    // ═══════════════════════════════════════════════════════════════════

    void register_maintenance_tools() {
        tools_.push_back({
            "version_check",
            "Check daemon version and protocol compatibility.",
            {
                {"type", "object"},
                {"properties", json::object()},
                {"required", json::array()}
            }
        });
        handlers_["version_check"] = [this](const json& p) { return tool_version_check(p); };

        tools_.push_back({
            "cycle",
            "Run a maintenance cycle: decay, feedback, synthesis, attractors, and optionally regenerate embeddings for nodes with zero vectors.",
            {
                {"type", "object"},
                {"properties", {
                    {"save", {{"type", "boolean"}, {"default", true}}},
                    {"attractors", {{"type", "boolean"}, {"default", false},
                                   {"description", "Run attractor dynamics"}}},
                    {"regenerate_embeddings", {{"type", "boolean"}, {"default", false},
                                               {"description", "Regenerate embeddings for nodes with zero vectors"}}},
                    {"batch_size", {{"type", "integer"}, {"default", 100},
                                   {"description", "Max nodes to regenerate per call"}}}
                }},
                {"required", json::array()}
            }
        });
        handlers_["cycle"] = [this](const json& p) { return tool_cycle(p); };
    }

    ToolResult tool_version_check(const json& params) {
        (void)params;
        json result = {
            {"software_version", CHITTA_VERSION},
            {"protocol_major", CHITTA_PROTOCOL_VERSION_MAJOR},
            {"protocol_minor", CHITTA_PROTOCOL_VERSION_MINOR}
        };
        std::ostringstream ss;
        ss << "Chitta v" << CHITTA_VERSION
           << " (protocol " << CHITTA_PROTOCOL_VERSION_MAJOR
           << "." << CHITTA_PROTOCOL_VERSION_MINOR << ")";
        return ToolResult::ok(ss.str(), result);
    }

    ToolResult tool_cycle(const json& params) {
        bool save = params.value("save", true);
        bool run_attractors = params.value("attractors", false);
        bool regen_embeddings = params.value("regenerate_embeddings", false);
        size_t batch_size = params.value("batch_size", 100);

        DynamicsReport report = mind_->tick();
        size_t feedback_applied = mind_->apply_feedback();
        size_t synthesized = mind_->synthesize_wisdom();

        Mind::AttractorReport attractor_report;
        if (run_attractors) {
            attractor_report = mind_->run_attractor_dynamics();
        }

        // Regenerate embeddings for nodes with zero vectors
        size_t embeddings_regenerated = 0;
        size_t zero_vectors_remaining = 0;
        if (regen_embeddings) {
            embeddings_regenerated = mind_->regenerate_embeddings(batch_size);
            zero_vectors_remaining = mind_->count_zero_vectors();
        }

        if (save) {
            mind_->snapshot();
        }

        Coherence coherence = mind_->coherence();

        json result = {
            {"coherence", coherence.tau_k()},
            {"decay_applied", report.decay_applied},
            {"triggers_fired", report.triggers_fired.size()},
            {"feedback_applied", feedback_applied},
            {"wisdom_synthesized", synthesized},
            {"saved", save}
        };

        if (run_attractors) {
            result["attractors_found"] = attractor_report.attractor_count;
            result["nodes_settled"] = attractor_report.nodes_settled;
        }

        if (regen_embeddings) {
            result["embeddings_regenerated"] = embeddings_regenerated;
            result["zero_vectors_remaining"] = zero_vectors_remaining;
        }

        std::ostringstream ss;
        ss << "Cycle complete. Coherence: " << int(coherence.tau_k() * 100) << "%";
        if (synthesized > 0) ss << ", synthesized: " << synthesized;
        if (feedback_applied > 0) ss << ", feedback: " << feedback_applied;
        if (embeddings_regenerated > 0) {
            ss << ", embeddings regenerated: " << embeddings_regenerated;
            if (zero_vectors_remaining > 0) {
                ss << " (" << zero_vectors_remaining << " remaining)";
            }
        }

        return ToolResult::ok(ss.str(), result);
    }

    // ═══════════════════════════════════════════════════════════════════
    // Analysis tools
    // ═══════════════════════════════════════════════════════════════════

    void register_analysis_tools() {
        tools_.push_back({
            "propagate",
            "Propagate confidence change through graph. When a node proves useful/wrong, "
            "connected nodes are affected proportionally. Use after feedback to spread impact.",
            {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Node ID to propagate from"}}},
                    {"delta", {{"type", "number"}, {"minimum", -0.5}, {"maximum", 0.5},
                              {"description", "Confidence change (+/- boost/penalty)"}}},
                    {"decay_factor", {{"type", "number"}, {"minimum", 0.1}, {"maximum", 0.9}, {"default", 0.5},
                                     {"description", "How much propagation decays per hop"}}},
                    {"max_depth", {{"type", "integer"}, {"minimum", 1}, {"maximum", 5}, {"default", 3}}}
                }},
                {"required", {"id", "delta"}}
            }
        });
        handlers_["propagate"] = [this](const json& p) { return tool_propagate(p); };

        tools_.push_back({
            "forget",
            "Deliberately forget a node with cascade effects. Connected nodes weaken, "
            "edges rewire around the forgotten node. Audit trail preserved.",
            {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Node ID to forget"}}},
                    {"cascade", {{"type", "boolean"}, {"default", true},
                                {"description", "Weaken connected nodes"}}},
                    {"rewire", {{"type", "boolean"}, {"default", true},
                               {"description", "Reconnect edges around forgotten node"}}},
                    {"cascade_strength", {{"type", "number"}, {"minimum", 0.05}, {"maximum", 0.3}, {"default", 0.1}}}
                }},
                {"required", {"id"}}
            }
        });
        handlers_["forget"] = [this](const json& p) { return tool_forget(p); };

        tools_.push_back({
            "epistemic_state",
            "Analyze what I know vs uncertain about. Shows knowledge gaps, "
            "unanswered questions, low-confidence beliefs, and coverage by domain.",
            {
                {"type", "object"},
                {"properties", {
                    {"domain", {{"type", "string"}, {"description", "Filter by domain (optional)"}}},
                    {"min_confidence", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"default", 0.3},
                                       {"description", "Threshold for 'certain' knowledge"}}},
                    {"limit", {{"type", "integer"}, {"minimum", 5}, {"maximum", 50}, {"default", 20}}}
                }},
                {"required", json::array()}
            }
        });
        handlers_["epistemic_state"] = [this](const json& p) { return tool_epistemic_state(p); };

        tools_.push_back({
            "bias_scan",
            "Detect patterns in my own beliefs and decisions. Looks for over-representation "
            "of topics, confidence inflation, and decision clustering.",
            {
                {"type", "object"},
                {"properties", {
                    {"sample_size", {{"type", "integer"}, {"minimum", 50}, {"maximum", 500}, {"default", 100}}}
                }},
                {"required", json::array()}
            }
        });
        handlers_["bias_scan"] = [this](const json& p) { return tool_bias_scan(p); };
    }

    ToolResult tool_propagate(const json& params) {
        auto err = validate_required(params, {"id", "delta"});
        if (!err.empty()) return ToolResult::error(err);

        std::string id_str = params["id"];
        float delta = params["delta"];
        float decay_factor = params.value("decay_factor", 0.5f);
        size_t max_depth = params.value("max_depth", 3);

        NodeId id = NodeId::from_string(id_str);
        if (!mind_->get(id)) {
            return ToolResult::error("Node not found: " + id_str);
        }

        auto result = mind_->propagate_confidence(id, delta, decay_factor, max_depth);

        json changes_array = json::array();
        for (const auto& [change_id, change_delta] : result.changes) {
            changes_array.push_back({
                {"id", change_id.to_string()},
                {"delta", change_delta}
            });
        }

        std::ostringstream ss;
        ss << "Propagated " << (delta >= 0 ? "+" : "") << delta
           << " to " << result.nodes_affected << " nodes"
           << " (total impact: " << result.total_delta_applied << ")";

        return ToolResult::ok(ss.str(), {
            {"source_id", id_str},
            {"delta", delta},
            {"nodes_affected", result.nodes_affected},
            {"total_impact", result.total_delta_applied},
            {"changes", changes_array}
        });
    }

    ToolResult tool_forget(const json& params) {
        auto err = validate_required(params, {"id"});
        if (!err.empty()) return ToolResult::error(err);

        std::string id_str = params["id"];
        bool cascade = params.value("cascade", true);
        bool rewire = params.value("rewire", true);
        float cascade_strength = params.value("cascade_strength", 0.1f);

        NodeId id = NodeId::from_string(id_str);
        auto node_opt = mind_->get(id);
        if (!node_opt) {
            return ToolResult::error("Node not found: " + id_str);
        }

        // Save audit trail
        std::string forgotten_text = mind_->text(id).value_or("");
        std::string audit = "FORGOTTEN: " + forgotten_text.substr(0, 100);

        // Collect edges before removal
        std::vector<NodeId> inbound, outbound;
        for (const auto& edge : node_opt->edges) {
            outbound.push_back(edge.target);
        }
        // Check reverse edges (nodes pointing to this one)
        mind_->for_each_node([&](const NodeId& other_id, const Node& other) {
            for (const auto& edge : other.edges) {
                if (edge.target == id) {
                    inbound.push_back(other_id);
                    break;
                }
            }
        });

        size_t affected = 0;
        // Cascade: weaken connected nodes
        if (cascade) {
            for (const auto& out_id : outbound) {
                mind_->weaken(out_id, cascade_strength);
                affected++;
            }
            for (const auto& in_id : inbound) {
                mind_->weaken(in_id, cascade_strength);
                affected++;
            }
        }

        // Rewire: connect inbound to outbound (skip the forgotten node)
        size_t rewired = 0;
        if (rewire && !inbound.empty() && !outbound.empty()) {
            for (const auto& in_id : inbound) {
                for (const auto& out_id : outbound) {
                    if (in_id != out_id) {
                        mind_->hebbian_strengthen(in_id, out_id, 0.1f);
                        rewired++;
                    }
                }
            }
        }

        // Remove the node
        mind_->remove_node(id);

        // Store audit trail
        if (mind_->has_yantra()) {
            mind_->remember(audit, NodeType::Episode, {"audit:forget"});
        }

        std::ostringstream ss;
        ss << "Forgotten: " << forgotten_text.substr(0, 50);
        if (cascade) ss << " (affected " << affected << " connected)";
        if (rewire) ss << " (rewired " << rewired << " paths)";

        return ToolResult::ok(ss.str(), {
            {"id", id_str},
            {"forgotten_preview", forgotten_text.substr(0, 100)},
            {"nodes_weakened", affected},
            {"edges_rewired", rewired}
        });
    }

    ToolResult tool_epistemic_state(const json& params) {
        std::string domain = params.value("domain", "");
        float min_confidence = params.value("min_confidence", 0.3f);
        size_t limit = params.value("limit", 20);

        // Collect epistemic data
        size_t total_nodes = 0;
        size_t gaps = 0, questions = 0, low_confidence = 0, high_confidence = 0;
        std::unordered_map<std::string, size_t> type_counts;
        std::vector<std::pair<NodeId, float>> lowest_confidence;

        mind_->for_each_node([&](const NodeId& id, const Node& node) {
            total_nodes++;
            float conf = node.kappa.effective();

            std::string type_name = node_type_to_string(node.node_type);
            type_counts[type_name]++;

            if (node.node_type == NodeType::Gap) gaps++;
            if (node.node_type == NodeType::Question) questions++;

            if (conf < min_confidence) {
                low_confidence++;
                if (lowest_confidence.size() < limit) {
                    lowest_confidence.push_back({id, conf});
                }
            } else {
                high_confidence++;
            }
        });

        // Sort lowest confidence
        std::sort(lowest_confidence.begin(), lowest_confidence.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });

        json uncertain_array = json::array();
        for (const auto& [id, conf] : lowest_confidence) {
            std::string text = mind_->text(id).value_or("");
            auto node = mind_->get(id);
            uncertain_array.push_back({
                {"id", id.to_string()},
                {"confidence", conf},
                {"type", node ? node_type_to_string(node->node_type) : "unknown"},
                {"preview", text.substr(0, 60)}
            });
        }

        json type_dist = json::object();
        for (const auto& [type, count] : type_counts) {
            type_dist[type] = count;
        }

        float certainty_ratio = total_nodes > 0 ?
            static_cast<float>(high_confidence) / total_nodes : 0.0f;

        std::ostringstream ss;
        ss << "Epistemic State:\n";
        ss << "  Total knowledge: " << total_nodes << " nodes\n";
        ss << "  High confidence (≥" << int(min_confidence * 100) << "%): "
           << high_confidence << " (" << int(certainty_ratio * 100) << "%)\n";
        ss << "  Low confidence: " << low_confidence << "\n";
        ss << "  Open questions: " << questions << "\n";
        ss << "  Knowledge gaps: " << gaps << "\n";

        return ToolResult::ok(ss.str(), {
            {"total_nodes", total_nodes},
            {"high_confidence", high_confidence},
            {"low_confidence", low_confidence},
            {"questions", questions},
            {"gaps", gaps},
            {"certainty_ratio", certainty_ratio},
            {"type_distribution", type_dist},
            {"most_uncertain", uncertain_array}
        });
    }

    ToolResult tool_bias_scan(const json& params) {
        size_t sample_size = params.value("sample_size", 100);

        // Collect samples for analysis
        std::vector<const Node*> samples;
        std::unordered_map<std::string, size_t> type_counts;
        std::unordered_map<std::string, std::vector<float>> confidence_by_type;
        size_t total_edges = 0;
        float total_confidence = 0.0f;

        mind_->for_each_node([&](const NodeId& id, const Node& node) {
            (void)id;
            if (samples.size() < sample_size) {
                std::string type = node_type_to_string(node.node_type);
                type_counts[type]++;
                confidence_by_type[type].push_back(node.kappa.effective());
                total_edges += node.edges.size();
                total_confidence += node.kappa.effective();
                samples.push_back(&node);
            }
        });

        if (samples.empty()) {
            return ToolResult::ok("No data for bias analysis", {{"biases", json::array()}});
        }

        // Analyze biases
        json biases = json::array();
        float avg_confidence = total_confidence / samples.size();
        float avg_edges = static_cast<float>(total_edges) / samples.size();

        // 1. Type imbalance
        size_t max_type_count = 0;
        std::string dominant_type;
        for (const auto& [type, count] : type_counts) {
            if (count > max_type_count) {
                max_type_count = count;
                dominant_type = type;
            }
        }
        float dominance_ratio = static_cast<float>(max_type_count) / samples.size();
        if (dominance_ratio > 0.5f) {
            biases.push_back({
                {"type", "type_dominance"},
                {"description", "Over-representation of " + dominant_type + " nodes"},
                {"severity", dominance_ratio},
                {"dominant_type", dominant_type},
                {"percentage", int(dominance_ratio * 100)}
            });
        }

        // 2. Confidence inflation/deflation
        if (avg_confidence > 0.85f) {
            biases.push_back({
                {"type", "confidence_inflation"},
                {"description", "Average confidence unusually high - may be overconfident"},
                {"severity", avg_confidence},
                {"average_confidence", avg_confidence}
            });
        } else if (avg_confidence < 0.4f) {
            biases.push_back({
                {"type", "confidence_deflation"},
                {"description", "Average confidence low - may be under-trusting knowledge"},
                {"severity", 1.0f - avg_confidence},
                {"average_confidence", avg_confidence}
            });
        }

        // 3. Connectivity bias
        if (avg_edges < 1.0f) {
            biases.push_back({
                {"type", "isolation"},
                {"description", "Nodes poorly connected - knowledge fragmented"},
                {"severity", 1.0f - avg_edges},
                {"average_edges", avg_edges}
            });
        } else if (avg_edges > 10.0f) {
            biases.push_back({
                {"type", "over_connection"},
                {"description", "Nodes heavily interconnected - may lack discrimination"},
                {"severity", avg_edges / 20.0f},
                {"average_edges", avg_edges}
            });
        }

        // 4. Type confidence variance
        for (const auto& [type, confs] : confidence_by_type) {
            if (confs.size() < 5) continue;
            float type_avg = 0.0f;
            for (float c : confs) type_avg += c;
            type_avg /= confs.size();

            if (std::abs(type_avg - avg_confidence) > 0.2f) {
                biases.push_back({
                    {"type", "type_confidence_bias"},
                    {"description", type + " has " + (type_avg > avg_confidence ? "higher" : "lower") +
                                   " confidence than average"},
                    {"node_type", type},
                    {"type_average", type_avg},
                    {"overall_average", avg_confidence}
                });
            }
        }

        std::ostringstream ss;
        ss << "Bias Scan (" << samples.size() << " samples):\n";
        if (biases.empty()) {
            ss << "  No significant biases detected\n";
        } else {
            ss << "  Found " << biases.size() << " potential bias(es)\n";
            for (const auto& b : biases) {
                ss << "  - " << b["description"].get<std::string>() << "\n";
            }
        }

        json type_dist = json::object();
        for (const auto& [type, count] : type_counts) {
            type_dist[type] = count;
        }

        return ToolResult::ok(ss.str(), {
            {"biases", biases},
            {"sample_size", samples.size()},
            {"average_confidence", avg_confidence},
            {"average_edges", avg_edges},
            {"type_distribution", type_dist}
        });
    }
};

} // namespace chitta::rpc
