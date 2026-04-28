// Included into FieldRpcHandler class body — declarations only.
// Bodies live in chitta/src/handlers/agent_protocol.cpp.

    ToolResult tool_register_task(FieldStore* fs, const json& params);
    ToolResult tool_update_task(FieldStore* fs, const json& params);
    ToolResult tool_add_delegation(FieldStore* fs, const json& params);
    ToolResult tool_link_evidence(FieldStore* fs, const json& params);
    ToolResult tool_add_probe(FieldStore* fs, const json& params);
    ToolResult tool_resolve_probe(FieldStore* fs, const json& params);
    ToolResult tool_set_criterion(FieldStore* fs, const json& params);
    ToolResult tool_get_task(FieldStore* fs, const json& params);
    ToolResult tool_query_tasks(FieldStore* fs, const json& params);
    ToolResult tool_agent_protocol_stats(FieldStore* fs, const json&);
