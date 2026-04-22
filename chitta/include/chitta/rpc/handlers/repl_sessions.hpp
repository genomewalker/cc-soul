// Included into FieldRpcHandler class body — not a standalone header

    ToolResult tool_repl_session_get(const json& params) {
        if (!field_store_) return ToolResult::error("chitta-field store unavailable");
        std::string session_id = params.value("session_id", "");
        if (session_id.empty()) return ToolResult::error("session_id required");

        std::string ns = field_store_->repl_session_get(session_id);
        if (ns.empty()) return ToolResult::ok("null", {{"session_id", session_id}, {"found", false}});

        return ToolResult::ok(ns, {{"session_id", session_id}, {"found", true}});
    }

    ToolResult tool_repl_session_set(const json& params) {
        if (!field_store_) return ToolResult::error("chitta-field store unavailable");
        std::string session_id     = params.value("session_id", "");
        std::string namespace_json = params.value("namespace_json", "");
        if (session_id.empty())     return ToolResult::error("session_id required");
        if (namespace_json.empty()) return ToolResult::error("namespace_json required");

        int64_t updated_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        int r = field_store_->repl_session_set(session_id, namespace_json, updated_ms);
        if (r != 0) return ToolResult::error("repl_session_set failed");
        return ToolResult::ok("ok", {{"session_id", session_id}, {"updated_ms", updated_ms}});
    }

    ToolResult tool_repl_session_delete(const json& params) {
        if (!field_store_) return ToolResult::error("chitta-field store unavailable");
        std::string session_id = params.value("session_id", "");
        if (session_id.empty()) return ToolResult::error("session_id required");

        int r = field_store_->repl_session_delete(session_id);
        return ToolResult::ok(r > 0 ? "deleted" : "not found", {{"session_id", session_id}, {"deleted", r > 0}});
    }

    ToolResult tool_repl_session_list() {
        if (!field_store_) return ToolResult::error("chitta-field store unavailable");
        std::string json_str = field_store_->repl_session_list();
        json out = json::parse(json_str, nullptr, false);
        if (out.is_discarded()) out = json::array();
        return ToolResult::ok(json_str, out);
    }
