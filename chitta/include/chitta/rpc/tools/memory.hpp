#pragma once
// RPC Memory Tools: recall, resonate, full_resonate, recall_by_tag
//
// Semantic search and retrieval operations on the soul's memory.

#include "../types.hpp"
#include "../protocol.hpp"  // for sanitize_utf8
#include "../../mind.hpp"
#include <sstream>
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace chitta::rpc::tools::memory {

using json = nlohmann::json;

// Helper: sanitize text for JSON
inline std::string safe_text(const std::string& text) {
    return chitta::rpc::sanitize_utf8(text);
}

// Helper: safe float-to-percentage conversion (handles NaN/infinity)
inline int safe_pct(float value) {
    if (std::isnan(value) || std::isinf(value)) return 0;
    return static_cast<int>(std::clamp(value * 100.0f, -999.0f, 999.0f));
}

// Helper: extract title from text (first line or first N chars)
inline std::string extract_title(const std::string& text, size_t max_len = 60) {
    size_t newline = text.find('\n');
    std::string title = (newline != std::string::npos && newline < max_len)
        ? text.substr(0, newline)
        : text.substr(0, std::min(text.length(), max_len));
    if (title.length() < text.length()) {
        // Trim trailing whitespace and add ellipsis
        while (!title.empty() && (title.back() == ' ' || title.back() == '\n')) {
            title.pop_back();
        }
        if (title.length() < text.length()) title += "...";
    }
    return title;
}

// Register memory tool schemas
inline void register_schemas(std::vector<ToolSchema>& tools) {
    tools.push_back({
        "recall",
        "Recall relevant wisdom and episodes. zoom='sparse' for overview (20+ titles), "
        "'normal' for balanced (5-10 full), 'dense' for deep context (3-5 with relationships "
        "and temporal info), 'full' for complete untruncated content (1-3 results). "
        "When learn=true, applies Hebbian learning to strengthen connections between "
        "co-retrieved nodes. When primed=true, boosts results based on session context.",
        {
            {"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}, {"description", "What to search for (semantic)"}}},
                {"zoom", {{"type", "string"}, {"enum", {"sparse", "normal", "dense", "full"}},
                         {"default", "normal"}, {"description", "Detail level"}}},
                {"tag", {{"type", "string"}, {"description", "Filter by exact tag match"}}},
                {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}},
                {"threshold", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"default", 0}}},
                {"learn", {{"type", "boolean"}, {"default", false},
                          {"description", "Apply Hebbian learning"}}},
                {"primed", {{"type", "boolean"}, {"default", false},
                           {"description", "Session priming: boost based on context"}}},
                {"compete", {{"type", "boolean"}, {"default", true},
                            {"description", "Lateral inhibition"}}}
            }},
            {"required", {"query"}}
        }
    });

    tools.push_back({
        "recall_by_tag",
        "Recall memories by exact tag match only (no semantic search). "
        "For precise thread/category lookup.",
        {
            {"type", "object"},
            {"properties", {
                {"tag", {{"type", "string"}, {"description", "Tag to filter by"}}},
                {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}, {"default", 50}}}
            }},
            {"required", {"tag"}}
        }
    });

    tools.push_back({
        "resonate",
        "Semantic search with spreading activation through memory graph. "
        "Activation spreads from seed matches through edges to related concepts.",
        {
            {"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}, {"description", "What to search for"}}},
                {"k", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}, {"default", 10}}},
                {"spread_strength", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"default", 0.5}}},
                {"learn", {{"type", "boolean"}, {"default", true}}},
                {"hebbian_strength", {{"type", "number"}, {"minimum", 0}, {"maximum", 0.5}, {"default", 0.03}}}
            }},
            {"required", {"query"}}
        }
    });

    tools.push_back({
        "full_resonate",
        "Full resonance with all mechanisms: session priming, spreading activation, "
        "attractor dynamics, lateral inhibition, and Hebbian learning.",
        {
            {"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}, {"description", "What to search for"}}},
                {"k", {{"type", "integer"}, {"minimum", 1}, {"maximum", 50}, {"default", 10}}},
                {"spread_strength", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"default", 0.5}}},
                {"hebbian_strength", {{"type", "number"}, {"minimum", 0}, {"maximum", 0.2}, {"default", 0.03}}},
                {"exclude_tags", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Tags to exclude from results (e.g., auto:cmd)"}}}
            }},
            {"required", {"query"}}
        }
    });

    tools.push_back({
        "proactive_surface",
        "Surface important memories the user didn't ask for but should know about. "
        "Finds failures (don't repeat mistakes), open questions, beliefs, and constraints "
        "that relate to the current context. Filters by confidence and epsilon.",
        {
            {"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}, {"description", "Current context/query"}}},
                {"exclude_ids", {{"type", "array"}, {"items", {{"type", "string"}}},
                               {"description", "IDs already in recall results (to avoid duplication)"}}},
                {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 10}, {"default", 3}}},
                {"min_relevance", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"default", 0.25}}},
                {"min_confidence", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"default", 0.6}}},
                {"min_epsilon", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"default", 0.7}}}
            }},
            {"required", {"query"}}
        }
    });

    tools.push_back({
        "detect_contradictions",
        "Detect potential contradictions between new content and existing memories. "
        "Uses negation patterns and opposite words to find conflicts. Tags found "
        "contradictions so proactive_surface will show them.",
        {
            {"type", "object"},
            {"properties", {
                {"content", {{"type", "string"}, {"description", "New content to check for contradictions"}}},
                {"similarity_threshold", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"default", 0.6}}},
                {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 10}, {"default", 5}}}
            }},
            {"required", {"content"}}
        }
    });
}

