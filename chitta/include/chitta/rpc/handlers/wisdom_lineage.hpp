// Included into FieldRpcHandler class body — declarations only.
// Bodies live in chitta/src/handlers/wisdom_lineage.cpp.

    ToolResult tool_enroll_wisdom_lineage(FieldStore* fs, const json& params);
    ToolResult tool_transition_wisdom_lineage(FieldStore* fs, const json& params);
    ToolResult tool_close_rederive(FieldStore* fs, const json& params);
    ToolResult tool_query_wisdom_lineages(FieldStore* fs, const json& params);
    ToolResult tool_get_wisdom_lineage(FieldStore* fs, const json& params);
    ToolResult tool_wisdom_lineage_stats(FieldStore* fs, const json&);
    ToolResult tool_tick_lineage_staleness(FieldStore* fs, const json&);
    ToolResult tool_lineage_expiry_check(FieldStore* fs, const json&);
