// field_misc.sadhana — bodies extracted from field_misc.cpp by theme.
// Declarations remain in chitta/include/chitta/rpc/handlers/field_misc.hpp.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

ToolResult FieldRpcHandler::tool_sadhana_start(const json& params) {
    if (!sadhana_manager_)
        return ToolResult::error("Sadhana manager not initialized");

    std::string goal = params.value("goal", "");
    if (goal.empty()) return ToolResult::error("Goal is required");

    std::string provider  = params.value("brain_provider", "");
    std::string model     = params.value("brain_model", "");
    int interval          = params.value("interval_seconds", 0);
    int max_turns         = params.value("max_turns", 0);
    std::string realm     = params.value("realm", "brahman");
    json goal_dsl;
    if (params.contains("goal_dsl") && params["goal_dsl"].is_object())
        goal_dsl = params["goal_dsl"];

    int64_t id = sadhana_manager_->create(goal, provider, model, interval, realm, goal_dsl, max_turns);
    if (id == 0) return ToolResult::error("Failed to create sadhana");

    if (!sadhana_manager_->start(id))
        return ToolResult::error("Created sadhana " + std::to_string(id) + " but failed to start");

    json result = {{"id", id}, {"state", "running"}, {"goal", goal.substr(0, 100)}};
    return ToolResult::ok("Started sadhana " + std::to_string(id), result);
}

ToolResult FieldRpcHandler::tool_sadhana_pause(const json& params) {
    if (!sadhana_manager_)
        return ToolResult::error("Sadhana manager not initialized");

    auto [id, id_str] = parse_id(params, "id");
    if (id == 0) return ToolResult::error("Invalid sadhana ID");

    if (!sadhana_manager_->pause(id))
        return ToolResult::error("Failed to pause sadhana " + std::to_string(id));

    return ToolResult::ok("Paused sadhana " + std::to_string(id),
        {{"id", id}, {"state", "paused"}});
}

ToolResult FieldRpcHandler::tool_sadhana_resume(const json& params) {
    if (!sadhana_manager_)
        return ToolResult::error("Sadhana manager not initialized");

    auto [id, id_str] = parse_id(params, "id");
    if (id == 0) return ToolResult::error("Invalid sadhana ID");

    if (!sadhana_manager_->resume(id))
        return ToolResult::error("Failed to resume sadhana " + std::to_string(id));

    return ToolResult::ok("Resumed sadhana " + std::to_string(id),
        {{"id", id}, {"state", "running"}});
}

ToolResult FieldRpcHandler::tool_sadhana_stop(const json& params) {
    if (!sadhana_manager_)
        return ToolResult::error("Sadhana manager not initialized");

    auto [id, id_str] = parse_id(params, "id");
    if (id == 0) return ToolResult::error("Invalid sadhana ID");

    bool success  = params.value("success", true);
    std::string reason = params.value("reason", "");

    if (!sadhana_manager_->stop(id, success, reason))
        return ToolResult::error("Failed to stop sadhana " + std::to_string(id));

    json result = {{"id", id}, {"state", success ? "done" : "failed"}, {"reason", reason}};
    return ToolResult::ok("Stopped sadhana " + std::to_string(id), result);
}