// Tool implementations
inline ToolResult recall(Mind* mind, const json& params) {
    std::string query = params.at("query");
    std::string zoom = params.value("zoom", "normal");
    std::string tag = params.value("tag", "");
    float threshold = params.value("threshold", 0.0f);
    bool learn = params.value("learn", false);
    bool primed = params.value("primed", false);
    bool compete = params.value("compete", true);

    // Temporarily adjust competition setting if needed
    bool original_compete = mind->competition_config().enabled;
    if (!compete && original_compete) {
        mind->set_competition_enabled(false);
    }

    // Zoom-aware default limits
    size_t default_limit = (zoom == "sparse") ? 25 :
                           (zoom == "dense") ? 5 :
                           (zoom == "full") ? 3 : 10;
    size_t limit = params.value("limit", static_cast<int>(default_limit));

    // Clamp limits per zoom level
    if (zoom == "sparse") {
        limit = std::clamp(limit, size_t(5), size_t(100));
    } else if (zoom == "dense") {
        limit = std::clamp(limit, size_t(1), size_t(10));
    } else if (zoom == "full") {
        limit = std::clamp(limit, size_t(1), size_t(5));
    } else {
        limit = std::clamp(limit, size_t(1), size_t(50));
    }

    if (!mind->has_yantra()) {
        return ToolResult::error("Yantra not ready - cannot perform semantic search");
    }

    std::vector<Recall> recalls;
    if (!tag.empty()) {
        recalls = mind->recall_with_tag_filter(query, tag, limit, threshold);
    } else if (primed) {
        recalls = mind->recall_primed(query, limit, threshold);
    } else {
        recalls = mind->recall(query, limit, threshold);
    }

    // Restore competition setting
    if (!compete && original_compete) {
        mind->set_competition_enabled(true);
    }

    // Apply Hebbian learning if enabled
    if (learn && recalls.size() >= 2) {
        std::vector<NodeId> co_retrieved;
        size_t learn_count = std::min(recalls.size(), size_t(5));
        co_retrieved.reserve(learn_count);
        for (size_t i = 0; i < learn_count; ++i) {
            co_retrieved.push_back(recalls[i].id);
        }
        mind->hebbian_update(co_retrieved, 0.05f);
    }

    json results_array = json::array();
    std::ostringstream ss;
    ss << "Found " << recalls.size() << " results";
    if (!tag.empty()) ss << " with tag '" << tag << "'";
    ss << " (" << zoom << " view):\n";

    Timestamp current = now();

    for (const auto& r : recalls) {
        mind->feedback_used(r.id);

        if (zoom == "sparse") {
            std::string title = extract_title(safe_text(r.text));
            results_array.push_back({
                {"id", r.id.to_string()},
                {"title", title},
                {"type", node_type_to_string(r.type)},
                {"relevance", r.relevance}
            });
            ss << "\n[" << node_type_to_string(r.type) << "] " << title;

        } else if (zoom == "dense") {
            auto result_tags = mind->get_tags(r.id);
            float age_days = static_cast<float>(current - r.created) / 86400000.0f;
            float access_age = static_cast<float>(current - r.accessed) / 86400000.0f;

            json edges_array = json::array();
            float decay_rate = 0.05f;
            if (auto node = mind->get(r.id)) {
                decay_rate = node->delta;
                for (size_t i = 0; i < std::min(node->edges.size(), size_t(5)); ++i) {
                    auto& edge = node->edges[i];
                    std::string rel_text = mind->text(edge.target).value_or("");
                    edges_array.push_back({
                        {"id", edge.target.to_string()},
                        {"type", static_cast<int>(edge.type)},
                        {"weight", edge.weight},
                        {"title", extract_title(safe_text(rel_text))}
                    });
                }
            }

            results_array.push_back({
                {"id", r.id.to_string()},
                {"text", safe_text(r.text)},
                {"similarity", r.similarity},
                {"relevance", r.relevance},
                {"type", node_type_to_string(r.type)},
                {"confidence", {
                    {"mu", r.confidence.mu},
                    {"sigma_sq", r.confidence.sigma_sq},
                    {"n", r.confidence.n},
                    {"effective", r.confidence.effective()}
                }},
                {"temporal", {
                    {"created", r.created},
                    {"accessed", r.accessed},
                    {"age_days", age_days},
                    {"access_age_days", access_age},
                    {"decay_rate", decay_rate}
                }},
                {"related", edges_array},
                {"tags", result_tags}
            });
            ss << "\n[" << node_type_to_string(r.type) << "] " << extract_title(safe_text(r.text), 80);
            if (!edges_array.empty()) ss << " (" << edges_array.size() << " related)";

        } else if (zoom == "full") {
            auto result_tags = mind->get_tags(r.id);
            std::string text = safe_text(r.text);
            results_array.push_back({
                {"id", r.id.to_string()},
                {"text", text},
                {"type", node_type_to_string(r.type)},
                {"relevance", r.relevance},
                {"confidence", r.confidence.mu},
                {"tags", result_tags}
            });
            ss << "\n\n=== [" << node_type_to_string(r.type) << "] ===\n";
            ss << text << "\n";

        } else {
            auto result_tags = mind->get_tags(r.id);
            std::string text = safe_text(r.text);
            results_array.push_back({
                {"id", r.id.to_string()},
                {"text", text},
                {"similarity", r.similarity},
                {"relevance", r.relevance},
                {"type", node_type_to_string(r.type)},
                {"confidence", r.confidence.mu},
                {"tags", result_tags}
            });
            ss << "\n[" << safe_pct(r.relevance) << "%] " << text.substr(0, 100);
            if (text.length() > 100) ss << "...";
        }
    }

    return ToolResult::ok(ss.str(), {{"results", results_array}, {"zoom", zoom}});
}

