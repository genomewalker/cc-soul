// Skill RPC handlers — bodies for declarations in
// chitta/include/chitta/rpc/handlers/field_skill.hpp.

#include "../../include/chitta/rpc/field_handler.hpp"
#include <chrono>

namespace chitta {

ToolResult FieldRpcHandler::tool_skill_upload(const json& params) {
    if (!field_store_) return ToolResult::error("chitta-field store unavailable");

    std::string skill_id = params.value("skill_id", "");
    std::string content  = params.value("content", "");
    std::string uploaded_by = params.value("uploaded_by", "");
    if (skill_id.empty() || content.empty())
        return ToolResult::error("skill_id and content required");

    json tags_arr = params.value("tags", json::array());
    std::string tags_json = tags_arr.dump();

    int64_t ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    int version = field_store_->skill_upload(skill_id, content, uploaded_by, tags_json, ts_ms);
    if (version < 0) return ToolResult::error("skill upload failed");

    return ToolResult::ok("Skill uploaded", {
        {"skill_id", skill_id},
        {"version", version},
    });
}

ToolResult FieldRpcHandler::tool_skill_read(const json& params) {
    if (!field_store_) return ToolResult::error("chitta-field store unavailable");

    std::string skill_id = params.value("skill_id", "");
    uint32_t version = params.value("version", 0);
    if (skill_id.empty()) return ToolResult::error("skill_id required");

    std::string json_str = field_store_->skill_read(skill_id, version);
    if (json_str.empty()) return ToolResult::error("skill not found");

    json out = json::parse(json_str, nullptr, false);
    if (out.is_discarded()) return ToolResult::error("parse error");
    return ToolResult::ok(json_str, out);
}

ToolResult FieldRpcHandler::tool_skill_list() {
    if (!field_store_) return ToolResult::error("chitta-field store unavailable");

    std::string json_str = field_store_->skill_list();
    json out = json::parse(json_str, nullptr, false);
    if (out.is_discarded()) out = json::array();
    return ToolResult::ok(json_str, out);
}

ToolResult FieldRpcHandler::tool_skill_search(const json& params) {
    if (!field_store_) return ToolResult::error("chitta-field store unavailable");

    std::string query = params.value("query", "");
    size_t limit = params.value("limit", 20);
    if (query.empty()) return ToolResult::error("query required");

    std::string json_str = field_store_->skill_search(query, limit);
    json out = json::parse(json_str, nullptr, false);
    if (out.is_discarded()) out = json::array();
    return ToolResult::ok(json_str, out);
}

ToolResult FieldRpcHandler::tool_skill_deprecate(const json& params) {
    if (!field_store_) return ToolResult::error("chitta-field store unavailable");

    std::string skill_id = params.value("skill_id", "");
    if (skill_id.empty()) return ToolResult::error("skill_id required");

    int r = field_store_->skill_deprecate(skill_id);
    if (r != 0) return ToolResult::error("skill not found");
    return ToolResult::ok("Skill deprecated", {{"skill_id", skill_id}});
}

} // namespace chitta
