// Included into FieldRpcHandler class body — declarations only.
// Bodies live in chitta/src/handlers/field_system.cpp.

    ToolResult tool_health_check(const json&);
    ToolResult tool_version_check();
    ToolResult tool_cycle(const json&);
    ToolResult tool_cleanup(const json& params);
    ToolResult tool_soul_context(const json&);
    ToolResult tool_resonance_stats(const json&);
    ToolResult tool_subconscious_stats(const json&);
    ToolResult tool_reembed_memories(const json& params);
    ToolResult tool_rebuild_fts_index(const json&);
    ToolResult tool_hygiene_stats(const json&);
    ToolResult tool_hygiene_run(const json& params);
    ToolResult tool_import_soul(const json& params);
    ToolResult tool_export_soul(const json& params);
    ToolResult tool_chitta_health(const json&);
    ToolResult tool_theme_list(const json& params);
    ToolResult tool_theme_get(const json& params);
    ToolResult tool_theme_recall(const json& params);
    ToolResult tool_theme_stats(const json& params);
    ToolResult tool_realm_list();
    ToolResult tool_realm_get(const json& params);
    ToolResult tool_realm_set(const json& params);
    ToolResult tool_realm_add(const json& params);
    ToolResult tool_realm_remove(const json& params);
    ToolResult tool_realm_visibility(const json& params);
    ToolResult tool_realm_detect();
    ToolResult tool_queue_status(const json&);
    ToolResult tool_ledger_health(const json&);
    ToolResult tool_memory_provenance(const json& params);
    ToolResult tool_memory_status(const json& params);
    ToolResult tool_trim_realm_names(const json&);
    ToolResult tool_save_spectral_snapshot(const json&);
    ToolResult tool_spectral_drift(const json&);
    ToolResult tool_compact_wal(const json&);
