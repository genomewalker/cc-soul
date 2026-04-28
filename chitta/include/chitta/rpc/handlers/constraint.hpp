// Included into FieldRpcHandler class body — declarations only.
// Bodies live in chitta/src/handlers/constraint.cpp.

    ToolResult tool_assert_fact(const json& params);
    ToolResult tool_retract_fact(const json& params);
    ToolResult tool_query_unify(const json& params);
    ToolResult tool_query_chain(const json& params);
    ToolResult tool_explain_fact(const json& params);
    ToolResult tool_branch_create(const json& params);
    ToolResult tool_branch_resolve(const json& params);
    ToolResult tool_trigger_add(const json& params);
    ToolResult tool_trigger_list(const json&);
    ToolResult tool_trigger_fire(const json& params);
    ToolResult tool_trigger_dismiss(const json& params);
    ToolResult tool_predict_needed(const json& params);
