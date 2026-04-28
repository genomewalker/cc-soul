// trajectory_compact RPC handlers — bodies for declarations in
// chitta/include/chitta/rpc/handlers/trajectory_compact.hpp.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

ToolResult FieldRpcHandler::tool_trajectory_compact(const json& params) {
    const std::string task = params.value("task", "");
    if (task.empty())
        return ToolResult::error("task (string) is required — describe what the downstream agent needs");

    std::string path       = params.value("path", "");
    std::string session_id = params.value("session_id", "");
    size_t budget_tokens   = static_cast<size_t>(params.value("budget_tokens", 4000));
    float mad_k            = params.value("mad_k", 1.5f);
    bool include_system    = params.value("include_system", false);
    std::string role_filter = params.value("role_filter", "");

    // ── Resolve transcript path ──────────────────────────────────────────
    if (path.empty() && !session_id.empty()) {
        const char* home_cstr = std::getenv("HOME");
        if (home_cstr) {
            std::string pattern = std::string(home_cstr)
                + "/.claude/projects/*/" + session_id + ".jsonl";
            glob_t g{};
            if (glob(pattern.c_str(), GLOB_TILDE, nullptr, &g) == 0 && g.gl_pathc > 0) {
                path = g.gl_pathv[0];
            }
            globfree(&g);
        }
    }

    if (path.empty())
        return ToolResult::error("No transcript: provide path or session_id");

    // ── Parse transcript via shared TranscriptParser ─────────────────────
    struct Turn {
        std::string role;
        std::string content;
        int turn_index;
    };

    std::vector<Turn> turns;
    {
        chitta::TranscriptParser tp;
        chitta::TranscriptParseOptions opts;
        opts.include_thinking = false;       // task-similarity scoring uses surface content
        opts.filter_system_reminders = true; // strip the noise tags
        auto convs = tp.parse(path, opts);
        if (convs.empty() && !tp.last_error().empty())
            return ToolResult::error(tp.last_error());

        int turn_idx = 0;
        for (auto& c : convs) {
            if (!role_filter.empty() && c.role != role_filter) continue;
            if (c.content.size() < 20) continue;          // drop trivially short turns
            turns.push_back({std::move(c.role), std::move(c.content), turn_idx++});
        }
    }

    if (turns.empty())
        return ToolResult::error("No turns found in transcript");

    std::vector<size_t> token_counts(turns.size());
    size_t total_tokens = 0;
    for (size_t i = 0; i < turns.size(); ++i) {
        token_counts[i] = estimate_tokens(turns[i].content);
        total_tokens += token_counts[i];
    }

    // If already under budget, return everything
    if (total_tokens <= budget_tokens) {
        std::ostringstream briefing;
        for (const auto& t : turns)
            briefing << "[" << t.role << "]\n" << t.content << "\n\n";

        json stats = {
            {"total_turns", turns.size()}, {"selected_turns", turns.size()},
            {"total_tokens", total_tokens}, {"budget_tokens", budget_tokens},
            {"compression_ratio", 1.0}, {"method", "passthrough"}
        };
        return ToolResult::ok(briefing.str(), {{"briefing", briefing.str()}, {"stats", stats}});
    }

    // ── Embed task + all turns ───────────────────────────────────────────
    const bool has_embedder = (yantra_ != nullptr);
    std::vector<float> task_emb;
    std::vector<std::vector<float>> turn_embs(turns.size());
    std::vector<float> scores(turns.size(), 0.0f);

    if (has_embedder) {
        task_emb = embed_query(task);

        // Batch embed turns (truncate to first 512 chars for speed)
        std::vector<std::string> texts;
        texts.reserve(turns.size());
        for (const auto& t : turns) {
            std::string snippet = t.content.substr(0, 512);
            texts.push_back(std::move(snippet));
        }

        auto arthas = yantra_->transform_batch(texts);
        for (size_t i = 0; i < arthas.size(); ++i)
            turn_embs[i] = arthas[i].nu.data;
    }

    // ── Score each turn ──────────────────────────────────────────────────
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

    const size_t n = turns.size();

    for (size_t i = 0; i < n; ++i) {
        // Semantic similarity to task
        float semantic = 0.5f;
        if (!task_emb.empty() && !turn_embs[i].empty())
            semantic = cosine_sim(turn_embs[i], task_emb);

        // Recency bias: later turns are more relevant
        float recency = (n <= 1) ? 1.0f
            : 0.2f + 0.8f * (static_cast<float>(i) / static_cast<float>(n - 1));

        // Role weight: user corrections/instructions matter more
        float role_w = (turns[i].role == "user") ? 1.1f : 1.0f;

        // Combined score: semantic-heavy since that's the Latent Briefing insight
        scores[i] = role_w * (0.65f * semantic + 0.25f * recency + 0.10f);
    }

    // ── MAD adaptive threshold ───────────────────────────────────────────
    // Keep turns scoring above median + k * MAD
    // This adapts to the score distribution: sparse relevant sessions keep fewer,
    // dense relevant ones keep more.

    std::vector<float> sorted_scores(scores.begin(), scores.end());
    std::sort(sorted_scores.begin(), sorted_scores.end());

    float median;
    if (n % 2 == 0)
        median = (sorted_scores[n/2 - 1] + sorted_scores[n/2]) / 2.0f;
    else
        median = sorted_scores[n/2];

    std::vector<float> abs_devs(n);
    for (size_t i = 0; i < n; ++i)
        abs_devs[i] = std::fabs(scores[i] - median);
    std::sort(abs_devs.begin(), abs_devs.end());

    float mad;
    if (n % 2 == 0)
        mad = (abs_devs[n/2 - 1] + abs_devs[n/2]) / 2.0f;
    else
        mad = abs_devs[n/2];

    float threshold = median + mad_k * mad;

    // ── Select turns above threshold ─────────────────────────────────────
    struct Candidate {
        size_t idx;
        float score;
    };

    std::vector<Candidate> above_threshold;
    std::vector<Candidate> below_threshold;

    for (size_t i = 0; i < n; ++i) {
        if (scores[i] >= threshold)
            above_threshold.push_back({i, scores[i]});
        else
            below_threshold.push_back({i, scores[i]});
    }

    // Sort below_threshold by score descending (for backfill)
    std::sort(below_threshold.begin(), below_threshold.end(),
              [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

    // ── Budget enforcement ───────────────────────────────────────────────
    // Start with above-threshold turns. If over budget, drop lowest.
    // If under budget, backfill from below-threshold.

    // Sort above_threshold by score ascending (so we can pop lowest first)
    std::sort(above_threshold.begin(), above_threshold.end(),
              [](const Candidate& a, const Candidate& b) { return a.score < b.score; });

    size_t selected_tokens = 0;
    for (const auto& c : above_threshold)
        selected_tokens += token_counts[c.idx];

    // Drop lowest-scoring above-threshold if over budget
    while (selected_tokens > budget_tokens && !above_threshold.empty()) {
        selected_tokens -= token_counts[above_threshold.front().idx];
        above_threshold.erase(above_threshold.begin());
    }

    // Backfill from below-threshold if under budget
    for (const auto& c : below_threshold) {
        if (selected_tokens + token_counts[c.idx] > budget_tokens) continue;
        above_threshold.push_back(c);
        selected_tokens += token_counts[c.idx];
    }

    // Always include last 2 turns (most recent context) if not already selected
    std::unordered_set<size_t> selected_set;
    for (const auto& c : above_threshold)
        selected_set.insert(c.idx);

    for (size_t tail = (n > 2 ? n - 2 : 0); tail < n; ++tail) {
        if (selected_set.count(tail)) continue;
        // Only add if it doesn't blow the budget by >20%
        if (selected_tokens + token_counts[tail] <= budget_tokens * 1.2) {
            above_threshold.push_back({tail, scores[tail]});
            selected_tokens += token_counts[tail];
            selected_set.insert(tail);
        }
    }

    // ── Assemble briefing in chronological order ─────────────────────────
    std::sort(above_threshold.begin(), above_threshold.end(),
              [](const Candidate& a, const Candidate& b) { return a.idx < b.idx; });

    std::ostringstream briefing;
    for (const auto& c : above_threshold) {
        const auto& t = turns[c.idx];
        briefing << "[" << t.role << "]\n" << t.content << "\n\n";
    }

    double compression_ratio = (total_tokens > 0)
        ? static_cast<double>(selected_tokens) / static_cast<double>(total_tokens)
        : 1.0;

    json stats = {
        {"total_turns",      n},
        {"selected_turns",   above_threshold.size()},
        {"total_tokens",     total_tokens},
        {"selected_tokens",  selected_tokens},
        {"budget_tokens",    budget_tokens},
        {"compression_ratio", compression_ratio},
        {"mad_threshold",    threshold},
        {"median_score",     median},
        {"mad",              mad},
        {"method",           has_embedder ? "embedding+mad" : "recency_fallback"},
        {"embedding",        has_embedder}
    };

    std::ostringstream summary;
    summary << "Compacted " << n << " turns → " << above_threshold.size()
            << " (" << total_tokens << " → " << selected_tokens << " tok, "
            << std::fixed << std::setprecision(1) << (compression_ratio * 100.0) << "%)";

    return ToolResult::ok(summary.str(), {{"briefing", briefing.str()}, {"stats", stats}});
}

} // namespace chitta