inline ToolResult recall_by_tag(Mind* mind, const json& params) {
    std::string tag = params.at("tag");
    size_t limit = params.value("limit", 50);

    auto recalls = mind->recall_by_tag(tag, limit);

    json results_array = json::array();
    std::ostringstream ss;
    ss << "Found " << recalls.size() << " results with tag '" << tag << "':\n";

    for (const auto& r : recalls) {
        mind->feedback_used(r.id);
        auto result_tags = mind->get_tags(r.id);
        std::string text = safe_text(r.text);

        results_array.push_back({
            {"id", r.id.to_string()},
            {"text", text},
            {"created", r.created},
            {"type", node_type_to_string(r.type)},
            {"confidence", r.confidence.mu},
            {"tags", result_tags}
        });

        ss << "\n[" << node_type_to_string(r.type) << "] " << text.substr(0, 100);
        if (text.length() > 100) ss << "...";
    }

    return ToolResult::ok(ss.str(), {{"results", results_array}});
}

inline ToolResult resonate(Mind* mind, const json& params) {
    std::string query = params.at("query");
    size_t k = params.value("k", 10);
    float spread_strength = params.value("spread_strength", 0.5f);
    bool learn = params.value("learn", true);
    float hebbian_strength = params.value("hebbian_strength", 0.03f);

    if (!mind->has_yantra()) {
        return ToolResult::error("Yantra not ready - cannot perform semantic search");
    }

    std::vector<Recall> recalls;
    if (learn) {
        recalls = mind->resonate_with_learning(query, k, spread_strength, hebbian_strength);
    } else {
        recalls = mind->resonate(query, k, spread_strength);
    }

    json results_array = json::array();
    std::ostringstream ss;
    ss << "Resonance search for: " << query << "\n";
    ss << "Found " << recalls.size() << " resonant nodes";
    ss << " (spread=" << spread_strength;
    if (learn) ss << ", hebbian=" << hebbian_strength;
    ss << "):\n";

    for (const auto& r : recalls) {
        mind->feedback_used(r.id);
        auto result_tags = mind->get_tags(r.id);
        std::string text = safe_text(r.text);

        results_array.push_back({
            {"id", r.id.to_string()},
            {"text", text},
            {"relevance", r.relevance},
            {"type", node_type_to_string(r.type)},
            {"confidence", r.confidence.mu},
            {"tags", result_tags}
        });

        ss << "\n[" << safe_pct(r.relevance) << "%] " << text.substr(0, 100);
        if (text.length() > 100) ss << "...";
    }

    json result = {
        {"results", results_array},
        {"spread_strength", spread_strength},
        {"learning_enabled", learn}
    };
    if (learn) {
        result["hebbian_strength"] = hebbian_strength;
    }

    return ToolResult::ok(ss.str(), result);
}

