// field_misc.file_index — bodies extracted from field_misc.cpp by theme.
// Declarations remain in chitta/include/chitta/rpc/handlers/field_misc.hpp.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

ToolResult FieldRpcHandler::tool_file_timeline(const json& params) {
    std::string query      = params.value("query", "");
    std::string session_id = params.value("session_id", "");
    std::string path       = params.value("path", "");
    size_t limit           = static_cast<size_t>(params.value("limit", 20));

    std::string search = query.empty() ? "file" : query;
    if (!path.empty()) search = path;

    auto hits = field_store_->recall_keyword(search, limit);
    json events = hits_to_results_json(hits);

    return ToolResult::ok(std::to_string(events.size()) + " file event(s) found",
        {{"events", events}, {"count", events.size()}});
}

ToolResult FieldRpcHandler::tool_file_at_time(const json&) {
    return ToolResult::error(
        "File time machine not available in chitta-field backend");
}

ToolResult FieldRpcHandler::tool_file_restore(const json&) {
    return ToolResult::error(
        "File restore not available in chitta-field backend");
}

ToolResult FieldRpcHandler::tool_file_index_session(const json& params) {
    std::string session_id = params.value("session_id", "");
    field_store_->emit_event("file_index", "session", session_id, "{}");
    return ToolResult::ok("File index session event emitted",
        {{"session_id", session_id}, {"status", "ok"}});
}

ToolResult FieldRpcHandler::tool_file_index_all(const json& params) {
    field_store_->emit_event("file_index", "all", "", "{}");
    return ToolResult::ok("File index all event emitted", {{"status", "ok"}});
}

ToolResult FieldRpcHandler::tool_learn_outcome(const json& params) {
    auto [memory_id, memory_str] = parse_id(params, "memory_id");
    if (memory_id <= 0) return ToolResult::error("memory_id is required");
    if (field_store_->get_content(static_cast<uint64_t>(memory_id)).empty()) {
        return ToolResult::error("memory not found: " + memory_str);
    }

    std::string outcome = params.value("outcome", "");
    std::string context = params.value("context", "");

    field_store_->emit_event("outcome", outcome, memory_str, context);

    float reward = 0.0f;
    if (outcome == "positive" || outcome == "helpful" || outcome == "correct") {
        field_store_->strengthen(static_cast<uint64_t>(memory_id), 0.1f);
        reward = 0.5f;
    } else if (outcome == "negative" || outcome == "unhelpful" || outcome == "incorrect") {
        field_store_->weaken(static_cast<uint64_t>(memory_id), 0.05f);
        reward = -0.3f;
    }

    // Feed reward back to route learner if episode_id provided
    uint64_t episode_id = params.value("episode_id", (uint64_t)0);
    if (episode_id > 0 && reward != 0.0f) {
        field_store_->route_feedback(episode_id, reward);
    }

    return ToolResult::ok("Outcome recorded for memory #" + memory_str,
        {{"memory_id", memory_str}, {"outcome", outcome}, {"reward", reward}});
}

ToolResult FieldRpcHandler::tool_log_exposure(const json& params) {
    std::string session_id = params.value("session_id", "");
    std::string turn_id    = params.value("turn_id", "");
    std::string hook_type  = params.value("hook_type", "");

    json payload;
    if (params.contains("memory_ids"))     payload["memory_ids"]      = params["memory_ids"];
    if (params.contains("ranks"))          payload["ranks"]            = params["ranks"];
    if (params.contains("resonance_scores")) payload["resonance_scores"] = params["resonance_scores"];
    payload["turn_id"] = turn_id;

    field_store_->emit_event("exposure", hook_type, session_id, payload.dump());

    return ToolResult::ok("Exposure logged for session " + session_id,
        {{"session_id", session_id}, {"hook_type", hook_type}});
}

ToolResult FieldRpcHandler::tool_get_sus_metrics(const json&) {
    return ToolResult::ok("SUS metrics (chitta-field stub)", {
        {"exposures", 0},
        {"note",      "SUS metrics not tracked in chitta-field"},
    });
}
} // namespace chitta
