// intervention RPC handlers — bodies for declarations in
// chitta/include/chitta/rpc/handlers/intervention.hpp.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

ToolResult FieldRpcHandler::tool_start_intervention(FieldStore* fs, const json& params) {
    json p;
    p["realm"]              = params.value("realm", "coding");
    p["session_id"]         = params.value("session_id", "");
    if (params.contains("task_id")) p["task_id"] = params["task_id"];
    p["agent_id"]           = params.value("agent_id", "");
    p["domain"]             = params.value("domain", "");
    p["intent"]             = params.value("intent", "");
    p["action_type"]        = params.value("action_type", 0);
    p["action_ref"]         = params.value("action_ref", "");
    if (params.contains("preconditions"))       p["preconditions"]       = params["preconditions"];
    if (params.contains("expected_observables"))p["expected_observables"]= params["expected_observables"];
    p["reversal_cost"]      = params.value("reversal_cost", 0);

    auto* raw = cf_start_intervention(fs->handle(), p.dump().c_str());
    if (!raw) return ToolResult::error("start_intervention failed");
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    auto id = result.value("intervention_id", 0ULL);
    return ToolResult::ok("Intervention #" + std::to_string(id) + " started", result);
}

ToolResult FieldRpcHandler::tool_add_observation(FieldStore* fs, const json& params) {
    json p;
    p["intervention_id"] = params.value("intervention_id", 0ULL);
    p["kind"]            = params.value("kind", 0);
    p["summary"]         = params.value("summary", "");
    p["confidence"]      = params.value("confidence", 1.0);
    if (params.contains("evidence_refs")) p["evidence_refs"] = params["evidence_refs"];

    auto* raw = cf_add_observation(fs->handle(), p.dump().c_str());
    if (!raw) return ToolResult::error("add_observation failed — intervention not found");
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    auto id = result.value("observation_id", 0ULL);
    return ToolResult::ok("Observation #" + std::to_string(id) + " recorded", result);
}

ToolResult FieldRpcHandler::tool_close_intervention(FieldStore* fs, const json& params) {
    auto intervention_id = params.value("intervention_id", 0ULL);
    auto status          = static_cast<uint8_t>(params.value("status", 2));
    int rc = cf_close_intervention(fs->handle(), intervention_id, status);
    if (rc == 0) return ToolResult::ok("Intervention #" + std::to_string(intervention_id) + " closed");
    if (rc == 1) return ToolResult::error("Intervention not found");
    return ToolResult::error("close_intervention failed");
}

ToolResult FieldRpcHandler::tool_record_attribution(FieldStore* fs, const json& params) {
    json p;
    p["intervention_id"]   = params.value("intervention_id", 0ULL);
    p["primary_class"]     = params.value("primary_class", 9);
    if (params.contains("secondary_class")) p["secondary_class"] = params["secondary_class"];
    p["confidence_delta"]  = params.value("confidence_delta", 0.5);
    if (params.contains("surprise_id"))       p["surprise_id"]       = params["surprise_id"];
    if (params.contains("debt_ids"))          p["debt_ids"]          = params["debt_ids"];
    if (params.contains("source_memory_ids")) p["source_memory_ids"] = params["source_memory_ids"];
    if (params.contains("skill_memory_ids"))  p["skill_memory_ids"]  = params["skill_memory_ids"];
    if (params.contains("note"))              p["note"]              = params["note"];

    int rc = cf_record_attribution(fs->handle(), p.dump().c_str());
    if (rc == 0) return ToolResult::ok("Attribution recorded and routed");
    if (rc == 1) return ToolResult::error("Intervention not found");
    return ToolResult::error("record_attribution failed");
}

ToolResult FieldRpcHandler::tool_query_interventions(FieldStore* fs, const json& params) {
    json p;
    if (params.contains("realm"))      p["realm"]      = params["realm"];
    if (params.contains("session_id")) p["session_id"] = params["session_id"];
    if (params.contains("status"))     p["status"]     = params["status"];
    p["limit"] = params.value("limit", 50);

    auto* raw = cf_query_interventions(fs->handle(), p.dump().c_str());
    if (!raw) return ToolResult::ok("[]", json::array());
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    return ToolResult::ok(std::to_string(result.size()) + " interventions", result);
}

ToolResult FieldRpcHandler::tool_get_intervention(FieldStore* fs, const json& params) {
    auto id = params.value("intervention_id", 0ULL);
    auto* raw = cf_get_intervention(fs->handle(), id);
    if (!raw) return ToolResult::error("Intervention #" + std::to_string(id) + " not found");
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    return ToolResult::ok("Intervention #" + std::to_string(id), result);
}

ToolResult FieldRpcHandler::tool_intervention_stats(FieldStore* fs, const json&) {
    auto* raw = cf_intervention_stats(fs->handle());
    if (!raw) return ToolResult::error("intervention_stats failed");
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    return ToolResult::ok("Intervention ledger stats", result);
}

ToolResult FieldRpcHandler::tool_list_open_interventions(FieldStore* fs, const json&) {
    auto* raw = cf_list_open_interventions(fs->handle());
    if (!raw) return ToolResult::ok("[]", json::array());
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    return ToolResult::ok(std::to_string(result.size()) + " open interventions", result);
}

} // namespace chitta