inline ToolResult full_resonate(Mind* mind, const json& params) {
    std::string query = params.at("query");
    size_t k = params.value("k", 10);
    float spread_strength = params.value("spread_strength", 0.5f);
    float hebbian_strength = params.value("hebbian_strength", 0.03f);

    // Parse exclude_tags
    std::unordered_set<std::string> exclude_tags;
    if (params.contains("exclude_tags") && params["exclude_tags"].is_array()) {
        for (const auto& tag : params["exclude_tags"]) {
            if (tag.is_string()) {
                exclude_tags.insert(tag.get<std::string>());
            }
        }
    }

    if (!mind->has_yantra()) {
        return ToolResult::error("Yantra not ready - cannot perform semantic search");
    }

    // Request more results if we're filtering
    size_t fetch_k = exclude_tags.empty() ? k : k * 2;
    auto recalls = mind->full_resonate(query, fetch_k, spread_strength, hebbian_strength);

    json results_array = json::array();
    std::ostringstream ss;

    size_t included = 0;
    for (const auto& r : recalls) {
        if (included >= k) break;

        auto result_tags = mind->get_tags(r.id);

        // Check if any result tag matches exclude_tags
        bool excluded = false;
        for (const auto& tag : result_tags) {
            if (exclude_tags.count(tag)) {
                excluded = true;
                break;
            }
        }
        if (excluded) continue;

        mind->feedback_used(r.id);
        ++included;

        std::string text = safe_text(r.text);
        results_array.push_back({
            {"id", r.id.to_string()},
            {"text", text},
            {"relevance", r.relevance},
            {"similarity", r.similarity},
            {"type", node_type_to_string(r.type)},
            {"confidence", r.confidence.mu},
            {"tags", result_tags}
        });

        ss << "\n[" << safe_pct(r.relevance) << "%] ";
        ss << "[" << node_type_to_string(r.type) << "] ";
        ss << text.substr(0, 90);
        if (text.length() > 90) ss << "...";
    }

    json result = {
        {"results", results_array},
        {"phases_active", {
            {"priming", true},
            {"spreading_activation", true},
            {"attractor_dynamics", true},
            {"lateral_inhibition", mind->competition_config().enabled},
            {"hebbian_learning", hebbian_strength > 0.0f}
        }},
        {"spread_strength", spread_strength},
        {"hebbian_strength", hebbian_strength}
    };

    return ToolResult::ok(ss.str(), result);
}

