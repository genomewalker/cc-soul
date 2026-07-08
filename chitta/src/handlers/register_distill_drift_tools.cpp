// register_distill_drift_tools — chunk extracted from register_tools() so editing
// tool metadata only retemplates one chunk at a time.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

void FieldRpcHandler::register_distill_drift_tools() {
    tools_.push_back({{"name","distill_status"},{"description","Get distillation status"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["distill_status"] = [this](const json& p) { return tool_distill_status(p); };

    tools_.push_back({{"name","distill_now"},
        {"description","Synchronously distill ONE session now and return the counts "
            "(learnings + value-facts stored/deduped). Runs the lock-fixed distill path "
            "in-daemon; recall stays responsive."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"session_id",{{"type","string"},{"description","Session ID to distill"}}},
            {"transcript_path",{{"type","string"},{"description","Optional JSONL path; registers it if given, else resolves from durable transcript/register"}}},
            {"realm",{{"type","string"},{"description","Optional realm (default brahman or the registered realm)"}}}
        }},{"required",{"session_id"}}}}
    });
    handlers_["distill_now"] = [this](const json& p) { return tool_distill_now(p); };

    tools_.push_back({{"name","distill_set_model"},{"description","Change distillation model"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"model",{{"type","string"}}},{"enabled",{{"type","boolean"}}}
        }},{"required",{"model"}}}}
    });
    handlers_["distill_set_model"] = [this](const json& p) { return tool_distill_set_model(p); };

    tools_.push_back({{"name","suggestion_track"},{"description","Track a suggestion"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"content",{{"type","string"}}},{"context",{{"type","string"}}},{"realm",{{"type","string"}}}
        }},{"required",{"content"}}}}
    });
    handlers_["suggestion_track"] = [this](const json& p) { return tool_suggestion_track(p); };

    tools_.push_back({{"name","suggestion_pending"},{"description","List pending suggestions"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"realm",{{"type","string"}}},{"limit",{{"type","integer"}}}
        }}}}
    });
    handlers_["suggestion_pending"] = [this](const json& p) { return tool_suggestion_pending(p); };

    tools_.push_back({{"name","suggestion_resolve"},{"description","Record suggestion outcome"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"helped",{{"type","boolean"}}},{"details",{{"type","string"}}}
        }},{"required",{"id","helped"}}}}
    });
    handlers_["suggestion_resolve"] = [this](const json& p) { return tool_suggestion_resolve(p); };

    tools_.push_back({{"name","suggestion_count"},{"description","Count pending suggestions"},
        {"inputSchema",{{"type","object"},{"properties",{{"realm",{{"type","string"}}}}}}}
    });
    handlers_["suggestion_count"] = [this](const json& p) { return tool_suggestion_count(p); };

    tools_.push_back({{"name","consolidation_scan"},{"description","Find similar memory pairs"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"similarity_threshold",{{"type","number"}}},{"limit",{{"type","integer"}}},
            {"realm",{{"type","string"}}}
        }}}}
    });
    handlers_["consolidation_scan"] = [this](const json& p) { return tool_consolidation_scan(p); };

    tools_.push_back({{"name","consolidation_merge"},{"description","Merge two similar memories"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"primary_id",{{"type","integer"}}},{"secondary_id",{{"type","integer"}}},
            {"merged_content",{{"type","string"}}}
        }},{"required",{"primary_id","secondary_id"}}}}
    });
    handlers_["consolidation_merge"] = [this](const json& p) { return tool_consolidation_merge(p); };

    tools_.push_back({{"name","consolidation_auto"},{"description","Auto-merge highly similar memories"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"similarity_threshold",{{"type","number"}}},{"max_merges",{{"type","integer"}}}
        }}}}
    });
    handlers_["consolidation_auto"] = [this](const json& p) { return tool_consolidation_auto(p); };

    tools_.push_back({{"name","metacognition_corrections"},{"description","Analyze patterns in corrections"},
        {"inputSchema",{{"type","object"},{"properties",{{"limit",{{"type","integer"}}}}}}}
    });
    handlers_["metacognition_corrections"] = [this](const json& p) { return tool_metacognition_corrections(p); };

    tools_.push_back({{"name","metacognition_outcomes"},{"description","Analyze suggestion outcomes"},
        {"inputSchema",{{"type","object"},{"properties",{{"limit",{{"type","integer"}}}}}}}
    });
    handlers_["metacognition_outcomes"] = [this](const json& p) { return tool_metacognition_outcomes(p); };

    tools_.push_back({{"name","metacognition_evaluate"},{"description","Self-evaluate learning effectiveness"},
        {"inputSchema",{{"type","object"},{"properties",json::object()}}}
    });
    handlers_["metacognition_evaluate"] = [this](const json& p) { return tool_metacognition_evaluate(p); };

    tools_.push_back({{"name","epiplexity_check"},{"description","Compute epiplexity score"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"original",{{"type","string"}}},{"seed",{{"type","string"}}},{"reconstructed",{{"type","string"}}}
        }},{"required",{"original","seed","reconstructed"}}}}
    });
    handlers_["epiplexity_check"] = [this](const json& p) { return tool_epiplexity_check(p); };

    tools_.push_back({{"name","ssl_convert"},{"description","Convert raw text to SSL format"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"content",{{"type","string"}}},{"domain",{{"type","string"}}},{"location",{{"type","string"}}}
        }},{"required",{"content"}}}}
    });
    handlers_["ssl_convert"] = [this](const json& p) { return tool_ssl_convert(p); };

    tools_.push_back({{"name","curiosity_note_gap"},{"description","Record a knowledge gap"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"gap",{{"type","string"}}},{"context",{{"type","string"}}},{"realm",{{"type","string"}}}
        }},{"required",{"gap"}}}}
    });
    handlers_["curiosity_note_gap"] = [this](const json& p) { return tool_curiosity_note_gap(p); };

    tools_.push_back({{"name","curiosity_gaps"},{"description","List knowledge gaps"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"limit",{{"type","integer"}}},{"realm",{{"type","string"}}}
        }}}}
    });
    handlers_["curiosity_gaps"] = [this](const json& p) { return tool_curiosity_gaps(p); };

    tools_.push_back({{"name","curiosity_resolve"},{"description","Mark gap as resolved"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","integer"}}},{"learned",{{"type","string"}}}
        }},{"required",{"id"}}}}
    });
    handlers_["curiosity_resolve"] = [this](const json& p) { return tool_curiosity_resolve(p); };

    // ── Skill Registry ──────────────────────────────────────────────────
    tools_.push_back({{"name","lookup"},{"description","Unified memory lookup. Classifies intent, fans out to keyword/semantic/triplet/temporal/code backends, fuses with weighted RRF. Use this as the default memory search."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query",{{"type","string"}}},
            {"limit",{{"type","integer"}}},
            {"realm",{{"type","string"}}},
            {"mode",{{"type","string"},{"enum",{"auto","fast","deep"}}}},
            {"explain",{{"type","boolean"}}}
        }},{"required",{"query"}}}}
    });
    handlers_["lookup"] = [this](const json& p) { return tool_lookup(p); };

    tools_.push_back({
        {"name", "compact_context"},
        {"description", "Memory-aware context compaction. Scores conversation messages by recency, "
            "semantic relevance to query, and memory coverage (content already in memory is safer to drop). "
            "Returns a subset of messages fitting the target token budget."},
        {"inputSchema", {{"type", "object"},
            {"properties", {
                {"messages", {{"type", "array"}, {"description", "Conversation messages [{role,content}]"},
                    {"items", {{"type", "object"}}}}},
                {"query", {{"type", "string"}, {"description", "Upcoming task hint for semantic scoring"}}},
                {"target_ratio", {{"type", "number"}, {"description", "Fraction of tokens to KEEP (default 0.4)"}}},
                {"distill_novel", {{"type", "boolean"}, {"description", "Reserved for future use"}}}
            }}, {"required", {"messages"}}
        }}
    });
    handlers_["compact_context"] = [this](const json& p) { return tool_compact_context(p); };

    // ── Trajectory compaction (Latent Briefing) ─────────────────────────
    tools_.push_back({
        {"name", "trajectory_compact"},
        {"description", "Attention-weighted turn selection from a transcript. "
            "Embeds each turn, scores by cosine similarity to the task description, "
            "applies MAD adaptive threshold, enforces token budget. "
            "Returns a lossless subset of the most task-relevant turns."},
        {"inputSchema", {{"type", "object"},
            {"properties", {
                {"task", {{"type", "string"}, {"description", "What the downstream agent needs to accomplish"}}},
                {"session_id", {{"type", "string"}, {"description", "Session ID (auto-finds transcript)"}}},
                {"path", {{"type", "string"}, {"description", "Direct path to JSONL transcript"}}},
                {"budget_tokens", {{"type", "integer"}, {"description", "Target token budget (default 4000)"}}},
                {"mad_k", {{"type", "number"}, {"description", "MAD threshold multiplier (default 1.5, lower=more turns)"}}},
                {"role_filter", {{"type", "string"}, {"description", "Filter by role: user, assistant, or empty for all"}}},
                {"include_system", {{"type", "boolean"}, {"description", "Include system turns (default false)"}}}
            }}, {"required", {"task"}}
        }}
    });
    handlers_["trajectory_compact"] = [this](const json& p) { return tool_trajectory_compact(p); };

    // ── Layer 1: Executable Constraints ─────────────────────────────────
    tools_.push_back({{"name","set_evidence_type"},{"description","Tag a memory with its epistemological evidence class (observation/inference/hearsay/authoritative/prediction)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"},{"description","Memory ID"}}},
            {"evidence_type",{{"type","string"},{"description","One of: observation, inference, hearsay, authoritative, prediction"}}}
        }},{"required",{"id","evidence_type"}}}}});
    handlers_["set_evidence_type"] = [this](const json& p) { return tool_set_evidence_type(p); };

    tools_.push_back({{"name","get_evidence_type"},{"description","Retrieve the evidence type tag of a memory"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"},{"description","Memory ID"}}}
        }},{"required",{"id"}}}}});
    handlers_["get_evidence_type"] = [this](const json& p) { return tool_get_evidence_type(p); };

    tools_.push_back({{"name","labile_memories"},{"description","List memories recalled multiple times recently — candidates for reconsolidation (excludes freshly-written hook memories)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"realm",{{"type","string"},{"description","Filter by realm"}}},
            {"limit",{{"type","integer"},{"description","Max results (default 20)"}}},
            {"window_hours",{{"type","number"},{"description","Recency window in hours (default 48)"}}},
            {"min_access",{{"type","integer"},{"description","Min recall count to qualify (default 2)"}}}
        }}}}});
    handlers_["labile_memories"] = [this](const json& p) { return tool_labile_memories(p); };

    tools_.push_back({{"name","reconsolidate"},{"description","Update content of a memory during its labile window (reconsolidation)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"},{"description","Memory ID to update"}}},
            {"content",{{"type","string"},{"description","New/corrected content"}}},
            {"reason",{{"type","string"},{"description","Optional reason for reconsolidation"}}}
        }},{"required",{"id","content"}}}}});
    handlers_["reconsolidate"] = [this](const json& p) { return tool_reconsolidate(p); };

    tools_.push_back({{"name","5w_search"},{"description","Multi-dimensional semantic search across who/what/when/where/why axes"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"who",{{"type","string"},{"description","Who is involved"}}},
            {"what",{{"type","string"},{"description","What is happening/topic"}}},
            {"when",{{"type","string"},{"description","Temporal description"}}},
            {"where",{{"type","string"},{"description","Location or context"}}},
            {"why",{{"type","string"},{"description","Motivation or reason"}}},
            {"realm",{{"type","string"},{"description","Filter by realm"}}},
            {"limit",{{"type","integer"},{"description","Max results (default 10)"}}}
        }}}}});
    handlers_["5w_search"] = [this](const json& p) { return tool_5w_search(p); };

    tools_.push_back({{"name","recall_ucb1"},{"description","Recall with UCB1 exploration bonus — surfaces novel under-accessed memories alongside relevant ones"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query",{{"type","string"},{"description","Search query"}}},
            {"realm",{{"type","string"},{"description","Filter by realm"}}},
            {"limit",{{"type","integer"},{"description","Max results (default 10)"}}},
            {"exploration",{{"type","number"},{"description","Exploration weight sqrt(2)≈1.414 (default)"}}},
            {"fetch_k",{{"type","integer"},{"description","Candidate pool size before re-ranking (default 40)"}}}
        }},{"required",{"query"}}}}});
    handlers_["recall_ucb1"] = [this](const json& p) { return tool_recall_ucb1(p); };

    tools_.push_back({{"name","find_near_duplicates"},{"description","Find memory pairs with high semantic similarity (near-duplicates)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"realm",{{"type","string"},{"description","Filter by realm"}}},
            {"limit",{{"type","integer"},{"description","Max pairs to return (default 20)"}}},
            {"threshold",{{"type","number"},{"description","Cosine similarity threshold (default 0.90)"}}}
        }}}}});
    handlers_["find_near_duplicates"] = [this](const json& p) { return tool_find_near_duplicates(p); };

    tools_.push_back({{"name","consolidate_similar"},{"description","Merge near-duplicate memories — keeps stronger, soft-deletes weaker"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"realm",{{"type","string"},{"description","Filter by realm"}}},
            {"threshold",{{"type","number"},{"description","Similarity threshold (default 0.92)"}}},
            {"dry_run",{{"type","boolean"},{"description","Preview without deleting (default true)"}}},
            {"limit",{{"type","integer"},{"description","Max pairs to merge (default 10)"}}}
        }}}}});
    handlers_["consolidate_similar"] = [this](const json& p) { return tool_consolidate_similar(p); };

    tools_.push_back({{"name","cooccurrence_graph"},{"description","Show top co-activated memory associations for a given memory"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"id",{{"type","string"},{"description","Memory ID"}}},
            {"limit",{{"type","integer"},{"description","Max edges to return (default 10)"}}}
        }},{"required",{"id"}}}}});
    handlers_["cooccurrence_graph"] = [this](const json& p) { return tool_cooccurrence_graph(p); };

    tools_.push_back({{"name","labile_memories_top"},{"description","List the most-accessed (most labile) memories — candidates for reconsolidation"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"realm",{{"type","string"},{"description","Filter by realm"}}},
            {"limit",{{"type","integer"},{"description","Max results (default 20)"}}}
        }}}}});
    handlers_["labile_memories_top"] = [this](const json& p) { return tool_labile_memories_top(p); };

    // ── Behavioral Probe ──────────────────────────────────────────────────
    tools_.push_back({{"name","probe_seed"},{"description","Store an exemplar text as a centroid for a behavioral class (sycophantic/hedging/shallow/direct). Bootstrap the probe with representative examples."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"class",{{"type","string"},{"description","Behavioral class: sycophantic | hedging | shallow | direct"}}},
            {"text",{{"type","string"},{"description","Exemplar text for this class"}}},
            {"note",{{"type","string"},{"description","Optional annotation"}}}
        }},{"required",{"class","text"}}}}});
    handlers_["probe_seed"] = [this](const json& p) { return tool_probe_seed(p); };

    tools_.push_back({{"name","behavioral_probe"},{"description","Score text against behavioral centroid clusters. Returns per-class similarity scores (sycophantic/hedging/shallow/direct) and overall quality estimate. Requires prior probe_seed calls."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"text",{{"type","string"},{"description","Text to probe (e.g. a Claude response)"}}}
        }},{"required",{"text"}}}}});
    handlers_["behavioral_probe"] = [this](const json& p) { return tool_behavioral_probe(p); };

    tools_.push_back({{"name","probe_calibrate"},{"description","Add a confirmed exemplar to a behavioral class to refine its centroid. Use when you have a clear example of the behavior."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"class",{{"type","string"},{"description","Behavioral class to update"}}},
            {"text",{{"type","string"},{"description","Confirmed exemplar text"}}}
        }},{"required",{"class","text"}}}}});
    handlers_["probe_calibrate"] = [this](const json& p) { return tool_probe_calibrate(p); };

    tools_.push_back({{"name","probe_status"},{"description","Show how many exemplars exist per behavioral class. Use to verify the probe is seeded before running behavioral_probe."},
        {"inputSchema",{{"type","object"}}}});
    handlers_["probe_status"] = [this](const json& p) { return tool_probe_status(p); };

    // Contradiction engine
}

} // namespace chitta
