// Included into FieldRpcHandler class body — declarations only.
// Bodies live in chitta/src/handlers/field_memory_ops.cpp.

    ToolResult tool_strengthen(const json& params);
    ToolResult tool_weaken(const json& params);
    ToolResult tool_forget(const json& params);
    ToolResult tool_batch_forget(const json& params);
    ToolResult tool_observe(const json& params);
    ToolResult tool_grow(const json& params);
    ToolResult tool_set_affect(const json& params);
    ToolResult tool_get(const json& params);
    ToolResult tool_expand_memory(const json& params);
    ToolResult tool_update(const json& params);
    ToolResult tool_query(const json& params);
    ToolResult tool_tag(const json& params);
    ToolResult tool_explore_recall(const json& params);
    ToolResult tool_explore_peek(const json& params);
    ToolResult tool_explore_expand(const json& params);
    ToolResult tool_explore_neighbors(const json& params);
    ToolResult tool_list_memories_brief(const json& params);
    ToolResult tool_forget_kind(const json& params);
    ToolResult tool_set_priority_tier(const json& params);
    ToolResult tool_set_memory_type(const json& params);
    ToolResult tool_memory_type_stats(const json& params);
    ToolResult tool_recall_by_priority(const json& params);
    ToolResult tool_expand_query(const json& params);
    ToolResult tool_connect(const json& params);
    ToolResult tool_connect_temporal(const json& params);
    ToolResult tool_triplet_history(const json& params);
    ToolResult tool_query_triplets_temporal(const json& params);
    ToolResult tool_triplet_query_as_of(const json& params);
    ToolResult tool_triplet_supersede(const json& params);
    ToolResult tool_graph_traverse(const json& params);
    ToolResult tool_graph_pagerank(const json& params);
    ToolResult tool_list_by_status(const json& params);
