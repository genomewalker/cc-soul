// Included into FieldRpcHandler class body — declarations only.
// Bodies live in chitta/src/handlers/field_distill.cpp.

    ToolResult tool_distill_status(const json&);
    ToolResult tool_distill_set_model(const json& params);
    ToolResult tool_suggestion_track(const json& params);
    ToolResult tool_suggestion_pending(const json& params);
    ToolResult tool_suggestion_resolve(const json& params);
    ToolResult tool_suggestion_count(const json&);
    ToolResult tool_consolidation_scan(const json& params);
    ToolResult tool_consolidation_merge(const json& params);
    ToolResult tool_consolidation_auto(const json& params);
    ToolResult tool_metacognition_corrections(const json& params);
    ToolResult tool_metacognition_outcomes(const json& params);
    ToolResult tool_metacognition_evaluate(const json&);
    ToolResult tool_epiplexity_check(const json& params);
    ToolResult tool_ssl_convert(const json& params);
    ToolResult tool_curiosity_note_gap(const json& params);
    ToolResult tool_curiosity_gaps(const json& params);
    ToolResult tool_curiosity_resolve(const json& params);