// Proactive surfacing: find important unrequested memories
// Surfaces failures, questions, beliefs, decisions that relate to context
inline ToolResult proactive_surface(Mind* mind, const json& params) {
    std::string query = params.value("query", "");
    json exclude_ids = params.value("exclude_ids", json::array());
    size_t limit = params.value("limit", 3);
    float min_relevance = params.value("min_relevance", 0.25f);
    float min_confidence = params.value("min_confidence", 0.6f);
    float min_epsilon = params.value("min_epsilon", 0.7f);

    if (query.empty()) {
        return ToolResult::error("Query required for proactive surfacing");
    }

    // Build exclusion set from already-recalled IDs
    std::unordered_set<std::string> excluded;
    for (const auto& id : exclude_ids) {
        if (id.is_string()) {
            excluded.insert(id.get<std::string>());
        }
    }

    // Proactive node types (things worth surfacing unrequested)
    std::unordered_set<NodeType> proactive_types = {
        NodeType::Failure,     // Don't repeat mistakes
        NodeType::Question,    // Open questions
        NodeType::Belief,      // Guiding principles
        NodeType::Invariant,   // Constraints to respect
        NodeType::Gap          // Knowledge gaps
    };

    // Search with broader threshold
    auto recalls = mind->recall(query, limit * 5, min_relevance);

    std::ostringstream ss;
    json results_array = json::array();
    size_t surfaced = 0;

    for (const auto& r : recalls) {
        if (surfaced >= limit) break;

        // Skip if already in regular recall
        if (excluded.count(r.id.to_string())) continue;

        // Must be a proactive type OR have decision/warning tag
        auto tags = mind->get_tags(r.id);
        bool is_proactive_type = proactive_types.count(r.type) > 0;
        bool has_proactive_tag = false;
        for (const auto& tag : tags) {
            if (tag == "decision" || tag == "warning" || tag == "important" ||
                tag == "contradiction" || tag == "blocker") {
                has_proactive_tag = true;
                break;
            }
        }

        if (!is_proactive_type && !has_proactive_tag) continue;

        // Get full node for confidence and epsilon check
        auto node = mind->get(r.id);
        if (!node) continue;

        // Filter by confidence and epsilon
        if (node->kappa.effective() < min_confidence) continue;
        if (node->epsilon < min_epsilon) continue;

        // This memory qualifies for proactive surfacing
        ++surfaced;

        // Icon based on type
        std::string icon;
        switch (r.type) {
            case NodeType::Failure:   icon = "!!"; break;
            case NodeType::Question:  icon = "??"; break;
            case NodeType::Belief:    icon = ">>"; break;
            case NodeType::Invariant: icon = "##"; break;
            case NodeType::Gap:       icon = "~~"; break;
            default:                  icon = "**"; break;
        }

        std::string text = safe_text(r.text);
        std::string title = extract_title(text, 70);

        results_array.push_back({
            {"id", r.id.to_string()},
            {"type", node_type_to_string(r.type)},
            {"title", title},
            {"relevance", r.relevance},
            {"confidence", node->kappa.effective()},
            {"epsilon", node->epsilon},
            {"tags", tags}
        });

        ss << icon << " [" << node_type_to_string(r.type) << "] " << title << "\n";
    }

    if (surfaced == 0) {
        return ToolResult::ok("No proactive memories to surface", {{"results", json::array()}});
    }

    json result = {
        {"results", results_array},
        {"count", surfaced},
        {"query", query}
    };

    return ToolResult::ok("Proactively surfacing:\n" + ss.str(), result);
}

