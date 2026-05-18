// field_misc.anticipation — bodies extracted from field_misc.cpp by theme.
// Declarations remain in chitta/include/chitta/rpc/handlers/field_misc.hpp.

#include "../../include/chitta/rpc/field_handler.hpp"

namespace chitta {

ToolResult FieldRpcHandler::tool_anticipation_observe(const json& params) {
    std::string context = params.value("context", "");
    std::string action  = params.value("action", "");
    std::string realm   = params.value("realm", "brahman");

    if (context.empty() || action.empty())
        return ToolResult::error("context and action are required");

    field_store_->add_triplet(context, "anticipates", action);

    std::string text = "anticipation: " + context + " → " + action;
    std::vector<float> embedding;
    if (params.contains("_preembedding")) {
        embedding = params["_preembedding"].get<std::vector<float>>();
    } else {
        embedding = embed_text(text);
    }
    uint64_t id = field_store_->remember("anticipation", realm, text, embedding, 0.7f, 0.001f);

    return ToolResult::ok("Pattern recorded (id: " + std::to_string(id) + ")",
        {{"id", std::to_string(id)}});
}

ToolResult FieldRpcHandler::tool_anticipation_predict(const json& params) {
    std::string context   = params.value("context", "");
    size_t limit          = static_cast<size_t>(params.value("limit", 5));
    float min_confidence  = params.value("min_confidence", 0.3f);
    std::string realm     = params.value("realm", "");

    if (context.empty()) return ToolResult::error("context is required");

    // Query triplets for "anticipates" relationships from this context
    std::string triplets_raw = field_store_->query_subject(context);
    json triplets_json;
    try { triplets_json = json::parse(triplets_raw, nullptr, false); } catch (...) {}
    if (triplets_json.is_discarded() || !triplets_json.is_array()) triplets_json = json::array();

    json patterns = json::array();
    for (const auto& t : triplets_json) {
        std::string pred = t.value("predicate", "");
        if (pred != "anticipates") continue;
        patterns.push_back({
            {"context",    context},
            {"action",     t.value("object", "")},
            {"confidence", 0.7f},
        });
        if (patterns.size() >= limit) break;
    }

    // Supplement with semantic recall
    if (patterns.size() < limit) {
        auto embedding = embed_query(context);
        if (!embedding.empty()) {
            auto hits = field_store_->recall(embedding, limit * 2, realm);
            for (const auto& h : hits) {
                if (h.kind != "anticipation") continue;
                if (h.confidence < min_confidence) continue;
                patterns.push_back({
                    {"id",         std::to_string(h.memory_id)},
                    {"content",    h.content},
                    {"confidence", h.confidence},
                    {"realm",      h.realm},
                });
                if (patterns.size() >= limit) break;
            }
        }
    }

    std::ostringstream ss;
    ss << "Predicted " << patterns.size() << " action(s) for context: " << context;
    return ToolResult::ok(ss.str(), {{"patterns", patterns}});
}

ToolResult FieldRpcHandler::tool_anticipation_success(const json& params) {
    auto [id, id_str] = parse_id(params);
    if (id <= 0) return ToolResult::error("id is required");

    if (field_store_->get_content(static_cast<uint64_t>(id)).empty()) {
        return ToolResult::error("Pattern #" + id_str + " not found");
    }
    field_store_->strengthen(static_cast<uint64_t>(id), 0.1f);
    return ToolResult::ok("Pattern #" + id_str + " marked successful");
}

ToolResult FieldRpcHandler::tool_anticipation_list(const json& params) {
    std::string realm = params.value("realm", "");
    size_t limit      = static_cast<size_t>(params.value("limit", 20));
    std::string sort_by = params.value("sort_by", "confidence");

    auto hits = field_store_->recall_by_kind("anticipation", limit);

    json patterns = json::array();
    for (const auto& h : hits) {
        if (!realm.empty() && h.realm != realm) continue;
        patterns.push_back({
            {"id",         std::to_string(h.memory_id)},
            {"content",    h.content},
            {"confidence", h.confidence},
            {"realm",      h.realm},
        });
    }

    if (sort_by == "confidence") {
        std::sort(patterns.begin(), patterns.end(), [](const json& a, const json& b) {
            return a.value("confidence", 0.0f) > b.value("confidence", 0.0f);
        });
    }

    std::ostringstream ss;
    ss << "Learned Anticipation Patterns\n"
       << "══════════════════════════════\n\n";
    if (patterns.empty()) {
        ss << "No patterns learned yet.\n";
    } else {
        for (const auto& p : patterns) {
            ss << "[" << p.value("id", "?") << "] " << p.value("content", "").substr(0, 80) << "\n";
        }
    }

    return ToolResult::ok(ss.str(), {{"count", patterns.size()}, {"patterns", patterns}});
}

ToolResult FieldRpcHandler::tool_anticipation_filter(const json& params) {
    std::string session_id = params.value("session_id", "");
    size_t max = static_cast<size_t>(params.value("max", 2));

    auto hits = field_store_->recall_by_kind("anticipation", max * 4);

    json candidates = json::array();
    for (const auto& h : hits) {
        if (h.confidence < 0.5f) continue;
        candidates.push_back({
            {"id",         std::to_string(h.memory_id)},
            {"prediction", h.content},
            {"confidence", h.confidence},
        });
        if (candidates.size() >= max) break;
    }

    if (candidates.empty()) {
        return ToolResult::ok("No predictions pass the annoyance gate", {
            {"session_id", session_id}, {"count", 0}, {"candidates", json::array()}
        });
    }

    return ToolResult::ok(
        std::to_string(candidates.size()) + " prediction(s) ready to surface",
        {{"session_id", session_id}, {"count", candidates.size()}, {"candidates", candidates}});
}

ToolResult FieldRpcHandler::tool_anticipation_gate_status(const json& params) {
    std::string session_id = params.value("session_id", "");
    return ToolResult::ok("Annoyance gate status (chitta-field stub)", {
        {"session_id",       session_id},
        {"gate_open",        true},
        {"budget_remaining", 5},
        {"confidence_floor", 0.5f},
        {"note",             "Gate state not persisted in chitta-field backend"},
    });
}

ToolResult FieldRpcHandler::tool_anticipation_record_outcome(const json& params) {
    auto [candidate_id, candidate_str] = parse_id(params, "candidate_id");
    if (candidate_id <= 0) return ToolResult::error("candidate_id is required");

    bool correct = params.value("correct", false);

    if (correct) {
        field_store_->strengthen(static_cast<uint64_t>(candidate_id), 0.1f);
    } else {
        field_store_->weaken(static_cast<uint64_t>(candidate_id), 0.05f);
    }

    field_store_->emit_event("anticipation",
        correct ? "correct" : "incorrect", candidate_str, "");

    return ToolResult::ok(
        "Recorded outcome: " + std::string(correct ? "correct" : "incorrect") +
        " for candidate #" + candidate_str,
        {{"candidate_id", candidate_id}, {"outcome", correct ? "correct" : "incorrect"}});
}

ToolResult FieldRpcHandler::tool_habit_observe(const json& params) {
    std::string trigger  = params.value("trigger", "");
    std::string response = params.value("response", "");
    std::string realm    = params.value("realm", "brahman");
    static constexpr int MIN_OBSERVATIONS = 3;

    if (trigger.empty() || response.empty())
        return ToolResult::error("trigger and response are required");

    // Always strengthen the triplet edge (tracks frequency as weight)
    field_store_->add_triplet(trigger, "triggers", response);

    // Count how many times this exact pair has been observed via triplet query
    std::string triplets_raw = field_store_->query_subject(trigger);
    int count = 0;
    try {
        auto arr = json::parse(triplets_raw);
        for (const auto& t : arr) {
            if (t.value("predicate","") == "triggers" && t.value("object","") == response)
                ++count;
        }
    } catch (...) {}

    // Only store a habit memory once the pair has been seen MIN_OBSERVATIONS times
    if (count < MIN_OBSERVATIONS) {
        return ToolResult::ok(
            "Habit observed (" + std::to_string(count) + "/" +
            std::to_string(MIN_OBSERVATIONS) + " before stored)",
            {{"count", count}, {"threshold", MIN_OBSERVATIONS}});
    }

    std::string text = "habit: " + trigger + " → " + response;
    auto embedding = embed_text(text);
    uint64_t id = field_store_->remember("habit", realm, text, embedding, 0.7f, 0.001f);

    return ToolResult::ok(
        "Habit stored (id: " + std::to_string(id) + ", observations=" + std::to_string(count) + ")",
        {{"id", std::to_string(id)}, {"count", count}});
}

ToolResult FieldRpcHandler::tool_habit_match(const json& params) {
    std::string context   = params.value("context", "");
    float min_strength    = params.value("min_strength", 0.3f);
    std::string realm     = params.value("realm", "");

    if (context.empty()) return ToolResult::error("context is required");

    // Triplet query for "triggers" relationships
    std::string triplets_raw = field_store_->query_subject(context);
    json triplets_json;
    try { triplets_json = json::parse(triplets_raw, nullptr, false); } catch (...) {}
    if (triplets_json.is_discarded() || !triplets_json.is_array()) triplets_json = json::array();

    json habits = json::array();
    for (const auto& t : triplets_json) {
        std::string pred = t.value("predicate", "");
        if (pred != "triggers") continue;
        habits.push_back({
            {"trigger",  context},
            {"response", t.value("object", "")},
            {"strength", 0.7f},
        });
    }

    // Supplement with semantic recall
    if (habits.empty()) {
        auto embedding = embed_query(context);
        if (!embedding.empty()) {
            auto hits = field_store_->recall(embedding, 10, realm);
            for (const auto& h : hits) {
                if (h.kind != "habit") continue;
                if (h.confidence < min_strength) continue;
                habits.push_back({
                    {"id",       std::to_string(h.memory_id)},
                    {"content",  h.content},
                    {"strength", h.confidence},
                    {"realm",    h.realm},
                });
            }
        }
    }

    std::ostringstream ss;
    ss << "Matching Habits\n═══════════════\n\n";
    if (habits.empty()) {
        ss << "No matching habits found.\n";
    } else {
        for (const auto& h : habits) {
            ss << "• " << h.value("trigger", h.value("content", "?"))
               << " → " << h.value("response", "") << "\n";
            ss << "  Strength: " << std::fixed << std::setprecision(2)
               << h.value("strength", 0.0f) << "\n\n";
        }
    }

    return ToolResult::ok(ss.str(), {{"habits", habits}});
}

ToolResult FieldRpcHandler::tool_habit_strengthen(const json& params) {
    auto [id, id_str] = parse_id(params);
    float amount = params.value("amount", 0.1f);
    if (id <= 0) return ToolResult::error("id is required");

    if (field_store_->get_content(static_cast<uint64_t>(id)).empty()) {
        return ToolResult::error("Habit #" + id_str + " not found");
    }
    field_store_->strengthen(static_cast<uint64_t>(id), amount);
    return ToolResult::ok(
        "Habit #" + id_str + " strengthened by " + std::to_string(amount));
}

ToolResult FieldRpcHandler::tool_habit_weaken(const json& params) {
    auto [id, id_str] = parse_id(params);
    float amount = params.value("amount", 0.05f);
    if (id <= 0) return ToolResult::error("id is required");

    if (field_store_->get_content(static_cast<uint64_t>(id)).empty()) {
        return ToolResult::error("Habit #" + id_str + " not found");
    }
    field_store_->weaken(static_cast<uint64_t>(id), amount);
    return ToolResult::ok(
        "Habit #" + id_str + " weakened by " + std::to_string(amount));
}

ToolResult FieldRpcHandler::tool_habit_list(const json& params) {
    std::string realm   = params.value("realm", "");
    float min_strength  = params.value("min_strength", 0.0f);
    size_t limit        = static_cast<size_t>(params.value("limit", 20));

    auto hits = field_store_->recall_by_kind("habit", limit);

    json habits = json::array();
    for (const auto& h : hits) {
        if (!realm.empty() && h.realm != realm) continue;
        if (h.confidence < min_strength) continue;
        habits.push_back({
            {"id",       std::to_string(h.memory_id)},
            {"content",  h.content},
            {"strength", h.confidence},
            {"realm",    h.realm},
        });
    }

    std::ostringstream ss;
    ss << "Formed Habits\n══════════════\n\n";
    if (habits.empty()) {
        ss << "No habits formed yet.\n";
    } else {
        for (const auto& h : habits) {
            ss << "[" << h.value("id", "?") << "] "
               << h.value("content", "").substr(0, 80) << "\n"
               << "  strength=" << std::fixed << std::setprecision(2)
               << h.value("strength", 0.0f) << "\n\n";
        }
    }

    return ToolResult::ok(ss.str(), {{"count", habits.size()}, {"habits", habits}});
}
} // namespace chitta
