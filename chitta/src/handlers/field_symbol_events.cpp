// field_symbol_events RPC handlers — symbol-keyed event log.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

ToolResult FieldRpcHandler::tool_symbol_event_log(const json& params) {
    std::string symbol_name = params.value("symbol_name", "");
    std::string file_path   = params.value("file_path", "");
    int limit               = params.value("limit", 50);

    json q;
    if (!symbol_name.empty()) q["symbol_name"] = symbol_name;
    if (!file_path.empty())   q["file_path"]   = file_path;
    q["limit"] = limit;

    std::string raw = field_store_->query_symbol_events(q.dump());
    try {
        auto arr = json::parse(raw);
        return ToolResult::ok(json{{"events", arr}, {"ok", true}}.dump());
    } catch (...) {
        return ToolResult::ok(raw);
    }
}

ToolResult FieldRpcHandler::tool_mark_memory_invalidated(const json& params) {
    if (!params.contains("memory_id")) {
        return ToolResult::error("memory_id required");
    }
    uint64_t memory_id = params["memory_id"].get<uint64_t>();
    std::string reason = params.value("reason", "manual");

    bool ok = field_store_->mark_memory_invalidated(memory_id, reason);
    return ToolResult::ok(json{{"ok", ok}, {"memory_id", memory_id}}.dump());
}

} // namespace chitta
