// Included into FieldRpcHandler class body — declarations only.
// Bodies live in chitta/src/handlers/intervention.cpp.

    ToolResult tool_start_intervention(FieldStore* fs, const json& params);
    ToolResult tool_add_observation(FieldStore* fs, const json& params);
    ToolResult tool_close_intervention(FieldStore* fs, const json& params);
    ToolResult tool_record_attribution(FieldStore* fs, const json& params);
    ToolResult tool_query_interventions(FieldStore* fs, const json& params);
    ToolResult tool_get_intervention(FieldStore* fs, const json& params);
    ToolResult tool_intervention_stats(FieldStore* fs, const json&);
    ToolResult tool_list_open_interventions(FieldStore* fs, const json&);
