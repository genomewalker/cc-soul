// field_write_gate RPC handlers — write-gate admission stats.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

ToolResult FieldRpcHandler::tool_write_gate_stats(const json& /*params*/) {
    auto lock = acquire_shared_lock();
    std::string stats = field_store_->write_gate_stats_json();
    try {
        auto j = json::parse(stats);
        j["ok"] = true;
        return ToolResult::ok(j.dump());
    } catch (...) {
        return ToolResult::ok(stats);
    }
}

} // namespace chitta
