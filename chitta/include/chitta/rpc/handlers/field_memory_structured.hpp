// Included into FieldRpcHandler class body — not a standalone header.
// Three-lens structured recall and natural language insight query (ask).

    ToolResult tool_structured_recall(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) return ToolResult::error("query is required");

        size_t limit      = static_cast<size_t>(params.value("limit", 15));
        std::string realm = params.value("realm", "");

        // Affective context for mood-congruent recall (Bower 1981) and
        // frustration-escalation correction boost.
        float qv = std::numeric_limits<float>::quiet_NaN();
        float qa = std::numeric_limits<float>::quiet_NaN();
        if (params.contains("query_valence") && params.contains("query_arousal")) {
            qv = params["query_valence"].get<float>();
            qa = params["query_arousal"].get<float>();
        }
        bool has_affect = !std::isnan(qv);

        // ── Lens 1: direct facts — semantic + keyword on raw query ────────────
        auto emb1 = embed_query(query);
        json lens1_results = json::array();
        if (!emb1.empty()) {
            auto sem_hits = has_affect
                ? field_store_->recall_ctx(emb1, limit, realm, qv, qa)
                : field_store_->recall(emb1, limit, realm);
            auto kw_hits  = field_store_->recall_keyword(query, limit);
            lens1_results = merge_results(hits_to_results_json(sem_hits), hits_to_results_json(kw_hits));
        }

        // ── Lens 2: context — semantic on expanded query ──────────────────────
        // Expand: prepend domain framing to pick up related background knowledge
        std::string ctx_query = "context background implications related to: " + query;
        auto emb2 = embed_query(ctx_query);
        json lens2_results = json::array();
        if (!emb2.empty()) {
            auto ctx_hits = has_affect
                ? field_store_->recall_ctx(emb2, limit, realm, qv, qa)
                : field_store_->recall(emb2, limit, realm);
            lens2_results = hits_to_results_json(ctx_hits);
        }

        // ── Lens 3: temporal — recent memories + corrections ──────────────────
        // Last 30 days + keyword filter for corrections/events
        int64_t now_ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch()).count();
        int64_t start_ms = now_ms - (int64_t)30 * 24 * 3600 * 1000;
        auto temp_hits   = field_store_->recall_temporal(start_ms, now_ms, limit, realm);
        // Also search explicitly for corrections related to the query
        auto corr_hits   = field_store_->recall_keyword("correction supersedes " + query, limit / 2);
        json lens3_results = merge_results(
            hits_to_results_json(temp_hits),
            hits_to_results_json(corr_hits)
        );

        // ── Merge all three lenses ─────────────────────────────────────────────
        json merged = merge_results(merge_results(lens1_results, lens2_results), lens3_results);
        if (merged.size() > limit) merged.erase(merged.begin() + static_cast<int>(limit), merged.end());

        // Tag each result with which lens(es) found it
        std::unordered_set<std::string> lens1_ids, lens2_ids, lens3_ids;
        auto collect_ids = [](const json& arr, std::unordered_set<std::string>& ids) {
            for (auto& r : arr) ids.insert(r.value("id", std::string{}));
        };
        collect_ids(lens1_results, lens1_ids);
        collect_ids(lens2_results, lens2_ids);
        collect_ids(lens3_results, lens3_ids);

        for (auto& r : merged) {
            std::string id = r.value("id", std::string{});
            std::string lens;
            if (lens1_ids.count(id)) lens += "F";   // Facts
            if (lens2_ids.count(id)) lens += "C";   // Context
            if (lens3_ids.count(id)) lens += "T";   // Temporal
            r["lens"] = lens.empty() ? "F" : lens;
        }

        fire_recall_callback(merged, 1);

        std::ostringstream ss;
        ss << "Structured recall [" << merged.size() << " results, FCT lenses]:\n";
        for (const auto& r : merged) {
            int pct = static_cast<int>(r.value("relevance", 0.0f) * 100);
            ss << "[" << pct << "% " << r.value("lens","?") << "] "
               << r.value("text","").substr(0, 500) << "\n";
        }
        return ToolResult::ok(ss.str(), {{"results", merged}, {"realm", realm}});
    }

    // ── ask: natural language insight query ───────────────────────────────────
    // Takes a natural language question, retrieves relevant memories via
    // structured recall, and synthesizes a grounded answer from them.
    // Answer is formatted for direct use by the agent without further LLM calls.
    ToolResult tool_ask(const json& params) {
        std::string question = params.value("question", params.value("query", ""));
        if (question.empty()) return ToolResult::error("question is required");

        size_t limit      = static_cast<size_t>(params.value("limit", 20));
        std::string realm = params.value("realm", "");

        // Reuse structured recall for three-lens retrieval
        json sr_params = {{"query", question}, {"limit", limit}, {"realm", realm}};
        auto sr = tool_structured_recall(sr_params);
        if (!sr.structured.contains("results")) return sr;

        const auto& results = sr.structured["results"];
        if (results.empty()) {
            return ToolResult::ok(
                "No relevant memories found for: " + question,
                {{"answer", ""}, {"memories_used", 0}}
            );
        }

        // Group by lens
        std::vector<json> facts, context, temporal;
        for (const auto& r : results) {
            std::string lens = r.value("lens", "F");
            if (lens.find('T') != std::string::npos)        temporal.push_back(r);
            else if (lens.find('C') != std::string::npos)   context.push_back(r);
            else                                             facts.push_back(r);
        }

        // Build synthesis
        std::ostringstream answer;
        answer << "## Memory synthesis: " << question << "\n\n";

        if (!facts.empty()) {
            answer << "**Direct knowledge:**\n";
            for (const auto& m : facts) {
                answer << "- " << m.value("text", "").substr(0, 500) << "\n";
            }
            answer << "\n";
        }
        if (!context.empty()) {
            answer << "**Related context:**\n";
            for (const auto& m : context) {
                answer << "- " << m.value("text", "").substr(0, 500) << "\n";
            }
            answer << "\n";
        }
        if (!temporal.empty()) {
            answer << "**Recent / updates:**\n";
            for (const auto& m : temporal) {
                answer << "- " << m.value("text", "").substr(0, 500) << "\n";
            }
            answer << "\n";
        }

        answer << "---\n";
        answer << "Based on " << results.size() << " memories";
        if (!realm.empty()) answer << " in realm " << realm;
        answer << ".";

        json out;
        out["answer"]        = answer.str();
        out["memories_used"] = (int)results.size();
        out["results"]       = results;
        out["question"]      = question;
        return ToolResult::ok(answer.str(), out);
    }