ToolResult FieldRpcHandler::tool_sadhana_status(const json& params) {
    if (!sadhana_manager_)
        return ToolResult::error("Sadhana manager not initialized");

    auto [id, id_str] = parse_id(params, "id");
    if (id == 0) return ToolResult::error("Invalid sadhana ID");

    auto opt = sadhana_manager_->get(id);
    if (!opt) return ToolResult::error("Sadhana " + std::to_string(id) + " not found");

    size_t history_limit = params.value("history_limit", 20);
    auto history = sadhana_manager_->get_history(id, history_limit);

    json result;
    result["id"]               = opt->id;
    result["goal"]             = opt->goal;
    result["state"]            = sadhana_state_to_string(opt->state);
    result["brain_provider"]   = opt->brain_provider;
    result["brain_model"]      = opt->brain_model;
    result["iterations"]       = opt->iterations;
    result["brain_calls"]      = opt->brain_calls;
    result["cost_usd"]         = opt->cost_usd;
    result["interval_seconds"] = opt->interval_seconds;
    result["max_turns"]        = opt->max_turns;
    result["realm"]            = opt->realm;
    result["created_at"]       = opt->created_at;
    result["updated_at"]       = opt->updated_at;
    result["last_sense"]       = opt->last_sense;
    result["last_action"]      = opt->last_action;
    result["last_result"]      = opt->last_result;
    result["history"]          = history;

    std::ostringstream msg;
    msg << "Sadhana " << id << " [" << sadhana_state_to_string(opt->state) << "] "
        << opt->iterations << " iterations";
    return ToolResult::ok(msg.str(), result);
}

ToolResult FieldRpcHandler::tool_sadhana_list(const json& params) {
    if (!sadhana_manager_)
        return ToolResult::error("Sadhana manager not initialized");

    std::string state = params.value("state", "");
    std::string realm = params.value("realm", "");
    size_t limit      = params.value("limit", 50);

    auto sadhanas = sadhana_manager_->list(state, realm, limit);

    json result;
    result["sadhanas"] = json::array();
    result["count"]    = sadhanas.size();

    for (const auto& s : sadhanas) {
        result["sadhanas"].push_back({
            {"id",               s.id},
            {"goal",             s.goal.substr(0, 100)},
            {"state",            sadhana_state_to_string(s.state)},
            {"brain_model",      s.brain_model},
            {"iterations",       s.iterations},
            {"interval_seconds", s.interval_seconds},
            {"realm",            s.realm},
            {"created_at",       s.created_at},
        });
    }

    return ToolResult::ok("Found " + std::to_string(sadhanas.size()) + " sadhana(s)", result);
}

ToolResult FieldRpcHandler::tool_sadhana_set_model(const json& params) {
    if (!sadhana_manager_)
        return ToolResult::error("Sadhana manager not initialized");

    auto [id, id_str] = parse_id(params, "id");
    if (id == 0) return ToolResult::error("Invalid sadhana ID");

    std::string model = params.value("model", "");
    if (model.empty()) return ToolResult::error("model is required");

    if (!sadhana_manager_->set_model(id, model))
        return ToolResult::error("Failed to set model for sadhana " + std::to_string(id));

    return ToolResult::ok("Set model to " + model + " for sadhana " + std::to_string(id),
        {{"id", id}, {"model", model}});
}

ToolResult FieldRpcHandler::tool_sadhana_set_goal(const json& params) {
    if (!sadhana_manager_)
        return ToolResult::error("Sadhana manager not initialized");

    auto [id, id_str] = parse_id(params, "id");
    if (id == 0) return ToolResult::error("Invalid sadhana ID");

    std::string goal = params.value("goal", "");
    if (goal.empty()) return ToolResult::error("goal is required");

    if (!sadhana_manager_->set_goal(id, goal))
        return ToolResult::error("Failed to set goal for sadhana " + std::to_string(id));

    return ToolResult::ok("Updated goal for sadhana " + std::to_string(id),
        {{"id", id}, {"goal", goal}});
}

ToolResult FieldRpcHandler::tool_sadhana_set_interval(const json& params) {
    if (!sadhana_manager_)
        return ToolResult::error("Sadhana manager not initialized");

    auto [id, id_str] = parse_id(params, "id");
    if (id == 0) return ToolResult::error("Invalid sadhana ID");

    int interval = params.value("interval", 0);
    if (interval <= 0) return ToolResult::error("interval must be positive");

    if (!sadhana_manager_->set_interval(id, interval))
        return ToolResult::error("Failed to set interval for sadhana " + std::to_string(id));

    return ToolResult::ok(
        "Set interval to " + std::to_string(interval) + "s for sadhana " + std::to_string(id),
        {{"id", id}, {"interval", interval}});
}

