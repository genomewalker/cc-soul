#pragma once
// Agent Protocol Memory RPC handlers (Layer 8)
// Handlers for task contracts, delegation edges, evidence links,
// pending probes, and completion criteria.

#include <string>

inline ToolResult tool_register_task(FieldStore* fs, const json& params) {
    json p;
    p["session_id"] = params.value("session_id", "");
    p["realm"]      = params.value("realm", "coding");
    p["goal"]       = params.value("goal", "");
    if (params.contains("constraints"))         p["constraints"]         = params["constraints"];
    if (params.contains("acceptance_criteria")) p["acceptance_criteria"] = params["acceptance_criteria"];
    p["priority"] = params.value("priority", 5);
    if (params.contains("parent_task_id")) p["parent_task_id"] = params["parent_task_id"];
    if (params.contains("tags"))           p["tags"]           = params["tags"];
    if (params.contains("deadline_ms"))    p["deadline_ms"]    = params["deadline_ms"];

    auto* raw = cf_register_task(fs->handle(), p.dump().c_str());
    if (!raw) return ToolResult::error("register_task failed");
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    auto id = result.value("task_id", 0ULL);
    return ToolResult::ok("Task #" + std::to_string(id) + " registered", result);
}

inline ToolResult tool_update_task(FieldStore* fs, const json& params) {
    json p;
    p["task_id"] = params.value("task_id", 0ULL);
    p["status"]  = params.value("status", 0);
    if (params.contains("add_intervention_id")) p["add_intervention_id"] = params["add_intervention_id"];
    if (params.contains("add_tag"))             p["add_tag"]             = params["add_tag"];

    int rc = cf_update_task(fs->handle(), p.dump().c_str());
    if (rc == 0) return ToolResult::ok("Task #" + std::to_string(p["task_id"].get<uint64_t>()) + " updated");
    if (rc == 1) return ToolResult::error("Task not found");
    return ToolResult::error("update_task failed");
}

inline ToolResult tool_add_delegation(FieldStore* fs, const json& params) {
    json p;
    p["task_id"]    = params.value("task_id", 0ULL);
    p["from_agent"] = params.value("from_agent", "");
    p["to_agent"]   = params.value("to_agent", "");
    if (params.contains("handoff_note")) p["handoff_note"] = params["handoff_note"];

    auto* raw = cf_add_delegation(fs->handle(), p.dump().c_str());
    if (!raw) return ToolResult::error("add_delegation failed — task not found");
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    auto id = result.value("delegation_id", 0ULL);
    return ToolResult::ok("Delegation #" + std::to_string(id) + " recorded", result);
}

inline ToolResult tool_link_evidence(FieldStore* fs, const json& params) {
    json p;
    p["task_id"]       = params.value("task_id", 0ULL);
    p["memory_id"]     = params.value("memory_id", 0ULL);
    p["produced_by"]   = params.value("produced_by", "");
    p["evidence_kind"] = params.value("evidence_kind", 0);
    p["relevance"]     = params.value("relevance", 1.0);

    auto* raw = cf_link_evidence(fs->handle(), p.dump().c_str());
    if (!raw) return ToolResult::error("link_evidence failed — task not found");
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    auto id = result.value("evidence_id", 0ULL);
    return ToolResult::ok("Evidence #" + std::to_string(id) + " linked", result);
}

inline ToolResult tool_add_probe(FieldStore* fs, const json& params) {
    json p;
    p["task_id"]  = params.value("task_id", 0ULL);
    p["question"] = params.value("question", "");
    if (params.contains("expected_answerer")) p["expected_answerer"] = params["expected_answerer"];
    p["priority"] = params.value("priority", 5);

    auto* raw = cf_add_probe(fs->handle(), p.dump().c_str());
    if (!raw) return ToolResult::error("add_probe failed — task not found");
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    auto id = result.value("probe_id", 0ULL);
    return ToolResult::ok("Probe #" + std::to_string(id) + " added", result);
}

inline ToolResult tool_resolve_probe(FieldStore* fs, const json& params) {
    json p;
    p["probe_id"] = params.value("probe_id", 0ULL);
    p["status"]   = params.value("status", 1);
    if (params.contains("answer")) p["answer"] = params["answer"];

    int rc = cf_resolve_probe(fs->handle(), p.dump().c_str());
    if (rc == 0) return ToolResult::ok("Probe #" + std::to_string(p["probe_id"].get<uint64_t>()) + " resolved");
    if (rc == 1) return ToolResult::error("Probe not found");
    return ToolResult::error("resolve_probe failed");
}

inline ToolResult tool_set_criterion(FieldStore* fs, const json& params) {
    json p;
    p["task_id"]   = params.value("task_id", 0ULL);
    p["criterion"] = params.value("criterion", "");
    p["is_met"]    = params.value("is_met", false);
    if (params.contains("evidence_note")) p["evidence_note"] = params["evidence_note"];

    auto* raw = cf_set_criterion(fs->handle(), p.dump().c_str());
    if (!raw) return ToolResult::error("set_criterion failed — task not found");
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    auto id = result.value("criterion_id", 0ULL);
    return ToolResult::ok("Criterion #" + std::to_string(id) + " set", result);
}

inline ToolResult tool_get_task(FieldStore* fs, const json& params) {
    auto id = params.value("task_id", 0ULL);
    auto* raw = cf_get_task(fs->handle(), id);
    if (!raw) return ToolResult::error("Task #" + std::to_string(id) + " not found");
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    return ToolResult::ok("Task #" + std::to_string(id), result);
}

inline ToolResult tool_query_tasks(FieldStore* fs, const json& params) {
    json p;
    if (params.contains("realm"))      p["realm"]      = params["realm"];
    if (params.contains("session_id")) p["session_id"] = params["session_id"];
    if (params.contains("status"))     p["status"]     = params["status"];
    if (params.contains("tag"))        p["tag"]        = params["tag"];
    p["limit"] = params.value("limit", 50);

    auto* raw = cf_query_tasks(fs->handle(), p.dump().c_str());
    if (!raw) return ToolResult::ok("[]", json::array());
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    return ToolResult::ok(std::to_string(result.size()) + " tasks", result);
}

inline ToolResult tool_agent_protocol_stats(FieldStore* fs, const json&) {
    auto* raw = cf_agent_protocol_stats(fs->handle());
    if (!raw) return ToolResult::error("agent_protocol_stats failed");
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    return ToolResult::ok("Agent protocol stats", result);
}
