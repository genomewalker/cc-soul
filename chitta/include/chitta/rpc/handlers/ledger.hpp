// Included into FieldRpcHandler class body — declarations only.
// Bodies live in chitta/src/handlers/ledger.cpp.

// Included into FieldRpcHandler class body — not a standalone header.
// Backed by chitta-field event log.
//
// Storage model: each ledger entry is a domain event with domain="ledger".
//   save:   emit_event("ledger", "save",   key, payload_json)
//   delete: emit_event("ledger", "delete", key, "")       (tombstone)
//   load:   get_latest_event("ledger", "save", key)
//   get:    same as load, by key
//   list:   iterate_log scanning for domain="ledger" events
//
// The key is: "{session_id}:{project}" (colon-joined).

    // ── helpers ──────────────────────────────────────────────────────────────

    static std::string ledger_key(const std::string& session_id, const std::string& project) {
        return session_id + ":" + project;
    }

    static json parse_json_safe(const std::string& s) {
        if (s.empty()) return json::object();
        try { return json::parse(s); } catch (...) { return json::object(); }
    }

    static json parse_json_array_safe(const std::string& s) {
        if (s.empty()) return json::array();
        try { return json::parse(s); } catch (...) { return json::array(); }
    }

    // ── tool implementations ─────────────────────────────────────────────────

    ToolResult tool_ledger_save(const json& params);
    ToolResult tool_ledger_load(const json& params);
    ToolResult tool_ledger_list(const json& params);
    ToolResult tool_ledger_get(const json& params);
    ToolResult tool_ledger_delete(const json& params);