ToolResult FieldRpcHandler::tool_sadhana_set_max_turns(const json& params) {
    if (!sadhana_manager_)
        return ToolResult::error("Sadhana manager not initialized");

    auto [id, id_str] = parse_id(params, "id");
    if (id == 0) return ToolResult::error("Invalid sadhana ID");

    int max_turns = params.value("max_turns", -1);
    if (max_turns < 0) return ToolResult::error("max_turns must be >= 0 (0 = use global default)");

    if (!sadhana_manager_->set_max_turns(id, max_turns))
        return ToolResult::error("Failed to set max_turns for sadhana " + std::to_string(id));

    std::string msg = max_turns == 0
        ? "Reset max_turns to global default for sadhana " + std::to_string(id)
        : "Set max_turns to " + std::to_string(max_turns) + " for sadhana " + std::to_string(id);
    return ToolResult::ok(msg, {{"id", id}, {"max_turns", max_turns}});
}

ToolResult FieldRpcHandler::tool_sadhana_checkpoint(const json& params) {
    if (!sadhana_manager_)
        return ToolResult::error("Sadhana manager not initialized");

    auto [id, id_str] = parse_id(params, "id");
    if (id == 0) return ToolResult::error("Invalid sadhana ID");

    std::string status  = params.value("status", "progressed");
    std::string summary = params.value("summary", "");
    if (summary.empty()) return ToolResult::error("summary is required");

    if (!sadhana_manager_->checkpoint(id, status, summary))
        return ToolResult::error("Checkpoint failed for sadhana " + std::to_string(id));

    return ToolResult::ok("Checkpoint [" + status + "] for sadhana " + std::to_string(id),
        {{"id", id}, {"status", status}, {"summary", summary}});
}

ToolResult FieldRpcHandler::tool_dream_cancel(const json& params) {
    if (!params.contains("id"))
        return ToolResult::error("id is required");

    int64_t dream_id = params["id"].is_string()
        ? std::stoll(params["id"].get<std::string>())
        : params["id"].get<int64_t>();

    field_store_->emit_event("dream", "cancelled", std::to_string(dream_id), "");

    return ToolResult::ok("Cancelled dream #" + std::to_string(dream_id),
        {{"dream_id", dream_id}, {"status", "cancelled"}});
}

ToolResult FieldRpcHandler::tool_dream_force_woke(const json& params) {
    if (!params.contains("id"))
        return ToolResult::error("id is required");

    int64_t dream_id = params["id"].is_string()
        ? std::stoll(params["id"].get<std::string>())
        : params["id"].get<int64_t>();

    field_store_->emit_event("dream", "force_woke", std::to_string(dream_id), "[force-woke]");

    return ToolResult::ok("Force-woke dream #" + std::to_string(dream_id),
        {{"dream_id", dream_id}, {"status", "woke"}});
}

ToolResult FieldRpcHandler::tool_dream_start(const json& params) {
    if (!sadhana_manager_)
        return ToolResult::error("Sadhana manager not initialized");
    if (subconscious_) subconscious_->notify_query();

    std::string topic = params.value("topic", "");
    if (topic.empty()) return ToolResult::error("Topic is required");

    std::string realm          = params.value("realm", "brahman");
    std::string publish_path   = params.value("publish_path", "");
    const auto& cfg = sadhana_manager_->config();
    std::string brain_provider = params.value("brain_provider", cfg.default_brain_provider);
    std::string brain_model    = params.value("brain_model",    cfg.default_brain_model);

    json goal_dsl = {{"kind", "dream"}, {"topic", topic}};
    if (!publish_path.empty()) goal_dsl["publish_path"] = publish_path;

    std::string goal = "[dream] Explore: " + topic;
    int64_t sadhana_id = sadhana_manager_->create(
        goal, brain_provider, brain_model, 0, realm, goal_dsl);

    if (sadhana_id == 0)
        return ToolResult::error("Failed to create dream sadhana");

    if (!sadhana_manager_->start(sadhana_id)) {
        return ToolResult::error(
            "Created dream sadhana " + std::to_string(sadhana_id) + " but failed to start");
    }

    // Record dream start as an event
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    field_store_->emit_event("dream", "started", std::to_string(sadhana_id),
        "{\"topic\":\"" + topic + "\",\"started_at\":" + std::to_string(now_ms) + "}");

    json result = {
        {"sadhana_id", sadhana_id},
        {"topic",      topic},
        {"status",     "dreaming"},
    };
    return ToolResult::ok(
        "Dream started: " + topic + " (sadhana #" + std::to_string(sadhana_id) + ")",
        result);
}

