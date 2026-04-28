// Included into FieldRpcHandler class body — declarations only.
// Bodies live in chitta/src/handlers/drift_consolidation.cpp.

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

        // Pairwise comparison with free-energy merge criterion (FEP §4.3).
        // Instead of pure cosine threshold, we check whether merging reduces
        // total free energy: F_merge = merge_loss + λ·complexity_gain < 0.
        // Falls back to cosine threshold when reconstruction error unavailable.
        const float fe_lambda = 0.5f;  // complexity penalty weight
        std::vector<DupPair> pairs;
        for (size_t i = 0; i < entries.size(); ++i) {
            for (size_t j = i + 1; j < entries.size(); ++j) {
                float sim = cosine_sim(entries[i].embedding, entries[j].embedding);
                if (sim < threshold * 0.8f) continue;  // fast reject

                // Try free-energy criterion
                float err_i = field_store_->reconstruction_error(entries[i].id);
                float err_j = field_store_->reconstruction_error(entries[j].id);

                bool should_merge;
                if (err_i >= 0.0f && err_j >= 0.0f) {
                    // Compute merged embedding (mean)
                    std::vector<float> merged(entries[i].embedding.size());
                    for (size_t d = 0; d < merged.size(); ++d) {
                        merged[d] = 0.5f * (entries[i].embedding[d] + entries[j].embedding[d]);
                    }
                    // Merge loss: how much reconstruction quality degrades
                    float avg_err = 0.5f * (err_i + err_j);
                    // Complexity gain: keeping two memories vs one
                    float complexity_gain = 1.0f;  // fixed cost of one extra memory
                    // Free energy: merge if the complexity saving outweighs accuracy loss
                    float free_energy = avg_err - fe_lambda * complexity_gain;
                    // Also require minimum cosine similarity
                    should_merge = (free_energy < 0.0f || sim >= threshold) && sim >= threshold * 0.9f;
                } else {
                    // Fallback: pure cosine threshold
                    should_merge = sim >= threshold;
                }

                if (should_merge) {
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


    // ── Tool: consolidate_similar ───────────────────────────────────────────

    ToolResult tool_find_near_duplicates(const json& params);
    ToolResult tool_consolidate_similar(const json& params);
