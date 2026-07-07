// field_contradiction RPC handlers — bodies for declarations in
// chitta/include/chitta/rpc/handlers/field_contradiction.hpp.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

ToolResult FieldRpcHandler::tool_why_active(const json& params) {
    std::string id_str = params.value("id", "");
    if (id_str.empty()) return ToolResult::error("id is required");

    uint64_t id = 0;
    try { id = std::stoull(id_str); } catch (...) {
        return ToolResult::error("invalid id");
    }

    std::string meta_json = field_store_->get_memory_metadata(id);
    if (meta_json.empty()) return ToolResult::error("memory not found");

    auto meta = json::parse(meta_json, nullptr, false);
    if (meta.is_discarded()) return ToolResult::error("failed to parse metadata");

    std::string status = meta.value("status", "Active");
    std::string epistemic = meta.value("epistemic_status", "UserStated");
    float confidence = meta.value("confidence", 0.0f);
    float strength = meta.value("strength", 0.0f);

    auto conflicts = field_store_->get_conflicts(id);
    auto confirmations = field_store_->get_confirmations(id);

    std::ostringstream ss;
    ss << "Memory #" << id << ": status=" << status
       << ", epistemic=" << epistemic
       << ", confidence=" << confidence
       << ", strength=" << strength << ".\n";

    if (!confirmations.empty()) {
        ss << "Confirmed by: [";
        for (size_t i = 0; i < confirmations.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << "#" << confirmations[i];
        }
        ss << "].\n";
    } else {
        ss << "No confirmations.\n";
    }

    if (!conflicts.empty()) {
        ss << "Contradictions: [";
        for (size_t i = 0; i < conflicts.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << "#" << conflicts[i];
        }
        ss << "].\n";
    } else {
        ss << "No contradictions.\n";
    }

    json data = {
        {"id", id_str},
        {"status", status},
        {"epistemic_status", epistemic},
        {"confidence", confidence},
        {"strength", strength},
        {"conflicts", conflicts},
        {"confirmations", confirmations},
    };
    return ToolResult::ok(ss.str(), data);
}

ToolResult FieldRpcHandler::tool_what_superseded(const json& params) {
    std::string id_str = params.value("id", "");
    if (id_str.empty()) return ToolResult::error("id is required");

    uint64_t id = 0;
    try { id = std::stoull(id_str); } catch (...) {
        return ToolResult::error("invalid id");
    }

    auto chain = field_store_->get_supersession_chain(id);

    std::ostringstream ss;
    ss << "Supersession chain for #" << id << " (" << chain.size() << " entries):\n";

    json chain_json = json::array();
    for (size_t i = 0; i < chain.size(); ++i) {
        std::string content = field_store_->get_content(chain[i]);
        std::string snippet = utf8_trunc(content, 200);
        ss << "  " << (i + 1) << ". #" << chain[i] << ": " << snippet;
        if (content.size() > 200) ss << "...";
        ss << "\n";

        chain_json.push_back({
            {"id", std::to_string(chain[i])},
            {"position", i},
            {"snippet", snippet},
        });
    }

    return ToolResult::ok(ss.str(), {{"chain", chain_json}});
}

ToolResult FieldRpcHandler::tool_show_conflicts(const json& params) {
    std::string query = params.value("query", "");
    if (query.empty()) return ToolResult::error("query is required");

    size_t limit = static_cast<size_t>(params.value("limit", 20));
    std::string realm = params.value("realm", "");

    auto embedding = embed_query(query);

    std::vector<FieldRecallHit> hits;
    if (!embedding.empty()) {
        hits = field_store_->recall(embedding, limit, realm);
    } else {
        hits = field_store_->recall_keyword(query, limit);
    }

    json groups = json::array();
    std::ostringstream ss;
    ss << "Conflict scan for " << hits.size() << " memories:\n";

    size_t conflict_count = 0;
    for (const auto& h : hits) {
        auto conflicts = field_store_->get_conflicts(h.memory_id);
        if (conflicts.empty()) continue;
        conflict_count++;

        json group = {
            {"id", std::to_string(h.memory_id)},
            {"snippet", utf8_trunc(h.content, 200)},
            {"conflicts", json::array()},
        };

        ss << "\n#" << h.memory_id << ": " << h.content.substr(0, 120) << "\n";
        ss << "  conflicts with: ";
        for (size_t i = 0; i < conflicts.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << "#" << conflicts[i];
            std::string c_content = field_store_->get_content(conflicts[i]);
            group["conflicts"].push_back({
                {"id", std::to_string(conflicts[i])},
                {"snippet", c_content.substr(0, 200)},
            });
        }
        ss << "\n";
        groups.push_back(std::move(group));
    }

    if (conflict_count == 0) {
        ss << "No contradictions found among recalled memories.\n";
    }

    return ToolResult::ok(ss.str(), {{"groups", groups}, {"conflict_count", conflict_count}});
}

