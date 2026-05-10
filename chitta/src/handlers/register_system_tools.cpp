// register_system_tools — chunk extracted from register_tools() so editing
// tool metadata only retemplates one chunk at a time.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

void FieldRpcHandler::register_system_tools() {
    tools_.push_back({{"name","memory_status"},{"description","Get effective status of a memory: active, superseded, or contradicted — checks incoming supersedes triplets"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"}}},{"memory_id",{{"type","integer"}}}
        }},{"required",json::array()}}}
    });
    handlers_["memory_status"] = [this](const json& p) { return tool_memory_status(p); };

    tools_.push_back({{"name","memory_provenance"},{"description","Show why a memory exists: source, evidence, superseded_by, supersedes relations"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"},{"description","Memory ID to inspect"}}},
            {"memory_id",{{"type","integer"},{"description","Memory ID (numeric)"}}}
        }},{"required",json::array()}}}
    });
    handlers_["memory_provenance"] = [this](const json& p) { return tool_memory_provenance(p); };

    tools_.push_back({{"name","compact_wal"},{"description","Compact WAL: save full snapshot then delete covered segments"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["compact_wal"] = [this](const json& p) { return tool_compact_wal(p); };

    tools_.push_back({{"name","trim_realm_names"},{"description","Fix realm names with trailing whitespace/newlines"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["trim_realm_names"] = [this](const json& p) { return tool_trim_realm_names(p); };

    tools_.push_back({{"name","save_spectral_snapshot"},{"description","Save spectral stats snapshot for drift tracking"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["save_spectral_snapshot"] = [this](const json& p) { return tool_save_spectral_snapshot(p); };

    tools_.push_back({{"name","spectral_drift"},{"description","Compare current embedding geometry with last snapshot"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["spectral_drift"] = [this](const json& p) { return tool_spectral_drift(p); };

    tools_.push_back({{"name","queue_status"},{"description","Show async queue stats: processed, failed, dead-letter path"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["queue_status"] = [this](const json& p) { return tool_queue_status(p); };

    tools_.push_back({{"name","health_check"},{"description","Check daemon health"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["health_check"] = [this](const json& p) { return tool_health_check(p); };

    tools_.push_back({{"name","version_check"},{"description","Get version info"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["version_check"] = [this](const json&) { return tool_version_check(); };

    tools_.push_back({{"name","cycle"},{"description","Run maintenance cycle"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"force",{{"type","boolean"}}}
        }}}}
    });
    handlers_["cycle"] = [this](const json& p) { return tool_cycle(p); };

    tools_.push_back({{"name","cleanup"},{"description","Remove garbage nodes"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"dry_run",{{"type","boolean"}}}
        }}}}
    });
    handlers_["cleanup"] = [this](const json& p) { return tool_cleanup(p); };

    tools_.push_back({{"name","soul_context"},{"description","Get current soul state and statistics"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["soul_context"] = [this](const json& p) { return tool_soul_context(p); };

    tools_.push_back({{"name","resonance_stats"},{"description","Show ResonanceLearner Bayesian bandit stats"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["resonance_stats"] = [this](const json& p) { return tool_resonance_stats(p); };

    tools_.push_back({{"name","subconscious_stats"},{"description","Get subconscious background processor stats"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["subconscious_stats"] = [this](const json& p) { return tool_subconscious_stats(p); };

    tools_.push_back({{"name","reembed_memories"},{"description","Re-embed memories with proper embeddings"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"all",{{"type","boolean"}}},{"kind",{{"type","string"}}},
            {"min_confidence",{{"type","number"}}},{"limit",{{"type","integer"}}},
            {"dry_run",{{"type","boolean"}}}
        }}}}
    });
    handlers_["reembed_memories"] = [this](const json& p) { return tool_reembed_memories(p); };

    tools_.push_back({{"name","rebuild_fts_index"},{"description","Rebuild FTS index for BM25 search"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["rebuild_fts_index"] = [this](const json& p) { return tool_rebuild_fts_index(p); };

    tools_.push_back({{"name","hygiene_stats"},{"description","Get memory hygiene statistics"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["hygiene_stats"] = [this](const json& p) { return tool_hygiene_stats(p); };

    tools_.push_back({{"name","hygiene_run"},{"description","Run memory hygiene: decay, prune, consolidate"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"prune_threshold",{{"type","number"}}},{"min_age_days",{{"type","number"}}},
            {"consolidation_threshold",{{"type","number"}}},{"max_consolidations",{{"type","integer"}}}
        }}}}
    });
    handlers_["hygiene_run"] = [this](const json& p) { return tool_hygiene_run(p); };

    tools_.push_back({{"name","import_soul"},{"description","Import .soul file (SSL format). Supports --realm and --source_session for targeted ingestion."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"file",{{"type","string"}}},{"content",{{"type","string"}}},
            {"realm",{{"type","string"},{"description","Target realm (default: brahman)"}}},
            {"source_session",{{"type","string"},{"description","Tag all imported memories with this session ID"}}},
            {"confidence",{{"type","number"},{"description","Confidence for imported memories (default: 0.8)"}}}
        }}}}
    });
    handlers_["import_soul"] = [this](const json& p) { return tool_import_soul(p); };

    tools_.push_back({{"name","export_soul"},{"description","Export memories to SSL format"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"file",{{"type","string"}}},{"tag",{{"type","string"}}},
            {"limit",{{"type","integer"}}}
        }}}}
    });
    handlers_["export_soul"] = [this](const json& p) { return tool_export_soul(p); };

    tools_.push_back({{"name","chitta_health"},{"description","Report feedback loop health diagnostics"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["chitta_health"] = [this](const json& p) { return tool_chitta_health(p); };

    tools_.push_back({{"name","theme_list"},{"description","List all themes with statistics"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"realm",{{"type","string"}}},{"limit",{{"type","integer"}}}
        }}}}
    });
    handlers_["theme_list"] = [this](const json& p) { return tool_theme_list(p); };

    tools_.push_back({{"name","theme_get"},{"description","Get theme details"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["theme_get"] = [this](const json& p) { return tool_theme_get(p); };

    tools_.push_back({{"name","theme_recall"},{"description","Two-stage theme-based retrieval"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query",{{"type","string"}}},{"limit",{{"type","integer"}}},{"realm",{{"type","string"}}}
        }},{"required",{"query"}}}}
    });
    handlers_["theme_recall"] = [this](const json& p) { return tool_theme_recall(p); };

    tools_.push_back({{"name","theme_stats"},{"description","Get theme organization statistics"},
        {"inputSchema",{{"type","object"},{"properties",{{"realm",{{"type","string"}}}}}}}
    });
    handlers_["theme_stats"] = [this](const json& p) { return tool_theme_stats(p); };

    // theme_maintain and theme_assign_orphans removed — no theme engine backend

    // ── Realm tools ─────────────────────────────────────────────────────
    tools_.push_back({{"name","realm_list"},{"description","List all known realms"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["realm_list"] = [this](const json&) { return tool_realm_list(); };

    tools_.push_back({{"name","realm_get"},{"description","Get all realms a memory belongs to"},
        {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","string"}}}}},{"required",{"id"}}}}
    });
    handlers_["realm_get"] = [this](const json& p) { return tool_realm_get(p); };

    tools_.push_back({{"name","realm_set"},{"description","Set primary realm for a memory"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"}}},{"realm",{{"type","string"}}}
        }},{"required",{"id","realm"}}}}
    });
    handlers_["realm_set"] = [this](const json& p) { return tool_realm_set(p); };

    tools_.push_back({{"name","realm_add"},{"description","Add memory to a shared realm (stub)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"}}},{"realm",{{"type","string"}}}
        }},{"required",{"id","realm"}}}}
    });
    handlers_["realm_add"] = [this](const json& p) { return tool_realm_add(p); };

    tools_.push_back({{"name","realm_remove"},{"description","Remove memory from a shared realm (stub)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"}}},{"realm",{{"type","string"}}}
        }},{"required",{"id","realm"}}}}
    });
    handlers_["realm_remove"] = [this](const json& p) { return tool_realm_remove(p); };

    tools_.push_back({{"name","realm_visibility"},{"description","Set visibility level (stub)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"}}},{"visibility",{{"type","integer"}}}
        }},{"required",{"id","visibility"}}}}
    });
    handlers_["realm_visibility"] = [this](const json& p) { return tool_realm_visibility(p); };

    tools_.push_back({{"name","realm_detect"},{"description","Detect current realm from environment"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["realm_detect"] = [this](const json&) { return tool_realm_detect(); };

    // ── Ledger + Long Task tools ────────────────────────────────────────
    tools_.push_back({{"name","ledger_save"},{"description","Save session checkpoint"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"session_id",{{"type","string"}}},{"project",{{"type","string"}}},
            {"transcript_path",{{"type","string"}}},{"mood",{{"type","string"}}},
            {"coherence",{{"type","number"}}},{"confidence",{{"type","number"}}},
            {"todos",{{"type","array"}}},{"active_files",{{"type","array"},{"items",{{"type","string"}}}}},
            {"decisions",{{"type","array"},{"items",{{"type","string"}}}}},
            {"next_steps",{{"type","array"},{"items",{{"type","string"}}}}},
            {"blockers",{{"type","array"},{"items",{{"type","string"}}}}},
            {"discoveries",{{"type","array"},{"items",{{"type","string"}}}}},
            {"snapshot",{{"type","string"}}}
        }},{"required",json::array()}}}
    });
    handlers_["ledger_save"] = [this](const json& p) { return tool_ledger_save(p); };

    tools_.push_back({{"name","ledger_load"},{"description","Load most recent checkpoint"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"session_id",{{"type","string"}}},{"project",{{"type","string"}}},
            {"include_snapshot",{{"type","boolean"},{"description","Include snapshot preview (truncated) in response"}}}
        }}}}
    });
    handlers_["ledger_load"] = [this](const json& p) { return tool_ledger_load(p); };

    tools_.push_back({{"name","ledger_list"},{"description","List recent checkpoints"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"project",{{"type","string"}}},{"limit",{{"type","integer"}}}
        }}}}
    });
    handlers_["ledger_list"] = [this](const json& p) { return tool_ledger_list(p); };

    tools_.push_back({
        {"name","ledger_get"},
        {"description","Get checkpoint by key or session/project"},
        {"inputSchema", {
            {"type","object"},
            {"properties", {
                {"key", {{"type","string"}, {"description","Canonical checkpoint key (session:project)"}}},
                {"session_id", {{"type","string"}}},
                {"project", {{"type","string"}}},
                {"include_snapshot", {{"type","boolean"}, {"description","Include snapshot preview (truncated) in response"}}}
            }}
        }}
    });
    handlers_["ledger_get"] = [this](const json& p) { return tool_ledger_get(p); };

    tools_.push_back({
        {"name","ledger_delete"},
        {"description","Delete checkpoint"},
        {"inputSchema", {
            {"type","object"},
            {"properties", {
                {"key", {{"type","string"}, {"description","Canonical checkpoint key (session:project)"}}},
                {"session_id", {{"type","string"}}},
                {"project", {{"type","string"}}}
            }}
        }}
    });
    handlers_["ledger_delete"] = [this](const json& p) { return tool_ledger_delete(p); };

    tools_.push_back({{"name","long_task_start"},{"description","Start a long-running task"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"task_id",{{"type","string"}}},{"goal",{{"type","string"}}},{"realm",{{"type","string"}}},
            {"hard_checks",{{"type","array"},{"items",{{"type","string"}}}}},
            {"soft_checks",{{"type","array"},{"items",{{"type","string"}}}}},
            {"work_items",{{"type","array"},{"items",{{"type","string"}}}}}
        }},{"required",{"task_id","goal"}}}}
    });
    handlers_["long_task_start"] = [this](const json& p) { return tool_long_task_start(p); };

    tools_.push_back({{"name","long_task_get"},{"description","Get a long-running task by ID"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"task_id",{{"type","string"}}}
        }},{"required",{"task_id"}}}}
    });
    handlers_["long_task_get"] = [this](const json& p) { return tool_long_task_get(p); };

    tools_.push_back({{"name","long_task_active"},{"description","Get active long-running task"},
        {"inputSchema",{{"type","object"},{"properties",{{"realm",{{"type","string"}}}}}}}
    });
    handlers_["long_task_active"] = [this](const json& p) { return tool_long_task_active(p); };

    tools_.push_back({{"name","long_task_update"},{"description","Update long-running task progress"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"task_id",{{"type","string"}}},{"completed_summary",{{"type","string"}}},
            {"work_items",{{"type","array"},{"items",{{"type","string"}}}}},
            {"blockers",{{"type","array"},{"items",{{"type","string"}}}}}
        }},{"required",{"task_id"}}}}
    });
    handlers_["long_task_update"] = [this](const json& p) { return tool_long_task_update(p); };

    tools_.push_back({{"name","long_task_complete"},{"description","Mark task as completed"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"task_id",{{"type","string"}}},{"outcome",{{"type","string"}}}
        }},{"required",{"task_id","outcome"}}}}
    });
    handlers_["long_task_complete"] = [this](const json& p) { return tool_long_task_complete(p); };

    tools_.push_back({{"name","long_task_event"},{"description","Append event to task log"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"task_id",{{"type","string"}}},{"kind",{{"type","string"}}},
            {"payload",{{"type","string"}}},{"tags",{{"type","array"},{"items",{{"type","string"}}}}},
            {"related_entities",{{"type","array"},{"items",{{"type","string"}}}}}
        }},{"required",{"task_id","kind"}}}}
    });
    handlers_["long_task_event"] = [this](const json& p) { return tool_long_task_event(p); };

    tools_.push_back({{"name","checkpoint"},{"description","Save session state"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"realm",{{"type","string"}}},{"mood",{{"type","string"}}},
            {"summary",{{"type","string"}}},{"next_steps",{{"type","array"},{"items",{{"type","string"}}}}},
            {"active_files",{{"type","array"},{"items",{{"type","string"}}}}},
            {"discoveries",{{"type","array"},{"items",{{"type","string"}}}}}
        }}}}
    });
    handlers_["checkpoint"] = [this](const json& p) { return tool_unified_checkpoint(p); };

    tools_.push_back({{"name","long_task_snapshot"},{"description","Get synthesized task context"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"task_id",{{"type","string"}}},{"mode",{{"type","string"}}},{"max_tokens",{{"type","integer"}}}
        }},{"required",{"task_id"}}}}
    });
    handlers_["long_task_snapshot"] = [this](const json& p) { return tool_long_task_snapshot(p); };

    tools_.push_back({{"name","long_task_evaluate"},{"description","Evaluate task completion"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"task_id",{{"type","string"}}}
        }},{"required",{"task_id"}}}}
    });
    handlers_["long_task_evaluate"] = [this](const json& p) { return tool_long_task_evaluate(p); };

    // ── Session/Transcript tools ────────────────────────────────────────
    tools_.push_back({{"name","skill_upload"},{"description","Upload a new skill version"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"skill_id",{{"type","string"}}},{"content",{{"type","string"}}},
            {"uploaded_by",{{"type","string"}}},{"tags",{{"type","array"},{"items",{{"type","string"}}}}}
        }},{"required",{"skill_id","content"}}}}
    });
    handlers_["skill_upload"] = [this](const json& p) { return tool_skill_upload(p); };

    tools_.push_back({{"name","skill_read"},{"description","Read a skill version (0=latest)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"skill_id",{{"type","string"}}},{"version",{{"type","integer"}}}
        }},{"required",{"skill_id"}}}}
    });
    handlers_["skill_read"] = [this](const json& p) { return tool_skill_read(p); };

    tools_.push_back({{"name","skill_list"},{"description","List all skills"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["skill_list"] = [this](const json&) { return tool_skill_list(); };

    tools_.push_back({{"name","skill_search"},{"description","Search skills by query"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query",{{"type","string"}}},{"limit",{{"type","integer"}}}
        }},{"required",{"query"}}}}
    });
    handlers_["skill_search"] = [this](const json& p) { return tool_skill_search(p); };

    tools_.push_back({{"name","skill_deprecate"},{"description","Deprecate a skill"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"skill_id",{{"type","string"}}}
        }},{"required",{"skill_id"}}}}
    });
    handlers_["skill_deprecate"] = [this](const json& p) { return tool_skill_deprecate(p); };

    // ── Agent Registry ─────────────────────────────────────────────────
    tools_.push_back({{"name","agent_upsert"},{"description","Register or update an agent"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"agent_id",{{"type","string"}}},{"display_name",{{"type","string"}}},
            {"description",{{"type","string"}}}
        }},{"required",{"agent_id"}}}}
    });
    handlers_["agent_upsert"] = [this](const json& p) { return tool_agent_upsert(p); };

    tools_.push_back({{"name","agent_get"},{"description","Get agent record"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"agent_id",{{"type","string"}}}
        }},{"required",{"agent_id"}}}}
    });
    handlers_["agent_get"] = [this](const json& p) { return tool_agent_get(p); };

    tools_.push_back({{"name","agent_list"},{"description","List all agents"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["agent_list"] = [this](const json&) { return tool_agent_list(); };

    tools_.push_back({{"name","agent_disable"},{"description","Disable an agent"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"agent_id",{{"type","string"}}}
        }},{"required",{"agent_id"}}}}
    });
    handlers_["agent_disable"] = [this](const json& p) { return tool_agent_disable(p); };

    // ── Misc tools ──────────────────────────────────────────────────────

    // Memory management
    tools_.push_back({{"name","why_active"},{"description","Explain why a memory is active: status, epistemic source, confirmations, contradictions"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"},{"description","Memory ID to inspect"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["why_active"] = [this](const json& p) { return tool_why_active(p); };

    tools_.push_back({{"name","what_superseded"},{"description","Show the full supersession chain for a memory"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"},{"description","Memory ID to trace"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["what_superseded"] = [this](const json& p) { return tool_what_superseded(p); };

    tools_.push_back({{"name","show_conflicts"},{"description","Semantic search + show contradiction pairs for matching memories"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query",{{"type","string"},{"description","Search query"}}},
            {"limit",{{"type","integer"},{"description","Max memories to scan (default 20)"}}},
            {"realm",{{"type","string"},{"description","Filter by realm"}}}
        }},{"required",{"query"}}}}
    });
    handlers_["show_conflicts"] = [this](const json& p) { return tool_show_conflicts(p); };

    tools_.push_back({{"name","detect_contradictions"},{"description","Detect contradictions for a stored memory against realm peers"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"memory_id",{{"type","string"},{"description","Memory ID to check"}}},
            {"realm",{{"type","string"},{"description","Realm to scan (default global)"}}}
        }},{"required",{"memory_id"}}}}
    });
    handlers_["detect_contradictions"] = [this](const json& p) { return tool_detect_contradictions(p); };

    tools_.push_back({{"name","scan_contradictions"},{"description","Background scan: find contradiction candidates across all memories in a realm"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"realm",{{"type","string"},{"description","Realm to scan"}}},
            {"limit",{{"type","integer"},{"description","Max candidates to return (default 50)"}}}
        }}}}
    });
    handlers_["scan_contradictions"] = [this](const json& p) { return tool_scan_contradictions(p); };

    tools_.push_back({{"name","resolve_contradiction"},{"description","Resolve a contradiction: declare winner supersedes loser, store CORRECTION memory"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"winner_id",{{"type","string"},{"description","Memory ID that is correct"}}},
            {"loser_id",{{"type","string"},{"description","Memory ID to demote"}}},
            {"reason",{{"type","string"},{"description","Explanation for the resolution"}}}
        }},{"required",{"winner_id","loser_id"}}}}
    });
    handlers_["resolve_contradiction"] = [this](const json& p) { return tool_resolve_contradiction(p); };

    // Operator controls
    tools_.push_back({{"name","approve_memory"},{"description","Approve a Proposed memory, promoting it to Active"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"},{"description","Memory ID to approve"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["approve_memory"] = [this](const json& p) { return tool_approve_memory(p); };

    tools_.push_back({{"name","reject_memory"},{"description","Reject a Proposed memory, archiving it"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"},{"description","Memory ID to reject"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["reject_memory"] = [this](const json& p) { return tool_reject_memory(p); };

    tools_.push_back({{"name","promote_memory"},{"description","Promote a memory one tier: Proposed→Observed→Verified→Active"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"},{"description","Memory ID"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["promote_memory"] = [this](const json& p) { return tool_promote_memory(p); };

    tools_.push_back({{"name","conflict_inspector"},{"description","Semantic search + show status and contradiction partners for each hit"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query",{{"type","string"},{"description","Search query"}}},
            {"limit",{{"type","integer"},{"description","Max memories to scan (default 10)"}}},
            {"realm",{{"type","string"},{"description","Filter by realm"}}}
        }},{"required",{"query"}}}}
    });
    handlers_["conflict_inspector"] = [this](const json& p) { return tool_conflict_inspector(p); };

    tools_.push_back({{"name","disable_source"},{"description","Add a source to the deny-list via triplet"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"source",{{"type","string"},{"description","Source identifier to deny"}}}
        }},{"required",{"source"}}}}
    });
    handlers_["disable_source"] = [this](const json& p) { return tool_disable_source(p); };

    // Override memory_history handler with richer operator version
    handlers_["memory_history"] = [this](const json& p) { return tool_operator_memory_history(p); };

    // ── Tier 1: Ingest source ──────────────────────────────────────────
}

} // namespace chitta