ToolResult FieldRpcHandler::tool_dream_wander(const json& params) {
    if (!sadhana_manager_)
        return ToolResult::error("Sadhana manager not initialized");
    if (subconscious_) subconscious_->notify_query();

    // Skip if a dream sadhana is already running.
    for (const auto& s : sadhana_manager_->list_active()) {
        if (s.goal_dsl.value("kind", "") == "dream")
            return ToolResult::ok("Dream sadhana already active, skipping",
                {{"sadhana_id", s.id}, {"status", "already_running"}});
    }

    std::string realm        = params.value("realm", "brahman");
    std::string publish_path = params.value("publish_path", "");
    if (publish_path.empty()) {
        const char* env_path = std::getenv("CHITTA_DREAM_PUBLISH_PATH");
        if (env_path) publish_path = env_path;
    }

    std::string topic;

    // Priority 1: open questions / gaps
    auto gaps = field_store_->recall_by_kind("question", 5);
    if (!gaps.empty()) {
        topic = gaps[0].content;
        if (topic.size() > 100) topic = topic.substr(0, 100);
    }

    // Priority 2: low-confidence memories
    if (topic.empty()) {
        auto low_conf = field_store_->recall_by_kind("episode", 20);
        for (const auto& h : low_conf) {
            if (h.confidence > 0.0f && h.confidence < 0.5f) {
                topic = h.content;
                if (topic.size() > 100) topic = topic.substr(0, 100);
                break;
            }
        }
    }

    // Priority 3: curiosity seeds
    if (topic.empty()) {
        static const std::vector<std::string> seeds = {
            "consciousness and the hard problem of subjective experience",
            "emergent complexity in distributed systems",
            "the nature of memory and forgetting in biological brains",
            "Vedantic philosophy and modern neuroscience",
            "language models and the limits of statistical learning",
            "self-organization in nature: from cells to civilizations",
            "the history of symbolic AI versus connectionism",
            "epistemic humility in scientific discovery",
            "attention mechanisms and the binding problem",
            "entropy, information, and the arrow of time",
        };
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        topic = seeds[static_cast<size_t>(now_ms) % seeds.size()];
    }

    const auto& cfg = sadhana_manager_->config();
    json start_params = {
        {"topic",          topic},
        {"realm",          realm},
        {"brain_provider", cfg.default_brain_provider},
        {"brain_model",    cfg.default_brain_model},
    };
    if (!publish_path.empty()) start_params["publish_path"] = publish_path;
    return tool_dream_start(start_params);
}

ToolResult FieldRpcHandler::tool_dream_list(const json& params) {
    if (subconscious_) subconscious_->notify_query();

    size_t limit      = static_cast<size_t>(params.value("limit", 10));
    std::string realm = params.value("realm", "");

    // Dreams are recorded as events; retrieve recent dream start events
    auto hits = field_store_->recall_keyword("dream started", std::min(limit, size_t{50}));

    json dreams = json::array();
    for (const auto& h : hits) {
        if (!realm.empty() && h.realm != realm) continue;
        dreams.push_back({
            {"sadhana_id", std::to_string(h.memory_id)},
            {"topic",      h.content},
            {"status",     "unknown"},
            {"realm",      h.realm},
        });
        if (dreams.size() >= limit) break;
    }

    return ToolResult::ok("Found " + std::to_string(dreams.size()) + " dream(s)",
        {{"dreams", dreams}, {"count", dreams.size()}});
}

