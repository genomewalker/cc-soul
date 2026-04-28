// field_code_intel RPC handlers — bodies for declarations in
// chitta/include/chitta/rpc/handlers/field_code_intel.hpp.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

ToolResult FieldRpcHandler::tool_enrichment_status(const json&) {
    size_t total_symbols = field_store_->symbol_count();
    // No direct "undescribed" count in FieldStore — report total only
    std::ostringstream ss;
    ss << "Code Enrichment Status:\n";
    ss << "  Total symbols: " << total_symbols << "\n";
    ss << "  Code files: " << field_store_->code_file_count() << "\n";
    return ToolResult::ok(ss.str(), {
        {"total_symbols", total_symbols},
        {"code_files", field_store_->code_file_count()}
    });
}

ToolResult FieldRpcHandler::tool_describe_symbol(const json& params) {
    int64_t symbol_id = params.value("symbol_id", static_cast<int64_t>(0));
    std::string description = params.value("description", "");
    if (symbol_id == 0) return ToolResult::error("symbol_id is required");
    if (description.empty()) return ToolResult::error("description is required");

    int rc = field_store_->set_symbol_description(static_cast<uint64_t>(symbol_id), description);
    if (rc != 0) return ToolResult::error("Failed to set symbol description");

    return ToolResult::ok("Symbol description set", {
        {"symbol_id", symbol_id},
        {"description_length", description.size()}
    });
}

