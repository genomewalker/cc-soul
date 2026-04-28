// register_code_intel_tools — chunk extracted from register_tools() so editing
// tool metadata only retemplates one chunk at a time.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

void FieldRpcHandler::register_code_intel_tools() {
    tools_.push_back({{"name","extract_symbols"},{"description","Extract symbols from source file using tree-sitter"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"path",{{"type","string"},{"description","File path to analyze"}}}
        }},{"required",{"path"}}}}
    });
    handlers_["extract_symbols"] = [this](const json& p) { return tool_extract_symbols(p); };

    tools_.push_back({{"name","learn_codebase"},{"description","Learn codebase by extracting symbols. path can be a local directory or a remote git URL (https://github.com/..., git@github.com:...). Remote repos are shallow-cloned into a temp dir, indexed, then deleted."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"path",{{"type","string"},{"description","Local path or remote git URL"}}},
            {"project",{{"type","string"},{"description","Project name (defaults to repo/dir name)"}}},
            {"branch",{{"type","string"},{"description","Branch, tag, or commit to clone (remote only)"}}},
            {"max_files",{{"type","integer"}}},{"exclude",{{"type","string"}}},
            {"incremental",{{"type","boolean"}}},{"force",{{"type","boolean"}}}
        }},{"required",{"path"}}}}
    });
    handlers_["learn_codebase"] = [this](const json& p) { return tool_learn_codebase(p); };

    tools_.push_back({{"name","find_symbol"},{"description","Search for symbols by name"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"name",{{"type","string"}}},{"kind",{{"type","string"}}}
        }},{"required",{"name"}}}}
    });
    handlers_["find_symbol"] = [this](const json& p) { return tool_find_symbol(p); };

    tools_.push_back({{"name","symbol_callers"},{"description","Find all symbols that call the given symbol"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"name",{{"type","string"}}},{"id",{{"type","integer"}}},
            {"kind",{{"type","string"}}},{"project",{{"type","string"}}}
        }}}}
    });
    handlers_["symbol_callers"] = [this](const json& p) { return tool_symbol_callers(p); };

    tools_.push_back({{"name","symbol_callees"},{"description","Find all symbols that the given symbol calls"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"name",{{"type","string"}}},{"id",{{"type","integer"}}},
            {"kind",{{"type","string"}}},{"project",{{"type","string"}}}
        }}}}
    });
    handlers_["symbol_callees"] = [this](const json& p) { return tool_symbol_callees(p); };

    tools_.push_back({{"name","read_symbol"},{"description","Read actual source code for a symbol"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"name",{{"type","string"}}},{"id",{{"type","integer"}}},
            {"kind",{{"type","string"}}},{"project",{{"type","string"}}}
        }}}}
    });
    handlers_["read_symbol"] = [this](const json& p) { return tool_read_symbol(p); };

    tools_.push_back({{"name","read_function"},{"description","Read source of a function/method by name"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"name",{{"type","string"}}},{"project",{{"type","string"}}}
        }},{"required",{"name"}}}}
    });
    handlers_["read_function"] = [this](const json& p) { return tool_read_function(p); };

    tools_.push_back({{"name","search_symbols"},{"description","Semantic search for code symbols"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query",{{"type","string"}}},{"kind",{{"type","string"}}},
            {"limit",{{"type","integer"}}},{"project",{{"type","string"}}}
        }},{"required",{"query"}}}}
    });
    handlers_["search_symbols"] = [this](const json& p) { return tool_search_symbols(p); };

    tools_.push_back({{"name","code_context"},{"description","Get code context summary"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"path",{{"type","string"}}}
        }}}}
    });
    handlers_["code_context"] = [this](const json& p) { return tool_code_context(p); };

    tools_.push_back({{"name","smart_context"},{"description","Build intelligent context combining memories, code, and graph"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"task",{{"type","string"}}},{"mode",{{"type","string"}}},
            {"limit",{{"type","integer"}}},{"memories",{{"type","boolean"}}},
            {"code",{{"type","boolean"}}},{"neighbors",{{"type","boolean"}}},
            {"realm",{{"type","string"}}}
        }},{"required",{"task"}}}}
    });
    handlers_["smart_context"] = [this](const json& p) { return tool_smart_context(p); };

    tools_.push_back({{"name","codebase_overview"},{"description","Get full indexed codebase structure"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"project",{{"type","string"}}},{"format",{{"type","string"}}},
            {"include_callsites",{{"type","boolean"}}}
        }}}}
    });
    handlers_["codebase_overview"] = [this](const json& p) { return tool_codebase_overview(p); };

    tools_.push_back({{"name","clear_codebase"},{"description","Remove all code intel data for a project"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"project",{{"type","string"}}},{"dry_run",{{"type","boolean"}}}
        }},{"required",{"project"}}}}
    });
    handlers_["clear_codebase"] = [this](const json& p) { return tool_clear_codebase(p); };

    tools_.push_back({{"name","clear_triplets"},{"description","Delete triplets by subject pattern"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"pattern",{{"type","string"}}},{"dry_run",{{"type","boolean"}}}
        }},{"required",{"pattern"}}}}
    });
    handlers_["clear_triplets"] = [this](const json& p) { return tool_clear_triplets(p); };

    tools_.push_back({{"name","resolve_callsites"},{"description","Resolve callsites to symbols for call graph"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"project",{{"type","string"}}}
        }}}}
    });
    handlers_["resolve_callsites"] = [this](const json& p) { return tool_resolve_callsites(p); };

    tools_.push_back({{"name","type_hierarchy"},{"description","Get type hierarchy for a type"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"name",{{"type","string"}}},{"direction",{{"type","string"}}}
        }},{"required",{"name"}}}}
    });
    handlers_["type_hierarchy"] = [this](const json& p) { return tool_type_hierarchy(p); };

    tools_.push_back({{"name","file_imports"},{"description","Get imports for a file"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"path",{{"type","string"}}}
        }},{"required",{"path"}}}}
    });
    handlers_["file_imports"] = [this](const json& p) { return tool_file_imports(p); };

    tools_.push_back({{"name","file_dependents"},{"description","Get files that import a module/file"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"module",{{"type","string"}}}
        }},{"required",{"module"}}}}
    });
    handlers_["file_dependents"] = [this](const json& p) { return tool_file_dependents(p); };

    tools_.push_back({{"name","embed_symbols"},{"description","Fast embed symbol metadata"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"batch_size",{{"type","integer"}}},{"reset",{{"type","boolean"}}}
        }}}}
    });
    handlers_["embed_symbols"] = [this](const json& p) { return tool_embed_symbols(p); };

    tools_.push_back({{"name","dedupe_symbols"},{"description","Remove duplicate symbols"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["dedupe_symbols"] = [this](const json& p) { return tool_dedupe_symbols(p); };

    tools_.push_back({{"name","describe_symbol"},{"description","Set description for a code symbol"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"symbol_id",{{"type","integer"}}},{"description",{{"type","string"}}}
        }},{"required",{"symbol_id","description"}}}}
    });
    handlers_["describe_symbol"] = [this](const json& p) { return tool_describe_symbol(p); };

    tools_.push_back({{"name","enrichment_status"},{"description","Get code enrichment progress"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["enrichment_status"] = [this](const json& p) { return tool_enrichment_status(p); };

    // ── System tools ────────────────────────────────────────────────────
    tools_.push_back({{"name","restore_code_intel_confidence"},{"description","Restore confidence for code intel memories"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"confidence",{{"type","number"}}},{"dry_run",{{"type","boolean"}}}
        }}}}
    });
    handlers_["restore_code_intel_confidence"] = [this](const json& p) { return tool_restore_code_intel_confidence(p); };

    // ── Theme tools ─────────────────────────────────────────────────────
}

} // namespace chitta
