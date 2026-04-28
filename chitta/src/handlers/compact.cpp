// compact RPC handlers — bodies for declarations in
// chitta/include/chitta/rpc/handlers/compact.hpp.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

ToolResult FieldRpcHandler::tool_compact_context(const json& params) {
    if (!params.contains("messages") || !params["messages"].is_array())
        return ToolResult::error("messages (array) is required");

    const auto& messages = params["messages"];
    if (messages.empty())
        return ToolResult::ok("No messages to compact", {
            {"messages", json::array()},
            {"stats", {{"before_tokens",0},{"after_tokens",0},{"dropped",0},
                       {"dropped_pct",0.0},{"distilled",0},{"embedding",false}}}});

    const std::string query        = params.value("query", "");
    float target_ratio             = params.value("target_ratio", 0.4f);
    const bool distill_novel       = params.value("distill_novel", false);
    const float novel_threshold    = params.value("novel_threshold", 0.5f);
    const std::string distill_realm = params.value("distill_realm", "brahman");

    target_ratio = std::clamp(target_ratio, 0.05f, 1.0f);

    const bool has_embedder = (yantra_ != nullptr);

    // ── Helpers ──────────────────────────────────────────────────────────

    auto extract_content = [](const json& msg) -> std::string {
        if (!msg.contains("content")) return "";
        const auto& c = msg["content"];
        if (c.is_string()) return c.get<std::string>();
        if (c.is_array()) {
            std::string out;
            for (const auto& block : c) {
                if (block.is_object() && block.value("type","") == "text" && block.contains("text")) {
                    if (!out.empty()) out += ' ';
                    out += block["text"].get<std::string>();
                }
            }
            return out;
        }
        return "";
    };

    auto cosine_sim = [](const std::vector<float>& a, const std::vector<float>& b) -> float {
        if (a.size() != b.size() || a.empty()) return 0.0f;
        float dot = 0, na = 0, nb = 0;
        for (size_t k = 0; k < a.size(); ++k) {
            dot += a[k] * b[k];
            na  += a[k] * a[k];
            nb  += b[k] * b[k];
        }
        float denom = std::sqrt(na) * std::sqrt(nb);
        return (denom > 1e-9f) ? (dot / denom) : 0.0f;
    };

    auto structural_weight = [](const std::string& role) -> float {
        if (role == "system")                        return 2.0f;
        if (role == "user")                          return 1.0f;
        if (role == "assistant")                     return 0.8f;
        if (role == "tool" || role == "tool_result") return 0.5f;
        return 0.6f;
    };

    // ── Extract roles / content / tokens ─────────────────────────────────
    const size_t n = messages.size();
    std::vector<std::string> contents(n);
    std::vector<std::string> roles(n);
    std::vector<size_t>      token_counts(n);
    std::vector<bool>        is_system(n, false);
    size_t total_tokens  = 0;
    size_t system_tokens = 0;

    for (size_t i = 0; i < n; ++i) {
        contents[i]     = extract_content(messages[i]);
        roles[i]        = messages[i].value("role", "");
        token_counts[i] = estimate_tokens(contents[i]);
        total_tokens   += token_counts[i];
        if (roles[i] == "system") {
            is_system[i]    = true;
            system_tokens  += token_counts[i];
        }
    }

    // Already under budget?
    const size_t target_tokens = static_cast<size_t>(total_tokens * target_ratio);
    if (target_tokens >= total_tokens) {
        json stats = {{"before_tokens",total_tokens},{"after_tokens",total_tokens},
                      {"dropped",0},{"dropped_pct",0.0},{"distilled",0},{"embedding",has_embedder}};
        return ToolResult::ok("Already within budget", {{"messages",messages},{"stats",stats}});
    }

    // ── Pre-embed all non-system messages (once, reused for both scoring + distillation) ──
    std::vector<std::vector<float>> embeddings(n);
    if (has_embedder) {
        for (size_t i = 0; i < n; ++i) {
            if (!is_system[i] && !contents[i].empty())
                embeddings[i] = embed_text(contents[i]);
        }
    }

    // Embed query once
    std::vector<float> query_emb;
    if (has_embedder && !query.empty())
        query_emb = embed_query(query);

    // ── Score each message ────────────────────────────────────────────────
    // memory_coverages stored so distill pass can reuse without re-querying
    std::vector<float> scores(n, 0.0f);
    std::vector<float> memory_coverages(n, 0.0f);

    size_t non_sys_total = 0;
    for (size_t i = 0; i < n; ++i)
        if (!is_system[i]) ++non_sys_total;

    size_t non_sys_idx = 0;
    for (size_t i = 0; i < n; ++i) {
        if (is_system[i]) { scores[i] = 1e9f; continue; }

        const float recency = (non_sys_total <= 1) ? 1.0f
            : 0.1f + 0.9f * (static_cast<float>(non_sys_idx) /
                              static_cast<float>(non_sys_total - 1));
        ++non_sys_idx;

        float semantic_sim = 0.5f;
        if (!query_emb.empty() && !embeddings[i].empty())
            semantic_sim = cosine_sim(embeddings[i], query_emb);

        float coverage = 0.0f;
        if (has_embedder && !embeddings[i].empty() && field_store_) {
            for (const auto& h : field_store_->recall(embeddings[i], 3))
                coverage = std::max(coverage, h.score);
        }
        memory_coverages[i] = coverage;

        const float sw = structural_weight(roles[i]);
        scores[i] = sw * (0.3f * recency + 0.4f * semantic_sim + 0.3f * (1.0f - coverage));
    }

    // ── Greedy keep ───────────────────────────────────────────────────────
    std::vector<size_t> indices(n);
    std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
        return scores[a] > scores[b];
    });

    const size_t budget = (target_tokens > system_tokens) ? (target_tokens - system_tokens) : 0;
    size_t kept_non_sys_tokens = 0;
    std::vector<bool> keep(n, false);
    for (size_t idx : indices) {
        if (is_system[idx]) { keep[idx] = true; continue; }
        if (kept_non_sys_tokens + token_counts[idx] <= budget) {
            keep[idx] = true;
            kept_non_sys_tokens += token_counts[idx];
        }
    }

    // ── distill_novel pass ────────────────────────────────────────────────
    // For messages that: (a) will be dropped, (b) have novel content
    // (memory_coverage < novel_threshold), distill into a soul memory
    // before discarding the verbatim text.
    int         distilled_count = 0;
    json        distilled_kinds = json::object();

    if (distill_novel && has_embedder && field_store_) {

        // Kind classifier: lightweight substring heuristic, no regex overhead
        auto classify_kind = [](const std::string& role, const std::string& text) -> std::string {
            // Helper: case-insensitive substring search
            auto contains_ci = [](const std::string& hay, const char* needle) -> bool {
                std::string lower = hay;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                return lower.find(needle) != std::string::npos;
            };

            // Correction — user/assistant pointing out errors
            if (contains_ci(text, "wrong") || contains_ci(text, "mistake") ||
                contains_ci(text, "incorrect") || contains_ci(text, "that's not") ||
                contains_ci(text, "not what i") || contains_ci(text, "should be") ||
                contains_ci(text, "actually, "))
                return "correction";

            // Preference — explicit behavioural instructions
            if (contains_ci(text, "i prefer") || contains_ci(text, "i always") ||
                contains_ci(text, "please always") || contains_ci(text, "please don't") ||
                contains_ci(text, "from now on") || contains_ci(text, "in the future") ||
                contains_ci(text, "never do") || contains_ci(text, "stop doing"))
                return "preference";

            // Habit — recurring patterns / learned workflows
            if (contains_ci(text, "every time") || contains_ci(text, "whenever") ||
                contains_ci(text, "habit") || contains_ci(text, "pattern") ||
                contains_ci(text, "convention") || contains_ci(text, "standard"))
                return "habit";

            // Wisdom — assistant technical reasoning (heuristic: assistant + knowledge markers)
            if (role == "assistant") {
                if (contains_ci(text, "the reason") || contains_ci(text, "because") ||
                    contains_ci(text, "the fix") || contains_ci(text, "the way to") ||
                    contains_ci(text, "the correct") || contains_ci(text, "you should") ||
                    contains_ci(text, "best practice") || contains_ci(text, "important:"))
                    return "wisdom";
            }

            return "episode";
        };

        for (size_t i = 0; i < n; ++i) {
            if (keep[i] || is_system[i]) continue;

            // Skip turns that are too short to carry meaningful knowledge
            if (contents[i].size() < 40) continue;

            // Skip turns whose content is already in memory
            if (memory_coverages[i] >= novel_threshold) continue;

            // This is a novel drop — distill it
            const std::string kind = classify_kind(roles[i], contents[i]);

            // Prefix with provenance so distilled memories are traceable
            std::string mem_content = "[distill-on-drop:" + roles[i] + "] "
                + contents[i].substr(0, 600);

            // Confidence proportional to novelty: more novel = more confident the
            // soul hasn't captured this yet and it's worth keeping
            const float confidence = 0.5f + 0.25f * (1.0f - memory_coverages[i]);

            // Store: use cached embedding; decay_rate matches episode default
            field_store_->remember(kind, distill_realm, mem_content,
                                   embeddings[i], confidence, 0.001f);

            ++distilled_count;
            distilled_kinds[kind] = distilled_kinds.value(kind, 0) + 1;
        }
    }

    // ── Assemble output (original order) ─────────────────────────────────
    json kept_messages = json::array();
    for (size_t i = 0; i < n; ++i)
        if (keep[i]) kept_messages.push_back(messages[i]);

    const size_t dropped     = n - kept_messages.size();
    const size_t after_tokens = system_tokens + kept_non_sys_tokens;
    const double dropped_pct  = (n > 0) ? (static_cast<double>(dropped) / n * 100.0) : 0.0;

    json stats = {
        {"before_tokens", total_tokens},
        {"after_tokens",  after_tokens},
        {"dropped",       dropped},
        {"dropped_pct",   dropped_pct},
        {"distilled",     distilled_count},
        {"distilled_kinds", distilled_kinds},
        {"embedding",     has_embedder}
    };

    std::ostringstream ss;
    ss << "Compacted " << total_tokens << " → " << after_tokens << " tok ("
       << dropped << " dropped";
    if (distilled_count > 0)
        ss << ", " << distilled_count << " distilled into memory";
    ss << ", " << std::fixed << std::setprecision(1) << dropped_pct << "%)";

    return ToolResult::ok(ss.str(), {{"messages", kept_messages}, {"stats", stats}});
}

} // namespace chitta
