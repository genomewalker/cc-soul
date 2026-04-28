// register_misc_tools — chunk extracted from register_tools() so editing
// tool metadata only retemplates one chunk at a time.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

void FieldRpcHandler::register_misc_tools() {
    tools_.push_back({{"name","anticipation_observe"},{"description","Record context->action pattern"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"context",{{"type","string"}}},{"action",{{"type","string"}}},{"realm",{{"type","string"}}}
        }},{"required",{"context","action"}}}}
    });
    handlers_["anticipation_observe"] = [this](const json& p) { return tool_anticipation_observe(p); };

    tools_.push_back({{"name","anticipation_predict"},{"description","Predict likely actions"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"context",{{"type","string"}}},{"limit",{{"type","integer"}}},
            {"min_confidence",{{"type","number"}}},{"realm",{{"type","string"}}}
        }},{"required",{"context"}}}}
    });
    handlers_["anticipation_predict"] = [this](const json& p) { return tool_anticipation_predict(p); };

    tools_.push_back({{"name","anticipation_success"},{"description","Mark prediction as successful"},
        {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","integer"}}}}},{"required",{"id"}}}}
    });
    handlers_["anticipation_success"] = [this](const json& p) { return tool_anticipation_success(p); };

    tools_.push_back({{"name","anticipation_list"},{"description","List learned anticipation patterns"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"realm",{{"type","string"}}},{"limit",{{"type","integer"}}},{"sort_by",{{"type","string"}}}
        }}}}
    });
    handlers_["anticipation_list"] = [this](const json& p) { return tool_anticipation_list(p); };

    tools_.push_back({{"name","anticipation_filter"},{"description","Get predictions passing annoyance gate"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"session_id",{{"type","string"}}},{"max",{{"type","integer"}}}
        }}}}
    });
    handlers_["anticipation_filter"] = [this](const json& p) { return tool_anticipation_filter(p); };

    tools_.push_back({{"name","anticipation_gate_status"},{"description","Show annoyance gate state"},
        {"inputSchema",{{"type","object"},{"properties",{{"session_id",{{"type","string"}}}}}}}
    });
    handlers_["anticipation_gate_status"] = [this](const json& p) { return tool_anticipation_gate_status(p); };

    tools_.push_back({{"name","anticipation_record_outcome"},{"description","Record prediction outcome"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"candidate_id",{{"type","integer"}}},{"correct",{{"type","boolean"}}}
        }},{"required",{"candidate_id","correct"}}}}
    });
    handlers_["anticipation_record_outcome"] = [this](const json& p) { return tool_anticipation_record_outcome(p); };

    tools_.push_back({{"name","habit_observe"},{"description","Record trigger->response pattern"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"trigger",{{"type","string"}}},{"response",{{"type","string"}}},{"realm",{{"type","string"}}}
        }},{"required",{"trigger","response"}}}}
    });
    handlers_["habit_observe"] = [this](const json& p) { return tool_habit_observe(p); };

    tools_.push_back({{"name","habit_match"},{"description","Find matching habits"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"context",{{"type","string"}}},{"min_strength",{{"type","number"}}},{"realm",{{"type","string"}}}
        }},{"required",{"context"}}}}
    });
    handlers_["habit_match"] = [this](const json& p) { return tool_habit_match(p); };

    tools_.push_back({{"name","habit_strengthen"},{"description","Strengthen a habit"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"amount",{{"type","number"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["habit_strengthen"] = [this](const json& p) { return tool_habit_strengthen(p); };

    tools_.push_back({{"name","habit_weaken"},{"description","Weaken a habit"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"amount",{{"type","number"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["habit_weaken"] = [this](const json& p) { return tool_habit_weaken(p); };

    tools_.push_back({{"name","habit_list"},{"description","List formed habits"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"realm",{{"type","string"}}},{"min_strength",{{"type","number"}}},
            {"limit",{{"type","integer"}}},{"sort_by",{{"type","string"}}}
        }}}}
    });
    handlers_["habit_list"] = [this](const json& p) { return tool_habit_list(p); };

    tools_.push_back({{"name","profile_get"},{"description","Get user profile"},
        {"inputSchema",{{"type","object"},{"properties",{{"user_id",{{"type","string"}}}}}}}
    });
    handlers_["profile_get"] = [this](const json& p) { return tool_profile_get(p); };

    tools_.push_back({{"name","profile_update"},{"description","Update profile field"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"user_id",{{"type","string"}}},{"field",{{"type","string"}}},{"value",{{"type","string"}}}
        }},{"required",{"field","value"}}}}
    });
    handlers_["profile_update"] = [this](const json& p) { return tool_profile_update(p); };

    tools_.push_back({{"name","profile_observe"},{"description","Record user observation"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"observation_type",{{"type","string"}}},{"value",{{"type","string"}}},{"user_id",{{"type","string"}}}
        }},{"required",{"observation_type","value"}}}}
    });
    handlers_["profile_observe"] = [this](const json& p) { return tool_profile_observe(p); };

    tools_.push_back({{"name","goal_set"},{"description","Define a long-term goal"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"title",{{"type","string"}}},{"description",{{"type","string"}}},
            {"milestones",{{"type","string"}}},{"deadline",{{"type","integer"}}},{"realm",{{"type","string"}}}
        }},{"required",{"title"}}}}
    });
    handlers_["goal_set"] = [this](const json& p) { return tool_goal_set(p); };

    tools_.push_back({{"name","goal_get"},{"description","Get goal by ID"},
        {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","integer"}}}}},{"required",{"id"}}}}
    });
    handlers_["goal_get"] = [this](const json& p) { return tool_goal_get(p); };

    tools_.push_back({{"name","goal_list"},{"description","List goals"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"status",{{"type","string"}}},{"realm",{{"type","string"}}},
            {"limit",{{"type","integer"}}},{"sort_by",{{"type","string"}}}
        }}}}
    });
    handlers_["goal_list"] = [this](const json& p) { return tool_goal_list(p); };

    tools_.push_back({{"name","goal_progress"},{"description","Update goal progress"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"progress",{{"type","number"}}},{"milestone",{{"type","string"}}}
        }},{"required",{"id","progress"}}}}
    });
    handlers_["goal_progress"] = [this](const json& p) { return tool_goal_progress(p); };

    tools_.push_back({{"name","goal_complete"},{"description","Mark goal completed"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"outcome",{{"type","string"}}}
        }},{"required",{"id","outcome"}}}}
    });
    handlers_["goal_complete"] = [this](const json& p) { return tool_goal_complete(p); };

    tools_.push_back({{"name","calibration_record"},{"description","Record prediction outcome"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"domain",{{"type","string"}}},{"success",{{"type","boolean"}}}
        }},{"required",{"domain","success"}}}}
    });
    handlers_["calibration_record"] = [this](const json& p) { return tool_calibration_record(p); };

    tools_.push_back({{"name","calibration_score"},{"description","Get accuracy score"},
        {"inputSchema",{{"type","object"},{"properties",{{"domain",{{"type","string"}}}}}}}
    });
    handlers_["calibration_score"] = [this](const json& p) { return tool_calibration_score(p); };

    // Narrative
    tools_.push_back({{"name","narrative_status"},{"description","Get work mode and segment summary"},
        {"inputSchema",{{"type","object"},{"properties",{{"session_id",{{"type","string"}}}}}}}
    });
    handlers_["narrative_status"] = [this](const json& p) { return tool_narrative_status(p); };

    tools_.push_back({{"name","narrative_log"},{"description","Append event to session log"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"session_id",{{"type","string"}}},{"kind",{{"type","string"}}},{"summary",{{"type","string"}}},
            {"tool_name",{{"type","string"}}},{"success",{{"type","boolean"}}},
            {"payload",{{"type","string"}}},{"files_mentioned",{{"type","string"}}}
        }},{"required",{"kind","summary"}}}}
    });
    handlers_["narrative_log"] = [this](const json& p) { return tool_narrative_log(p); };

    tools_.push_back({{"name","narrative_history"},{"description","Get work mode segment history"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"session_id",{{"type","string"}}},{"limit",{{"type","integer"}}}
        }}}}
    });
    handlers_["narrative_history"] = [this](const json& p) { return tool_narrative_history(p); };

    // Sadhana
    tools_.push_back({{"name","sadhana_start"},{"description","Create and start an autonomous agent"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"goal",{{"type","string"}}},{"brain_provider",{{"type","string"}}},
            {"brain_model",{{"type","string"}}},{"interval_seconds",{{"type","integer"}}},
            {"max_turns",{{"type","integer"}}},{"realm",{{"type","string"}}},
            {"goal_dsl",{{"type","object"}}}
        }},{"required",{"goal"}}}}
    });
    handlers_["sadhana_start"] = [this](const json& p) { return tool_sadhana_start(p); };

    tools_.push_back({{"name","sadhana_pause"},{"description","Pause a sadhana"},
        {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","integer"}}}}},{"required",{"id"}}}}
    });
    handlers_["sadhana_pause"] = [this](const json& p) { return tool_sadhana_pause(p); };

    tools_.push_back({{"name","sadhana_resume"},{"description","Resume a paused sadhana"},
        {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","integer"}}}}},{"required",{"id"}}}}
    });
    handlers_["sadhana_resume"] = [this](const json& p) { return tool_sadhana_resume(p); };

    tools_.push_back({{"name","sadhana_stop"},{"description","Stop a sadhana"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"success",{{"type","boolean"}}},{"reason",{{"type","string"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["sadhana_stop"] = [this](const json& p) { return tool_sadhana_stop(p); };

    tools_.push_back({{"name","sadhana_status"},{"description","Get sadhana status"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"history_limit",{{"type","integer"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["sadhana_status"] = [this](const json& p) { return tool_sadhana_status(p); };

    tools_.push_back({{"name","sadhana_list"},{"description","List sadhanas"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"state",{{"type","string"}}},{"realm",{{"type","string"}}},{"limit",{{"type","integer"}}}
        }}}}
    });
    handlers_["sadhana_list"] = [this](const json& p) { return tool_sadhana_list(p); };

    tools_.push_back({{"name","sadhana_set_model"},{"description","Change brain model"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"model",{{"type","string"}}}
        }},{"required",{"id","model"}}}}
    });
    handlers_["sadhana_set_model"] = [this](const json& p) { return tool_sadhana_set_model(p); };

    tools_.push_back({{"name","sadhana_set_goal"},{"description","Change sadhana goal"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"goal",{{"type","string"}}}
        }},{"required",{"id","goal"}}}}
    });
    handlers_["sadhana_set_goal"] = [this](const json& p) { return tool_sadhana_set_goal(p); };

    tools_.push_back({{"name","sadhana_set_interval"},{"description","Change tick interval"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"interval",{{"type","integer"}}}
        }},{"required",{"id","interval"}}}}
    });
    handlers_["sadhana_set_interval"] = [this](const json& p) { return tool_sadhana_set_interval(p); };

    tools_.push_back({{"name","sadhana_set_max_turns"},{"description","Set max turns per cycle"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"max_turns",{{"type","integer"}}}
        }},{"required",{"id","max_turns"}}}}
    });
    handlers_["sadhana_set_max_turns"] = [this](const json& p) { return tool_sadhana_set_max_turns(p); };

    tools_.push_back({{"name","sadhana_checkpoint"},{"description","Report mid-cycle checkpoint"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},
            {"status",{{"type","string"},{"enum",{"progressed","achieved","blocked"}}}},
            {"summary",{{"type","string"}}}
        }},{"required",{"id","status","summary"}}}}
    });
    handlers_["sadhana_checkpoint"] = [this](const json& p) { return tool_sadhana_checkpoint(p); };

    // Dream
    tools_.push_back({{"name","dream_start"},{"description","Start an autonomous dream"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"topic",{{"type","string"}}},{"realm",{{"type","string"}}},{"publish_path",{{"type","string"}}},
            {"brain_provider",{{"type","string"},{"description","Brain provider: claude or local"}}},
            {"brain_model",{{"type","string"},{"description","Model name, e.g. gemma4:26b or sonnet"}}}
        }},{"required",{"topic"}}}}
    });
    handlers_["dream_start"] = [this](const json& p) { return tool_dream_start(p); };

    tools_.push_back({{"name","dream_wander"},{"description","Auto-select topic and dream"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"realm",{{"type","string"}}},{"publish_path",{{"type","string"}}}
        }}}}
    });
    handlers_["dream_wander"] = [this](const json& p) { return tool_dream_wander(p); };

    tools_.push_back({{"name","dream_cancel"},{"description","Cancel a dream"},
        {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","integer"}}}}},{"required",{"id"}}}}
    });
    handlers_["dream_cancel"] = [this](const json& p) { return tool_dream_cancel(p); };

    tools_.push_back({{"name","dream_force_woke"},{"description","Force stuck dream to woke"},
        {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","integer"}}}}},{"required",{"id"}}}}
    });
    handlers_["dream_force_woke"] = [this](const json& p) { return tool_dream_force_woke(p); };

    tools_.push_back({{"name","dream_list"},{"description","List recent dreams"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"limit",{{"type","integer"}}},{"realm",{{"type","string"}}}
        }}}}
    });
    handlers_["dream_list"] = [this](const json& p) { return tool_dream_list(p); };

    tools_.push_back({{"name","dream_status"},{"description","Get dream details"},
        {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","integer"}}}}},{"required",{"id"}}}}
    });
    handlers_["dream_status"] = [this](const json& p) { return tool_dream_status(p); };

    tools_.push_back({{"name","think_wander"},{"description","Trigger internal memory synthesis"},
        {"inputSchema",{{"type","object"},{"properties",{{"realm",{{"type","string"}}}}}}}
    });
    handlers_["think_wander"] = [this](const json& p) { return tool_think_wander(p); };

    tools_.push_back({{"name","impl_start"},{"description","Start self-improvement implementation sadhana"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"repo",{{"type","string"}}},{"interval_seconds",{{"type","integer"}}},
            {"max_turns",{{"type","integer"}}},{"realm",{{"type","string"}}}
        }}}}
    });
    handlers_["impl_start"] = [this](const json& p) { return tool_impl_start(p); };

    // Context Repository
    tools_.push_back({{"name","memory_history"},{"description","View memory version history"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"limit",{{"type","integer"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["memory_history"] = [this](const json& p) { return tool_memory_history(p); };

    tools_.push_back({{"name","memory_revert"},{"description","Revert memory to previous version"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"version",{{"type","integer"}}},{"reason",{{"type","string"}}}
        }},{"required",{"id","version"}}}}
    });
    handlers_["memory_revert"] = [this](const json& p) { return tool_memory_revert(p); };

    tools_.push_back({{"name","pin_memory"},{"description","Pin memory to keep hot"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"reason",{{"type","string"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["pin_memory"] = [this](const json& p) { return tool_pin_memory(p); };

    tools_.push_back({{"name","unpin_memory"},{"description","Unpin a memory"},
        {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","integer"}}}}},{"required",{"id"}}}}
    });
    handlers_["unpin_memory"] = [this](const json& p) { return tool_unpin_memory(p); };

    tools_.push_back({{"name","list_pinned"},{"description","List pinned memories"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"realm",{{"type","string"}}},{"limit",{{"type","integer"}}}
        }}}}
    });
    handlers_["list_pinned"] = [this](const json& p) { return tool_list_pinned(p); };

    tools_.push_back({{"name","memory_lock"},{"description","Acquire memory lock"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"holder_id",{{"type","string"}}},
            {"holder_type",{{"type","string"}}},{"duration",{{"type","integer"}}}
        }},{"required",{"id","holder_id"}}}}
    });
    handlers_["memory_lock"] = [this](const json& p) { return tool_memory_lock(p); };

    tools_.push_back({{"name","memory_unlock"},{"description","Release memory lock"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"holder_id",{{"type","string"}}}
        }},{"required",{"id","holder_id"}}}}
    });
    handlers_["memory_unlock"] = [this](const json& p) { return tool_memory_unlock(p); };

    tools_.push_back({{"name","memory_lock_status"},{"description","Check lock status"},
        {"inputSchema",{{"type","object"},{"properties",{{"id",{{"type","integer"}}}}},{"required",{"id"}}}}
    });
    handlers_["memory_lock_status"] = [this](const json& p) { return tool_memory_lock_status(p); };

    tools_.push_back({{"name","propose_change"},{"description","Propose change to memory"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"content",{{"type","string"}}},{"proposed_by",{{"type","string"}}}
        }},{"required",{"id","content","proposed_by"}}}}
    });
    handlers_["propose_change"] = [this](const json& p) { return tool_propose_change(p); };

    tools_.push_back({{"name","list_merge_queue"},{"description","List pending merge proposals"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"status",{{"type","string"}}},{"limit",{{"type","integer"}}}
        }}}}
    });
    handlers_["list_merge_queue"] = [this](const json& p) { return tool_list_merge_queue(p); };

    tools_.push_back({{"name","resolve_merge"},{"description","Resolve merge proposal"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"merge_id",{{"type","integer"}}},{"resolution",{{"type","string"}}},{"status",{{"type","string"}}}
        }},{"required",{"merge_id","status"}}}}
    });
    handlers_["resolve_merge"] = [this](const json& p) { return tool_resolve_merge(p); };

    // File Time Machine
    tools_.push_back({{"name","file_timeline"},{"description","Show files modified in time range"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query",{{"type","string"}}},{"session_id",{{"type","string"}}},
            {"path",{{"type","string"}}},{"file_pattern",{{"type","string"}}},
            {"limit",{{"type","integer"}}},{"cross_session",{{"type","boolean"}}}
        }}}}
    });
    handlers_["file_timeline"] = [this](const json& p) { return tool_file_timeline(p); };

    tools_.push_back({{"name","file_at_time"},{"description","Get file content at time (stub)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"file_path",{{"type","string"}}},{"time",{{"type","string"}}},
            {"session_id",{{"type","string"}}},{"show_diff",{{"type","boolean"}}}
        }},{"required",{"file_path"}}}}
    });
    handlers_["file_at_time"] = [this](const json& p) { return tool_file_at_time(p); };

    tools_.push_back({{"name","file_restore"},{"description","Restore file version (stub)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"file_path",{{"type","string"}}},{"version_id",{{"type","integer"}}},{"preview",{{"type","boolean"}}}
        }},{"required",{"file_path"}}}}
    });
    handlers_["file_restore"] = [this](const json& p) { return tool_file_restore(p); };

    tools_.push_back({{"name","file_index_session"},{"description","Index file-history from session"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"session_id",{{"type","string"}}},{"force",{{"type","boolean"}}}
        }},{"required",{"session_id"}}}}
    });
    handlers_["file_index_session"] = [this](const json& p) { return tool_file_index_session(p); };

    tools_.push_back({{"name","file_index_all"},{"description","Index all sessions for cross-session timeline"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"force",{{"type","boolean"}}},{"limit",{{"type","integer"}}}
        }}}}
    });
    handlers_["file_index_all"] = [this](const json& p) { return tool_file_index_all(p); };

    // Misc
    tools_.push_back({{"name","learn_outcome"},{"description","Record memory usage outcome"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"memory_id",{{"type","string"}}},
            {"outcome",{{"type","string"},{"enum",{"positive","negative","neutral"}}}},
            {"context",{{"type","string"}}}
        }},{"required",{"memory_id","outcome"}}}}
    });
    handlers_["learn_outcome"] = [this](const json& p) { return tool_learn_outcome(p); };

    tools_.push_back({{"name","log_exposure"},{"description","Log memory exposure (SUS)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"session_id",{{"type","string"}}},{"turn_id",{{"type","integer"}}},
            {"hook_type",{{"type","string"}}},{"memory_ids",{{"type","array"},{"items",{{"type","integer"}}}}},
            {"ranks",{{"type","array"},{"items",{{"type","integer"}}}}},
            {"resonance_scores",{{"type","array"},{"items",{{"type","number"}}}}}
        }},{"required",{"session_id","turn_id","hook_type","memory_ids"}}}}
    });
    handlers_["log_exposure"] = [this](const json& p) { return tool_log_exposure(p); };

    tools_.push_back({{"name","get_sus_metrics"},{"description","Get Soul Utility Score metrics"},
        {"inputSchema",{{"type","object"},{"properties",{{"days",{{"type","integer"}}}}}}}
    });
    handlers_["get_sus_metrics"] = [this](const json& p) { return tool_get_sus_metrics(p); };

    tools_.push_back({{"name","episode_cluster_status"},{"description","Find similar episode clusters"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"similarity_threshold",{{"type","number"}}},{"min_occurrences",{{"type","integer"}}}
        }}}}
    });
    handlers_["episode_cluster_status"] = [this](const json& p) { return tool_episode_cluster_status(p); };

    tools_.push_back({{"name","insight_promote"},{"description","Promote memory to global"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"reason",{{"type","string"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["insight_promote"] = [this](const json& p) { return tool_insight_promote(p); };

    tools_.push_back({{"name","insight_global"},{"description","List global insights"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"limit",{{"type","integer"}}},{"tag",{{"type","string"}}}
        }}}}
    });
    handlers_["insight_global"] = [this](const json& p) { return tool_insight_global(p); };

    tools_.push_back({{"name","list_by_aspect"},{"description","List memories by semantic aspect"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"aspect",{{"type","string"}}},{"limit",{{"type","integer"}}},{"min_confidence",{{"type","number"}}}
        }},{"required",{"aspect"}}}}
    });
    handlers_["list_by_aspect"] = [this](const json& p) { return tool_list_by_aspect(p); };

    tools_.push_back({{"name","list_aspects"},{"description","List available semantic aspects"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["list_aspects"] = [this](const json& p) { return tool_list_aspects(p); };

    tools_.push_back({{"name","query_claims"},{"description","Query semantic claims"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"subject",{{"type","string"}}},{"predicate",{{"type","string"}}},
            {"scope",{{"type","string"}}},{"active_only",{{"type","boolean"}}},{"limit",{{"type","integer"}}}
        }}}}
    });
    handlers_["query_claims"] = [this](const json& p) { return tool_query_claims(p); };

    tools_.push_back({{"name","get_policies"},{"description","Get active policies"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"scope",{{"type","string"}}},{"type",{{"type","string"}}},{"limit",{{"type","integer"}}}
        }}}}
    });
    handlers_["get_policies"] = [this](const json& p) { return tool_get_policies(p); };

    tools_.push_back({{"name","get_entities"},{"description","Get tracked entities"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"type",{{"type","string"}}},{"limit",{{"type","integer"}}}
        }}}}
    });
    handlers_["get_entities"] = [this](const json& p) { return tool_get_entities(p); };

    tools_.push_back({{"name","get_relationship_events"},{"description","Get relationship events"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"event_type",{{"type","string"}}},{"session_id",{{"type","string"}}},{"limit",{{"type","integer"}}}
        }}}}
    });
    handlers_["get_relationship_events"] = [this](const json& p) { return tool_get_relationship_events(p); };

    // ── Context compaction ─────────────────────────────────────────────
    tools_.push_back({{"name","ingest_source"},
        {"description","Ingest external content (URL, file, directory) into memory via SSL distillation"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"source",{{"type","string"},{"description","URL, file path, or directory path"}}},
            {"realm",{{"type","string"},{"description","Target realm (default: brahman)"}}},
            {"type",{{"type","string"},{"description","Source type: auto|url|file|directory (default: auto)"}}},
            {"model",{{"type","string"},{"description","LLM model (default: gemma4:26b)"}}},
            {"endpoint",{{"type","string"},{"description","OpenAI-compatible endpoint (auto-discovered if empty)"}}},
            {"max_chunks",{{"type","integer"},{"description","Max chunks to process (default: 30)"}}}
        }},{"required",{"source"}}}}
    });
    handlers_["ingest_source"] = [this](const json& p) { return tool_ingest_source(p); };

    // ── Tier 2: Wiki export ────────────────────────────────────────────
    tools_.push_back({{"name","wiki_export"},
        {"description","Export memories as Obsidian-compatible .md wiki with backlinks"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"output_dir",{{"type","string"},{"description","Output directory (default: ~/.claude/wiki/)"}}},
            {"realm",{{"type","string"},{"description","Filter to specific realm (default: all)"}}},
            {"max_memories",{{"type","integer"},{"description","Max memories per realm (default: 5000)"}}}
        }}}}
    });
    handlers_["wiki_export"] = [this](const json& p) { return tool_wiki_export(p); };

    // ── Tier 3: Health-check sadhana ───────────────────────────────────
    tools_.push_back({{"name","health_check_start"},
        {"description","Start autonomous health-check sadhana that monitors memory quality, dedup ratio, and embedding coverage"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"interval_seconds",{{"type","integer"},{"description","Check interval in seconds (default: 3600)"}}},
            {"realm",{{"type","string"},{"description","Realm to monitor (default: brahman)"}}},
            {"max_turns",{{"type","integer"},{"description","Max check cycles (default: 0 = unlimited)"}}}
        }}}}
    });
    handlers_["health_check_start"] = [this](const json& p) { return tool_health_check_start(p); };

    // ── Tier 4: Export training pairs ──────────────────────────────────
    tools_.push_back({{"name","export_training_pairs"},
        {"description","Export query-passage pairs as JSONL for BGE embedding fine-tuning"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"output_path",{{"type","string"},{"description","Output JSONL path (default: ~/.claude/training/pairs.jsonl)"}}},
            {"realm",{{"type","string"},{"description","Filter to specific realm (default: all)"}}},
            {"max_pairs",{{"type","integer"},{"description","Max pairs to export (default: 10000)"}}},
            {"min_confidence",{{"type","number"},{"description","Min confidence threshold (default: 0.5)"}}},
            {"include_negatives",{{"type","boolean"},{"description","Generate hard negatives (default: true)"}}}
        }}}}
    });
    handlers_["export_training_pairs"] = [this](const json& p) { return tool_export_training_pairs(p); };

    // ── Soul REPL Session Store ──────────────────────────────────────────
}

} // namespace chitta
