// field_misc.profile_goal — bodies extracted from field_misc.cpp by theme.
// Declarations remain in chitta/include/chitta/rpc/handlers/field_misc.hpp.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

ToolResult FieldRpcHandler::tool_profile_get(const json& params) {
    std::string user_id = params.value("user_id", "default");
    auto hits = field_store_->recall_keyword("profile " + user_id, 10);
    json profile_entries = hits_to_results_json(hits);
    return ToolResult::ok("Profile entries for " + user_id,
        {{"user_id", user_id}, {"entries", profile_entries}});
}

ToolResult FieldRpcHandler::tool_profile_update(const json& params) {
    std::string user_id = params.value("user_id", "default");
    std::string field   = params.value("field", "");
    std::string value   = params.value("value", "");

    if (field.empty()) return ToolResult::error("field is required");

    std::string content = "profile:" + field + "=" + value;
    std::vector<float> embedding;
    if (params.contains("_preembedding")) {
        embedding = params["_preembedding"].get<std::vector<float>>();
    } else {
        embedding = embed_text("profile " + user_id + " " + field + " " + value);
    }
    uint64_t id = field_store_->remember("profile", "brahman", content, embedding, 0.9f, 0.0f);

    field_store_->add_triplet("profile:" + user_id, "has_" + field, value);

    return ToolResult::ok("Profile updated for " + user_id,
        {{"id", std::to_string(id)}, {"user_id", user_id}, {"field", field}, {"value", value}});
}

ToolResult FieldRpcHandler::tool_profile_observe(const json& params) {
    std::string observation_type = params.value("observation_type", "");
    std::string value            = params.value("value", "");
    std::string user_id          = params.value("user_id", "default");

    std::string content = "observation[" + observation_type + "] for " + user_id + ": " + value;
    std::vector<float> embedding;
    if (params.contains("_preembedding")) {
        embedding = params["_preembedding"].get<std::vector<float>>();
    } else {
        embedding = embed_text(content);
    }
    uint64_t id = field_store_->remember("observation", "brahman", content, embedding, 0.7f, 0.01f);

    return ToolResult::ok("Observation recorded #" + std::to_string(id),
        {{"id", std::to_string(id)}});
}

ToolResult FieldRpcHandler::tool_goal_set(const json& params) {
    std::string title       = params.value("title", "");
    std::string description = params.value("description", "");
    std::string deadline    = params.value("deadline", "");
    std::string realm       = params.value("realm", "brahman");

    if (title.empty()) return ToolResult::error("title is required");

    std::string content = title;
    if (!description.empty()) content += ": " + description;
    if (!deadline.empty())    content += " (by " + deadline + ")";

    if (params.contains("milestones") && params["milestones"].is_array()) {
        content += "\nMilestones:";
        for (const auto& m : params["milestones"]) {
            if (m.is_string()) content += "\n- " + m.get<std::string>();
        }
    }

    std::vector<float> embedding;
    if (params.contains("_preembedding")) {
        embedding = params["_preembedding"].get<std::vector<float>>();
    } else {
        embedding = embed_text(content);
    }
    uint64_t id = field_store_->remember("goal", realm, content, embedding, 0.9f, 0.0f);

    return ToolResult::ok("Goal set #" + std::to_string(id),
        {{"id", std::to_string(id)}, {"title", title}, {"realm", realm}});
}

ToolResult FieldRpcHandler::tool_goal_get(const json& params) {
    auto [id, id_str] = parse_id(params);
    if (id <= 0) return ToolResult::error("id is required");

    auto hits = field_store_->recall_by_kind("goal", 100);
    for (const auto& h : hits) {
        if (static_cast<int64_t>(h.memory_id) == id) {
            return ToolResult::ok("Goal #" + id_str, {
                {"id",         id_str},
                {"content",    h.content},
                {"confidence", h.confidence},
                {"realm",      h.realm},
            });
        }
    }
    return ToolResult::error("Goal #" + id_str + " not found");
}

ToolResult FieldRpcHandler::tool_goal_list(const json& params) {
    std::string realm = params.value("realm", "");
    size_t limit      = static_cast<size_t>(params.value("limit", 20));

    auto hits = field_store_->recall_by_kind("goal", limit);

    json goals = json::array();
    for (const auto& h : hits) {
        if (!realm.empty() && h.realm != realm) continue;
        goals.push_back({
            {"id",         std::to_string(h.memory_id)},
            {"content",    h.content},
            {"confidence", h.confidence},
            {"realm",      h.realm},
        });
    }

    return ToolResult::ok(std::to_string(goals.size()) + " goal(s)",
        {{"goals", goals}, {"count", goals.size()}});
}

