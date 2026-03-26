// Included into FieldRpcHandler class body — not a standalone header.
// Memory recall tools: remember, recall (semantic/temporal/keyword/hybrid/smart/full_resonate).

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

        // Hebbian co-occurrence: strengthen associations between co-retrieved memories
        if (hits.size() >= 2) {
            std::vector<uint64_t> ids;
            ids.reserve(hits.size());
            for (const auto& h : hits) ids.push_back(h.memory_id);
            field_store_->record_co_retrieval(ids);
        }

        // Drift scoring: anti-perseveration penalty + curiosity boost
        {
            const size_t total = field_store_->memory_count();
            if (total >= 5 && !hits.empty()) {
                const float max_access = 10.0f;
                const float exploration = 1.0f;
                for (auto& h : hits) {
                    float saturation = std::min(1.0f, static_cast<float>(h.access_count) / max_access);
                    float anti_perserv = 1.0f - 0.25f * saturation;
                    float curiosity = (total > 0)
                        ? exploration * std::sqrt(std::log(static_cast<float>(total) + 1.0f) /
                                                  (static_cast<float>(h.access_count) + 1.0f))
                        : 0.0f;
                    curiosity = std::min(curiosity, 0.3f);
                    h.score = h.score * anti_perserv + curiosity;
                }
                std::sort(hits.begin(), hits.end(),
                          [](const FieldRecallHit& a, const FieldRecallHit& b) {
                              return a.score > b.score;
                          });
            }
        }

        json results_json = hits_to_results_json(hits);

        std::ostringstream ss;
        ss << "Found " << hits.size() << " results";
        if (!realm.empty()) ss << " in realm '" << realm << "'";
        ss << ":\n";
        for (const auto& r : results_json) {
            int pct = static_cast<int>(r.value("relevance", 0.0f) * 100);
            ss << "[" << pct << "%] [" << r.value("type", "?") << "] "
               << r.value("text", "").substr(0, 400) << "\n";
        }

        auto result = DuckDBToolResult::ok(ss.str(), {{"results", results_json}, {"realm", realm}});
        fire_recall_callback(results_json, 1);
        return result;
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
                    ss << "[" << r.value("type", "?") << "] " << r.value("text", "").substr(0, 400) << "\n";
                }
                return DuckDBToolResult::ok(ss.str(), {{"results", merged}, {"count", merged.size()}, {"realm", realm}});
            }
        }

        json results_json = hits_to_results_json(hits);
        std::ostringstream ss;
        ss << "Found " << hits.size() << " memories:\n";
        for (const auto& r : results_json) {
            ss << "[" << r.value("type", "?") << "] " << r.value("text", "").substr(0, 400) << "\n";
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
            ss << "[" << pct << "%] " << r.value("text", "").substr(0, 400) << "\n";
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

        auto semantic_hits = field_store_->recall(embedding, limit, realm);
        auto keyword_hits  = field_store_->recall_keyword(query, limit);

        // Drift scoring on each source's raw hits (no Hebbian — merged sources would over-count)
        {
            const size_t total = field_store_->memory_count();
            if (total >= 5) {
                auto apply_drift = [&](std::vector<FieldRecallHit>& hits) {
                    const float max_access = 10.0f;
                    const float exploration = 1.0f;
                    for (auto& h : hits) {
                        float saturation = std::min(1.0f, static_cast<float>(h.access_count) / max_access);
                        float anti_perserv = 1.0f - 0.25f * saturation;
                        float curiosity = exploration * std::sqrt(std::log(static_cast<float>(total) + 1.0f) /
                                                                  (static_cast<float>(h.access_count) + 1.0f));
                        curiosity = std::min(curiosity, 0.3f);
                        h.score = h.score * anti_perserv + curiosity;
                    }
                };
                apply_drift(semantic_hits);
                apply_drift(keyword_hits);
            }
        }

        json semantic = hits_to_results_json(semantic_hits);
        json keyword  = hits_to_results_json(keyword_hits);
        json merged   = merge_results(semantic, keyword);

        if (merged.size() > limit) merged.erase(merged.begin() + static_cast<int>(limit), merged.end());

        std::ostringstream ss;
        ss << "Hybrid recall: " << merged.size() << " results\n";
        for (const auto& r : merged) {
            int pct = static_cast<int>(r.value("relevance", 0.0f) * 100);
            ss << "[" << pct << "%] " << r.value("text", "").substr(0, 400) << "\n";
        }
        auto result = DuckDBToolResult::ok(ss.str(), {{"results", merged}, {"realm", realm}});
        fire_recall_callback(merged, 1);
        return result;
    }

    DuckDBToolResult tool_smart_recall(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) return DuckDBToolResult::error("query is required");

        size_t limit       = static_cast<size_t>(params.value("limit", 20));
        std::string realm  = params.value("realm", "");

        // Ask route learner for best retrieval strategy
        auto route_sel = field_store_->select_route(query);
        uint64_t episode_id = route_sel.episode_id;
        uint8_t  route      = route_sel.route;
        // 0=Semantic, 1=Keyword, 2=Temporal, 3=Artifact, 4=Hybrid, 5=Full

        auto eq = expand_query(query);
        bool is_code = looks_like_code_query(query);
        if (is_code) route = 1;  // code queries always keyword

        // Drift scoring lambda
        auto apply_drift_smart = [&](std::vector<FieldRecallHit>& hits) {
            const size_t total = field_store_->memory_count();
            if (total < 5 || hits.empty()) return;
            const float max_access = 10.0f;
            const float exploration = 1.0f;
            for (auto& h : hits) {
                float saturation = std::min(1.0f, static_cast<float>(h.access_count) / max_access);
                float anti_perserv = 1.0f - 0.25f * saturation;
                float curiosity = exploration * std::sqrt(std::log(static_cast<float>(total) + 1.0f) /
                                                          (static_cast<float>(h.access_count) + 1.0f));
                curiosity = std::min(curiosity, 0.3f);
                h.score = h.score * anti_perserv + curiosity;
            }
        };

        json results;
        static const char* route_names[] = {"semantic","keyword","temporal","artifact","hybrid","full"};
        std::string route_name = route < 6 ? route_names[route] : "hybrid";

        if (route == 1) {  // Keyword
            auto kw_hits = field_store_->recall_keyword(eq.lex, limit);
            apply_drift_smart(kw_hits);
            results = hits_to_results_json(kw_hits);
        } else if (route == 2) {  // Temporal
            int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            auto temp_hits = field_store_->recall_temporal(now_ms - (int64_t)30*24*3600*1000, now_ms, limit, realm);
            apply_drift_smart(temp_hits);
            results = hits_to_results_json(temp_hits);
        } else {  // Semantic / Hybrid / Full / Artifact — all use semantic+keyword merge
            auto embedding = embed_query(eq.vec);
            if (embedding.empty()) {
                // Fallback to keyword if embedding fails
                auto kw_hits = field_store_->recall_keyword(eq.lex, limit);
                apply_drift_smart(kw_hits);
                results = hits_to_results_json(kw_hits);
                field_store_->route_feedback(episode_id, -0.1f);  // slight penalty for forced fallback
                episode_id = 0;  // skip normal feedback
            } else {
                auto sem_hits = field_store_->recall(embedding, limit, realm);
                if (route == 0) {  // Semantic only
                    apply_drift_smart(sem_hits);
                    results = hits_to_results_json(sem_hits);
                } else {  // Hybrid / Full
                    auto kw_hits = field_store_->recall_keyword(eq.lex, limit);
                    apply_drift_smart(sem_hits);
                    apply_drift_smart(kw_hits);
                    results = merge_results(hits_to_results_json(sem_hits), hits_to_results_json(kw_hits));
                }
            }
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
        ss << "Smart recall (" << route_name << ", ep=" << episode_id << "): " << results.size() << " results\n";
        for (const auto& r : results) {
            int pct = static_cast<int>(r.value("relevance", 0.0f) * 100);
            ss << "[" << pct << "%] [" << r.value("type", "?") << "] "
               << r.value("text", "").substr(0, 400) << "\n";
        }
        auto result = DuckDBToolResult::ok(ss.str(), {{"results", results}, {"intent", is_code ? "code" : "semantic"}});
        fire_recall_callback(results, 1);
        return result;
    }

    void fire_recall_callback(const json& results, int passes) {
        if (!recall_callback_ || !results.is_array() || results.empty()) return;
        std::vector<uint64_t> ids;
        ids.reserve(results.size());
        for (const auto& r : results) {
            std::string id_str = r.value("id", "");
            if (!id_str.empty()) {
                try { ids.push_back(std::stoull(id_str)); } catch (...) {}
            }
        }
        if (!ids.empty()) recall_callback_(ids, passes);
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
               << r.value("text", "").substr(0, 400) << "\n";
        }
        return DuckDBToolResult::ok(ss.str(), {{"results", final_merged}, {"passes", passes_run}});
    }

    // ── Strength/forget ops ──────────────────────────────────────────────────

