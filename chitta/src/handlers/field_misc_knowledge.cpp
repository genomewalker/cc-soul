// field_misc.knowledge — bodies extracted from field_misc.cpp by theme.
// Declarations remain in chitta/include/chitta/rpc/handlers/field_misc.hpp.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

ToolResult FieldRpcHandler::tool_episode_cluster_status(const json& params) {
    float similarity_threshold = params.value("similarity_threshold", 0.8f);
    int min_occurrences        = params.value("min_occurrences", 2);

    return ToolResult::ok("Episode cluster status (chitta-field stub)", {
        {"clusters",            0},
        {"similarity_threshold", similarity_threshold},
        {"min_occurrences",      min_occurrences},
        {"note",                 "Episode clustering not available in chitta-field backend"},
    });
}

ToolResult FieldRpcHandler::tool_insight_promote(const json& params) {
    auto [id, id_str] = parse_id(params);
    if (id <= 0) return ToolResult::error("id is required");
    if (field_store_->get_content(static_cast<uint64_t>(id)).empty()) {
        return ToolResult::error("Insight #" + id_str + " not found");
    }

    std::string reason = params.value("reason", "");
    field_store_->strengthen(static_cast<uint64_t>(id), 0.5f);
    field_store_->add_triplet(id_str, "promoted", "global");
    if (!reason.empty())
        field_store_->add_triplet(id_str, "promotion_reason", reason);

    return ToolResult::ok("Insight #" + id_str + " promoted to global",
        {{"id", id_str}, {"reason", reason}});
}

ToolResult FieldRpcHandler::tool_insight_global(const json& params) {
    size_t limit   = static_cast<size_t>(params.value("limit", 20));
    std::string tag = params.value("tag", "");

    auto hits = field_store_->recall_by_kind("insight", limit);

    json insights = json::array();
    for (const auto& h : hits) {
        // Filter by tag triplet if requested (approximate: check content)
        if (!tag.empty() && h.content.find(tag) == std::string::npos) continue;
        insights.push_back({
            {"id",         std::to_string(h.memory_id)},
            {"content",    h.content},
            {"confidence", h.confidence},
            {"realm",      h.realm},
        });
    }

    return ToolResult::ok(std::to_string(insights.size()) + " global insight(s)",
        {{"insights", insights}, {"count", insights.size()}});
}

ToolResult FieldRpcHandler::tool_list_by_aspect(const json& params) {
    std::string aspect = params.value("aspect", "");
    size_t limit       = static_cast<size_t>(params.value("limit", 20));
    float min_confidence = params.value("min_confidence", 0.0f);

    if (aspect.empty()) return ToolResult::error("aspect is required");

    auto it = ASPECT_KINDS.find(aspect);
    if (it == ASPECT_KINDS.end())
        return ToolResult::error("Unknown aspect: " + aspect);

    json results = json::array();
    for (const auto& kind : it->second) {
        auto hits = field_store_->recall_by_kind(kind, limit);
        for (const auto& h : hits) {
            if (h.confidence < min_confidence) continue;
            results.push_back({
                {"id",         std::to_string(h.memory_id)},
                {"kind",       h.kind},
                {"content",    h.content},
                {"confidence", h.confidence},
                {"realm",      h.realm},
            });
        }
    }

    // Sort by confidence descending
    std::sort(results.begin(), results.end(), [](const json& a, const json& b) {
        return a.value("confidence", 0.0f) > b.value("confidence", 0.0f);
    });
    if (results.size() > limit)
        results.erase(results.begin() + static_cast<int>(limit), results.end());

    return ToolResult::ok(
        std::to_string(results.size()) + " result(s) for aspect '" + aspect + "'",
        {{"aspect", aspect}, {"results", results}, {"count", results.size()}});
}

ToolResult FieldRpcHandler::tool_list_aspects(const json&) {
    json aspects = json::array();
    for (const auto& [key, _] : ASPECT_KINDS) {
        aspects.push_back(key);
    }
    std::sort(aspects.begin(), aspects.end());
    return ToolResult::ok(std::to_string(aspects.size()) + " aspect(s) available",
        {{"aspects", aspects}});
}

ToolResult FieldRpcHandler::tool_query_claims(const json& params) {
    std::string subject   = params.value("subject", "");
    std::string predicate = params.value("predicate", "");
    size_t limit          = static_cast<size_t>(params.value("limit", 20));

    if (subject.empty()) return ToolResult::error("subject is required");

    std::string triplets_raw = field_store_->query_subject(subject);
    json triplets_json;
    try { triplets_json = json::parse(triplets_raw, nullptr, false); } catch (...) {}
    if (triplets_json.is_discarded() || !triplets_json.is_array()) triplets_json = json::array();

    json claims = json::array();
    for (const auto& t : triplets_json) {
        std::string pred = t.value("predicate", "");
        if (!predicate.empty() && pred != predicate) continue;
        claims.push_back({
            {"subject",   t.value("subject", subject)},
            {"predicate", pred},
            {"object",    t.value("object", "")},
        });
        if (claims.size() >= limit) break;
    }

    return ToolResult::ok(
        std::to_string(claims.size()) + " claim(s) for subject '" + subject + "'",
        {{"claims", claims}, {"count", claims.size()}});
}

ToolResult FieldRpcHandler::tool_get_policies(const json& params) {
    size_t limit = static_cast<size_t>(params.value("limit", 20));
    auto hits    = field_store_->recall_by_kind("policy", limit);
    json policies = hits_to_results_json(hits);
    return ToolResult::ok(std::to_string(policies.size()) + " policy(ies)",
        {{"policies", policies}, {"count", policies.size()}});
}

ToolResult FieldRpcHandler::tool_get_entities(const json& params) {
    std::string type = params.value("type", "");
    size_t limit     = static_cast<size_t>(params.value("limit", 20));

    auto hits = field_store_->recall_by_kind(type.empty() ? "entity" : type, limit);
    json entities = hits_to_results_json(hits);
    return ToolResult::ok(std::to_string(entities.size()) + " entit(y/ies)",
        {{"entities", entities}, {"count", entities.size()}});
}

ToolResult FieldRpcHandler::tool_get_relationship_events(const json& params) {
    std::string event_type = params.value("event_type", "");
    size_t limit           = static_cast<size_t>(params.value("limit", 20));

    std::string search = "relationship " + event_type;
    auto hits = field_store_->recall_keyword(search, limit);
    json events = hits_to_results_json(hits);
    return ToolResult::ok(std::to_string(events.size()) + " relationship event(s)",
        {{"events", events}, {"count", events.size()}});
}
} // namespace chitta
