#pragma once
// Wisdom Homeostasis RPC handlers (Layer 9)
// Living belief tissue — wisdom nodes that earn their place against lived reality.

#include <string>

inline ToolResult tool_enroll_wisdom_lineage(FieldStore* fs, const json& params) {
    json p;
    p["wisdom_candidate_id"] = params.value("wisdom_candidate_id", 0ULL);
    p["claim"]               = params.value("claim", "");
    // Accept either nested envelope object or flat domain/action_types/etc. params
    if (params.contains("envelope")) {
        p["envelope"] = params["envelope"];
    } else {
        json env = json::object();
        env["domain"]         = params.value("domain", "");
        env["action_types"]   = params.value("action_types", json::array());
        env["preconditions"]  = params.value("preconditions", json::array());
        env["source_families"] = params.value("source_families", json::array());
        p["envelope"] = env;
    }
    if (params.contains("seed_episode_ids"))      p["seed_episode_ids"]      = params["seed_episode_ids"];
    if (params.contains("seed_surprise_ids"))     p["seed_surprise_ids"]     = params["seed_surprise_ids"];
    if (params.contains("seed_intervention_ids")) p["seed_intervention_ids"] = params["seed_intervention_ids"];
    if (params.contains("seed_debt_ids"))         p["seed_debt_ids"]         = params["seed_debt_ids"];
    if (params.contains("ancestor_lineage_id"))   p["ancestor_lineage_id"]   = params["ancestor_lineage_id"];
    if (params.contains("derivation_relation"))   p["derivation_relation"]   = params["derivation_relation"];

    auto* raw = cf_enroll_wisdom_lineage(fs->handle(), p.dump().c_str());
    if (!raw) return ToolResult::error("enroll_wisdom_lineage failed");
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    auto id = result.value("lineage_id", 0ULL);
    return ToolResult::ok("Lineage #" + std::to_string(id) + " enrolled", result);
}

inline ToolResult tool_transition_wisdom_lineage(FieldStore* fs, const json& params) {
    auto lineage_id = params.value("lineage_id", 0ULL);
    auto new_state  = static_cast<uint8_t>(params.value("new_state", 0));
    auto reason     = params.value("reason", "manual");
    auto task_id    = params.value("rederive_task_id", 0ULL);

    int rc = cf_transition_wisdom_lineage(
        fs->handle(), lineage_id, new_state, reason.c_str(), task_id);
    if (rc == 1) return ToolResult::ok("Lineage #" + std::to_string(lineage_id) + " transitioned");
    if (rc == 0) return ToolResult::ok("No change — already in that state");
    return ToolResult::error("transition failed");
}

inline ToolResult tool_close_rederive(FieldStore* fs, const json& params) {
    json p;
    p["lineage_id"] = params.value("lineage_id", 0ULL);
    // action: 0=reaffirm, 1=narrow, 2=split, 3=demote
    p["action"] = params.value("action", 3);
    if (params.contains("new_envelope"))    p["new_envelope"]    = params["new_envelope"];
    if (params.contains("fork_claim"))      p["fork_claim"]      = params["fork_claim"];
    if (params.contains("fork_lineage_id")) p["fork_lineage_id"] = params["fork_lineage_id"];

    int rc = cf_close_rederive(fs->handle(), p.dump().c_str());
    if (rc == 0) {
        static const char* actions[] = {"reaffirmed", "narrowed", "split", "demoted"};
        uint8_t a = p["action"].get<uint8_t>();
        std::string label = (a < 4) ? actions[a] : "unknown";
        return ToolResult::ok("Lineage #" + std::to_string(p["lineage_id"].get<uint64_t>())
                              + " " + label);
    }
    return ToolResult::error("close_rederive failed");
}

inline ToolResult tool_query_wisdom_lineages(FieldStore* fs, const json& params) {
    json p;
    if (params.contains("state"))  p["state"]  = params["state"];
    if (params.contains("domain")) p["domain"] = params["domain"];
    p["limit"] = params.value("limit", 50);

    auto* raw = cf_query_wisdom_lineages(fs->handle(), p.dump().c_str());
    if (!raw) return ToolResult::ok("No lineages found", json::array());
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    return ToolResult::ok(std::to_string(result.size()) + " lineages", result);
}

inline ToolResult tool_get_wisdom_lineage(FieldStore* fs, const json& params) {
    auto lineage_id = params.value("lineage_id", 0ULL);
    auto* raw = cf_get_wisdom_lineage(fs->handle(), lineage_id);
    if (!raw) return ToolResult::error("Lineage #" + std::to_string(lineage_id) + " not found");
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    return ToolResult::ok("Lineage #" + std::to_string(lineage_id), result);
}

inline ToolResult tool_wisdom_lineage_stats(FieldStore* fs, const json&) {
    auto* raw = cf_wisdom_lineage_stats(fs->handle());
    if (!raw) return ToolResult::error("wisdom_lineage_stats failed");
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    return ToolResult::ok("Wisdom lineage stats", result);
}

inline ToolResult tool_tick_lineage_staleness(FieldStore* fs, const json&) {
    auto* raw = cf_tick_lineage_staleness(fs->handle());
    if (!raw) return ToolResult::ok("No lineages ticked", json::object());
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    auto count = result.value("count", 0);
    return ToolResult::ok(std::to_string(count) + " lineage(s) transitioned", result);
}

inline ToolResult tool_lineage_expiry_check(FieldStore* fs, const json&) {
    auto* raw = cf_lineage_expiry_check(fs->handle());
    if (!raw) return ToolResult::ok("No expired lineages", json::object());
    std::string s(raw); cf_free_string(raw);
    auto result = json::parse(s, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    auto count = result.value("count", 0);
    return ToolResult::ok(std::to_string(count) + " expired lineage(s)", result);
}
