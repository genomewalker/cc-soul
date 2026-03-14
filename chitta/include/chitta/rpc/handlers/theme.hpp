// Included into DuckDBHandler class body — not a standalone header

    DuckDBToolResult tool_theme_list(const json& params) {
        std::string realm = params.value("realm", "");
        size_t limit = params.value("limit", 20);

        auto themes = mind_->store().theme_list(realm, limit);

        std::ostringstream ss;
        ss << "Themes (" << themes.size() << "):\n";

        json theme_array = json::array();
        for (const auto& t : themes) {
            ss << "  [" << t.id << "] " << t.name
               << " (" << t.memory_count << " memories, coherence="
               << std::fixed << std::setprecision(2) << t.coherence << ")\n";

            theme_array.push_back({
                {"id", t.id},
                {"name", t.name},
                {"memory_count", t.memory_count},
                {"coherence", t.coherence},
                {"sparsity", t.sparsity},
                {"realm", t.realm}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"themes", theme_array}});
    }

    DuckDBToolResult tool_theme_get(const json& params) {
        int64_t theme_id = params.value("id", 0);
        if (theme_id == 0) {
            return DuckDBToolResult::error("theme id required");
        }

        auto theme = mind_->store().theme_get(theme_id);
        if (!theme) {
            return DuckDBToolResult::error("Theme not found");
        }

        // Get representatives
        auto reps = mind_->store().theme_representatives(theme_id, 5);

        std::ostringstream ss;
        ss << "Theme: " << theme->name << " (id=" << theme->id << ")\n"
           << "  Memories: " << theme->memory_count << "\n"
           << "  Coherence: " << std::fixed << std::setprecision(2) << theme->coherence << "\n"
           << "  Realm: " << theme->realm << "\n"
           << "  Representatives (" << reps.size() << "):\n";

        json rep_array = json::array();
        for (const auto& r : reps) {
            std::string preview = r.content.substr(0, 100);
            if (r.content.size() > 100) preview += "...";
            ss << "    [" << r.id << "] " << preview << "\n";
            rep_array.push_back({
                {"id", r.id},
                {"content", r.content},
                {"kind", r.kind},
                {"confidence", r.confidence}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"id", theme->id},
            {"name", theme->name},
            {"memory_count", theme->memory_count},
            {"coherence", theme->coherence},
            {"sparsity", theme->sparsity},
            {"realm", theme->realm},
            {"representatives", rep_array}
        });
    }

    DuckDBToolResult tool_theme_recall(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) {
            return DuckDBToolResult::error("query required");
        }

        size_t limit = params.value("limit", 10);
        std::string realm = params.value("realm", "");

        auto recalls = mind_->theme_recall(query, limit, realm);

        std::ostringstream ss;
        ss << "Theme Recall (" << recalls.size() << " results):\n\n";

        json result_array = json::array();
        for (const auto& r : recalls) {
            std::string preview = r.text.substr(0, 150);
            if (r.text.size() > 150) preview += "...";

            ss << "[" << std::fixed << std::setprecision(2) << r.relevance << "] "
               << preview << "\n\n";

            json result_obj;
            result_obj["id"] = r.id.low;
            result_obj["content"] = r.text;
            result_obj["relevance"] = r.relevance;
            result_obj["similarity"] = r.similarity;
            result_obj["confidence"] = r.confidence.mu;
            result_array.push_back(result_obj);
        }

        return DuckDBToolResult::ok(ss.str(), {{"results", result_array}});
    }

    DuckDBToolResult tool_theme_stats(const json& params) {
        std::string realm = params.value("realm", "");

        auto stats = mind_->store().theme_stats(realm);

        std::ostringstream ss;
        ss << "Theme Organization Stats:\n"
           << "  Total themes: " << stats.total_themes << "\n"
           << "  Total memberships: " << stats.total_memberships << "\n"
           << "  Orphan memories: " << stats.orphan_memories << "\n"
           << "  Avg theme size: " << std::fixed << std::setprecision(1) << stats.avg_theme_size << "\n"
           << "  Avg coherence: " << std::setprecision(2) << stats.avg_coherence << "\n"
           << "  Size variance: " << std::setprecision(1) << stats.size_variance << "\n"
           << "  Undersized themes (<3): " << stats.undersized_themes << "\n"
           << "  Oversized themes (>100): " << stats.oversized_themes << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"total_themes", stats.total_themes},
            {"total_memberships", stats.total_memberships},
            {"orphan_memories", stats.orphan_memories},
            {"avg_theme_size", stats.avg_theme_size},
            {"avg_coherence", stats.avg_coherence},
            {"size_variance", stats.size_variance},
            {"undersized_themes", stats.undersized_themes},
            {"oversized_themes", stats.oversized_themes}
        });
    }

    DuckDBToolResult tool_theme_maintain(const json& params) {
        std::string realm = params.value("realm", "");

        auto result = mind_->run_theme_maintenance(realm);

        std::ostringstream ss;
        ss << "Theme Maintenance Complete:\n"
           << "  Themes split: " << result.themes_split << "\n"
           << "  Themes merged: " << result.themes_merged << "\n"
           << "  Memories reassigned: " << result.memories_reassigned << "\n"
           << "  Representatives updated: " << result.representatives_updated << "\n"
           << "  Centroids recomputed: " << result.centroids_recomputed << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"themes_split", result.themes_split},
            {"themes_merged", result.themes_merged},
            {"memories_reassigned", result.memories_reassigned},
            {"representatives_updated", result.representatives_updated},
            {"centroids_recomputed", result.centroids_recomputed}
        });
    }

    DuckDBToolResult tool_theme_assign_orphans(const json& params) {
        size_t batch_size = params.value("batch_size", 100);
        std::string realm = params.value("realm", "");

        auto* theme_mgr = mind_->theme_manager();
        if (!theme_mgr) {
            return DuckDBToolResult::error("ThemeManager not initialized");
        }

        size_t assigned = theme_mgr->assign_orphans(batch_size, realm);
        size_t remaining = theme_mgr->orphan_count(realm);
        size_t theme_count = mind_->store().theme_list(realm).size();

        std::ostringstream ss;
        ss << "Assigned " << assigned << " orphan memories to themes\n"
           << "Remaining orphans: " << remaining << "\n"
           << "Total themes: " << theme_count;

        return DuckDBToolResult::ok(ss.str(), {
            {"assigned", assigned},
            {"remaining_orphans", remaining},
            {"theme_count", theme_count}
        });
    }