ToolResult FieldRpcHandler::tool_dream_status(const json& params) {
    if (subconscious_) subconscious_->notify_query();

    if (!params.contains("id"))
        return ToolResult::error("id is required");

    int64_t dream_id = params["id"].is_string()
        ? std::stoll(params["id"].get<std::string>())
        : params["id"].get<int64_t>();

    // In chitta-field, dream state is tracked via sadhana
    json dream = {{"sadhana_id", dream_id}, {"status", "unknown"}};
    if (sadhana_manager_) {
        auto opt = sadhana_manager_->get(dream_id);
        if (opt) {
            dream["state"] = sadhana_state_to_string(opt->state);
            dream["iterations"] = opt->iterations;
            dream["last_action"] = opt->last_action;
        }
    }

    return ToolResult::ok("Dream/sadhana #" + std::to_string(dream_id), dream);
}

ToolResult FieldRpcHandler::tool_impl_start(const json& params) {
    if (subconscious_) subconscious_->notify_query();
    if (!sadhana_manager_)
        return ToolResult::error("Sadhana manager not available");

    std::string realm  = params.value("realm", std::string("brahman"));
    int interval       = params.value("interval_seconds", 86400);
    int max_turns      = params.value("max_turns", 15);
    std::string repo   = params.value("repo", std::string(""));
    if (repo.empty())
        repo = "/maps/projects/fernandezguerra/apps/repos/cc-soul";

    json goal_dsl = {{"kind", "impl"}, {"repo", repo}};

    std::string goal =
        "Autonomous self-improvement loop for cc-soul. "
        "Each cycle: find one pending [impl]/[thought][impl]/[dream][impl] memory, "
        "implement the change in " + repo + ", "
        "run review gate, commit only if approved.";

    int64_t sadhana_id = sadhana_manager_->create(
        goal, "local", "gemma4:26b", interval, realm, goal_dsl, max_turns);
    if (!sadhana_id)
        return ToolResult::error("Failed to create impl sadhana");

    if (!sadhana_manager_->start(sadhana_id))
        return ToolResult::error("Failed to start impl sadhana");

    json result = {
        {"sadhana_id",       sadhana_id},
        {"status",           "running"},
        {"realm",            realm},
        {"interval_seconds", interval},
        {"repo",             repo},
    };
    return ToolResult::ok(
        "Impl sadhana #" + std::to_string(sadhana_id) +
        " started (daily, " + std::to_string(max_turns) + " turns/cycle)",
        result);
}

ToolResult FieldRpcHandler::tool_think_wander(const json& params) {
    if (subconscious_) subconscious_->notify_query();
    if (!sadhana_manager_)
        return ToolResult::error("Sadhana manager not available");

    // Skip if a think sadhana is already running to prevent uncontrolled token usage.
    for (const auto& s : sadhana_manager_->list_active()) {
        if (s.goal_dsl.value("kind", "") == "think")
            return ToolResult::ok("Think sadhana already active, skipping",
                {{"sadhana_id", s.id}, {"status", "already_running"}});
    }

    std::string realm = params.value("realm", std::string("brahman"));
    json goal_dsl = {{"kind", "think"}};
    std::string goal = "[think] Internal memory synthesis: find patterns, connect gaps";

    int64_t sadhana_id = sadhana_manager_->create(
        goal, "local", "gemma4:26b", 0, realm, goal_dsl, 10);
    if (!sadhana_id)
        return ToolResult::error("Failed to create think sadhana");

    if (!sadhana_manager_->start(sadhana_id))
        return ToolResult::error("Failed to start think sadhana");

    return ToolResult::ok("Think sadhana #" + std::to_string(sadhana_id) + " started",
        {{"sadhana_id", sadhana_id}, {"status", "thinking"}, {"realm", realm}});
}
} // namespace chitta
