// Included into FieldRpcHandler class body — not a standalone header.
// Memory tools: remember, recall, strengthen, weaken, forget, observe, grow,
// hybrid_recall, smart_recall, recall_keyword, recall_temporal, etc.

    // ── Core write ops ───────────────────────────────────────────────────────

    DuckDBToolResult tool_remember(const json& params) {
        std::string content = params.value("content", "");
        if (content.empty()) return DuckDBToolResult::error("content is required");

        std::string kind  = params.value("type", "episode");
        std::string realm = params.value("realm", "brahman");
        float confidence  = params.value("confidence", 0.8f);
        float decay_rate  = 0.001f;

        // Code-intel kinds get zero decay
        static const std::unordered_set<std::string> code_kinds = {
            "symbol", "projectessence", "modulestate", "patternstate"
        };
        if (code_kinds.count(kind)) decay_rate = 0.0f;

        auto embedding = embed_text(content);

        uint64_t id = field_store_->remember(kind, realm, content, embedding, confidence, decay_rate);

        // Create triplets from tags if provided
        if (params.contains("tags") && params["tags"].is_array()) {
            for (const auto& tag : params["tags"]) {
                if (tag.is_string()) {
                    field_store_->add_triplet(
                        std::to_string(id), "tagged", tag.get<std::string>());
                }
            }
        }

        std::string id_str = std::to_string(id);
        return DuckDBToolResult::ok("Stored memory #" + id_str, {
            {"id", id_str}, {"type", kind}, {"realm", realm}
        });
    }

    DuckDBToolResult tool_recall(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) return DuckDBToolResult::error("query is required");

        size_t limit      = static_cast<size_t>(params.value("limit", 10));
        std::string realm = params.value("realm", "");

        auto embedding = embed_query(query);
        if (embedding.empty()) return DuckDBToolResult::error("Failed to embed query");

        auto hits = field_store_->recall(embedding, limit, realm);
        json results_json = hits_to_results_json(hits);

        std::ostringstream ss;
        ss << "Found " << hits.size() << " results";
        if (!realm.empty()) ss << " in realm '" << realm << "'";
        ss << ":\n";
        for (const auto& r : results_json) {
            int pct = static_cast<int>(r.value("relevance", 0.0f) * 100);
            ss << "[" << pct << "%] [" << r.value("type", "?") << "] "
               << r.value("text", "").substr(0, 100) << "\n";
        }

        return DuckDBToolResult::ok(ss.str(), {{"results", results_json}, {"realm", realm}});
    }

    DuckDBToolResult tool_recall_temporal(const json& params) {
        std::string query = params.value("query", "");
        std::string realm = params.value("realm", "");
        size_t limit      = static_cast<size_t>(params.value("limit", 20));

        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        int64_t start_ms = 0, end_ms = 0;
        if (params.contains("start")) {
            auto ts = parse_timestamp_str(params["start"].get<std::string>());
            if (ts) start_ms = *ts;
        }
        if (params.contains("end")) {
            auto ts = parse_timestamp_str(params["end"].get<std::string>());
            if (ts) end_ms = *ts;
        }

        // Default: last 7 days when neither specified
        if (start_ms == 0 && end_ms == 0) {
            end_ms = now_ms;
            start_ms = end_ms - (7LL * 24 * 3600 * 1000);
        }
        // If only start specified, default end to now
        if (end_ms == 0) {
            end_ms = now_ms;
        }

        auto hits = field_store_->recall_temporal(start_ms, end_ms, limit, realm);

        // If query provided, re-rank by semantic similarity
        if (!query.empty() && !hits.empty()) {
            auto qemb = embed_query(query);
            if (!qemb.empty()) {
                auto semantic_hits = field_store_->recall(qemb, limit, realm);
                json temporal_json = hits_to_results_json(hits);
                json semantic_json = hits_to_results_json(semantic_hits);
                json merged = merge_results(temporal_json, semantic_json);
                if (merged.size() > limit) merged.erase(merged.begin() + static_cast<int>(limit), merged.end());

                std::ostringstream ss;
                ss << "Found " << merged.size() << " temporal+semantic results:\n";
                for (const auto& r : merged) {
                    ss << "[" << r.value("type", "?") << "] " << r.value("text", "").substr(0, 100) << "\n";
                }
                return DuckDBToolResult::ok(ss.str(), {{"results", merged}, {"count", merged.size()}, {"realm", realm}});
            }
        }

        json results_json = hits_to_results_json(hits);
        std::ostringstream ss;
        ss << "Found " << hits.size() << " memories:\n";
        for (const auto& r : results_json) {
            ss << "[" << r.value("type", "?") << "] " << r.value("text", "").substr(0, 100) << "\n";
        }
        return DuckDBToolResult::ok(ss.str(), {{"results", results_json}, {"count", hits.size()}, {"realm", realm}});
    }

    DuckDBToolResult tool_recall_keyword(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) return DuckDBToolResult::error("query is required");
        size_t k = static_cast<size_t>(params.value("limit", 10));

        auto hits = field_store_->recall_keyword(query, k);
        json results_json = hits_to_results_json(hits);

        std::ostringstream ss;
        ss << "Found " << hits.size() << " keyword results for '" << query << "':\n";
        for (const auto& r : results_json) {
            int pct = static_cast<int>(r.value("relevance", 0.0f) * 100);
            ss << "[" << pct << "%] " << r.value("text", "").substr(0, 100) << "\n";
        }
        return DuckDBToolResult::ok(ss.str(), {{"results", results_json}});
    }

    DuckDBToolResult tool_hybrid_recall(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) return DuckDBToolResult::error("query is required");

        size_t limit      = static_cast<size_t>(params.value("limit", 10));
        std::string realm = params.value("realm", "");

        auto embedding = embed_query(query);
        if (embedding.empty()) return DuckDBToolResult::error("Failed to embed query");

        json semantic = hits_to_results_json(field_store_->recall(embedding, limit, realm));
        json keyword  = hits_to_results_json(field_store_->recall_keyword(query, limit));
        json merged   = merge_results(semantic, keyword);

        if (merged.size() > limit) merged.erase(merged.begin() + static_cast<int>(limit), merged.end());

        std::ostringstream ss;
        ss << "Hybrid recall: " << merged.size() << " results\n";
        for (const auto& r : merged) {
            int pct = static_cast<int>(r.value("relevance", 0.0f) * 100);
            ss << "[" << pct << "%] " << r.value("text", "").substr(0, 100) << "\n";
        }
        return DuckDBToolResult::ok(ss.str(), {{"results", merged}, {"realm", realm}});
    }

    DuckDBToolResult tool_smart_recall(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) return DuckDBToolResult::error("query is required");

        size_t limit       = static_cast<size_t>(params.value("limit", 20));
        std::string realm  = params.value("realm", "");

        // Classify query intent
        auto eq = expand_query(query);
        bool is_code = looks_like_code_query(query);

        json results;
        if (is_code) {
            results = hits_to_results_json(field_store_->recall_keyword(eq.lex, limit));
        } else {
            auto embedding = embed_query(eq.vec);
            if (embedding.empty()) return DuckDBToolResult::error("Failed to embed query");
            json semantic = hits_to_results_json(field_store_->recall(embedding, limit, realm));
            json keyword  = hits_to_results_json(field_store_->recall_keyword(eq.lex, limit));
            results = merge_results(semantic, keyword);
        }

        if (results.size() > limit) results.erase(results.begin() + static_cast<int>(limit), results.end());

        // Auto-expand top results
        size_t expand_top = static_cast<size_t>(params.value("expand_top", 2));
        if (expand_top > 0 && !results.empty()) {
            for (size_t i = 0; i < std::min(expand_top, results.size()); ++i) {
                std::string content = field_store_->get_content(
                    std::stoull(results[i].value("id", "0")));
                if (!content.empty()) {
                    results[i]["full_text"] = content;
                }
            }
        }

        std::ostringstream ss;
        ss << "Smart recall (" << (is_code ? "keyword" : "hybrid") << "): " << results.size() << " results\n";
        for (const auto& r : results) {
            int pct = static_cast<int>(r.value("relevance", 0.0f) * 100);
            ss << "[" << pct << "%] [" << r.value("type", "?") << "] "
               << r.value("text", "").substr(0, 100) << "\n";
        }
        return DuckDBToolResult::ok(ss.str(), {{"results", results}, {"intent", is_code ? "code" : "semantic"}});
    }

    // ── Iterative resonance helpers ────────────────────────────────────────

    static constexpr int    kMaxResonancePasses   = 3;
    static constexpr float  kResonanceAlpha       = 0.7f;
    static constexpr float  kEntropyStopDelta     = 0.01f;
    static constexpr float  kBaseAssocDelta       = 0.03f;
    static constexpr size_t kRetrievalCtxDims     = 32;

    static float score_entropy(const std::vector<std::pair<uint64_t, float>>& scored) {
        if (scored.empty()) return 0.0f;
        float sum = 0.0f;
        for (const auto& [id, s] : scored) sum += s;
        if (sum < 1e-9f) return 0.0f;
        float h = 0.0f;
        for (const auto& [id, s] : scored) {
            float p = s / sum;
            if (p > 1e-9f) h -= p * std::log2f(p);
        }
        return h;
    }

    static uint64_t query_context_hash(const std::vector<float>& q, const std::string& realm) {
        uint64_t h = 14695981039346656037ULL;
        for (float v : q) {
            uint32_t bits;
            std::memcpy(&bits, &v, sizeof(bits));
            h ^= bits;
            h *= 1099511628211ULL;
        }
        for (char c : realm) {
            h ^= static_cast<uint8_t>(c);
            h *= 1099511628211ULL;
        }
        return h;
    }

    static void project_to_sketch(const std::vector<float>& q,
                                   std::vector<int8_t>& out_q,
                                   float& out_scale) {
        out_q.resize(kRetrievalCtxDims);
        size_t stride = q.size() / kRetrievalCtxDims;
        if (stride == 0) stride = 1;
        float max_abs = 0.0f;
        std::vector<float> sketch(kRetrievalCtxDims, 0.0f);
        for (size_t i = 0; i < kRetrievalCtxDims && i * stride < q.size(); ++i) {
            float sum = 0.0f;
            int cnt = 0;
            for (size_t j = i * stride; j < std::min((i + 1) * stride, q.size()); ++j) {
                sum += q[j]; ++cnt;
            }
            sketch[i] = cnt > 0 ? sum / cnt : 0.0f;
            max_abs = std::max(max_abs, std::abs(sketch[i]));
        }
        out_scale = max_abs > 1e-9f ? max_abs / 127.0f : 1.0f;
        for (size_t i = 0; i < kRetrievalCtxDims; ++i) {
            out_q[i] = static_cast<int8_t>(std::clamp(sketch[i] / out_scale, -127.0f, 127.0f));
        }
    }

    DuckDBToolResult tool_full_resonate(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) return DuckDBToolResult::error("query is required");

        size_t k          = static_cast<size_t>(params.value("k", 10));
        std::string realm = params.value("realm", "");

        auto q0 = embed_query(query);
        if (q0.empty()) return DuckDBToolResult::error("Failed to embed query");

        auto query_embedding = q0;  // mutable copy refined each pass
        float prev_entropy = -1.0f;
        std::vector<uint64_t> prev_top_ids;
        json final_merged;
        int passes_run = 0;

        for (int pass = 0; pass < kMaxResonancePasses; ++pass) {
            passes_run = pass + 1;

            // Run existing resonance phases
            json semantic = hits_to_results_json(field_store_->recall(query_embedding, k, realm));
            json keyword  = hits_to_results_json(field_store_->recall_keyword(query, k));
            json merged   = merge_results(semantic, keyword);
            if (merged.size() > k) merged.erase(merged.begin() + static_cast<int>(k), merged.end());
            final_merged = merged;

            // Collect top-k IDs and scored pairs for entropy
            std::vector<uint64_t> cur_top_ids;
            std::vector<std::pair<uint64_t, float>> cur_scored;
            for (const auto& r : merged) {
                uint64_t mid = 0;
                try { mid = std::stoull(r.value("id", "0")); } catch (...) {}
                if (mid != 0) {
                    cur_top_ids.push_back(mid);
                    cur_scored.emplace_back(mid, r.value("relevance", 0.0f));
                }
            }

            // Early stop check
            float cur_entropy = score_entropy(cur_scored);
            bool ids_unchanged = (cur_top_ids == prev_top_ids);
            bool entropy_converged = (prev_entropy >= 0.0f &&
                                      std::abs(cur_entropy - prev_entropy) < kEntropyStopDelta);
            prev_entropy = cur_entropy;
            prev_top_ids = cur_top_ids;

            if (pass == kMaxResonancePasses - 1 || ids_unchanged || entropy_converged) {
                // Final pass — record recall batch for Hebbian learning
                if (!cur_top_ids.empty() && field_store_->handle()) {
                    std::vector<int8_t> centroid_q;
                    float centroid_scale;
                    project_to_sketch(q0, centroid_q, centroid_scale);
                    uint64_t ctx_hash = query_context_hash(q0, realm);
                    int64_t ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    cf_record_recall_batch(
                        field_store_->handle(),
                        cur_top_ids.data(), cur_top_ids.size(),
                        centroid_q.data(), centroid_q.size(),
                        centroid_scale,
                        ctx_hash,
                        ts_ms,
                        kBaseAssocDelta
                    );
                }
                break;
            }

            // Refine query embedding for next pass: fetch top-k embeddings
            if (!cur_top_ids.empty() && field_store_->handle()) {
                std::vector<char> emb_buf(cur_top_ids.size() * q0.size() * sizeof(float) + 4096);
                size_t emb_written = 0;
                int emb_rc = cf_get_memory_embeddings_batch(
                    field_store_->handle(),
                    cur_top_ids.data(), cur_top_ids.size(),
                    emb_buf.data(), emb_buf.size(), &emb_written);

                if (emb_rc == 0 && emb_written > 0) {
                    // Parse JSON result: array of {id, embedding: [f32...]}
                    auto emb_json = json::parse(
                        std::string_view(emb_buf.data(), emb_written), nullptr, false);
                    if (!emb_json.is_discarded() && emb_json.is_array() && !emb_json.empty()) {
                        // Compute mean embedding
                        std::vector<float> mean_emb(q0.size(), 0.0f);
                        size_t emb_count = 0;
                        for (const auto& entry : emb_json) {
                            if (entry.contains("embedding") && entry["embedding"].is_array()) {
                                const auto& evec = entry["embedding"];
                                if (evec.size() == q0.size()) {
                                    for (size_t d = 0; d < q0.size(); ++d) {
                                        mean_emb[d] += evec[d].get<float>();
                                    }
                                    ++emb_count;
                                }
                            }
                        }
                        if (emb_count > 0) {
                            for (float& v : mean_emb) v /= static_cast<float>(emb_count);
                            // Blend: q_next = alpha * q0 + (1-alpha) * mean
                            for (size_t d = 0; d < query_embedding.size(); ++d) {
                                query_embedding[d] = kResonanceAlpha * q0[d]
                                                   + (1.0f - kResonanceAlpha) * mean_emb[d];
                            }
                            // Normalize
                            float norm = 0.0f;
                            for (float v : query_embedding) norm += v * v;
                            norm = std::sqrt(norm);
                            if (norm > 1e-9f) {
                                for (float& v : query_embedding) v /= norm;
                            }
                        }
                    }
                }
            }
        }

        if (recall_callback_ && !prev_top_ids.empty()) {
            recall_callback_(prev_top_ids, passes_run);
        }

        std::ostringstream ss;
        ss << "Found " << final_merged.size() << " results (" << passes_run << " pass"
           << (passes_run > 1 ? "es" : "") << "):\n";
        for (const auto& r : final_merged) {
            int pct = static_cast<int>(r.value("relevance", 0.0f) * 100);
            ss << "[" << pct << "%] [" << r.value("type", "?") << "] "
               << r.value("text", "").substr(0, 100) << "\n";
        }
        return DuckDBToolResult::ok(ss.str(), {{"results", final_merged}, {"passes", passes_run}});
    }

    // ── Strength/forget ops ──────────────────────────────────────────────────

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
        float confidence = params.contains("confidence")
            ? params["confidence"].get<float>()
            : category_to_confidence(category);

        std::string ssl_content = to_ssl_format(content, category);

        auto embedding = embed_text(ssl_content);
        uint64_t id = field_store_->remember(category, "brahman", ssl_content, embedding, confidence, 0.001f);

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
        // Remove is a no-op for now (triplet removal not in FieldStore API)
        if (!rm_tag.empty()) {
            return DuckDBToolResult::ok("Tag removal not supported in chitta-field (stub)");
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
