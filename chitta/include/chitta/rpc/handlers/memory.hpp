// Included into DuckDBHandler class body — not a standalone header

    DuckDBToolResult tool_remember(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string content = params.value("content", "");
        if (content.empty()) {
            return DuckDBToolResult::error("Content is required");
        }

        std::string type_str = params.value("type", "episode");
        std::string realm = params.value("realm", "brahman");

        // Auto-convert to SSL format if not already
        // Skip for code-intel types which have their own format
        bool is_code_intel = (type_str == "symbol" || type_str == "projectessence" ||
                              type_str == "modulestate" || type_str == "patternstate");
        if (!is_code_intel && !is_ssl_format(content)) {
            // Infer domain from realm or type
            std::string domain = (realm != "brahman") ? realm : type_str;
            content = to_ssl_format(content, domain);
        }
        int visibility_int = params.value("visibility", 0);
        RealmVisibility visibility = static_cast<RealmVisibility>(std::clamp(visibility_int, 0, 2));

        std::vector<std::string> shared_realms;
        if (params.contains("shared_realms") && params["shared_realms"].is_array()) {
            for (const auto& r : params["shared_realms"]) {
                if (r.is_string()) shared_realms.push_back(r.get<std::string>());
            }
        }

        NodeType type = NodeType::Episode;
        if (type_str == "wisdom") type = NodeType::Wisdom;
        else if (type_str == "belief") type = NodeType::Belief;
        else if (type_str == "intention") type = NodeType::Intention;

        // Validate before calling mind_->remember
        if (!mind_->passes_quality_gate_public(content)) {
            return DuckDBToolResult::error("Failed: quality gate (length=" +
                std::to_string(content.size()) + ", min=10)");
        }
        if (!mind_->embedder_ready()) {
            return DuckDBToolResult::error("Failed: embedder not ready");
        }

        NodeId id;
        if (params.contains("tags") && params["tags"].is_array()) {
            std::vector<std::string> tags;
            for (const auto& t : params["tags"]) {
                if (t.is_string()) tags.push_back(t.get<std::string>());
            }
            id = mind_->remember(content, type, tags);
        } else {
            id = mind_->remember(content, type);
        }

        static const NodeId null_id{};
        if (id == null_id) {
            std::string err = mind_->store().last_error();
            return DuckDBToolResult::error("Failed: " + (err.empty() ? "store.remember returned -1" : err));
        }

        // Set realm, visibility, and confidence if non-default
        int64_t db_id = static_cast<int64_t>(id.low);
        if (realm != "brahman") {
            mind_->store().set_realm(db_id, realm);
        }
        if (visibility != RealmVisibility::Private) {
            mind_->store().set_visibility(db_id, visibility);
        }
        // Apply custom confidence if specified (default is 0.8)
        if (params.contains("confidence")) {
            float target = std::clamp(params.value("confidence", 0.8f), 0.0f, 1.0f);
            float delta = target - 0.8f;  // Default confidence is 0.8
            if (delta > 0.01f) {
                mind_->store().strengthen(db_id, delta);
            } else if (delta < -0.01f) {
                mind_->store().weaken(db_id, -delta);
            }
        }
        for (const auto& shared : shared_realms) {
            mind_->store().add_to_realm(db_id, shared);
        }

        std::string preview = content.substr(0, 50);
        if (content.size() > 50) preview += "...";

        json result = {{"id", id.to_string()}, {"realm", realm}};
        if (visibility != RealmVisibility::Private) {
            result["visibility"] = visibility_int;
        }
        if (!shared_realms.empty()) {
            result["shared_realms"] = shared_realms;
        }

        return DuckDBToolResult::ok("Remembered: " + preview, result);
    }

    DuckDBToolResult tool_recall(const json& params) {
        // Notify subconscious that we're handling a query (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string query = params.value("query", "");
        if (query.empty()) {
            return DuckDBToolResult::error("Query is required");
        }

        size_t limit = params.value("limit", 10);
        float min_confidence = params.value("min_confidence", 0.0f);
        std::string tag = params.value("tag", "");
        std::string realm = params.value("realm", "");
        bool include_global = params.value("include_global", true);
        bool separation_mode = params.value("separation_mode", false);

        // If tag specified, use tag-filtered recall for proper scoping
        std::vector<Recall> results;
        if (!tag.empty()) {
            // Get embedding for query
            if (!mind_->embedder_ready()) {
                return DuckDBToolResult::error("Embedder not ready");
            }
            auto embedding = mind_->embedder().embed_query(query).data;
            if (embedding.empty()) {
                return DuckDBToolResult::error("Failed to generate query embedding");
            }
            // Use tag-filtered recall
            auto store_results = mind_->store().recall_with_tag(embedding, tag, limit);
            for (const auto& r : store_results) {
                Recall rec;
                rec.id = NodeId(r.id);
                rec.text = r.content;
                rec.type = NodeType::Episode; // Default type
                rec.similarity = r.similarity;
                rec.relevance = r.similarity;
                rec.confidence = Confidence(r.confidence);
                results.push_back(rec);
            }
        } else {
            // Use normal recall
            size_t fetch_limit = !realm.empty() ? limit * 3 : limit;
            results = mind_->recall(query, fetch_limit, separation_mode);
        }

        std::ostringstream ss;
        json results_json = json::array();
        size_t count = 0;

        for (const auto& r : results) {
            int64_t mem_id = static_cast<int64_t>(r.id.low);

            // Tag filtering is handled by recall_with_tag, no post-hoc filter needed

            // Post-hoc realm filtering if realm specified
            if (!realm.empty()) {
                auto realms = mind_->store().get_realms(mem_id);
                bool in_realm = false;
                for (const auto& rm : realms) {
                    if (rm == realm) { in_realm = true; break; }
                }
                // Check if memory is global and include_global is true
                auto mem = mind_->store().get_memory(mem_id);
                if (mem && mem->visibility == RealmVisibility::Global && include_global) {
                    in_realm = true;
                }
                if (!in_realm) continue;
            }

            int pct = static_cast<int>(std::min(r.relevance, 1.0f) * 100);
            std::string type_name = node_type_name(r.type);

            json result_entry = {
                {"id", r.id.to_string()},
                {"relevance", r.relevance},
                {"similarity", r.similarity},
                {"type", type_name},
                {"text", r.text}
            };

            // Include realm info in results and apply confidence filter
            auto mem = mind_->store().get_memory(mem_id);
            if (mem) {
                // Skip if below min_confidence threshold
                if (mem->confidence < min_confidence) continue;

                result_entry["realm"] = mem->realm;
                result_entry["confidence"] = mem->confidence;
                if (mem->visibility != RealmVisibility::Private) {
                    result_entry["visibility"] = static_cast<int>(mem->visibility);
                }
            }

            results_json.push_back(result_entry);
            count++;
            if (count >= limit) break;
        }

        // Build output text with header and results
        ss << "Found " << count << " results";
        if (!realm.empty()) ss << " in realm '" << realm << "'";
        ss << ":\n";
        for (const auto& r : results_json) {
            int pct = static_cast<int>(r["relevance"].get<double>() * 100);
            std::string r_type = r["type"].is_string() ? r["type"].get<std::string>() : "?";
            std::string r_text = r["text"].is_string() ? r["text"].get<std::string>() : "";
            ss << "[" << pct << "%] [" << r_type << "] " << r_text.substr(0, 100);
            if (r_text.size() > 100) ss << "...";
            ss << "\n";
        }

        // SUS: log recall query
        try {
            std::string _sus_sid = get_session_id(params);
            if (!_sus_sid.empty() && _sus_sid != "default") {
                json _sus_ids = json::array();
                json _sus_scores = json::array();
                for (const auto& rj : results_json) {
                    _sus_ids.push_back(rj["id"]);
                    _sus_scores.push_back(rj.value("similarity", 0.0));
                }
                mind_->store().log_recall_query(
                    _sus_sid, 0, "recall", query,
                    _sus_ids.dump(), _sus_scores.dump());
            }
        } catch (...) {}

        return DuckDBToolResult::ok(ss.str(), {{"results", results_json}, {"realm", realm}});
    }

    DuckDBToolResult tool_recall_temporal(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        std::string query = params.value("query", "");
        std::string start_str = params.value("start", "");
        std::string end_str = params.value("end", "");
        size_t limit = params.value("limit", 20);
        std::string realm = params.value("realm", "");
        bool include_global = params.value("include_global", true);

        // Parse timestamps - default to last 7 days if not specified
        auto start_time = parse_timestamp_str(start_str);
        auto end_time = parse_timestamp_str(end_str);

        if (!start_time && !end_time) {
            // Default: last 7 days
            auto now = std::chrono::system_clock::now();
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            end_time = now_ms;
            start_time = now_ms - (7 * 24 * 60 * 60 * 1000LL);  // 7 days ago
        }

        // If end time is a date without time, set to end of day
        if (end_time && end_str.find('T') == std::string::npos && end_str.find(':') == std::string::npos) {
            *end_time += 86400000 - 1;  // Add 24 hours minus 1 ms
        }

        // Get query embedding if query provided
        std::vector<float> embedding;
        if (!query.empty() && mind_->embedder_ready()) {
            embedding = mind_->embedder().embed_query(query).data;
        }

        auto results = mind_->store().recall_temporal(
            embedding, start_time, end_time, limit, realm, include_global
        );

        std::ostringstream ss;
        json results_json = json::array();

        for (const auto& r : results) {
            // Format timestamp for display
            std::time_t created_sec = r.created_at / 1000;
            std::tm* tm = std::localtime(&created_sec);
            char time_buf[32];
            std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", tm);

            json result_entry = {
                {"id", std::to_string(r.id)},
                {"kind", r.kind},
                {"content", r.content},
                {"confidence", r.confidence},
                {"created_at", r.created_at},
                {"created_at_str", std::string(time_buf)},
                {"realm", r.realm}
            };

            if (!embedding.empty()) {
                result_entry["similarity"] = r.similarity;
            }

            results_json.push_back(result_entry);

            // Build text output
            int sim_pct = embedding.empty() ? 0 : static_cast<int>(r.similarity * 100);
            ss << "[" << time_buf << "]";
            if (!embedding.empty()) ss << " [" << sim_pct << "%]";
            ss << " [" << r.kind << "] ";
            std::string preview = r.content.substr(0, 100);
            size_t nl = preview.find('\n');
            if (nl != std::string::npos) preview = preview.substr(0, nl);
            ss << preview;
            if (r.content.size() > 100) ss << "...";
            ss << "\n";
        }

        std::ostringstream header;
        header << "Found " << results.size() << " memories";
        if (start_time || end_time) {
            header << " from ";
            if (start_time) header << start_str;
            else header << "beginning";
            header << " to ";
            if (end_time) header << end_str;
            else header << "now";
        }
        if (!realm.empty()) header << " in realm '" << realm << "'";
        header << ":\n";

        return DuckDBToolResult::ok(header.str() + ss.str(), {
            {"results", results_json},
            {"count", results.size()},
            {"start", start_str},
            {"end", end_str},
            {"realm", realm}
        });
    }

    DuckDBToolResult tool_explore_recall(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) {
            return DuckDBToolResult::error("Query is required");
        }

        size_t limit = params.value("limit", 15);
        auto results = mind_->recall(query, limit);

        // Return lightweight results: id, title (first line), score only
        json hints = json::array();
        std::ostringstream ss;
        ss << "Found " << results.size() << " hints:\n";

        for (const auto& r : results) {
            // Extract title (first line or first 80 chars)
            std::string title = r.text;
            size_t newline = title.find('\n');
            if (newline != std::string::npos) title = title.substr(0, newline);
            if (title.size() > 80) title = title.substr(0, 77) + "...";

            int pct = static_cast<int>(std::min(r.relevance, 1.0f) * 100);
            ss << "  [" << pct << "%] " << r.id.to_string() << ": " << title << "\n";

            hints.push_back({
                {"id", r.id.to_string()},
                {"title", title},
                {"score", r.relevance},
                {"type", node_type_name(r.type)}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"hints", hints}, {"count", results.size()}});
    }

    DuckDBToolResult tool_explore_peek(const json& params) {
        auto [db_id, id_str] = parse_id(params);
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        auto result = mind_->store().get_memory(db_id);
        if (!result) {
            return DuckDBToolResult::error("Memory not found: " + id_str);
        }

        // Return first 200 chars as summary
        std::string summary = result->content;
        if (summary.size() > 200) {
            summary = summary.substr(0, 197) + "...";
        }

        return DuckDBToolResult::ok(summary, {
            {"id", id_str},
            {"kind", result->kind},
            {"confidence", result->confidence},
            {"summary", summary},
            {"full_length", result->content.size()}
        });
    }

    DuckDBToolResult tool_explore_expand(const json& params) {
        auto [db_id, id_str] = parse_id(params);
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        auto result = mind_->store().get_memory(db_id);
        if (!result) {
            return DuckDBToolResult::error("Memory not found: " + id_str);
        }

        return DuckDBToolResult::ok(result->content, {
            {"id", id_str},
            {"kind", result->kind},
            {"confidence", result->confidence},
            {"content", result->content}
        });
    }

    DuckDBToolResult tool_explore_neighbors(const json& params) {
        std::string node = params.value("node", "");
        if (node.empty()) {
            return DuckDBToolResult::error("Node is required");
        }

        std::string direction = params.value("direction", "both");

        json neighbors = json::array();
        std::ostringstream ss;
        ss << "Neighbors of '" << node << "':\n";

        // Outgoing: node → predicate → ?
        if (direction == "both" || direction == "outgoing") {
            auto outgoing = mind_->query_subject(node);
            for (const auto& [pred, obj, weight] : outgoing) {
                ss << "  → " << pred << " → " << obj << "\n";
                neighbors.push_back({
                    {"node", obj},
                    {"predicate", pred},
                    {"direction", "outgoing"},
                    {"weight", weight}
                });
            }
        }

        // Incoming: ? → predicate → node
        if (direction == "both" || direction == "incoming") {
            auto incoming = mind_->query_object(node);
            for (const auto& [subj, pred, weight] : incoming) {
                ss << "  " << subj << " → " << pred << " →\n";
                neighbors.push_back({
                    {"node", subj},
                    {"predicate", pred},
                    {"direction", "incoming"},
                    {"weight", weight}
                });
            }
        }

        return DuckDBToolResult::ok(ss.str(), {{"neighbors", neighbors}, {"count", neighbors.size()}});
    }

    DuckDBToolResult tool_strengthen(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        auto [db_id, id_str] = parse_id(params);
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        NodeId id;
        id.low = static_cast<uint64_t>(db_id);
        float amount = params.value("amount", 0.1f);

        if (!mind_->strengthen(id, amount)) {
            return DuckDBToolResult::error("Node not found");
        }

        return DuckDBToolResult::ok("Strengthened node " + id_str);
    }

    DuckDBToolResult tool_weaken(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        auto [db_id, id_str] = parse_id(params);
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        NodeId id;
        id.low = static_cast<uint64_t>(db_id);
        float amount = params.value("amount", 0.1f);

        if (!mind_->weaken(id, amount)) {
            return DuckDBToolResult::error("Node not found");
        }

        return DuckDBToolResult::ok("Weakened node " + id_str);
    }

    DuckDBToolResult tool_forget(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        auto [db_id, id_str] = parse_id(params);
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        NodeId id;
        id.low = static_cast<uint64_t>(db_id);
        if (!mind_->remove(id)) {
            return DuckDBToolResult::error("Node not found");
        }

        return DuckDBToolResult::ok("Forgot node " + id_str);
    }

    DuckDBToolResult tool_batch_forget(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::vector<std::string> ids_to_delete;
        size_t deleted = 0;
        size_t not_found = 0;

        // Mode 1: Delete by explicit IDs
        if (params.contains("ids") && params["ids"].is_array()) {
            for (const auto& id_val : params["ids"]) {
                if (id_val.is_string()) {
                    ids_to_delete.push_back(id_val.get<std::string>());
                }
            }
        }

        // Mode 2: Delete by pattern (search and delete matching)
        if (params.contains("pattern") && params["pattern"].is_string()) {
            std::string pattern = params["pattern"].get<std::string>();
            auto results = mind_->recall(pattern, 100);  // Find up to 100 matching
            for (const auto& r : results) {
                // Check if content contains the pattern (case-insensitive would be better)
                if (r.text.find(pattern) != std::string::npos) {
                    ids_to_delete.push_back(r.id.to_string());
                }
            }
        }

        if (ids_to_delete.empty()) {
            return DuckDBToolResult::error("No IDs provided (use 'ids' array or 'pattern' string)");
        }

        // Delete each one
        for (const auto& id_str : ids_to_delete) {
            NodeId nid = NodeId::from_string(id_str);
            if (mind_->remove(nid)) {
                deleted++;
            } else {
                not_found++;
            }
        }

        std::ostringstream ss;
        ss << "Batch forget complete: " << deleted << " deleted";
        if (not_found > 0) {
            ss << ", " << not_found << " not found";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"deleted", deleted},
            {"not_found", not_found},
            {"total_requested", ids_to_delete.size()}
        });
    }

    DuckDBToolResult tool_observe(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string title = params.value("title", "");
        std::string content = params.value("content", "");
        std::string category = params.value("category", "wisdom");

        if (title.empty() || content.empty()) {
            return DuckDBToolResult::error("Title and content are required");
        }

        // Derive confidence from category (or use explicit override)
        float confidence = params.contains("confidence")
            ? params.value("confidence", 0.8f)
            : category_to_confidence(category);

        // Map category to NodeType
        NodeType type = NodeType::Wisdom;
        if (category == "episode") type = NodeType::Episode;
        else if (category == "belief") type = NodeType::Belief;
        // correction, preference, solution, decision, failure, wisdom, insight all use Wisdom

        std::string full_text = title + "\n" + content;
        NodeId id = mind_->remember(full_text, type, "brahman", RealmVisibility::Private, confidence);

        static const NodeId null_id{};
        if (id == null_id) {
            return DuckDBToolResult::error("Failed to observe");
        }

        // Process tags if provided
        std::string tags = params.value("tags", "");
        if (!tags.empty()) {
            int64_t db_id = static_cast<int64_t>(id.low);
            // Split comma-separated tags
            std::istringstream tag_stream(tags);
            std::string tag;
            while (std::getline(tag_stream, tag, ',')) {
                // Trim whitespace
                while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.front()))) tag.erase(tag.begin());
                while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.back()))) tag.pop_back();
                if (!tag.empty()) {
                    mind_->store().add_tag(db_id, tag);
                }
            }
        }

        return DuckDBToolResult::ok(
            "Observed: " + title.substr(0, 50),
            {{"id", id.to_string()}, {"category", category}, {"confidence", confidence}}
        );
    }

    DuckDBToolResult tool_full_resonate(const json& params) {
        // Notify subconscious that we're handling a query (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string query = params.value("query", "");
        if (query.empty()) {
            return DuckDBToolResult::error("Query is required");
        }

        size_t k = params.value("k", 10);
        std::string realm = params.value("realm", "");
        bool include_global = params.value("include_global", true);

        // Parse exclude_kinds parameter (accept both underscore and hyphen conventions)
        std::vector<std::string> exclude_kinds;
        const json* kinds_array = nullptr;
        if (params.contains("exclude_kinds") && params["exclude_kinds"].is_array()) {
            kinds_array = &params["exclude_kinds"];
        } else if (params.contains("exclude-kinds") && params["exclude-kinds"].is_array()) {
            kinds_array = &params["exclude-kinds"];
        } else if (params.contains("exclude-kinds") && params["exclude-kinds"].is_string()) {
            // CLI passes comma-separated string
            std::string kinds_str = params["exclude-kinds"].get<std::string>();
            std::istringstream iss(kinds_str);
            std::string kind;
            while (std::getline(iss, kind, ',')) {
                // Trim whitespace
                size_t start = kind.find_first_not_of(" \t");
                size_t end = kind.find_last_not_of(" \t");
                if (start != std::string::npos && end != std::string::npos) {
                    exclude_kinds.push_back(kind.substr(start, end - start + 1));
                }
            }
        }
        if (kinds_array) {
            for (const auto& kind : *kinds_array) {
                if (kind.is_string()) {
                    exclude_kinds.push_back(kind.get<std::string>());
                }
            }
        }

        // partnership_only flag: exclude all code intel kinds and code-tagged wisdom
        // Accept both underscore (JSON convention) and hyphen (CLI convention)
        bool partnership_only = params.value("partnership_only", false) ||
                                params.value("partnership-only", false);
        if (partnership_only) {
            exclude_kinds = {"symbol", "projectessence", "modulestate", "patternstate"};
        }

        // PARTNERSHIP FIRST: Query partnership memories (beliefs, preferences) separately
        // These are the memories that make Claude feel personalized
        std::vector<Recall> partnership_results;
        if (mind_->embedder_ready()) {
            Artha artha = mind_->embedder().transform_query(query);
            // Get global partnership memories directly
            auto globals = mind_->store().list_global_memories(k, "");
            for (const auto& mem : globals) {
                // Calculate similarity if we have embeddings
                auto recalls = mind_->store().recall(artha.nu.data, 50, "", true);
                for (const auto& r : recalls) {
                    if (r.id == mem.id) {
                        Recall recall;
                        recall.id.high = 0;
                        recall.id.low = static_cast<uint64_t>(r.id);
                        recall.text = r.content;
                        recall.similarity = r.similarity;
                        recall.relevance = r.similarity * r.confidence * 1.5f;  // 1.5x boost for partnership
                        // Inline string_to_node_type
                        if (r.kind == "belief") recall.type = NodeType::Belief;
                        else if (r.kind == "wisdom") recall.type = NodeType::Wisdom;
                        else if (r.kind == "episode") recall.type = NodeType::Episode;
                        else if (r.kind == "intention") recall.type = NodeType::Intention;
                        else recall.type = NodeType::Episode;
                        recall.confidence = Confidence(r.confidence);
                        partnership_results.push_back(recall);
                        break;
                    }
                }
            }
            // Sort by boosted relevance
            std::sort(partnership_results.begin(), partnership_results.end(),
                [](const Recall& a, const Recall& b) { return a.relevance > b.relevance; });
        }

        // Use full resonance architecture for general memories:
        // 1. Session Priming - context biases retrieval
        // 2. Spreading Activation - flows through triplet graph
        // 3. Attractor Dynamics - results pulled toward conceptual gravity wells
        // 4. Lateral Inhibition - similar patterns compete
        // 5. Hebbian Learning - co-activated nodes strengthen connections
        bool separation_mode = params.value("separation_mode", false);
        auto general_results = mind_->full_resonate(query, realm.empty() ? k : k * 2, exclude_kinds, separation_mode);

        // Merge: partnership memories first, then general
        std::vector<Recall> results;
        std::unordered_set<uint64_t> seen_ids;

        // Add partnership memories first (up to k/2)
        size_t partnership_limit = std::min(partnership_results.size(), k / 2);
        for (size_t i = 0; i < partnership_limit; ++i) {
            results.push_back(partnership_results[i]);
            seen_ids.insert(partnership_results[i].id.low);
        }

        // Add general results (avoiding duplicates)
        for (const auto& r : general_results) {
            if (seen_ids.find(r.id.low) == seen_ids.end()) {
                // Skip code-tagged wisdom when partnership_only is true
                if (partnership_only && r.text.rfind("[code]", 0) == 0) {
                    continue;
                }
                results.push_back(r);
                seen_ids.insert(r.id.low);
            }
        }

        std::ostringstream ss;
        json results_json = json::array();
        size_t count = 0;

        for (const auto& r : results) {
            // Post-hoc realm filtering if realm specified
            if (!realm.empty()) {
                auto realms = mind_->store().get_realms(static_cast<int64_t>(r.id.low));
                bool in_realm = false;
                for (const auto& rm : realms) {
                    if (rm == realm) { in_realm = true; break; }
                }
                // Check if memory is global and include_global is true
                auto mem = mind_->store().get_memory(static_cast<int64_t>(r.id.low));
                if (mem && mem->visibility == RealmVisibility::Global && include_global) {
                    in_realm = true;
                }
                if (!in_realm) continue;
            }

            if (count >= k) break;
            count++;

            int pct = static_cast<int>(std::min(r.relevance, 1.0f) * 100);
            std::string type_name = node_type_name(r.type);

            ss << "[" << pct << "%] [" << type_name << "] "
               << r.text.substr(0, 200);
            if (r.text.size() > 200) ss << "...";
            ss << "\n\n";

            // Check for provenance: wisdom derived_from episode
            std::string provenance;
            if (r.type == NodeType::Wisdom) {
                std::string wisdom_ref = "wisdom:" + r.id.to_string();
                auto provenance_triplets = mind_->store().query_subject(wisdom_ref);
                for (const auto& pt : provenance_triplets) {
                    if (pt.predicate == "derived_from" && pt.object.substr(0, 8) == "episode:") {
                        // Extract episode ID and fetch content
                        std::string episode_id_str = pt.object.substr(8);
                        try {
                            NodeId episode_id = NodeId::from_string(episode_id_str);
                            auto episode_mem = mind_->store().get_memory(static_cast<int64_t>(episode_id.low));
                            if (episode_mem) {
                                provenance = episode_mem->content.substr(0, 300);
                                if (episode_mem->content.size() > 300) provenance += "...";
                            }
                        } catch (...) {}
                        break;
                    }
                }
            }

            json result_entry = {
                {"id", r.id.to_string()},
                {"relevance", r.relevance},
                {"type", type_name},
                {"text", r.text}
            };
            if (!provenance.empty()) {
                result_entry["provenance"] = provenance;
                ss << "  ↳ Source: " << provenance.substr(0, 100) << "...\n\n";
            }
            results_json.push_back(result_entry);
        }

        // Graph expansion - find related concepts via triplets
        std::vector<std::string> terms = extract_terms(query);
        std::set<std::string> seen_triplets;
        json triplets_json = json::array();

        auto add_triplet = [&](const StringTriplet& t) {
            std::string key = t.subject + "→" + t.predicate + "→" + t.object;
            if (seen_triplets.find(key) == seen_triplets.end()) {
                seen_triplets.insert(key);
                triplets_json.push_back({
                    {"subject", t.subject},
                    {"predicate", t.predicate},
                    {"object", t.object}
                });
            }
        };

        for (const auto& term : terms) {
            for (const auto& t : mind_->store().query_subject(term)) add_triplet(t);
            for (const auto& t : mind_->store().query_object(term)) add_triplet(t);
        }

        // Get attractor info for diagnostics
        auto attractors = mind_->find_attractors();
        json attractors_json = json::array();
        for (const auto& attr : attractors) {
            attractors_json.push_back({
                {"entity", attr.entity},
                {"strength", attr.strength},
                {"connections", attr.connections}
            });
        }

        // Build output
        std::ostringstream header;
        header << "[Resonance]\n";
        if (!results_json.empty()) {
            header << "Found " << results_json.size() << " results";
            if (!attractors.empty()) {
                header << " (attractor: " << attractors[0].entity << ")";
            }
            header << ":\n\n";
        }

        std::string output = header.str() + ss.str();

        // Add graph relationships if found
        if (!triplets_json.empty()) {
            output += "[Related]\n";
            for (const auto& t : triplets_json) {
                output += t["subject"].get<std::string>() + " → " +
                         t["predicate"].get<std::string>() + " → " +
                         t["object"].get<std::string>() + "\n";
            }
        }

        // Push event to subconscious for pattern detection (always, even if no results)
        if (subconscious_) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            subconscious_->push_event({
                SubconsciousEventType::UserMessage,
                query,
                realm,
                now
            });
        }

        if (results_json.empty() && triplets_json.empty()) {
            return DuckDBToolResult::ok("No memories or relationships found.", {
                {"results", json::array()},
                {"triplets", json::array()},
                {"attractors", json::array()}
            });
        }

        return DuckDBToolResult::ok(output, {
            {"results", results_json},
            {"triplets", triplets_json},
            {"attractors", attractors_json}
        });
    }

    DuckDBToolResult tool_grow(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string type_str = params.value("type", "");
        std::string content = params.value("content", "");
        std::string title = params.value("title", "");
        std::string realm = params.value("realm", "brahman");
        int visibility = params.value("visibility", 0);

        if (type_str.empty() || content.empty()) {
            return DuckDBToolResult::error("Type and content are required");
        }

        NodeType type = NodeType::Wisdom;
        if (type_str == "belief") type = NodeType::Belief;
        else if (type_str == "failure" || type_str == "episode") type = NodeType::Episode;
        else if (type_str == "aspiration") type = NodeType::Aspiration;
        else if (type_str == "dream") type = NodeType::Dream;
        else if (type_str == "symbol") type = NodeType::Symbol;
        else if (type_str == "projectessence") type = NodeType::ProjectEssence;
        else if (type_str == "modulestate") type = NodeType::ModuleState;
        else if (type_str == "patternstate") type = NodeType::PatternState;

        std::string full_content = title.empty() ? content : title + "\n" + content;
        NodeId id = mind_->remember(full_content, type, realm, static_cast<RealmVisibility>(visibility));

        static const NodeId null_id{};
        if (id == null_id) {
            return DuckDBToolResult::error("Failed to grow (quality gate or embedding failed)");
        }

        return DuckDBToolResult::ok(
            "Grew " + type_str + ": " + (title.empty() ? content.substr(0, 50) : title),
            {{"id", id.to_string()}, {"type", type_str}, {"realm", realm}}
        );
    }

    DuckDBToolResult tool_get(const json& params) {
        auto [db_id, id_str] = parse_id(params);
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        auto result = mind_->store().get_memory(db_id);
        if (!result) {
            return DuckDBToolResult::error("Node not found: " + id_str);
        }

        std::ostringstream ss;
        ss << "Node " << id_str << ":\n";
        ss << "  Kind: " << result->kind << "\n";
        ss << "  Confidence: " << result->confidence << "\n";
        ss << "  Content:\n" << result->content << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"id", db_id},
            {"kind", result->kind},
            {"content", result->content},
            {"confidence", result->confidence}
        });
    }

    DuckDBToolResult tool_expand_memory(const json& params) {
        auto [db_id, id_str] = parse_id(params);
        if (id_str.empty()) {
            return DuckDBToolResult::error("Memory ID is required");
        }

        int depth = params.value("depth", 3);
        if (depth < 1) depth = 1;
        if (depth > 3) depth = 3;

        auto expanded = mind_->store().expand_memory(db_id, depth);
        if (!expanded) {
            return DuckDBToolResult::error("Memory not found: " + id_str);
        }

        std::ostringstream ss;
        ss << "=== Level 1: SSL Memory ===\n";
        ss << "ID: " << expanded->memory_id << "\n";
        ss << "Type: " << expanded->memory_type << "\n";
        ss << "Confidence: " << expanded->confidence << "\n";
        ss << "Content:\n" << expanded->memory_content << "\n";

        json result = {
            {"memory_id", expanded->memory_id},
            {"memory_type", expanded->memory_type},
            {"memory_content", expanded->memory_content},
            {"confidence", expanded->confidence}
        };

        if (depth >= 2 && expanded->episode_id > 0) {
            ss << "\n=== Level 2: Episode ===\n";
            ss << "Episode ID: " << expanded->episode_id << "\n";
            ss << "Session: " << expanded->session_id << "\n";
            ss << "Title: " << expanded->episode_title << "\n";
            ss << "Turn range: " << expanded->start_turn << " - " << expanded->end_turn << "\n";
            if (!expanded->episode_summary.empty()) {
                ss << "Summary: " << expanded->episode_summary << "\n";
            }

            result["episode_id"] = expanded->episode_id;
            result["session_id"] = expanded->session_id;
            result["episode_title"] = expanded->episode_title;
            result["start_turn"] = expanded->start_turn;
            result["end_turn"] = expanded->end_turn;
            result["episode_summary"] = expanded->episode_summary;
        }

        if (depth >= 3 && !expanded->turns.empty()) {
            ss << "\n=== Level 3: Full Turns (" << expanded->turns.size() << ") ===\n";
            json turns_json = json::array();
            for (const auto& turn : expanded->turns) {
                ss << "\n[" << turn.role << "] (turn " << turn.turn_index << ")\n";
                ss << turn.content << "\n";

                turns_json.push_back({
                    {"turn_index", turn.turn_index},
                    {"role", turn.role},
                    {"content", turn.content},
                    {"token_count", turn.token_count}
                });
            }
            result["turns"] = turns_json;
        }

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_update(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        auto [db_id, id_str] = parse_id(params);
        std::string content = params.value("content", "");

        if (id_str.empty() || content.empty()) {
            return DuckDBToolResult::error("ID and content are required");
        }

        bool ok = mind_->store().update_content(db_id, content);
        if (!ok) {
            return DuckDBToolResult::error("Failed to update node");
        }

        return DuckDBToolResult::ok("Updated node " + id_str);
    }

    DuckDBToolResult tool_query(const json& params) {
        std::string subject = params.value("subject", "");
        std::string predicate = params.value("predicate", "");
        std::string object = params.value("object", "");

        json results_json = json::array();
        std::ostringstream ss;

        if (!subject.empty()) {
            auto triplets = mind_->store().query_subject(subject);
            for (const auto& t : triplets) {
                if (!predicate.empty() && t.predicate != predicate) continue;
                if (!object.empty() && t.object != object) continue;
                ss << subject << " → " << t.predicate << " → " << t.object << "\n";
                results_json.push_back({
                    {"subject", subject},
                    {"predicate", t.predicate},
                    {"object", t.object},
                    {"weight", t.weight}
                });
            }
        }

        if (!object.empty() && subject.empty()) {
            auto triplets = mind_->store().query_object(object);
            for (const auto& t : triplets) {
                if (!predicate.empty() && t.predicate != predicate) continue;
                ss << t.subject << " → " << t.predicate << " → " << object << "\n";
                results_json.push_back({
                    {"subject", t.subject},
                    {"predicate", t.predicate},
                    {"object", object},
                    {"weight", t.weight}
                });
            }
        }

        if (!predicate.empty() && subject.empty() && object.empty()) {
            auto triplets = mind_->store().query_predicate(predicate);
            for (const auto& t : triplets) {
                ss << t.subject << " → " << predicate << " → " << t.object << "\n";
                results_json.push_back({
                    {"subject", t.subject},
                    {"predicate", predicate},
                    {"object", t.object},
                    {"weight", t.weight}
                });
            }
        }

        if (results_json.empty()) {
            return DuckDBToolResult::ok("No triplets found", {{"triplets", json::array()}});
        }

        return DuckDBToolResult::ok(ss.str(), {{"triplets", results_json}, {"count", results_json.size()}});
    }

    DuckDBToolResult tool_tag(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        auto [db_id, id_str] = parse_id(params);
        std::string add_tag = params.value("add", "");
        std::string remove_tag = params.value("remove", "");

        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        if (add_tag.empty() && remove_tag.empty()) {
            return DuckDBToolResult::error("Either 'add' or 'remove' tag is required");
        }

        std::string result_msg;
        if (!add_tag.empty()) {
            mind_->store().add_tag(db_id, add_tag);
            result_msg = "Added tag '" + add_tag + "'";
        }
        if (!remove_tag.empty()) {
            mind_->store().remove_tag(db_id, remove_tag);
            if (!result_msg.empty()) result_msg += ", ";
            result_msg += "Removed tag '" + remove_tag + "'";
        }

        return DuckDBToolResult::ok(result_msg + " on node " + id_str);
    }

    DuckDBToolResult tool_insight_promote(const json& params) {
        auto [id, id_str] = parse_id(params);
        std::string reason = params.value("reason", "");

        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        // Get the memory
        auto mem = mind_->store().get_memory(id);
        if (!mem) {
            return DuckDBToolResult::error("Memory not found: " + id_str);
        }

        // Update visibility to global
        bool ok = mind_->store().update_visibility(id, RealmVisibility::Global);
        if (!ok) {
            return DuckDBToolResult::error("Failed to promote memory");
        }

        // Add promotion triplet for tracking
        std::string slug = "memory_" + std::to_string(id);
        mind_->store().connect(slug, "promoted_to", "global");
        if (!reason.empty()) {
            mind_->store().connect(slug, "promotion_reason", reason);
        }

        std::ostringstream ss;
        ss << "Promoted memory #" << id << " to global visibility\n";
        ss << "Content: " << mem->content.substr(0, 100) << (mem->content.size() > 100 ? "..." : "") << "\n";
        if (!reason.empty()) {
            ss << "Reason: " << reason;
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"id", id},
            {"visibility", 2},
            {"reason", reason}
        });
    }

    DuckDBToolResult tool_insight_global(const json& params) {
        size_t limit = params.value("limit", 20);
        std::string kind = params.value("kind", "");

        auto memories = mind_->store().list_global_memories(limit, kind);

        if (memories.empty()) {
            return DuckDBToolResult::ok("No global memories found", {{"count", 0}});
        }

        std::ostringstream ss;
        ss << "Global Insights (" << memories.size() << "):\n";
        ss << "══════════════════════════════\n\n";

        json items = json::array();
        for (const auto& m : memories) {
            ss << "#" << m.id << " [" << m.kind << "] ";
            ss << m.content.substr(0, 80) << (m.content.size() > 80 ? "..." : "") << "\n";
            ss << "  Confidence: " << std::fixed << std::setprecision(2) << m.confidence;
            ss << " | Source: " << m.realm << "\n\n";

            items.push_back({
                {"id", m.id},
                {"kind", m.kind},
                {"content", m.content},
                {"confidence", m.confidence},
                {"realm", m.realm}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"count", memories.size()},
            {"memories", items}
        });
    }

    DuckDBToolResult tool_list_by_aspect(const json& params) {
        std::string aspect = params.value("aspect", "");
        if (aspect.empty()) {
            return DuckDBToolResult::error("aspect parameter required");
        }

        size_t limit = params.value("limit", 30);
        float min_confidence = params.value("min_confidence", 0.1f);

        auto memories = mind_->store().list_by_aspect(aspect, limit, min_confidence);

        if (memories.empty()) {
            // Check if aspect is valid
            if (ASPECT_KINDS.find(aspect) == ASPECT_KINDS.end()) {
                std::ostringstream ss;
                ss << "Unknown aspect: '" << aspect << "'. Valid aspects: ";
                bool first = true;
                for (const auto& [name, _] : ASPECT_KINDS) {
                    if (!first) ss << ", ";
                    ss << name;
                    first = false;
                }
                return DuckDBToolResult::error(ss.str());
            }
            return DuckDBToolResult::ok("No memories found for aspect: " + aspect, {{"count", 0}});
        }

        std::ostringstream ss;
        ss << "Memories for aspect '" << aspect << "' (" << memories.size() << "):\n";
        ss << "══════════════════════════════\n\n";

        json items = json::array();
        for (const auto& m : memories) {
            ss << "#" << m.id << " [" << m.kind << "] ";
            ss << m.content.substr(0, 80) << (m.content.size() > 80 ? "..." : "") << "\n";
            ss << "  Confidence: " << std::fixed << std::setprecision(2) << m.confidence;
            ss << " | Realm: " << m.realm << "\n\n";

            items.push_back({
                {"id", m.id},
                {"kind", m.kind},
                {"content", m.content},
                {"confidence", m.confidence},
                {"realm", m.realm}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"count", memories.size()},
            {"aspect", aspect},
            {"memories", items}
        });
    }

    DuckDBToolResult tool_list_aspects(const json& params) {
        (void)params;  // Unused

        std::ostringstream ss;
        ss << "Available Semantic Aspects:\n";
        ss << "══════════════════════════════\n\n";

        json aspects = json::array();
        for (const auto& [aspect, kinds] : ASPECT_KINDS) {
            ss << "  " << aspect << ": ";
            for (size_t i = 0; i < kinds.size(); ++i) {
                if (i > 0) ss << ", ";
                ss << kinds[i];
            }
            ss << "\n";

            aspects.push_back({
                {"name", aspect},
                {"kinds", kinds}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"aspects", aspects}});
    }

    DuckDBToolResult tool_list_memories_brief(const json& params) {
        size_t limit = params.value("limit", 200);
        std::string realm = params.value("realm", "");
        std::string kind = params.value("kind", "");
        std::optional<PriorityTier> tier;
        if (params.contains("priority_tier")) {
            tier = static_cast<PriorityTier>(params["priority_tier"].get<int>());
        }

        auto entries = mind_->store().list_memories_brief(limit, realm, kind, tier);

        std::ostringstream ss;
        ss << "Memory Index (" << entries.size() << " entries):\n";
        ss << "══════════════════════════════════════════════════════════════════\n";
        ss << "ID       | Tier | Kind       | Date       | Preview\n";
        ss << "---------|------|------------|------------|---------------------------\n";

        json items = json::array();
        for (const auto& e : entries) {
            // Format date
            auto ms = std::chrono::milliseconds(e.created_at);
            auto tp = std::chrono::system_clock::time_point(ms);
            auto tt = std::chrono::system_clock::to_time_t(tp);
            std::tm tm = *std::localtime(&tt);
            char date_buf[16];
            std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm);

            // Tier emoji
            const char* tier_str = "🟢";
            if (e.priority_tier == PriorityTier::Critical) tier_str = "🔴";
            else if (e.priority_tier == PriorityTier::Notable) tier_str = "🟡";

            // Truncate one-liner for display
            std::string preview = e.one_liner;
            if (preview.size() > 40) preview = preview.substr(0, 37) + "...";

            ss << std::setw(8) << e.id << " | " << tier_str << "   | "
               << std::setw(10) << e.kind.substr(0, 10) << " | "
               << date_buf << " | " << preview << "\n";

            items.push_back({
                {"id", e.id},
                {"kind", e.kind},
                {"priority_tier", static_cast<int>(e.priority_tier)},
                {"created_at", e.created_at},
                {"one_liner", e.one_liner}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"count", entries.size()},
            {"entries", items}
        });
    }

    DuckDBToolResult tool_set_priority_tier(const json& params) {
        if (!params.contains("memory_id") || !params.contains("tier")) {
            return DuckDBToolResult::error("memory_id and tier required");
        }

        int64_t memory_id = params["memory_id"].get<int64_t>();
        int tier_val = params["tier"].get<int>();

        if (tier_val < 0 || tier_val > 2) {
            return DuckDBToolResult::error("tier must be 0 (background), 1 (notable), or 2 (critical)");
        }

        PriorityTier tier = static_cast<PriorityTier>(tier_val);
        bool success = mind_->store().set_priority_tier(memory_id, tier);

        if (!success) {
            return DuckDBToolResult::error("Failed to set priority tier");
        }

        const char* tier_emoji = tier == PriorityTier::Critical ? "🔴" :
                                 tier == PriorityTier::Notable ? "🟡" : "🟢";
        const char* tier_name = tier == PriorityTier::Critical ? "critical" :
                                tier == PriorityTier::Notable ? "notable" : "background";

        std::ostringstream ss;
        ss << "Set memory #" << memory_id << " to " << tier_emoji << " " << tier_name << " tier";

        return DuckDBToolResult::ok(ss.str(), {
            {"memory_id", memory_id},
            {"tier", tier_val},
            {"tier_name", tier_name}
        });
    }

    DuckDBToolResult tool_recall_by_priority(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        std::string query = params.value("query", "");
        size_t budget_tokens = params.value("budget_tokens", 4000);
        std::string realm = params.value("realm", "");
        bool include_global = params.value("include_global", true);

        // Get embedding if query provided
        std::vector<float> embedding;
        if (!query.empty() && mind_->embedder_ready()) {
            embedding = mind_->embedder().embed_query(query).data;
        }

        auto results = mind_->store().recall_by_priority(embedding, budget_tokens, realm, include_global);

        std::ostringstream ss;
        ss << "Budget-Aware Recall (" << results.size() << " memories, ~" << budget_tokens << " token budget):\n";
        ss << "══════════════════════════════════════════════════════════════════\n\n";

        // Count by tier
        int critical_count = 0, notable_count = 0, background_count = 0;
        size_t total_chars = 0;
        json items = json::array();

        for (const auto& m : results) {
            if (m.priority_tier == PriorityTier::Critical) critical_count++;
            else if (m.priority_tier == PriorityTier::Notable) notable_count++;
            else background_count++;
            total_chars += m.content.size();

            const char* tier_emoji = m.priority_tier == PriorityTier::Critical ? "🔴" :
                                     m.priority_tier == PriorityTier::Notable ? "🟡" : "🟢";

            ss << tier_emoji << " #" << m.id << " [" << m.kind << "]\n";
            ss << "   " << m.content.substr(0, 100) << (m.content.size() > 100 ? "..." : "") << "\n\n";

            items.push_back({
                {"id", m.id},
                {"kind", m.kind},
                {"content", m.content},
                {"confidence", m.confidence},
                {"priority_tier", static_cast<int>(m.priority_tier)}
            });
        }

        ss << "───────────────────────────────────────────\n";
        ss << "Tiers: 🔴 " << critical_count << " | 🟡 " << notable_count << " | 🟢 " << background_count << "\n";
        ss << "Est. tokens: ~" << (total_chars / 4) << " / " << budget_tokens << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"count", results.size()},
            {"critical_count", critical_count},
            {"notable_count", notable_count},
            {"background_count", background_count},
            {"estimated_tokens", total_chars / 4},
            {"budget_tokens", budget_tokens},
            {"memories", items}
        });
    }

    DuckDBToolResult tool_set_memory_type(const json& params) {
        if (!params.contains("memory_id") || !params.contains("type")) {
            return DuckDBToolResult::error("memory_id and type required");
        }

        int64_t memory_id = params["memory_id"].is_number() ? params["memory_id"].get<int64_t>() : std::stoll(params["memory_id"].get<std::string>());
        if (!params["type"].is_string()) {
            return DuckDBToolResult::error("type must be a string");
        }
        std::string type = params["type"].get<std::string>();

        // Validate type
        bool valid = false;
        for (const auto& t : VALID_MEMORY_TYPES) {
            if (type == t) { valid = true; break; }
        }

        if (!valid) {
            std::ostringstream err;
            err << "Invalid type '" << type << "'. Valid types: ";
            for (size_t i = 0; i < VALID_MEMORY_TYPES.size(); ++i) {
                if (i > 0) err << ", ";
                err << VALID_MEMORY_TYPES[i];
            }
            return DuckDBToolResult::error(err.str());
        }

        // Check memory exists
        auto mem = mind_->store().get_memory(memory_id);
        if (!mem) {
            return DuckDBToolResult::error("Memory not found");
        }

        // Update kind via store
        bool success = mind_->store().update_kind(memory_id, type);
        if (!success) {
            return DuckDBToolResult::error("Failed to update memory type");
        }

        return DuckDBToolResult::ok(
            "Set memory #" + std::to_string(memory_id) + " type to: " + type,
            {{"memory_id", memory_id}, {"type", type}, {"previous_type", mem->kind}}
        );
    }

    DuckDBToolResult tool_memory_type_stats(const json& params) {
        std::string realm = params.value("realm", "");

        std::ostringstream where;
        if (!realm.empty()) {
            std::string escaped;
            for (char c : realm) {
                if (c == '\'') escaped += "''";
                else escaped += c;
            }
            where << " WHERE (realm = '" << escaped << "' OR visibility = 2)";
        }

        // Query for kind counts
        std::ostringstream sql;
        sql << "SELECT COALESCE(kind, 'unknown') as kind, COUNT(*) as count "
            << "FROM memory" << where.str()
            << " GROUP BY kind ORDER BY count DESC";

        auto result = mind_->store().raw_query(sql.str());

        std::ostringstream ss;
        ss << "Memory Type Statistics:\n";
        ss << "══════════════════════════════\n\n";

        json by_kind = json::object();
        if (result && !result->HasError()) {
            while (auto chunk = result->Fetch()) {
                if (!chunk || chunk->size() == 0) break;
                for (size_t i = 0; i < chunk->size(); ++i) {
                    std::string kind = chunk->GetValue(0, i).GetValue<std::string>();
                    int64_t count = chunk->GetValue(1, i).GetValue<int64_t>();
                    ss << "  " << std::setw(15) << kind << ": " << count << "\n";
                    by_kind[kind] = count;
                }
            }
        }

        // Query for priority tier counts
        std::ostringstream tier_sql;
        tier_sql << "SELECT COALESCE(priority_tier, 0) as tier, COUNT(*) as count "
                 << "FROM memory" << where.str()
                 << " GROUP BY priority_tier ORDER BY tier DESC";

        auto tier_result = mind_->store().raw_query(tier_sql.str());

        ss << "\nPriority Tiers:\n";
        json by_tier = json::object();
        int critical = 0, notable = 0, background = 0;
        if (tier_result && !tier_result->HasError()) {
            while (auto chunk = tier_result->Fetch()) {
                if (!chunk || chunk->size() == 0) break;
                for (size_t i = 0; i < chunk->size(); ++i) {
                    int tier = chunk->GetValue(0, i).GetValue<int32_t>();
                    int64_t count = chunk->GetValue(1, i).GetValue<int64_t>();
                    if (tier == 2) { critical = count; ss << "  🔴 Critical: " << count << "\n"; }
                    else if (tier == 1) { notable = count; ss << "  🟡 Notable: " << count << "\n"; }
                    else { background = count; ss << "  🟢 Background: " << count << "\n"; }
                }
            }
        }

        by_tier["critical"] = critical;
        by_tier["notable"] = notable;
        by_tier["background"] = background;

        return DuckDBToolResult::ok(ss.str(), {
            {"by_kind", by_kind},
            {"by_tier", by_tier}
        });
    }

    DuckDBToolResult tool_smart_recall(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        std::string query = params.value("query", "");
        if (query.empty()) {
            return DuckDBToolResult::error("query parameter required");
        }

        size_t limit = params.value("limit", 20);
        size_t expand_top = params.value("expand_top", 2);
        std::string realm = params.value("realm", "");
        bool include_global = params.value("include_global", true);
        bool separation_mode = params.value("separation_mode", false);

        // 1. Classify the query intent
        QueryIntentClassifier classifier;
        QueryIntent intent = classifier.classify(query);

        std::vector<MemoryResult> results;
        std::string route_taken;

        // 2. Route based on intent type
        switch (intent.type) {
            case QueryIntentType::Aspect: {
                // Use list_by_aspect with the detected aspect (with realm filtering)
                if (intent.aspect) {
                    results = mind_->store().list_by_aspect(*intent.aspect, limit, 0.1f, realm, include_global);
                    route_taken = "aspect:" + *intent.aspect;
                }
                break;
            }

            case QueryIntentType::Temporal: {
                // Enhanced temporal routing based on subtype
                json temporal_candidates = json::array();

                // Query temporal triplets for extracted entities
                for (const auto& entity : intent.entities) {
                    std::string entity_lower;
                    entity_lower.reserve(entity.size());
                    for (char c : entity) {
                        entity_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }

                    auto triplets = mind_->store().query_triplets_temporal(
                        entity_lower, "", "", 0, limit
                    );

                    for (const auto& t : triplets) {
                        if (t.valid_from_ms > 0) {
                            temporal_candidates.push_back({
                                {"entity", t.subject},
                                {"predicate", t.predicate},
                                {"object", t.object},
                                {"date", TemporalResolver::format_iso_date(t.valid_from_ms)},
                                {"timestamp_ms", t.valid_from_ms}
                            });
                        }
                    }
                }

                // If we have temporal candidates, return them with context
                if (!temporal_candidates.empty()) {
                    // Also do semantic recall for supporting context
                    if (mind_->embedder_ready()) {
                        auto embedding = mind_->embedder().embed_query(query).data;
                        results = mind_->store().recall(embedding, limit, realm, include_global);
                    }

                    std::ostringstream ss;
                    ss << "Temporal Query Results\n";
                    ss << "══════════════════════════════\n";
                    ss << "Subtype: " << temporal_subtype_to_string(intent.temporal_subtype) << "\n\n";
                    ss << "Temporal Facts (" << temporal_candidates.size() << "):\n";
                    for (const auto& tc : temporal_candidates) {
                        ss << "  - " << tc["entity"].get<std::string>()
                           << " " << tc["predicate"].get<std::string>()
                           << " " << tc["object"].get<std::string>()
                           << " @" << tc["date"].get<std::string>() << "\n";
                    }

                    json results_json = json::array();
                    for (const auto& r : results) {
                        results_json.push_back({
                            {"id", std::to_string(r.id)},
                            {"content", r.content.substr(0, 200)},
                            {"similarity", r.similarity}
                        });
                    }

                    return DuckDBToolResult::ok(ss.str(), {
                        {"intent", {
                            {"type", query_intent_type_to_string(intent.type)},
                            {"temporal_subtype", temporal_subtype_to_string(intent.temporal_subtype)},
                            {"confidence", intent.confidence},
                            {"entities", intent.entities}
                        }},
                        {"route", "temporal"},
                        {"temporal_candidates", temporal_candidates},
                        {"results", results_json},
                        {"count", results.size()}
                    });
                }

                // Fall back to temporal recall with time range if no triplet matches
                if (intent.time_range && intent.time_range->valid()) {
                    auto& tr = *intent.time_range;
                    std::optional<int64_t> start_ms, end_ms;
                    if (tr.start) {
                        start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            tr.start->time_since_epoch()).count();
                    }
                    if (tr.end) {
                        end_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            tr.end->time_since_epoch()).count();
                    }
                    // Get query embedding if there's still an entity component
                    std::vector<float> embedding;
                    if (intent.entity && !intent.entity->empty() && mind_->embedder_ready()) {
                        embedding = mind_->embedder().embed_query(*intent.entity).data;
                    }
                    results = mind_->store().recall_temporal(embedding, start_ms, end_ms, limit, realm, include_global);
                    route_taken = "temporal";
                }
                break;
            }

            case QueryIntentType::Entity: {
                // Standard semantic recall on the entity
                if (intent.entity && !intent.entity->empty() && mind_->embedder_ready()) {
                    auto embedding = mind_->embedder().embed_query(*intent.entity).data;
                    results = mind_->store().recall(embedding, limit, realm, include_global);
                    route_taken = "entity:" + *intent.entity;
                }
                break;
            }

            case QueryIntentType::Relationship: {
                // Query the triplet graph for connections between entities
                if (intent.subject && intent.object) {
                    // Get triplets from subject and object
                    auto subject_triplets = mind_->store().query_subject(*intent.subject);
                    auto object_triplets = mind_->store().query_object(*intent.object);

                    // Build response from triplet data (no direct memory results)
                    std::ostringstream ss;
                    ss << "Relationships involving '" << *intent.subject << "' and '" << *intent.object << "':\n";
                    ss << "══════════════════════════════\n\n";

                    json triplets_json = json::array();
                    size_t count = 0;

                    // Subject triplets
                    for (const auto& t : subject_triplets) {
                        if (count >= limit) break;
                        ss << "  " << t.subject << " --[" << t.predicate << "]--> " << t.object << "\n";
                        triplets_json.push_back({
                            {"subject", t.subject},
                            {"predicate", t.predicate},
                            {"object", t.object},
                            {"weight", t.weight}
                        });
                        count++;
                    }

                    // Object triplets (if room)
                    for (const auto& t : object_triplets) {
                        if (count >= limit) break;
                        ss << "  " << t.subject << " --[" << t.predicate << "]--> " << t.object << "\n";
                        triplets_json.push_back({
                            {"subject", t.subject},
                            {"predicate", t.predicate},
                            {"object", t.object},
                            {"weight", t.weight}
                        });
                        count++;
                    }

                    return DuckDBToolResult::ok(ss.str(), {
                        {"intent", {
                            {"type", query_intent_type_to_string(intent.type)},
                            {"confidence", intent.confidence},
                            {"subject", intent.subject.value_or("")},
                            {"object", intent.object.value_or("")}
                        }},
                        {"route", "relationship"},
                        {"triplets", triplets_json},
                        {"count", count}
                    });
                }
                break;
            }

            case QueryIntentType::Code: {
                // Code queries should go through find_symbol/search_symbols
                return DuckDBToolResult::ok(
                    "Code queries are best handled by dedicated tools:\n"
                    "  - find_symbol: search by name/kind\n"
                    "  - search_symbols: semantic search\n"
                    "  - symbol_callers/symbol_callees: call graph navigation\n"
                    "  - read_symbol/read_function: get source code\n",
                    {
                        {"intent", {
                            {"type", "code"},
                            {"confidence", intent.confidence},
                            {"entity", intent.entity.value_or("")}
                        }},
                        {"route", "code"},
                        {"suggestion", "Use find_symbol or search_symbols for code queries"}
                    }
                );
            }

            case QueryIntentType::Meta: {
                // Return memory stats
                auto health = mind_->store().health();
                auto hygiene = mind_->store().hygiene_stats();

                std::ostringstream ss;
                ss << "Memory Health Stats:\n";
                ss << "══════════════════════════════\n\n";
                ss << "  Total memories: " << health.total_memories << "\n";
                ss << "  Total symbols: " << health.total_symbols << "\n";
                ss << "  Total triplets: " << health.total_triplets << "\n";
                ss << "  Average confidence: " << std::fixed << std::setprecision(2) << health.avg_confidence << "\n";
                ss << "\n";
                ss << "  High confidence (>0.7): " << hygiene.high_confidence << "\n";
                ss << "  Medium confidence: " << hygiene.medium_confidence << "\n";
                ss << "  Low confidence (<0.3): " << hygiene.low_confidence << "\n";
                ss << "  Old unaccessed (30+ days): " << hygiene.old_unaccessed << "\n";
                ss << "  Consolidation candidates: " << hygiene.consolidation_candidates << "\n";

                return DuckDBToolResult::ok(ss.str(), {
                    {"intent", {
                        {"type", "meta"},
                        {"confidence", intent.confidence}
                    }},
                    {"route", "meta"},
                    {"health", {
                        {"total_memories", health.total_memories},
                        {"total_symbols", health.total_symbols},
                        {"total_triplets", health.total_triplets},
                        {"avg_confidence", health.avg_confidence}
                    }},
                    {"hygiene", {
                        {"high_confidence", hygiene.high_confidence},
                        {"medium_confidence", hygiene.medium_confidence},
                        {"low_confidence", hygiene.low_confidence},
                        {"old_unaccessed", hygiene.old_unaccessed},
                        {"consolidation_candidates", hygiene.consolidation_candidates}
                    }}
                });
            }

            case QueryIntentType::Exploratory:
            default: {
                // Fall back to standard semantic recall
                if (mind_->embedder_ready()) {
                    auto embedding = mind_->embedder().embed_query(query).data;
                    results = mind_->store().recall(embedding, limit, realm, include_global);
                    route_taken = "exploratory";
                }
                break;
            }
        }

        // Pattern separation: MMR reranking for maximally diverse results
        if (separation_mode && results.size() > 1) {
            // Load SDRs and apply MMR on MemoryResult vector
            std::vector<SparseVector> sdrs;
            sdrs.reserve(results.size());
            for (const auto& r : results) {
                std::string sdr_str = mind_->store().get_sdr(r.id);
                sdrs.push_back(SparseVector::deserialize(sdr_str));
            }

            std::vector<MemoryResult> selected;
            std::vector<bool> chosen(results.size(), false);
            selected.reserve(std::min(limit, results.size()));
            constexpr float lambda = 0.3f;

            while (selected.size() < limit && selected.size() < results.size()) {
                float best_score = -1e9f;
                size_t best_idx = 0;
                bool found = false;

                for (size_t j = 0; j < results.size(); j++) {
                    if (chosen[j]) continue;
                    float relevance = results[j].similarity;
                    float max_sim = 0.0f;
                    for (const auto& s : selected) {
                        // Find original index of selected item
                        for (size_t si = 0; si < results.size(); si++) {
                            if (results[si].id == s.id) {
                                max_sim = std::max(max_sim, sdrs[j].iou(sdrs[si]));
                                break;
                            }
                        }
                    }
                    float score = lambda * relevance - (1.0f - lambda) * max_sim;
                    if (!found || score > best_score) {
                        best_score = score;
                        best_idx = j;
                        found = true;
                    }
                }
                if (!found) break;
                chosen[best_idx] = true;
                selected.push_back(std::move(results[best_idx]));
            }
            results = std::move(selected);
        }

        // Format results (for non-special cases)
        if (results.empty() && route_taken.empty()) {
            return DuckDBToolResult::error("Could not process query - embedder may not be ready");
        }

        std::ostringstream ss;
        ss << "Smart Recall Results (" << results.size() << " found)\n";
        ss << "Route: " << route_taken << " | Intent: " << query_intent_type_to_string(intent.type);
        ss << " (" << static_cast<int>(intent.confidence * 100) << "% confidence)\n";
        ss << "══════════════════════════════\n\n";

        json results_json = json::array();
        json expanded_json = json::array();
        size_t expanded_count = 0;

        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            // Format timestamp for display
            std::time_t created_sec = r.created_at / 1000;
            std::tm* tm = std::localtime(&created_sec);
            char time_buf[32];
            std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", tm);

            int sim_pct = static_cast<int>(r.similarity * 100);
            ss << "#" << r.id << " [" << r.kind << "]";
            if (r.similarity > 0) ss << " [" << sim_pct << "%]";
            ss << " " << time_buf << "\n";
            ss << "  " << r.content.substr(0, 100) << (r.content.size() > 100 ? "..." : "") << "\n";

            json mem_json = {
                {"id", std::to_string(r.id)},
                {"kind", r.kind},
                {"content", r.content},
                {"confidence", r.confidence},
                {"similarity", r.similarity},
                {"created_at", r.created_at},
                {"created_at_str", std::string(time_buf)},
                {"realm", r.realm}
            };

            // Hierarchical expansion for top N results
            if (expand_top > 0 && expanded_count < expand_top) {
                auto expanded = mind_->store().expand_memory(r.id, 3);
                if (expanded && expanded->episode_id > 0) {
                    json exp;
                    exp["memory_id"] = r.id;
                    exp["episode_id"] = expanded->episode_id;
                    exp["episode_title"] = expanded->episode_title;
                    exp["session_id"] = expanded->session_id;
                    exp["start_turn"] = expanded->start_turn;
                    exp["end_turn"] = expanded->end_turn;

                    ss << "  └─ Episode: " << expanded->episode_title
                       << " (turns " << expanded->start_turn << "-" << expanded->end_turn << ")\n";

                    if (!expanded->turns.empty()) {
                        json turns_arr = json::array();
                        size_t total_chars = 0;
                        const size_t max_total_chars = 20000;  // Limit expanded content
                        const size_t max_turn_chars = 2000;    // Limit per turn

                        for (const auto& turn : expanded->turns) {
                            if (total_chars >= max_total_chars) {
                                turns_arr.push_back({
                                    {"role", "system"},
                                    {"content", "[... truncated, " + std::to_string(expanded->turns.size() - turns_arr.size()) + " more turns]"},
                                    {"turn_index", -1}
                                });
                                break;
                            }

                            std::string content = turn.content;
                            if (content.size() > max_turn_chars) {
                                content = content.substr(0, max_turn_chars) + "... [truncated]";
                            }
                            total_chars += content.size();

                            turns_arr.push_back({
                                {"role", turn.role},
                                {"content", content},
                                {"turn_index", turn.turn_index}
                            });
                        }
                        exp["turns"] = turns_arr;
                        exp["turn_count"] = expanded->turns.size();
                        exp["truncated"] = total_chars >= max_total_chars;
                    }

                    mem_json["expanded"] = exp;
                    expanded_json.push_back(exp);
                    expanded_count++;
                }
            }

            results_json.push_back(mem_json);
            ss << "\n";
        }

        // SUS: log recall query
        try {
            std::string _sus_sid = get_session_id(params);
            if (!_sus_sid.empty() && _sus_sid != "default") {
                json _sus_ids = json::array();
                json _sus_scores = json::array();
                for (const auto& rj : results_json) {
                    _sus_ids.push_back(rj["id"]);
                    _sus_scores.push_back(rj.value("similarity", 0.0));
                }
                mind_->store().log_recall_query(
                    _sus_sid, 0, "smart_recall", query,
                    _sus_ids.dump(), _sus_scores.dump());
            }
        } catch (...) {}

        return DuckDBToolResult::ok(ss.str(), {
            {"intent", {
                {"type", query_intent_type_to_string(intent.type)},
                {"temporal_subtype", temporal_subtype_to_string(intent.temporal_subtype)},
                {"confidence", intent.confidence},
                {"aspect", intent.aspect.value_or("")},
                {"entity", intent.entity.value_or("")},
                {"entities", intent.entities}
            }},
            {"route", route_taken},
            {"results", results_json},
            {"expanded", expanded_json},
            {"expanded_count", expanded_count},
            {"count", results.size()}
        });
    }

    DuckDBToolResult tool_expand_query(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) return DuckDBToolResult::error("query required");
        auto eq = expand_query(query);
        return DuckDBToolResult::ok("Query expanded into lex/vec/hyde variants", {
            {"lex", eq.lex}, {"vec", eq.vec}, {"hyde", eq.hyde}
        });
    }

    DuckDBToolResult tool_hybrid_recall(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) {
            return DuckDBToolResult::error("Query is required");
        }

        size_t limit = params.value("limit", 10);
        std::string tag = params.value("tag", "");
        std::string realm = params.value("realm", "");

        // Feature 2: BM25 short-circuit for strong lexical matches
        std::vector<MemoryResult> sc_results;
        bool short_circuited = try_bm25_short_circuit(query, limit, realm, true, sc_results);
        if (short_circuited && !sc_results.empty()) {
            json result;
            result["short_circuit"] = true;
            result["memories"] = json::array();
            size_t count = 0;
            for (const auto& r : sc_results) {
                json mem;
                mem["id"] = r.id;
                mem["kind"] = r.kind;
                mem["content"] = r.content.substr(0, 300);
                mem["similarity"] = r.similarity;
                mem["confidence"] = r.confidence;
                mem["realm"] = r.realm;
                mem["created_at"] = r.created_at;
                result["memories"].push_back(mem);
                count++;
                if (count >= limit) break;
            }
            result["count"] = count;

            // SUS: log recall query
            try {
                std::string _sus_sid = get_session_id(params);
                if (!_sus_sid.empty() && _sus_sid != "default") {
                    json _sus_ids = json::array();
                    json _sus_scores = json::array();
                    for (const auto& mem : result["memories"]) {
                        _sus_ids.push_back(mem["id"]);
                        _sus_scores.push_back(mem.value("similarity", 0.0));
                    }
                    mind_->store().log_recall_query(
                        _sus_sid, 0, "hybrid_recall", query,
                        _sus_ids.dump(), _sus_scores.dump());
                }
            } catch (...) {}

            return DuckDBToolResult::ok(
                "Found " + std::to_string(count) + " memories (BM25 short-circuit)", result);
        }

        // Feature 1: Typed query expansion
        auto eq = expand_query(query);

        // Get embedding for vector-optimized query
        if (!mind_->embedder_ready()) {
            return DuckDBToolResult::error("Embedder not ready");
        }
        auto embedding = mind_->embedder().embed_query(eq.vec).data;
        if (embedding.empty()) {
            return DuckDBToolResult::error("Failed to generate query embedding");
        }

        std::vector<chitta::MemoryResult> results;

        if (!tag.empty()) {
            // Use tag-filtered recall for proper scoping
            results = mind_->store().recall_with_tag(embedding, tag, limit);
        } else {
            // Build config from params
            DuckDBStore::HybridRecallConfig config;
            if (params.contains("vector_weight")) config.vector_weight = params["vector_weight"].get<float>();
            if (params.contains("bm25_weight")) config.bm25_weight = params["bm25_weight"].get<float>();
            if (params.contains("graph_weight")) config.graph_weight = params["graph_weight"].get<float>();
            if (params.contains("recency_weight")) config.recency_weight = params["recency_weight"].get<float>();

            // Pass lex and hyde variants to store for improved BM25 matching
            results = mind_->store().hybrid_recall(embedding, query, limit, realm, true, config, eq.lex, eq.hyde);
        }

        json result;
        result["memories"] = json::array();
        size_t count = 0;

        for (const auto& r : results) {
            json mem;
            mem["id"] = r.id;
            mem["kind"] = r.kind;
            mem["content"] = r.content.substr(0, 300);
            mem["similarity"] = r.similarity;
            mem["confidence"] = r.confidence;
            mem["realm"] = r.realm;
            mem["created_at"] = r.created_at;
            result["memories"].push_back(mem);
            count++;
            if (count >= limit) break;
        }
        result["count"] = count;

        // Only include config when not using tag-filtered recall
        if (tag.empty()) {
            json cfg;
            cfg["note"] = "Config only applies to hybrid mode (no tag filter)";
            result["config"] = cfg;
        }

        std::ostringstream msg;
        msg << "Found " << results.size() << " memory/memories via hybrid retrieval";

        // SUS: log recall query
        try {
            std::string _sus_sid = get_session_id(params);
            if (!_sus_sid.empty() && _sus_sid != "default") {
                json _sus_ids = json::array();
                json _sus_scores = json::array();
                for (const auto& mem : result["memories"]) {
                    _sus_ids.push_back(mem["id"]);
                    _sus_scores.push_back(mem.value("similarity", 0.0));
                }
                mind_->store().log_recall_query(
                    _sus_sid, 0, "hybrid_recall", query,
                    _sus_ids.dump(), _sus_scores.dump());
            }
        } catch (...) {}

        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_memory_history(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        int32_t limit = params.value("limit", 20);
        auto history = mind_->store().get_history(id, limit);

        if (history.empty()) {
            return DuckDBToolResult::ok("No version history for memory #" + std::to_string(id),
                                       {{"memory_id", id}, {"count", 0}, {"history", json::array()}});
        }

        std::ostringstream ss;
        ss << "Version history for memory #" << id << " (" << history.size() << " versions)\n";
        ss << std::string(60, '-') << "\n";

        json history_json = json::array();
        for (const auto& entry : history) {
            ss << "v" << entry.version << " [" << entry.operation << "] ";
            if (!entry.commit_message.empty()) {
                ss << entry.commit_message.substr(0, 50);
            }
            ss << "\n";

            history_json.push_back({
                {"version", entry.version},
                {"operation", entry.operation},
                {"content_before", entry.content_before.substr(0, 200)},
                {"content_after", entry.content_after.substr(0, 200)},
                {"confidence_before", entry.confidence_before},
                {"confidence_after", entry.confidence_after},
                {"commit_message", entry.commit_message},
                {"session_id", entry.session_id},
                {"tool_name", entry.tool_name},
                {"created_at", entry.created_at}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"memory_id", id},
            {"count", history.size()},
            {"history", history_json}
        });
    }

    DuckDBToolResult tool_memory_revert(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        int32_t version = params.value("version", 0);
        if (version <= 0) {
            return DuckDBToolResult::error("version is required");
        }

        std::string reason = params.value("reason", "");

        if (!mind_->store().revert_to_version(id, version, reason)) {
            return DuckDBToolResult::error("Failed to revert memory #" + std::to_string(id) +
                                          " to version " + std::to_string(version));
        }

        return DuckDBToolResult::ok(
            "Reverted memory #" + std::to_string(id) + " to version " + std::to_string(version),
            {{"memory_id", id}, {"reverted_to_version", version}, {"reason", reason}}
        );
    }

    DuckDBToolResult tool_pin_memory(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        std::string reason = params.value("reason", "important");

        if (!mind_->store().pin_memory(id, reason)) {
            return DuckDBToolResult::error("Failed to pin memory #" + std::to_string(id));
        }

        return DuckDBToolResult::ok(
            "Pinned memory #" + std::to_string(id) + " (" + reason + ")",
            {{"memory_id", id}, {"pinned", true}, {"reason", reason}}
        );
    }

    DuckDBToolResult tool_unpin_memory(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        if (!mind_->store().unpin_memory(id)) {
            return DuckDBToolResult::error("Failed to unpin memory #" + std::to_string(id));
        }

        return DuckDBToolResult::ok(
            "Unpinned memory #" + std::to_string(id),
            {{"memory_id", id}, {"pinned", false}}
        );
    }

    DuckDBToolResult tool_list_pinned(const json& params) {
        std::string realm = params.value("realm", "");
        size_t limit = params.value("limit", 50);

        auto pinned = mind_->store().list_pinned(realm, limit);

        std::ostringstream ss;
        ss << "Pinned Memories";
        if (!realm.empty()) ss << " (realm: " << realm << ")";
        ss << "\n" << std::string(40, '-') << "\n";

        json pinned_json = json::array();
        for (const auto& mem : pinned) {
            ss << "• #" << mem.id << " [" << mem.kind << "] "
               << mem.content.substr(0, 60) << "...\n";

            pinned_json.push_back({
                {"id", mem.id},
                {"content", mem.content.substr(0, 200)},
                {"kind", mem.kind},
                {"confidence", mem.confidence},
                {"realm", mem.realm}
            });
        }

        if (pinned.empty()) {
            ss << "No pinned memories.\n";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"count", pinned.size()},
            {"pinned", pinned_json}
        });
    }

    DuckDBToolResult tool_memory_lock(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        std::string holder_id = params.value("holder_id", "");
        if (holder_id.empty()) {
            return DuckDBToolResult::error("holder_id is required");
        }

        std::string holder_type = params.value("holder_type", "session");
        int64_t duration = params.value("duration", 300);  // 5 min default

        if (!mind_->store().acquire_lock(id, holder_id, holder_type, "exclusive", duration)) {
            auto existing = mind_->store().get_lock(id);
            if (existing) {
                return DuckDBToolResult::error(
                    "Memory #" + std::to_string(id) + " is locked by " +
                    existing->holder_id + " (" + existing->holder_type + ")"
                );
            }
            return DuckDBToolResult::error("Failed to acquire lock on memory #" + std::to_string(id));
        }

        return DuckDBToolResult::ok(
            "Acquired lock on memory #" + std::to_string(id) + " for " + std::to_string(duration) + "s",
            {{"memory_id", id}, {"holder_id", holder_id}, {"duration_seconds", duration}, {"locked", true}}
        );
    }

    DuckDBToolResult tool_memory_unlock(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        std::string holder_id = params.value("holder_id", "");
        if (holder_id.empty()) {
            return DuckDBToolResult::error("holder_id is required");
        }

        if (!mind_->store().release_lock(id, holder_id)) {
            return DuckDBToolResult::error("Failed to release lock (not held by " + holder_id + "?)");
        }

        return DuckDBToolResult::ok(
            "Released lock on memory #" + std::to_string(id),
            {{"memory_id", id}, {"locked", false}}
        );
    }

    DuckDBToolResult tool_memory_lock_status(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        auto lock = mind_->store().get_lock(id);
        if (!lock) {
            return DuckDBToolResult::ok(
                "Memory #" + std::to_string(id) + " is not locked",
                {{"memory_id", id}, {"locked", false}}
            );
        }

        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        int64_t remaining_ms = lock->expires_at - now_ms;
        int64_t remaining_s = remaining_ms / 1000;

        std::ostringstream ss;
        ss << "Memory #" << id << " is locked\n";
        ss << "  Holder: " << lock->holder_id << " (" << lock->holder_type << ")\n";
        ss << "  Type: " << lock->lock_type << "\n";
        ss << "  Expires in: " << remaining_s << "s\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"memory_id", id},
            {"locked", true},
            {"holder_id", lock->holder_id},
            {"holder_type", lock->holder_type},
            {"lock_type", lock->lock_type},
            {"expires_in_seconds", remaining_s}
        });
    }

    DuckDBToolResult tool_propose_change(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        std::string content = params.value("content", "");
        if (content.empty()) {
            return DuckDBToolResult::error("content is required");
        }

        std::string proposed_by = params.value("proposed_by", "unknown");

        int64_t merge_id = mind_->store().propose_change(id, content, proposed_by);
        if (merge_id <= 0) {
            return DuckDBToolResult::error("Failed to propose change");
        }

        return DuckDBToolResult::ok(
            "Change proposed for memory #" + std::to_string(id) + " (merge request #" + std::to_string(merge_id) + ")",
            {{"memory_id", id}, {"merge_id", merge_id}, {"status", "pending"}}
        );
    }

    DuckDBToolResult tool_list_merge_queue(const json& params) {
        std::string status = params.value("status", "pending");
        size_t limit = params.value("limit", 50);

        auto queue = mind_->store().list_merge_queue(status, limit);

        std::ostringstream ss;
        ss << "Merge Queue";
        if (!status.empty()) ss << " (status: " << status << ")";
        ss << "\n" << std::string(40, '-') << "\n";

        json queue_json = json::array();
        for (const auto& entry : queue) {
            ss << "• #" << entry.id << " → memory #" << entry.memory_id
               << " (v" << entry.base_version << ") by " << entry.proposed_by << "\n";

            queue_json.push_back({
                {"merge_id", entry.id},
                {"memory_id", entry.memory_id},
                {"proposed_content", entry.proposed_content.substr(0, 200)},
                {"proposed_by", entry.proposed_by},
                {"base_version", entry.base_version},
                {"status", entry.status},
                {"created_at", entry.created_at}
            });
        }

        if (queue.empty()) {
            ss << "No pending merge requests.\n";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"count", queue.size()},
            {"queue", queue_json}
        });
    }

    DuckDBToolResult tool_resolve_merge(const json& params) {
        int64_t merge_id = params.value("merge_id", 0);
        if (merge_id <= 0) {
            return DuckDBToolResult::error("merge_id is required");
        }

        std::string status = params.value("status", "applied");
        std::string resolution = params.value("resolution", "");

        if (!mind_->store().resolve_merge(merge_id, resolution, status)) {
            return DuckDBToolResult::error("Failed to resolve merge request #" + std::to_string(merge_id));
        }

        std::string action = (status == "applied") ? "applied" :
                            (status == "rejected") ? "rejected" : "marked as " + status;

        return DuckDBToolResult::ok(
            "Merge request #" + std::to_string(merge_id) + " " + action,
            {{"merge_id", merge_id}, {"status", status}, {"resolution", resolution}}
        );
    }
