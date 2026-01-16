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
#include "tools/yajna.hpp"
#include "../mind.hpp"
#include "../version.hpp"
#include <unordered_map>
#include <string>
#include <sstream>
#include <ctime>
#include <chrono>
#include <unistd.h>
#include <utility>

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

struct HandlerContext {
    std::string socket_path;
    std::string db_path;
};

class Handler {
public:
    explicit Handler(Mind* mind, HandlerContext context = {})
        : mind_(mind),
          context_(std::move(context)),
          start_time_(std::chrono::steady_clock::now()) {
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
    HandlerContext context_;
    std::chrono::steady_clock::time_point start_time_;
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

        // Yajna tools (yajna_list, yajna_inspect, tag) for epsilon-yajna ceremony
        tools::yajna::register_schemas(tools_);
        tools::yajna::register_handlers(mind_, handlers_);

        // Phase 7: Scale tools (realm, review, eval, epiplexity)
        register_phase7_tools();
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

        AttractorReport report;
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

        // Get Voice attention weights for each lens
        auto get_voice_weights = [](const std::string& name) -> std::unordered_map<NodeType, float> {
            using NT = NodeType;
            if (name == "manas") {
                // Manas: Quick intuition, boosts episodes
                return {{NT::Wisdom, 0.8f}, {NT::Episode, 1.2f}, {NT::Intention, 1.0f}};
            } else if (name == "buddhi") {
                // Buddhi: Deep analysis, boosts wisdom and beliefs
                return {{NT::Wisdom, 1.5f}, {NT::Belief, 1.3f}, {NT::Episode, 0.7f}};
            } else if (name == "ahamkara") {
                // Ahamkara: Self-protective critic, boosts failures
                return {{NT::Failure, 1.5f}, {NT::Invariant, 1.3f}, {NT::Dream, 0.5f}};
            } else if (name == "chitta") {
                // Chitta: Memory patterns, boosts episodes and terms
                return {{NT::Episode, 1.5f}, {NT::Wisdom, 1.2f}, {NT::Term, 1.3f}};
            } else if (name == "vikalpa") {
                // Vikalpa: Imagination, boosts dreams and aspirations
                return {{NT::Dream, 1.5f}, {NT::Aspiration, 1.3f}, {NT::Belief, 0.7f}};
            } else if (name == "sakshi") {
                // Sakshi: Witness, boosts invariants and beliefs
                return {{NT::Invariant, 1.5f}, {NT::Belief, 1.2f}, {NT::Wisdom, 1.0f}, {NT::Episode, 0.5f}};
            }
            return {};  // No weights for unknown lens
        };

        auto apply_voice_weight = [&](const Recall& r, const std::unordered_map<NodeType, float>& weights) -> float {
            float attn = 1.0f;
            auto it = weights.find(r.type);
            if (it != weights.end()) attn = it->second;
            return r.relevance * attn;
        };

        json result = json::object();
        std::ostringstream ss;
        ss << "Lens search for: " << query << "\n";

        // Get more results to filter through lenses
        auto recalls = mind_->recall(query, limit * 10);

        auto process_lens = [&](const std::string& name) {
            auto weights = get_voice_weights(name);

            // Score and sort recalls through this lens
            struct ScoredRecall {
                const Recall* recall;
                float score;
            };
            std::vector<ScoredRecall> scored;
            for (const auto& r : recalls) {
                float score = apply_voice_weight(r, weights);
                scored.push_back({&r, score});
            }

            // Sort by lens-weighted score
            std::sort(scored.begin(), scored.end(),
                [](const ScoredRecall& a, const ScoredRecall& b) {
                    return a.score > b.score;
                });

            // Build result
            json arr = json::array();
            size_t count = 0;
            for (const auto& sr : scored) {
                if (count++ >= limit) break;
                json item;
                item["id"] = sr.recall->id.to_string();
                item["text"] = sr.recall->text;
                item["score"] = sr.score;
                item["raw_score"] = sr.recall->relevance;
                item["type"] = node_type_to_string(sr.recall->type);
                arr.push_back(item);
            }
            result[name] = arr;
            ss << "\n" << name << ": " << arr.size() << " results";
        };

        if (lens == "all" || lens == "manas") process_lens("manas");
        if (lens == "all" || lens == "buddhi") process_lens("buddhi");
        if (lens == "all" || lens == "ahamkara") process_lens("ahamkara");
        if (lens == "all" || lens == "chitta") process_lens("chitta");
        if (lens == "all" || lens == "vikalpa") process_lens("vikalpa");
        if (lens == "all" || lens == "sakshi") process_lens("sakshi");

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
            "health_check",
            "Return daemon health, version, and readiness metadata.",
            {
                {"type", "object"},
                {"properties", json::object()},
                {"required", json::array()}
            }
        });
        handlers_["health_check"] = [this](const json& p) { return tool_health_check(p); };

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

    ToolResult tool_health_check(const json& params) {
        (void)params;
        auto now = std::chrono::steady_clock::now();
        auto uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();
        json result = {
            {"software_version", CHITTA_VERSION},
            {"protocol_major", CHITTA_PROTOCOL_VERSION_MAJOR},
            {"protocol_minor", CHITTA_PROTOCOL_VERSION_MINOR},
            {"pid", static_cast<int>(getpid())},
            {"uptime_ms", static_cast<uint64_t>(uptime_ms)},
            {"socket_path", context_.socket_path},
            {"db_path", context_.db_path},
            {"status", "ok"}
        };
        std::ostringstream ss;
        ss << "Chitta v" << CHITTA_VERSION
           << " (protocol " << CHITTA_PROTOCOL_VERSION_MAJOR
           << "." << CHITTA_PROTOCOL_VERSION_MINOR << ")";
        return ToolResult::ok(ss.str(), result);
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

        AttractorReport attractor_report;
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

        // Phase 3.7: Competence Mapping
        tools_.push_back({
            "competence",
            "Analyze competence by domain. Shows what I'm good at (high confidence, successes) "
            "vs weak at (low confidence, failures) across different topics/projects.",
            {
                {"type", "object"},
                {"properties", {
                    {"min_samples", {{"type", "integer"}, {"minimum", 3}, {"maximum", 50}, {"default", 5},
                                    {"description", "Minimum nodes per domain to include"}}},
                    {"top_n", {{"type", "integer"}, {"minimum", 3}, {"maximum", 20}, {"default", 10}}}
                }},
                {"required", json::array()}
            }
        });
        handlers_["competence"] = [this](const json& p) { return tool_competence(p); };

        // Phase 3.8: Cross-Project Query
        tools_.push_back({
            "cross_project",
            "Query knowledge across projects. Find patterns that transfer between domains.",
            {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "What to search for across projects"}}},
                    {"source_project", {{"type", "string"}, {"description", "Project to transfer FROM (optional)"}}},
                    {"target_project", {{"type", "string"}, {"description", "Project to transfer TO (optional)"}}},
                    {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 20}, {"default", 10}}}
                }},
                {"required", {"query"}}
            }
        });
        handlers_["cross_project"] = [this](const json& p) { return tool_cross_project(p); };
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

    // Phase 3.7: Competence Mapping
    ToolResult tool_competence(const json& params) {
        size_t min_samples = params.value("min_samples", 5);
        size_t top_n = params.value("top_n", 10);

        struct DomainStats {
            size_t count = 0;
            float total_confidence = 0.0f;
            size_t failures = 0;
            size_t wisdom = 0;
        };
        std::unordered_map<std::string, DomainStats> domains;

        mind_->for_each_node([&](const NodeId& nid, const Node& node) {
            std::string text(node.payload.begin(), node.payload.end());
            std::string domain = "general";

            if (text.size() > 2 && text[0] == '[') {
                size_t end = text.find(']');
                if (end != std::string::npos && end < 50) {
                    domain = text.substr(1, end - 1);
                }
            }

            auto tags = mind_->get_tags(nid);
            for (const auto& tag : tags) {
                if (tag.find("project:") == 0) {
                    domain = tag.substr(8);
                    break;
                }
            }

            auto& stats = domains[domain];
            stats.count++;
            stats.total_confidence += node.kappa.effective();
            if (node.node_type == NodeType::Failure) stats.failures++;
            if (node.node_type == NodeType::Wisdom) stats.wisdom++;
        });

        struct CompetenceScore {
            std::string domain;
            float score;
            float avg_confidence;
            size_t count;
            size_t failures;
            size_t wisdom;
        };
        std::vector<CompetenceScore> scores;

        for (const auto& [domain, stats] : domains) {
            if (stats.count < min_samples) continue;
            float avg_conf = stats.total_confidence / stats.count;
            float wisdom_ratio = static_cast<float>(stats.wisdom) / stats.count;
            float failure_ratio = static_cast<float>(stats.failures) / stats.count;
            float score = avg_conf + (wisdom_ratio * 0.3f) - (failure_ratio * 0.5f);
            scores.push_back({domain, score, avg_conf, stats.count, stats.failures, stats.wisdom});
        }

        std::sort(scores.begin(), scores.end(),
                  [](const auto& a, const auto& b) { return a.score > b.score; });

        json strengths = json::array();
        json weaknesses = json::array();
        std::ostringstream ss;
        ss << "Competence Analysis (" << scores.size() << " domains):\n\n";
        ss << "STRENGTHS:\n";

        for (size_t i = 0; i < std::min(top_n, scores.size()); ++i) {
            const auto& s = scores[i];
            strengths.push_back({{"domain", s.domain}, {"score", s.score}, {"count", s.count}});
            ss << "  [" << static_cast<int>(s.score * 100) << "%] " << s.domain
               << " (" << s.count << " nodes)\n";
        }

        ss << "\nWEAKNESSES:\n";
        for (size_t i = scores.size(); i > 0 && scores.size() - i < top_n; --i) {
            const auto& s = scores[i - 1];
            weaknesses.push_back({{"domain", s.domain}, {"score", s.score}, {"failures", s.failures}});
            ss << "  [" << static_cast<int>(s.score * 100) << "%] " << s.domain
               << " (" << s.failures << " failures)\n";
        }

        return ToolResult::ok(ss.str(), {{"strengths", strengths}, {"weaknesses", weaknesses}});
    }

    // Phase 3.8: Cross-Project Query
    ToolResult tool_cross_project(const json& params) {
        auto err = validate_required(params, {"query"});
        if (!err.empty()) return ToolResult::error(err);

        std::string query = params["query"];
        size_t limit = params.value("limit", 10);

        if (!mind_->has_yantra()) {
            return ToolResult::error("Yantra not ready");
        }

        auto all_results = mind_->recall(query, limit * 3);

        std::unordered_map<std::string, std::vector<const Recall*>> by_project;
        for (const auto& r : all_results) {
            std::string project = "general";
            if (r.text.size() > 2 && r.text[0] == '[') {
                size_t end = r.text.find(']');
                if (end != std::string::npos && end < 50) {
                    project = r.text.substr(1, end - 1);
                }
            }
            auto tags = mind_->get_tags(r.id);
            for (const auto& tag : tags) {
                if (tag.find("project:") == 0) {
                    project = tag.substr(8);
                    break;
                }
            }
            by_project[project].push_back(&r);
        }

        json projects = json::object();
        json transferable = json::array();
        std::ostringstream ss;
        ss << "Cross-Project Query: " << query << "\n\n";

        for (const auto& [project, results] : by_project) {
            json proj_results = json::array();
            size_t shown = 0;
            for (const auto* rp : results) {
                if (shown++ >= limit) break;
                proj_results.push_back({
                    {"id", rp->id.to_string()},
                    {"text", rp->text.substr(0, 100)},
                    {"relevance", rp->relevance}
                });
            }
            projects[project] = proj_results;
            ss << "[" << project << "] " << results.size() << " results\n";

            for (const auto* rp : results) {
                if (rp->type == NodeType::Wisdom && rp->relevance > 0.5f) {
                    transferable.push_back({
                        {"from", project},
                        {"pattern", rp->text.substr(0, 80)}
                    });
                }
            }
        }

        if (!transferable.empty()) {
            ss << "\nTRANSFERABLE (" << transferable.size() << "):\n";
            for (const auto& t : transferable) {
                ss << "  [" << t["from"].get<std::string>() << "] " << t["pattern"].get<std::string>() << "\n";
            }
        }

        return ToolResult::ok(ss.str(), {{"projects", projects}, {"transferable", transferable}});
    }

    // ═══════════════════════════════════════════════════════════════════
    // Phase 7: Scale tools (100M+ infrastructure)
    // ═══════════════════════════════════════════════════════════════════

    void register_phase7_tools() {
        // Realm tools
        tools_.push_back({
            "realm_get",
            "Get current realm context. Realms gate which nodes are visible during recall.",
            {{"type", "object"}, {"properties", json::object()}, {"required", json::array()}}
        });
        handlers_["realm_get"] = [this](const json&) { return tool_realm_get(); };

        tools_.push_back({
            "realm_set",
            "Set current realm (persists across sessions). Only nodes scoped to this realm are visible.",
            {{"type", "object"},
             {"properties", {{"realm", {{"type", "string"}, {"description", "Realm name (e.g., 'project:cc-soul')"}}}}},
             {"required", {"realm"}}}
        });
        handlers_["realm_set"] = [this](const json& p) { return tool_realm_set(p); };

        tools_.push_back({
            "realm_create",
            "Create a new realm with optional parent. Realms form a hierarchy from 'brahman' (root).",
            {{"type", "object"},
             {"properties", {
                 {"name", {{"type", "string"}, {"description", "Realm name"}}},
                 {"parent", {{"type", "string"}, {"default", "brahman"}, {"description", "Parent realm"}}}
             }},
             {"required", {"name"}}}
        });
        handlers_["realm_create"] = [this](const json& p) { return tool_realm_create(p); };

        // Review tools
        tools_.push_back({
            "review_list",
            "List items in review queue for human oversight.",
            {{"type", "object"},
             {"properties", {
                 {"status", {{"type", "string"}, {"enum", {"pending", "approved", "rejected", "deferred", "all"}}, {"default", "pending"}}},
                 {"limit", {{"type", "integer"}, {"default", 10}}}
             }},
             {"required", json::array()}}
        });
        handlers_["review_list"] = [this](const json& p) { return tool_review_list(p); };

        tools_.push_back({
            "review_decide",
            "Make a review decision. Updates confidence and provenance trust based on decision.",
            {{"type", "object"},
             {"properties", {
                 {"id", {{"type", "string"}, {"description", "Node ID"}}},
                 {"decision", {{"type", "string"}, {"enum", {"approve", "reject", "edit", "defer"}}}},
                 {"comment", {{"type", "string"}}},
                 {"edited_content", {{"type", "string"}}},
                 {"quality_rating", {{"type", "number"}, {"minimum", 0}, {"maximum", 5}, {"default", 3}}}
             }},
             {"required", {"id", "decision"}}}
        });
        handlers_["review_decide"] = [this](const json& p) { return tool_review_decide(p); };

        tools_.push_back({
            "review_batch",
            "Batch review: apply same decision to multiple items.",
            {{"type", "object"},
             {"properties", {
                 {"decision", {{"type", "string"}, {"enum", {"approve", "reject", "defer"}}}},
                 {"ids", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                 {"limit", {{"type", "integer"}, {"default", 10}}},
                 {"comment", {{"type", "string"}}},
                 {"quality_rating", {{"type", "number"}, {"default", 3}}}
             }},
             {"required", {"decision"}}}
        });
        handlers_["review_batch"] = [this](const json& p) { return tool_review_batch(p); };

        tools_.push_back({
            "review_stats",
            "Get review queue statistics.",
            {{"type", "object"}, {"properties", json::object()}, {"required", json::array()}}
        });
        handlers_["review_stats"] = [this](const json&) { return tool_review_stats(); };

        // Eval tools
        tools_.push_back({
            "eval_run",
            "Run golden recall test suite.",
            {{"type", "object"},
             {"properties", {{"test_name", {{"type", "string"}, {"description", "Specific test (empty = all)"}}}}},
             {"required", json::array()}}
        });
        handlers_["eval_run"] = [this](const json& p) { return tool_eval_run(p); };

        tools_.push_back({
            "eval_add_test",
            "Add a test case to eval harness.",
            {{"type", "object"},
             {"properties", {
                 {"name", {{"type", "string"}}},
                 {"query", {{"type", "string"}}},
                 {"expected", {{"type", "string"}, {"description", "Comma-separated expected node IDs"}}}
             }},
             {"required", {"name", "query", "expected"}}}
        });
        handlers_["eval_add_test"] = [this](const json& p) { return tool_eval_add_test(p); };

        // Epiplexity tools
        tools_.push_back({
            "epiplexity_check",
            "Check compression quality: can I reconstruct from seed?",
            {{"type", "object"},
             {"properties", {
                 {"node_ids", {{"type", "string"}, {"description", "Comma-separated IDs (empty = sample)"}}},
                 {"sample_size", {{"type", "integer"}, {"default", 10}}}
             }},
             {"required", json::array()}}
        });
        handlers_["epiplexity_check"] = [this](const json& p) { return tool_epiplexity_check(p); };

        tools_.push_back({
            "epiplexity_drift",
            "Analyze epsilon drift over time.",
            {{"type", "object"},
             {"properties", {{"lookback_days", {{"type", "integer"}, {"default", 7}}}}},
             {"required", json::array()}}
        });
        handlers_["epiplexity_drift"] = [this](const json& p) { return tool_epiplexity_drift(p); };
    }

    // Phase 7 tool implementations
    ToolResult tool_realm_get() {
        std::string current = mind_->current_realm();
        std::ostringstream ss;
        ss << "Current realm: " << current << "\n";
        ss << "(Realm context persists across sessions)\n";
        return ToolResult::ok(ss.str(), {{"current_realm", current}});
    }

    ToolResult tool_realm_set(const json& params) {
        std::string realm = params.value("realm", "");
        if (realm.empty()) return ToolResult::error("realm parameter required");

        std::string old_realm = mind_->current_realm();
        mind_->set_realm(realm);
        std::string new_realm = mind_->current_realm();

        std::ostringstream ss;
        ss << "Realm changed: " << old_realm << " -> " << new_realm << "\n";
        return ToolResult::ok(ss.str(), {{"old_realm", old_realm}, {"new_realm", new_realm}});
    }

    ToolResult tool_realm_create(const json& params) {
        std::string name = params.value("name", "");
        std::string parent = params.value("parent", "brahman");
        if (name.empty()) return ToolResult::error("name parameter required");

        mind_->create_realm(name, parent);

        std::ostringstream ss;
        ss << "Created realm: " << name << " (parent: " << parent << ")\n";
        return ToolResult::ok(ss.str(), {{"name", name}, {"parent", parent}});
    }

    ToolResult tool_review_list(const json& params) {
        std::string status = params.value("status", "pending");
        size_t limit = params.value("limit", 10);
        auto& queue = mind_->review_queue();

        std::vector<ReviewItem> items;
        if (status == "pending") items = queue.get_batch(limit);
        else if (status == "all") items = queue.get_batch(limit);

        std::ostringstream ss;
        ss << "=== Review Queue (" << status << ") ===\n";
        json items_json = json::array();
        for (const auto& item : items) {
            ss << "[" << item.id.to_string().substr(0,8) << "] " << item.content.substr(0, 60) << "...\n";
            items_json.push_back({{"id", item.id.to_string()}, {"content", item.content.substr(0, 100)}});
        }
        return ToolResult::ok(ss.str(), {{"items", items_json}});
    }

    ToolResult tool_review_decide(const json& params) {
        std::string id_str = params.at("id");
        std::string decision = params.at("decision");
        std::string comment = params.value("comment", "");
        std::string edited_content = params.value("edited_content", "");
        float quality_rating = params.value("quality_rating", 3.0f);

        NodeId id = NodeId::from_string(id_str);
        auto& queue = mind_->review_queue();
        Timestamp current = mind_->now();

        float q = std::clamp(quality_rating, 0.0f, 5.0f);
        float conf_delta = 0.0f;

        if (decision == "approve") {
            queue.approve(id, comment, quality_rating, current);
            conf_delta = (q > 0.0f) ? (q - 3.0f) * 0.05f : 0.05f;
            mind_->strengthen(id, std::max(0.0f, conf_delta));
        } else if (decision == "reject") {
            queue.reject(id, comment, current);
            conf_delta = -std::max(0.1f, (3.0f - q) * 0.07f);
            mind_->weaken(id, -conf_delta);
        } else if (decision == "edit") {
            queue.approve_with_edits(id, edited_content, comment, quality_rating, current);
            if (!edited_content.empty()) mind_->update_content(id, edited_content);
            conf_delta = (q > 0.0f) ? (q - 3.0f) * 0.05f : 0.05f;
            mind_->strengthen(id, std::max(0.0f, conf_delta));
        } else if (decision == "defer") {
            queue.defer(id, comment);
        } else {
            return ToolResult::error("Invalid decision: " + decision);
        }

        if (conf_delta != 0.0f) mind_->update_provenance_trust(id, conf_delta * 0.5f);

        return ToolResult::ok("Review decision: " + decision, {{"id", id_str}, {"decision", decision}, {"confidence_delta", conf_delta}});
    }

    ToolResult tool_review_batch(const json& params) {
        std::string decision = params.at("decision");
        size_t limit = params.value("limit", 10);
        std::string comment = params.value("comment", "Batch decision");
        float quality_rating = params.value("quality_rating", 3.0f);

        auto& queue = mind_->review_queue();
        Timestamp current = mind_->now();

        std::vector<NodeId> ids;
        if (params.contains("ids") && params["ids"].is_array()) {
            for (const auto& id_str : params["ids"]) {
                ids.push_back(NodeId::from_string(id_str.get<std::string>()));
            }
        } else {
            auto items = queue.get_batch(limit);
            for (const auto& item : items) ids.push_back(item.id);
        }

        size_t processed = 0;
        for (const auto& id : ids) {
            if (decision == "approve") queue.approve(id, comment, quality_rating, current);
            else if (decision == "reject") queue.reject(id, comment, current);
            else if (decision == "defer") queue.defer(id, comment);
            processed++;
        }

        std::ostringstream ss;
        ss << "Batch " << decision << ": " << processed << " items\n";
        return ToolResult::ok(ss.str(), {{"decision", decision}, {"processed", processed}});
    }

    ToolResult tool_review_stats() {
        auto& queue = mind_->review_queue();
        auto stats = queue.get_stats();

        std::ostringstream ss;
        ss << "=== Review Stats ===\n";
        ss << "Pending: " << stats.pending << "\n";
        ss << "Approved: " << stats.approved << "\n";
        ss << "Rejected: " << stats.rejected << "\n";
        ss << "Approval rate: " << std::fixed << std::setprecision(1) << stats.approval_rate * 100 << "%\n";

        return ToolResult::ok(ss.str(), {
            {"pending", stats.pending}, {"approved", stats.approved},
            {"rejected", stats.rejected}, {"approval_rate", stats.approval_rate}
        });
    }

    ToolResult tool_eval_run(const json& params) {
        std::string test_name = params.value("test_name", "");
        (void)test_name;
        auto& harness = mind_->eval_harness();

        std::ostringstream ss;
        ss << "=== Eval Harness ===\n";
        ss << "Test cases loaded: " << harness.test_count() << "\n";
        ss << "(Running tests requires recall callback - use programmatic API)\n";

        json result;
        result["test_count"] = harness.test_count();
        result["status"] = "ready";
        return ToolResult::ok(ss.str(), result);
    }

    ToolResult tool_eval_add_test(const json& params) {
        std::string name = params.at("name");
        std::string query_str = params.at("query");
        std::string expected_str = params.at("expected");

        // Parse comma-separated node IDs into ExpectedResult vector
        std::vector<ExpectedResult> expected;
        std::stringstream ess(expected_str);
        std::string id_str;
        while (std::getline(ess, id_str, ',')) {
            if (!id_str.empty()) {
                ExpectedResult er;
                er.id = NodeId::from_string(id_str);
                er.min_score = 0.0f;
                er.max_rank = 10;
                er.required = true;
                expected.push_back(er);
            }
        }

        GoldenTestCase test;
        test.name = name;
        test.query = query_str;
        test.expected = expected;

        auto& harness = mind_->eval_harness();
        harness.add_test(test);

        json result;
        result["name"] = name;
        result["expected_count"] = expected.size();
        return ToolResult::ok("Added test: " + name, result);
    }

    ToolResult tool_epiplexity_check(const json& params) {
        size_t sample_size = params.value("sample_size", 10);
        (void)sample_size;

        // Simplified: just report the test is available
        std::ostringstream ss;
        ss << "=== Epiplexity Check ===\n";
        ss << "Epiplexity test infrastructure ready.\n";
        ss << "Use specific node IDs to measure compression quality.\n";

        json result;
        result["status"] = "ready";
        result["message"] = "Use node IDs for specific measurements";
        return ToolResult::ok(ss.str(), result);
    }

    ToolResult tool_epiplexity_drift(const json& params) {
        int lookback_days = params.value("lookback_days", 7);
        (void)lookback_days;  // Not implemented yet

        std::ostringstream ss;
        ss << "=== Epiplexity Drift ===\n";
        ss << "Drift analysis not yet implemented\n";
        ss << "(Requires historical epsilon measurements)\n";

        json result;
        result["drift_detected"] = false;
        result["message"] = "Not implemented";
        return ToolResult::ok(ss.str(), result);
    }
};

} // namespace chitta::rpc
