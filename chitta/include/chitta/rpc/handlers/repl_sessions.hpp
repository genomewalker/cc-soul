// Included into FieldRpcHandler class body — declarations only.
// Bodies live in chitta/src/handlers/repl_sessions.cpp.

    ToolResult tool_repl_session_get(const json& params);
    ToolResult tool_repl_session_set(const json& params);
    ToolResult tool_repl_session_delete(const json& params);
    ToolResult tool_repl_execute(const json& params);
    ToolResult tool_repl_session_list();
