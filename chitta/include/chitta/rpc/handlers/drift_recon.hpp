// Included into FieldRpcHandler class body — not a standalone header.
// Reconsolidation: evidence typing + memory lability for drift-memory features.
//
// Tools:
//   set_evidence_type  — tag a memory with its evidence category
//   get_evidence_type  — retrieve the evidence type of a memory
//   labile_memories    — list recently-accessed (labile) memories
//   reconsolidate      — update content of a labile memory

#include <chrono>

    DuckDBToolResult tool_set_evidence_type(const json& params) {
        uint64_t id = extract_id(params);
        if (id == 0) return DuckDBToolResult::error("id is required");

        std::string evidence_type = params.value("evidence_type", "");
        if (evidence_type.empty()) return DuckDBToolResult::error("evidence_type is required");

        static const std::unordered_set<std::string> valid_types = {
            "observation", "inference", "hearsay", "authoritative", "prediction"
        };
        if (!valid_types.count(evidence_type)) {
            return DuckDBToolResult::error(
                "Invalid evidence_type '" + evidence_type +
                "'. Must be one of: observation, inference, hearsay, authoritative, prediction");
        }

        std::string id_str = std::to_string(id);
        field_store_->add_triplet(id_str, "has_evidence_type", evidence_type);

        return DuckDBToolResult::ok("Evidence type set to '" + evidence_type + "'",
            {{"id", id_str}, {"evidence_type", evidence_type}});
    }

    DuckDBToolResult tool_get_evidence_type(const json& params) {
        uint64_t id = extract_id(params);
        if (id == 0) return DuckDBToolResult::error("id is required");

        std::string id_str = std::to_string(id);
        std::string raw = field_store_->query_subject(id_str);

        std::string evidence_type = "unknown";
        try {
            auto arr = json::parse(raw);
            if (arr.is_array()) {
                for (const auto& t : arr) {
                    if (t.value("predicate", "") == "has_evidence_type") {
                        evidence_type = t.value("object", "unknown");
                        break;
                    }
                }
            }
        } catch (...) {}

        return DuckDBToolResult::ok(evidence_type,
            {{"id", id_str}, {"evidence_type", evidence_type}});
    }

    DuckDBToolResult tool_labile_memories(const json& params) {
        std::string realm = params.value("realm", "");
        size_t limit = static_cast<size_t>(params.value("limit", 20));
        double window_hours = params.value("window_hours", 24.0);

        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        int64_t cutoff_ms = now_ms - static_cast<int64_t>(window_hours * 3600.0 * 1000.0);

        // List recent memories sorted by recency, then filter by last_accessed_ms
        std::string raw = field_store_->list_memories("", realm, "recency", limit * 5, 0);
        json results = json::array();
        try {
            auto arr = json::parse(raw);
            if (arr.is_array()) {
                for (const auto& m : arr) {
                    int64_t last_accessed = m.value("last_accessed_ms", int64_t(0));
                    if (last_accessed >= cutoff_ms) {
                        std::string content = m.value("content", "");
                        std::string id_str;
                        if (m.contains("id")) {
                            if (m["id"].is_string()) id_str = m["id"].get<std::string>();
                            else id_str = std::to_string(m["id"].get<int64_t>());
                        }
                        results.push_back({
                            {"id", id_str},
                            {"preview", content.substr(0, std::min(content.size(), size_t(100)))},
                            {"access_count", m.value("access_count", 0)},
                            {"last_accessed_ms", last_accessed}
                        });
                        if (results.size() >= limit) break;
                    }
                }
            }
        } catch (...) {}

        // Fallback: if no last_accessed_ms data, use temporal recall
        if (results.empty()) {
            auto hits = field_store_->recall_temporal(cutoff_ms, now_ms, limit, realm);
            for (const auto& h : hits) {
                results.push_back({
                    {"id", std::to_string(h.memory_id)},
                    {"preview", h.content.substr(0, 100)},
                    {"access_count", 0},
                    {"last_accessed_ms", h.ts_ms}
                });
            }
        }

        std::ostringstream ss;
        ss << results.size() << " labile memories (last " << window_hours << "h):\n";
        for (const auto& r : results) {
            ss << "#" << r.value("id", std::string("?")) << " " << r.value("preview", "") << "\n";
        }

        return DuckDBToolResult::ok(ss.str(),
            {{"memories", results}, {"count", results.size()}, {"window_hours", window_hours}});
    }

    DuckDBToolResult tool_reconsolidate(const json& params) {
        uint64_t id = extract_id(params);
        if (id == 0) return DuckDBToolResult::error("id is required");

        std::string content = params.value("content", "");
        if (content.empty()) return DuckDBToolResult::error("content is required");

        std::string reason = params.value("reason", "");

        std::string existing = field_store_->get_content(id);
        if (existing.empty()) return DuckDBToolResult::error("Memory not found: " + std::to_string(id));

        auto embedding = embed_text(content);
        int rc = field_store_->update_memory_content(id, content, embedding);
        if (rc != 0) return DuckDBToolResult::error("Failed to update memory content");

        field_store_->touch(id);

        std::string id_str = std::to_string(id);

        // Record reconsolidation event as triplet
        field_store_->add_triplet(id_str, "reconsolidated_at",
            std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()));

        return DuckDBToolResult::ok("Reconsolidated memory #" + id_str,
            {{"id", id_str}, {"reason", reason}, {"content_length", content.size()}});
    }
