// Included into FieldRpcHandler class body — declarations only.
// Bodies live in chitta/src/handlers/long_task.cpp.

// Included into FieldRpcHandler class body — not a standalone header.
// Backed by chitta-field task + event APIs.
//
// Storage model:
//   task_create()          — creates a task record in chitta-field
//   task_get()             — returns JSON payload for a task_id
//   task_transition()      — moves task through lifecycle states
//   task_update_payload()  — updates the task's payload blob
//   emit_event()           — domain event log for fine-grained tracking
//   get_latest_event()     — read back latest event for a task
//   iterate_log()          — scan log for listing/filtering

    // ── helpers ──────────────────────────────────────────────────────────────

    static int64_t field_now_ms() {  // Prefixed to avoid shadowing local now_ms vars
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    static json parse_json_obj(const std::string& s) {
        if (s.empty()) return json::object();
        try { return json::parse(s); } catch (...) { return json::object(); }
    }

    static json parse_json_arr(const std::string& s) {
        if (s.empty()) return json::array();
        try { return json::parse(s); } catch (...) { return json::array(); }
    }

    // ── tool implementations ─────────────────────────────────────────────────

    ToolResult tool_long_task_start(const json& params);
    ToolResult tool_long_task_get(const json& params);
    ToolResult tool_long_task_active(const json& params);
    ToolResult tool_long_task_update(const json& params);
    ToolResult tool_long_task_complete(const json& params);
    ToolResult tool_long_task_event(const json& params);
    ToolResult tool_unified_checkpoint(const json& params);
    ToolResult tool_long_task_snapshot(const json& params);
    ToolResult tool_long_task_evaluate(const json& params);
