// Included into FieldRpcHandler class body — not a standalone header.
// Operator control tools: approve_memory, reject_memory, conflict_inspector, disable_source, memory_history.

    ToolResult tool_approve_memory(const json& params) {
        std::string id_str = params.value("id", "");
        if (id_str.empty()) return ToolResult::error("id is required");

        uint64_t id = 0;
        try { id = std::stoull(id_str); } catch (...) {
            return ToolResult::error("invalid id");
        }

        std::string meta_json = field_store_->get_memory_metadata(id);
        if (meta_json.empty()) return ToolResult::error("memory not found");

        auto meta = json::parse(meta_json, nullptr, false);
        if (meta.is_discarded()) return ToolResult::error("failed to parse metadata");

        std::string status = meta.value("status", "Active");
        if (status != "Proposed") {
            return ToolResult::error("memory is not in Proposed state (current: " + status + ")");
        }

        field_store_->set_memory_status(id, 0); // 0 = Active
        return ToolResult::ok(
            "Memory #" + id_str + " approved: Proposed -> Active",
            {{"id", id_str}, {"old_status", "Proposed"}, {"new_status", "Active"}}
        );
    }

    ToolResult tool_reject_memory(const json& params) {
        std::string id_str = params.value("id", "");
        if (id_str.empty()) return ToolResult::error("id is required");

        uint64_t id = 0;
        try { id = std::stoull(id_str); } catch (...) {
            return ToolResult::error("invalid id");
        }

        std::string meta_json = field_store_->get_memory_metadata(id);
        if (meta_json.empty()) return ToolResult::error("memory not found");

        auto meta = json::parse(meta_json, nullptr, false);
        if (meta.is_discarded()) return ToolResult::error("failed to parse metadata");

        std::string status = meta.value("status", "Active");
        if (status != "Proposed") {
            return ToolResult::error("memory is not in Proposed state (current: " + status + ")");
        }

        field_store_->set_memory_status(id, 3); // 3 = Archived
        return ToolResult::ok(
            "Memory #" + id_str + " rejected: Proposed -> Archived",
            {{"id", id_str}, {"old_status", "Proposed"}, {"new_status", "Archived"}}
        );
    }

    ToolResult tool_promote_memory(const json& params) {
        std::string id_str = params.value("id", "");
        if (id_str.empty()) return ToolResult::error("id is required");

        uint64_t id = 0;
        try { id = std::stoull(id_str); } catch (...) {
            return ToolResult::error("invalid id");
        }

        std::string meta_json = field_store_->get_memory_metadata(id);
        if (meta_json.empty()) return ToolResult::error("memory not found");

        auto meta = json::parse(meta_json, nullptr, false);
        if (meta.is_discarded()) return ToolResult::error("failed to parse metadata");

        std::string status = meta.value("status", "Active");

        // Promotion chain: Proposed(4)->Observed(5)->Verified(6)->Active(0)
        static const std::map<std::string, std::pair<uint8_t, std::string>> chain = {
            {"Proposed", {5, "Observed"}},
            {"Observed", {6, "Verified"}},
            {"Verified", {0, "Active"}},
        };

        auto it = chain.find(status);
        if (it == chain.end()) {
            return ToolResult::error(
                "cannot promote memory in status " + status +
                " (only Proposed/Observed/Verified can be promoted)");
        }

        field_store_->set_memory_status(id, it->second.first);
        std::string new_status = it->second.second;
        return ToolResult::ok(
            "Memory #" + id_str + " promoted: " + status + " → " + new_status,
            {{"id", id_str}, {"old_status", status}, {"new_status", new_status}}
        );
    }

    ToolResult tool_conflict_inspector(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) return ToolResult::error("query is required");

        size_t limit = static_cast<size_t>(params.value("limit", 10));
        std::string realm = params.value("realm", "");

        auto embedding = embed_query(query);
        if (embedding.empty()) return ToolResult::error("Failed to embed query");

        auto hits = field_store_->recall(embedding, limit, realm);

        json results = json::array();
        std::ostringstream ss;
        ss << "Conflict inspector: " << hits.size() << " memories scanned\n\n";

        for (const auto& h : hits) {
            auto conflicts = field_store_->get_conflicts(h.memory_id);

            std::string meta_json = field_store_->get_memory_metadata(h.memory_id);
            auto meta = json::parse(meta_json, nullptr, false);
            std::string status = meta.is_discarded() ? "?" : meta.value("status", "Active");

            int pct = static_cast<int>(h.score * 100);
            ss << "[" << pct << "%] [" << h.kind << "] #" << h.memory_id
               << " (status=" << status << ") " << h.content.substr(0, 120);

            json entry = {
                {"id", std::to_string(h.memory_id)},
                {"relevance", h.score},
                {"type", h.kind},
                {"status", status},
                {"snippet", h.content.substr(0, 200)},
                {"conflicts", json::array()},
            };

            if (!conflicts.empty()) {
                ss << "\n  -> Conflicts: ";
                for (size_t i = 0; i < conflicts.size(); ++i) {
                    if (i > 0) ss << ", ";
                    ss << "#" << conflicts[i];
                    std::string c_content = field_store_->get_content(conflicts[i]);
                    entry["conflicts"].push_back({
                        {"id", std::to_string(conflicts[i])},
                        {"snippet", c_content.substr(0, 200)},
                    });
                }
            }
            ss << "\n";
            results.push_back(std::move(entry));
        }

        return ToolResult::ok(ss.str(), {{"results", results}});
    }

    ToolResult tool_disable_source(const json& params) {
        std::string source = params.value("source", "");
        if (source.empty()) return ToolResult::error("source is required");

        field_store_->add_triplet("system", "denied_source", source);

        return ToolResult::ok(
            "Source '" + source + "' added to deny-list. New memories from this source will be rejected.",
            {{"source", source}, {"action", "denied"}}
        );
    }

    ToolResult tool_operator_memory_history(const json& params) {
        std::string id_str = params.value("id", "");
        if (id_str.empty()) return ToolResult::error("id is required");

        uint64_t id = 0;
        try { id = std::stoull(id_str); } catch (...) {
            return ToolResult::error("invalid id");
        }

        std::string meta_json = field_store_->get_memory_metadata(id);
        if (meta_json.empty()) return ToolResult::error("memory not found");

        auto meta = json::parse(meta_json, nullptr, false);
        if (meta.is_discarded()) return ToolResult::error("failed to parse metadata");

        std::string content = field_store_->get_content(id);

        json snapshot = {
            {"id", id_str},
            {"kind", meta.value("kind", "")},
            {"realm", meta.value("realm", "")},
            {"status", meta.value("status", "Active")},
            {"epistemic_status", meta.value("epistemic_status", "UserStated")},
            {"confidence", meta.value("confidence", 0.0f)},
            {"strength", meta.value("strength", 0.0f)},
            {"access_count", meta.value("access_count", 0)},
            {"pinned", meta.value("pinned", false)},
            {"tier", meta.value("tier", 0)},
            {"decay_rate", meta.value("decay_rate", 0.0f)},
            {"created_at_ms", meta.value("created_at_ms", int64_t(0))},
            {"last_accessed_ms", meta.value("last_accessed_ms", int64_t(0))},
            {"last_strengthened_ms", meta.value("last_strengthened_ms", int64_t(0))},
            {"last_state_op_ts_ms", meta.value("last_state_op_ts_ms", int64_t(0))},
            {"content_preview", content.substr(0, 500)},
        };

        auto conflicts = field_store_->get_conflicts(id);
        auto confirmations = field_store_->get_confirmations(id);
        auto chain = field_store_->get_supersession_chain(id);

        snapshot["conflicts"] = conflicts;
        snapshot["confirmations"] = confirmations;
        snapshot["supersession_chain"] = chain;

        std::ostringstream ss;
        ss << "Memory #" << id << " history:\n"
           << "  Status: " << meta.value("status", "Active")
           << " | Epistemic: " << meta.value("epistemic_status", "UserStated") << "\n"
           << "  Confidence: " << meta.value("confidence", 0.0f)
           << " | Strength: " << meta.value("strength", 0.0f)
           << " | Access count: " << meta.value("access_count", 0) << "\n"
           << "  Tier: " << meta.value("tier", 0)
           << " | Pinned: " << (meta.value("pinned", false) ? "yes" : "no") << "\n"
           << "  Conflicts: " << conflicts.size()
           << " | Confirmations: " << confirmations.size()
           << " | Supersession chain: " << chain.size() << " entries\n"
           << "  Content: " << content.substr(0, 300) << "\n";

        return ToolResult::ok(ss.str(), {{"history", json::array({snapshot})}});
    }