ToolResult FieldRpcHandler::tool_goal_progress(const json& params) {
    auto [id, id_str] = parse_id(params);
    if (id <= 0) return ToolResult::error("id is required");

    std::string progress  = params.value("progress", "");
    std::string milestone = params.value("milestone", "");

    std::string payload = progress;
    if (!milestone.empty()) payload += "; milestone: " + milestone;

    field_store_->emit_event("goal", "progress", id_str, payload);
    field_store_->strengthen(static_cast<uint64_t>(id), 0.05f);

    return ToolResult::ok("Progress recorded for goal #" + id_str,
        {{"id", id_str}, {"progress", progress}});
}

ToolResult FieldRpcHandler::tool_goal_complete(const json& params) {
    auto [id, id_str] = parse_id(params);
    if (id <= 0) return ToolResult::error("id is required");

    std::string outcome = params.value("outcome", "");
    field_store_->emit_event("goal", "complete", id_str, outcome);
    field_store_->strengthen(static_cast<uint64_t>(id), 0.2f);
    field_store_->add_triplet(id_str, "status", "completed");

    return ToolResult::ok("Goal #" + id_str + " completed",
        {{"id", id_str}, {"outcome", outcome}});
}

ToolResult FieldRpcHandler::tool_calibration_record(const json& params) {
    std::string domain = params.value("domain", "");
    bool success       = params.value("success", true);

    if (domain.empty()) return ToolResult::error("domain is required");

    field_store_->emit_event("calibration",
        success ? "success" : "failure", domain, "");

    return ToolResult::ok(
        "Calibration recorded: " + domain + " " + (success ? "success" : "failure"),
        {{"domain", domain}, {"success", success}});
}

ToolResult FieldRpcHandler::tool_calibration_score(const json& params) {
    std::string domain = params.value("domain", "");
    if (domain.empty()) return ToolResult::error("domain is required");

    auto hits = field_store_->recall_keyword("calibration " + domain, 20);

    size_t successes = 0, failures = 0;
    for (const auto& h : hits) {
        if (h.content.find("success") != std::string::npos) successes++;
        else if (h.content.find("failure") != std::string::npos) failures++;
    }
    size_t total = successes + failures;
    float score  = total > 0 ? static_cast<float>(successes) / static_cast<float>(total) : 0.5f;

    json result = {
        {"domain",    domain},
        {"score",     score},
        {"successes", successes},
        {"failures",  failures},
        {"total",     total},
    };
    std::ostringstream ss;
    ss << "Calibration [" << domain << "]: "
       << std::fixed << std::setprecision(1) << (score * 100) << "% ("
       << successes << "/" << total << ")";
    return ToolResult::ok(ss.str(), result);
}

ToolResult FieldRpcHandler::tool_narrative_status(const json& params) {
    std::string session_id = params.value("session_id", get_session_id());
    return ToolResult::ok("Narrative status (chitta-field)", {
        {"session_id", session_id},
        {"mode",       "work"},
        {"segment",    "active"},
        {"note",       "Narrative engine not available in chitta-field backend"},
    });
}

ToolResult FieldRpcHandler::tool_narrative_log(const json& params) {
    std::string session_id = params.value("session_id", get_session_id());
    std::string kind       = params.value("kind", "");
    std::string summary    = params.value("summary", "");

    if (kind.empty() || summary.empty())
        return ToolResult::error("kind and summary are required");

    std::string payload = summary;
    if (params.contains("payload")) payload = params["payload"].dump();

    field_store_->emit_event("narrative", kind, session_id, payload);

    return ToolResult::ok("Event logged, kind: " + kind, {
        {"session_id", session_id},
        {"kind",       kind},
        {"mode",       "work"},
    });
}

ToolResult FieldRpcHandler::tool_narrative_history(const json& params) {
    std::string session_id = params.value("session_id", get_session_id());
    size_t limit           = static_cast<size_t>(params.value("limit", 20));

    auto hits = field_store_->recall_keyword("narrative " + session_id, limit);
    json segments = hits_to_results_json(hits);

    return ToolResult::ok(
        std::to_string(segments.size()) + " narrative event(s) for session " + session_id,
        {{"session_id", session_id}, {"count", segments.size()}, {"segments", segments}});
}
} // namespace chitta
