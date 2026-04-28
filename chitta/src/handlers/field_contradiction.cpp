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
        std::string snippet = content.substr(0, 200);
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
    if (embedding.empty()) return ToolResult::error("Failed to embed query");

    auto hits = field_store_->recall(embedding, limit, realm);

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
            {"snippet", h.content.substr(0, 200)},
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

} // namespace chitta
