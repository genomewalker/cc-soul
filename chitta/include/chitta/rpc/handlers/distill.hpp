// Included into DuckDBHandler class body — not a standalone header

    DuckDBToolResult tool_suggestion_track(const json& params) {
        std::string content = params.value("content", "");
        if (content.empty()) {
            return DuckDBToolResult::error("content is required");
        }

        Suggestion s;
        s.content = content;
        s.context = params.value("context", "");
        s.realm = params.value("realm", "brahman");

        int64_t id = mind_->store().suggestion_track(s);
        if (id < 0) {
            return DuckDBToolResult::error("Failed to track suggestion");
        }

        std::ostringstream ss;
        ss << "Tracked suggestion #" << id << ": " << content.substr(0, 100)
           << (content.size() > 100 ? "..." : "");

        return DuckDBToolResult::ok(ss.str(), {
            {"id", id},
            {"content", content},
            {"realm", s.realm}
        });
    }

    DuckDBToolResult tool_suggestion_pending(const json& params) {
        std::string realm = params.value("realm", "");
        size_t limit = params.value("limit", 20);

        auto suggestions = mind_->store().suggestion_list_pending(realm, limit);

        if (suggestions.empty()) {
            return DuckDBToolResult::ok("No pending suggestions", {{"count", 0}});
        }

        std::ostringstream ss;
        ss << "Pending suggestions (" << suggestions.size() << "):\n";
        json items = json::array();

        for (const auto& s : suggestions) {
            ss << "  #" << s.id << ": " << s.content.substr(0, 80)
               << (s.content.size() > 80 ? "..." : "") << "\n";
            items.push_back({
                {"id", s.id},
                {"content", s.content},
                {"context", s.context},
                {"realm", s.realm},
                {"suggested_at", s.suggested_at}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"count", suggestions.size()},
            {"suggestions", items}
        });
    }

    DuckDBToolResult tool_suggestion_resolve(const json& params) {
        int64_t id = params.value("id", 0);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        bool helped = params.value("helped", false);
        std::string details = params.value("details", "");

        // First, create an outcome memory
        std::string outcome_type = helped ? "worked" : "failed";
        auto suggestion = mind_->store().suggestion_get(id);
        if (!suggestion) {
            return DuckDBToolResult::error("Suggestion not found");
        }

        // Store outcome as memory
        std::string memory_content = "[outcome:" + outcome_type + "] " + suggestion->content;
        if (!details.empty()) {
            memory_content += "\nDetails: " + details;
        }

        // Remember the outcome
        std::vector<float> embed;
        if (mind_->embedder_ready()) {
            embed = mind_->embedder().embed(memory_content).data;
        }
        int64_t memory_id = mind_->store().remember(
            memory_content,
            "episode",
            embed,
            0.8f,       // confidence
            0.05f,      // decay_rate
            suggestion->realm,
            RealmVisibility::Global  // Outcomes are globally visible
        );

        // Resolve the suggestion
        bool ok = mind_->store().suggestion_resolve(id, helped, details, memory_id);
        if (!ok) {
            return DuckDBToolResult::error("Failed to resolve suggestion");
        }

        // Create triplet for feedback tracking
        std::string slug = suggestion->content.substr(0, 40);
        for (char& c : slug) {
            if (c == ' ') c = '_';
            else c = std::tolower(c);
        }
        mind_->store().connect(slug, "resulted_in", outcome_type);

        std::ostringstream ss;
        ss << "Resolved suggestion #" << id << "\n"
           << "  Helped: " << (helped ? "yes" : "no") << "\n"
           << "  Memory: #" << memory_id;

        return DuckDBToolResult::ok(ss.str(), {
            {"id", id},
            {"helped", helped},
            {"memory_id", memory_id}
        });
    }

    DuckDBToolResult tool_suggestion_count(const json& params) {
        std::string realm = params.value("realm", "");
        size_t count = mind_->store().suggestion_count_pending(realm);

        return DuckDBToolResult::ok(
            "Pending suggestions: " + std::to_string(count),
            {{"count", count}, {"realm", realm.empty() ? "all" : realm}}
        );
    }

    DuckDBToolResult tool_consolidation_scan(const json& params) {
        float threshold = params.value("similarity_threshold", 0.85f);
        size_t limit = params.value("limit", 20);
        std::string realm = params.value("realm", "");

        auto candidates = mind_->store().consolidation_scan(threshold, limit, realm);

        if (candidates.empty()) {
            return DuckDBToolResult::ok("No similar memory pairs found", {{"count", 0}});
        }

        std::ostringstream ss;
        ss << "Found " << candidates.size() << " consolidation candidates:\n\n";
        json items = json::array();

        for (const auto& c : candidates) {
            int pct = static_cast<int>(c.similarity * 100);
            ss << "[" << pct << "%] #" << c.primary_id << " <-> #" << c.secondary_id << "\n"
               << "  Primary: " << c.primary_content.substr(0, 60) << "...\n"
               << "  Secondary: " << c.secondary_content.substr(0, 60) << "...\n\n";

            items.push_back({
                {"primary_id", c.primary_id},
                {"secondary_id", c.secondary_id},
                {"similarity", c.similarity},
                {"primary_preview", c.primary_content.substr(0, 100)},
                {"secondary_preview", c.secondary_content.substr(0, 100)}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"count", candidates.size()},
            {"candidates", items}
        });
    }

    DuckDBToolResult tool_consolidation_merge(const json& params) {
        int64_t primary_id = params.value("primary_id", 0);
        int64_t secondary_id = params.value("secondary_id", 0);
        std::string merged_content = params.value("merged_content", "");

        if (primary_id <= 0 || secondary_id <= 0) {
            return DuckDBToolResult::error("primary_id and secondary_id are required");
        }

        bool ok = mind_->store().consolidation_merge(primary_id, secondary_id, merged_content);
        if (!ok) {
            return DuckDBToolResult::error("Failed to merge memories");
        }

        std::ostringstream ss;
        ss << "Merged memories:\n"
           << "  Primary #" << primary_id << " absorbed #" << secondary_id << "\n"
           << "  Secondary marked for pruning";

        return DuckDBToolResult::ok(ss.str(), {
            {"primary_id", primary_id},
            {"secondary_id", secondary_id},
            {"merged", true}
        });
    }

    DuckDBToolResult tool_consolidation_auto(const json& params) {
        float threshold = params.value("similarity_threshold", 0.90f);
        size_t max_merges = params.value("max_merges", 20);

        size_t merged = mind_->store().consolidation_auto(threshold, max_merges);

        std::ostringstream ss;
        ss << "Auto-consolidation complete:\n"
           << "  Merged " << merged << " memory pairs\n"
           << "  Threshold: " << static_cast<int>(threshold * 100) << "%";

        return DuckDBToolResult::ok(ss.str(), {
            {"merged_count", merged},
            {"threshold", threshold}
        });
    }

    DuckDBToolResult tool_metacognition_corrections(const json& params) {
        size_t limit = params.value("limit", 50);

        // Query memories tagged as corrections (tags in separate table)
        std::string sql = "SELECT DISTINCT m.content, m.confidence, m.created_at FROM memory m "
                          "LEFT JOIN memory_tags t ON m.id = t.memory_id "
                          "WHERE m.content LIKE '%[correction]%' OR t.tag = 'correction' "
                          "ORDER BY m.created_at DESC LIMIT " + std::to_string(limit);

        auto result = mind_->store().raw_query(sql);
        if (!result) {
            return DuckDBToolResult::error("Failed to query corrections");
        }

        std::vector<std::string> corrections;
        std::map<std::string, int> domains;  // Domain -> count

        auto chunk = result->Fetch();
        while (chunk && chunk->size() > 0) {
            for (size_t i = 0; i < chunk->size(); i++) {
                std::string content = chunk->GetValue(0, i).ToString();
                corrections.push_back(content);

                // Extract domain from content like "[correction:domain]"
                size_t start = content.find("[correction:");
                if (start != std::string::npos) {
                    size_t end = content.find("]", start);
                    if (end != std::string::npos) {
                        std::string domain = content.substr(start + 12, end - start - 12);
                        domains[domain]++;
                    }
                } else {
                    domains["general"]++;
                }
            }
            chunk = result->Fetch();
        }

        std::ostringstream ss;
        ss << "Correction analysis (" << corrections.size() << " corrections):\n\n";

        if (!domains.empty()) {
            ss << "Domains with corrections:\n";
            for (const auto& [domain, count] : domains) {
                ss << "  " << domain << ": " << count << "\n";
            }
        }

        // Sample recent corrections
        ss << "\nRecent corrections:\n";
        for (size_t i = 0; i < std::min(corrections.size(), size_t(5)); i++) {
            ss << "  - " << corrections[i].substr(0, 100) << "...\n";
        }

        json result_json = {
            {"total_corrections", corrections.size()},
            {"domains", domains}
        };

        return DuckDBToolResult::ok(ss.str(), result_json);
    }

    DuckDBToolResult tool_metacognition_outcomes(const json& params) {
        size_t limit = params.value("limit", 50);

        // Query outcome memories
        std::string sql = "SELECT content, tags, created_at FROM memory "
                          "WHERE content LIKE '%[outcome:%' "
                          "ORDER BY created_at DESC LIMIT " + std::to_string(limit);

        auto result = mind_->store().raw_query(sql);
        if (!result) {
            return DuckDBToolResult::error("Failed to query outcomes");
        }

        int worked = 0, failed = 0;
        std::vector<std::string> worked_examples, failed_examples;

        auto chunk = result->Fetch();
        while (chunk && chunk->size() > 0) {
            for (size_t i = 0; i < chunk->size(); i++) {
                std::string content = chunk->GetValue(0, i).ToString();
                if (content.find("[outcome:worked]") != std::string::npos) {
                    worked++;
                    if (worked_examples.size() < 3) worked_examples.push_back(content);
                } else if (content.find("[outcome:failed]") != std::string::npos) {
                    failed++;
                    if (failed_examples.size() < 3) failed_examples.push_back(content);
                }
            }
            chunk = result->Fetch();
        }

        int total = worked + failed;
        float success_rate = total > 0 ? (float)worked / total * 100.0f : 0.0f;

        std::ostringstream ss;
        ss << "Outcome analysis:\n\n"
           << "  Total tracked: " << total << "\n"
           << "  Worked: " << worked << " (" << std::fixed << std::setprecision(1) << success_rate << "%)\n"
           << "  Failed: " << failed << "\n";

        if (!failed_examples.empty()) {
            ss << "\nRecent failures (learn from these):\n";
            for (const auto& ex : failed_examples) {
                ss << "  - " << ex.substr(0, 80) << "...\n";
            }
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"total", total},
            {"worked", worked},
            {"failed", failed},
            {"success_rate", success_rate}
        });
    }

    DuckDBToolResult tool_metacognition_evaluate(const json& params) {
        // Get various learning metrics
        std::map<std::string, int> tag_counts;

        // Count memories by key learning tags (tags stored in memory_tags table)
        std::vector<std::string> learning_tags = {"correction", "preference", "insight", "outcome"};
        for (const auto& tag : learning_tags) {
            std::string sql = "SELECT COUNT(DISTINCT m.id) FROM memory m "
                              "LEFT JOIN memory_tags t ON m.id = t.memory_id "
                              "WHERE t.tag = '" + tag + "' OR m.content LIKE '%[" + tag + "%'";
            auto result = mind_->store().raw_query(sql);
            if (result) {
                auto chunk = result->Fetch();
                if (chunk && chunk->size() > 0) {
                    tag_counts[tag] = chunk->GetValue(0, 0).GetValue<int64_t>();
                }
            }
        }

        // Get outcome success rate
        std::string outcome_sql = "SELECT "
            "SUM(CASE WHEN content LIKE '%[outcome:worked]%' THEN 1 ELSE 0 END) as worked, "
            "SUM(CASE WHEN content LIKE '%[outcome:failed]%' THEN 1 ELSE 0 END) as failed "
            "FROM memory WHERE content LIKE '%[outcome:%'";
        auto outcome_result = mind_->store().raw_query(outcome_sql);

        int worked = 0, failed = 0;
        if (outcome_result) {
            auto chunk = outcome_result->Fetch();
            if (chunk && chunk->size() > 0) {
                worked = chunk->GetValue(0, 0).GetValue<int64_t>();
                failed = chunk->GetValue(1, 0).GetValue<int64_t>();
            }
        }

        // Evaluate health
        std::vector<std::string> recommendations;
        float health_score = 0.5f;

        // Check if tracking outcomes
        if (tag_counts["outcome"] < 5) {
            recommendations.push_back("Track more suggestion outcomes to close feedback loops");
        } else {
            health_score += 0.1f;
        }

        // Check correction ratio
        if (tag_counts["correction"] > 20 && tag_counts["insight"] < 5) {
            recommendations.push_back("Many corrections but few insights - look for patterns in mistakes");
        }

        // Success rate
        int total = worked + failed;
        float success_rate = total > 0 ? (float)worked / total * 100.0f : 50.0f;
        if (success_rate > 70) health_score += 0.2f;
        else if (success_rate < 40) recommendations.push_back("Low success rate - review failed suggestions");

        // Check preferences captured
        if (tag_counts["preference"] >= 5) health_score += 0.1f;
        else recommendations.push_back("Capture more user preferences");

        std::ostringstream ss;
        ss << "Meta-cognition evaluation:\n\n"
           << "Learning metrics:\n"
           << "  Corrections: " << tag_counts["correction"] << "\n"
           << "  Preferences: " << tag_counts["preference"] << "\n"
           << "  Insights: " << tag_counts["insight"] << "\n"
           << "  Outcomes: " << tag_counts["outcome"] << " (success: " << std::fixed << std::setprecision(1) << success_rate << "%)\n\n"
           << "Health score: " << std::fixed << std::setprecision(2) << health_score << "/1.0\n";

        if (!recommendations.empty()) {
            ss << "\nRecommendations:\n";
            for (const auto& r : recommendations) {
                ss << "  - " << r << "\n";
            }
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"health_score", health_score},
            {"corrections", tag_counts["correction"]},
            {"preferences", tag_counts["preference"]},
            {"insights", tag_counts["insight"]},
            {"outcomes", tag_counts["outcome"]},
            {"success_rate", success_rate},
            {"recommendations", recommendations}
        });
    }

    DuckDBToolResult tool_curiosity_note_gap(const json& params) {
        std::string gap = params.value("gap", "");
        if (gap.empty()) {
            return DuckDBToolResult::error("gap is required");
        }

        std::string context = params.value("context", "");
        std::string realm = params.value("realm", "brahman");

        // Store as a memory with type "gap"
        std::string content = "[gap] " + gap;
        if (!context.empty()) {
            content += "\nContext: " + context;
        }

        std::vector<float> embed;
        if (mind_->embedder_ready()) {
            embed = mind_->embedder().embed(content).data;
        }

        int64_t id = mind_->store().remember(
            content,
            "gap",  // NodeType::Gap
            embed,
            0.7f,   // moderate confidence
            0.02f,  // slow decay - gaps should persist
            realm,
            RealmVisibility::Private
        );

        // Tag it
        mind_->store().add_tag(id, "gap");
        mind_->store().add_tag(id, "unresolved");

        return DuckDBToolResult::ok(
            "Gap noted #" + std::to_string(id) + ": " + gap.substr(0, 60),
            {{"id", id}, {"gap", gap}}
        );
    }

    DuckDBToolResult tool_curiosity_gaps(const json& params) {
        size_t limit = params.value("limit", 10);
        std::string realm = params.value("realm", "");

        std::string sql = "SELECT m.id, m.content, m.created_at FROM memory m "
                          "JOIN memory_tags t ON m.id = t.memory_id "
                          "WHERE t.tag = 'unresolved' AND m.kind = 'gap' ";
        if (!realm.empty()) {
            sql += "AND m.realm = '" + realm + "' ";
        }
        sql += "ORDER BY m.created_at DESC LIMIT " + std::to_string(limit);

        auto result = mind_->store().raw_query(sql);
        if (!result) {
            return DuckDBToolResult::error("Failed to query gaps");
        }

        std::ostringstream ss;
        ss << "Knowledge gaps:\n";
        json gaps = json::array();

        auto chunk = result->Fetch();
        while (chunk && chunk->size() > 0) {
            for (size_t i = 0; i < chunk->size(); i++) {
                int64_t id = chunk->GetValue(0, i).GetValue<int64_t>();
                std::string content = chunk->GetValue(1, i).ToString();
                ss << "  #" << id << ": " << content.substr(0, 80) << "...\n";
                gaps.push_back({{"id", id}, {"content", content}});
            }
            chunk = result->Fetch();
        }

        if (gaps.empty()) {
            return DuckDBToolResult::ok("No unresolved knowledge gaps", {{"count", 0}});
        }

        return DuckDBToolResult::ok(ss.str(), {{"count", gaps.size()}, {"gaps", gaps}});
    }

    DuckDBToolResult tool_curiosity_resolve(const json& params) {
        int64_t id = params.value("id", 0);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        std::string learned = params.value("learned", "");

        // Remove unresolved tag, add resolved
        mind_->store().remove_tag(id, "unresolved");
        mind_->store().add_tag(id, "resolved");

        // If learned something, store it as an insight
        if (!learned.empty()) {
            std::vector<float> embed;
            if (mind_->embedder_ready()) {
                embed = mind_->embedder().embed(learned).data;
            }

            int64_t insight_id = mind_->store().remember(
                "[insight:exploration] " + learned,
                "wisdom",
                embed,
                0.8f,
                0.05f,
                "brahman",
                RealmVisibility::Global
            );

            // Link gap to insight
            mind_->store().connect(std::to_string(id), "led_to", std::to_string(insight_id));
        }

        return DuckDBToolResult::ok(
            "Gap #" + std::to_string(id) + " resolved",
            {{"id", id}, {"learned", learned}}
        );
    }

    DuckDBToolResult tool_distill_status(const json& params) {
        auto transcripts = mind_->store().get_pending_transcripts();

        // Group by realm
        std::map<std::string, std::vector<const TranscriptState*>> by_realm;
        for (const auto& t : transcripts) {
            by_realm[t.realm].push_back(&t);
        }

        // Count pending work per transcript
        size_t total_pending = 0;
        json transcripts_json = json::array();

        for (const auto& t : transcripts) {
            // Check file for new lines
            size_t file_lines = 0;
            size_t pending_lines = 0;

            std::ifstream file(t.transcript_path);
            if (file) {
                std::string line;
                while (std::getline(file, line)) {
                    file_lines++;
                }
                pending_lines = (file_lines > static_cast<size_t>(t.last_processed_line))
                    ? file_lines - t.last_processed_line : 0;
                total_pending += pending_lines;
            }

            transcripts_json.push_back({
                {"session_id", t.session_id},
                {"realm", t.realm},
                {"last_processed_line", t.last_processed_line},
                {"file_lines", file_lines},
                {"pending_lines", pending_lines},
                {"last_distilled_at", t.last_distilled_at}
            });
        }

        // Build realm summary
        json realms_json = json::object();
        for (const auto& [realm, ts] : by_realm) {
            realms_json[realm] = ts.size();
        }

        std::ostringstream ss;
        ss << "Distillation Status\n";
        ss << "═══════════════════════════════\n\n";
        ss << "Registered transcripts: " << transcripts.size() << "\n";
        ss << "Total pending lines: " << total_pending << "\n\n";

        ss << "By realm:\n";
        for (const auto& [realm, ts] : by_realm) {
            ss << "  " << realm << ": " << ts.size() << " transcript(s)\n";
        }

        ss << "\nTranscripts:\n";
        for (const auto& t : transcripts) {
            ss << "  [" << t.session_id.substr(0, 8) << "...] "
               << t.realm << " - line " << t.last_processed_line << "\n";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"transcript_count", transcripts.size()},
            {"total_pending_lines", total_pending},
            {"realms", realms_json},
            {"transcripts", transcripts_json}
        });
    }

    DuckDBToolResult tool_epiplexity_check(const json& params) {
        std::string original = params.value("original", "");
        std::string seed = params.value("seed", "");
        std::string reconstructed = params.value("reconstructed", "");

        if (original.empty() || seed.empty() || reconstructed.empty()) {
            return DuckDBToolResult::error("original, seed, and reconstructed are all required");
        }

        Epiplexity e = mind_->compute_epiplexity(original, seed, reconstructed);

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "Epiplexity Analysis\n";
        ss << "═══════════════════════════════\n\n";
        ss << "ε = " << e.score << " (combined score)\n\n";
        ss << "Components:\n";
        ss << "  S (semantic fidelity):    " << e.semantic_fidelity << "\n";
        ss << "  K (entity preservation):  " << e.entity_preservation << "\n";
        ss << "  D (information density):  " << e.information_density << "\n";
        ss << "  C (compression utility):  " << e.compression_utility << "\n\n";

        // Quality assessment
        std::string quality;
        if (e.score >= 0.8f) quality = "Excellent - seed is highly reconstructable";
        else if (e.score >= 0.6f) quality = "Good - seed preserves key meaning";
        else if (e.score >= 0.4f) quality = "Fair - some information loss";
        else quality = "Poor - consider expanding seed or using full content";

        ss << "Quality: " << quality << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"score", e.score},
            {"semantic_fidelity", e.semantic_fidelity},
            {"entity_preservation", e.entity_preservation},
            {"information_density", e.information_density},
            {"compression_utility", e.compression_utility}
        });
    }

    DuckDBToolResult tool_learn_outcome(const json& params) {
        // Parse memory ID (can be integer or UUID string)
        // Try both "memory_id" (RPC) and "memory-id" (CLI)
        auto [memory_id, id_str] = parse_id(params, "memory_id");
        if (memory_id == 0) {
            std::tie(memory_id, id_str) = parse_id(params, "memory-id");
        }
        if (memory_id == 0) {
            return DuckDBToolResult::error("Invalid memory_id");
        }

        std::string outcome = params.value("outcome", "");
        if (outcome != "positive" && outcome != "negative" && outcome != "neutral") {
            return DuckDBToolResult::error("outcome must be positive, negative, or neutral");
        }

        std::string context = params.value("context", "");

        // Get session ID (if available from environment or use default)
        std::string session_id = "current_session";

        // Record the outcome
        int64_t outcome_id = mind_->store().record_usage_outcome(memory_id, session_id, outcome, context);
        if (outcome_id < 0) {
            return DuckDBToolResult::error("Failed to record outcome");
        }

        // Adjust confidence based on outcome
        // Construct NodeId from int64
        NodeId nid;
        nid.high = 0;
        nid.low = static_cast<uint64_t>(memory_id);

        if (outcome == "positive") {
            mind_->strengthen(nid, 0.1f);
        } else if (outcome == "negative") {
            mind_->weaken(nid, 0.15f);
            // Flag high-value categories for review if negative
            auto mem = mind_->store().get_memory(memory_id);
            if (mem && (mem->kind == "correction" || mem->kind == "preference" ||
                        mem->kind == "solution" || mem->kind == "gotcha")) {
                // Could add to synthesis queue for review
            }
        }

        // Get updated stats
        auto stats = mind_->store().get_usage_stats(memory_id);

        std::ostringstream ss;
        ss << "Recorded " << outcome << " outcome for memory " << memory_id << "\n"
           << "Stats: " << stats.positive << " positive, "
           << stats.negative << " negative, "
           << stats.neutral << " neutral "
           << "(positive rate: " << std::fixed << std::setprecision(1)
           << (stats.positive_rate * 100) << "%)\n";

        if (outcome == "positive") {
            ss << "Confidence boosted by 0.1";
        } else if (outcome == "negative") {
            ss << "Confidence reduced by 0.15";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"outcome_id", outcome_id},
            {"memory_id", memory_id},
            {"outcome", outcome},
            {"stats", {
                {"positive", stats.positive},
                {"negative", stats.negative},
                {"neutral", stats.neutral},
                {"positive_rate", stats.positive_rate}
            }}
        });
    }

    DuckDBToolResult tool_log_exposure(const json& params) {
        auto session_id = params.value("session_id", std::string{});
        auto turn_id = params.value("turn_id", 0);
        auto hook_type = params.value("hook_type", std::string{});

        if (session_id.empty() || hook_type.empty()) {
            return DuckDBToolResult::error("session_id and hook_type required");
        }

        std::vector<int64_t> memory_ids;
        if (params.contains("memory_ids") && params["memory_ids"].is_array()) {
            for (const auto& id : params["memory_ids"]) memory_ids.push_back(id.get<int64_t>());
        }
        if (memory_ids.empty()) return DuckDBToolResult::error("memory_ids required");

        std::vector<int> ranks;
        if (params.contains("ranks") && params["ranks"].is_array()) {
            for (const auto& r : params["ranks"]) ranks.push_back(r.get<int>());
        }
        std::vector<double> scores;
        if (params.contains("resonance_scores") && params["resonance_scores"].is_array()) {
            for (const auto& s : params["resonance_scores"]) scores.push_back(s.get<double>());
        }

        auto n = mind_->store().log_exposures_batch(
            session_id, turn_id, hook_type, memory_ids, ranks, {}, scores, {});

        return DuckDBToolResult::ok("Logged " + std::to_string(n) + " exposures",
            {{"logged", n}});
    }

    DuckDBToolResult tool_get_sus_metrics(const json& params) {
        int days = params.value("days", 7);
        auto m = compute_sus(days);

        json result = {
            {"days", m.days},
            {"n_sessions", m.n_sessions},
            {"n_exposures", m.n_exposures},
            {"n_recalls", m.n_recalls},
            {"n_memories", m.n_memories},
            {"R", m.R >= 0 ? json(m.R) : json(nullptr)},
            {"P", m.P >= 0 ? json(m.P) : json(nullptr)},
            {"M", m.M >= 0 ? json(m.M) : json(nullptr)},
            {"T", m.T >= 0 ? json(m.T) : json(nullptr)},
            {"D", m.D >= 0 ? json(m.D) : json(nullptr)},
            {"sus_partial", m.sus >= 0 ? json(m.sus) : json(nullptr)},
            {"note", m.M >= 0 ? "Full SUS score (all dimensions)" : "M accumulating (need >=3 correction exposures)"}
        };

        std::string summary = "SUS(" + std::to_string(days) + "d): ";
        if (m.sus >= 0) summary += std::to_string((int)m.sus);
        else summary += "--";
        summary += "  R:" + (m.R >= 0 ? std::to_string(m.R).substr(0,4) : "--");
        summary += " P:" + (m.P >= 0 ? std::to_string(m.P).substr(0,4) : "--");
        summary += " D:" + (m.D >= 0 ? std::to_string(m.D).substr(0,4) : "--");

        return DuckDBToolResult::ok(summary, result);
    }

    DuckDBToolResult tool_episode_cluster_status(const json& params) {
        // Accept both underscore and hyphen versions of params
        float similarity_threshold = params.contains("similarity_threshold")
            ? params.value("similarity_threshold", 0.85f)
            : params.value("similarity-threshold", 0.85f);
        size_t min_occurrences = params.contains("min_occurrences")
            ? params.value("min_occurrences", 3)
            : params.value("min-occurrences", 3);

        auto candidates = mind_->store().find_distill_candidates(
            similarity_threshold, min_occurrences, 20);

        std::ostringstream ss;
        ss << "Episode Cluster Status (for Auto-Distillation)\n"
           << "Similarity threshold: " << similarity_threshold << "\n"
           << "Minimum occurrences: " << min_occurrences << "\n\n";

        if (candidates.empty()) {
            ss << "No episode clusters found for distillation.\n";
        } else {
            ss << "Found " << candidates.size() << " candidate clusters:\n\n";
            for (size_t i = 0; i < candidates.size(); ++i) {
                const auto& c = candidates[i];
                ss << "Cluster " << (i + 1) << ":\n"
                   << "  Episodes: " << c.episode_ids.size() << "\n"
                   << "  Avg similarity: " << std::fixed << std::setprecision(2)
                   << c.avg_similarity << "\n"
                   << "  Avg confidence: " << c.avg_confidence << "\n"
                   << "  Pattern: " << c.pattern_content.substr(0, 100)
                   << (c.pattern_content.size() > 100 ? "..." : "") << "\n\n";
            }
        }

        json result = {
            {"similarity_threshold", similarity_threshold},
            {"min_occurrences", min_occurrences},
            {"cluster_count", candidates.size()},
            {"clusters", json::array()}
        };
        for (const auto& c : candidates) {
            result["clusters"].push_back({
                {"episode_count", c.episode_ids.size()},
                {"avg_similarity", c.avg_similarity},
                {"avg_confidence", c.avg_confidence},
                {"pattern_preview", c.pattern_content.substr(0, 200)}
            });
        }

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_ssl_convert(const json& params) {
        std::string content = params.value("content", "");
        if (content.empty()) {
            return DuckDBToolResult::error("Content is required");
        }

        std::string domain = params.value("domain", "note");
        std::string location = params.value("location", "");

        if (is_ssl_format(content)) {
            return DuckDBToolResult::ok("Already in SSL format", {
                {"converted", false},
                {"content", content}
            });
        }

        std::string ssl_content = to_ssl_format(content, domain, location);

        std::ostringstream ss;
        ss << "Converted to SSL format:\n" << ssl_content;

        return DuckDBToolResult::ok(ss.str(), {
            {"converted", true},
            {"content", ssl_content},
            {"domain", domain}
        });
    }
