// Included into FieldRpcHandler class body — not a standalone header.
// Layer 1: Executable Constraints — Datalog-like facts with provenance, scope, branches.
// Layer 2: Trigger Tissue — prospective memory as event-driven automata.
// Layer 3: Predictive Memory — Markov chain access predictor.
//
// References:
//   [1] GPT-5.4 + Opus brainstorm (2026-04-11) — contradiction lattice + unification recall
//   [2] Einstein & McDaniel (1990). Normal aging and prospective memory.
//   [3] Anderson & Schooler (1991). Reflections of the environment in memory.

    // ── Layer 1: Executable Constraints ─────────────────────────────────

    ToolResult tool_assert_fact(const json& params) {
        json p;
        p["subject"]   = params.value("subject", "");
        p["predicate"]  = params.value("predicate", "");
        p["object"]     = params.value("object", "");
        p["confidence"] = params.value("confidence", 0.8);
        p["scope"]      = params.value("scope", "global");
        p["branch_id"]  = params.value("branch_id", 0);
        p["provenance_source"]  = params.value("provenance_source", "tool");
        p["confidence_basis"]   = params.value("confidence_basis", "observed");
        if (params.contains("session_id"))
            p["session_id"] = params["session_id"];
        if (params.contains("source_memory_id"))
            p["source_memory_id"] = params["source_memory_id"];

        if (p["subject"].get<std::string>().empty() || p["predicate"].get<std::string>().empty())
            return ToolResult::error("subject and predicate are required");

        auto* raw = cf_assert_constraint(field_store_->handle(), p.dump().c_str());
        if (!raw) return ToolResult::error("assert_fact failed");
        std::string result_str(raw);
        cf_free_string(raw);

        auto result = json::parse(result_str, nullptr, false);
        if (result.is_discarded()) return ToolResult::error("parse error");

        std::string summary = "Asserted fact #" + std::to_string(result.value("fact_id", 0));
        if (!result["conflict"].is_null()) {
            summary += " (conflict detected: branch #" +
                std::to_string(result["conflict"].value("new_branch_id", 0)) + " created)";
        }
        return ToolResult::ok(summary, result);
    }

    ToolResult tool_retract_fact(const json& params) {
        uint64_t fact_id = params.value("fact_id", uint64_t(0));
        if (fact_id == 0) return ToolResult::error("fact_id is required");

        int r = cf_retract_constraint(field_store_->handle(), fact_id);
        if (r != 0) return ToolResult::error("fact not found or already retracted");
        return ToolResult::ok("Retracted fact #" + std::to_string(fact_id));
    }

    ToolResult tool_query_unify(const json& params) {
        json p;
        if (params.contains("subject"))   p["subject"]   = params["subject"];
        if (params.contains("predicate")) p["predicate"]  = params["predicate"];
        if (params.contains("object"))    p["object"]     = params["object"];
        if (params.contains("scope"))     p["scope"]      = params["scope"];

        auto* raw = cf_query_constraints(field_store_->handle(), p.dump().c_str());
        if (!raw) return ToolResult::ok("[]", json::array());
        std::string result_str(raw);
        cf_free_string(raw);

        auto results = json::parse(result_str, nullptr, false);
        if (results.is_discarded()) results = json::array();

        std::string summary = std::to_string(results.size()) + " facts matched";
        return ToolResult::ok(summary, {{"facts", results}, {"count", results.size()}});
    }

    ToolResult tool_query_chain(const json& params) {
        std::string subject = params.value("subject", "");
        if (subject.empty()) return ToolResult::error("subject is required");

        // Chain queries go through the Rust store directly via query_constraints
        // For now, return single-hop results for each predicate in the chain
        auto predicates = params.value("predicates", std::vector<std::string>{});
        if (predicates.empty()) return ToolResult::error("predicates array is required");

        json all_results = json::array();
        std::string current_subject = subject;

        for (const auto& pred : predicates) {
            json p;
            p["subject"] = current_subject;
            p["predicate"] = pred;
            auto* raw = cf_query_constraints(field_store_->handle(), p.dump().c_str());
            if (!raw) break;
            std::string result_str(raw);
            cf_free_string(raw);
            auto facts = json::parse(result_str, nullptr, false);
            if (facts.is_discarded() || facts.empty()) break;
            all_results.push_back({{"predicate", pred}, {"facts", facts}});
            // Follow first match's object as next subject
            if (facts[0].contains("object"))
                current_subject = facts[0]["object"].get<std::string>();
        }

        return ToolResult::ok("Chain: " + std::to_string(all_results.size()) + " hops", {{"chain", all_results}});
    }

    ToolResult tool_explain_fact(const json& params) {
        uint64_t fact_id = params.value("fact_id", uint64_t(0));
        if (fact_id == 0) return ToolResult::error("fact_id is required");

        auto* raw = cf_explain_constraint(field_store_->handle(), fact_id);
        if (!raw) return ToolResult::error("fact not found");
        std::string result_str(raw);
        cf_free_string(raw);

        auto result = json::parse(result_str, nullptr, false);
        if (result.is_discarded()) return ToolResult::error("parse error");
        return ToolResult::ok("Explanation for fact #" + std::to_string(fact_id), result);
    }

    ToolResult tool_branch_create(const json& params) {
        uint64_t parent_id = params.value("parent_id", uint64_t(0));
        std::string scope = params.value("scope", "global");

        int64_t branch_id = cf_create_constraint_branch(field_store_->handle(), parent_id, scope.c_str());
        if (branch_id < 0) return ToolResult::error("failed to create branch");
        return ToolResult::ok("Created branch #" + std::to_string(branch_id),
            {{"branch_id", branch_id}});
    }

    ToolResult tool_branch_resolve(const json& params) {
        uint64_t winner_id = params.value("winner_id", uint64_t(0));
        uint64_t loser_id  = params.value("loser_id", uint64_t(0));
        if (winner_id == 0 || loser_id == 0)
            return ToolResult::error("winner_id and loser_id are required");

        int r = cf_resolve_constraint_branch(field_store_->handle(), winner_id, loser_id);
        if (r != 0) return ToolResult::error("branch resolution failed");
        return ToolResult::ok("Resolved: branch #" + std::to_string(winner_id) +
            " wins over #" + std::to_string(loser_id));
    }

    // ── Layer 2: Trigger Tissue ─────────────────────────────────────────

    ToolResult tool_trigger_add(const json& params) {
        std::string name = params.value("name", "");
        if (name.empty()) return ToolResult::error("name is required");
        if (!params.contains("condition") || !params.contains("action"))
            return ToolResult::error("condition and action are required");

        json p;
        p["name"]      = name;
        p["condition"]  = params["condition"];
        p["action"]     = params["action"];
        p["deadline_ms"]       = params.value("deadline_ms", 0);
        p["tension_threshold"] = params.value("tension_threshold", 0.8);
        p["gain"]              = params.value("gain", 0.5);
        p["realm"]             = params.value("realm", "global");
        if (params.contains("session_id"))
            p["session_id"] = params["session_id"];

        int64_t trigger_id = cf_add_trigger(field_store_->handle(), p.dump().c_str());
        if (trigger_id < 0) return ToolResult::error("failed to add trigger");
        return ToolResult::ok("Armed trigger #" + std::to_string(trigger_id) + " (" + name + ")",
            {{"trigger_id", trigger_id}});
    }

    ToolResult tool_trigger_list(const json&) {
        auto* raw = cf_list_triggers(field_store_->handle());
        if (!raw) return ToolResult::ok("[]", json::array());
        std::string result_str(raw);
        cf_free_string(raw);

        auto triggers = json::parse(result_str, nullptr, false);
        if (triggers.is_discarded()) triggers = json::array();

        size_t armed = 0;
        for (const auto& t : triggers)
            if (t.value("status", "") == "Armed") ++armed;

        return ToolResult::ok(std::to_string(triggers.size()) + " triggers (" +
            std::to_string(armed) + " armed)", {{"triggers", triggers}});
    }

    ToolResult tool_trigger_fire(const json& params) {
        uint64_t trigger_id = params.value("trigger_id", uint64_t(0));
        if (trigger_id == 0) return ToolResult::error("trigger_id is required");

        auto* raw = cf_fire_trigger(field_store_->handle(), trigger_id);
        if (!raw) return ToolResult::error("trigger not found or not armed");
        std::string result_str(raw);
        cf_free_string(raw);

        auto result = json::parse(result_str, nullptr, false);
        if (result.is_discarded()) return ToolResult::error("parse error");
        return ToolResult::ok("Fired trigger #" + std::to_string(trigger_id), result);
    }

    ToolResult tool_trigger_dismiss(const json& params) {
        uint64_t trigger_id = params.value("trigger_id", uint64_t(0));
        if (trigger_id == 0) return ToolResult::error("trigger_id is required");

        int r = cf_dismiss_trigger(field_store_->handle(), trigger_id);
        if (r != 0) return ToolResult::error("trigger not found or not armed");
        return ToolResult::ok("Dismissed trigger #" + std::to_string(trigger_id));
    }

    // ── Layer 3: Predictive Memory ──────────────────────────────────────

    ToolResult tool_predict_needed(const json& params) {
        size_t k = static_cast<size_t>(params.value("k", 8));

        auto* raw = cf_predict_needed(field_store_->handle(), k);
        if (!raw) return ToolResult::ok("[]", json::array());
        std::string result_str(raw);
        cf_free_string(raw);

        auto predictions = json::parse(result_str, nullptr, false);
        if (predictions.is_discarded()) predictions = json::array();

        // Get stats
        auto* stats_raw = cf_constraint_stats(field_store_->handle());
        json stats;
        if (stats_raw) {
            stats = json::parse(stats_raw, nullptr, false);
            cf_free_string(stats_raw);
        }

        return ToolResult::ok(std::to_string(predictions.size()) + " predicted memories",
            {{"predictions", predictions}, {"stats", stats}});
    }
