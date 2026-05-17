// field_introspection RPC handler — what_do_i_know_about.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

static std::string str_or(const json& j, const std::string& key, const std::string& def = "") {
    if (!j.contains(key) || j[key].is_null()) return def;
    return j[key].get<std::string>();
}

ToolResult FieldRpcHandler::tool_what_do_i_know_about(const json& params) {
    std::string topic = params.value("topic", "");
    if (topic.empty()) return ToolResult::error("topic is required");

    int k                  = params.value("k", 10);
    std::string realm      = params.value("realm", "");
    double confidence_min  = params.value("confidence_min", 0.0);
    bool include_stale     = params.value("include_stale", true);
    bool include_contradictions = params.value("include_contradictions", true);

    auto emb = embed_query(topic);
    if (emb.empty()) return ToolResult::error("Failed to embed topic");

    auto hits = field_store_->recall(emb, k * 2, realm);

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    json claims = json::array();

    for (const auto& h : hits) {
        if (h.confidence < confidence_min) continue;

        // Provenance + staged info
        std::string claim_info_raw = field_store_->memory_claim_info_json(h.memory_id, now_ms);
        json claim_info;
        try { claim_info = json::parse(claim_info_raw); } catch (...) { claim_info = json::object(); }

        bool staged = claim_info.value("staged", false);
        std::string invalidated_by = str_or(claim_info, "invalidated_by");

        // Staleness
        std::string stale_raw = field_store_->symbol_stale_for_memory_json(h.memory_id);
        json stale_info;
        try { stale_info = json::parse(stale_raw); } catch (...) { stale_info = {{"stale", false}, {"reason", nullptr}}; }

        bool is_stale = stale_info.value("stale", false);
        if (!include_stale && is_stale) continue;

        // Contradictions
        json contradictions = json::array();
        if (include_contradictions) {
            auto conflicts = field_store_->get_conflicts(h.memory_id);
            for (uint64_t peer_id : conflicts) {
                std::string resolution = "conflict";
                auto chain = field_store_->get_supersession_chain(peer_id);
                if (!chain.empty()) resolution = "superseded";
                contradictions.push_back({
                    {"peer_id", peer_id},
                    {"resolution", resolution},
                });
            }
        }

        std::string stale_reason = str_or(stale_info, "reason");
        json claim = {
            {"memory_id", h.memory_id},
            {"content", h.content},
            {"confidence", h.confidence},
            {"strength", h.strength},
            {"score", h.score},
            {"staged", staged},
            {"provenance", {
                {"session",    str_or(claim_info, "source_session")},
                {"tool",       str_or(claim_info, "source_tool")},
                {"age_days",   claim_info.value("age_days", 0.0)},
                {"created_at_ms", claim_info.value("created_at_ms", int64_t(0))},
            }},
            {"staleness", {
                {"stale", is_stale},
                {"reason", stale_reason.empty() ? json(nullptr) : json(stale_reason)},
            }},
            {"contradictions", contradictions},
        };

        if (!invalidated_by.empty()) claim["invalidated_by"] = invalidated_by;

        claims.push_back(std::move(claim));
        if ((int)claims.size() >= k) break;
    }

    // Cross-harness conflicts scoped to the recalled cluster
    json cross_harness = json::array();
    if (!claims.empty()) {
        std::string ch_raw = field_store_->query_cross_harness_conflicts(realm, 10, 0.3f);
        try {
            auto ch_arr = json::parse(ch_raw);
            // Keep only pairs where at least one memory_id is in our claims set
            std::unordered_set<uint64_t> claim_ids;
            for (const auto& c : claims) claim_ids.insert(c["memory_id"].get<uint64_t>());
            for (auto& pair : ch_arr) {
                uint64_t a = pair.value("memory_a_id", uint64_t(0));
                uint64_t b = pair.value("memory_b_id", uint64_t(0));
                if (claim_ids.count(a) || claim_ids.count(b))
                    cross_harness.push_back(std::move(pair));
            }
        } catch (...) {}
    }

    return ToolResult::ok(
        json{{"claims", claims}, {"cross_harness_conflicts", cross_harness},
             {"topic", topic}, {"total", claims.size()}, {"ok", true}}.dump()
    );
}

} // namespace chitta
