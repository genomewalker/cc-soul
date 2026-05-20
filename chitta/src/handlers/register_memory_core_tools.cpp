// register_memory_core_tools — chunk extracted from register_tools() so editing
// tool metadata only retemplates one chunk at a time.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

void FieldRpcHandler::register_memory_core_tools() {
    tools_.push_back({
        {"name", "remember"},
        {"description", "Store text in memory with optional tags and realm"},
        {"inputSchema", {{"type", "object"},
            {"properties", {
                {"content", {{"type", "string"}, {"description", "Text to remember"}}},
                {"type", {{"type", "string"}, {"description", "Node type (wisdom, insight, signal, episode)"}}},
                {"confidence", {{"type", "number"}, {"description", "Initial confidence 0-1 (default: 0.8)"}}},
                {"tags", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Optional tags"}}},
                {"realm", {{"type", "string"}, {"description", "Primary realm (default: brahman)"}}},
                {"visibility", {{"type", "integer"}, {"description", "0=Private, 1=Shared, 2=Global (default: 0)"}}},
                {"shared_realms", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Additional realms"}}}
            }}, {"required", {"content"}}
        }}
    });
    handlers_["remember"] = [this](const json& p) { return tool_remember(p); };

    tools_.push_back({
        {"name", "recall"},
        {"description", "Search memory by semantic similarity with realm filtering"},
        {"inputSchema", {{"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}, {"description", "Search query"}}},
                {"limit", {{"type", "integer"}, {"description", "Max results (default 10)"}}},
                {"min_confidence", {{"type", "number"}, {"description", "Minimum confidence threshold"}}},
                {"tag", {{"type", "string"}, {"description", "Filter by tag"}}},
                {"realm", {{"type", "string"}, {"description", "Filter by realm"}}},
                {"include_global", {{"type", "boolean"}, {"description", "Include global memories (default: true)"}}},
                {"separation_mode", {{"type", "boolean"}, {"description", "Diverse results via MMR (default: false)"}}},
                {"gwt_mode", {{"type", "boolean"}, {"description", "Global Workspace Theory mode (default: false)"}}},
                {"explain", {{"type", "boolean"}, {"description", "Include score decomposition per hit (default: false)"}}}
            }}, {"required", {"query"}}
        }}
    });
    handlers_["recall"] = [this](const json& p) { return tool_recall(p); };

    tools_.push_back({
        {"name", "recall_temporal"},
        {"description", "Search memories within a time window (defaults to last 7 days)"},
        {"inputSchema", {{"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}, {"description", "Optional semantic search query"}}},
                {"start", {{"type", "string"}, {"description", "Start date (ISO8601 or YYYY-MM-DD)"}}},
                {"end", {{"type", "string"}, {"description", "End date"}}},
                {"limit", {{"type", "integer"}, {"description", "Max results (default 20)"}}},
                {"realm", {{"type", "string"}, {"description", "Filter by realm"}}},
                {"include_global", {{"type", "boolean"}, {"description", "Include global memories"}}}
            }}
        }}
    });
    handlers_["recall_temporal"] = [this](const json& p) { return tool_recall_temporal(p); };

    tools_.push_back({{"name","recall_keyword"},{"description","BM25 keyword search"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query",{{"type","string"},{"description","Search query"}}},
            {"limit",{{"type","integer"},{"description","Max results (default 10)"}}},
            {"explain",{{"type","boolean"},{"description","Include score decomposition per hit (default: false)"}}}
        }},{"required",{"query"}}}}
    });
    handlers_["recall_keyword"] = [this](const json& p) { return tool_recall_keyword(p); };

    // Exploration primitives
    tools_.push_back({{"name","explore_recall"},{"description","Lightweight recall - titles/scores only"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query",{{"type","string"},{"description","Search query"}}},
            {"limit",{{"type","integer"},{"description","Max results (default 10)"}}}
        }},{"required",{"query"}}}}
    });
    handlers_["explore_recall"] = [this](const json& p) { return tool_explore_recall(p); };

    tools_.push_back({{"name","explore_peek"},{"description","Get summary of a memory (first 200 chars)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"},{"description","Memory ID"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["explore_peek"] = [this](const json& p) { return tool_explore_peek(p); };

    tools_.push_back({{"name","explore_expand"},{"description","Get full content of a memory"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"},{"description","Memory ID"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["explore_expand"] = [this](const json& p) { return tool_explore_expand(p); };

    tools_.push_back({{"name","explore_neighbors"},{"description","Get nodes connected via triplets"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"node",{{"type","string"},{"description","Node name"}}},
            {"direction",{{"type","string"},{"description","outgoing, incoming, or both"}}}
        }},{"required",{"node"}}}}
    });
    handlers_["explore_neighbors"] = [this](const json& p) { return tool_explore_neighbors(p); };

    // Graph tools
    tools_.push_back({{"name","connect"},{"description","Create a triplet relationship"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"subject",{{"type","string"},{"description","Subject entity"}}},
            {"predicate",{{"type","string"},{"description","Relationship type"}}},
            {"object",{{"type","string"},{"description","Object entity"}}}
        }},{"required",{"subject","predicate","object"}}}}
    });
    handlers_["connect"] = [this](const json& p) { return tool_connect(p); };

    tools_.push_back({{"name","query_graph"},{"description","Query triplets by subject or object"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"subject",{{"type","string"},{"description","Query by subject"}}},
            {"object",{{"type","string"},{"description","Query by object"}}}
        }}}}
    });
    handlers_["query_graph"] = [this](const json& p) { return tool_query(p); };

    tools_.push_back({{"name","query_triplets_temporal"},{"description","Query triplets at a point in time"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"subject",{{"type","string"}}},{"predicate",{{"type","string"}}},
            {"object",{{"type","string"}}},
            {"at_date",{{"type","string"},{"description","YYYY-MM-DD"}}},
            {"limit",{{"type","integer"}}}
        }}}}
    });
    handlers_["query_triplets_temporal"] = [this](const json& p) { return tool_query_triplets_temporal(p); };

    tools_.push_back({{"name","triplet_history"},{"description","Get history of a subject-predicate relationship"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"subject",{{"type","string"}}},{"predicate",{{"type","string"}}},
            {"limit",{{"type","integer"}}}
        }},{"required",{"subject","predicate"}}}}
    });
    handlers_["triplet_history"] = [this](const json& p) { return tool_triplet_history(p); };

    tools_.push_back({{"name","triplet_query_as_of"},{"description","Query triplets for a subject valid at a given world timestamp, excluding superseded entries"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"subject",{{"type","string"},{"description","Subject node to query"}}},
            {"world_ms",{{"type","integer"},{"description","World-time epoch ms (default: now)"}}}
        }},{"required",{"subject"}}}}
    });
    handlers_["triplet_query_as_of"] = [this](const json& p) { return tool_triplet_query_as_of(p); };

    tools_.push_back({{"name","triplet_supersede"},{"description","Mark one triplet as superseded by another (bi-temporal update)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"old_id",{{"type","integer"},{"description","Triplet ID being superseded"}}},
            {"new_id",{{"type","integer"},{"description","Replacing triplet ID"}}},
            {"at_ms",{{"type","integer"},{"description","Ingestion-time of supersession (default: now)"}}}
        }},{"required",{"old_id","new_id"}}}}
    });
    handlers_["triplet_supersede"] = [this](const json& p) { return tool_triplet_supersede(p); };

    tools_.push_back({{"name","graph_traverse"},{"description","BFS graph traversal from a start node over triplet edges"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"start",{{"type","string"},{"description","Starting node"}}},
            {"edge_types",{{"type","array"},{"items",{{"type","string"}}},{"description","Predicate filter (empty = all)"}}},
            {"max_hops",{{"type","integer"},{"description","Max BFS depth (default 3)"}}},
            {"max_results",{{"type","integer"},{"description","Max nodes returned (default 50)"}}},
            {"direction",{{"type","string"},{"enum",{"outgoing","incoming","both"}},{"description","Edge direction (default outgoing)"}}}
        }},{"required",{"start"}}}}
    });
    handlers_["graph_traverse"] = [this](const json& p) { return tool_graph_traverse(p); };

    tools_.push_back({{"name","graph_pagerank"},{"description","Personalized PageRank over the triplet graph"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"seeds",{{"type","array"},{"items",{{"type","string"}}},{"description","Seed nodes"}}},
            {"edge_types",{{"type","array"},{"items",{{"type","string"}}},{"description","Predicate filter (empty = all)"}}},
            {"damping",{{"type","number"},{"description","Damping factor (default 0.85)"}}},
            {"iterations",{{"type","integer"},{"description","PPR iterations (default 20)"}}},
            {"top_k",{{"type","integer"},{"description","Nodes to return (default 20)"}}}
        }},{"required",{"seeds"}}}}
    });
    handlers_["graph_pagerank"] = [this](const json& p) { return tool_graph_pagerank(p); };

    tools_.push_back({{"name","connect_temporal"},{"description","Create triplet with temporal validity"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"subject",{{"type","string"}}},{"predicate",{{"type","string"}}},{"object",{{"type","string"}}},
            {"valid_from",{{"type","string"}}},{"valid_to",{{"type","string"}}},
            {"context_date",{{"type","string"}}}
        }},{"required",{"subject","predicate","object"}}}}
    });
    handlers_["connect_temporal"] = [this](const json& p) { return tool_connect_temporal(p); };

    // Strength/forget
    tools_.push_back({{"name","strengthen"},{"description","Increase confidence of a memory"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"},{"description","Node ID"}}},
            {"amount",{{"type","number"},{"description","Amount (default 0.1)"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["strengthen"] = [this](const json& p) { return tool_strengthen(p); };

    tools_.push_back({{"name","weaken"},{"description","Decrease confidence of a memory"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"}}},{"amount",{{"type","number"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["weaken"] = [this](const json& p) { return tool_weaken(p); };

    tools_.push_back({{"name","forget"},{"description","Remove a memory"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"},{"description","Node ID to forget"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["forget"] = [this](const json& p) { return tool_forget(p); };

    tools_.push_back({{"name","batch_forget"},{"description","Delete multiple nodes by ID"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"ids",{{"type","array"},{"items",{{"type","string"}}}}},
            {"pattern",{{"type","string"}}}
        }}}}
    });
    handlers_["batch_forget"] = [this](const json& p) { return tool_batch_forget(p); };

    // Observe/Grow
    tools_.push_back({{"name","observe"},{"description","Store an observation/learning (SSL v0.4)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"category",{{"type","string"}}},{"title",{{"type","string"}}},
            {"content",{{"type","string"}}},{"tags",{{"type","string"}}},
            {"confidence",{{"type","number"}}},
            {"valence",{{"type","number"},{"description","Affect valence: -1.0 to +1.0"}}},
            {"arousal",{{"type","number"},{"description","Affect arousal: 0.0 to 1.0"}}},
            {"flags",{{"type","string"},{"description","Structural flags: ORIGIN,CORE,PIVOT,GENESIS,TURNING"}}},
            {"refs",{{"type","string"},{"description","Cross-references: comma-separated tag names or memory IDs"}}},
            {"granularity",{{"type","integer"},{"description","SSL v0.4 granularity tier: 0=atom,1=episode,2=claim,3=operator,4=boundary"}}},
            {"derivation",{{"type","string"},{"description","SSL v0.4 <=@ provenance: comma-separated source memory IDs this was abstracted from (required at G:1+)"}}},
            {"source_loc",{{"type","string"},{"description","SSL v0.4 src: external source grounding, e.g. file:line or doc section"}}}
        }},{"required",{"title","content"}}}}
    });
    handlers_["observe"] = [this](const json& p) { return tool_observe(p); };

    tools_.push_back({{"name","full_resonate"},{"description","Semantic search with full context"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query",{{"type","string"}}},{"k",{{"type","integer"}}},
            {"realm",{{"type","string"}}},{"include_global",{{"type","boolean"}}},
            {"exclude_kinds",{{"type","array"},{"items",{{"type","string"}}}}},
            {"partnership_only",{{"type","boolean"}}},{"separation_mode",{{"type","boolean"}}}
        }},{"required",{"query"}}}}
    });
    handlers_["full_resonate"] = [this](const json& p) { return tool_full_resonate(p); };

    tools_.push_back({{"name","grow"},{"description","Add wisdom, belief, failure, aspiration, or dream"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"type",{{"type","string"}}},{"content",{{"type","string"}}},
            {"title",{{"type","string"}}},{"tags",{{"type","string"}}}
        }},{"required",{"type","content"}}}}
    });
    handlers_["grow"] = [this](const json& p) { return tool_grow(p); };

    tools_.push_back({{"name","get"},{"description","Get a node by ID"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["get"] = [this](const json& p) { return tool_get(p); };

    tools_.push_back({{"name","expand_memory"},{"description","Expand a memory to full hierarchical context"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"}}},{"depth",{{"type","integer"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["expand_memory"] = [this](const json& p) { return tool_expand_memory(p); };

    tools_.push_back({{"name","update"},{"description","Update node content"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"}}},{"content",{{"type","string"}}}
        }},{"required",{"id","content"}}}}
    });
    handlers_["update"] = [this](const json& p) { return tool_update(p); };

    tools_.push_back({{"name","query"},{"description","Query triplets with flexible filters"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"subject",{{"type","string"}}},{"predicate",{{"type","string"}}},
            {"object",{{"type","string"}}}
        }}}}
    });
    handlers_["query"] = [this](const json& p) { return tool_query(p); };

    tools_.push_back({{"name","tag"},{"description","Add or remove tags from a node"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"}}},{"add",{{"type","string"}}},{"remove",{{"type","string"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["tag"] = [this](const json& p) { return tool_tag(p); };

    tools_.push_back({{"name","set_affect"},{"description","Set affect dimensions (valence, arousal) on a memory"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"}}},
            {"valence",{{"type","number"},{"description","Emotional valence: -1.0 to +1.0"}}},
            {"arousal",{{"type","number"},{"description","Emotional arousal: 0.0 to 1.0"}}}
        }},{"required",{"id","valence","arousal"}}}}
    });
    handlers_["set_affect"] = [this](const json& p) { return tool_set_affect(p); };

    // ── Code Intelligence tools ─────────────────────────────────────────
    tools_.push_back({{"name","list_by_status"},{"description","List memories filtered by lifecycle status (active/superseded/contradicted/archived)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"status",{{"type","string"},{"description","Filter: active, superseded, contradicted, archived, or all"},{"default","superseded"}}},
            {"limit",{{"type","integer"},{"description","Max results"},{"default",50}}},
            {"realm",{{"type","string"},{"description","Filter by realm"}}}
        }},{"required",json::array()}}}
    });
    handlers_["list_by_status"] = [this](const json& p) { return tool_list_by_status(p); };

    tools_.push_back({{"name","list_memories_brief"},{"description","Fast memory index"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"limit",{{"type","integer"}}},{"realm",{{"type","string"}}},
            {"kind",{{"type","string"}}},{"priority_tier",{{"type","integer"}}}
        }}}}
    });
    handlers_["list_memories_brief"] = [this](const json& p) { return tool_list_memories_brief(p); };

    tools_.push_back({{"name","set_priority_tier"},{"description","Set memory priority tier"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"memory_id",{{"type","integer"}}},{"tier",{{"type","integer"}}}
        }},{"required",{"memory_id","tier"}}}}
    });
    handlers_["set_priority_tier"] = [this](const json& p) { return tool_set_priority_tier(p); };

    tools_.push_back({{"name","recall_by_priority"},{"description","Budget-aware recall"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query",{{"type","string"}}},{"budget_tokens",{{"type","integer"}}},
            {"realm",{{"type","string"}}},{"include_global",{{"type","boolean"}}}
        }}}}
    });
    handlers_["recall_by_priority"] = [this](const json& p) { return tool_recall_by_priority(p); };

    tools_.push_back({{"name","set_memory_type"},{"description","Set memory semantic type"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"memory_id",{{"type","integer"}}},{"type",{{"type","string"}}}
        }},{"required",{"memory_id","type"}}}}
    });
    handlers_["set_memory_type"] = [this](const json& p) { return tool_set_memory_type(p); };

    tools_.push_back({{"name","memory_type_stats"},{"description","Get memory type statistics"},
        {"inputSchema",{{"type","object"},{"properties",{{"realm",{{"type","string"}}}}}}}
    });
    handlers_["memory_type_stats"] = [this](const json& p) { return tool_memory_type_stats(p); };

    tools_.push_back({{"name","forget_kind"},{"description","Bulk-delete all memories of a given kind (e.g. 'habit'). Optionally filter by realm."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"kind",{{"type","string"},{"description","Memory kind to delete (e.g. habit, unknown)"}}},
            {"realm",{{"type","string"},{"description","Optional realm filter"}}},
            {"limit",{{"type","integer"},{"description","Max to delete (default 5000)"}}}
        }},{"required",{"kind"}}}}});
    handlers_["forget_kind"] = [this](const json& p) { return tool_forget_kind(p); };

    tools_.push_back({{"name","smart_recall"},{"description","Intelligent memory recall with hierarchical expansion"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query",{{"type","string"}}},{"limit",{{"type","integer"}}},
            {"expand_top",{{"type","integer"}}},{"realm",{{"type","string"}}},
            {"include_global",{{"type","boolean"}}},
            {"separation_mode",{{"type","boolean"}}},{"gwt_mode",{{"type","boolean"}}}
        }},{"required",{"query"}}}}
    });
    handlers_["smart_recall"] = [this](const json& p) { return tool_smart_recall(p); };

    tools_.push_back({{"name","hybrid_recall"},{"description","Combined vector + BM25 + graph recall"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query",{{"type","string"}}},{"limit",{{"type","integer"}}},
            {"tag",{{"type","string"}}},{"realm",{{"type","string"}}},
            {"vector_weight",{{"type","number"}}},{"bm25_weight",{{"type","number"}}},
            {"graph_weight",{{"type","number"}}},{"recency_weight",{{"type","number"}}},
            {"explain",{{"type","boolean"},{"description","Include score decomposition per hit (default: false)"}}}
        }},{"required",{"query"}}}}
    });
    handlers_["hybrid_recall"] = [this](const json& p) { return tool_hybrid_recall(p); };

    tools_.push_back({{"name","recall_session"},{"description","Session-level recall: groups chunk evidence by source session using noisy-OR aggregation. Returns ranked sessions with best evidence."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query",{{"type","string"},{"description","Natural language query"}}},
            {"limit",{{"type","integer"},{"description","Max sessions to return (default 10)"}}},
            {"realm",{{"type","string"},{"description","Realm filter (optional)"}}}
        }},{"required",{"query"}}}}
    });
    handlers_["recall_session"] = [this](const json& p) { return tool_recall_session(p); };

    tools_.push_back({{"name","recall_spreading"},{"description","Retrieve memories via entity graph spreading activation"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query",{{"type","string"},{"description","Query; entity seeds extracted automatically"}}},
            {"limit",{{"type","integer"},{"description","Max results (default 10)"}}},
            {"realm",{{"type","string"},{"description","Realm filter (optional)"}}}
        }},{"required",{"query"}}}}
    });
    handlers_["recall_spreading"] = [this](const json& p) { return tool_recall_spreading(p); };

    tools_.push_back({{"name","structured_recall"},{"description","Three-lens recall: facts, context, and temporal agents merged for high-fidelity retrieval"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query",{{"type","string"}}},{"limit",{{"type","integer"}}},
            {"realm",{{"type","string"}}}
        }},{"required",{"query"}}}}
    });
    handlers_["structured_recall"] = [this](const json& p) { return tool_structured_recall(p); };

    tools_.push_back({{"name","route_stats"},{"description","Show route learner status and arm configuration for smart_recall"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["route_stats"] = [this](const json& p) { return tool_route_stats(p); };

    tools_.push_back({{"name","ask"},{"description","Natural language insight query: retrieves and synthesizes memories to answer a question about the user, session, or project"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"question",{{"type","string"}}},{"limit",{{"type","integer"}}},
            {"realm",{{"type","string"}}}
        }},{"required",{"question"}}}}
    });
    handlers_["ask"] = [this](const json& p) { return tool_ask(p); };

    tools_.push_back({{"name","expand_query"},{"description","Expand query into typed variants"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query",{{"type","string"}}}
        }},{"required",{"query"}}}}
    });
    handlers_["expand_query"] = [this](const json& p) { return tool_expand_query(p); };

    // Anticipation/Habit/Profile/Goal/Calibration

    // ── CEC: Event tape + CDAWG ──────────────────────────────────────────────
    tools_.push_back({{"name","log_event"},{"description","Log a structured action event to the CEC tape and CDAWG (tool, entity, outcome: 0=success 1=fail 2=error 3=partial)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"tool",       {{"type","string"}}},
            {"entity",     {{"type","string"}}},
            {"outcome",    {{"type","integer"}}},
            {"session_id", {{"type","integer"}}},
            {"ts_ms",      {{"type","integer"}}}
        }},{"required",{"tool","entity"}}}}
    });
    handlers_["log_event"] = [this](const json& p) { return tool_log_event(p); };

    tools_.push_back({{"name","recall_last_action"},{"description","Return last k occurrences of (tool, entity) from the CEC event tape"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"tool",   {{"type","string"}}},
            {"entity", {{"type","string"}}},
            {"k",      {{"type","integer"}}}
        }},{"required",{"tool","entity"}}}}
    });
    handlers_["recall_last_action"] = [this](const json& p) { return tool_recall_last_action(p); };

    tools_.push_back({{"name","recall_failure_pattern"},{"description","Return top-k CDAWG states with high failure rates (fail_ratio > 0.6, fail_count >= 3)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"k", {{"type","integer"}}}
        }}}}
    });
    handlers_["recall_failure_pattern"] = [this](const json& p) { return tool_recall_failure_pattern(p); };

    tools_.push_back({{"name","recall_causal_antecedent"},{"description","PMI-ranked causal antecedents: what actions typically precede (tool, entity)?"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"tool",   {{"type","string"}}},
            {"entity", {{"type","string"}}},
            {"k",      {{"type","integer"}}}
        }},{"required",{"tool","entity"}}}}
    });
    handlers_["recall_causal_antecedent"] = [this](const json& p) { return tool_recall_causal_antecedent(p); };

    tools_.push_back({{"name","recall_hdcbind"},{"description","Heteroassociative HDC query: given known_role=known_val, infer query_role. Roles: tool, entity, outcome."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"known_role", {{"type","string"}}},
            {"known_val",  {{"type","string"}}},
            {"query_role", {{"type","string"}}},
            {"k",          {{"type","integer"}}}
        }},{"required",{"known_role","known_val","query_role"}}}}
    });
    handlers_["recall_hdcbind"] = [this](const json& p) { return tool_recall_hdcbind(p); };

    tools_.push_back({{"name","consolidation_pass"},{"description","Run Sequitur grammar consolidation: find frequent bigrams in EventTape and promote rules to the triplet KG (subject=rule:..., predicates: compresses/avg_outcome/support/tape_range)."},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["consolidation_pass"] = [this](const json& p) { return tool_consolidation_pass(p); };

    tools_.push_back({{"name","recall_counterfactual"},{"description","CDAWG sibling-edge counterfactual: what alternative tool/entity would have had a lower failure rate in this same context?"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"tool",    {{"type","string"}}},
            {"entity",  {{"type","string"}}},
            {"outcome", {{"type","integer"},{"description","0=success 1=fail 2=error 3=partial (default 1)"}}},
            {"k",       {{"type","integer"}}}
        }},{"required",{"tool","entity"}}}}
    });
    handlers_["recall_counterfactual"] = [this](const json& p) { return tool_recall_counterfactual(p); };

    tools_.push_back({{"name","refutation_stats"},{"description","Show Sequitur rules that are being falsified: rules whose antecedent appears but is NOT followed by the expected consequent. Returns live/refuted counts and top-k by refutation ratio."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"k", {{"type","integer"},{"description","Max rules to show (default 10)"}}}
        }}}}
    });
    handlers_["refutation_stats"] = [this](const json& p) { return tool_refutation_stats(p); };

    tools_.push_back({{"name","recall_motif_value"},{"description","Return top-k CDAWG motif states reachable from (tool, entity) ranked by Q-value: which action sequences have the highest expected success rate from this context?"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"tool",   {{"type","string"}}},
            {"entity", {{"type","string"}}},
            {"k",      {{"type","integer"},{"description","Max states to return (default 5)"}}}
        }},{"required",{"tool","entity"}}}}
    });
    handlers_["recall_motif_value"] = [this](const json& p) { return tool_recall_motif_value(p); };

    tools_.push_back({{"name","executor_flush"},{"description","Promote shadow intervention policies that have passed the 20-event / lift>0.15 gate, demote policies whose source rule is refuted, and report active policy stats. Safe to call any time; idempotent."},
        {"inputSchema",{{"type","object"},{"properties",{}}}}
    });
    handlers_["executor_flush"] = [this](const json& p) { return tool_executor_flush(p); };

    tools_.push_back({{"name","list_policies"},{"description","List CEC intervention policies (shadow and active). Each entry shows rule source, kind (OpenTask/TurnInjection/GuardPolicy), shadow event count, lift, and fire count."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"active_only", {{"type","boolean"},{"description","If true, only return active (promoted) policies"}}}
        }}}}
    });
    handlers_["list_policies"] = [this](const json& p) { return tool_list_policies(p); };

    tools_.push_back({{"name","recall_true_counterfactual"},{"description","Return decision points where (tool, entity) was explicitly considered and rejected. Uses the DecisionTape (Phase 10), not CDAWG sibling inference. Requires prior log_event_ex or log_decision calls."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"tool",    {{"type","string"}}},
            {"entity",  {{"type","string"}}},
            {"outcome", {{"type","integer"},{"description","0=success 1=fail 2=error (default 0)"}}},
            {"k",       {{"type","integer"},{"description","Max results (default 5)"}}}
        }},{"required",{"tool","entity"}}}}
    });
    handlers_["recall_true_counterfactual"] = [this](const json& p) { return tool_recall_true_counterfactual(p); };

    tools_.push_back({{"name","hypothesis_probes"},{"description","Top-k Sequitur rules ranked by expected information gain (Wilson probe_value). Maximized at p_hat=0.5 — rules the system is most uncertain about. Run consolidation_pass first to populate."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"k", {{"type","integer"},{"description","Max rules to return (default 10)"}}}
        }}}}
    });
    handlers_["hypothesis_probes"] = [this](const json& p) { return tool_hypothesis_probes(p); };
    handlers_["turiya_status"]     = [this](const json& p) { return tool_turiya_status(p); };
    handlers_["tape_stats"]        = [this](const json& p) { return tool_tape_stats(p); };
    handlers_["verbalize_rules"]   = [this](const json& p) { return tool_verbalize_rules(p); };
    handlers_["queue_experiments"] = [this](const json& p) { return tool_queue_experiments(p); };
    handlers_["fep_status"]        = [this](const json& p) { return tool_fep_status(p); };
    handlers_["routed_recall"]     = [this](const json& p) { return tool_routed_recall(p); };
    handlers_["witness_memory"]    = [this](const json& p) { return tool_witness_memory(p); };
    handlers_["reconcile_pass"]    = [this](const json& p) { return tool_reconcile_pass(p); };
    handlers_["harvest_scope"]     = [this](const json& p) { return tool_harvest_scope(p); };
    handlers_["seed_hdc_geometry"] = [this](const json& p) { return tool_seed_hdc_geometry(p); };

    tools_.push_back({{"name","log_event_ex"},{"description","Log a CEC event with regret-shaping telemetry (token_cost, latency_ms, retry_count). Updates Q-values with utility = outcome_reward - 0.001*token_cost - 0.00001*latency_ms - 0.1*retry_count."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"tool",        {{"type","string"}}},
            {"entity",      {{"type","string"}}},
            {"outcome",     {{"type","integer"},{"description","0=success 1=fail 2=error 3=partial"}}},
            {"session_id",  {{"type","integer"}}},
            {"ts_ms",       {{"type","integer"}}},
            {"token_cost",  {{"type","integer"},{"description","Tokens consumed (0=unknown)"}}},
            {"latency_ms",  {{"type","integer"},{"description","Wall-clock ms (0=unknown)"}}},
            {"retry_count", {{"type","integer"},{"description","Retries before this outcome"}}}
        }},{"required",{"tool","entity"}}}}
    });
    handlers_["log_event_ex"] = [this](const json& p) { return tool_log_event_ex(p); };

    tools_.push_back({{"name","log_decision"},{"description","Log a decision point to the DecisionTape: chosen action + alternatives considered and rejected. Enables recall_true_counterfactual."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"chosen_tool",      {{"type","string"}}},
            {"chosen_entity",    {{"type","string"}}},
            {"chosen_outcome",   {{"type","integer"},{"description","0=success 1=fail 2=error 3=partial"}}},
            {"rejected_json",    {{"type","string"},{"description","JSON array of [sym_u64, rejection_reason_u8] pairs"}}},
            {"confidence_delta", {{"type","number"},{"description","chosen_confidence - best_alternative_confidence"}}},
            {"ts_ms",            {{"type","integer"}}}
        }},{"required",{"chosen_tool","chosen_entity"}}}}
    });
    handlers_["log_decision"] = [this](const json& p) { return tool_log_decision(p); };
}

} // namespace chitta
