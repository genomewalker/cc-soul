// Included into FieldRpcHandler class body — not a standalone header.
// Memory operation tools: strengthen, weaken, forget, observe, grow, update, query,
// explore, kind/priority ops, and triplet graph tools.

    DuckDBToolResult tool_strengthen(const json& params) {
        uint64_t id  = extract_id(params);
        float amount = params.value("amount", 0.1f);
        if (id == 0) return DuckDBToolResult::error("id is required");
        field_store_->strengthen(id, amount);
        return DuckDBToolResult::ok("Strengthened memory #" + std::to_string(id));
    }

    DuckDBToolResult tool_weaken(const json& params) {
        uint64_t id  = extract_id(params);
        float amount = params.value("amount", 0.1f);
        if (id == 0) return DuckDBToolResult::error("id is required");
        field_store_->weaken(id, amount);
        return DuckDBToolResult::ok("Weakened memory #" + std::to_string(id));
    }

    DuckDBToolResult tool_forget(const json& params) {
        uint64_t id = extract_id(params);
        if (id == 0) return DuckDBToolResult::error("id is required");
        field_store_->forget(id);
        return DuckDBToolResult::ok("Forgot memory #" + std::to_string(id));
    }

    DuckDBToolResult tool_batch_forget(const json& params) {
        size_t count = 0;
        if (params.contains("ids") && params["ids"].is_array()) {
            for (const auto& v : params["ids"]) {
                uint64_t id = 0;
                if (v.is_number_integer()) id = static_cast<uint64_t>(v.get<int64_t>());
                else if (v.is_string()) { try { id = std::stoull(v.get<std::string>()); } catch (...) {} }
                if (id != 0) { field_store_->forget(id); count++; }
            }
        }
        if (params.contains("pattern") && params["pattern"].is_string()) {
            std::string pattern = params["pattern"].get<std::string>();
            auto hits = field_store_->recall_keyword(pattern, 100);
            for (const auto& h : hits) {
                field_store_->forget(h.memory_id);
                count++;
            }
        }
        return DuckDBToolResult::ok("Forgot " + std::to_string(count) + " memories", {{"count", count}});
    }

    // ── Observe (hook learning) ──────────────────────────────────────────────

    DuckDBToolResult tool_observe(const json& params) {
        std::string title   = params.value("title", "");
        std::string content = params.value("content", "");
        if (content.empty()) return DuckDBToolResult::error("content is required");

        std::string category = params.value("category", "episode");
        std::string realm    = params.value("realm", "brahman");
        std::string source   = params.value("source", "mcp_tool");
        std::string evidence = params.value("evidence", "");
        float confidence = params.contains("confidence")
            ? params["confidence"].get<float>()
            : category_to_confidence(category);

        std::string ssl_content = to_ssl_format(content, category);

        auto embedding = embed_text(ssl_content);
        uint64_t id = field_store_->remember(category, realm, ssl_content, embedding, confidence, 0.001f);

        // Provenance: source + evidence + epistemic status + initial lifecycle status
        if (id > 0 && !source.empty()) {
            field_store_->add_triplet(std::to_string(id), "source", source);
            if (!evidence.empty())
                field_store_->add_triplet(std::to_string(id), "evidence", evidence);
            // Epistemic: 0=UserStated,1=ToolDerived(default),2=ModelInferred,3=AutonomousSynthesis
            uint8_t es = (source == "mcp_tool") ? 0 : (source == "distillation" || source == "system") ? 2 : 1;
            // Initial lifecycle: 4=Proposed(hooks), 0=Active(distillation/mcp)
            uint8_t ms = (source == "hook_regex" || source == "hook_compliance") ? 4 : 0;
            if (es != 1) field_store_->set_epistemic_status(id, es);
            if (ms != 0) field_store_->set_memory_status(id, ms);
        }
        if (params.contains("tags") && params["tags"].is_string()) {
            std::istringstream iss(params["tags"].get<std::string>());
            std::string tag;
            while (std::getline(iss, tag, ',')) {
                tag.erase(0, tag.find_first_not_of(' '));
                tag.erase(tag.find_last_not_of(' ') + 1);
                if (!tag.empty()) {
                    field_store_->add_triplet(std::to_string(id), "tagged", tag);
                }
            }
        }

        // Correction supersession: when category=correction, weaken semantically similar memories
        if (category == "correction" && id > 0 && !embedding.empty()) {
            auto hits = field_store_->recall(embedding, 5, realm);
            std::string target_id = params.value("target_id", "");
            if (!target_id.empty()) {
                // Explicit target
                try {
                    uint64_t tid = std::stoull(target_id);
                    field_store_->add_triplet(std::to_string(id), "supersedes", target_id, 1.0f, id);
                    field_store_->weaken(tid, 0.15f);
                    field_store_->set_memory_status(tid, 1); // Superseded
                } catch (...) {}
            } else {
                for (const auto& h : hits) {
                    if (h.memory_id == id) continue;
                    if (h.realm != realm) continue;
                    if (h.kind == "correction") continue;
                    if (h.semantic_score < 0.88f) continue;  // slightly lower threshold for direct calls
                    field_store_->add_triplet(std::to_string(id), "supersedes", std::to_string(h.memory_id), 1.0f, id);
                    field_store_->weaken(h.memory_id, 0.15f);
                    field_store_->set_memory_status(h.memory_id, 1);
                }
            }
        }

        return DuckDBToolResult::ok("Observed [" + category + "] #" + std::to_string(id),
            {{"id", std::to_string(id)}, {"category", category}, {"confidence", confidence}});
    }

    DuckDBToolResult tool_grow(const json& params) {
        std::string type    = params.value("type", "wisdom");
        std::string content = params.value("content", "");
        if (content.empty()) return DuckDBToolResult::error("content is required");

        std::string title = params.value("title", "");
        std::string ssl = title.empty() ? to_ssl_format(content, type) :
            "[" + type + "] " + title + "\n" + content;

        auto embedding = embed_text(ssl);
        float confidence = (type == "wisdom" || type == "belief") ? 0.85f : 0.70f;
        uint64_t id = field_store_->remember(type, "brahman", ssl, embedding, confidence, 0.001f);

        if (params.contains("tags") && params["tags"].is_string()) {
            std::istringstream iss(params["tags"].get<std::string>());
            std::string tag;
            while (std::getline(iss, tag, ',')) {
                tag.erase(0, tag.find_first_not_of(' '));
                tag.erase(tag.find_last_not_of(' ') + 1);
                if (!tag.empty()) field_store_->add_triplet(std::to_string(id), "tagged", tag);
            }
        }

        return DuckDBToolResult::ok("Grew " + type + " #" + std::to_string(id),
            {{"id", std::to_string(id)}, {"type", type}});
    }

    DuckDBToolResult tool_get(const json& params) {
        uint64_t id = extract_id(params);
        if (id == 0) return DuckDBToolResult::error("id is required");

        std::string content = field_store_->get_content(id);
        if (content.empty()) return DuckDBToolResult::error("Memory not found: " + std::to_string(id));

        std::string meta_json = field_store_->get_memory_metadata(id);
        json meta = meta_json.empty() ? json::object() : json::parse(meta_json, nullptr, false);

        json result;
        result["id"] = std::to_string(id);
        result["content"] = content;
        if (meta.is_object()) {
            result["type"]       = meta.value("kind", "");
            result["realm"]      = meta.value("realm", "");
            result["confidence"] = meta.value("confidence", 0.0f);
            result["strength"]   = meta.value("strength", 0.0f);
        }

        return DuckDBToolResult::ok(content.substr(0, 500), result);
    }

    DuckDBToolResult tool_expand_memory(const json& params) {
        uint64_t id = extract_id(params);
        if (id == 0) return DuckDBToolResult::error("id is required");

        std::string content = field_store_->get_content(id);
        if (content.empty()) return DuckDBToolResult::error("Memory not found");

        std::string meta_json = field_store_->get_memory_metadata(id);
        json result;
        result["id"] = std::to_string(id);
        result["content"] = content;
        if (!meta_json.empty()) {
            json meta = json::parse(meta_json, nullptr, false);
            if (meta.is_object()) result["metadata"] = meta;
        }

        // Expand associations
        std::vector<uint64_t> seeds = {id};
        auto assoc = field_store_->expand_associations(seeds, 2, 10);
        if (!assoc.empty()) {
            result["associations"] = hits_to_results_json(assoc);
        }

        return DuckDBToolResult::ok(content, result);
    }

    DuckDBToolResult tool_update(const json& params) {
        uint64_t id = extract_id(params);
        std::string content = params.value("content", "");
        if (id == 0 || content.empty()) return DuckDBToolResult::error("id and content are required");

        // Re-store: forget old, remember new with same kind/realm
        std::string meta_json = field_store_->get_memory_metadata(id);
        std::string kind = "episode", realm = "brahman";
        float confidence = 0.8f;
        if (!meta_json.empty()) {
            json meta = json::parse(meta_json, nullptr, false);
            if (meta.is_object()) {
                kind = meta.value("kind", kind);
                realm = meta.value("realm", realm);
                confidence = meta.value("confidence", confidence);
            }
        }

        field_store_->forget(id);
        auto embedding = embed_text(content);
        uint64_t new_id = field_store_->remember(kind, realm, content, embedding, confidence, 0.001f);

        // Link old→new for audit
        field_store_->add_triplet(std::to_string(new_id), "updated_from", std::to_string(id));

        return DuckDBToolResult::ok("Updated → #" + std::to_string(new_id),
            {{"old_id", std::to_string(id)}, {"new_id", std::to_string(new_id)}});
    }

    DuckDBToolResult tool_query(const json& params) {
        std::string subject = params.value("subject", "");
        std::string object  = params.value("object", "");

        json results_json = json::array();
        std::ostringstream ss;

        if (!subject.empty()) {
            std::string raw = field_store_->query_subject(subject);
            try {
                auto arr = json::parse(raw);
                for (const auto& t : arr) {
                    ss << "  " << subject << " → " << t.value("predicate", "?") << " → " << t.value("object", "?") << "\n";
                    results_json.push_back(t);
                }
            } catch (...) {}
        }
        if (!object.empty()) {
            std::string raw = field_store_->query_object(object);
            try {
                auto arr = json::parse(raw);
                for (const auto& t : arr) {
                    ss << "  " << t.value("subject", "?") << " → " << t.value("predicate", "?") << " → " << object << "\n";
                    results_json.push_back(t);
                }
            } catch (...) {}
        }

        if (subject.empty() && object.empty()) {
            return DuckDBToolResult::error("subject or object required");
        }

        return DuckDBToolResult::ok(ss.str(), {{"triplets", results_json}});
    }

    DuckDBToolResult tool_tag(const json& params) {
        uint64_t id = extract_id(params);
        if (id == 0) return DuckDBToolResult::error("id is required");

        std::string add_tag = params.value("add", "");
        std::string rm_tag  = params.value("remove", "");

        if (!add_tag.empty()) {
            field_store_->add_triplet(std::to_string(id), "tagged", add_tag);
        }
        if (!rm_tag.empty()) {
            bool removed = field_store_->forget_triplet(std::to_string(id), "tagged", rm_tag);
            if (!removed) return DuckDBToolResult::ok("Tag not found", {{"id", std::to_string(id)}, {"tag", rm_tag}});
        }

        return DuckDBToolResult::ok("OK", {{"id", std::to_string(id)}});
    }

    // ── Exploration primitives ───────────────────────────────────────────────

    DuckDBToolResult tool_explore_recall(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) return DuckDBToolResult::error("query is required");
        size_t limit = static_cast<size_t>(params.value("limit", 10));

        auto embedding = embed_query(query);
        if (embedding.empty()) return DuckDBToolResult::error("Failed to embed query");

        auto hits = field_store_->recall(embedding, limit);
        json results = json::array();
        std::ostringstream ss;
        for (const auto& h : hits) {
            std::string preview = h.content.substr(0, 80);
            results.push_back({
                {"id", std::to_string(h.memory_id)},
                {"score", h.score},
                {"type", h.kind},
                {"preview", preview}
            });
            int pct = static_cast<int>(h.score * 100);
            ss << "[" << pct << "%] #" << h.memory_id << " " << preview << "\n";
        }
        return DuckDBToolResult::ok(ss.str(), {{"results", results}});
    }

    DuckDBToolResult tool_explore_peek(const json& params) {
        uint64_t id = extract_id(params);
        if (id == 0) return DuckDBToolResult::error("id is required");
        std::string content = field_store_->get_content(id);
        if (content.empty()) return DuckDBToolResult::error("Not found");
        std::string preview = content.substr(0, 200);
        return DuckDBToolResult::ok(preview, {{"id", std::to_string(id)}, {"preview", preview}});
    }

    DuckDBToolResult tool_explore_expand(const json& params) {
        return tool_get(params);
    }

    DuckDBToolResult tool_explore_neighbors(const json& params) {
        std::string node = params.value("node", "");
        if (node.empty()) return DuckDBToolResult::error("node is required");

        std::string raw = field_store_->list_triplets_for_entity(node, 50);
        try {
            auto arr = json::parse(raw);
            std::ostringstream ss;
            ss << "Neighbors of '" << node << "':\n";
            for (const auto& t : arr) {
                ss << "  " << t.value("subject", "?") << " → " << t.value("predicate", "?")
                   << " → " << t.value("object", "?") << "\n";
            }
            return DuckDBToolResult::ok(ss.str(), {{"triplets", arr}});
        } catch (...) {
            return DuckDBToolResult::ok(raw);
        }
    }

    // ── Memory management ────────────────────────────────────────────────────

    DuckDBToolResult tool_list_memories_brief(const json& params) {
        size_t limit      = static_cast<size_t>(params.value("limit", 200));
        std::string realm = params.value("realm", "");
        std::string kind  = params.value("kind", "");

        std::string raw = field_store_->list_memories(kind, realm, "recency", limit, 0);
        try {
            auto arr = json::parse(raw);
            std::ostringstream ss;
            json results = json::array();
            for (const auto& m : arr) {
                std::string content = m.value("content", "");
                std::string preview = content.substr(0, 80);
                json brief = {
                    {"id", m.value("id", 0)},
                    {"kind", m.value("kind", "")},
                    {"preview", preview},
                    {"confidence", m.value("confidence", 0.0f)},
                };
                results.push_back(brief);
                ss << "#" << m.value("id", 0) << " [" << m.value("kind", "?") << "] " << preview << "\n";
            }
            return DuckDBToolResult::ok(ss.str(), {{"memories", results}, {"count", results.size()}});
        } catch (...) {
            return DuckDBToolResult::ok(raw);
        }
    }

    DuckDBToolResult tool_forget_kind(const json& params) {
        std::string kind  = params.value("kind", "");
        std::string realm = params.value("realm", "");
        size_t limit      = static_cast<size_t>(params.value("limit", 5000));
        if (kind.empty()) return DuckDBToolResult::error("kind is required");

        std::string raw = field_store_->list_memories(kind, realm, "recency", limit, 0);
        json arr;
        try { arr = json::parse(raw); } catch (...) {
            return DuckDBToolResult::error("failed to list memories");
        }

        size_t deleted = 0, failed = 0;
        for (const auto& m : arr) {
            uint64_t id = 0;
            if (m["id"].is_number()) id = m["id"].get<uint64_t>();
            else if (m["id"].is_string()) {
                try { id = std::stoull(m["id"].get<std::string>()); } catch (...) {}
            }
            if (id == 0) { ++failed; continue; }
            try { field_store_->forget(id); ++deleted; } catch (...) { ++failed; }
        }

        return DuckDBToolResult::ok(
            "Deleted " + std::to_string(deleted) + " " + kind + " memories"
            + (failed ? " (" + std::to_string(failed) + " failed)" : ""),
            {{"deleted", deleted}, {"failed", failed}, {"kind", kind}});
    }

    DuckDBToolResult tool_set_priority_tier(const json& params) {
        // Priority tiers stored as triplets
        uint64_t id = extract_id(params, "memory_id");
        int tier = params.value("tier", 0);
        if (id == 0) return DuckDBToolResult::error("memory_id is required");
        field_store_->add_triplet(std::to_string(id), "priority_tier", std::to_string(tier));
        return DuckDBToolResult::ok("Set tier " + std::to_string(tier) + " for #" + std::to_string(id));
    }

    DuckDBToolResult tool_set_memory_type(const json& params) {
        uint64_t id = extract_id(params, "memory_id");
        std::string type = params.value("type", "");
        if (id == 0 || type.empty()) return DuckDBToolResult::error("memory_id and type are required");
        field_store_->update_memory_kind(id, type);
        return DuckDBToolResult::ok("Set type to '" + type + "' for #" + std::to_string(id));
    }

    DuckDBToolResult tool_memory_type_stats(const json& params) {
        std::string realm = params.value("realm", "");
        std::string raw = field_store_->memory_stats(realm);
        try {
            auto j = json::parse(raw);
            return DuckDBToolResult::ok(j.dump(2), j);
        } catch (...) {
            return DuckDBToolResult::ok(raw);
        }
    }

    DuckDBToolResult tool_recall_by_priority(const json& params) {
        // Simple: recall by strength (high confidence first = de-facto priority)
        std::string query = params.value("query", "");
        std::string realm = params.value("realm", "");
        size_t budget     = static_cast<size_t>(params.value("budget_tokens", 4000));

        std::string raw = field_store_->recall_filtered("", realm, 0.5f, 0.0f, 50);
        try {
            auto arr = json::parse(raw);
            json results = json::array();
            size_t tokens_used = 0;
            for (const auto& m : arr) {
                std::string content = m.value("content", "");
                size_t est_tokens = content.size() / 4;
                if (tokens_used + est_tokens > budget) break;
                tokens_used += est_tokens;
                results.push_back(m);
            }
            return DuckDBToolResult::ok("Priority recall: " + std::to_string(results.size()) + " memories",
                {{"results", results}, {"tokens_used", tokens_used}});
        } catch (...) {
            return DuckDBToolResult::ok(raw);
        }
    }

    DuckDBToolResult tool_expand_query(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) return DuckDBToolResult::error("query is required");
        auto eq = expand_query(query);
        return DuckDBToolResult::ok(
            "lex: " + eq.lex + "\nvec: " + eq.vec + "\nhyde: " + eq.hyde,
            {{"lex", eq.lex}, {"vec", eq.vec}, {"hyde", eq.hyde}});
    }

    DuckDBToolResult tool_connect(const json& params) {
        std::string subject   = params.value("subject", "");
        std::string predicate = params.value("predicate", "");
        std::string object    = params.value("object", "");
        if (subject.empty() || predicate.empty() || object.empty())
            return DuckDBToolResult::error("subject, predicate, and object are required");
        field_store_->add_triplet(subject, predicate, object);
        return DuckDBToolResult::ok(subject + " --[" + predicate + "]--> " + object,
            {{"subject", subject}, {"predicate", predicate}, {"object", object}});
    }

    DuckDBToolResult tool_connect_temporal(const json& params) {
        std::string subject    = params.value("subject", "");
        std::string predicate  = params.value("predicate", "");
        std::string object     = params.value("object", "");
        std::string valid_from = params.value("valid_from", "");
        std::string valid_to   = params.value("valid_to", "");
        if (subject.empty() || predicate.empty() || object.empty())
            return DuckDBToolResult::error("subject, predicate, and object are required");
        field_store_->add_triplet(subject, predicate, object);
        json payload = {{"valid_from", valid_from}, {"valid_to", valid_to}};
        field_store_->emit_event("triplet", "temporal", subject + "|" + predicate + "|" + object,
                                 payload.dump());
        return DuckDBToolResult::ok(subject + " --[" + predicate + "]--> " + object + " (temporal)",
            {{"subject", subject}, {"predicate", predicate}, {"object", object},
             {"valid_from", valid_from}, {"valid_to", valid_to}});
    }

    DuckDBToolResult tool_triplet_history(const json& params) {
        std::string subject   = params.value("subject", "");
        std::string predicate = params.value("predicate", "");
        if (subject.empty() || predicate.empty())
            return DuckDBToolResult::error("subject and predicate are required");
        auto raw = field_store_->query_subject(subject);
        auto triplets = json::parse(raw, nullptr, false);
        json results = json::array();
        if (!triplets.is_discarded() && triplets.is_array()) {
            for (const auto& t : triplets) {
                if (t.value("predicate", "") == predicate)
                    results.push_back(t);
            }
        }
        return DuckDBToolResult::ok(std::to_string(results.size()) + " history entries",
            {{"subject", subject}, {"predicate", predicate}, {"history", results}});
    }

    DuckDBToolResult tool_query_triplets_temporal(const json& params) {
        std::string subject   = params.value("subject", "");
        std::string predicate = params.value("predicate", "");
        std::string object    = params.value("object", "");
        // Query by subject or object
        json results = json::array();
        if (!subject.empty()) {
            auto raw = field_store_->query_subject(subject);
            auto triplets = json::parse(raw, nullptr, false);
            if (!triplets.is_discarded() && triplets.is_array()) {
                for (const auto& t : triplets) {
                    if (!predicate.empty() && t.value("predicate", "") != predicate) continue;
                    if (!object.empty() && t.value("object", "") != object) continue;
                    results.push_back(t);
                }
            }
        } else if (!object.empty()) {
            auto raw = field_store_->query_object(object);
            auto triplets = json::parse(raw, nullptr, false);
            if (!triplets.is_discarded() && triplets.is_array()) {
                for (const auto& t : triplets) {
                    if (!predicate.empty() && t.value("predicate", "") != predicate) continue;
                    results.push_back(t);
                }
            }
        }
        return DuckDBToolResult::ok(std::to_string(results.size()) + " triplet(s)",
            {{"triplets", results}, {"count", results.size()}});
    }

    // ── List by lifecycle status ────────────────────────────────────────────────
    DuckDBToolResult tool_list_by_status(const json& params) {
        std::string status_filter = params.value("status", "superseded");
        size_t limit = params.value("limit", 50);
        std::string realm = params.value("realm", "");

        // Over-fetch to account for filtering
        std::string raw = field_store_->list_memories("", realm, "recency", limit * 3, 0);
        json all_mems = json::parse(raw, nullptr, false);
        if (all_mems.is_discarded() || !all_mems.is_array()) {
            return DuckDBToolResult::ok("0 memories with status=" + status_filter,
                {{"memories", json::array()}, {"status_filter", status_filter}});
        }

        json filtered = json::array();
        for (const auto& m : all_mems) {
            if (filtered.size() >= limit) break;
            auto mid = std::to_string(m.value("id", uint64_t(0)));
            auto ms_params = json{{"id", mid}};
            auto ms = tool_memory_status(ms_params);
            auto status = ms.structured.value("status", "active");
            if (status_filter == "all" || status == status_filter) {
                json entry = {
                    {"id", mid},
                    {"status", status},
                    {"kind", m.value("kind", "")},
                    {"text", m.value("content", "").substr(0, 120)},
                    {"confidence", m.value("confidence", 0.0f)},
                };
                filtered.push_back(entry);
            }
        }
        std::ostringstream ss;
        ss << filtered.size() << " memories with status=" << status_filter << "\n";
        for (const auto& entry : filtered) {
            ss << "  [" << entry.value("status","?") << "] #" << entry.value("id","?")
               << " " << entry.value("text","").substr(0,80) << "\n";
        }
        return DuckDBToolResult::ok(ss.str(), {{"memories", filtered}, {"status_filter", status_filter}});
    }

    // ── Structured recall: three-lens retrieval ───────────────────────────────
    // Lens 1 (facts):    semantic search on the raw query
    // Lens 2 (context):  semantic search on an expanded contextual variant
    // Lens 3 (temporal): temporal search + corrections/updates filter
