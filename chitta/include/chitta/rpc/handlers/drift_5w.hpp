// Included into FieldRpcHandler class body — not a standalone header.
// 5W dimensional search + UCB1 exploration recall (drift-memory features 5 & 6).
//
// Tools:
//   5w_search    — multi-dimensional semantic search across who/what/when/where/why
//   recall_ucb1  — recall with UCB1 exploration bonus to surface novel memories

#include <cmath>

    DuckDBToolResult tool_5w_search(const json& params) {
        std::string realm = params.value("realm", "");
        size_t limit = static_cast<size_t>(params.value("limit", 10));

        // Collect non-empty dimensions
        std::vector<std::pair<std::string, std::string>> dimensions;
        for (const auto& dim : {"who", "what", "when", "where", "why"}) {
            std::string val = params.value(dim, "");
            if (!val.empty()) {
                dimensions.emplace_back(dim, val);
            }
        }

        if (dimensions.empty()) {
            return DuckDBToolResult::error("at least one of who/what/when/where/why must be provided");
        }

        // Merge map: memory_id -> {best_score, result_json, matched_dimensions}
        struct MergedEntry {
            float best_score = 0.0f;
            json result;
            std::vector<std::string> matched_dims;
        };
        std::unordered_map<uint64_t, MergedEntry> merged;

        for (const auto& [label, query] : dimensions) {
            auto embedding = embed_query(query);
            if (embedding.empty()) continue;

            auto hits = field_store_->recall(embedding, limit * 2, realm);
            for (const auto& h : hits) {
                auto& entry = merged[h.memory_id];
                entry.matched_dims.push_back(label);
                if (h.score > entry.best_score) {
                    entry.best_score = h.score;
                    entry.result = {
                        {"id",         std::to_string(h.memory_id)},
                        {"relevance",  h.score},
                        {"similarity", h.semantic_score},
                        {"type",       h.kind.empty() ? "episode" : h.kind},
                        {"text",       h.content},
                        {"realm",      h.realm},
                        {"confidence", h.confidence},
                    };
                }
            }
        }

        // Sort by merged score, take top limit
        std::vector<std::pair<uint64_t, MergedEntry>> sorted(merged.begin(), merged.end());
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second.best_score > b.second.best_score; });

        json arr = json::array();
        size_t count = 0;
        for (const auto& [mid, entry] : sorted) {
            if (count >= limit) break;
            json r = entry.result;
            r["dimensions"] = entry.matched_dims;
            arr.push_back(r);
            ++count;
        }

        std::ostringstream ss;
        ss << "5W search: " << arr.size() << " results across "
           << dimensions.size() << " dimension(s):\n";
        for (const auto& r : arr) {
            int pct = static_cast<int>(r.value("relevance", 0.0f) * 100);
            ss << "[" << pct << "%] [" << r.value("type", "?") << "] "
               << r.value("text", "").substr(0, 100) << "\n";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"results", arr},
            {"dimensions_queried", dimensions.size()}
        });
    }

    DuckDBToolResult tool_recall_ucb1(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) return DuckDBToolResult::error("query is required");

        std::string realm = params.value("realm", "");
        size_t limit = static_cast<size_t>(params.value("limit", 10));
        float exploration = params.value("exploration", 1.414f);
        size_t fetch_k = static_cast<size_t>(params.value("fetch_k", 40));

        auto embedding = embed_query(query);
        if (embedding.empty()) return DuckDBToolResult::error("Failed to embed query");

        auto hits = field_store_->recall(embedding, fetch_k, realm);
        size_t total = field_store_->memory_count();

        // Compute UCB1 scores and sort
        struct ScoredHit {
            FieldRecallHit hit;
            float ucb1_score;
            uint64_t access_count;
        };
        std::vector<ScoredHit> scored;
        scored.reserve(hits.size());

        for (auto& h : hits) {
            uint64_t ac = h.access_count;
            float bonus = exploration * std::sqrt(std::log(static_cast<double>(total) + 1.0) /
                                                  (static_cast<double>(ac) + 1.0));
            float ucb1 = h.score + bonus;
            scored.push_back({std::move(h), ucb1, ac});
        }

        std::sort(scored.begin(), scored.end(),
            [](const auto& a, const auto& b) { return a.ucb1_score > b.ucb1_score; });

        json arr = json::array();
        std::ostringstream ss;
        ss << "UCB1 recall: ";

        size_t count = 0;
        for (const auto& s : scored) {
            if (count >= limit) break;
            const auto& h = s.hit;
            json r = {
                {"id",           std::to_string(h.memory_id)},
                {"relevance",    h.score},
                {"similarity",   h.semantic_score},
                {"type",         h.kind.empty() ? "episode" : h.kind},
                {"text",         h.content},
                {"realm",        h.realm},
                {"confidence",   h.confidence},
                {"ucb1_score",   s.ucb1_score},
                {"access_count", s.access_count},
            };
            arr.push_back(r);
            ++count;
        }

        ss << arr.size() << " results (exploration=" << exploration << "):\n";
        for (const auto& r : arr) {
            int pct = static_cast<int>(r.value("ucb1_score", 0.0f) * 100);
            ss << "[ucb1:" << pct << "%] [" << r.value("type", "?") << "] "
               << r.value("text", "").substr(0, 100) << "\n";
        }

        return DuckDBToolResult::ok(ss.str(), {{"results", arr}, {"total_memories", total}});
    }
