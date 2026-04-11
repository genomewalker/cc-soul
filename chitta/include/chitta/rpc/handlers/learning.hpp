#pragma once
// Autonomous Learning RPC handlers (Moves 1-6)
//
// Handlers for surprise credit tracking, wisdom promotion,
// debt evidence, and learned scorer calibration.

// Included from field_handler.hpp — FieldStore, ToolResult, json already available.
#include <string>

// ── Surprise Learning (Moves 1-2) ────────────────────────────────────────

inline ToolResult tool_surprise_learning_stats(FieldStore* fs, const json&) {
    auto* raw = cf_surprise_learning_stats(fs->handle());
    if (!raw) return ToolResult::error("surprise_learning_stats failed");
    std::string result_str(raw);
    cf_free_string(raw);
    auto result = json::parse(result_str, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");

    std::string msg = "Surprise learning: " +
        std::to_string(result.value("tracked_memories", 0)) + " tracked memories, " +
        std::to_string(result.value("total_gates_passed", 0)) + " gates passed";
    return ToolResult::ok(msg, result);
}

// ── Wisdom Promotion (Move 5) ────────────────────────────────────────────

inline ToolResult tool_upsert_wisdom_candidate(FieldStore* fs, const json& params) {
    json p;
    p["cluster_key"] = params.value("cluster_key", "");
    p["domain"] = params.value("domain", "");
    p["action"] = params.value("action", "");
    p["summary"] = params.value("summary", "");
    if (params.contains("episode_ids")) p["episode_ids"] = params["episode_ids"];
    if (params.contains("debt_ids")) p["debt_ids"] = params["debt_ids"];
    p["support_count"] = params.value("support_count", 0);
    p["cross_session_count"] = params.value("cross_session_count", 0);
    p["mean_surprise"] = params.value("mean_surprise", 0.0);
    p["promotion_score"] = params.value("promotion_score", 0.0);

    if (p["cluster_key"].get<std::string>().empty())
        return ToolResult::error("cluster_key is required");

    auto* raw = cf_upsert_wisdom_candidate(fs->handle(), p.dump().c_str());
    if (!raw) return ToolResult::error("upsert_wisdom_candidate failed");
    std::string result_str(raw);
    cf_free_string(raw);
    auto result = json::parse(result_str, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");

    return ToolResult::ok(
        "Wisdom candidate #" + std::to_string(result.value("candidate_id", 0)),
        result);
}

inline ToolResult tool_update_wisdom_lifecycle(FieldStore* fs, const json& params) {
    uint64_t candidate_id = params.value("candidate_id", uint64_t(0));
    uint8_t new_state = params.value("new_state", uint8_t(0));
    if (candidate_id == 0)
        return ToolResult::error("candidate_id is required");

    int r = cf_update_wisdom_lifecycle(fs->handle(), candidate_id, new_state);
    if (r != 0) return ToolResult::error("candidate not found or update failed");
    static const char* state_names[] = {"candidate", "provisional", "trusted", "demoted"};
    std::string state_name = (new_state < 4) ? state_names[new_state] : "unknown";
    return ToolResult::ok("Candidate #" + std::to_string(candidate_id) + " → " + state_name);
}

inline ToolResult tool_query_wisdom_candidates(FieldStore* fs, const json& params) {
    json p;
    if (params.contains("lifecycle")) p["lifecycle"] = params["lifecycle"];
    if (params.contains("domain")) p["domain"] = params["domain"];
    p["limit"] = params.value("limit", 50);

    auto* raw = cf_query_wisdom_candidates(fs->handle(), p.dump().c_str());
    if (!raw) return ToolResult::ok("No wisdom candidates", json::array());
    std::string result_str(raw);
    cf_free_string(raw);
    auto results = json::parse(result_str, nullptr, false);
    if (results.is_discarded()) results = json::array();

    json wrapped = {{"items", results}, {"count", results.size()}};
    return ToolResult::ok(
        std::to_string(results.size()) + " wisdom candidate(s)",
        wrapped);
}

inline ToolResult tool_wisdom_promotion_stats(FieldStore* fs, const json&) {
    auto* raw = cf_wisdom_promotion_stats(fs->handle());
    if (!raw) return ToolResult::error("wisdom_promotion_stats failed");
    std::string result_str(raw);
    cf_free_string(raw);
    auto result = json::parse(result_str, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    return ToolResult::ok(
        std::to_string(result.value("total_candidates", 0)) + " total candidates",
        result);
}

// ── Debt Evidence (Move 3) ──────────────────────────────────��────────────

inline ToolResult tool_attach_debt_evidence(FieldStore* fs, const json& params) {
    uint64_t debt_id = params.value("debt_id", uint64_t(0));
    if (debt_id == 0)
        return ToolResult::error("debt_id is required");

    json p;
    if (params.contains("memory_ids")) p["memory_ids"] = params["memory_ids"];
    p["confidence"] = params.value("confidence", 0.5);
    if (params.contains("note")) p["note"] = params["note"];

    int r = cf_attach_debt_evidence(fs->handle(), debt_id, p.dump().c_str());
    if (r != 0) return ToolResult::error("debt not found or attach failed");
    return ToolResult::ok("Evidence attached to debt #" + std::to_string(debt_id));
}

// ── Learned Scorer (Move 6) ──────────────────────────────────────────────

inline ToolResult tool_update_scorer_model(FieldStore* fs, const json& params) {
    json p;
    if (params.contains("weights")) p["weights"] = params["weights"];
    p["model_version"] = params.value("model_version", 0);
    p["mean_loss"] = params.value("mean_loss", 0.0);
    p["outcome_count"] = params.value("outcome_count", 0);

    int r = cf_update_scorer_model(fs->handle(), p.dump().c_str());
    if (r != 0) return ToolResult::error("scorer model update failed");
    return ToolResult::ok("Scorer model updated to v" + std::to_string(params.value("model_version", 0)));
}

inline ToolResult tool_learned_scorer_stats(FieldStore* fs, const json&) {
    auto* raw = cf_learned_scorer_stats(fs->handle());
    if (!raw) return ToolResult::error("learned_scorer_stats failed");
    std::string result_str(raw);
    cf_free_string(raw);
    auto result = json::parse(result_str, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    return ToolResult::ok(
        "Scorer model v" + std::to_string(result.value("model_version", 0)) +
        ", " + std::to_string(result.value("factor_count", 0)) + " learned factors",
        result);
}

inline ToolResult tool_effective_scorer_weights(FieldStore* fs, const json&) {
    auto* raw = cf_effective_scorer_weights(fs->handle());
    if (!raw) return ToolResult::error("effective_scorer_weights failed");
    std::string result_str(raw);
    cf_free_string(raw);
    auto result = json::parse(result_str, nullptr, false);
    if (result.is_discarded()) return ToolResult::error("parse error");
    return ToolResult::ok("Effective scorer weights", result);
}
