// Included into FieldRpcHandler class body — not a standalone header.
// Sleep consolidation: find and merge near-duplicate memories.
//
// Tools:
//   find_near_duplicates  — list memory pairs with high semantic similarity
//   consolidate_similar   — merge near-duplicate memories (soft-delete weaker)

    // ── Private helper: cosine similarity between two vectors ────────────────

    static float cosine_sim(const std::vector<float>& a, const std::vector<float>& b) {
        if (a.size() != b.size() || a.empty()) return 0.0f;
        float dot = 0.0f, na = 0.0f, nb = 0.0f;
        for (size_t i = 0; i < a.size(); ++i) {
            dot += a[i] * b[i];
            na  += a[i] * a[i];
            nb  += b[i] * b[i];
        }
        float denom = std::sqrt(na) * std::sqrt(nb);
        return denom > 1e-9f ? dot / denom : 0.0f;
    }

    // ── Private helper: find duplicate pairs from a set of memories ─────────

    struct DupPair {
        uint64_t    a_id;
        uint64_t    b_id;
        float       similarity;
        std::string a_preview;
        std::string b_preview;
        float       a_score;   // confidence * strength
        float       b_score;
    };

    std::vector<DupPair> find_dup_pairs(const std::string& realm, size_t candidate_limit,
                                         float threshold) {
        // Fetch recent memories via temporal recall (broad window)
        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        int64_t start_ms = 0;  // all time

        auto hits = field_store_->recall_temporal(start_ms, now_ms, candidate_limit, realm);
        if (hits.size() < 2) return {};

        // Embed each memory's content and cache
        struct MemEntry {
            uint64_t    id;
            std::string content;
            float       confidence;
            float       strength;
            std::vector<float> embedding;
        };
        std::vector<MemEntry> entries;
        entries.reserve(hits.size());

        for (const auto& h : hits) {
            if (h.content.empty()) continue;
            auto emb = embed_text(h.content);
            if (emb.empty()) continue;
            entries.push_back({h.memory_id, h.content, h.confidence, h.strength, std::move(emb)});
        }

        // Pairwise comparison — O(n^2) but n <= candidate_limit (small)
        std::vector<DupPair> pairs;
        for (size_t i = 0; i < entries.size(); ++i) {
            for (size_t j = i + 1; j < entries.size(); ++j) {
                float sim = cosine_sim(entries[i].embedding, entries[j].embedding);
                if (sim >= threshold) {
                    pairs.push_back({
                        entries[i].id, entries[j].id, sim,
                        entries[i].content.substr(0, 100),
                        entries[j].content.substr(0, 100),
                        entries[i].confidence * entries[i].strength,
                        entries[j].confidence * entries[j].strength,
                    });
                }
            }
        }

        // Sort by similarity descending
        std::sort(pairs.begin(), pairs.end(),
            [](const DupPair& a, const DupPair& b) { return a.similarity > b.similarity; });

        return pairs;
    }

    // ── Tool: find_near_duplicates ──────────────────────────────────────────

    DuckDBToolResult tool_find_near_duplicates(const json& params) {
        std::string realm = params.value("realm", "");
        size_t limit      = static_cast<size_t>(params.value("limit", 20));
        float threshold   = params.value("threshold", 0.90f);

        auto pairs = find_dup_pairs(realm, 200, threshold);
        if (pairs.size() > limit) pairs.resize(limit);

        json pairs_json = json::array();
        for (const auto& p : pairs) {
            pairs_json.push_back({
                {"a_id",       std::to_string(p.a_id)},
                {"b_id",       std::to_string(p.b_id)},
                {"similarity", p.similarity},
                {"a_preview",  p.a_preview},
                {"b_preview",  p.b_preview},
            });
        }

        std::ostringstream ss;
        ss << pairs.size() << " near-duplicate pair(s) found (threshold=" << threshold << "):\n";
        for (const auto& p : pairs) {
            int pct = static_cast<int>(p.similarity * 100);
            ss << "  [" << pct << "%] #" << p.a_id << " <-> #" << p.b_id << "\n";
        }

        return DuckDBToolResult::ok(ss.str(),
            {{"pairs", pairs_json}, {"count", pairs.size()}, {"threshold", threshold}});
    }

    // ── Tool: consolidate_similar ───────────────────────────────────────────

    DuckDBToolResult tool_consolidate_similar(const json& params) {
        std::string realm = params.value("realm", "");
        float threshold   = params.value("threshold", 0.92f);
        bool dry_run      = params.value("dry_run", true);
        size_t limit      = static_cast<size_t>(params.value("limit", 10));

        auto pairs = find_dup_pairs(realm, 200, threshold);
        if (pairs.size() > limit) pairs.resize(limit);

        size_t merged = 0;
        json merged_pairs = json::array();

        // Track already-processed IDs to avoid double-deleting
        std::unordered_set<uint64_t> processed;

        for (const auto& p : pairs) {
            if (processed.count(p.a_id) || processed.count(p.b_id)) continue;

            // Keep the stronger memory, forget the weaker
            uint64_t kept_id, weaker_id;
            if (p.a_score >= p.b_score) {
                kept_id   = p.a_id;
                weaker_id = p.b_id;
            } else {
                kept_id   = p.b_id;
                weaker_id = p.a_id;
            }

            json pair_info = {
                {"kept_id",    std::to_string(kept_id)},
                {"removed_id", std::to_string(weaker_id)},
                {"similarity", p.similarity},
            };

            if (!dry_run) {
                field_store_->forget(weaker_id);
                field_store_->strengthen(kept_id, 0.1f);

                // Record consolidation as triplet for audit
                field_store_->add_triplet(
                    std::to_string(kept_id), "consolidated_from", std::to_string(weaker_id));
            }

            processed.insert(weaker_id);
            merged_pairs.push_back(pair_info);
            ++merged;
        }

        std::ostringstream ss;
        if (dry_run) {
            ss << "[dry_run] Would merge " << merged << " pair(s):\n";
        } else {
            ss << "Merged " << merged << " pair(s):\n";
        }
        for (const auto& mp : merged_pairs) {
            ss << "  #" << mp.value("kept_id", "?") << " <- #" << mp.value("removed_id", "?")
               << " (sim=" << mp.value("similarity", 0.0f) << ")\n";
        }

        return DuckDBToolResult::ok(ss.str(),
            {{"merged", merged}, {"pairs", merged_pairs}, {"dry_run", dry_run},
             {"threshold", threshold}});
    }
