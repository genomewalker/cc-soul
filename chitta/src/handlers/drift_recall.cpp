// drift_recall RPC handlers — bodies for declarations in
// chitta/include/chitta/rpc/handlers/drift_recall.hpp.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

ToolResult FieldRpcHandler::tool_cooccurrence_graph(const json& params) {
    uint64_t id = extract_id(params);
    if (id == 0) return ToolResult::error("id is required");

    size_t limit = static_cast<size_t>(params.value("limit", 10));

    std::string raw = field_store_->get_assoc_edges(id, limit);
    json edges;
    try {
        edges = json::parse(raw);
    } catch (...) {
        edges = json::array();
    }
    if (!edges.is_array()) edges = json::array();

    for (auto& edge : edges) {
        uint64_t src = edge.value("src", uint64_t(0));
        uint64_t dst = edge.value("dst", uint64_t(0));
        if (src != 0) {
            std::string content = field_store_->get_content(src);
            edge["a_preview"] = utf8_trunc(content, 80);
        }
        if (dst != 0) {
            std::string content = field_store_->get_content(dst);
            edge["b_preview"] = utf8_trunc(content, 80);
        }
    }

    std::ostringstream ss;
    ss << "Co-occurrence edges for #" << id << ": " << edges.size() << " edges\n";
    for (const auto& e : edges) {
        ss << "  " << e.value("src", uint64_t(0)) << " <-> " << e.value("dst", uint64_t(0))
           << " w=" << e.value("weight", 0.0f) << "\n";
    }

    return ToolResult::ok(ss.str(), {{"edges", edges}, {"id", std::to_string(id)}});
}

ToolResult FieldRpcHandler::tool_labile_memories_top(const json& params) {
    std::string realm = params.value("realm", "");
    size_t limit = static_cast<size_t>(params.value("limit", 20));

    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto hits = field_store_->recall_temporal(0, now_ms, limit * 3, realm);

    // Sort by access_count descending to find most labile
    std::sort(hits.begin(), hits.end(),
              [](const FieldRecallHit& a, const FieldRecallHit& b) {
                  return a.access_count > b.access_count;
              });

    if (hits.size() > limit) hits.resize(limit);

    json results = json::array();
    std::ostringstream ss;
    ss << "Top " << hits.size() << " most-accessed (labile) memories:\n";
    for (const auto& h : hits) {
        std::string preview = utf8_trunc(h.content, 100);
        json entry = {
            {"id", std::to_string(h.memory_id)},
            {"preview", preview},
            {"access_count", h.access_count},
            {"strength", h.strength},
            {"confidence", h.confidence},
        };
        results.push_back(entry);
        ss << "  #" << h.memory_id << " [" << h.access_count << " accesses] " << preview << "\n";
    }

    return ToolResult::ok(ss.str(), {{"memories", results}, {"count", results.size()}});
}

} // namespace chitta
