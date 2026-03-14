// Included into DuckDBHandler class body — not a standalone header

    DuckDBToolResult tool_enrichment_status(const json&) {
        size_t total_symbols = mind_->store().count_total_symbols();
        size_t pending = mind_->store().count_undescribed_symbols();
        size_t described_symbols = total_symbols - pending;
        float coverage = total_symbols > 0 ? (float)described_symbols / total_symbols * 100.0f : 0.0f;

        std::ostringstream ss;
        ss << "Code Enrichment Status:\n";
        ss << "  Total symbols: " << total_symbols << "\n";
        ss << "  Described: " << described_symbols << "\n";
        ss << "  Pending: " << pending << "\n";
        ss << "  Coverage: " << std::fixed << std::setprecision(1) << coverage << "%\n";

        if (pending > 0 && described_symbols > 0) {
            // Dynamic estimate based on current rate (symbols per minute)
            // Assume ~20 symbols per minute at batch=20, interval=1m
            size_t rate_per_min = 20;  // Conservative estimate
            size_t minutes_remaining = pending / rate_per_min;
            size_t hours = minutes_remaining / 60;
            size_t mins = minutes_remaining % 60;
            ss << "  Est. remaining: ~" << hours << "h " << mins << "m (at " << rate_per_min << "/min)\n";
        } else if (pending > 0) {
            ss << "  Est. remaining: calculating...\n";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"total_symbols", total_symbols},
            {"described", described_symbols},
            {"pending", pending},
            {"coverage_percent", coverage}
        });
    }

    DuckDBToolResult tool_describe_symbol(const json& params) {
        int64_t symbol_id = params.value("symbol_id", static_cast<int64_t>(0));
        std::string description = params.value("description", "");

        if (symbol_id == 0) {
            return DuckDBToolResult::error("symbol_id is required");
        }
        if (description.empty()) {
            return DuckDBToolResult::error("description is required");
        }

        bool success = mind_->store().set_symbol_description(symbol_id, description);
        if (!success) {
            return DuckDBToolResult::error("Failed to set symbol description");
        }

        return DuckDBToolResult::ok("Symbol description set", {
            {"symbol_id", symbol_id},
            {"description_length", description.size()}
        });
    }

    DuckDBToolResult tool_cleanup_code_wisdom(const json& params) {
        // Migration tool: delete [code] wisdom memories and clear orphaned symbol.memory_id
        // Accept both hyphen and underscore conventions
        bool dry_run = params.value("dry_run", params.value("dry-run", true));

        // Count what would be deleted
        auto count_result = mind_->store().raw_query(
            "SELECT COUNT(*) FROM memory WHERE kind = 'wisdom' AND content LIKE '[code]%'");
        size_t wisdom_count = 0;
        if (count_result && !count_result->HasError()) {
            auto chunk = count_result->Fetch();
            if (chunk && chunk->size() > 0) {
                wisdom_count = chunk->GetValue(0, 0).GetValue<int64_t>();
            }
        }

        // Count orphaned memory_id references
        auto orphan_result = mind_->store().raw_query(
            "SELECT COUNT(*) FROM symbol WHERE memory_id IS NOT NULL AND memory_id NOT IN (SELECT id FROM memory)");
        size_t orphan_count = 0;
        if (orphan_result && !orphan_result->HasError()) {
            auto chunk = orphan_result->Fetch();
            if (chunk && chunk->size() > 0) {
                orphan_count = chunk->GetValue(0, 0).GetValue<int64_t>();
            }
        }

        if (dry_run) {
            std::ostringstream ss;
            ss << "Cleanup preview (dry_run=true):\n";
            ss << "  [code] wisdom memories to delete: " << wisdom_count << "\n";
            ss << "  Orphaned symbol.memory_id to clear: " << orphan_count << "\n";
            ss << "\nRun with dry_run=false to execute.";
            return DuckDBToolResult::ok(ss.str(), {
                {"dry_run", true},
                {"wisdom_to_delete", wisdom_count},
                {"orphans_to_clear", orphan_count}
            });
        }

        // Execute cleanup
        bool ok1 = mind_->store().execute_raw(
            "DELETE FROM memory WHERE kind = 'wisdom' AND content LIKE '[code]%'");
        bool ok2 = mind_->store().execute_raw(
            "UPDATE symbol SET memory_id = NULL WHERE memory_id IS NOT NULL AND memory_id NOT IN (SELECT id FROM memory)");

        if (!ok1 || !ok2) {
            return DuckDBToolResult::error("Cleanup failed");
        }

        std::ostringstream ss;
        ss << "Cleanup complete:\n";
        ss << "  [code] wisdom memories deleted: " << wisdom_count << "\n";
        ss << "  Orphaned symbol.memory_id cleared: " << orphan_count << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"dry_run", false},
            {"wisdom_deleted", wisdom_count},
            {"orphans_cleared", orphan_count}
        });
    }

    DuckDBToolResult tool_reembed_memories(const json& params) {
        if (!mind_->has_yantra()) {
            return DuckDBToolResult::error("Yantra (embedder) not attached");
        }

        std::string kind_filter = params.value("kind", "");
        float min_confidence = params.value("min_confidence", 0.0f);
        int limit = params.value("limit", 30);
        bool dry_run = params.value("dry_run", false);
        bool all = params.value("all", false);  // Re-embed ALL memories, not just global

        std::vector<std::pair<int64_t, std::string>> to_reembed;
        size_t total_checked = 0;

        if (all) {
            // Query ALL memories with NULL embeddings directly
            std::ostringstream sql;
            sql << "SELECT id, COALESCE(content, '') FROM memory WHERE embedding IS NULL";
            if (!kind_filter.empty()) {
                std::string escaped = kind_filter;
                for (size_t pos = 0; (pos = escaped.find('\'', pos)) != std::string::npos; pos += 2) {
                    escaped.replace(pos, 1, "''");
                }
                sql << " AND kind = '" << escaped << "'";
            }
            if (min_confidence > 0) {
                sql << " AND confidence >= " << min_confidence;
            }
            sql << " ORDER BY confidence DESC LIMIT " << limit;

            auto result = mind_->store().raw_query(sql.str());
            if (result) {
                while (auto chunk = result->Fetch()) {
                    for (size_t i = 0; i < chunk->size(); ++i) {
                        int64_t id = chunk->GetValue(0, i).GetValue<int64_t>();
                        std::string content = chunk->GetValue(1, i).ToString();
                        if (!content.empty()) {
                            to_reembed.push_back({id, content});
                        }
                        total_checked++;
                    }
                }
            }
        } else {
            // Original behavior: only global memories
            auto memories = mind_->store().list_global_memories(limit, kind_filter);
            total_checked = memories.size();

            for (const auto& mem : memories) {
                if (min_confidence > 0 && mem.confidence < min_confidence) continue;

                // Check if memory has meaningful embedding by recalling it
                auto recalls = mind_->store().recall(
                    mind_->embedder().transform(mem.content).nu.data,
                    5, "", true
                );

                bool found_self = false;
                for (const auto& r : recalls) {
                    if (r.id == mem.id && r.similarity > 0.9f) {
                        found_self = true;
                        break;
                    }
                }

                if (!found_self) {
                    to_reembed.push_back({mem.id, mem.content});
                }
            }
        }

        size_t zero_embed_count = to_reembed.size();

        if (dry_run) {
            std::ostringstream ss;
            ss << "Dry run - found " << zero_embed_count << " memories likely needing re-embedding out of "
               << total_checked << " checked.\n";
            if (!to_reembed.empty()) {
                ss << "\nWould re-embed:\n";
                for (size_t i = 0; i < std::min(to_reembed.size(), size_t(10)); ++i) {
                    ss << "  #" << to_reembed[i].first << ": "
                       << to_reembed[i].second.substr(0, 60) << "...\n";
                }
                if (to_reembed.size() > 10) {
                    ss << "  ... and " << (to_reembed.size() - 10) << " more\n";
                }
            }
            return DuckDBToolResult::ok(ss.str(), {
                {"dry_run", true},
                {"total_checked", total_checked},
                {"zero_embed_count", zero_embed_count}
            });
        }

        // Actually re-embed
        size_t reembedded = 0;
        size_t failed = 0;

        for (const auto& [id, content] : to_reembed) {
            try {
                // Generate new embedding
                Artha artha = mind_->embedder().transform(content);

                // Update the memory with new embedding
                if (mind_->store().set_memory_embedding(id, artha.nu.data)) {
                    reembedded++;
                } else {
                    failed++;
                }
            } catch (...) {
                failed++;
            }
        }

        std::ostringstream ss;
        ss << "Re-embedded " << reembedded << " memories";
        if (failed > 0) {
            ss << " (" << failed << " failed)";
        }
        ss << " out of " << zero_embed_count << " needing re-embedding.";

        return DuckDBToolResult::ok(ss.str(), {
            {"total_checked", total_checked},
            {"zero_embed_count", zero_embed_count},
            {"reembedded", reembedded},
            {"failed", failed}
        });
    }

    DuckDBToolResult tool_embed_symbols(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        if (!mind_->has_yantra()) {
            return DuckDBToolResult::error("Yantra (embedder) not attached");
        }

        // Reset all embeddings if requested (for re-embedding with richer text)
        bool reset = params.value("reset", false);
        size_t purged = 0;
        if (reset) {
            // Clear all from separate embeddings DB
            purged = mind_->store().clear_symbol_embeddings();
            // Clear main DB embeddings and described_at
            mind_->store().execute_raw(
                "UPDATE symbol SET embedding = NULL, described_at = 0 "
                "WHERE embedding IS NOT NULL OR described_at > 0");
        }

        size_t batch_size = params.value("batch_size", 100);
        auto symbols = mind_->store().get_unembedded_symbols(batch_size);

        if (symbols.empty()) {
            return DuckDBToolResult::ok("All symbols embedded", {{"embedded", 0}, {"remaining", 0}});
        }

        size_t embedded = 0;
        auto start = std::chrono::steady_clock::now();

        // Pre-fetch triplet members for classes/structs (contains predicate)
        // to enrich embedding text with member names
        std::unordered_map<std::string, std::vector<std::string>> class_members;
        {
            auto members_result = mind_->store().execute_sql_query(
                "SELECT subject, object FROM triplet WHERE predicate = 'contains' "
                "AND subject IN (SELECT DISTINCT subject FROM triplet WHERE predicate = 'contains')");
            if (members_result.success) {
                for (const auto& row : members_result.rows) {
                    if (row.size() >= 2) {
                        // Extract leaf name from object for cleaner text
                        std::string leaf = row[1];
                        size_t cpos = leaf.rfind(':');
                        if (cpos != std::string::npos && cpos + 1 < leaf.size())
                            leaf = leaf.substr(cpos + 1);
                        class_members[row[0]].push_back(leaf);
                    }
                }
            }
        }

        for (const auto& sym : symbols) {
            // Build rich searchable text from metadata + context
            std::string disp = display_path(sym.file_path);

            std::ostringstream text;
            text << sym.kind << " " << sym.name;
            text << " in " << disp;

            if (!sym.signature.empty() && sym.signature != sym.name) {
                text << ": " << sym.signature;
            }

            // For classes/structs, append member names for richer semantics
            if (sym.kind == "class" || sym.kind == "struct" || sym.kind == "interface") {
                // Build lowercase key to match triplet subjects (connect_batch lowercases)
                std::string lower_name;
                for (char c : sym.name) {
                    lower_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                // Try both the raw name and common triplet key patterns
                for (const auto& key : {lower_name, sym.kind + ":" + lower_name}) {
                    auto it = class_members.find(key);
                    if (it != class_members.end() && !it->second.empty()) {
                        text << " { ";
                        size_t count = 0;
                        for (const auto& member : it->second) {
                            if (count++ > 0) text << ", ";
                            if (count > 8) { text << "..."; break; }  // Cap at 8 members
                            text << member;
                        }
                        text << " }";
                        break;
                    }
                }
            }

            // Embed using Yantra
            auto artha = mind_->embedder().transform(text.str());
            if (!artha.nu.is_zero()) {
                if (mind_->store().set_symbol_embedding(sym.id, artha.nu.data)) {
                    embedded++;
                }
            }
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        float rate = ms > 0 ? (float)embedded * 1000.0f / ms : 0;

        size_t remaining = mind_->store().count_unembedded_symbols();

        std::ostringstream ss;
        if (purged > 0) {
            ss << "Purged " << purged << " zero-vector embeddings\n";
        }
        ss << "Embedded " << embedded << " symbols in " << ms << "ms";
        ss << " (" << std::fixed << std::setprecision(1) << rate << "/sec)\n";
        ss << "Remaining: " << remaining;

        json result = {
            {"embedded", embedded},
            {"remaining", remaining},
            {"elapsed_ms", ms},
            {"rate_per_sec", rate}
        };
        if (purged > 0) result["purged_zeros"] = purged;
        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_dedupe_symbols(const json& /*params*/) {
        // Delete duplicate symbols, keeping the one with lowest id
        size_t before = mind_->store().count_total_symbols();

        // Use a subquery to find duplicates and delete them
        std::string sql = R"(
            DELETE FROM symbol
            WHERE id NOT IN (
                SELECT MIN(id) FROM symbol
                GROUP BY kind, name, file_path, line_start
            )
        )";

        if (!mind_->store().execute_raw(sql)) {
            return DuckDBToolResult::error("Failed to dedupe symbols");
        }

        size_t after = mind_->store().count_total_symbols();
        size_t removed = before - after;

        // Clean orphaned embeddings left behind by deleted duplicates
        size_t orphans_cleaned = mind_->store().clean_orphaned_symbol_embeddings();

        std::ostringstream ss;
        ss << "Removed " << removed << " duplicate symbols\n";
        ss << "Before: " << before << ", After: " << after;
        if (orphans_cleaned > 0) {
            ss << "\nCleaned " << orphans_cleaned << " orphaned embeddings";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"before", before},
            {"after", after},
            {"removed", removed},
            {"orphaned_embeddings_cleaned", orphans_cleaned}
        });
    }

    DuckDBToolResult tool_migrate_vss(const json& /*params*/) {
        // Migrate embeddings from main DB to separate VSS database
        size_t migrated = mind_->store().migrate_embeddings_to_vss();

        std::ostringstream ss;
        ss << "Migrated " << migrated << " embeddings to VSS database\n";
        ss << "HNSW index is now isolated from main database";

        return DuckDBToolResult::ok(ss.str(), {
            {"migrated", migrated}
        });
    }

    DuckDBToolResult tool_extract_symbols(const json& params) {
        std::string path = params.value("path", "");
        if (path.empty()) {
            return DuckDBToolResult::error("Path is required");
        }

        if (!std::filesystem::exists(path)) {
            return DuckDBToolResult::error("Path does not exist: " + path);
        }

        CodeIntel intel;
        auto symbols = intel.extract_file(path);

        if (symbols.empty()) {
            std::string lang = intel.detect_language(path);
            if (lang.empty()) {
                return DuckDBToolResult::error("Unsupported file type");
            }
            return DuckDBToolResult::ok("No symbols found in " + path, {{"symbols", json::array()}});
        }

        std::ostringstream ss;
        ss << "Extracted " << symbols.size() << " symbols from " << path << ":\n";

        json symbols_json = json::array();
        for (const auto& sym : symbols) {
            ss << "  " << sym.kind << " " << sym.name << " @" << sym.line_start;
            if (!sym.parent.empty()) ss << " (in " << sym.parent << ")";
            ss << "\n";

            symbols_json.push_back({
                {"kind", sym.kind},
                {"name", sym.name},
                {"file", sym.file_path},
                {"line_start", sym.line_start},
                {"line_end", sym.line_end},
                {"parent", sym.parent}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"symbols", symbols_json}, {"count", symbols.size()}});
    }

    DuckDBToolResult tool_learn_codebase(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string path = params.value("path", "");
        if (path.empty()) {
            return DuckDBToolResult::error("Path is required");
        }

        if (!std::filesystem::exists(path)) {
            return DuckDBToolResult::error("Path does not exist: " + path);
        }

        std::string project = params.value("project", "");
        if (project.empty()) {
            project = std::filesystem::path(path).filename().string();
        }

        size_t max_files = params.value("max_files", 500);
        bool incremental = params.value("incremental", true);  // Default to incremental
        bool force = params.value("force", false);  // Force full re-index

        std::vector<std::string> exclude = {"node_modules", ".git", "build", "__pycache__", "venv", "target", ".venv"};
        if (params.contains("exclude") && params["exclude"].is_string()) {
            std::string exclude_str = params["exclude"].get<std::string>();
            std::istringstream iss(exclude_str);
            std::string dir;
            while (std::getline(iss, dir, ',')) {
                if (!dir.empty()) exclude.push_back(dir);
            }
        }

        CodeIntel intel;
        std::ostringstream ss;

        if (incremental && !force) {
            // Incremental: only process changed files
            auto inc_result = intel.extract_directory_incremental(
                mind_->store(), path, project, exclude, max_files);

            if (inc_result.files_processed == 0 && inc_result.files_skipped > 0) {
                ss << "Codebase up-to-date: " << project << "\n";
                ss << "  Files: " << inc_result.files_skipped << " (all current)\n";
                return DuckDBToolResult::ok(ss.str(), {
                    {"project", project},
                    {"path", path},
                    {"mode", "incremental"},
                    {"files_skipped", inc_result.files_skipped},
                    {"up_to_date", true}
                });
            }

            // Store new symbols and callsites
            size_t symbols_stored = 0, callsites_stored = 0;
            size_t symbols_embedded = 0;
            if (!inc_result.extracted.symbols.empty() || !inc_result.extracted.callsites.empty()) {
                auto [s, c] = intel.store_full(mind_->store(), inc_result.extracted);
                symbols_stored = s;
                callsites_stored = c;

                // Pre-embed symbols if yantra available (move embedding cost to index time)
                if (mind_->has_yantra() && symbols_stored > 0) {
                    // Collect file paths from extracted symbols
                    std::unordered_set<std::string> files;
                    for (const auto& sym : inc_result.extracted.symbols) {
                        files.insert(sym.file_path);
                    }
                    std::vector<std::string> file_list(files.begin(), files.end());

                    // Get unembedded symbols for these files
                    auto unembedded = mind_->store().get_unembedded_symbols(100);  // Batch of 100

                    // Build embedding texts and embed in batch
                    std::vector<std::string> texts;
                    std::vector<int64_t> ids;
                    for (const auto& sym : unembedded) {
                        std::string text = sym.kind + " " + sym.name;
                        if (!sym.signature.empty()) text += " " + sym.signature;
                        texts.push_back(text);
                        ids.push_back(sym.id);
                    }

                    if (!texts.empty()) {
                        auto embeddings = mind_->embedder().embed_batch(texts);
                        for (size_t i = 0; i < embeddings.size(); ++i) {
                            if (!embeddings[i].is_zero()) {
                                mind_->store().set_symbol_embedding(ids[i], embeddings[i].data);
                                symbols_embedded++;
                            }
                        }
                    }
                }
            }

            ss << "Learned codebase (incremental): " << project << "\n";
            ss << "  Path: " << path << "\n";
            ss << "  Files processed: " << inc_result.files_processed << "\n";
            ss << "  Files skipped (up-to-date): " << inc_result.files_skipped << "\n";
            ss << "  Symbols added: " << symbols_stored << "\n";
            ss << "  Symbols embedded: " << symbols_embedded << "\n";
            ss << "  Callsites added: " << callsites_stored << "\n";
            if (inc_result.symbols_deleted > 0 || inc_result.triplets_deleted > 0) {
                ss << "  Old data cleaned: " << inc_result.symbols_deleted << " symbols, "
                   << inc_result.triplets_deleted << " triplets\n";
            }

            // Summary by kind
            std::unordered_map<std::string, size_t> by_kind;
            for (const auto& sym : inc_result.extracted.symbols) {
                by_kind[sym.kind]++;
            }
            if (!by_kind.empty()) {
                ss << "  Symbol breakdown:\n";
                for (const auto& [kind, count] : by_kind) {
                    ss << "    " << kind << ": " << count << "\n";
                }
            }

            return DuckDBToolResult::ok(ss.str(), {
                {"project", project},
                {"path", path},
                {"mode", "incremental"},
                {"files_processed", inc_result.files_processed},
                {"files_skipped", inc_result.files_skipped},
                {"symbols_stored", symbols_stored},
                {"symbols_embedded", symbols_embedded},
                {"callsites_stored", callsites_stored},
                {"symbols_deleted", inc_result.symbols_deleted},
                {"triplets_deleted", inc_result.triplets_deleted}
            });
        }

        // Full extraction (force or non-incremental)
        auto result = intel.extract_directory_full(path, exclude, max_files);

        if (result.symbols.empty()) {
            return DuckDBToolResult::ok("No symbols found in " + path, {{"stored", 0}});
        }

        // Store symbols and callsites in DuckDB
        auto [symbols_stored, callsites_stored] = intel.store_full(mind_->store(), result);

        // Pre-embed symbols if yantra available
        size_t symbols_embedded = 0;
        if (mind_->has_yantra() && symbols_stored > 0) {
            auto unembedded = mind_->store().get_unembedded_symbols(100);
            std::vector<std::string> texts;
            std::vector<int64_t> ids;
            for (const auto& sym : unembedded) {
                std::string text = sym.kind + " " + sym.name;
                if (!sym.signature.empty()) text += " " + sym.signature;
                texts.push_back(text);
                ids.push_back(sym.id);
            }
            if (!texts.empty()) {
                auto embeddings = mind_->embedder().embed_batch(texts);
                for (size_t i = 0; i < embeddings.size(); ++i) {
                    if (!embeddings[i].is_zero()) {
                        mind_->store().set_symbol_embedding(ids[i], embeddings[i].data);
                        symbols_embedded++;
                    }
                }
            }
        }

        // Clean orphaned embeddings after force re-index
        size_t orphans_cleaned = mind_->store().clean_orphaned_symbol_embeddings();

        // Create project triplet
        mind_->connect(project, "contains", std::to_string(symbols_stored) + "_symbols");

        ss << "Learned codebase: " << project << "\n";
        ss << "  Path: " << path << "\n";
        ss << "  Mode: " << (force ? "force" : "full") << "\n";
        ss << "  Symbols: " << symbols_stored << "\n";
        ss << "  Symbols embedded: " << symbols_embedded << "\n";
        ss << "  Callsites: " << callsites_stored << "\n";
        if (orphans_cleaned > 0) {
            ss << "  Orphaned embeddings cleaned: " << orphans_cleaned << "\n";
        }

        // Summary by kind
        std::unordered_map<std::string, size_t> by_kind;
        for (const auto& sym : result.symbols) {
            by_kind[sym.kind]++;
        }
        ss << "  Symbol breakdown:\n";
        for (const auto& [kind, count] : by_kind) {
            ss << "    " << kind << ": " << count << "\n";
        }

        // Callsite summary by kind
        std::unordered_map<std::string, size_t> callsites_by_kind;
        for (const auto& cs : result.callsites) {
            callsites_by_kind[call_kind_to_string(cs.kind)]++;
        }
        if (!callsites_by_kind.empty()) {
            ss << "  Callsite breakdown:\n";
            for (const auto& [kind, count] : callsites_by_kind) {
                ss << "    " << kind << ": " << count << "\n";
            }
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"project", project},
            {"path", path},
            {"mode", force ? "force" : "full"},
            {"symbols_stored", symbols_stored},
            {"symbols_embedded", symbols_embedded},
            {"callsites_stored", callsites_stored},
            {"symbols_by_kind", by_kind},
            {"callsites_by_kind", callsites_by_kind}
        });
    }

    DuckDBToolResult tool_find_symbol(const json& params) {
        // Notify subconscious that we're handling a query (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string name = params.value("name", "");
        if (name.empty()) {
            return DuckDBToolResult::error("Name is required");
        }

        std::string kind = params.value("kind", "");
        auto symbols = mind_->store().find_symbol(name, kind);

        if (symbols.empty()) {
            return DuckDBToolResult::ok("No symbols found matching '" + name + "'", {{"symbols", json::array()}});
        }

        std::ostringstream ss;
        ss << "Found " << symbols.size() << " symbols matching '" << name << "':\n";

        json symbols_json = json::array();
        for (const auto& sym : symbols) {
            ss << "  " << sym.kind << " " << sym.name << " @" << sym.file_path << ":" << sym.line_start << "\n";

            symbols_json.push_back({
                {"id", sym.id},
                {"kind", sym.kind},
                {"name", sym.name},
                {"file", sym.file_path},
                {"line_start", sym.line_start},
                {"line_end", sym.line_end},
                {"signature", sym.signature}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"symbols", symbols_json}, {"count", symbols.size()}});
    }

    DuckDBToolResult tool_symbol_callers(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        auto sym_opt = resolve_symbol(params);
        if (!sym_opt) {
            return DuckDBToolResult::error("Symbol not found. Provide 'name' or 'id'.");
        }
        const auto& sym = *sym_opt;

        auto caller_ids = mind_->store().callers(sym.id);

        if (caller_ids.empty()) {
            return DuckDBToolResult::ok(
                "No callers found for " + sym.kind + " " + sym.name,
                {{"symbol", sym.name}, {"callers", json::array()}}
            );
        }

        std::ostringstream ss;
        ss << "Found " << caller_ids.size() << " callers for " << sym.kind << " " << sym.name << ":\n";

        json callers_json = json::array();
        for (int64_t cid : caller_ids) {
            auto caller_opt = mind_->store().get_symbol_by_id(cid);
            if (caller_opt) {
                const auto& c = *caller_opt;
                ss << "  " << c.kind << " " << c.name << " @" << c.file_path << ":" << c.line_start << "\n";
                callers_json.push_back({
                    {"id", c.id},
                    {"kind", c.kind},
                    {"name", c.name},
                    {"file", c.file_path},
                    {"line_start", c.line_start},
                    {"line_end", c.line_end}
                });
            }
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"symbol", sym.name},
            {"symbol_id", sym.id},
            {"callers", callers_json},
            {"count", callers_json.size()}
        });
    }

    DuckDBToolResult tool_symbol_callees(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        auto sym_opt = resolve_symbol(params);
        if (!sym_opt) {
            return DuckDBToolResult::error("Symbol not found. Provide 'name' or 'id'.");
        }
        const auto& sym = *sym_opt;

        auto callee_ids = mind_->store().callees(sym.id);

        if (callee_ids.empty()) {
            return DuckDBToolResult::ok(
                "No callees found for " + sym.kind + " " + sym.name,
                {{"symbol", sym.name}, {"callees", json::array()}}
            );
        }

        std::ostringstream ss;
        ss << "Found " << callee_ids.size() << " callees for " << sym.kind << " " << sym.name << ":\n";

        json callees_json = json::array();
        for (int64_t cid : callee_ids) {
            auto callee_opt = mind_->store().get_symbol_by_id(cid);
            if (callee_opt) {
                const auto& c = *callee_opt;
                ss << "  " << c.kind << " " << c.name << " @" << c.file_path << ":" << c.line_start << "\n";
                callees_json.push_back({
                    {"id", c.id},
                    {"kind", c.kind},
                    {"name", c.name},
                    {"file", c.file_path},
                    {"line_start", c.line_start},
                    {"line_end", c.line_end}
                });
            }
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"symbol", sym.name},
            {"symbol_id", sym.id},
            {"callees", callees_json},
            {"count", callees_json.size()}
        });
    }

    DuckDBToolResult tool_read_symbol(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        auto sym_opt = resolve_symbol(params);
        if (!sym_opt) {
            return DuckDBToolResult::error("Symbol not found. Provide 'name' or 'id'.");
        }
        const auto& sym = *sym_opt;

        // Read the source file
        std::ifstream file(sym.file_path);
        if (!file) {
            return DuckDBToolResult::error("Cannot open file: " + sym.file_path);
        }

        // Read lines from line_start to line_end
        std::ostringstream source;
        std::string line;
        int line_num = 1;
        while (std::getline(file, line)) {
            if (line_num >= sym.line_start && line_num <= sym.line_end) {
                source << line << "\n";
            }
            if (line_num > sym.line_end) break;
            line_num++;
        }

        std::string code = source.str();
        if (code.empty()) {
            return DuckDBToolResult::error("No code found at " + sym.file_path + ":" +
                                          std::to_string(sym.line_start) + "-" +
                                          std::to_string(sym.line_end));
        }

        std::ostringstream ss;
        ss << sym.kind << " " << sym.name << " @" << sym.file_path << ":"
           << sym.line_start << "-" << sym.line_end << "\n\n" << code;

        return DuckDBToolResult::ok(ss.str(), {
            {"symbol", sym.name},
            {"kind", sym.kind},
            {"file", sym.file_path},
            {"line_start", sym.line_start},
            {"line_end", sym.line_end},
            {"code", code}
        });
    }

    DuckDBToolResult tool_read_function(const json& params) {
        // Shorthand for read_symbol with kind = function or method
        std::string name = params.value("name", "");
        if (name.empty()) {
            return DuckDBToolResult::error("Function name is required");
        }

        // Try function first, then method
        auto symbols = mind_->store().find_symbol(name, "function");
        if (symbols.empty()) {
            symbols = mind_->store().find_symbol(name, "method");
        }

        if (symbols.empty()) {
            return DuckDBToolResult::error("Function/method '" + name + "' not found");
        }

        // Find exact match
        const Symbol* best = nullptr;
        for (const auto& s : symbols) {
            if (s.name == name) {
                best = &s;
                break;
            }
        }
        if (!best) best = &symbols[0];

        // Read the source file
        std::ifstream file(best->file_path);
        if (!file) {
            return DuckDBToolResult::error("Cannot open file: " + best->file_path);
        }

        std::ostringstream source;
        std::string line;
        int line_num = 1;
        while (std::getline(file, line)) {
            if (line_num >= best->line_start && line_num <= best->line_end) {
                source << line << "\n";
            }
            if (line_num > best->line_end) break;
            line_num++;
        }

        std::string code = source.str();
        std::ostringstream ss;
        ss << best->kind << " " << best->name << " @" << best->file_path << ":"
           << best->line_start << "-" << best->line_end << "\n\n" << code;

        return DuckDBToolResult::ok(ss.str(), {
            {"symbol", best->name},
            {"kind", best->kind},
            {"file", best->file_path},
            {"line_start", best->line_start},
            {"line_end", best->line_end},
            {"code", code}
        });
    }

    DuckDBToolResult tool_search_symbols(const json& params) {
        // Notify subconscious that we're handling a query (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string query = params.value("query", "");
        if (query.empty()) {
            return DuckDBToolResult::error("Query is required");
        }

        std::string kind = params.value("kind", "");
        size_t limit = params.value("limit", 10);
        std::string mode = params.value("mode", "auto");  // auto, bm25, semantic
        std::string project = params.value("project", "");

        bool is_code_query = looks_like_code_query(query);
        bool use_bm25 = (mode == "bm25") || (mode == "auto" && is_code_query);
        bool use_semantic = (mode == "semantic") || (mode == "auto" && !is_code_query);

        // If semantic requested but no yantra, fall back to BM25
        if (use_semantic && !mind_->has_yantra()) {
            use_bm25 = true;
            use_semantic = false;
        }

        std::vector<DuckDBStore::SymbolMatch> semantic_matches;
        std::vector<Symbol> bm25_matches;
        std::string search_mode;

        // BM25 search (fast, ~50ms)
        if (use_bm25) {
            bm25_matches = mind_->store().bm25_search_symbols(query, limit, project);
            search_mode = "bm25";
        }

        // Semantic search (slow, ~2-5s on CPU)
        if (use_semantic && mind_->has_yantra()) {
            auto artha = mind_->embedder().transform_query(query);
            if (!artha.nu.is_zero()) {
                semantic_matches = mind_->store().search_symbols_by_embedding(artha.nu.data, limit, kind, project);
                search_mode = use_bm25 ? "hybrid" : "semantic";
            }
        }

        // Merge results: semantic first for NL queries, BM25 first for code queries
        json symbols_json = json::array();
        std::unordered_set<int64_t> seen_ids;
        std::ostringstream ss;

        auto add_symbol = [&](const Symbol& sym, float score, const std::string& source) {
            if (seen_ids.count(sym.id) || symbols_json.size() >= limit) return;
            seen_ids.insert(sym.id);

            std::string disp = display_path(sym.file_path);

            // Filter by kind if specified
            if (!kind.empty() && sym.kind != kind) return;

            if (score > 0) {
                ss << "  [" << std::fixed << std::setprecision(0) << (score * 100) << "%] ";
            } else {
                ss << "  ";
            }
            ss << sym.kind << " " << sym.name << " @" << disp << ":" << sym.line_start
               << " (" << source << ")\n";

            json sym_json = {
                {"id", sym.id},
                {"kind", sym.kind},
                {"name", sym.name},
                {"file", sym.file_path},
                {"line_start", sym.line_start},
                {"line_end", sym.line_end},
                {"signature", sym.signature},
                {"source", source}
            };
            if (score > 0) sym_json["score"] = score;
            symbols_json.push_back(sym_json);
        };

        // Order depends on query type
        if (is_code_query) {
            // Code query: BM25 first
            for (const auto& sym : bm25_matches) add_symbol(sym, 0, "bm25");
            for (const auto& m : semantic_matches) add_symbol(m.symbol, m.score, "semantic");
        } else {
            // NL query: semantic first
            for (const auto& m : semantic_matches) add_symbol(m.symbol, m.score, "semantic");
            for (const auto& sym : bm25_matches) add_symbol(sym, 0, "bm25");
        }

        if (symbols_json.empty()) {
            return DuckDBToolResult::ok("No symbols found for query: " + query,
                {{"symbols", json::array()}, {"mode", search_mode}});
        }

        std::ostringstream header;
        header << "Found " << symbols_json.size() << " symbols for '" << query
               << "' (" << search_mode << ", " << (is_code_query ? "code" : "NL") << " query):\n";

        return DuckDBToolResult::ok(header.str() + ss.str(),
            {{"symbols", symbols_json}, {"count", symbols_json.size()}, {"mode", search_mode}});
    }

    DuckDBToolResult tool_code_context(const json& params) {
        // Notify subconscious that we're handling a query (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string path = params.value("path", "");

        // Get symbol counts
        auto health = mind_->store().health();

        std::ostringstream ss;
        ss << "Code Context:\n";
        ss << "  Symbols: " << health.total_symbols << " indexed\n";

        json result;
        result["total_symbols"] = health.total_symbols;

        // If path specified, get file-specific info
        if (!path.empty() && std::filesystem::exists(path)) {
            CodeIntel intel;
            if (std::filesystem::is_regular_file(path)) {
                auto symbols = intel.extract_file(path);
                ss << "  File: " << path << " (" << symbols.size() << " symbols)\n";
                result["file_symbols"] = symbols.size();
            } else if (std::filesystem::is_directory(path)) {
                auto symbols = intel.extract_directory(path, {}, 50);
                ss << "  Directory: " << path << " (" << symbols.size() << " symbols in sample)\n";
                result["dir_symbols"] = symbols.size();
            }
        }

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_smart_context(const json& params) {
        // Notify subconscious that we're handling a query (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string task = params.value("task", "");
        if (task.empty()) {
            return DuckDBToolResult::error("task is required");
        }

        std::string mode = params.value("mode", "full");
        size_t token_limit = params.value("limit", 300);
        bool include_memories = params.value("memories", true);
        bool include_code = params.value("code", true);
        bool include_neighbors = params.value("neighbors", true);
        std::string realm = params.value("realm", "");

        bool fast = (mode == "fast");
        std::ostringstream ss;
        json result;

        // Performance budgets:
        // fast: 3 memories (vector), 3 symbols (BM25), 1 neighbor expansion
        // full: 5 memories (full_resonate), 5 symbols (semantic), 3 neighbor expansions
        size_t mem_limit = fast ? 3 : 5;
        size_t sym_limit = fast ? 3 : 5;
        size_t neighbor_limit = fast ? 1 : 3;

        // 1. MEMORIES
        std::vector<Recall> memories;
        json memories_json = json::array();

        if (include_memories) {
            if (fast) {
                // Fast: simple vector recall
                memories = mind_->recall(task, mem_limit);
            } else {
                // Full: full resonance with spreading activation
                memories = mind_->full_resonate(task, mem_limit);
            }

            ss << "[mem]\n";
            for (const auto& m : memories) {
                // Post-hoc realm filtering if specified
                if (!realm.empty()) {
                    auto realms = mind_->store().get_realms(static_cast<int64_t>(m.id.low));
                    bool in_realm = false;
                    for (const auto& rm : realms) {
                        if (rm == realm) { in_realm = true; break; }
                    }
                    // Also include global memories
                    auto mem = mind_->store().get_memory(static_cast<int64_t>(m.id.low));
                    if (mem && mem->visibility == RealmVisibility::Global) {
                        in_realm = true;
                    }
                    if (!in_realm) continue;
                }

                int pct = static_cast<int>(std::min(m.relevance, 1.0f) * 100);
                std::string type_name = node_type_name(m.type);
                // Short type code
                std::string type_short = type_name.substr(0, 3);
                if (type_name == "wisdom") type_short = "wis";
                else if (type_name == "belief") type_short = "bel";
                else if (type_name == "episode") type_short = "epi";

                // Extract title (first line or first ~60 chars)
                std::string title = m.text.substr(0, 60);
                size_t newline = title.find('\n');
                if (newline != std::string::npos) {
                    title = title.substr(0, newline);
                }

                ss << "[" << pct << "%:" << type_short << ":" << m.id.to_string().substr(0, 8)
                   << "] " << title << "\n";

                json mem_entry;
                mem_entry["id"] = m.id.to_string();
                mem_entry["relevance"] = m.relevance;
                mem_entry["type"] = type_name;
                mem_entry["text"] = m.text;
                memories_json.push_back(mem_entry);
            }
        }

        // 2. CODE SYMBOLS
        json symbols_json = json::array();

        if (include_code) {
            bool is_code_query = looks_like_code_query(task);

            if (fast || is_code_query) {
                // Fast mode or code-like query: BM25 search
                auto bm25_results = mind_->store().bm25_search_symbols(task, sym_limit);

                if (!bm25_results.empty()) {
                    ss << "\n[code]\n";
                    for (const auto& sym : bm25_results) {
                        ss << sym.file_path << ":" << sym.line_start
                           << " " << sym.kind << " " << sym.name << "\n";

                        json sym_entry;
                        sym_entry["name"] = sym.name;
                        sym_entry["kind"] = sym.kind;
                        sym_entry["file"] = sym.file_path;
                        sym_entry["line_start"] = sym.line_start;
                        sym_entry["line_end"] = sym.line_end;
                        symbols_json.push_back(sym_entry);
                    }
                }
            } else {
                // Full mode with natural language: semantic search
                if (mind_->has_yantra()) {
                    auto artha = mind_->embedder().transform_query(task);
                    if (!artha.nu.is_zero()) {
                        auto semantic_results = mind_->store().search_symbols_by_embedding(
                            artha.nu.data, sym_limit, "");

                        if (!semantic_results.empty()) {
                            ss << "\n[code]\n";
                            for (const auto& match : semantic_results) {
                                const auto& sym = match.symbol;
                                ss << sym.file_path << ":" << sym.line_start
                                   << " " << sym.kind << " " << sym.name
                                   << " (" << static_cast<int>(match.score * 100) << "%)\n";

                                json sym_entry;
                                sym_entry["name"] = sym.name;
                                sym_entry["kind"] = sym.kind;
                                sym_entry["file"] = sym.file_path;
                                sym_entry["line_start"] = sym.line_start;
                                sym_entry["similarity"] = match.score;
                                symbols_json.push_back(sym_entry);
                            }
                        }
                    }
                } else {
                    // Fallback to BM25 if no embedder
                    auto bm25_results = mind_->store().bm25_search_symbols(task, sym_limit);
                    if (!bm25_results.empty()) {
                        ss << "\n[code]\n";
                        for (const auto& sym : bm25_results) {
                            ss << sym.file_path << ":" << sym.line_start
                               << " " << sym.kind << " " << sym.name << "\n";
                            json sym_entry;
                            sym_entry["name"] = sym.name;
                            sym_entry["kind"] = sym.kind;
                            sym_entry["file"] = sym.file_path;
                            sym_entry["line_start"] = sym.line_start;
                            symbols_json.push_back(sym_entry);
                        }
                    }
                }
            }
        }

        // 3. TRIPLET NEIGHBORS (from top memory terms)
        json triplets_json = json::array();

        if (include_neighbors && !memories.empty()) {
            ss << "\n[graph]\n";
            std::set<std::string> seen_triplets;
            size_t processed = 0;

            for (const auto& m : memories) {
                if (processed >= neighbor_limit) break;

                // Extract key terms from memory content
                auto terms = extract_terms(m.text);
                if (terms.empty()) continue;

                // Query triplets for the first significant term
                for (const auto& term : terms) {
                    if (term.length() < 4) continue;  // Skip short terms

                    auto subj_triplets = mind_->store().query_subject(term);
                    auto obj_triplets = mind_->store().query_object(term);

                    for (const auto& t : subj_triplets) {
                        std::string key = t.subject + "→" + t.predicate + "→" + t.object;
                        if (seen_triplets.find(key) == seen_triplets.end()) {
                            seen_triplets.insert(key);
                            ss << t.subject << "→" << t.predicate << "→" << t.object << "\n";
                            json triplet_entry;
                            triplet_entry["subject"] = t.subject;
                            triplet_entry["predicate"] = t.predicate;
                            triplet_entry["object"] = t.object;
                            triplets_json.push_back(triplet_entry);
                        }
                        if (triplets_json.size() >= 5) break;  // Limit triplets
                    }

                    for (const auto& t : obj_triplets) {
                        std::string key = t.subject + "→" + t.predicate + "→" + t.object;
                        if (seen_triplets.find(key) == seen_triplets.end()) {
                            seen_triplets.insert(key);
                            ss << t.subject << "→" << t.predicate << "→" << t.object << "\n";
                            json triplet_entry;
                            triplet_entry["subject"] = t.subject;
                            triplet_entry["predicate"] = t.predicate;
                            triplet_entry["object"] = t.object;
                            triplets_json.push_back(triplet_entry);
                        }
                        if (triplets_json.size() >= 5) break;
                    }

                    if (triplets_json.size() >= 3) break;  // Found enough
                }

                processed++;
            }
        }

        // Build result
        result["memories"] = memories_json;
        result["symbols"] = symbols_json;
        result["triplets"] = triplets_json;
        result["mode"] = mode;
        result["task"] = task;

        std::string output = ss.str();
        if (output.empty()) {
            output = "No context found for: " + task;
        }

        return DuckDBToolResult::ok(output, result);
    }

    DuckDBToolResult tool_codebase_overview(const json& params) {
        std::string project = params.value("project", "");
        std::string format = params.value("format", "tree");
        bool include_callsites = params.value("include_callsites", false);

        std::ostringstream ss;
        json result;

        // Get all files for project
        auto files = mind_->store().list_project_files(project);

        if (files.empty()) {
            ss << "No indexed files";
            if (!project.empty()) ss << " for project: " << project;
            ss << "\nRun: learn_codebase --path /your/project --project " << (project.empty() ? "myproj" : project);
            return DuckDBToolResult::ok(ss.str(), {{"files", 0}});
        }

        // Summary
        size_t total_symbols = 0, total_callsites = 0;
        for (const auto& f : files) {
            total_symbols += f.symbols_count;
            total_callsites += f.callsites_count;
        }

        ss << "Codebase: " << (project.empty() ? "(all)" : project) << "\n";
        ss << "  Files: " << files.size() << "\n";
        ss << "  Symbols: " << total_symbols << "\n";
        ss << "  Callsites: " << total_callsites << "\n\n";

        // List files with counts
        ss << "Files:\n";
        for (const auto& f : files) {
            std::filesystem::path p(f.path);
            ss << "  " << p.filename().string() << " (" << f.symbols_count << " symbols";
            if (include_callsites) ss << ", " << f.callsites_count << " callsites";
            ss << ")\n";
        }

        // Get symbol breakdown by querying triplets
        auto contains_triplets = mind_->store().query_predicate("contains");
        std::unordered_map<std::string, std::vector<std::string>> file_symbols;

        for (const auto& t : contains_triplets) {
            // subject is file or class, object is symbol
            file_symbols[t.subject].push_back(t.object);
        }

        if (format == "tree" && !file_symbols.empty()) {
            ss << "\nStructure:\n";
            for (const auto& [parent, children] : file_symbols) {
                if (children.size() > 1) {  // Only show containers
                    ss << "  " << parent << ":\n";
                    for (const auto& child : children) {
                        if (child.find("callsite") == std::string::npos) {  // Skip callsites in tree
                            ss << "    - " << child << "\n";
                        }
                    }
                }
            }
        }

        result["files"] = files.size();
        result["symbols"] = total_symbols;
        result["callsites"] = total_callsites;
        result["project"] = project;

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_clear_codebase(const json& params) {
        std::string project = params.value("project", "");
        if (project.empty()) {
            return DuckDBToolResult::error("Project name is required");
        }

        bool dry_run = params.value("dry_run", false);

        // Get current stats for preview
        auto files = mind_->store().list_project_files(project);
        if (files.empty()) {
            return DuckDBToolResult::ok("No code intelligence data found for project: " + project,
                {{"project", project}, {"files", 0}});
        }

        if (dry_run) {
            // Count what would be deleted
            size_t total_symbols = 0, total_callsites = 0;
            for (const auto& f : files) {
                total_symbols += f.symbols_count;
                total_callsites += f.callsites_count;
            }

            std::ostringstream ss;
            ss << "Would clear codebase: " << project << "\n";
            ss << "  Files: " << files.size() << "\n";
            ss << "  Symbols: " << total_symbols << "\n";
            ss << "  Callsites: " << total_callsites << "\n";

            return DuckDBToolResult::ok(ss.str(), {
                {"project", project},
                {"dry_run", true},
                {"files", files.size()},
                {"symbols", total_symbols},
                {"callsites", total_callsites}
            });
        }

        // Actually clear
        auto result = mind_->store().clear_project_codebase(project);

        std::ostringstream ss;
        ss << "Cleared codebase: " << project << "\n";
        ss << "  Files deleted: " << result.files_deleted << "\n";
        ss << "  Symbols deleted: " << result.symbols_deleted << "\n";
        ss << "  Triplets deleted: " << result.triplets_deleted << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"project", project},
            {"files_deleted", result.files_deleted},
            {"symbols_deleted", result.symbols_deleted},
            {"triplets_deleted", result.triplets_deleted}
        });
    }

    DuckDBToolResult tool_clear_triplets(const json& params) {
        std::string pattern = params.value("pattern", "");
        if (pattern.empty()) {
            return DuckDBToolResult::error("Pattern is required (e.g., '%.cpp', '%.hpp')");
        }

        bool dry_run = params.value("dry_run", false);

        size_t count = mind_->store().count_triplets_by_pattern(pattern);

        if (count == 0) {
            return DuckDBToolResult::ok("No triplets match pattern: " + pattern,
                {{"pattern", pattern}, {"count", 0}});
        }

        if (dry_run) {
            std::ostringstream ss;
            ss << "Would delete " << count << " triplets matching: " << pattern;
            return DuckDBToolResult::ok(ss.str(), {
                {"pattern", pattern},
                {"dry_run", true},
                {"count", count}
            });
        }

        // Actually delete
        size_t deleted = mind_->store().delete_triplets_by_pattern(pattern);

        std::ostringstream ss;
        ss << "Deleted " << deleted << " triplets matching: " << pattern;
        return DuckDBToolResult::ok(ss.str(), {
            {"pattern", pattern},
            {"deleted", deleted}
        });
    }

    DuckDBToolResult tool_resolve_callsites(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        std::string project = params.value("project", "");

        SymbolResolver resolver(mind_->store());
        auto stats = resolver.resolve_project(project);

        std::ostringstream ss;
        ss << "Resolved " << stats.resolved << "/" << stats.total_callsites << " callsites\n";
        ss << "  Resolved (high confidence): " << stats.resolved << "\n";
        ss << "  Ambiguous (low confidence): " << stats.ambiguous << "\n";
        ss << "  Unresolved: " << stats.unresolved << "\n";
        ss << "  Indirect/skipped: " << stats.indirect << "\n";
        ss << "  Avg confidence: " << std::fixed << std::setprecision(2) << stats.avg_confidence;

        return DuckDBToolResult::ok(ss.str(), {
            {"total", stats.total_callsites},
            {"resolved", stats.resolved},
            {"ambiguous", stats.ambiguous},
            {"unresolved", stats.unresolved},
            {"indirect", stats.indirect},
            {"avg_confidence", stats.avg_confidence}
        });
    }

    DuckDBToolResult tool_type_hierarchy(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        std::string name = params.value("name", "");
        if (name.empty()) {
            return DuckDBToolResult::error("Type name is required");
        }

        std::string direction = params.value("direction", "both");

        json ancestors = json::array();
        json descendants = json::array();

        // Query ancestors (what this type extends/implements)
        if (direction == "ancestors" || direction == "both") {
            auto triplets = mind_->store().query_subject(name);
            for (const auto& t : triplets) {
                if (t.predicate == "extends" || t.predicate == "implements" || t.predicate == "embeds") {
                    ancestors.push_back({
                        {"name", t.object},
                        {"relationship", t.predicate}
                    });
                }
            }
        }

        // Query descendants (what extends/implements this type)
        if (direction == "descendants" || direction == "both") {
            auto triplets = mind_->store().query_object(name);
            for (const auto& t : triplets) {
                if (t.predicate == "extends" || t.predicate == "implements" || t.predicate == "embeds") {
                    descendants.push_back({
                        {"name", t.subject},
                        {"relationship", t.predicate}
                    });
                }
            }
        }

        std::ostringstream ss;
        ss << "Type hierarchy for " << name << ":\n";

        if (!ancestors.empty()) {
            ss << "  Ancestors (" << ancestors.size() << "):\n";
            for (const auto& a : ancestors) {
                ss << "    " << a["relationship"].get<std::string>() << " " << a["name"].get<std::string>() << "\n";
            }
        }

        if (!descendants.empty()) {
            ss << "  Descendants (" << descendants.size() << "):\n";
            for (const auto& d : descendants) {
                ss << "    " << d["name"].get<std::string>() << " " << d["relationship"].get<std::string>() << " " << name << "\n";
            }
        }

        if (ancestors.empty() && descendants.empty()) {
            ss << "  (no type relationships found)";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"type", name},
            {"ancestors", ancestors},
            {"descendants", descendants}
        });
    }

    DuckDBToolResult tool_file_imports(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        std::string path = params.value("path", "");
        if (path.empty()) {
            return DuckDBToolResult::error("File path is required");
        }

        // Get just the filename if a full path is provided
        std::filesystem::path p(path);
        std::string filename = p.filename().string();

        json imports = json::array();

        // Query imports triplets
        auto triplets = mind_->store().query_subject(filename);
        for (const auto& t : triplets) {
            if (t.predicate == "imports") {
                imports.push_back({
                    {"module", t.object},
                    {"type", "module"}
                });
            } else if (t.predicate == "imports_name") {
                imports.push_back({
                    {"module", t.object},
                    {"type", "name"}
                });
            } else if (t.predicate == "imports_as") {
                imports.push_back({
                    {"alias", t.object},
                    {"type", "alias"}
                });
            }
        }

        std::ostringstream ss;
        ss << "Imports for " << filename << ":\n";
        for (const auto& imp : imports) {
            if (imp["type"] == "module") {
                ss << "  import " << imp["module"].get<std::string>() << "\n";
            } else if (imp["type"] == "name") {
                ss << "  from ... import " << imp["module"].get<std::string>() << "\n";
            }
        }

        if (imports.empty()) {
            ss << "  (no imports found)";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"file", filename},
            {"imports", imports}
        });
    }

    DuckDBToolResult tool_file_dependents(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        std::string module = params.value("module", "");
        if (module.empty()) {
            return DuckDBToolResult::error("Module name is required");
        }

        json dependents = json::array();

        // Query files that import this module
        auto triplets = mind_->store().query_object(module);
        for (const auto& t : triplets) {
            if (t.predicate == "imports" || t.predicate == "imports_name") {
                dependents.push_back({
                    {"file", t.subject},
                    {"source_file", t.weight}  // Note: weight is reused, may not be useful
                });
            }
        }

        std::ostringstream ss;
        ss << "Files that import " << module << ":\n";
        for (const auto& d : dependents) {
            ss << "  " << d["file"].get<std::string>() << "\n";
        }

        if (dependents.empty()) {
            ss << "  (no dependents found)";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"module", module},
            {"dependents", dependents}
        });
    }

    DuckDBToolResult tool_restore_code_intel_confidence(const json& params) {
        float confidence = params.value("confidence", 0.8f);
        bool dry_run = params.value("dry_run", false);

        auto result = mind_->store().restore_code_intel_confidence(confidence, dry_run);

        std::ostringstream ss;
        ss << "Code intel confidence restoration " << (dry_run ? "(DRY RUN)" : "complete") << ":\n";
        for (size_t i = 0; i < result.counts_by_kind.size(); ++i) {
            const auto& [kind, count] = result.counts_by_kind[i];
            float avg_before = i < result.avg_confidence_before.size()
                ? result.avg_confidence_before[i].second : 0.0f;
            ss << "  " << kind << ": " << count << " memories"
               << " (avg conf before: " << std::fixed << std::setprecision(2) << avg_before;
            if (!dry_run) {
                ss << " → " << confidence;
            }
            ss << ")\n";
        }
        if (!dry_run) {
            ss << "Total updated: " << result.total_updated << " memories\n";
            ss << "Decay rate set to 0.0 (never decay)\n";
        }

        json result_json = {
            {"dry_run", dry_run},
            {"confidence", confidence},
            {"total_updated", result.total_updated}
        };
        for (size_t i = 0; i < result.counts_by_kind.size(); ++i) {
            const auto& [kind, count] = result.counts_by_kind[i];
            float avg_before = i < result.avg_confidence_before.size()
                ? result.avg_confidence_before[i].second : 0.0f;
            result_json[kind] = {
                {"count", count},
                {"avg_confidence_before", avg_before}
            };
        }

        return DuckDBToolResult::ok(ss.str(), result_json);
    }
