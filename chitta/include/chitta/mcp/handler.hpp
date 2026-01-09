#pragma once
// MCP Handler: Central request handler for all MCP tools
//
// This handler can be used by both:
// - The socket server (daemon mode)
// - The MCP stdio server (thin client mode, though it forwards to daemon)

#include "protocol.hpp"
#include "types.hpp"
#include "tools/memory.hpp"
#include "tools/learning.hpp"
#include "../mind.hpp"
#include <unordered_map>
#include <string>
#include <sstream>

namespace chitta::mcp {

using json = nlohmann::json;

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
            return response.dump();
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
        json capabilities = {
            {"tools", {}}
        };
        return make_result(id, {
            {"protocolVersion", "2024-11-05"},
            {"serverInfo", {
                {"name", "chitta"},
                {"version", "2.27.0"}
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
        std::string query = params.at("query");
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
        std::string want = params.at("want");
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
        std::string question = params.at("question");
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
        std::string question_id_str = params.at("question_id");
        std::string resolution = params.at("resolution");

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
            "Save or load session state (Atman snapshot).",
            {
                {"type", "object"},
                {"properties", {
                    {"action", {{"type", "string"}, {"enum", {"save", "load"}}}},
                    {"content", {{"type", "object"}, {"description", "State to save"}}}
                }},
                {"required", {"action"}}
            }
        });
        handlers_["ledger"] = [this](const json& p) { return tool_ledger(p); };
    }

    ToolResult tool_narrate(const json& params) {
        std::string action = params.at("action");

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
            std::string episode_id = params.at("episode_id");
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

    ToolResult tool_ledger(const json& params) {
        std::string action = params.at("action");

        if (action == "save") {
            json content = params.value("content", json::object());
            mind_->save_ledger(content.dump());
            return ToolResult::ok("Ledger saved", {{"status", "saved"}});

        } else if (action == "load") {
            auto ledger = mind_->load_ledger();
            if (ledger) {
                try {
                    json content = json::parse(ledger->second);
                    return ToolResult::ok("Ledger loaded", {
                        {"id", ledger->first.to_string()},
                        {"content", content}
                    });
                } catch (...) {
                    return ToolResult::ok("Ledger loaded (raw)", {
                        {"id", ledger->first.to_string()},
                        {"content", {{"raw", ledger->second}}}
                    });
                }
            }
            return ToolResult::ok("No ledger found", {{"status", "empty"}});
        }

        return ToolResult::error("Unknown action: " + action);
    }

    // ═══════════════════════════════════════════════════════════════════
    // Maintenance tools
    // ═══════════════════════════════════════════════════════════════════

    void register_maintenance_tools() {
        tools_.push_back({
            "cycle",
            "Run a maintenance cycle: decay, feedback, synthesis, attractors.",
            {
                {"type", "object"},
                {"properties", {
                    {"save", {{"type", "boolean"}, {"default", true}}},
                    {"attractors", {{"type", "boolean"}, {"default", false},
                                   {"description", "Run attractor dynamics"}}}
                }},
                {"required", json::array()}
            }
        });
        handlers_["cycle"] = [this](const json& p) { return tool_cycle(p); };
    }

    ToolResult tool_cycle(const json& params) {
        bool save = params.value("save", true);
        bool run_attractors = params.value("attractors", false);

        DynamicsReport report = mind_->tick();
        size_t feedback_applied = mind_->apply_feedback();
        size_t synthesized = mind_->synthesize_wisdom();

        Mind::AttractorReport attractor_report;
        if (run_attractors) {
            attractor_report = mind_->run_attractor_dynamics();
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

        std::ostringstream ss;
        ss << "Cycle complete. Coherence: " << int(coherence.tau_k() * 100) << "%";
        if (synthesized > 0) ss << ", synthesized: " << synthesized;
        if (feedback_applied > 0) ss << ", feedback: " << feedback_applied;

        return ToolResult::ok(ss.str(), result);
    }
};

} // namespace chitta::mcp