ToolResult FieldRpcHandler::tool_detect_contradictions(const json& params) {
    // memory_id can arrive as string (CLI) or integer (MCP)
    uint64_t memory_id = 0;
    if (params.contains("memory_id")) {
        auto& v = params["memory_id"];
        if (v.is_string()) {
            try { memory_id = std::stoull(v.get<std::string>()); } catch (...) {}
        } else if (v.is_number_unsigned()) {
            memory_id = v.get<uint64_t>();
        } else if (v.is_number_integer()) {
            memory_id = static_cast<uint64_t>(v.get<int64_t>());
        }
    }
    if (memory_id == 0) return ToolResult::error("memory_id is required");
    std::string realm = params.value("realm", "");

    std::string candidates_json = field_store_->detect_contradictions(memory_id, realm);
    auto candidates = json::parse(candidates_json, nullptr, false);
    if (candidates.is_discarded() || !candidates.is_array() || candidates.empty()) {
        return ToolResult::ok("No contradictions detected.", {{"candidates", json::array()}});
    }

    std::ostringstream ss;
    ss << "Detected " << candidates.size() << " contradiction(s) for memory #" << memory_id << ":\n";
    for (auto& c : candidates) {
        ss << "  score=" << c.value("score", 0.0f)
           << "  #" << c.value("memory_a", 0UL)
           << " vs #" << c.value("memory_b", 0UL)
           << "  reason: " << c.value("reason", "") << "\n";
    }
    return ToolResult::ok(ss.str(), {{"candidates", candidates}});
}

ToolResult FieldRpcHandler::tool_scan_contradictions(const json& params) {
    std::string realm = params.value("realm", "");
    uint32_t limit = static_cast<uint32_t>(params.value("limit", 50));

    std::string candidates_json = field_store_->scan_contradictions(realm, limit);
    auto candidates = json::parse(candidates_json, nullptr, false);
    if (candidates.is_discarded()) candidates = json::array();

    std::ostringstream ss;
    ss << "Scanned realm '" << realm << "': found " << candidates.size() << " contradiction candidate(s).\n";
    for (auto& c : candidates) {
        ss << "  [" << c.value("score", 0.0f) << "] #" << c.value("memory_a", 0UL)
           << " vs #" << c.value("memory_b", 0UL)
           << " — " << c.value("reason", "") << "\n";
    }
    return ToolResult::ok(ss.str(), {{"candidates", candidates}, {"count", candidates.size()}});
}

ToolResult FieldRpcHandler::tool_resolve_contradiction(const json& params) {
    std::string winner_str = params.value("winner_id", "");
    std::string loser_str  = params.value("loser_id", "");
    if (winner_str.empty() || loser_str.empty())
        return ToolResult::error("winner_id and loser_id are required");

    uint64_t winner_id = std::stoull(winner_str);
    uint64_t loser_id  = std::stoull(loser_str);
    std::string reason = params.value("reason", "manual resolution");

    std::string ops_json = field_store_->resolve_contradiction(winner_id, loser_id, reason);
    auto ops = json::parse(ops_json, nullptr, false);
    if (ops.is_discarded() || ops.empty()) return ToolResult::error("resolve failed");

    // Apply triplets
    if (ops.contains("add_triplets") && ops["add_triplets"].is_array()) {
        for (auto& t : ops["add_triplets"]) {
            std::string subj = t[0].get<std::string>();
            std::string pred = t[1].get<std::string>();
            std::string obj  = t[2].get<std::string>();
            field_store_->add_triplet(subj, pred, obj);
        }
    }

    // Demote loser confidence (delta toward ~0.05)
    if (ops.contains("demote_memory_id")) {
        uint64_t demote_id = ops["demote_memory_id"].get<uint64_t>();
        field_store_->update_confidence(demote_id, -0.90f);
    }

    // Store CORRECTION memory
    if (ops.contains("correction_content")) {
        std::string correction = ops["correction_content"].get<std::string>();
        // Get realm from winner metadata
        std::string realm;
        std::string meta_json = field_store_->get_memory_metadata(winner_id);
        if (!meta_json.empty()) {
            auto meta = json::parse(meta_json, nullptr, false);
            if (!meta.is_discarded()) realm = meta.value("realm", "");
        }
        auto emb = embed_query(correction);
        if (!emb.empty()) {
            field_store_->remember("correction", realm, correction, emb, 0.95f, 0.01f);
        }
    }

    std::ostringstream ss;
    ss << "Resolved: #" << winner_id << " supersedes #" << loser_id << ". Reason: " << reason;
    return ToolResult::ok(ss.str(), {{"ops", ops}});
}

ToolResult FieldRpcHandler::tool_cross_harness_conflicts(const json& params) {
    std::string realm     = params.value("realm", "");
    int limit             = params.value("limit", 20);
    double min_score      = params.value("min_disagreement_score", 0.3);

    std::string raw = field_store_->query_cross_harness_conflicts(
        realm, static_cast<uint32_t>(limit), static_cast<float>(min_score));

    try {
        auto arr = json::parse(raw);
        return ToolResult::ok(
            json{{"conflicts", arr}, {"total", arr.size()}, {"ok", true}}.dump()
        );
    } catch (...) {
        return ToolResult::ok(raw);
    }
}

} // namespace chitta
