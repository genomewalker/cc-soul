// Included into FieldRpcHandler class body — not a standalone header.
// Context compaction: scores conversation messages and keeps the most valuable ones.

#include <algorithm>
#include <numeric>
#include <sstream>
#include <cmath>

    DuckDBToolResult tool_compact_context(const json& params) {
        if (!params.contains("messages") || !params["messages"].is_array())
            return DuckDBToolResult::error("messages (array) is required");

        const auto& messages = params["messages"];
        if (messages.empty())
            return DuckDBToolResult::ok("No messages to compact", {{"messages", json::array()}, {"stats", {{"before_tokens", 0}, {"after_tokens", 0}, {"dropped", 0}, {"dropped_pct", 0.0}, {"embedding", false}}}});

        std::string query = params.value("query", "");
        float target_ratio = params.value("target_ratio", 0.4f);
        target_ratio = std::clamp(target_ratio, 0.05f, 1.0f);

        bool has_embedder = (yantra_ != nullptr);

        // Extract text content from a message's content field (string or array of blocks)
        auto extract_content = [](const json& msg) -> std::string {
            if (!msg.contains("content")) return "";
            const auto& c = msg["content"];
            if (c.is_string()) return c.get<std::string>();
            if (c.is_array()) {
                std::string out;
                for (const auto& block : c) {
                    if (block.is_object() && block.value("type", "") == "text" && block.contains("text")) {
                        if (!out.empty()) out += ' ';
                        out += block["text"].get<std::string>();
                    }
                }
                return out;
            }
            return "";
        };

        // Token estimation: whitespace-separated words * 1.3
        auto estimate_tokens = [](const std::string& text) -> size_t {
            if (text.empty()) return 0;
            size_t words = 0;
            bool in_word = false;
            for (char ch : text) {
                if (std::isspace(static_cast<unsigned char>(ch))) {
                    in_word = false;
                } else if (!in_word) {
                    in_word = true;
                    ++words;
                }
            }
            return static_cast<size_t>(words * 1.3);
        };

        auto cosine_sim = [](const std::vector<float>& a, const std::vector<float>& b) -> float {
            if (a.size() != b.size() || a.empty()) return 0.0f;
            float dot = 0.0f, na = 0.0f, nb = 0.0f;
            for (size_t i = 0; i < a.size(); ++i) {
                dot += a[i] * b[i];
                na  += a[i] * a[i];
                nb  += b[i] * b[i];
            }
            float denom = std::sqrt(na) * std::sqrt(nb);
            return (denom > 1e-9f) ? (dot / denom) : 0.0f;
        };

        // Structural weight by role
        auto structural_weight = [](const std::string& role) -> float {
            if (role == "system")      return 2.0f;
            if (role == "user")        return 1.0f;
            if (role == "assistant")   return 0.8f;
            if (role == "tool" || role == "tool_result") return 0.5f;
            return 0.6f;
        };

        // Pre-extract content and roles, estimate tokens
        size_t n = messages.size();
        std::vector<std::string> contents(n);
        std::vector<std::string> roles(n);
        std::vector<size_t> token_counts(n);
        std::vector<bool> is_system(n, false);
        size_t total_tokens = 0;
        size_t system_tokens = 0;

        for (size_t i = 0; i < n; ++i) {
            contents[i] = extract_content(messages[i]);
            roles[i] = messages[i].value("role", "");
            token_counts[i] = estimate_tokens(contents[i]);
            total_tokens += token_counts[i];
            if (roles[i] == "system") {
                is_system[i] = true;
                system_tokens += token_counts[i];
            }
        }

        // Already under budget? Return everything.
        size_t target_tokens = static_cast<size_t>(total_tokens * target_ratio);
        if (target_tokens >= total_tokens) {
            json stats = {
                {"before_tokens", total_tokens},
                {"after_tokens", total_tokens},
                {"dropped", 0},
                {"dropped_pct", 0.0},
                {"embedding", has_embedder}
            };
            return DuckDBToolResult::ok("Already within budget", {{"messages", messages}, {"stats", stats}});
        }

        // Pre-embed all non-system messages
        std::vector<std::vector<float>> embeddings(n);
        if (has_embedder) {
            for (size_t i = 0; i < n; ++i) {
                if (!is_system[i] && !contents[i].empty()) {
                    embeddings[i] = embed_text(contents[i]);
                }
            }
        }

        // Embed the query once
        std::vector<float> query_emb;
        if (has_embedder && !query.empty()) {
            query_emb = embed_query(query);
        }

        // Count non-system messages for recency scaling
        size_t non_system_count = 0;
        for (size_t i = 0; i < n; ++i) {
            if (!is_system[i]) ++non_system_count;
        }

        // Score each message
        std::vector<float> scores(n, 0.0f);
        size_t non_sys_idx = 0;
        for (size_t i = 0; i < n; ++i) {
            if (is_system[i]) {
                scores[i] = 1e9f;
                continue;
            }

            float recency = (non_system_count <= 1) ? 1.0f
                : 0.1f + 0.9f * (static_cast<float>(non_sys_idx) / static_cast<float>(non_system_count - 1));
            ++non_sys_idx;

            float semantic_sim = 0.5f;
            if (!query_emb.empty() && !embeddings[i].empty()) {
                semantic_sim = cosine_sim(embeddings[i], query_emb);
            }

            float memory_coverage = 0.0f;
            if (has_embedder && !embeddings[i].empty() && field_store_) {
                auto hits = field_store_->recall(embeddings[i], 3);
                for (const auto& h : hits) {
                    memory_coverage = std::max(memory_coverage, h.score);
                }
            }

            float sw = structural_weight(roles[i]);
            scores[i] = sw * (0.3f * recency + 0.4f * semantic_sim + 0.3f * (1.0f - memory_coverage));
        }

        // Greedy keep: sort indices by score descending, accumulate until budget met
        std::vector<size_t> indices(n);
        std::iota(indices.begin(), indices.end(), 0);
        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
            return scores[a] > scores[b];
        });

        // System messages always kept; compute non-system budget
        size_t budget = (target_tokens > system_tokens) ? (target_tokens - system_tokens) : 0;
        size_t kept_non_system_tokens = 0;
        std::vector<bool> keep(n, false);

        for (size_t idx : indices) {
            if (is_system[idx]) {
                keep[idx] = true;
                continue;
            }
            if (kept_non_system_tokens + token_counts[idx] <= budget) {
                keep[idx] = true;
                kept_non_system_tokens += token_counts[idx];
            }
        }

        // Re-sort kept messages by original index to preserve conversation order
        json kept_messages = json::array();
        size_t after_tokens = system_tokens + kept_non_system_tokens;
        for (size_t i = 0; i < n; ++i) {
            if (keep[i]) kept_messages.push_back(messages[i]);
        }

        size_t dropped = n - kept_messages.size();
        double dropped_pct = (n > 0) ? (static_cast<double>(dropped) / static_cast<double>(n) * 100.0) : 0.0;

        json stats = {
            {"before_tokens", total_tokens},
            {"after_tokens", after_tokens},
            {"dropped", dropped},
            {"dropped_pct", dropped_pct},
            {"embedding", has_embedder}
        };

        std::ostringstream ss;
        ss << "Compacted " << total_tokens << " -> " << after_tokens << " tokens ("
           << dropped << " messages dropped, " << std::fixed << std::setprecision(1) << dropped_pct << "%)";

        return DuckDBToolResult::ok(ss.str(), {{"messages", kept_messages}, {"stats", stats}});
    }
