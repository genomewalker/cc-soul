// field_misc.memory — bodies extracted from field_misc.cpp by theme.
// Declarations remain in chitta/include/chitta/rpc/handlers/field_misc.hpp.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

ToolResult FieldRpcHandler::tool_memory_history(const json& params) {
    auto [id, id_str] = parse_id(params);
    if (id <= 0) return ToolResult::error("id is required");

    auto hits = field_store_->recall_by_kind("episode", 1);
    std::string content;
    for (const auto& h : hits) {
        if (static_cast<int64_t>(h.memory_id) == id) {
            content = h.content;
            break;
        }
    }

    json versions = json::array();
    versions.push_back({{"version", 1}, {"content", content}});

    return ToolResult::ok("Memory #" + id_str + " history (chitta-field)",
        {{"id", id_str}, {"versions", versions},
         {"note", "Version history is append-only in chitta-field"}});
}

ToolResult FieldRpcHandler::tool_memory_revert(const json& params) {
    return ToolResult::error(
        "Version history not available in chitta-field backend");
}

ToolResult FieldRpcHandler::tool_pin_memory(const json& params) {
    auto [id, id_str] = parse_id(params);
    if (id <= 0) return ToolResult::error("id is required");

    std::string reason = params.value("reason", "");
    field_store_->strengthen(static_cast<uint64_t>(id), 0.3f);
    field_store_->add_triplet(id_str, "pinned", "true");
    if (!reason.empty())
        field_store_->add_triplet(id_str, "pin_reason", reason);

    return ToolResult::ok("Memory #" + id_str + " pinned",
        {{"id", id_str}, {"reason", reason}});
}

ToolResult FieldRpcHandler::tool_unpin_memory(const json& params) {
    auto [id, id_str] = parse_id(params);
    if (id <= 0) return ToolResult::error("id is required");

    field_store_->weaken(static_cast<uint64_t>(id), 0.1f);
    field_store_->add_triplet(id_str, "pinned", "false");

    return ToolResult::ok("Memory #" + id_str + " unpinned", {{"id", id_str}});
}

ToolResult FieldRpcHandler::tool_list_pinned(const json& params) {
    std::string realm = params.value("realm", "");
    size_t limit      = static_cast<size_t>(params.value("limit", 20));

    auto hits = field_store_->recall_keyword("pinned", limit);
    json pinned = hits_to_results_json(hits);

    return ToolResult::ok(std::to_string(pinned.size()) + " pinned memory(ies)",
        {{"pinned", pinned}, {"count", pinned.size()}});
}

ToolResult FieldRpcHandler::tool_memory_lock(const json& params) {
    auto [id, id_str] = parse_id(params);
    if (id <= 0) return ToolResult::error("id is required");

    std::string holder_id   = params.value("holder_id", "");
    std::string holder_type = params.value("holder_type", "");
    int duration            = params.value("duration", 0);

    json payload = {
        {"holder_id",   holder_id},
        {"holder_type", holder_type},
        {"duration",    duration},
    };
    field_store_->emit_event("lock", "acquire", id_str, payload.dump());

    return ToolResult::ok("Memory #" + id_str + " locked",
        {{"id", id_str}, {"holder_id", holder_id}, {"status", "locked"}});
}

ToolResult FieldRpcHandler::tool_memory_unlock(const json& params) {
    auto [id, id_str] = parse_id(params);
    if (id <= 0) return ToolResult::error("id is required");

    std::string holder_id = params.value("holder_id", "");
    field_store_->emit_event("lock", "release", id_str, holder_id);

    return ToolResult::ok("Memory #" + id_str + " unlocked",
        {{"id", id_str}, {"status", "unlocked"}});
}

ToolResult FieldRpcHandler::tool_memory_lock_status(const json& params) {
    auto [id, id_str] = parse_id(params);
    if (id <= 0) return ToolResult::error("id is required");

    return ToolResult::ok("Lock status for memory #" + id_str,
        {{"id", id_str}, {"locked", false},
         {"note", "Lock state not persisted in chitta-field backend"}});
}

ToolResult FieldRpcHandler::tool_propose_change(const json& params) {
    auto [id, id_str] = parse_id(params);
    if (id <= 0) return ToolResult::error("id is required");

    std::string content     = params.value("content", "");
    std::string proposed_by = params.value("proposed_by", "");

    if (content.empty()) return ToolResult::error("content is required");

    std::string text = "change to " + id_str + ": " + content;
    if (!proposed_by.empty()) text += " (by " + proposed_by + ")";

    auto embedding = embed_text(text);
    uint64_t proposal_id = field_store_->remember(
        "proposal", "brahman", text, embedding, 0.7f, 0.001f);

    return ToolResult::ok("Proposal #" + std::to_string(proposal_id) + " created",
        {{"merge_id", std::to_string(proposal_id)}, {"target_id", id_str}});
}

ToolResult FieldRpcHandler::tool_list_merge_queue(const json& params) {
    size_t limit = static_cast<size_t>(params.value("limit", 20));
    auto hits = field_store_->recall_by_kind("proposal", limit);
    json queue = hits_to_results_json(hits);
    return ToolResult::ok(std::to_string(queue.size()) + " proposal(s) in queue",
        {{"queue", queue}, {"count", queue.size()}});
}

ToolResult FieldRpcHandler::tool_resolve_merge(const json& params) {
    auto [merge_id, merge_str] = parse_id(params, "merge_id");
    if (merge_id <= 0) return ToolResult::error("merge_id is required");

    std::string status     = params.value("status", "");
    std::string resolution = params.value("resolution", "");

    field_store_->emit_event("proposal", status, merge_str, resolution);

    if (status == "accepted") {
        field_store_->strengthen(static_cast<uint64_t>(merge_id), 0.1f);
    } else if (status == "rejected") {
        field_store_->weaken(static_cast<uint64_t>(merge_id), 0.2f);
    }

    return ToolResult::ok("Proposal #" + merge_str + " " + status,
        {{"merge_id", merge_str}, {"status", status}});
}
} // namespace chitta
