// Included into FieldRpcHandler class body — declarations only.
// Bodies live in chitta/src/handlers/learning.cpp.

    ToolResult tool_surprise_learning_stats(FieldStore* fs, const json&);
    ToolResult tool_upsert_wisdom_candidate(FieldStore* fs, const json& params);
    ToolResult tool_update_wisdom_lifecycle(FieldStore* fs, const json& params);
    ToolResult tool_query_wisdom_candidates(FieldStore* fs, const json& params);
    ToolResult tool_wisdom_promotion_stats(FieldStore* fs, const json&);
    ToolResult tool_attach_debt_evidence(FieldStore* fs, const json& params);
    ToolResult tool_update_scorer_model(FieldStore* fs, const json& params);
    ToolResult tool_learned_scorer_stats(FieldStore* fs, const json&);
    ToolResult tool_effective_scorer_weights(FieldStore* fs, const json&);
