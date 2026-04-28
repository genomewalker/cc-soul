// register_protocol_tools — chunk extracted from register_tools() so editing
// tool metadata only retemplates one chunk at a time.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

void FieldRpcHandler::register_protocol_tools() {
    tools_.push_back({{"name","assert_fact"},{"description","Assert a constraint fact (subject-predicate-object) with provenance and scope. Auto-detects conflicts and creates rival branches."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"subject",{{"type","string"},{"description","Entity (e.g. 'user', 'project-X')"}}},
            {"predicate",{{"type","string"},{"description","Relation (e.g. 'prefers', 'uses', 'located-in')"}}},
            {"object",{{"type","string"},{"description","Value (e.g. 'Rust', 'vim', 'Copenhagen')"}}},
            {"confidence",{{"type","number"},{"description","Confidence 0-1 (default 0.8)"}}},
            {"scope",{{"type","string"},{"description","Scope: global, realm name, or session (default: global)"}}},
            {"branch_id",{{"type","integer"},{"description","Branch to assert into (0=trunk)"}}},
            {"provenance_source",{{"type","string"},{"description","Source: user, tool, distillation, inference"}}},
            {"confidence_basis",{{"type","string"},{"description","Basis: stated, observed, derived, corrected"}}}
        }},{"required",{"subject","predicate","object"}}}}});
    handlers_["assert_fact"] = [this](const json& p) { return tool_assert_fact(p); };

    tools_.push_back({{"name","retract_fact"},{"description","Soft-retract a constraint fact (preserves history)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"fact_id",{{"type","integer"},{"description","Fact ID to retract"}}}
        }},{"required",{"fact_id"}}}}});
    handlers_["retract_fact"] = [this](const json& p) { return tool_retract_fact(p); };

    tools_.push_back({{"name","query_unify"},{"description","Pattern-match query against constraint store (unification with wildcards)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"subject",{{"type","string"},{"description","Subject filter (omit for wildcard)"}}},
            {"predicate",{{"type","string"},{"description","Predicate filter"}}},
            {"object",{{"type","string"},{"description","Object filter"}}},
            {"scope",{{"type","string"},{"description","Scope filter"}}}
        }}}}});
    handlers_["query_unify"] = [this](const json& p) { return tool_query_unify(p); };

    tools_.push_back({{"name","query_chain"},{"description","Follow predicate chain: A→B→C through constraint facts"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"subject",{{"type","string"},{"description","Starting entity"}}},
            {"predicates",{{"type","array"},{"items",{{"type","string"}}},{"description","Ordered list of predicates to follow"}}}
        }},{"required",{"subject","predicates"}}}}});
    handlers_["query_chain"] = [this](const json& p) { return tool_query_chain(p); };

    tools_.push_back({{"name","explain_fact"},{"description","Explain a fact: provenance chain + supporting/conflicting facts"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"fact_id",{{"type","integer"},{"description","Fact ID to explain"}}}
        }},{"required",{"fact_id"}}}}});
    handlers_["explain_fact"] = [this](const json& p) { return tool_explain_fact(p); };

    tools_.push_back({{"name","branch_create"},{"description","Fork a rival branch for conflicting interpretations"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"parent_id",{{"type","integer"},{"description","Parent branch ID (0=trunk)"}}},
            {"scope",{{"type","string"},{"description","Branch scope (default: global)"}}}
        }}}}});
    handlers_["branch_create"] = [this](const json& p) { return tool_branch_create(p); };

    tools_.push_back({{"name","branch_resolve"},{"description","Resolve a branch conflict: winner stays, loser abandoned"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"winner_id",{{"type","integer"},{"description","Branch ID that wins"}}},
            {"loser_id",{{"type","integer"},{"description","Branch ID to abandon"}}}
        }},{"required",{"winner_id","loser_id"}}}}});
    handlers_["branch_resolve"] = [this](const json& p) { return tool_branch_resolve(p); };

    // ── Layer 2: Trigger Tissue ─────────────────────────────────────────
    tools_.push_back({{"name","trigger_add"},{"description","Create a trigger automaton (prospective memory). Arms on creation, fires when conditions met or tension exceeds threshold."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"name",{{"type","string"},{"description","Human-readable trigger name"}}},
            {"condition",{{"type","object"},{"description","Trigger condition (TimeAfter, ConstraintMatch, EventMatch, AllOf, AnyOf)"}}},
            {"action",{{"type","object"},{"description","Action on fire (Notify, InjectMemory, EmitEvent, RememberFact)"}}},
            {"deadline_ms",{{"type","integer"},{"description","Deadline timestamp ms (0=no deadline)"}}},
            {"tension_threshold",{{"type","number"},{"description","Tension level to auto-fire (default 0.8)"}}},
            {"gain",{{"type","number"},{"description","Emotional importance 0-1 (default 0.5)"}}},
            {"realm",{{"type","string"},{"description","Realm scope (default: global)"}}}
        }},{"required",{"name","condition","action"}}}}});
    handlers_["trigger_add"] = [this](const json& p) { return tool_trigger_add(p); };

    tools_.push_back({{"name","trigger_list"},{"description","List all triggers with their status"},
        {"inputSchema",{{"type","object"}}}});
    handlers_["trigger_list"] = [this](const json& p) { return tool_trigger_list(p); };

    tools_.push_back({{"name","trigger_fire"},{"description","Manually fire a trigger"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"trigger_id",{{"type","integer"},{"description","Trigger ID to fire"}}}
        }},{"required",{"trigger_id"}}}}});
    handlers_["trigger_fire"] = [this](const json& p) { return tool_trigger_fire(p); };

    tools_.push_back({{"name","trigger_dismiss"},{"description","Expire/dismiss a trigger without firing"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"trigger_id",{{"type","integer"},{"description","Trigger ID to dismiss"}}}
        }},{"required",{"trigger_id"}}}}});
    handlers_["trigger_dismiss"] = [this](const json& p) { return tool_trigger_dismiss(p); };

    // ── Layer 3: Predictive Memory ──────────────────────────────────────
    tools_.push_back({{"name","predict_needed"},{"description","Get predicted next-needed memories from the Markov chain access predictor"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"k",{{"type","integer"},{"description","Number of predictions (default 8)"}}}
        }}}}});
    handlers_["predict_needed"] = [this](const json& p) { return tool_predict_needed(p); };

    // ── Layer 4: Surprise Memory ─────────────────────────────────────────
    tools_.push_back({{"name","record_surprise"},{"description","Record a prediction error event — what was expected vs what actually happened"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"context_sketch",{{"type","string"},{"description","What was happening when the surprise occurred"}}},
            {"action",{{"type","string"},{"description","What action was taken"}}},
            {"expected",{{"type","string"},{"description","What was predicted/expected (optional)"}}},
            {"actual",{{"type","string"},{"description","What actually happened (required)"}}},
            {"surprise_magnitude",{{"type","number"},{"description","How surprising [0-1] (default 0.5)"}}},
            {"domain",{{"type","string"},{"description","Domain: recall, tool, user_correction, constraint (default general)"}}},
            {"realm",{{"type","string"},{"description","Realm filter (default global)"}}},
            {"session_id",{{"type","string"},{"description","Session ID (optional)"}}},
            {"source_memory_id",{{"type","integer"},{"description","Related memory ID (optional)"}}}
        }},{"required",{"actual"}}}}});
    handlers_["record_surprise"] = [this](const json& p) { return tool_record_surprise(p); };

    tools_.push_back({{"name","query_surprises"},{"description","Query recorded surprise/prediction-error events with filters"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"domain",{{"type","string"},{"description","Filter by domain"}}},
            {"realm",{{"type","string"},{"description","Filter by realm"}}},
            {"min_magnitude",{{"type","number"},{"description","Minimum surprise magnitude"}}},
            {"since_ms",{{"type","integer"},{"description","Only events after this timestamp (ms)"}}},
            {"limit",{{"type","integer"},{"description","Max results (default 50)"}}}
        }}}}});
    handlers_["query_surprises"] = [this](const json& p) { return tool_query_surprises(p); };

    tools_.push_back({{"name","get_blind_spots"},{"description","Identify recurring surprise patterns — domains/actions where predictions consistently fail"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"realm",{{"type","string"},{"description","Filter by realm"}}},
            {"limit",{{"type","integer"},{"description","Max blind spots (default 10)"}}}
        }}}}});
    handlers_["get_blind_spots"] = [this](const json& p) { return tool_get_blind_spots(p); };

    tools_.push_back({{"name","surprise_stats"},{"description","Summary statistics for surprise memory: counts, avg magnitude, domain breakdown"},
        {"inputSchema",{{"type","object"}}}});
    handlers_["surprise_stats"] = [this](const json& p) { return tool_surprise_stats(p); };

    // ── Layer 5: Epistemic Debt ──────────────────────────────────────────
    tools_.push_back({{"name","register_debt"},{"description","Register an epistemic uncertainty — competing hypotheses that need resolution"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"pattern",{{"type","string"},{"description","The uncertain pattern/belief (required)"}}},
            {"competing_hypotheses",{{"type","array"},{"items",{{"type","string"}}},{"description","Competing explanations"}}},
            {"discriminating_test",{{"type","string"},{"description","How to distinguish between hypotheses"}}},
            {"fragility_score",{{"type","number"},{"description","How fragile this belief is [0-1] (default 0.5)"}}},
            {"domain",{{"type","string"},{"description","Domain (default general)"}}},
            {"realm",{{"type","string"},{"description","Realm (default global)"}}},
            {"session_id",{{"type","string"},{"description","Session ID (optional)"}}}
        }},{"required",{"pattern"}}}}});
    handlers_["register_debt"] = [this](const json& p) { return tool_register_debt(p); };

    tools_.push_back({{"name","resolve_debt"},{"description","Mark an epistemic debt as resolved with a resolution"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"debt_id",{{"type","integer"},{"description","Debt ID to resolve"}}},
            {"resolution",{{"type","string"},{"description","How the uncertainty was resolved"}}}
        }},{"required",{"debt_id","resolution"}}}}});
    handlers_["resolve_debt"] = [this](const json& p) { return tool_resolve_debt(p); };

    tools_.push_back({{"name","defer_debt"},{"description","Defer an epistemic debt for later investigation"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"debt_id",{{"type","integer"},{"description","Debt ID to defer"}}}
        }},{"required",{"debt_id"}}}}});
    handlers_["defer_debt"] = [this](const json& p) { return tool_defer_debt(p); };

    tools_.push_back({{"name","query_debts"},{"description","Query epistemic debts with filters"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"status",{{"type","string"},{"description","Filter: open, resolved, deferred"}}},
            {"domain",{{"type","string"},{"description","Filter by domain"}}},
            {"realm",{{"type","string"},{"description","Filter by realm"}}},
            {"min_fragility",{{"type","number"},{"description","Minimum fragility score"}}},
            {"limit",{{"type","integer"},{"description","Max results (default 50)"}}}
        }}}}});
    handlers_["query_debts"] = [this](const json& p) { return tool_query_debts(p); };

    tools_.push_back({{"name","get_fragile_decisions"},{"description","List open epistemic debts sorted by fragility — decisions most likely to be wrong"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"threshold",{{"type","number"},{"description","Minimum fragility threshold (default 0.5)"}}},
            {"limit",{{"type","integer"},{"description","Max results (default 20)"}}}
        }}}}});
    handlers_["get_fragile_decisions"] = [this](const json& p) { return tool_get_fragile_decisions(p); };

    tools_.push_back({{"name","debt_stats"},{"description","Summary statistics for epistemic debt: counts by status, avg fragility"},
        {"inputSchema",{{"type","object"}}}});
    handlers_["debt_stats"] = [this](const json& p) { return tool_debt_stats(p); };

    // ── Layer 6: Integration Kernel ──────────────────────────────────────
    tools_.push_back({{"name","record_feedback"},{"description","Record whether a recall source was useful — updates learned source weights"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"query_domain",{{"type","string"},{"description","Domain of the query (default general)"}}},
            {"source",{{"type","string"},{"description","Source: semantic, keyword, temporal, artifact, association (required)"}}},
            {"was_useful",{{"type","boolean"},{"description","Whether the source's results were useful (default true)"}}}
        }},{"required",{"source"}}}}});
    handlers_["record_feedback"] = [this](const json& p) { return tool_record_feedback(p); };

    tools_.push_back({{"name","get_source_weights"},{"description","View learned recall source weights — how much each source is trusted per domain"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"domain",{{"type","string"},{"description","Filter by domain (omit for all)"}}}
        }}}}});
    handlers_["get_source_weights"] = [this](const json& p) { return tool_get_source_weights(p); };

    tools_.push_back({{"name","update_source_weight"},{"description","Manually override a recall source weight"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"source",{{"type","string"},{"description","Source name (required)"}}},
            {"domain",{{"type","string"},{"description","Domain (default general)"}}},
            {"weight",{{"type","number"},{"description","New weight [0-2] (default 1.0)"}}}
        }},{"required",{"source"}}}}});
    handlers_["update_source_weight"] = [this](const json& p) { return tool_update_source_weight(p); };

    tools_.push_back({{"name","integration_stats"},{"description","Per-source success rates and learned weights across all domains"},
        {"inputSchema",{{"type","object"}}}});
    handlers_["integration_stats"] = [this](const json& p) { return tool_integration_stats(p); };

    // ── Autonomous Learning (Moves 1-6) ─────────────────────────────────
    tools_.push_back({{"name","surprise_learning_stats"},{"description","Rolling surprise credit stats — tracked memories, gates passed, strength adjustments"},
        {"inputSchema",{{"type","object"}}}});
    handlers_["surprise_learning_stats"] = [this](const json& p) { return tool_surprise_learning_stats(field_store_, p); };

    tools_.push_back({{"name","upsert_wisdom_candidate"},{"description","Create or update a wisdom candidate from clustered surprise patterns"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"cluster_key",{{"type","string"},{"description","Unique key for this pattern cluster (domain+action+sig)"}}},
            {"domain",{{"type","string"},{"description","Knowledge domain"}}},
            {"action",{{"type","string"},{"description","Action or behavior pattern"}}},
            {"summary",{{"type","string"},{"description","Human-readable summary of the wisdom"}}},
            {"episode_ids",{{"type","array"},{"items",{{"type","integer"}}},{"description","Surprise event IDs supporting this candidate"}}},
            {"debt_ids",{{"type","array"},{"items",{{"type","integer"}}},{"description","Resolved debt IDs linked to this candidate"}}},
            {"support_count",{{"type","integer"},{"description","Number of supporting episodes"}}},
            {"cross_session_count",{{"type","integer"},{"description","Number of distinct sessions with evidence"}}},
            {"mean_surprise",{{"type","number"},{"description","Average surprise magnitude across episodes"}}},
            {"promotion_score",{{"type","number"},{"description","Computed promotion readiness score 0-1"}}}
        }},{"required",{"cluster_key"}}}}});
    handlers_["upsert_wisdom_candidate"] = [this](const json& p) { return tool_upsert_wisdom_candidate(field_store_, p); };

    tools_.push_back({{"name","update_wisdom_lifecycle"},{"description","Advance a wisdom candidate through lifecycle stages: candidate→provisional→trusted→demoted"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"candidate_id",{{"type","integer"},{"description","Wisdom candidate ID"}}},
            {"new_state",{{"type","integer"},{"description","0=candidate, 1=provisional, 2=trusted, 3=demoted"}}}
        }},{"required",{"candidate_id","new_state"}}}}});
    handlers_["update_wisdom_lifecycle"] = [this](const json& p) { return tool_update_wisdom_lifecycle(field_store_, p); };

    tools_.push_back({{"name","query_wisdom_candidates"},{"description","Query wisdom candidates by lifecycle stage and/or domain"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"lifecycle",{{"type","integer"},{"description","Filter by lifecycle: 0=candidate, 1=provisional, 2=trusted, 3=demoted"}}},
            {"domain",{{"type","string"},{"description","Filter by domain"}}},
            {"limit",{{"type","integer"},{"description","Max results (default 50)"}}}
        }}}}});
    handlers_["query_wisdom_candidates"] = [this](const json& p) { return tool_query_wisdom_candidates(field_store_, p); };

    tools_.push_back({{"name","wisdom_promotion_stats"},{"description","Overview of wisdom promotion pipeline — total candidates by lifecycle stage"},
        {"inputSchema",{{"type","object"}}}});
    handlers_["wisdom_promotion_stats"] = [this](const json& p) { return tool_wisdom_promotion_stats(field_store_, p); };

    tools_.push_back({{"name","attach_debt_evidence"},{"description","Attach supporting evidence to an epistemic debt — memory IDs + confidence"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"debt_id",{{"type","integer"},{"description","Epistemic debt ID"}}},
            {"memory_ids",{{"type","array"},{"items",{{"type","integer"}}},{"description","Memory IDs that serve as evidence"}}},
            {"confidence",{{"type","number"},{"description","Evidence confidence 0-1 (default 0.5)"}}},
            {"note",{{"type","string"},{"description","Optional note about the evidence"}}}
        }},{"required",{"debt_id"}}}}});
    handlers_["attach_debt_evidence"] = [this](const json& p) { return tool_attach_debt_evidence(field_store_, p); };

    tools_.push_back({{"name","update_scorer_model"},{"description","Apply learned weight deltas to the scoring model from outcome calibration"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"weights",{{"type","object"},{"description","Factor name → {delta, min_delta, max_delta} learned adjustments"}}},
            {"model_version",{{"type","integer"},{"description","Monotonic version number"}}},
            {"mean_loss",{{"type","number"},{"description","EWMA loss from calibration"}}},
            {"outcome_count",{{"type","integer"},{"description","Total outcomes used for calibration"}}}
        }}}}});
    handlers_["update_scorer_model"] = [this](const json& p) { return tool_update_scorer_model(field_store_, p); };

    tools_.push_back({{"name","learned_scorer_stats"},{"description","Current learned scoring model — version, factor count, loss, outcome count"},
        {"inputSchema",{{"type","object"}}}});
    handlers_["learned_scorer_stats"] = [this](const json& p) { return tool_learned_scorer_stats(field_store_, p); };

    tools_.push_back({{"name","effective_scorer_weights"},{"description","Show effective scoring weights — baseline + learned deltas for all factors"},
        {"inputSchema",{{"type","object"}}}});
    handlers_["effective_scorer_weights"] = [this](const json& p) { return tool_effective_scorer_weights(field_store_, p); };

    // ── Layer 7: Intervention Ledger ─────────────────────────────────────
    tools_.push_back({{"name","start_intervention"},{"description","Begin tracking an agent intervention — records intent, action, preconditions and expected observables before execution"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"realm",{{"type","string"},{"description","Realm: coding, research, planning (default: coding)"}}},
            {"session_id",{{"type","string"},{"description","Current session ID"}}},
            {"task_id",{{"type","integer"},{"description","Optional task ID"}}},
            {"agent_id",{{"type","string"},{"description","Agent performing the action"}}},
            {"domain",{{"type","string"},{"description","Domain area (e.g. git, filesystem, testing, compiler)"}}},
            {"intent",{{"type","string"},{"description","What the agent intends to achieve"}}},
            {"action_type",{{"type","integer"},{"description","0=ToolCall 1=MultiStepPlan 2=Delegation 3=Edit 4=Command"}}},
            {"action_ref",{{"type","string"},{"description","Reference to the action (tool name, file path, command)"}}},
            {"preconditions",{{"type","array"},{"items",{{"type","string"}}},{"description","Known preconditions"}}},
            {"expected_observables",{{"type","array"},{"items",{{"type","string"}}},{"description","What success looks like"}}},
            {"reversal_cost",{{"type","integer"},{"description","0=None 1=Low 2=Medium 3=High"}}}
        }},{"required",{"intent","action_ref"}}}}});
    handlers_["start_intervention"] = [this](const json& p) { return tool_start_intervention(field_store_, p); };

    tools_.push_back({{"name","add_observation"},{"description","Record an observation during an open intervention (stdout, test result, file diff, etc.)"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"intervention_id",{{"type","integer"},{"description","Intervention ID from start_intervention"}}},
            {"kind",{{"type","integer"},{"description","0=Stdout 1=Stderr 2=FileDiff 3=TestResult 4=EnvState 5=UserFeedback"}}},
            {"summary",{{"type","string"},{"description","Human-readable observation summary"}}},
            {"confidence",{{"type","number"},{"description","Confidence in this observation (0.0-1.0)"}}},
            {"evidence_refs",{{"type","array"},{"items",{{"type","integer"}}},{"description","Memory IDs that constitute evidence"}}}
        }},{"required",{"intervention_id","summary"}}}}});
    handlers_["add_observation"] = [this](const json& p) { return tool_add_observation(field_store_, p); };

    tools_.push_back({{"name","close_intervention"},{"description","Close an intervention with its final outcome status"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"intervention_id",{{"type","integer"},{"description","Intervention ID to close"}}},
            {"status",{{"type","integer"},{"description","0=Open 1=Succeeded 2=Failed 3=Partial 4=Aborted"}}}
        }},{"required",{"intervention_id","status"}}}}});
    handlers_["close_intervention"] = [this](const json& p) { return tool_close_intervention(field_store_, p); };

    tools_.push_back({{"name","record_attribution"},{"description","Attribute a closed intervention to a causal class — routes feedback to the appropriate learning subsystem"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"intervention_id",{{"type","integer"},{"description","Intervention ID"}}},
            {"primary_class",{{"type","integer"},{"description","0=MemoryRecallError 1=SourceTrustError 2=ProcedureError 3=ToolExecutionError 4=EnvironmentShift 5=HiddenPrecondition 6=AmbiguousState 7=GoalSpecError 8=UserOverride 9=ExternalNondeterminism"}}},
            {"secondary_class",{{"type","integer"},{"description","Optional secondary attribution class (same enum)"}}},
            {"confidence_delta",{{"type","number"},{"description","Magnitude of the learning signal (0.0-1.0)"}}},
            {"surprise_id",{{"type","integer"},{"description","Linked surprise event ID if available"}}},
            {"debt_ids",{{"type","array"},{"items",{{"type","integer"}}},{"description","Linked epistemic debt IDs"}}},
            {"source_memory_ids",{{"type","array"},{"items",{{"type","integer"}}},{"description","Memory IDs that contributed to this outcome"}}},
            {"skill_memory_ids",{{"type","array"},{"items",{{"type","integer"}}},{"description","Skill memory IDs that were applied"}}},
            {"note",{{"type","string"},{"description","Optional human-readable note"}}}
        }},{"required",{"intervention_id","primary_class"}}}}});
    handlers_["record_attribution"] = [this](const json& p) { return tool_record_attribution(field_store_, p); };

    tools_.push_back({{"name","query_interventions"},{"description","Query the intervention ledger with optional filters"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"realm",{{"type","string"},{"description","Filter by realm"}}},
            {"session_id",{{"type","string"},{"description","Filter by session ID"}}},
            {"status",{{"type","integer"},{"description","Filter by status (0-4)"}}},
            {"limit",{{"type","integer"},{"description","Max results (default 50)"}}}
        }}}}});
    handlers_["query_interventions"] = [this](const json& p) { return tool_query_interventions(field_store_, p); };

    tools_.push_back({{"name","get_intervention"},{"description","Get a single intervention record by ID"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"intervention_id",{{"type","integer"},{"description","Intervention ID"}}}
        }},{"required",{"intervention_id"}}}}});
    handlers_["get_intervention"] = [this](const json& p) { return tool_get_intervention(field_store_, p); };

    tools_.push_back({{"name","intervention_stats"},{"description","Show intervention ledger statistics — total, open, succeeded, failed, aborted counts"},
        {"inputSchema",{{"type","object"}}}});
    handlers_["intervention_stats"] = [this](const json& p) { return tool_intervention_stats(field_store_, p); };

    tools_.push_back({{"name","list_open_interventions"},{"description","List all currently open (in-progress) interventions"},
        {"inputSchema",{{"type","object"}}}});
    handlers_["list_open_interventions"] = [this](const json& p) { return tool_list_open_interventions(field_store_, p); };

    // ── Layer 8: Agent Protocol Memory ───────────────────────────────────
    tools_.push_back({{"name","register_task"},{"description","Register a task contract — records goal, constraints, acceptance criteria, priority, and optional deadline for an ongoing agent task"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"session_id",{{"type","string"},{"description","Session this task belongs to"}}},
            {"realm",{{"type","string"},{"description","Realm: coding, research, planning (default: coding)"}}},
            {"goal",{{"type","string"},{"description","Task goal description"}}},
            {"constraints",{{"type","array"},{"items",{{"type","string"}}},{"description","Constraints that must be respected"}}},
            {"acceptance_criteria",{{"type","array"},{"items",{{"type","string"}}},{"description","Criteria for task completion"}}},
            {"priority",{{"type","integer"},{"description","Priority 1-10 (default 5)"}}},
            {"parent_task_id",{{"type","integer"},{"description","Parent task ID for subtasks"}}},
            {"tags",{{"type","array"},{"items",{{"type","string"}}},{"description","Optional tags"}}},
            {"deadline_ms",{{"type","integer"},{"description","Optional deadline as Unix ms timestamp"}}}
        }},{"required",{"goal"}}}}});
    handlers_["register_task"] = [this](const json& p) { return tool_register_task(field_store_, p); };

    tools_.push_back({{"name","update_task"},{"description","Update task status (Active=0, Blocked=1, Completed=2, Failed=3, Abandoned=4), optionally attach an intervention ID or add a tag"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"task_id",{{"type","integer"},{"description","Task ID"}}},
            {"status",{{"type","integer"},{"description","0=Active 1=Blocked 2=Completed 3=Failed 4=Abandoned"}}},
            {"add_intervention_id",{{"type","integer"},{"description","Attach intervention to task"}}},
            {"add_tag",{{"type","string"},{"description","Add a tag to the task"}}}
        }},{"required",{"task_id","status"}}}}});
    handlers_["update_task"] = [this](const json& p) { return tool_update_task(field_store_, p); };

    tools_.push_back({{"name","add_delegation"},{"description","Record a delegation edge — tracks which agent handed off to which, with optional handoff note"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"task_id",{{"type","integer"},{"description","Task ID"}}},
            {"from_agent",{{"type","string"},{"description","Delegating agent name"}}},
            {"to_agent",{{"type","string"},{"description","Receiving agent name"}}},
            {"handoff_note",{{"type","string"},{"description","Optional context passed at handoff"}}}
        }},{"required",{"task_id","from_agent","to_agent"}}}}});
    handlers_["add_delegation"] = [this](const json& p) { return tool_add_delegation(field_store_, p); };

    tools_.push_back({{"name","link_evidence"},{"description","Link a memory to a task as evidence — records which agent produced it and evidence kind"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"task_id",{{"type","integer"},{"description","Task ID"}}},
            {"memory_id",{{"type","integer"},{"description","Memory ID to link"}}},
            {"produced_by",{{"type","string"},{"description","Agent that produced this evidence"}}},
            {"evidence_kind",{{"type","integer"},{"description","0=Observation 1=Artifact 2=Result 3=Analysis 4=UserFeedback"}}},
            {"relevance",{{"type","number"},{"description","Relevance score 0-1 (default 1.0)"}}}
        }},{"required",{"task_id","memory_id"}}}}});
    handlers_["link_evidence"] = [this](const json& p) { return tool_link_evidence(field_store_, p); };

    tools_.push_back({{"name","add_probe"},{"description","Add a pending probe — an open question that must be answered to unblock or complete a task"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"task_id",{{"type","integer"},{"description","Task ID"}}},
            {"question",{{"type","string"},{"description","Open question to be answered"}}},
            {"expected_answerer",{{"type","string"},{"description","Agent or role expected to answer"}}},
            {"priority",{{"type","integer"},{"description","Priority 1-10 (default 5)"}}}
        }},{"required",{"task_id","question"}}}}});
    handlers_["add_probe"] = [this](const json& p) { return tool_add_probe(field_store_, p); };

    tools_.push_back({{"name","resolve_probe"},{"description","Resolve a pending probe — mark as Answered (1) or Dismissed (2) and optionally record the answer"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"probe_id",{{"type","integer"},{"description","Probe ID"}}},
            {"status",{{"type","integer"},{"description","1=Answered 2=Dismissed"}}},
            {"answer",{{"type","string"},{"description","Optional answer text"}}}
        }},{"required",{"probe_id","status"}}}}});
    handlers_["resolve_probe"] = [this](const json& p) { return tool_resolve_probe(field_store_, p); };

    tools_.push_back({{"name","set_criterion"},{"description","Upsert a completion criterion for a task — creates if new, updates if existing criterion text matches"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"task_id",{{"type","integer"},{"description","Task ID"}}},
            {"criterion",{{"type","string"},{"description","Criterion description"}}},
            {"is_met",{{"type","boolean"},{"description","Whether criterion is met (default false)"}}},
            {"evidence_note",{{"type","string"},{"description","Optional evidence supporting the criterion check"}}}
        }},{"required",{"task_id","criterion"}}}}});
    handlers_["set_criterion"] = [this](const json& p) { return tool_set_criterion(field_store_, p); };

    tools_.push_back({{"name","get_task"},{"description","Get full task view — contract, delegations, evidence links, pending probes, and completion criteria"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"task_id",{{"type","integer"},{"description","Task ID"}}}
        }},{"required",{"task_id"}}}}});
    handlers_["get_task"] = [this](const json& p) { return tool_get_task(field_store_, p); };

    tools_.push_back({{"name","query_tasks"},{"description","Query task contracts — filter by realm, session, status, or tag"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"realm",{{"type","string"},{"description","Filter by realm"}}},
            {"session_id",{{"type","string"},{"description","Filter by session ID"}}},
            {"status",{{"type","integer"},{"description","Filter by status (0-4)"}}},
            {"tag",{{"type","string"},{"description","Filter by tag"}}},
            {"limit",{{"type","integer"},{"description","Max results (default 50)"}}}
        }}}}});
    handlers_["query_tasks"] = [this](const json& p) { return tool_query_tasks(field_store_, p); };

    tools_.push_back({{"name","agent_protocol_stats"},{"description","Show agent protocol memory statistics — total tasks, delegations, evidence links, probes, criteria counts"},
        {"inputSchema",{{"type","object"}}}});
    handlers_["agent_protocol_stats"] = [this](const json& p) { return tool_agent_protocol_stats(field_store_, p); };

    // ── Wisdom Homeostasis (Layer 9) ─────────────────────────────────────
    tools_.push_back({{"name","enroll_wisdom_lineage"},{"description","Enroll a Trusted wisdom candidate into the Wisdom Homeostasis layer — creates a living WisdomLineage record that tracks belief integrity over time"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"wisdom_candidate_id",{{"type","integer"},{"description","ID of the WisdomCandidate to enroll"}}},
            {"claim",{{"type","string"},{"description","The claim this wisdom encodes"}}},
            {"envelope",{{"type","object"},{"description","Applicability envelope: {domain, action_types, preconditions, source_families}"}}},
            {"seed_episode_ids",{{"type","array"},{"items",{{"type","integer"}}},{"description","Episode IDs that seeded this wisdom"}}},
            {"seed_surprise_ids",{{"type","array"},{"items",{{"type","integer"}}},{"description","Surprise event IDs"}}},
            {"seed_intervention_ids",{{"type","array"},{"items",{{"type","integer"}}},{"description","Intervention IDs"}}},
            {"seed_debt_ids",{{"type","array"},{"items",{{"type","integer"}}},{"description","Epistemic debt IDs"}}},
            {"ancestor_lineage_id",{{"type","integer"},{"description","Parent lineage ID if this is a fork/split"}}},
            {"derivation_relation",{{"type","string"},{"description","Relation to ancestor: supersedes|branches_from|narrows|splits_from"}}}
        }},{"required",{"wisdom_candidate_id","claim","envelope"}}}}});
    handlers_["enroll_wisdom_lineage"] = [this](const json& p) { return tool_enroll_wisdom_lineage(field_store_, p); };

    tools_.push_back({{"name","transition_wisdom_lineage"},{"description","Manually transition a wisdom lineage state (Trusted/Watch/Inflamed/Demoted). Normally automatic — use for overrides."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"lineage_id",{{"type","integer"},{"description","Lineage ID"}}},
            {"new_state",{{"type","integer"},{"description","0=Trusted 1=Watch 2=Inflamed 3=Demoted"}}},
            {"reason",{{"type","string"},{"description","Why this transition is happening"}}},
            {"rederive_task_id",{{"type","integer"},{"description","Task contract ID if opening re-derivation"}}}
        }},{"required",{"lineage_id","new_state"}}}}});
    handlers_["transition_wisdom_lineage"] = [this](const json& p) { return tool_transition_wisdom_lineage(field_store_, p); };

    tools_.push_back({{"name","close_rederive"},{"description","Close a re-derivation contract for an Inflamed wisdom lineage. Actions: reaffirm (0), narrow (1), split (2), demote (3)."},
        {"inputSchema",{{"type","object"},{"properties",{
            {"lineage_id",{{"type","integer"},{"description","Lineage ID"}}},
            {"action",{{"type","integer"},{"description","0=reaffirm 1=narrow 2=split 3=demote"}}},
            {"new_envelope",{{"type","object"},{"description","Narrowed applicability envelope (for action=narrow/split)"}}},
            {"fork_claim",{{"type","string"},{"description","Claim for the forked lineage (action=split)"}}},
            {"fork_lineage_id",{{"type","integer"},{"description","Pre-enrolled fork lineage ID (action=split)"}}}
        }},{"required",{"lineage_id","action"}}}}});
    handlers_["close_rederive"] = [this](const json& p) { return tool_close_rederive(field_store_, p); };

    tools_.push_back({{"name","query_wisdom_lineages"},{"description","List wisdom lineages filtered by state and/or domain"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"state",{{"type","string"},{"description","Filter: trusted|watch|inflamed|demoted"}}},
            {"domain",{{"type","string"},{"description","Filter by domain"}}},
            {"limit",{{"type","integer"},{"description","Max results (default 50)"}}}
        }}}}});
    handlers_["query_wisdom_lineages"] = [this](const json& p) { return tool_query_wisdom_lineages(field_store_, p); };

    tools_.push_back({{"name","get_wisdom_lineage"},{"description","Get full details of a wisdom lineage by ID, including challenger evidence and state history"},
        {"inputSchema",{{"type","object"},{"properties",{
            {"lineage_id",{{"type","integer"},{"description","Lineage ID"}}}
        }},{"required",{"lineage_id"}}}}});
    handlers_["get_wisdom_lineage"] = [this](const json& p) { return tool_get_wisdom_lineage(field_store_, p); };

    tools_.push_back({{"name","wisdom_lineage_stats"},{"description","Show wisdom homeostasis statistics — counts by state, mean staleness, support/contradiction mass totals"},
        {"inputSchema",{{"type","object"}}}});
    handlers_["wisdom_lineage_stats"] = [this](const json& p) { return tool_wisdom_lineage_stats(field_store_, p); };

    tools_.push_back({{"name","tick_lineage_staleness"},{"description","Manually trigger a staleness tick — grows staleness mass on lineages with no recent support. Normally called by the subconscious cycle."},
        {"inputSchema",{{"type","object"}}}});
    handlers_["tick_lineage_staleness"] = [this](const json& p) { return tool_tick_lineage_staleness(field_store_, p); };

    tools_.push_back({{"name","lineage_expiry_check"},{"description","List Inflamed lineages whose re-derivation TTL has expired — these should be demoted or re-derived urgently"},
        {"inputSchema",{{"type","object"}}}});
    handlers_["lineage_expiry_check"] = [this](const json& p) { return tool_lineage_expiry_check(field_store_, p); };

    // ── Drift-memory tools ───────────────────────────────────────────────
}

} // namespace chitta