ToolResult FieldRpcHandler::tool_embed_symbols(const json& params) {
    if (subconscious_) subconscious_->notify_query();
    if (!yantra_) return ToolResult::error("Yantra (embedder) not attached");

    // FieldStore doesn't have get_unembedded_symbols — re-embed all found symbols
    // by searching broadly
    size_t batch_size = static_cast<size_t>(params.value("batch_size", 100));
    auto symbols = field_store_->search_symbols_by_name("", batch_size);

    if (symbols.empty()) {
        return ToolResult::ok("No symbols to embed", {{"embedded", 0}});
    }

    size_t embedded = 0;
    auto start = std::chrono::steady_clock::now();

    for (const auto& sym : symbols) {
        auto s = from_cf_hit(sym);
        std::string disp = display_path(s.file_path);
        std::ostringstream text;
        text << s.kind << " " << s.name << " in " << disp;
        if (!s.signature.empty() && s.signature != s.name) {
            text << ": " << s.signature;
        }

        auto emb = embed_text(text.str());
        if (!emb.empty()) {
            // Re-upsert with embedding to update the symbol's embedding
            field_store_->upsert_symbol(
                s.kind, s.name, s.signature, s.file_path,
                s.line_start, s.line_end, 0, emb);
            embedded++;
        }
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    float rate = ms > 0 ? static_cast<float>(embedded) * 1000.0f / static_cast<float>(ms) : 0;

    std::ostringstream ss;
    ss << "Embedded " << embedded << " symbols in " << ms << "ms";
    ss << " (" << std::fixed << std::setprecision(1) << rate << "/sec)";

    return ToolResult::ok(ss.str(), {
        {"embedded", embedded}, {"elapsed_ms", ms}, {"rate_per_sec", rate}
    });
}

ToolResult FieldRpcHandler::tool_dedupe_symbols(const json&) {
    // No direct dedupe in FieldStore — report current count
    size_t count = field_store_->symbol_count();
    return ToolResult::ok(
        "Symbol deduplication not available in chitta-field (symbols: " + std::to_string(count) + ")",
        {{"symbols", count}, {"note", "dedupe requires Rust-side implementation"}}
    );
}

ToolResult FieldRpcHandler::tool_extract_symbols(const json& params) {
    std::string path = params.value("path", "");
    if (path.empty()) return ToolResult::error("Path is required");
    if (!std::filesystem::exists(path)) {
        return ToolResult::error("Path does not exist: " + path);
    }

    CodeIntel intel;
    auto symbols = intel.extract_file(path);

    if (symbols.empty()) {
        std::string lang = intel.detect_language(path);
        if (lang.empty()) return ToolResult::error("Unsupported file type");
        return ToolResult::ok("No symbols found in " + path, {{"symbols", json::array()}});
    }

    std::ostringstream ss;
    ss << "Extracted " << symbols.size() << " symbols from " << path << ":\n";

    json symbols_json = json::array();
    for (const auto& sym : symbols) {
        ss << "  " << sym.kind << " " << sym.name << " @" << sym.line_start;
        if (!sym.parent.empty()) ss << " (in " << sym.parent << ")";
        ss << "\n";

        symbols_json.push_back({
            {"kind", sym.kind}, {"name", sym.name}, {"file", sym.file_path},
            {"line_start", sym.line_start}, {"line_end", sym.line_end},
            {"parent", sym.parent}
        });
    }
    return ToolResult::ok(ss.str(), {{"symbols", symbols_json}, {"count", symbols.size()}});
}

ToolResult FieldRpcHandler::tool_learn_codebase(const json& params) {
    if (subconscious_) subconscious_->notify_query();

    std::string path = params.value("path", "");
    if (path.empty()) return ToolResult::error("Path is required");

    std::string branch = params.value("branch", "");
    bool cloned = false;
    std::string clone_tmpdir;

    if (is_remote_url(path)) {
        std::string clone_err;
        clone_tmpdir = clone_remote(path, branch, clone_err);
        if (clone_tmpdir.empty()) {
            return ToolResult::error("git clone failed for " + path + ": " + clone_err);
        }
        cloned = true;
        path = clone_tmpdir;
    } else {
        if (!std::filesystem::exists(path))
            return ToolResult::error("Path does not exist: " + path);
    }

    // Cleanup guard — removes tmpdir when we exit this scope (cloned repos only)
    struct CloneGuard {
        std::string dir;
        bool active;
        ~CloneGuard() {
            if (active && !dir.empty()) {
                std::string cmd = "rm -rf " + dir;
                (void)system(cmd.c_str());
            }
        }
    } guard{clone_tmpdir, cloned};

    std::string project = params.value("project", "");
    if (project.empty()) {
        project = cloned
            ? project_from_url(params.value("path", path))
            : std::filesystem::path(path).filename().string();
    }

    size_t max_files = static_cast<size_t>(params.value("max_files", 500));

    std::vector<std::string> exclude = {
        "node_modules", ".git", "build", "__pycache__", "venv", "target", ".venv"
    };
    if (params.contains("exclude") && params["exclude"].is_string()) {
        std::istringstream iss(params["exclude"].get<std::string>());
        std::string dir;
        while (std::getline(iss, dir, ',')) {
            if (!dir.empty()) exclude.push_back(dir);
        }
    }

    // Clean up stale entries: files in the index but gone from disk
    size_t stale_removed = 0;
    try {
        auto code_files_json = field_store_->list_code_files(project);
        auto arr = json::parse(code_files_json, nullptr, false);
        if (arr.is_array()) {
            std::unordered_set<std::string> stale_paths;
            for (const auto& item : arr) {
                std::string indexed_path = item.value("path", "");
                if (!indexed_path.empty() && !std::filesystem::exists(indexed_path)) {
                    stale_paths.insert(indexed_path);
                    field_store_->invalidate_triplets_by_source_file(indexed_path);
                }
            }
            if (!stale_paths.empty()) {
                auto all_syms = field_store_->search_symbols_by_name("", 5000);
                for (const auto& s : all_syms) {
                    std::string sym_path(reinterpret_cast<const char*>(s.file_path));
                    if (stale_paths.count(sym_path)) {
                        field_store_->remove_symbol(s.symbol_id);
                        stale_removed++;
                    }
                }
            }
        }
    } catch (...) {}

    CodeIntel intel;

    // Pass 1: collect source file paths (no tree-sitter parsing yet)
    auto all_files = intel.collect_source_files(path, exclude, max_files);
    if (all_files.empty()) {
        return ToolResult::ok("No symbols found in " + path, {{"stored", 0}});
    }

    std::unordered_set<std::string> seen_files(all_files.begin(), all_files.end());

    // Detect git repo root once, batch provenance for all files
    std::string repo_root = git_repo_root(path);
    auto prov_map = git_batch_provenance(repo_root, seen_files);

    // Pass 2: hash each file, upsert code file, determine which changed
    std::unordered_set<std::string> changed_files;
    for (const auto& fp : seen_files) {
        int64_t mtime = 0;
        try {
            mtime = static_cast<int64_t>(
                std::filesystem::last_write_time(fp).time_since_epoch().count());
        } catch (...) {}

        std::string hash = compute_content_hash(fp);
        auto it = prov_map.find(fp);
        std::string commit, author;
        int64_t git_ts = -1;
        if (it != prov_map.end()) {
            commit = it->second.commit;
            author = it->second.author;
            git_ts = it->second.timestamp_ms;
        }
        auto [file_id, was_updated] = field_store_->upsert_code_file_v2(
            fp, project, mtime, hash, commit, author, git_ts);

        if (was_updated) {
            changed_files.insert(fp);
        }
    }

    // Pass 3: parse only changed files with tree-sitter
    auto result = intel.extract_files(changed_files);
    if (result.symbols.empty() && changed_files.empty()) {
        return ToolResult::ok("No symbols found in " + path, {{"stored", 0}});
    }

    // Store symbols (all are from changed files — parsed only those)
    size_t symbols_stored = 0, symbols_embedded = 0;
    size_t symbols_skipped = seen_files.size() - changed_files.size();
    for (const auto& sym : result.symbols) {
        std::vector<float> emb;
        if (yantra_) {
            std::string text = sym.kind + " " + sym.name;
            if (!sym.signature.empty()) text += " " + sym.signature;
            emb = embed_text(text);
        }
        field_store_->upsert_symbol(
            sym.kind, sym.name,
            sym.signature.empty() ? sym.name : sym.signature,
            sym.file_path, sym.line_start, sym.line_end, 0, emb);
        symbols_stored++;
        if (!emb.empty()) symbols_embedded++;
    }

    // Invalidate old callsite triplets for changed files, then insert new ones
    std::unordered_set<std::string> invalidated_files;
    size_t callsites_stored = 0;
    for (const auto& cs : result.callsites) {
        if (invalidated_files.insert(cs.file_path).second) {
            field_store_->invalidate_triplets_by_source_file(cs.file_path);
        }
        std::string caller_key = cs.file_path + ":" + std::to_string(cs.line);
        field_store_->add_triplet_with_source(caller_key, "calls", cs.callee_leaf,
                                              1.0f, 0, cs.file_path);
        callsites_stored++;
    }

    // Project triplet
    field_store_->add_triplet(project, "contains",
        std::to_string(symbols_stored) + "_symbols");

    std::ostringstream ss;
    ss << "Learned codebase: " << project << "\n";
    if (cloned) {
        ss << "  Source: " << params.value("path", path) << "\n";
        if (!branch.empty()) ss << "  Branch: " << branch << "\n";
    } else {
        ss << "  Path: " << path << "\n";
    }
    ss << "  Symbols stored: " << symbols_stored << "\n";
    ss << "  Symbols skipped (unchanged): " << symbols_skipped << "\n";
    ss << "  Symbols embedded: " << symbols_embedded << "\n";
    ss << "  Callsites: " << callsites_stored << "\n";
    ss << "  Files changed: " << changed_files.size() << "/" << seen_files.size() << "\n";
    if (stale_removed > 0) ss << "  Stale symbols removed: " << stale_removed << "\n";

    std::unordered_map<std::string, size_t> by_kind;
    for (const auto& sym : result.symbols) {
        if (changed_files.count(sym.file_path)) by_kind[sym.kind]++;
    }
    if (!by_kind.empty()) {
        ss << "  Symbol breakdown:\n";
        for (const auto& [kind, count] : by_kind) {
            ss << "    " << kind << ": " << count << "\n";
        }
    }

    return ToolResult::ok(ss.str(), {
        {"project", project}, {"path", path},
        {"symbols_stored", symbols_stored},
        {"symbols_skipped", symbols_skipped},
        {"symbols_embedded", symbols_embedded},
        {"callsites_stored", callsites_stored},
        {"files_changed", changed_files.size()},
        {"files_total", seen_files.size()},
        {"stale_removed", stale_removed}
    });
}

ToolResult FieldRpcHandler::tool_find_symbol(const json& params) {
    if (subconscious_) subconscious_->notify_query();

    std::string name = params.value("name", "");
    if (name.empty()) return ToolResult::error("Name is required");

    auto hits = field_store_->search_symbols_by_name(name, 50);
    if (hits.empty()) {
        return ToolResult::ok("No symbols found matching '" + name + "'",
            {{"symbols", json::array()}});
    }

    std::ostringstream ss;
    ss << "Found " << hits.size() << " symbols matching '" << name << "':\n";

    json symbols_json = json::array();
    std::string kind_filter = params.value("kind", "");
    for (const auto& h : hits) {
        auto s = from_cf_hit(h);
        if (!kind_filter.empty() && s.kind != kind_filter) continue;
        ss << "  " << s.kind << " " << s.name << " @" << s.file_path << ":" << s.line_start << "\n";
        symbols_json.push_back(sym_to_json(s));
    }
    return ToolResult::ok(ss.str(), {{"symbols", symbols_json}, {"count", symbols_json.size()}});
}

ToolResult FieldRpcHandler::tool_symbol_callers(const json& params) {
    if (subconscious_) subconscious_->notify_query();

    auto sym_opt = resolve_symbol_field(params);
    if (!sym_opt) return ToolResult::error("Symbol not found. Provide 'name' or 'id'.");
    const auto& sym = *sym_opt;

    auto caller_ids = field_store_->get_callers(sym.id);
    if (caller_ids.empty()) {
        return ToolResult::ok(
            "No callers found for " + sym.kind + " " + sym.name,
            {{"symbol", sym.name}, {"callers", json::array()}}
        );
    }

    std::ostringstream ss;
    ss << "Found " << caller_ids.size() << " callers for " << sym.kind << " " << sym.name << ":\n";

    json callers_json = json::array();
    for (uint64_t cid : caller_ids) {
        // Find the caller symbol by scanning
        auto all = field_store_->search_symbols_by_name("", 1000);
        for (const auto& h : all) {
            if (h.symbol_id == cid) {
                auto c = from_cf_hit(h);
                ss << "  " << c.kind << " " << c.name << " @" << c.file_path << ":" << c.line_start << "\n";
                callers_json.push_back(sym_to_json(c));
                break;
            }
        }
    }

    return ToolResult::ok(ss.str(), {
        {"symbol", sym.name}, {"symbol_id", sym.id},
        {"callers", callers_json}, {"count", callers_json.size()}
    });
}

ToolResult FieldRpcHandler::tool_symbol_callees(const json& params) {
    if (subconscious_) subconscious_->notify_query();

    auto sym_opt = resolve_symbol_field(params);
    if (!sym_opt) return ToolResult::error("Symbol not found. Provide 'name' or 'id'.");
    const auto& sym = *sym_opt;

    auto callee_ids = field_store_->get_callees(sym.id);
    if (callee_ids.empty()) {
        return ToolResult::ok(
            "No callees found for " + sym.kind + " " + sym.name,
            {{"symbol", sym.name}, {"callees", json::array()}}
        );
    }

    std::ostringstream ss;
    ss << "Found " << callee_ids.size() << " callees for " << sym.kind << " " << sym.name << ":\n";

    json callees_json = json::array();
    for (uint64_t cid : callee_ids) {
        auto all = field_store_->search_symbols_by_name("", 1000);
        for (const auto& h : all) {
            if (h.symbol_id == cid) {
                auto c = from_cf_hit(h);
                ss << "  " << c.kind << " " << c.name << " @" << c.file_path << ":" << c.line_start << "\n";
                callees_json.push_back(sym_to_json(c));
                break;
            }
        }
    }

    return ToolResult::ok(ss.str(), {
        {"symbol", sym.name}, {"symbol_id", sym.id},
        {"callees", callees_json}, {"count", callees_json.size()}
    });
}

ToolResult FieldRpcHandler::tool_read_symbol(const json& params) {
    if (subconscious_) subconscious_->notify_query();

    auto sym_opt = resolve_symbol_field(params);
    if (!sym_opt) return ToolResult::error("Symbol not found. Provide 'name' or 'id'.");
    const auto& sym = *sym_opt;

    std::string code = read_source_lines(sym.file_path, sym.line_start, sym.line_end);
    if (code.empty()) {
        return ToolResult::error("No code found at " + sym.file_path + ":" +
            std::to_string(sym.line_start) + "-" + std::to_string(sym.line_end));
    }

    std::ostringstream ss;
    ss << sym.kind << " " << sym.name << " @" << sym.file_path << ":"
       << sym.line_start << "-" << sym.line_end << "\n\n" << code;

    return ToolResult::ok(ss.str(), {
        {"symbol", sym.name}, {"kind", sym.kind}, {"file", sym.file_path},
        {"line_start", sym.line_start}, {"line_end", sym.line_end}, {"code", code}
    });
}

ToolResult FieldRpcHandler::tool_read_function(const json& params) {
    std::string name = params.value("name", "");
    if (name.empty()) return ToolResult::error("Function name is required");

    auto hits = field_store_->search_symbols_by_name(name, 50);
    if (hits.empty()) {
        return ToolResult::error("Function/method '" + name + "' not found");
    }

    // Find exact match with function or method kind
    const CfSymbolHit* best = nullptr;
    for (const auto& h : hits) {
        auto s = from_cf_hit(h);
        if (s.name == name && (s.kind == "function" || s.kind == "method")) {
            best = &h;
            break;
        }
    }
    // Fallback to exact name match of any kind
    if (!best) {
        for (const auto& h : hits) {
            auto s = from_cf_hit(h);
            if (s.name == name) { best = &h; break; }
        }
    }
    if (!best) best = &hits[0];

    auto sym = from_cf_hit(*best);
    std::string code = read_source_lines(sym.file_path, sym.line_start, sym.line_end);

    std::ostringstream ss;
    ss << sym.kind << " " << sym.name << " @" << sym.file_path << ":"
       << sym.line_start << "-" << sym.line_end << "\n\n" << code;

    return ToolResult::ok(ss.str(), {
        {"symbol", sym.name}, {"kind", sym.kind}, {"file", sym.file_path},
        {"line_start", sym.line_start}, {"line_end", sym.line_end}, {"code", code}
    });
}

ToolResult FieldRpcHandler::tool_search_symbols(const json& params) {
    if (subconscious_) subconscious_->notify_query();

    std::string query = params.value("query", "");
    if (query.empty()) return ToolResult::error("Query is required");

    std::string kind = params.value("kind", "");
    size_t limit = static_cast<size_t>(params.value("limit", 10));
    bool is_code_query = looks_like_code_query(query);

    json symbols_json = json::array();
    std::unordered_set<uint64_t> seen_ids;
    std::ostringstream ss;
    std::string search_mode;

    // BM25-style name search
    auto bm25_hits = field_store_->search_symbols_by_name(query, limit);
    if (!bm25_hits.empty()) search_mode = "name";

    // Semantic search if not code query and yantra available
    std::vector<CfSymbolHit> semantic_hits;
    if (!is_code_query && yantra_) {
        auto emb = embed_query(query);
        if (!emb.empty()) {
            semantic_hits = field_store_->search_symbols_semantic(emb, limit);
            search_mode = bm25_hits.empty() ? "semantic" : "hybrid";
        }
    }

    auto add_symbol = [&](const CfSymbolHit& h, float score, const std::string& source) {
        if (seen_ids.count(h.symbol_id) || symbols_json.size() >= limit) return;
        auto s = from_cf_hit(h);
        if (!kind.empty() && s.kind != kind) return;
        seen_ids.insert(h.symbol_id);

        std::string disp = display_path(s.file_path);
        if (score > 0) {
            ss << "  [" << std::fixed << std::setprecision(0) << (score * 100) << "%] ";
        } else {
            ss << "  ";
        }
        ss << s.kind << " " << s.name << " @" << disp << ":" << s.line_start
           << " (" << source << ")\n";

        json sym_json = sym_to_json(s);
        sym_json["source"] = source;
        if (score > 0) sym_json["score"] = score;
        symbols_json.push_back(sym_json);
    };

    if (is_code_query) {
        for (const auto& h : bm25_hits) add_symbol(h, 0, "name");
        for (const auto& h : semantic_hits) add_symbol(h, h.score, "semantic");
    } else {
        for (const auto& h : semantic_hits) add_symbol(h, h.score, "semantic");
        for (const auto& h : bm25_hits) add_symbol(h, 0, "name");
    }

    if (symbols_json.empty()) {
        return ToolResult::ok("No symbols found for query: " + query,
            {{"symbols", json::array()}, {"mode", search_mode}});
    }

    std::ostringstream header;
    header << "Found " << symbols_json.size() << " symbols for '" << query
           << "' (" << search_mode << ", " << (is_code_query ? "code" : "NL") << " query):\n";

    return ToolResult::ok(header.str() + ss.str(),
        {{"symbols", symbols_json}, {"count", symbols_json.size()}, {"mode", search_mode}});
}

ToolResult FieldRpcHandler::tool_code_context(const json& params) {
    if (subconscious_) subconscious_->notify_query();

    std::string path = params.value("path", "");
    size_t total_symbols = field_store_->symbol_count();

    std::ostringstream ss;
    ss << "Code Context:\n";
    ss << "  Symbols: " << total_symbols << " indexed\n";
    ss << "  Code files: " << field_store_->code_file_count() << "\n";

    json result;
    result["total_symbols"] = total_symbols;
    result["code_files"] = field_store_->code_file_count();

    if (!path.empty() && std::filesystem::exists(path)) {
        if (std::filesystem::is_regular_file(path)) {
            auto file_syms = field_store_->symbols_in_file(path);
            ss << "  File: " << path << " (" << file_syms.size() << " symbols)\n";
            result["file_symbols"] = file_syms.size();
        } else if (std::filesystem::is_directory(path)) {
            CodeIntel intel;
            auto dir_result = intel.extract_directory_full(path, {}, 50);
            ss << "  Directory: " << path << " (" << dir_result.symbols.size() << " symbols in sample)\n";
            result["dir_symbols"] = dir_result.symbols.size();
        }
    }

    return ToolResult::ok(ss.str(), result);
}

ToolResult FieldRpcHandler::tool_smart_context(const json& params) {
    if (subconscious_) subconscious_->notify_query();

    std::string task = params.value("task", "");
    if (task.empty()) return ToolResult::error("task is required");

    std::string mode = params.value("mode", "full");
    bool include_memories = params.value("memories", true);
    bool include_code = params.value("code", true);
    bool include_neighbors = params.value("neighbors", true);
    std::string realm = params.value("realm", "");
    bool fast = (mode == "fast");

    size_t mem_limit = fast ? 3 : 5;
    size_t sym_limit = fast ? 3 : 5;

    // Affective context for mood-congruent recall + frustration-escalation
    float qv = std::numeric_limits<float>::quiet_NaN();
    float qa = std::numeric_limits<float>::quiet_NaN();
    if (params.contains("query_valence") && params.contains("query_arousal")) {
        qv = params["query_valence"].get<float>();
        qa = params["query_arousal"].get<float>();
    }
    bool has_affect = !std::isnan(qv);

    // Classify query intent for optimal recall routing (Omni-SimpleMem §3.2.2)
    chitta::QueryIntentClassifier intent_clf;
    auto intent = intent_clf.classify(task);
    bool is_temporal     = (intent.type == chitta::QueryIntentType::Temporal);
    bool is_code_intent  = (intent.type == chitta::QueryIntentType::Code);
    bool is_relationship = (intent.type == chitta::QueryIntentType::Relationship);

    std::ostringstream ss;
    json result;

    // 1. MEMORIES — routed by intent
    json memories_json = json::array();
    if (include_memories) {
        std::vector<FieldRecallHit> hits;

        if (is_temporal) {
            // Temporal: last 30 days, re-ranked by recency
            int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            hits = field_store_->recall_temporal(
                now_ms - (int64_t)30 * 24 * 3600 * 1000, now_ms,
                mem_limit * 2, realm);
        } else if (is_code_intent) {
            // Code: BM25 keyword search
            hits = field_store_->recall_keyword(task, mem_limit);
        } else if (is_relationship) {
            // Relationship: skip dense recall, let graph section handle it
            // (triplet neighbors cover this better)
        } else {
            // Default: dense semantic recall (with affect if available)
            auto emb = embed_query(task);
            if (!emb.empty()) {
                hits = has_affect
                    ? field_store_->recall_ctx(emb, mem_limit, realm, qv, qa)
                    : field_store_->recall(emb, mem_limit, realm);
            }
        }

        if (!hits.empty()) {
            ss << "[mem]\n";
            for (const auto& h : hits) {
                int pct = static_cast<int>(std::min(h.score, 1.0f) * 100);
                std::string type_short = h.kind.substr(0, 3);
                std::string title = h.content.substr(0, 60);
                size_t newline = title.find('\n');
                if (newline != std::string::npos) title = title.substr(0, newline);
                ss << "[" << pct << "%:" << type_short << ":#" << h.memory_id << "] " << title << "\n";

                // Progressive pyramid: depth of content scales with relevance
                size_t text_limit = (h.score >= 0.75f) ? 300
                                  : (h.score >= 0.50f) ? 120
                                                       : 60;
                memories_json.push_back({
                    {"id", std::to_string(h.memory_id)},
                    {"relevance", h.score},
                    {"type", h.kind},
                    {"text", h.content.substr(0, std::min(h.content.size(), text_limit))}
                });
            }
        }
    }

    // 2. CODE SYMBOLS
    json symbols_json = json::array();
    if (include_code) {
        bool is_code = looks_like_code_query(task);
        if (fast || is_code) {
            auto name_hits = field_store_->search_symbols_by_name(task, sym_limit);
            if (!name_hits.empty()) {
                ss << "\n[code]\n";
                for (const auto& h : name_hits) {
                    auto s = from_cf_hit(h);
                    ss << s.file_path << ":" << s.line_start
                       << " " << s.kind << " " << s.name << "\n";
                    symbols_json.push_back({
                        {"name", s.name}, {"kind", s.kind},
                        {"file", s.file_path}, {"line_start", s.line_start}
                    });
                }
            }
        } else if (yantra_) {
            auto emb = embed_query(task);
            if (!emb.empty()) {
                auto sem_hits = field_store_->search_symbols_semantic(emb, sym_limit);
                if (!sem_hits.empty()) {
                    ss << "\n[code]\n";
                    for (const auto& h : sem_hits) {
                        auto s = from_cf_hit(h);
                        ss << s.file_path << ":" << s.line_start
                           << " " << s.kind << " " << s.name
                           << " (" << static_cast<int>(h.score * 100) << "%)\n";
                        symbols_json.push_back({
                            {"name", s.name}, {"kind", s.kind},
                            {"file", s.file_path}, {"line_start", s.line_start},
                            {"similarity", h.score}
                        });
                    }
                }
            }
        }
    }

    // 3. TRIPLET NEIGHBORS
    json triplets_json = json::array();
    if (include_neighbors) {
        auto terms = extract_terms(task);
        ss << "\n[graph]\n";
        for (const auto& term : terms) {
            if (term.length() < 4 || triplets_json.size() >= 5) break;
            std::string raw = field_store_->query_subject(term);
            try {
                auto arr = json::parse(raw);
                for (const auto& t : arr) {
                    if (triplets_json.size() >= 5) break;
                    ss << t.value("subject", "?") << " -> "
                       << t.value("predicate", "?") << " -> "
                       << t.value("object", "?") << "\n";
                    triplets_json.push_back(t);
                }
            } catch (...) {}
        }
    }

    result["memories"] = memories_json;
    result["symbols"] = symbols_json;
    result["triplets"] = triplets_json;
    result["mode"] = mode;
    result["task"] = task;

    std::string output = ss.str();
    if (output.empty()) output = "No context found for: " + task;
    return ToolResult::ok(output, result);
}

ToolResult FieldRpcHandler::tool_codebase_overview(const json& params) {
    std::string project = params.value("project", "");

    std::string files_json_str = field_store_->list_code_files(project);
    json files;
    try { files = json::parse(files_json_str); } catch (...) { files = json::array(); }

    if (files.empty()) {
        std::ostringstream ss;
        ss << "No indexed files";
        if (!project.empty()) ss << " for project: " << project;
        ss << "\nRun: learn_codebase --path /your/project --project "
           << (project.empty() ? "myproj" : project);
        return ToolResult::ok(ss.str(), {{"files", 0}});
    }

    size_t total_symbols = field_store_->symbol_count();
    std::ostringstream ss;
    ss << "Codebase: " << (project.empty() ? "(all)" : project) << "\n";
    ss << "  Files: " << files.size() << "\n";
    ss << "  Symbols: " << total_symbols << "\n\n";

    ss << "Files:\n";
    for (const auto& f : files) {
        std::string path = f.value("path", "");
        std::filesystem::path p(path);
        ss << "  " << p.filename().string() << "\n";
    }

    json result;
    result["files"] = files.size();
    result["symbols"] = total_symbols;
    result["project"] = project;

    return ToolResult::ok(ss.str(), result);
}

ToolResult FieldRpcHandler::tool_clear_codebase(const json& params) {
    std::string project = params.value("project", "");
    if (project.empty()) return ToolResult::error("Project name is required");

    bool dry_run = params.value("dry_run", false);

    if (dry_run) {
        size_t syms = field_store_->symbol_count();
        size_t files = field_store_->code_file_count();
        std::ostringstream ss;
        ss << "Would clear codebase: " << project << "\n";
        ss << "  Symbols: " << syms << "\n";
        ss << "  Files: " << files << "\n";
        return ToolResult::ok(ss.str(), {
            {"project", project}, {"dry_run", true},
            {"symbols", syms}, {"files", files}
        });
    }

    int rc = field_store_->clear_project(project);
    std::ostringstream ss;
    ss << "Cleared codebase: " << project << " (rc=" << rc << ")";
    return ToolResult::ok(ss.str(), {{"project", project}, {"rc", rc}});
}

ToolResult FieldRpcHandler::tool_clear_triplets(const json& params) {
    std::string pattern = params.value("pattern", "");
    if (pattern.empty()) return ToolResult::error("Pattern is required");

    bool dry_run = params.value("dry_run", false);

    // Query triplets matching pattern and delete them
    std::string raw = field_store_->query_subject(pattern);
    json arr;
    try { arr = json::parse(raw); } catch (...) { arr = json::array(); }

    size_t count = arr.size();
    if (dry_run || count == 0) {
        std::ostringstream ss;
        ss << (dry_run ? "Would delete " : "No triplets match ") << count
           << " triplets matching: " << pattern;
        return ToolResult::ok(ss.str(), {
            {"pattern", pattern}, {"dry_run", dry_run}, {"count", count}
        });
    }

    // Triplet deletion not directly supported per-pattern in FieldStore
    return ToolResult::ok(
        "Triplet deletion by pattern not yet supported in chitta-field (found " +
        std::to_string(count) + " matching)",
        {{"pattern", pattern}, {"count", count}, {"note", "requires Rust-side implementation"}}
    );
}

ToolResult FieldRpcHandler::tool_resolve_callsites(const json& params) {
    if (subconscious_) subconscious_->notify_query();
    // FieldStore doesn't have SymbolResolver — callsites are stored as triplets
    return ToolResult::ok(
        "Callsite resolution uses triplet-based call graph in chitta-field",
        {{"note", "call edges stored via cf_add_sym_call_edge"}}
    );
}

ToolResult FieldRpcHandler::tool_type_hierarchy(const json& params) {
    if (subconscious_) subconscious_->notify_query();

    std::string name = params.value("name", "");
    if (name.empty()) return ToolResult::error("Type name is required");

    std::string direction = params.value("direction", "both");

    json ancestors = json::array();
    json descendants = json::array();

    // Query ancestors (what this type extends/implements)
    if (direction == "ancestors" || direction == "both") {
        std::string raw = field_store_->query_subject(name);
        try {
            auto arr = json::parse(raw);
            for (const auto& t : arr) {
                std::string pred = t.value("predicate", "");
                if (pred == "extends" || pred == "implements" || pred == "embeds") {
                    ancestors.push_back({
                        {"name", t.value("object", "")},
                        {"relationship", pred}
                    });
                }
            }
        } catch (...) {}
    }

    // Query descendants (what extends/implements this type)
    if (direction == "descendants" || direction == "both") {
        std::string raw = field_store_->query_object(name);
        try {
            auto arr = json::parse(raw);
            for (const auto& t : arr) {
                std::string pred = t.value("predicate", "");
                if (pred == "extends" || pred == "implements" || pred == "embeds") {
                    descendants.push_back({
                        {"name", t.value("subject", "")},
                        {"relationship", pred}
                    });
                }
            }
        } catch (...) {}
    }

    std::ostringstream ss;
    ss << "Type hierarchy for " << name << ":\n";
    if (!ancestors.empty()) {
        ss << "  Ancestors (" << ancestors.size() << "):\n";
        for (const auto& a : ancestors) {
            ss << "    " << a["relationship"].get<std::string>() << " "
               << a["name"].get<std::string>() << "\n";
        }
    }
    if (!descendants.empty()) {
        ss << "  Descendants (" << descendants.size() << "):\n";
        for (const auto& d : descendants) {
            ss << "    " << d["name"].get<std::string>() << " "
               << d["relationship"].get<std::string>() << " " << name << "\n";
        }
    }
    if (ancestors.empty() && descendants.empty()) {
        ss << "  (no type relationships found)";
    }

    return ToolResult::ok(ss.str(), {
        {"type", name}, {"ancestors", ancestors}, {"descendants", descendants}
    });
}

ToolResult FieldRpcHandler::tool_file_imports(const json& params) {
    if (subconscious_) subconscious_->notify_query();

    std::string path = params.value("path", "");
    if (path.empty()) return ToolResult::error("File path is required");

    std::filesystem::path p(path);
    std::string filename = p.filename().string();

    json imports = json::array();
    std::string raw = field_store_->query_subject(filename);
    try {
        auto arr = json::parse(raw);
        for (const auto& t : arr) {
            std::string pred = t.value("predicate", "");
            if (pred == "imports") {
                imports.push_back({{"module", t.value("object", "")}, {"type", "module"}});
            } else if (pred == "imports_name") {
                imports.push_back({{"module", t.value("object", "")}, {"type", "name"}});
            } else if (pred == "imports_as") {
                imports.push_back({{"alias", t.value("object", "")}, {"type", "alias"}});
            }
        }
    } catch (...) {}

    std::ostringstream ss;
    ss << "Imports for " << filename << ":\n";
    for (const auto& imp : imports) {
        if (imp["type"] == "module") ss << "  import " << imp["module"].get<std::string>() << "\n";
        else if (imp["type"] == "name") ss << "  from ... import " << imp["module"].get<std::string>() << "\n";
    }
    if (imports.empty()) ss << "  (no imports found)";

    return ToolResult::ok(ss.str(), {{"file", filename}, {"imports", imports}});
}

ToolResult FieldRpcHandler::tool_file_dependents(const json& params) {
    if (subconscious_) subconscious_->notify_query();

    std::string module = params.value("module", "");
    if (module.empty()) return ToolResult::error("Module name is required");

    json dependents = json::array();
    std::string raw = field_store_->query_object(module);
    try {
        auto arr = json::parse(raw);
        for (const auto& t : arr) {
            std::string pred = t.value("predicate", "");
            if (pred == "imports" || pred == "imports_name") {
                dependents.push_back({{"file", t.value("subject", "")}});
            }
        }
    } catch (...) {}

    std::ostringstream ss;
    ss << "Files that import " << module << ":\n";
    for (const auto& d : dependents) ss << "  " << d["file"].get<std::string>() << "\n";
    if (dependents.empty()) ss << "  (no dependents found)";

    return ToolResult::ok(ss.str(), {{"module", module}, {"dependents", dependents}});
}

ToolResult FieldRpcHandler::tool_restore_code_intel_confidence(const json& params) {
    float confidence = params.value("confidence", 0.8f);
    bool dry_run = params.value("dry_run", false);

    // Get code-intel kind memories and update their confidence
    static const std::vector<std::string> code_kinds = {
        "symbol", "projectessence", "modulestate", "patternstate"
    };

    size_t total = 0;
    for (const auto& kind : code_kinds) {
        auto hits = field_store_->recall_by_kind(kind, 1000);
        total += hits.size();
        if (!dry_run) {
            for (const auto& h : hits) {
                field_store_->strengthen(h.memory_id, confidence - h.confidence);
            }
        }
    }

    std::ostringstream ss;
    ss << "Code intel confidence restoration " << (dry_run ? "(DRY RUN)" : "complete") << ":\n";
    ss << "  Total code-intel memories: " << total << "\n";
    if (!dry_run) {
        ss << "  Target confidence: " << std::fixed << std::setprecision(2) << confidence << "\n";
    }

    return ToolResult::ok(ss.str(), {
        {"dry_run", dry_run}, {"confidence", confidence}, {"total", total}
    });
}

} // namespace chitta
