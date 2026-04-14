// Included into FieldRpcHandler class body — not a standalone header.
// Unified lookup tool: intent-classified, multi-backend, weighted RRF fusion.

    ToolResult tool_lookup(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) return ToolResult::error("query is required");

        size_t limit      = static_cast<size_t>(params.value("limit", 10));
        std::string realm = params.value("realm", "");
        std::string mode  = params.value("mode", "auto");
        bool explain      = params.value("explain", false);

        // 1. Classify intent
        chitta::QueryIntentClassifier intent_clf;
        auto intent = intent_clf.classify(query);
        bool is_temporal     = (intent.type == chitta::QueryIntentType::Temporal);
        bool is_code_intent  = (intent.type == chitta::QueryIntentType::Code);
        bool is_relationship = (intent.type == chitta::QueryIntentType::Relationship);
        bool is_entity       = (intent.type == chitta::QueryIntentType::Entity);

        // 2. Intent-based backend weights: {semantic, keyword, triplet, temporal, code}
        struct BackendWeights {
            float semantic;
            float keyword;
            float triplet;
            float temporal;
            float code;
        };

        BackendWeights weights;
        if (is_entity) {
            weights = {0.6f, 0.3f, 0.1f, 0.0f, 0.0f};
        } else if (is_relationship) {
            weights = {0.2f, 0.1f, 0.6f, 0.0f, 0.1f};
        } else if (is_code_intent) {
            weights = {0.1f, 0.4f, 0.1f, 0.0f, 0.4f};
        } else if (is_temporal) {
            weights = {0.2f, 0.1f, 0.0f, 0.7f, 0.0f};
        } else {
            // Aspect, Exploratory, Meta, default
            weights = {0.5f, 0.3f, 0.1f, 0.1f, 0.0f};
        }

        constexpr float kRRF = 60.0f;

        // Fused score map: memory id -> accumulated weighted RRF score
        std::unordered_map<std::string, float> fused_scores;
        std::unordered_map<std::string, json>  fused_entries;
        std::unordered_map<std::string, int>   backend_count;  // how many backends returned this id

        std::vector<std::string> backends_used;
        bool early_exit = false;

        // Helper: add ranked list to fusion map with given weight
        auto fuse = [&](const json& arr, float weight, const std::string& backend_name) {
            if (arr.empty() || weight <= 0.0f) return;
            backends_used.push_back(backend_name);
            int rank = 1;
            for (const auto& entry : arr) {
                std::string id = entry.value("id", "");
                if (id.empty()) { rank++; continue; }
                fused_scores[id] += weight / (kRRF + static_cast<float>(rank));
                if (fused_entries.find(id) == fused_entries.end()) fused_entries[id] = entry;
                backend_count[id]++;
                rank++;
            }
        };

        // 3a. Keyword — always run first (BM25 is fast, catches exact matches)
        auto kw_hits = field_store_->recall_keyword(query, limit);
        json kw_json = hits_to_results_json(kw_hits);
        fuse(kw_json, weights.keyword, "keyword");

        // Early exit: if top keyword score >= 0.90, skip remaining backends
        if (!kw_hits.empty() && kw_hits[0].score >= 0.90f) {
            early_exit = true;
        }

        // 3b. Semantic (dense vector)
        if (!early_exit && weights.semantic > 0.0f) {
            auto emb = embed_query(query);
            if (!emb.empty()) {
                auto sem_hits = field_store_->recall(emb, limit, realm);
                sem_hits.erase(
                    std::remove_if(sem_hits.begin(), sem_hits.end(),
                        [](const FieldRecallHit& h) { return h.content.empty(); }),
                    sem_hits.end());
                json sem_json = hits_to_results_json(sem_hits);
                fuse(sem_json, weights.semantic, "semantic");
            }
        }

        // 3c. Triplet (graph) — extract entity tokens len>=4, query subject (max 3 tokens)
        if (!early_exit && weights.triplet > 0.0f) {
            auto terms = extract_terms(query);
            int triplet_queries = 0;
            json triplet_results = json::array();
            for (const auto& term : terms) {
                if (term.length() < 4 || triplet_queries >= 3) break;
                std::string raw = field_store_->query_subject(term);
                try {
                    auto arr = json::parse(raw);
                    for (const auto& t : arr) {
                        // Convert triplet to memory-like entry using subject as id proxy
                        std::string subj = t.value("subject", "");
                        std::string pred = t.value("predicate", "");
                        std::string obj  = t.value("object", "");
                        if (subj.empty()) continue;
                        std::string text = subj + " " + pred + " " + obj;
                        json entry = {{"id", subj}, {"type", "triplet"}, {"text", text}, {"relevance", 0.5f}};
                        if (fused_entries.find(subj) == fused_entries.end()) {
                            triplet_results.push_back(entry);
                        }
                    }
                } catch (...) {}
                triplet_queries++;
            }
            fuse(triplet_results, weights.triplet, "triplet");
        }

        // 3d. Temporal — only if intent is Temporal
        if (!early_exit && is_temporal && weights.temporal > 0.0f) {
            int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            int64_t start_ms = now_ms - (int64_t)30 * 24 * 3600 * 1000;
            auto temp_hits = field_store_->recall_temporal(start_ms, now_ms, limit, realm);
            json temp_json = hits_to_results_json(temp_hits);
            fuse(temp_json, weights.temporal, "temporal");
        }

        // 3e. Code symbols — only if Code intent or looks_like_code_query
        if (!early_exit && weights.code > 0.0f && (is_code_intent || looks_like_code_query(query))) {
            auto sym_hits = field_store_->recall_keyword(query, limit);
            // Also try symbol name search
            auto name_hits = field_store_->search_symbols_by_name(query, limit);
            json sym_json = json::array();
            for (const auto& h : name_hits) {
                auto s = from_cf_hit(h);
                sym_json.push_back({
                    {"id", "sym:" + s.name},
                    {"type", "symbol"},
                    {"text", s.kind + " " + s.name + " @ " + s.file_path + ":" + std::to_string(s.line_start)},
                    {"relevance", h.score}
                });
            }
            fuse(sym_json, weights.code, "code");
        }

        // 4. Apply boosts and build sorted result list
        // Collect all ids
        std::vector<std::pair<std::string, float>> ranked;
        ranked.reserve(fused_scores.size());

        for (auto& [id, score] : fused_scores) {
            float boosted = score;

            // +0.10 if id appears in 2+ backends
            if (backend_count.count(id) && backend_count[id] >= 2) {
                boosted += 0.10f;
            }

            // +0.15 if exact token match in text
            const auto& entry = fused_entries[id];
            std::string text = entry.value("text", "");
            // Simple check: lowercase query tokens against lowercase text
            {
                std::string lower_text = text;
                std::string lower_query = query;
                for (char& c : lower_text)  c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                for (char& c : lower_query) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (lower_text.find(lower_query) != std::string::npos) {
                    boosted += 0.15f;
                }
            }

            ranked.emplace_back(id, boosted);
        }

        std::sort(ranked.begin(), ranked.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        if (ranked.size() > limit) ranked.resize(limit);

        // 5. Escalation: if top score < 0.55 and mode != "fast", run full_resonate
        bool escalated = false;
        json escalated_results;
        if (!ranked.empty() && ranked[0].second < 0.55f && mode != "fast") {
            auto deep_result = tool_full_resonate({{"query", query}, {"k", (int)limit}, {"realm", realm}});
            if (!deep_result.is_error) {
                escalated = true;
                try {
                    escalated_results = deep_result.structured.value("results", json::array());
                } catch (...) {}
            }
        }

        // 6. Format output
        std::ostringstream ss;

        if (explain) {
            ss << "[intent:" << chitta::query_intent_type_to_string(intent.type)
               << " conf:" << static_cast<int>(intent.confidence * 100) << "%]\n";
            ss << "[backends:";
            for (size_t i = 0; i < backends_used.size(); ++i) {
                if (i > 0) ss << ",";
                ss << backends_used[i];
            }
            if (escalated) ss << ",deep";
            ss << "]\n";
        }

        json results_json = json::array();

        if (escalated && !escalated_results.empty()) {
            ss << "Found " << escalated_results.size() << " results (deep search):\n";
            for (const auto& r : escalated_results) {
                int pct = static_cast<int>(r.value("relevance", 0.0f) * 100);
                ss << "[" << pct << "%] [" << r.value("type", "?") << "] "
                   << r.value("text", "").substr(0, 400) << "\n";
            }
            results_json = escalated_results;
        } else {
            ss << "Found " << ranked.size() << " results:\n";
            for (const auto& [id, score] : ranked) {
                const auto& entry = fused_entries[id];
                int pct = static_cast<int>(std::min(score, 1.0f) * 100);
                json r = entry;
                r["relevance"] = score;
                ss << "[" << pct << "%] [" << r.value("type", "?") << "] "
                   << r.value("text", "").substr(0, 400) << "\n";
                results_json.push_back(std::move(r));
            }
        }

        json structured = {
            {"results", results_json},
            {"intent", chitta::query_intent_type_to_string(intent.type)},
            {"backends", backends_used},
            {"escalated", escalated}
        };

        return ToolResult::ok(ss.str(), structured);
    }
