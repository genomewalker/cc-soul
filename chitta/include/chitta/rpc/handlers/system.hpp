// Included into DuckDBHandler class body — not a standalone header

    DuckDBToolResult tool_convergence_metrics(const json&) {
        std::ostringstream ss;
        json metrics;

        // NULL-safe parsers (execute_sql_query serializes NULLs as "NULL")
        auto to_i = [](const std::string& s) -> int64_t {
            return (s.empty() || s == "NULL") ? 0 : std::stoll(s);
        };
        auto to_d = [](const std::string& s) -> double {
            return (s.empty() || s == "NULL") ? 0.0 : std::stod(s);
        };

        // --- Correction recall rate ---
        // Wisdom/distilled memories tagged correction/compliance: are they being recalled?
        auto corr = mind_->store().execute_sql_query(
            "SELECT COUNT(*) as total, "
            "SUM(CASE WHEN access_count = 0 THEN 1 ELSE 0 END) as never_recalled, "
            "AVG(access_count) as avg_access, "
            "SUM(CASE WHEN access_count >= 3 THEN 1 ELSE 0 END) as well_recalled "
            "FROM memory WHERE kind IN ('wisdom', 'distilled') "
            "AND (content LIKE '%CORRECTION%' OR content LIKE '%[correction%' "
            "  OR id IN (SELECT memory_id FROM memory_tags "
            "    WHERE tag IN ('correction', 'compliance', 'gotcha')))");

        int64_t total_corrections = 0, never_recalled = 0, well_recalled = 0;
        double avg_access = 0.0;
        if (corr.success && !corr.rows.empty() && corr.rows[0].size() >= 4) {
            const auto& r = corr.rows[0];
            total_corrections = to_i(r[0]);
            never_recalled    = to_i(r[1]);
            avg_access        = to_d(r[2]);
            well_recalled     = to_i(r[3]);
        }
        double recall_rate = total_corrections > 0
            ? 100.0 * (total_corrections - never_recalled) / total_corrections : 0.0;

        ss << "=== Correction Recall ===\n";
        ss << "  Total correction memories: " << total_corrections << "\n";
        ss << "  Never recalled:            " << never_recalled << "\n";
        ss << "  Well recalled (>=3x):      " << well_recalled << "\n";
        ss << "  Avg access count:          " << std::fixed << std::setprecision(1) << avg_access << "\n";
        ss << "  Recall rate:               " << std::fixed << std::setprecision(1) << recall_rate << "%\n\n";

        metrics["correction"] = {
            {"total", total_corrections},
            {"never_recalled", never_recalled},
            {"well_recalled", well_recalled},
            {"avg_access", avg_access},
            {"recall_rate_pct", recall_rate}
        };

        // --- Triplet traversal rate ---
        auto trav = mind_->store().execute_sql_query(
            "SELECT COUNT(*) as total, "
            "SUM(CASE WHEN use_count > 0 THEN 1 ELSE 0 END) as traversed, "
            "MAX(use_count) as max_use, "
            "AVG(CASE WHEN use_count > 0 THEN CAST(use_count AS DOUBLE) ELSE NULL END) as avg_active_use "
            "FROM triplet WHERE valid_to_ms = 0");

        int64_t total_triplets = 0, traversed = 0, max_use = 0;
        double avg_active_use = 0.0;
        if (trav.success && !trav.rows.empty() && trav.rows[0].size() >= 4) {
            const auto& r = trav.rows[0];
            total_triplets = to_i(r[0]);
            traversed      = to_i(r[1]);
            max_use        = to_i(r[2]);
            avg_active_use = to_d(r[3]);
        }
        double traversal_rate = total_triplets > 0
            ? 100.0 * traversed / total_triplets : 0.0;

        ss << "=== Triplet Traversal ===\n";
        ss << "  Active triplets:           " << total_triplets << "\n";
        ss << "  Ever traversed:            " << traversed << "\n";
        ss << "  Traversal rate:            " << std::fixed << std::setprecision(2) << traversal_rate << "%\n";
        ss << "  Max traversals (one edge): " << max_use << "\n";
        ss << "  Avg traversals (active):   " << std::fixed << std::setprecision(1) << avg_active_use << "\n\n";

        metrics["triplets"] = {
            {"total_active", total_triplets},
            {"traversed", traversed},
            {"traversal_rate_pct", traversal_rate},
            {"max_use", max_use},
            {"avg_active_use", avg_active_use}
        };

        // --- Top traversed triplets ---
        auto top = mind_->store().execute_sql_query(
            "SELECT subject, predicate, object, use_count FROM triplet "
            "WHERE use_count > 0 AND valid_to_ms = 0 "
            "ORDER BY use_count DESC LIMIT 10");

        ss << "=== Most Traversed Edges ===\n";
        json top_json = json::array();
        if (top.success) {
            for (const auto& row : top.rows) {
                if (row.size() < 4) continue;
                ss << "  [" << row[3] << "x] " << row[0] << " -> " << row[1] << " -> " << row[2] << "\n";
                top_json.push_back({
                    {"subject", row[0]}, {"predicate", row[1]},
                    {"object", row[2]},  {"use_count", std::stoll(row[3])}
                });
            }
        }
        if (top_json.empty()) ss << "  (none yet -- traversal tracking just started)\n";

        // --- Convergence signal ---
        ss << "\n=== Signal ===\n";
        bool converging = recall_rate >= 60.0 && traversal_rate >= 1.0;
        ss << "  " << (converging ? "CONVERGING" : "DRIFTING") << " -- "
           << "corrections recalled " << std::fixed << std::setprecision(0) << recall_rate << "%, "
           << "graph explored " << std::fixed << std::setprecision(2) << traversal_rate << "%\n";

        metrics["signal"] = converging ? "converging" : "drifting";
        metrics["top_traversed"] = top_json;

        return DuckDBToolResult::ok(ss.str(), metrics);
    }

    DuckDBToolResult tool_resonance_stats(const json&) {
        auto stats = mind_->get_learning_stats();
        auto best  = mind_->learner().get_best_params();

        std::ostringstream ss;
        ss << "=== ResonanceLearner State ===\n";
        ss << "  Learning enabled: " << (mind_->is_learning_enabled() ? "yes" : "no") << "\n";
        ss << "  Total feedback:   " << stats.total_feedback << "\n";
        ss << "  Positive:         " << stats.positive_feedback << "\n";
        ss << "  Negative:         " << stats.negative_feedback << "\n";
        ss << "  Avg reward:       " << std::fixed << std::setprecision(3) << stats.avg_reward << "\n\n";

        ss << "=== Parameter Posteriors (Beta distribution means ± std dev) ===\n";
        for (const auto& [name, mean] : stats.param_means) {
            double uncertainty = 0.0;
            auto it = stats.param_uncertainties.find(name);
            if (it != stats.param_uncertainties.end()) uncertainty = it->second;
            ss << "  " << std::left << std::setw(26) << name
               << " mean=" << std::fixed << std::setprecision(3) << mean
               << "  ±" << std::fixed << std::setprecision(3) << uncertainty << "\n";
        }

        ss << "\n=== Current Best Config (posterior exploitation) ===\n";
        ss << "  spread_strength:      " << std::fixed << std::setprecision(3) << best.spread_strength << "\n";
        ss << "  spread_decay:         " << std::fixed << std::setprecision(3) << best.spread_decay << "\n";
        ss << "  hebbian_strength:     " << std::fixed << std::setprecision(3) << best.hebbian_strength << "\n";
        ss << "  basin_boost:          " << std::fixed << std::setprecision(3) << best.basin_boost << "\n";
        ss << "  similarity_threshold: " << std::fixed << std::setprecision(3) << best.similarity_threshold << "\n";
        ss << "  inhibition_strength:  " << std::fixed << std::setprecision(3) << best.inhibition_strength << "\n";
        ss << "  semantic_weight:      " << std::fixed << std::setprecision(3) << best.semantic_weight << "\n";
        ss << "  activation_weight:    " << std::fixed << std::setprecision(3) << best.activation_weight << "\n";

        if (stats.total_feedback == 0) {
            ss << "\nNOTE: No feedback received yet — learner is using uniform Beta(1,1) priors.\n"
               << "Call strengthen/weaken on memory IDs to start training the posterior.\n";
        }

        json result;
        result["learning_enabled"] = mind_->is_learning_enabled();
        result["feedback"] = {
            {"total",    (int64_t)stats.total_feedback},
            {"positive", (int64_t)stats.positive_feedback},
            {"negative", (int64_t)stats.negative_feedback},
            {"avg_reward", stats.avg_reward}
        };
        result["param_means"]         = stats.param_means;
        result["param_uncertainties"] = stats.param_uncertainties;
        result["best_config"] = {
            {"spread_strength",      best.spread_strength},
            {"spread_decay",         best.spread_decay},
            {"hebbian_strength",     best.hebbian_strength},
            {"basin_boost",          best.basin_boost},
            {"similarity_threshold", best.similarity_threshold},
            {"inhibition_strength",  best.inhibition_strength},
            {"semantic_weight",      best.semantic_weight},
            {"activation_weight",    best.activation_weight}
        };

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_soul_context(const json&) {
        auto cached = mind_->store().cached_health();

        // Query actual relationship state (not just stats)
        size_t preferences = 0, corrections = 0, insights = 0, solutions = 0;
        size_t wisdom_nodes = 0, beliefs = 0, episodes = 0;
        float strongest_conf = 0.0f;
        std::string strongest_memory;

        auto type_counts = mind_->store().execute_sql_query(
            "SELECT "
            "  SUM(CASE WHEN content LIKE '[preference%' OR content LIKE '[pref]%' THEN 1 ELSE 0 END) as prefs, "
            "  SUM(CASE WHEN content LIKE '[correction%' OR content LIKE '[gotcha%' THEN 1 ELSE 0 END) as corrections, "
            "  SUM(CASE WHEN content LIKE '[insight%' THEN 1 ELSE 0 END) as insights, "
            "  SUM(CASE WHEN content LIKE '[solution%' OR content LIKE '[sol]%' THEN 1 ELSE 0 END) as solutions, "
            "  SUM(CASE WHEN kind = 'wisdom' OR kind = 'distilled' THEN 1 ELSE 0 END) as wisdom, "
            "  SUM(CASE WHEN kind = 'belief' THEN 1 ELSE 0 END) as beliefs, "
            "  SUM(CASE WHEN kind = 'episode' THEN 1 ELSE 0 END) as episodes "
            "FROM memory WHERE confidence > 0.01"
        );
        if (type_counts.success && !type_counts.rows.empty()) {
            const auto& r = type_counts.rows[0];
            if (r.size() >= 7) {
                preferences = r[0].empty() ? 0 : std::stoull(r[0]);
                corrections = r[1].empty() ? 0 : std::stoull(r[1]);
                insights = r[2].empty() ? 0 : std::stoull(r[2]);
                solutions = r[3].empty() ? 0 : std::stoull(r[3]);
                wisdom_nodes = r[4].empty() ? 0 : std::stoull(r[4]);
                beliefs = r[5].empty() ? 0 : std::stoull(r[5]);
                episodes = r[6].empty() ? 0 : std::stoull(r[6]);
            }
        }

        // Get strongest memory (highest confidence, most accessed)
        auto top = mind_->store().execute_sql_query(
            "SELECT content, confidence FROM memory WHERE confidence > 0.5 "
            "ORDER BY confidence DESC, accessed_at DESC LIMIT 1"
        );
        if (top.success && !top.rows.empty() && top.rows[0].size() >= 2) {
            strongest_memory = top.rows[0][0];
            if (strongest_memory.size() > 80) strongest_memory = strongest_memory.substr(0, 80) + "...";
            strongest_conf = std::stof(top.rows[0][1]);
        }

        // Get indexed projects
        auto projects = mind_->store().execute_sql_query(
            "SELECT project, COUNT(*) as files FROM code_file GROUP BY project ORDER BY files DESC"
        );
        json projects_json = json::array();
        if (projects.success) {
            for (const auto& row : projects.rows) {
                if (row.size() >= 2) {
                    projects_json.push_back({{"name", row[0]}, {"files", std::stoi(row[1])}});
                }
            }
        }

        std::ostringstream ss;
        ss << "Soul State:\n";
        ss << "  Partnership: " << preferences << " preferences, "
           << corrections << " corrections, " << insights << " insights, "
           << solutions << " solutions\n";
        ss << "  Memory: " << wisdom_nodes << " wisdom, " << beliefs << " beliefs, "
           << episodes << " episodes (" << cached.total_memories << " total)\n";
        ss << "  Confidence: " << std::fixed << std::setprecision(2) << cached.avg_confidence << " avg\n";
        if (!strongest_memory.empty()) {
            ss << "  Strongest: [" << std::setprecision(0) << (strongest_conf * 100) << "%] "
               << strongest_memory << "\n";
        }
        ss << "  Code: " << cached.total_symbols << " symbols, "
           << cached.total_triplets << " triplets";
        if (!projects_json.empty()) {
            ss << " across " << projects_json.size() << " project"
               << (projects_json.size() > 1 ? "s" : "");
        }
        ss << "\n";
        ss << "  Yantra: " << (mind_->has_yantra() ? "ready" : "not attached") << "\n";
        ss << "  Status: " << (cached.is_open ? "OK" : "ERROR");

        return DuckDBToolResult::ok(ss.str(), {
            {"version", CHITTA_VERSION},
            {"partnership", {
                {"preferences", preferences},
                {"corrections", corrections},
                {"insights", insights},
                {"solutions", solutions}
            }},
            {"memory", {
                {"wisdom", wisdom_nodes},
                {"beliefs", beliefs},
                {"episodes", episodes},
                {"total", cached.total_memories},
                {"avg_confidence", cached.avg_confidence}
            }},
            {"code", {
                {"symbols", cached.total_symbols},
                {"triplets", cached.total_triplets},
                {"projects", projects_json}
            }},
            {"yantra_ready", mind_->has_yantra()},
            {"status", cached.is_open ? "OK" : "ERROR"}
        });
    }

    DuckDBToolResult tool_subconscious_stats(const json&) {
        if (!subconscious_) {
            return DuckDBToolResult::ok("Subconscious not attached", {{"attached", false}});
        }

        const auto& stats = subconscious_->stats();
        const auto& config = subconscious_->config();

        // Calculate uptime
        int64_t uptime_ms = 0;
        if (stats.started_at > 0) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            uptime_ms = now - stats.started_at;
        }
        int64_t uptime_mins = uptime_ms / 60000;

        std::ostringstream ss;
        ss << "Subconscious Status:\n";
        ss << "  Running: " << (subconscious_->is_running() ? "yes" : "no") << "\n";
        ss << "  Uptime: " << uptime_mins << " minutes\n";
        ss << "\nEvents:\n";
        ss << "  Processed: " << stats.events_processed.load() << "\n";
        ss << "\nPattern Detection:\n";
        ss << "  Corrections: " << stats.corrections_detected.load() << "\n";
        ss << "  Preferences: " << stats.preferences_detected.load() << "\n";
        ss << "  Frustrations: " << stats.frustrations_detected.load() << "\n";
        ss << "  Milestones: " << stats.milestones_detected.load() << "\n";
        ss << "\nFeedback Loops:\n";
        ss << "  Suggestions tracked: " << stats.suggestions_tracked.load() << "\n";
        ss << "  Outcomes verified: " << stats.outcomes_verified.load() << "\n";
        ss << "\nMaintenance:\n";
        ss << "  Hygiene runs: " << stats.hygiene_runs.load() << "\n";
        if (stats.last_hygiene_at > 0) {
            int64_t mins_since_hygiene = (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count() - stats.last_hygiene_at) / 60000;
            ss << "  Last hygiene: " << mins_since_hygiene << " minutes ago\n";
        }
        ss << "\nConfig:\n";
        ss << "  Hygiene enabled: " << (config.enable_hygiene ? "yes" : "no") << "\n";
        ss << "  Anticipation enabled: " << (config.enable_anticipation ? "yes" : "no") << "\n";
        ss << "  Pattern detection enabled: " << (config.enable_pattern_detection ? "yes" : "no") << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"running", subconscious_->is_running()},
            {"uptime_minutes", uptime_mins},
            {"events_processed", stats.events_processed.load()},
            {"corrections_detected", stats.corrections_detected.load()},
            {"preferences_detected", stats.preferences_detected.load()},
            {"frustrations_detected", stats.frustrations_detected.load()},
            {"milestones_detected", stats.milestones_detected.load()},
            {"suggestions_tracked", stats.suggestions_tracked.load()},
            {"outcomes_verified", stats.outcomes_verified.load()},
            {"hygiene_runs", stats.hygiene_runs.load()},
            {"last_hygiene_at", stats.last_hygiene_at.load()}
        });
    }

    DuckDBToolResult tool_health_check(const json&) {
        bool yantra = mind_->has_yantra();

        // Memory/symbol counts: prefer chitta-field (primary store) over DuckDB cache
        int64_t total_memories = 0;
        int64_t total_symbols = 0;
        int64_t total_triplets = 0;
        double avg_confidence = 0.0;
        bool using_field = false;

#ifdef CHITTA_FIELD_AVAILABLE
        bool field_init_pending = field_initializing_.load(std::memory_order_acquire);
        if (field_store_) {
            total_memories = static_cast<int64_t>(field_store_->memory_count());
            total_symbols  = static_cast<int64_t>(field_store_->symbol_count());
            using_field = true;
        } else if (field_init_pending) {
            // Async init in progress — report chitta-field as backend but not yet ready
            using_field = true;
        }
#endif
        if (!using_field) {
            auto cached = mind_->store().cached_health();
            total_memories  = cached.total_memories;
            total_symbols   = cached.total_symbols;
            total_triplets  = cached.total_triplets;
            avg_confidence  = cached.avg_confidence;
        }

        // Get execution provider from embedder's yantra
        std::string exec_provider = "N/A";
        auto embedder_yantra = mind_->embedder_yantra();
        if (embedder_yantra) {
            exec_provider = embedder_yantra->execution_provider_name();
        }

        // Count stale sessions (with dead PIDs) - use public session_list API
        auto stale_list = mind_->store().session_list("", "stale");
        int64_t stale_sessions = static_cast<int64_t>(stale_list.size());

        // Count queue items and failed observations
        int64_t queue_depth = 0;
        int64_t failed_observations = 0;
        const char* home = std::getenv("HOME");
        if (home) {
            std::string mind_dir = std::string(home) + "/.claude/mind";

            std::string queue_file = mind_dir + "/.queue.jsonl";
            std::ifstream qf(queue_file);
            if (qf) {
                std::string line;
                while (std::getline(qf, line)) {
                    if (!line.empty()) queue_depth++;
                }
            }

            std::string failed_file = mind_dir + "/.failed_observations.jsonl";
            std::ifstream ff(failed_file);
            if (ff) {
                std::string line;
                while (std::getline(ff, line)) {
                    if (!line.empty()) failed_observations++;
                }
            }
        }

        std::ostringstream ss;
        ss << "Health Check:\n";
        ss << "  Status: OK\n";
#ifdef CHITTA_FIELD_AVAILABLE
        if (field_store_)          ss << "  Backend: chitta-field\n";
        else if (field_initializing_.load(std::memory_order_acquire))
                                   ss << "  Backend: chitta-field (initializing)\n";
        else                       ss << "  Backend: DuckDB\n";
#else
        ss << "  Backend: DuckDB\n";
#endif
        ss << "  Memories: " << total_memories << "\n";
        ss << "  Symbols: " << total_symbols << "\n";
        if (!using_field)
            ss << "  Triplets: " << total_triplets << "\n";
        ss << "  Yantra: " << (yantra ? "ready" : "not attached") << "\n";
        ss << "  Execution: " << exec_provider << "\n";
        ss << "  Stale Sessions: " << stale_sessions << "\n";
        ss << "  Queue Depth: " << queue_depth << "\n";
        ss << "  Failed Observations: " << failed_observations << "\n";

#ifdef CHITTA_FIELD_AVAILABLE
        std::string backend_id = field_store_ ? "chitta-field"
                               : (field_initializing_.load() ? "chitta-field-init" : "duckdb");
#else
        std::string backend_id = "duckdb";
#endif

        return DuckDBToolResult::ok(ss.str(), {
            {"status", "ok"},
            {"daemon", "healthy"},
            {"backend", backend_id},
            {"software_version", CHITTA_VERSION},
            {"protocol_major", CHITTA_PROTOCOL_VERSION_MAJOR},
            {"protocol_minor", CHITTA_PROTOCOL_VERSION_MINOR},
            {"memories", total_memories},
            {"symbols", total_symbols},
            {"triplets", total_triplets},
            {"avg_confidence", avg_confidence},
            {"yantra_ready", yantra},
            {"execution_provider", exec_provider},
            {"stale_sessions", stale_sessions},
            {"queue_depth", queue_depth},
            {"failed_observations", failed_observations}
        });
    }

    DuckDBToolResult tool_version_check() {
        return DuckDBToolResult::ok(
            "cc-soul " + std::string(CHITTA_VERSION) + " (DuckDB backend)",
            {{"version", CHITTA_VERSION}, {"backend", "duckdb"}}
        );
    }

    DuckDBToolResult tool_cycle(const json& params) {
        bool force = params.value("force", false);

        size_t decayed = mind_->tick();
        std::ostringstream ss;
        ss << "Maintenance cycle complete:\n";
        ss << "  Decayed: " << decayed << " nodes\n";

        return DuckDBToolResult::ok(ss.str(), {{"decayed", decayed}, {"forced", force}});
    }

    DuckDBToolResult tool_cleanup(const json& params) {
        bool dry_run = params.value("dry_run", true);

        // Find low-confidence nodes that should be removed
        auto health = mind_->store().health();
        size_t removed = 0;

        if (!dry_run) {
            removed = mind_->store().prune(0.1f, 7.0f);  // Remove nodes with <10% confidence after 7 days
        }

        std::ostringstream ss;
        ss << "Cleanup " << (dry_run ? "(dry run)" : "") << ":\n";
        ss << "  Total memories: " << health.total_memories << "\n";
        ss << "  Removed: " << removed << " weak nodes\n";

        return DuckDBToolResult::ok(ss.str(), {{"removed", removed}, {"dry_run", dry_run}});
    }

    DuckDBToolResult tool_import_soul(const json& params) {
        std::string file = params.value("file", "");
        std::string content = params.value("content", "");

        if (file.empty() && content.empty()) {
            return DuckDBToolResult::error("Either file or content is required");
        }

        std::string ssl_content = content;
        if (!file.empty()) {
            std::ifstream f(file);
            if (!f) {
                return DuckDBToolResult::error("Cannot read file: " + file);
            }
            std::stringstream buffer;
            buffer << f.rdbuf();
            ssl_content = buffer.str();
        }

        // Parse SSL content
        size_t learns = 0;
        size_t triplets = 0;
        std::istringstream stream(ssl_content);
        std::string line;
        std::string current_learn;

        while (std::getline(stream, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') continue;

            // [LEARN] lines
            if (line.find("[LEARN]") == 0 || line.find("[REMEMBER]") == 0) {
                if (!current_learn.empty()) {
                    mind_->remember(current_learn, NodeType::Wisdom);
                    learns++;
                }
                current_learn = line.substr(line.find(']') + 1);
                // Trim leading space
                while (!current_learn.empty() && current_learn[0] == ' ') {
                    current_learn = current_learn.substr(1);
                }
            }
            // [ε] expansion lines - append to current learn
            else if (line.find("[ε]") == 0 || line.find("[e]") == 0) {
                if (!current_learn.empty()) {
                    current_learn += "\n" + line;
                }
            }
            // [TRIPLET] lines
            else if (line.find("[TRIPLET]") == 0) {
                std::string triplet = line.substr(10);
                // Parse "subject predicate object"
                std::istringstream ts(triplet);
                std::string subj, pred, obj;
                ts >> subj >> pred;
                std::getline(ts, obj);
                // Trim obj
                while (!obj.empty() && obj[0] == ' ') obj = obj.substr(1);

                if (!subj.empty() && !pred.empty() && !obj.empty()) {
                    mind_->connect(subj, pred, obj);
                    triplets++;
                }
            }
        }

        // Don't forget the last learn
        if (!current_learn.empty()) {
            mind_->remember(current_learn, NodeType::Wisdom);
            learns++;
        }

        std::ostringstream ss;
        ss << "Imported:\n";
        ss << "  [LEARN] entries: " << learns << "\n";
        ss << "  [TRIPLET] entries: " << triplets << "\n";

        return DuckDBToolResult::ok(ss.str(), {{"learns", learns}, {"triplets", triplets}});
    }

    DuckDBToolResult tool_export_soul(const json& params) {
        std::string file = params.value("file", "");
        std::string tag = params.value("tag", "");
        size_t limit = params.value("limit", 100);

        // Get memories
        std::vector<MemoryResult> memories;
        // For now, do a broad recall to get memories
        if (!tag.empty()) {
            // TODO: Add tag-based recall to store
            return DuckDBToolResult::error("Tag-based export not yet implemented");
        }

        // Export all (limited)
        auto all = mind_->recall("*", limit);  // Broad query

        std::ostringstream ss;
        ss << "# Soul Export\n";
        ss << "# Generated by cc-soul " << CHITTA_VERSION << "\n\n";

        for (const auto& m : all) {
            ss << "[LEARN] " << m.text.substr(0, 200) << "\n";
            if (m.text.size() > 200) {
                ss << "[ε] Full content truncated\n";
            }
            ss << "\n";
        }

        if (!file.empty()) {
            std::ofstream f(file);
            if (!f) {
                return DuckDBToolResult::error("Cannot write to file: " + file);
            }
            f << ss.str();
            return DuckDBToolResult::ok("Exported " + std::to_string(all.size()) + " memories to " + file,
                                        {{"count", all.size()}, {"file", file}});
        }

        return DuckDBToolResult::ok(ss.str(), {{"count", all.size()}});
    }

    DuckDBToolResult tool_chitta_health(const json& /*params*/) {
        json metrics;
        std::ostringstream report;
        bool healthy = true;

        // 1. Correction implementation rate
        // correction_detected=true means mistake was repeated (bad); false means followed (good)
        {
            auto r = mind_->store().execute_sql_query(
                "SELECT COUNT(*) AS total, "
                "       COUNT(CASE WHEN correction_detected = false THEN 1 END) AS followed "
                "FROM correction_outcome");
            int64_t total = 0, followed = 0;
            if (r.success && !r.rows.empty() && r.rows[0].size() >= 2) {
                try { total   = std::stoll(r.rows[0][0]); } catch (...) {}
                try { followed = std::stoll(r.rows[0][1]); } catch (...) {}
            }
            double rate = total > 0 ? (double)followed / total : -1.0;
            metrics["correction_implementation_rate"] = rate < 0 ? "no_data" : std::to_string((int)(rate * 100)) + "%";
            if (rate >= 0 && rate < 0.60) healthy = false;
            report << "Correction follow rate: "
                   << (rate < 0 ? "no data" : std::to_string((int)(rate * 100)) + "% (" + std::to_string(followed) + "/" + std::to_string(total) + ")")
                   << (rate >= 0 && rate < 0.60 ? " ⚠ NEEDS ATTENTION" : "") << "\n";
        }

        // 2. Recurring corrections (same mistake repeated 2+ times)
        {
            auto r = mind_->store().execute_sql_query(
                "SELECT correction_memory_id, COUNT(*) AS repeats "
                "FROM correction_outcome WHERE correction_detected = true "
                "GROUP BY correction_memory_id HAVING COUNT(*) >= 2 "
                "ORDER BY repeats DESC LIMIT 5");
            int recurring = r.success ? (int)r.rows.size() : 0;
            if (recurring > 0) healthy = false;
            json arr = json::array();
            for (const auto& row : r.rows) {
                if (row.size() >= 2) arr.push_back({{"memory_id", row[0]}, {"repeats", row[1]}});
            }
            metrics["recurring_corrections"] = arr;
            report << "Recurring mistakes: " << recurring
                   << (recurring > 0 ? " ⚠ NEEDS ATTENTION — see recurring_corrections" : " (none)") << "\n";
        }

        // 3. Dream synthesis lag — dreams completed without synthesis
        {
            auto r = mind_->store().execute_sql_query(
                "SELECT COUNT(*) FROM dream d "
                "WHERE d.status = 'woke' AND d.memories_created >= 3 "
                "  AND NOT EXISTS ("
                "    SELECT 1 FROM triplets t "
                "    WHERE t.subject = 'dream:' || CAST(d.id AS VARCHAR) "
                "      AND t.predicate = 'synthesized_by'"
                "  )");
            int unsynth = 0;
            if (r.success && !r.rows.empty() && !r.rows[0].empty()) {
                try { unsynth = std::stoi(r.rows[0][0]); } catch (...) {}
            }
            if (unsynth > 2) healthy = false;
            metrics["unsynth_dreams"] = unsynth;
            report << "Dreams awaiting synthesis: " << unsynth
                   << (unsynth > 2 ? " ⚠ NEEDS ATTENTION" : "") << "\n";
        }

        // 4. Memory type distribution
        {
            auto r = mind_->store().execute_sql_query(
                "SELECT "
                "  COUNT(CASE WHEN content LIKE '%[correction]%' THEN 1 END) AS corrections, "
                "  COUNT(CASE WHEN content LIKE '%[gap]%' THEN 1 END) AS gaps, "
                "  COUNT(CASE WHEN content LIKE '%[curiosity]%' THEN 1 END) AS curiosity, "
                "  COUNT(CASE WHEN content LIKE '%[dream]%' THEN 1 END) AS dreams, "
                "  COUNT(CASE WHEN content LIKE '%[meta-correction]%' THEN 1 END) AS meta_corrections "
                "FROM memory");
            if (r.success && !r.rows.empty() && r.rows[0].size() >= 5) {
                json dist;
                dist["corrections"]      = r.rows[0][0];
                dist["gaps"]             = r.rows[0][1];
                dist["curiosity"]        = r.rows[0][2];
                dist["dreams"]           = r.rows[0][3];
                dist["meta_corrections"] = r.rows[0][4];
                metrics["memory_type_distribution"] = dist;
                report << "Memory types — corrections:" << r.rows[0][0]
                       << " gaps:" << r.rows[0][1]
                       << " curiosity:" << r.rows[0][2]
                       << " dreams:" << r.rows[0][3]
                       << " meta-corrections:" << r.rows[0][4] << "\n";
            }
        }

        std::string status = healthy ? "HEALTHY" : "NEEDS ATTENTION";
        metrics["status"] = status;
        report << "\nOverall: " << status << "\n";
        return DuckDBToolResult::ok(report.str(), metrics);
    }

    DuckDBToolResult tool_background_schedule(const json& params) {
        std::string task_type = params.value("task_type", "");
        std::string realm = params.value("realm", "brahman");

        if (task_type.empty()) {
            return DuckDBToolResult::error("task_type is required");
        }

        // Validate task type
        std::vector<std::string> valid_types = {"consolidation", "decay", "pruning", "pattern_extraction"};
        bool valid = std::find(valid_types.begin(), valid_types.end(), task_type) != valid_types.end();
        if (!valid) {
            return DuckDBToolResult::error("Invalid task_type. Valid: consolidation, decay, pruning, pattern_extraction");
        }

        int64_t id = mind_->store().background_schedule(task_type, realm);
        if (id <= 0) {
            return DuckDBToolResult::error("Failed to schedule task: " + mind_->store().last_error());
        }

        return DuckDBToolResult::ok("Background task scheduled (id: " + std::to_string(id) + ")", {
            {"id", id},
            {"task_type", task_type}
        });
    }

    DuckDBToolResult tool_background_status(const json& params) {
        auto status = mind_->store().background_status();

        std::ostringstream ss;
        ss << "Background Processing Status\n";
        ss << "════════════════════════════\n\n";
        ss << "Task Queue:\n";
        ss << "  Pending:         " << status.pending << "\n";
        ss << "  Running:         " << status.running << "\n";
        ss << "  Completed today: " << status.completed_today << "\n";
        ss << "  Failed today:    " << status.failed_today << "\n";

        json result = {
            {"pending", status.pending},
            {"running", status.running},
            {"completed_today", status.completed_today},
            {"failed_today", status.failed_today}
        };

        // Add subconscious stats if available
        if (subconscious_) {
            const auto& stats = subconscious_->stats();
            const auto& config = subconscious_->config();
            bool idle = subconscious_->is_idle();
            int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            int64_t idle_for = stats.last_query_at > 0 ? (now - stats.last_query_at) / 1000 : -1;

            ss << "\nEmbedding Scheduler:\n";
            ss << "  Enabled:          " << (config.enable_background_embedding ? "yes" : "no") << "\n";
            ss << "  Status:           " << (idle ? "IDLE (will embed)" : "BUSY (queries active)") << "\n";
            ss << "  Idle threshold:   " << config.idle_threshold.count() << "s\n";
            if (idle_for >= 0) {
                ss << "  Idle for:         " << idle_for << "s\n";
            }
            ss << "  Queue size:       " << subconscious_->embedding_queue_size() << "\n";
            ss << "  Embedded total:   " << stats.symbols_embedded.load() << "\n";
            ss << "  Skipped (busy):   " << stats.embedding_skips.load() << "\n";

            result["embedding"] = {
                {"enabled", config.enable_background_embedding},
                {"is_idle", idle},
                {"idle_threshold_s", config.idle_threshold.count()},
                {"idle_for_s", idle_for},
                {"queue_size", subconscious_->embedding_queue_size()},
                {"embedded_total", stats.symbols_embedded.load()},
                {"skipped_busy", stats.embedding_skips.load()}
            };
        }

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_background_run_cycle(const json& params) {
        size_t processed = mind_->store().background_run_cycle();

        std::ostringstream ss;
        if (processed == 0) {
            ss << "No pending background tasks to process.";
        } else {
            ss << "Processed " << processed << " background task(s).";
        }

        return DuckDBToolResult::ok(ss.str(), {{"processed", processed}});
    }

    DuckDBToolResult tool_profile_get(const json& params) {
        std::string user_id = params.value("user_id", "default");

        auto profile = mind_->store().profile_get(user_id);
        if (!profile) {
            return DuckDBToolResult::ok("No profile found for user: " + user_id, {{"found", false}});
        }

        std::ostringstream ss;
        ss << "User Profile: " << profile->user_id << "\n";
        ss << "  Expertise: " << profile->expertise_json << "\n";
        ss << "  Style: " << profile->style_json << "\n";
        ss << "  Patterns: " << profile->patterns_json << "\n";
        ss << "  Preferences: " << profile->preferences_json << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"found", true},
            {"user_id", profile->user_id},
            {"expertise", profile->expertise_json},
            {"style", profile->style_json},
            {"patterns", profile->patterns_json},
            {"preferences", profile->preferences_json},
            {"updated_at", profile->updated_at}
        });
    }

    DuckDBToolResult tool_profile_update(const json& params) {
        std::string user_id = params.value("user_id", "default");
        std::string field = params.value("field", "");
        std::string value = params.value("value", "");

        if (field.empty()) {
            return DuckDBToolResult::error("field is required");
        }
        if (value.empty()) {
            return DuckDBToolResult::error("value is required");
        }

        bool success = mind_->store().profile_update(user_id, field, value);
        if (!success) {
            return DuckDBToolResult::error("Failed to update profile: " + mind_->store().last_error());
        }

        return DuckDBToolResult::ok("Updated " + field + " for user " + user_id, {
            {"user_id", user_id},
            {"field", field},
            {"success", true}
        });
    }

    DuckDBToolResult tool_profile_observe(const json& params) {
        std::string observation_type = params.value("observation_type", "");
        std::string value = params.value("value", "");
        std::string user_id = params.value("user_id", "default");

        if (observation_type.empty()) {
            return DuckDBToolResult::error("observation_type is required");
        }
        if (value.empty()) {
            return DuckDBToolResult::error("value is required");
        }

        bool success = mind_->store().profile_observe(observation_type, value, user_id);
        if (!success) {
            return DuckDBToolResult::error("Failed to observe: " + mind_->store().last_error());
        }

        return DuckDBToolResult::ok("Observed " + observation_type + ": " + value.substr(0, 50), {
            {"user_id", user_id},
            {"observation_type", observation_type},
            {"success", true}
        });
    }

    DuckDBToolResult tool_goal_set(const json& params) {
        std::string title = params.value("title", "");
        if (title.empty()) {
            return DuckDBToolResult::error("title is required");
        }

        std::string description = params.value("description", "");
        std::string milestones = params.value("milestones", "[]");
        int64_t deadline = params.value("deadline", 0);
        std::string realm = params.value("realm", "brahman");

        int64_t id = mind_->store().goal_set(title, description, milestones, deadline, realm);
        if (id < 0) {
            return DuckDBToolResult::error("Failed to create goal");
        }

        return DuckDBToolResult::ok("Goal created: #" + std::to_string(id) + " " + title, {
            {"id", id},
            {"title", title}
        });
    }

    DuckDBToolResult tool_goal_get(const json& params) {
        int64_t id = params.value("id", 0);
        if (id == 0) {
            return DuckDBToolResult::error("id is required");
        }

        auto goal = mind_->store().goal_get(id);
        if (!goal) {
            return DuckDBToolResult::ok("Goal not found: #" + std::to_string(id), {{"found", false}});
        }

        std::ostringstream ss;
        ss << "Goal #" << goal->id << ": " << goal->title << "\n";
        ss << "  Status: " << goal->status << " (" << (int)(goal->progress * 100) << "%)\n";
        if (!goal->description.empty()) {
            ss << "  Description: " << goal->description.substr(0, 100) << "\n";
        }
        ss << "  Milestones: " << goal->milestones_json << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"found", true},
            {"id", goal->id},
            {"title", goal->title},
            {"description", goal->description},
            {"status", goal->status},
            {"progress", goal->progress},
            {"milestones", goal->milestones_json},
            {"deadline", goal->deadline},
            {"realm", goal->realm}
        });
    }

    DuckDBToolResult tool_goal_list(const json& params) {
        std::string status = params.value("status", "active");
        std::string realm = params.value("realm", "");
        size_t limit = params.value("limit", 20);
        std::string sort_by = params.value("sort_by", "updated_at");

        auto goals = mind_->store().goal_list(status, realm, limit);

        // Apply sorting
        if (sort_by == "progress") {
            std::sort(goals.begin(), goals.end(), [](const auto& a, const auto& b) {
                return a.progress > b.progress;
            });
        } else if (sort_by == "created_at") {
            std::sort(goals.begin(), goals.end(), [](const auto& a, const auto& b) {
                return a.created_at > b.created_at;
            });
        }
        // Default: updated_at (already sorted by store)

        if (goals.empty()) {
            return DuckDBToolResult::ok("No " + status + " goals", {{"count", 0}, {"goals", json::array()}});
        }

        std::ostringstream ss;
        ss << "Goals (" << status << "):\n";
        json goals_json = json::array();

        for (const auto& g : goals) {
            ss << "  #" << g.id << " [" << (int)(g.progress * 100) << "%] " << g.title << "\n";
            goals_json.push_back({
                {"id", g.id},
                {"title", g.title},
                {"progress", g.progress},
                {"status", g.status}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"count", goals.size()}, {"goals", goals_json}});
    }

    DuckDBToolResult tool_goal_progress(const json& params) {
        int64_t id = params.value("id", 0);
        if (id == 0) {
            return DuckDBToolResult::error("id is required");
        }

        float progress = params.value("progress", 0.0f);
        std::string milestone = params.value("milestone", "");

        bool success = mind_->store().goal_progress(id, progress, milestone);
        if (!success) {
            return DuckDBToolResult::error("Failed to update progress");
        }

        std::string msg = "Goal #" + std::to_string(id) + " progress: " + std::to_string((int)(progress * 100)) + "%";
        if (!milestone.empty()) {
            msg += " (completed: " + milestone + ")";
        }

        return DuckDBToolResult::ok(msg, {
            {"id", id},
            {"progress", progress},
            {"milestone_completed", milestone}
        });
    }

    DuckDBToolResult tool_goal_complete(const json& params) {
        int64_t id = params.value("id", 0);
        if (id == 0) {
            return DuckDBToolResult::error("id is required");
        }

        std::string outcome = params.value("outcome", "");
        if (outcome.empty()) {
            return DuckDBToolResult::error("outcome is required");
        }

        bool success = mind_->store().goal_complete(id, outcome);
        if (!success) {
            return DuckDBToolResult::error("Failed to complete goal");
        }

        return DuckDBToolResult::ok("Goal #" + std::to_string(id) + " completed: " + outcome.substr(0, 50), {
            {"id", id},
            {"outcome", outcome},
            {"status", "completed"}
        });
    }

    DuckDBToolResult tool_calibration_record(const json& params) {
        std::string domain = params.value("domain", "");
        if (domain.empty()) {
            return DuckDBToolResult::error("domain is required");
        }

        bool success = params.value("success", false);

        bool recorded = mind_->store().calibration_record(domain, success);
        if (!recorded) {
            return DuckDBToolResult::error("Failed to record calibration");
        }

        std::string msg = "Recorded " + std::string(success ? "success" : "failure") + " for domain: " + domain;

        // Get updated score
        auto score = mind_->store().calibration_get(domain);
        if (score) {
            msg += " (accuracy: " + std::to_string((int)(score->accuracy * 100)) + "%)";
        }

        return DuckDBToolResult::ok(msg, {
            {"domain", domain},
            {"success", success},
            {"recorded", true}
        });
    }

    DuckDBToolResult tool_calibration_score(const json& params) {
        std::string domain = params.value("domain", "");

        if (!domain.empty()) {
            // Get specific domain
            auto score = mind_->store().calibration_get(domain);
            if (!score) {
                return DuckDBToolResult::ok("No calibration data for domain: " + domain, {
                    {"found", false},
                    {"domain", domain}
                });
            }

            std::ostringstream ss;
            ss << "Calibration for " << domain << ":\n"
               << "  Predictions: " << score->predictions << "\n"
               << "  Successes: " << score->successes << " (" << (int)(score->accuracy * 100) << "%)\n"
               << "  Failures: " << score->failures << "\n"
               << "  Confidence adjustment: " << std::showpos << std::fixed << std::setprecision(2)
               << score->confidence_adjustment;

            return DuckDBToolResult::ok(ss.str(), {
                {"found", true},
                {"domain", score->domain},
                {"predictions", score->predictions},
                {"successes", score->successes},
                {"failures", score->failures},
                {"accuracy", score->accuracy},
                {"confidence_adjustment", score->confidence_adjustment}
            });
        }

        // Get all domains
        auto scores = mind_->store().calibration_all();
        if (scores.empty()) {
            return DuckDBToolResult::ok("No calibration data yet", {{"count", 0}});
        }

        std::ostringstream ss;
        ss << "Calibration scores:\n";
        json scores_json = json::array();

        for (const auto& s : scores) {
            ss << "  " << s.domain << ": " << (int)(s.accuracy * 100) << "% "
               << "(" << s.successes << "/" << s.predictions << ")";
            if (s.confidence_adjustment != 0.0f) {
                ss << " [adj: " << std::showpos << std::fixed << std::setprecision(2)
                   << s.confidence_adjustment << "]";
            }
            ss << "\n";

            scores_json.push_back({
                {"domain", s.domain},
                {"accuracy", s.accuracy},
                {"predictions", s.predictions},
                {"adjustment", s.confidence_adjustment}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"count", scores.size()}, {"scores", scores_json}});
    }

    DuckDBToolResult tool_hygiene_stats(const json&) {
        auto stats = mind_->store().hygiene_stats();

        std::ostringstream ss;
        ss << "Memory Hygiene Stats:\n"
           << "  Total: " << stats.total_memories << " memories\n"
           << "  Confidence: " << stats.high_confidence << " high, "
           << stats.medium_confidence << " medium, "
           << stats.low_confidence << " low\n"
           << "  Avg confidence: " << std::fixed << std::setprecision(2) << stats.avg_confidence << "\n"
           << "  Stale (30+ days): " << stats.old_unaccessed << "\n"
           << "  Growth rate: " << std::setprecision(1) << stats.growth_rate_per_day << "/day\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"total", stats.total_memories},
            {"high_confidence", stats.high_confidence},
            {"medium_confidence", stats.medium_confidence},
            {"low_confidence", stats.low_confidence},
            {"avg_confidence", stats.avg_confidence},
            {"stale", stats.old_unaccessed},
            {"growth_rate", stats.growth_rate_per_day}
        });
    }

    DuckDBToolResult tool_hygiene_run(const json& params) {
        float prune_threshold = params.value("prune_threshold", 0.1f);
        float min_age_days = params.value("min_age_days", 7.0f);
        float consolidation_threshold = params.value("consolidation_threshold", 0.85f);
        size_t max_consolidations = params.value("max_consolidations", 10);

        auto result = mind_->store().hygiene_run(prune_threshold, min_age_days,
                                                  consolidation_threshold, max_consolidations);

        std::ostringstream ss;
        ss << "Hygiene run complete:\n"
           << "  Decayed: " << result.decayed << " memories\n"
           << "  Pruned: " << result.pruned << " memories\n"
           << "  Consolidated: " << result.consolidated << " pairs\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"decayed", result.decayed},
            {"pruned", result.pruned},
            {"consolidated", result.consolidated}
        });
    }

    DuckDBToolResult tool_rebuild_fts_index(const json& params) {
        bool success = mind_->store().rebuild_fts_index();

        if (success) {
            return DuckDBToolResult::ok("FTS index rebuilt successfully. Keyword search should now work.", {
                {"success", true}
            });
        } else {
            return DuckDBToolResult::error("Failed to rebuild FTS index. FTS extension may not be available.");
        }
    }

    DuckDBToolResult tool_sql_query(const json& params) {
        std::string query = params.value("query", "");
        size_t limit = params.value("limit", 20);

        if (query.empty()) {
            return DuckDBToolResult::error("Query is required");
        }

        // Safety: only allow SELECT queries
        std::string upper_query = query;
        std::transform(upper_query.begin(), upper_query.end(), upper_query.begin(), ::toupper);
        if (upper_query.find("SELECT") != 0 && upper_query.find("WITH") != 0 &&
            upper_query.find("SHOW") != 0 && upper_query.find("DESCRIBE") != 0) {
            return DuckDBToolResult::error("Only SELECT/WITH/SHOW/DESCRIBE queries allowed");
        }

        // Add LIMIT if not present
        if (upper_query.find("LIMIT") == std::string::npos) {
            query += " LIMIT " + std::to_string(limit);
        }

        auto result = mind_->store().execute_sql_query(query);
        if (!result.success) {
            return DuckDBToolResult::error("SQL error: " + result.error);
        }

        if (result.rows.empty()) {
            return DuckDBToolResult::ok("Query returned 0 rows", {{"rows", json::array()}, {"count", 0}});
        }

        std::ostringstream ss;
        json rows_json = json::array();

        // Format as table header
        ss << "| ";
        for (const auto& col : result.columns) {
            ss << col << " | ";
        }
        ss << "\n|";
        for (size_t i = 0; i < result.columns.size(); ++i) {
            ss << "---|";
        }
        ss << "\n";

        // Format rows
        size_t displayed = 0;
        for (const auto& row : result.rows) {
            if (displayed >= limit) break;
            json row_obj;
            ss << "| ";
            for (size_t col = 0; col < row.size() && col < result.columns.size(); ++col) {
                row_obj[result.columns[col]] = row[col];
                ss << row[col] << " | ";
            }
            ss << "\n";
            rows_json.push_back(row_obj);
            ++displayed;
        }

        ss << "\n" << displayed << " row(s)";

        return DuckDBToolResult::ok(ss.str(), {{"rows", rows_json}, {"count", displayed}});
    }
