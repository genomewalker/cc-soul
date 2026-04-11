// Included into FieldRpcHandler class body — not a standalone header.
// Layer 4: Surprise Memory — prediction error tuples that reveal blind spots.
// Layer 5: Epistemic Debt — uncertainty boundaries and competing hypotheses.
// Layer 6: Integration Kernel — recall source arbitration with learned weights.
//
// References:
//   [1] Friston (2010). The free-energy principle: a unified brain theory?
//   [2] Sperber et al. (2010). Epistemic vigilance.
//   [3] Shazeer et al. (2017). Outrageously large neural networks (MoE gating).

    // ── Layer 4: Surprise Memory ───────────────────────────────────────

    ToolResult tool_record_surprise(const json& params) {
        json p;
        p["context_sketch"]     = params.value("context_sketch", "");
        p["action"]             = params.value("action", "");
        p["actual"]             = params.value("actual", "");
        p["surprise_magnitude"] = params.value("surprise_magnitude", 0.5);
        p["domain"]             = params.value("domain", "general");
        p["realm"]              = params.value("realm", "global");
        if (params.contains("expected"))
            p["expected"] = params["expected"];
        if (params.contains("session_id"))
            p["session_id"] = params["session_id"];
        if (params.contains("source_memory_id"))
            p["source_memory_id"] = params["source_memory_id"];

        if (p["actual"].get<std::string>().empty())
            return ToolResult::error("actual is required");

        auto* raw = cf_record_surprise(field_store_->handle(), p.dump().c_str());
        if (!raw) return ToolResult::error("record_surprise failed");
        std::string result_str(raw);
        cf_free_string(raw);

        auto result = json::parse(result_str, nullptr, false);
        if (result.is_discarded()) return ToolResult::error("parse error");

        return ToolResult::ok("Recorded surprise #" + std::to_string(result.value("event_id", 0)), result);
    }

    ToolResult tool_query_surprises(const json& params) {
        json p;
        if (params.contains("domain"))        p["domain"] = params["domain"];
        if (params.contains("realm"))         p["realm"] = params["realm"];
        if (params.contains("min_magnitude")) p["min_magnitude"] = params["min_magnitude"];
        if (params.contains("since_ms"))      p["since_ms"] = params["since_ms"];
        p["limit"] = params.value("limit", 50);

        auto* raw = cf_query_surprises(field_store_->handle(), p.dump().c_str());
        if (!raw) return ToolResult::ok("[]", json::array());
        std::string result_str(raw);
        cf_free_string(raw);

        auto results = json::parse(result_str, nullptr, false);
        if (results.is_discarded()) results = json::array();

        return ToolResult::ok(std::to_string(results.size()) + " surprise events",
            {{"events", results}, {"count", results.size()}});
    }

    ToolResult tool_get_blind_spots(const json& params) {
        json p;
        if (params.contains("realm")) p["realm"] = params["realm"];
        p["limit"] = params.value("limit", 10);

        auto* raw = cf_get_blind_spots(field_store_->handle(), p.dump().c_str());
        if (!raw) return ToolResult::ok("[]", json::array());
        std::string result_str(raw);
        cf_free_string(raw);

        auto results = json::parse(result_str, nullptr, false);
        if (results.is_discarded()) results = json::array();

        return ToolResult::ok(std::to_string(results.size()) + " blind spots identified",
            {{"blind_spots", results}, {"count", results.size()}});
    }

    ToolResult tool_surprise_stats(const json&) {
        auto* raw = cf_surprise_stats(field_store_->handle());
        if (!raw) return ToolResult::error("surprise_stats failed");
        std::string result_str(raw);
        cf_free_string(raw);

        auto result = json::parse(result_str, nullptr, false);
        if (result.is_discarded()) return ToolResult::error("parse error");
        return ToolResult::ok("Surprise memory stats", result);
    }

    // ── Layer 5: Epistemic Debt ────────────────────────────────────────

    ToolResult tool_register_debt(const json& params) {
        json p;
        p["pattern"]        = params.value("pattern", "");
        p["fragility_score"] = params.value("fragility_score", 0.5);
        p["domain"]         = params.value("domain", "general");
        p["realm"]          = params.value("realm", "global");
        if (params.contains("competing_hypotheses"))
            p["competing_hypotheses"] = params["competing_hypotheses"];
        if (params.contains("discriminating_test"))
            p["discriminating_test"] = params["discriminating_test"];
        if (params.contains("session_id"))
            p["session_id"] = params["session_id"];

        if (p["pattern"].get<std::string>().empty())
            return ToolResult::error("pattern is required");

        auto* raw = cf_register_debt(field_store_->handle(), p.dump().c_str());
        if (!raw) return ToolResult::error("register_debt failed");
        std::string result_str(raw);
        cf_free_string(raw);

        auto result = json::parse(result_str, nullptr, false);
        if (result.is_discarded()) return ToolResult::error("parse error");

        return ToolResult::ok("Registered debt #" + std::to_string(result.value("debt_id", 0)), result);
    }

    ToolResult tool_resolve_debt(const json& params) {
        uint64_t debt_id = params.value("debt_id", uint64_t(0));
        if (debt_id == 0) return ToolResult::error("debt_id is required");
        std::string resolution = params.value("resolution", "");
        if (resolution.empty()) return ToolResult::error("resolution is required");

        int r = cf_resolve_debt(field_store_->handle(), debt_id, resolution.c_str());
        if (r != 0) return ToolResult::error("debt not found");
        return ToolResult::ok("Resolved debt #" + std::to_string(debt_id));
    }

    ToolResult tool_defer_debt(const json& params) {
        uint64_t debt_id = params.value("debt_id", uint64_t(0));
        if (debt_id == 0) return ToolResult::error("debt_id is required");

        int r = cf_defer_debt(field_store_->handle(), debt_id);
        if (r != 0) return ToolResult::error("debt not found");
        return ToolResult::ok("Deferred debt #" + std::to_string(debt_id));
    }

    ToolResult tool_query_debts(const json& params) {
        json p;
        if (params.contains("status"))        p["status"] = params["status"];
        if (params.contains("domain"))        p["domain"] = params["domain"];
        if (params.contains("realm"))         p["realm"] = params["realm"];
        if (params.contains("min_fragility")) p["min_fragility"] = params["min_fragility"];
        p["limit"] = params.value("limit", 50);

        auto* raw = cf_query_debts(field_store_->handle(), p.dump().c_str());
        if (!raw) return ToolResult::ok("[]", json::array());
        std::string result_str(raw);
        cf_free_string(raw);

        auto results = json::parse(result_str, nullptr, false);
        if (results.is_discarded()) results = json::array();

        return ToolResult::ok(std::to_string(results.size()) + " debts found",
            {{"debts", results}, {"count", results.size()}});
    }

    ToolResult tool_get_fragile_decisions(const json& params) {
        json p;
        p["threshold"] = params.value("threshold", 0.5);
        p["limit"]     = params.value("limit", 20);

        auto* raw = cf_get_fragile_decisions(field_store_->handle(), p.dump().c_str());
        if (!raw) return ToolResult::ok("[]", json::array());
        std::string result_str(raw);
        cf_free_string(raw);

        auto results = json::parse(result_str, nullptr, false);
        if (results.is_discarded()) results = json::array();

        return ToolResult::ok(std::to_string(results.size()) + " fragile decisions",
            {{"debts", results}, {"count", results.size()}});
    }

    ToolResult tool_debt_stats(const json&) {
        auto* raw = cf_debt_stats(field_store_->handle());
        if (!raw) return ToolResult::error("debt_stats failed");
        std::string result_str(raw);
        cf_free_string(raw);

        auto result = json::parse(result_str, nullptr, false);
        if (result.is_discarded()) return ToolResult::error("parse error");
        return ToolResult::ok("Epistemic debt stats", result);
    }

    // ── Layer 6: Integration Kernel ────────────────────────────────────

    ToolResult tool_record_feedback(const json& params) {
        json p;
        p["query_domain"] = params.value("query_domain", "general");
        p["source"]       = params.value("source", "");
        p["was_useful"]   = params.value("was_useful", true);

        if (p["source"].get<std::string>().empty())
            return ToolResult::error("source is required");

        auto* raw = cf_record_feedback(field_store_->handle(), p.dump().c_str());
        if (!raw) return ToolResult::error("record_feedback failed");
        std::string result_str(raw);
        cf_free_string(raw);

        auto result = json::parse(result_str, nullptr, false);
        if (result.is_discarded()) return ToolResult::error("parse error");

        return ToolResult::ok("Updated weight for " + p["source"].get<std::string>() +
            " → " + std::to_string(result.value("weight", 0.0)), result);
    }

    ToolResult tool_get_source_weights(const json& params) {
        json p;
        if (params.contains("domain")) p["domain"] = params["domain"];

        auto* raw = cf_get_source_weights(field_store_->handle(), p.dump().c_str());
        if (!raw) return ToolResult::ok("[]", json::array());
        std::string result_str(raw);
        cf_free_string(raw);

        auto results = json::parse(result_str, nullptr, false);
        if (results.is_discarded()) results = json::array();

        return ToolResult::ok(std::to_string(results.size()) + " source weights",
            {{"weights", results}, {"count", results.size()}});
    }

    ToolResult tool_update_source_weight(const json& params) {
        json p;
        p["source"] = params.value("source", "");
        p["domain"] = params.value("domain", "general");
        p["weight"] = params.value("weight", 1.0);

        if (p["source"].get<std::string>().empty())
            return ToolResult::error("source is required");

        int r = cf_update_source_weight(field_store_->handle(), p.dump().c_str());
        if (r != 0) return ToolResult::error("update failed");
        return ToolResult::ok("Updated weight for " + p["source"].get<std::string>() +
            " in domain " + p["domain"].get<std::string>());
    }

    ToolResult tool_integration_stats(const json&) {
        auto* raw = cf_integration_stats(field_store_->handle());
        if (!raw) return ToolResult::error("integration_stats failed");
        std::string result_str(raw);
        cf_free_string(raw);

        auto result = json::parse(result_str, nullptr, false);
        if (result.is_discarded()) return ToolResult::error("parse error");
        return ToolResult::ok("Integration kernel stats", result);
    }
