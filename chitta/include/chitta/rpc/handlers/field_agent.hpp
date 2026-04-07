// Included into FieldRpcHandler class body — not a standalone header

    ToolResult tool_agent_upsert(const json& params) {
        if (!field_store_) return ToolResult::error("chitta-field store unavailable");

        std::string agent_id = params.value("agent_id", "");
        std::string display_name = params.value("display_name", "");
        std::string description = params.value("description", "");
        if (agent_id.empty()) return ToolResult::error("agent_id required");

        int64_t ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        int r = field_store_->agent_upsert(agent_id, display_name, description, ts_ms);
        if (r < 0) return ToolResult::error("agent upsert failed");

        return ToolResult::ok(r == 1 ? "Agent registered" : "Agent updated", {
            {"agent_id", agent_id},
            {"is_new", r == 1},
        });
    }

    ToolResult tool_agent_get(const json& params) {
        if (!field_store_) return ToolResult::error("chitta-field store unavailable");

        std::string agent_id = params.value("agent_id", "");
        if (agent_id.empty()) return ToolResult::error("agent_id required");

        std::string json_str = field_store_->agent_get(agent_id);
        if (json_str.empty()) return ToolResult::error("agent not found");

        json out = json::parse(json_str, nullptr, false);
        if (out.is_discarded()) return ToolResult::error("parse error");
        return ToolResult::ok(json_str, out);
    }

    ToolResult tool_agent_list() {
        if (!field_store_) return ToolResult::error("chitta-field store unavailable");

        std::string json_str = field_store_->agent_list();
        json out = json::parse(json_str, nullptr, false);
        if (out.is_discarded()) out = json::array();
        return ToolResult::ok(json_str, out);
    }

    ToolResult tool_agent_disable(const json& params) {
        if (!field_store_) return ToolResult::error("chitta-field store unavailable");

        std::string agent_id = params.value("agent_id", "");
        if (agent_id.empty()) return ToolResult::error("agent_id required");

        int r = field_store_->agent_disable(agent_id);
        if (r != 0) return ToolResult::error("agent not found");
        return ToolResult::ok("Agent disabled", {{"agent_id", agent_id}});
    }
