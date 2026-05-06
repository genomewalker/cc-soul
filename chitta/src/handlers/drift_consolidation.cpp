// drift_consolidation RPC handlers — bodies for declarations in
// chitta/include/chitta/rpc/handlers/drift_consolidation.hpp.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

ToolResult FieldRpcHandler::tool_find_near_duplicates(const json& params) {
    std::string realm = params.value("realm", "");
    size_t limit      = static_cast<size_t>(params.value("limit", 20));
    float threshold   = params.value("threshold", 0.90f);

    size_t candidates = static_cast<size_t>(params.value("candidates", 1000));
    auto pairs = find_dup_pairs(realm, candidates, threshold);
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

    return ToolResult::ok(ss.str(),
        {{"pairs", pairs_json}, {"count", pairs.size()}, {"threshold", threshold}});
}

ToolResult FieldRpcHandler::tool_consolidate_similar(const json& params) {
    std::string realm = params.value("realm", "");
    float threshold   = params.value("threshold", 0.92f);
    bool dry_run      = params.value("dry_run", true);
    size_t limit      = static_cast<size_t>(params.value("limit", 10));

    size_t candidates = static_cast<size_t>(params.value("candidates", 1000));
    auto pairs = find_dup_pairs(realm, candidates, threshold);
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
            if (field_store_->get_content(kept_id).empty() || field_store_->get_content(weaker_id).empty()) {
                continue;
            }
            field_store_->forget(weaker_id);
            if (!field_store_->get_content(weaker_id).empty()) {
                continue;
            }
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

    return ToolResult::ok(ss.str(),
        {{"merged", merged}, {"pairs", merged_pairs}, {"dry_run", dry_run},
         {"threshold", threshold}});
}

} // namespace chitta