// Contradiction detection: find memories that may conflict with new content
inline ToolResult detect_contradictions(Mind* mind, const json& params) {
    std::string content = params.value("content", "");
    float similarity_threshold = params.value("similarity_threshold", 0.6f);
    size_t limit = params.value("limit", 5);

    if (content.empty()) {
        return ToolResult::error("Content required for contradiction detection");
    }

    // Negation indicators
    static const std::vector<std::string> negations = {
        "not ", "don't ", "doesn't ", "never ", "shouldn't ", "won't ",
        "isn't ", "aren't ", "wasn't ", "can't ", "cannot ", "avoid ",
        "bad ", "wrong ", "false ", "fails ", "broken "
    };

    static const std::vector<std::pair<std::string, std::string>> opposites = {
        {"always", "never"}, {"good", "bad"}, {"true", "false"},
        {"works", "fails"}, {"use", "avoid"}, {"do", "don't"},
        {"should", "shouldn't"}, {"can", "cannot"}, {"is", "isn't"},
        {"fast", "slow"}, {"safe", "unsafe"}, {"correct", "incorrect"}
    };

    // Check if content has negation
    std::string content_lower = content;
    std::transform(content_lower.begin(), content_lower.end(), content_lower.begin(), ::tolower);

    bool content_has_negation = false;
    for (const auto& neg : negations) {
        if (content_lower.find(neg) != std::string::npos) {
            content_has_negation = true;
            break;
        }
    }

    // Search for similar content
    auto recalls = mind->recall(content, limit * 3, similarity_threshold);

    std::ostringstream ss;
    json contradictions = json::array();
    size_t found = 0;

    for (const auto& r : recalls) {
        if (found >= limit) break;

        std::string recall_text = r.text;
        std::string recall_lower = recall_text;
        std::transform(recall_lower.begin(), recall_lower.end(), recall_lower.begin(), ::tolower);

        // Check if recall has negation
        bool recall_has_negation = false;
        for (const auto& neg : negations) {
            if (recall_lower.find(neg) != std::string::npos) {
                recall_has_negation = true;
                break;
            }
        }

        // Contradiction: one has negation, other doesn't, AND high similarity
        bool potential_contradiction = false;

        if (content_has_negation != recall_has_negation && r.similarity > similarity_threshold) {
            potential_contradiction = true;
        }

        // Also check for opposite words
        if (!potential_contradiction) {
            for (const auto& [word1, word2] : opposites) {
                bool content_has_w1 = content_lower.find(word1) != std::string::npos;
                bool content_has_w2 = content_lower.find(word2) != std::string::npos;
                bool recall_has_w1 = recall_lower.find(word1) != std::string::npos;
                bool recall_has_w2 = recall_lower.find(word2) != std::string::npos;

                if ((content_has_w1 && recall_has_w2) || (content_has_w2 && recall_has_w1)) {
                    potential_contradiction = true;
                    break;
                }
            }
        }

        if (potential_contradiction) {
            ++found;
            contradictions.push_back({
                {"id", r.id.to_string()},
                {"text", safe_text(recall_text).substr(0, 100)},
                {"similarity", r.similarity},
                {"type", node_type_to_string(r.type)}
            });

            // Tag the existing memory as potentially contradicting
            auto node = mind->get(r.id);
            if (node) {
                auto tags = node->tags;
                bool has_tag = std::find(tags.begin(), tags.end(), "contradiction") != tags.end();
                if (!has_tag) {
                    tags.push_back("contradiction");
                    Node updated = *node;
                    updated.tags = tags;
                    mind->update_node(r.id, updated);
                }
            }

            ss << "!! " << safe_text(recall_text).substr(0, 80) << "...\n";
        }
    }

    if (found == 0) {
        return ToolResult::ok("No contradictions detected", {{"contradictions", json::array()}});
    }

    json result = {
        {"contradictions", contradictions},
        {"count", found},
        {"content_preview", content.substr(0, 50)}
    };

    return ToolResult::ok("Potential contradictions:\n" + ss.str(), result);
}

// Register all memory tool handlers
inline void register_handlers(Mind* mind,
                               std::unordered_map<std::string, ToolHandler>& handlers) {
    handlers["recall"] = [mind](const json& p) { return recall(mind, p); };
    handlers["recall_by_tag"] = [mind](const json& p) { return recall_by_tag(mind, p); };
    handlers["resonate"] = [mind](const json& p) { return resonate(mind, p); };
    handlers["full_resonate"] = [mind](const json& p) { return full_resonate(mind, p); };
    handlers["proactive_surface"] = [mind](const json& p) { return proactive_surface(mind, p); };
    handlers["detect_contradictions"] = [mind](const json& p) { return detect_contradictions(mind, p); };
}

} // namespace chitta::rpc::tools::memory
