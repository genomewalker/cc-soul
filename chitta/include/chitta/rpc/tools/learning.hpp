#pragma once
// RPC Learning Tools: grow, observe, feedback
//
// Tools for adding knowledge to the soul and providing feedback
// on existing memories to strengthen or weaken them.

#include "../types.hpp"
#include "../../mind.hpp"
#include <sstream>

namespace chitta::rpc::tools::learning {

using json = nlohmann::json;

// Helper: convert string to NodeType
inline NodeType string_to_node_type(const std::string& s) {
    if (s == "wisdom") return NodeType::Wisdom;
    if (s == "belief") return NodeType::Belief;
    if (s == "intention") return NodeType::Intention;
    if (s == "aspiration") return NodeType::Aspiration;
    if (s == "episode") return NodeType::Episode;
    if (s == "failure") return NodeType::Failure;
    if (s == "dream") return NodeType::Dream;
    if (s == "term") return NodeType::Term;
    return NodeType::Episode;  // default
}

// Register learning tool schemas
inline void register_schemas(std::vector<ToolSchema>& tools) {
    tools.push_back({
        "grow",
        "Add to the soul: wisdom, beliefs, failures, aspirations, dreams, or terms. "
        "Each type has different decay and confidence properties.",
        {
            {"type", "object"},
            {"properties", {
                {"type", {{"type", "string"},
                         {"enum", {"wisdom", "belief", "failure", "aspiration", "dream", "term"}},
                         {"description", "What to grow"}}},
                {"content", {{"type", "string"}, {"description", "The content/statement to add"}}},
                {"title", {{"type", "string"}, {"description", "Short title (required for wisdom/failure)"}}},
                {"domain", {{"type", "string"}, {"description", "Domain context (optional)"}}},
                {"confidence", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"default", 0.8}}},
                {"epsilon", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"default", 0.5},
                            {"description", "Epiplexity: reconstructability from title (Claude-assessed, 0-1)"}}}
            }},
            {"required", {"type", "content"}}
        }
    });

    tools.push_back({
        "observe",
        "Record an observation (episode). Categories determine decay rate: "
        "bugfix/decision (slow), discovery/feature (medium), session_ledger/signal (fast).",
        {
            {"type", "object"},
            {"properties", {
                {"category", {{"type", "string"},
                             {"enum", {"bugfix", "decision", "discovery", "feature", "refactor",
                                       "session_ledger", "signal"}},
                             {"description", "Category affecting decay rate"}}},
                {"title", {{"type", "string"}, {"maxLength", 80}, {"description", "Short title"}}},
                {"content", {{"type", "string"}, {"description", "Full observation content"}}},
                {"project", {{"type", "string"}, {"description", "Project name (optional)"}}},
                {"tags", {{"type", "string"}, {"description", "Comma-separated tags for filtering"}}},
                {"epsilon", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"default", 0.5},
                            {"description", "Epiplexity: reconstructability from title (Claude-assessed, 0-1)"}}}
            }},
            {"required", {"category", "title", "content"}}
        }
    });

    tools.push_back({
        "feedback",
        "Mark a memory as helpful or misleading. Affects confidence scores "
        "and influences future retrieval.",
        {
            {"type", "object"},
            {"properties", {
                {"memory_id", {{"type", "string"}, {"description", "UUID of the memory"}}},
                {"helpful", {{"type", "boolean"}, {"description", "true=helpful, false=misleading"}}},
                {"context", {{"type", "string"}, {"description", "Why this feedback was given (optional)"}}}
            }},
            {"required", {"memory_id", "helpful"}}
        }
    });

    tools.push_back({
        "update",
        "Update a node's content (high-ε migration). Replaces payload while preserving embedding.",
        {
            {"type", "object"},
            {"properties", {
                {"id", {{"type", "string"}, {"description", "UUID of the node to update"}}},
                {"content", {{"type", "string"}, {"description", "New content (natural language)"}}}
            }},
            {"required", {"id", "content"}}
        }
    });

    tools.push_back({
        "connect",
        "Create a semantic relationship (triplet): subject --[predicate]--> object.",
        {
            {"type", "object"},
            {"properties", {
                {"subject", {{"type", "string"}, {"description", "Subject entity"}}},
                {"predicate", {{"type", "string"}, {"description", "Relationship type"}}},
                {"object", {{"type", "string"}, {"description", "Object entity"}}},
                {"weight", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"default", 1.0}}}
            }},
            {"required", {"subject", "predicate", "object"}}
        }
    });

    tools.push_back({
        "query",
        "Query triplet relationships. Use empty string as wildcard.",
        {
            {"type", "object"},
            {"properties", {
                {"subject", {{"type", "string"}, {"description", "Subject entity (empty = any)"}}},
                {"predicate", {{"type", "string"}, {"description", "Relationship type (empty = any)"}}},
                {"object", {{"type", "string"}, {"description", "Object entity (empty = any)"}}}
            }},
            {"required", {}}
        }
    });
}

// Tool implementations
inline ToolResult grow(Mind* mind, const json& params) {
    std::string type_str = params.at("type");
    std::string content = params.at("content");
    std::string title = params.value("title", "");
    std::string domain = params.value("domain", "");
    float confidence = params.value("confidence", 0.8f);
    float epsilon = params.value("epsilon", 0.5f);

    NodeType type = string_to_node_type(type_str);

    // Validate requirements
    if ((type == NodeType::Wisdom || type == NodeType::Failure) && title.empty()) {
        return ToolResult::error("Title required for wisdom/failure");
    }

    // Create combined text for embedding
    std::string full_text = content;
    if (!title.empty()) {
        full_text = title + ": " + content;
    }
    if (!domain.empty()) {
        full_text = "[" + domain + "] " + full_text;
    }

    NodeId id;
    if (mind->has_yantra()) {
        id = mind->remember(full_text, type, Confidence(confidence));
    } else {
        id = mind->remember(type, Vector::zeros(), Confidence(confidence),
                            std::vector<uint8_t>(full_text.begin(), full_text.end()));
    }

    // Set epsilon on the created node
    if (auto node = mind->get(id)) {
        Node updated = *node;
        updated.epsilon = std::clamp(epsilon, 0.0f, 1.0f);
        mind->update_node(id, updated);
    }

    json result = {
        {"id", id.to_string()},
        {"type", type_str},
        {"title", title},
        {"confidence", confidence},
        {"epsilon", epsilon}
    };

    std::ostringstream ss;
    ss << "Grew " << type_str << ": " << (title.empty() ? content.substr(0, 50) : title);
    ss << " (id: " << id.to_string() << ", ε=" << static_cast<int>(epsilon * 100) << "%)";

    return ToolResult::ok(ss.str(), result);
}

inline ToolResult observe(Mind* mind, const json& params) {
    std::string category = params.at("category");
    std::string title = params.at("title");
    std::string content = params.at("content");
    std::string project = params.value("project", "");
    std::string tags_str = params.value("tags", "");
    float epsilon = params.value("epsilon", 0.5f);

    // Determine decay rate based on category
    float decay = 0.05f;  // default
    if (category == "bugfix" || category == "decision") {
        decay = 0.02f;  // slow decay
    } else if (category == "session_ledger" || category == "signal") {
        decay = 0.15f;  // fast decay
    }

    // Parse tags into vector
    std::vector<std::string> tags_vec;
    if (!tags_str.empty()) {
        std::stringstream ss(tags_str);
        std::string tag;
        while (std::getline(ss, tag, ',')) {
            size_t start = tag.find_first_not_of(" \t");
            size_t end = tag.find_last_not_of(" \t");
            if (start != std::string::npos) {
                tags_vec.push_back(tag.substr(start, end - start + 1));
            }
        }
    }

    // Create full observation text
    std::string full_text = title + "\n" + content;
    if (!project.empty()) {
        full_text = "[" + project + "] " + full_text;
    }
    if (!tags_str.empty()) {
        full_text += "\nTags: " + tags_str;
    }

    NodeId id;
    if (mind->has_yantra()) {
        if (!tags_vec.empty()) {
            id = mind->remember(full_text, NodeType::Episode, tags_vec);
        } else {
            id = mind->remember(full_text, NodeType::Episode);
        }
    } else {
        id = mind->remember(NodeType::Episode, Vector::zeros(),
                            std::vector<uint8_t>(full_text.begin(), full_text.end()));
    }

    // Set epsilon and decay on the created node
    if (auto node = mind->get(id)) {
        Node updated = *node;
        updated.epsilon = std::clamp(epsilon, 0.0f, 1.0f);
        updated.delta = decay;
        mind->update_node(id, updated);
    }

    json result = {
        {"id", id.to_string()},
        {"category", category},
        {"title", title},
        {"decay_rate", decay},
        {"epsilon", epsilon},
        {"tags", tags_vec}
    };

    return ToolResult::ok("Observed: " + title + " (ε=" + std::to_string(static_cast<int>(epsilon * 100)) + "%)", result);
}

inline ToolResult feedback(Mind* mind, const json& params) {
    std::string memory_id_str = params.at("memory_id");
    bool helpful = params.at("helpful");
    std::string context = params.value("context", "");

    NodeId memory_id = NodeId::from_string(memory_id_str);
    auto node = mind->get(memory_id);

    if (!node) {
        return ToolResult::error("Memory not found: " + memory_id_str);
    }

    // Apply feedback - strengthen or weaken
    float delta = helpful ? 0.1f : -0.15f;  // Negative feedback slightly stronger

    if (helpful) {
        mind->strengthen(memory_id, delta);
    } else {
        mind->weaken(memory_id, -delta);
    }

    // Record the feedback event
    std::string feedback_text = (helpful ? "[HELPFUL] " : "[MISLEADING] ");
    feedback_text += "Memory: " + memory_id_str;
    if (!context.empty()) {
        feedback_text += " | " + context;
    }

    if (mind->has_yantra()) {
        mind->remember(feedback_text, NodeType::Episode, Confidence(0.5f));
    }

    json result = {
        {"memory_id", memory_id_str},
        {"helpful", helpful},
        {"delta", delta},
        {"new_confidence", node->kappa.effective() + delta}
    };

    return ToolResult::ok(helpful ? "Memory strengthened" : "Memory weakened", result);
}

// Update: replace node content and re-embed
inline ToolResult update(Mind* mind, const json& params) {
    std::string id_str = params.at("id");
    std::string content = params.at("content");

    NodeId id = NodeId::from_string(id_str);
    auto node_opt = mind->get(id);
    if (!node_opt) {
        return ToolResult::error("Node not found: " + id_str);
    }

    // Update payload and re-embed
    Node updated = *node_opt;
    updated.payload = std::vector<uint8_t>(content.begin(), content.end());
    updated.touch();

    // Re-compute embedding from new content
    if (mind->has_yantra()) {
        auto vec = mind->embed(content);
        if (vec) {
            updated.nu = std::move(*vec);
        }
    }

    if (!mind->update_node(id, updated)) {
        return ToolResult::error("Failed to update node: " + id_str);
    }

    json result = {
        {"id", id_str},
        {"content", content.substr(0, 50) + (content.size() > 50 ? "..." : "")},
        {"re_embedded", mind->has_yantra()}
    };

    return ToolResult::ok("Updated: " + content.substr(0, 50), result);
}

// Connect: create triplet as a first-class node
inline ToolResult connect(Mind* mind, const json& params) {
    std::string subject = params.at("subject");
    std::string predicate = params.at("predicate");
    std::string object = params.at("object");
    float confidence = params.value("confidence", 0.8f);

    // Create natural language content for the triplet
    std::string content = subject + " " + predicate + " " + object;

    // Tags for filtering and querying
    std::vector<std::string> tags = {
        "triplet",
        "predicate:" + predicate,
        "subject:" + subject,
        "object:" + object
    };

    // Store as a triplet node (searchable, embeddable, decays)
    NodeId id = mind->remember(content, NodeType::Triplet, Confidence(confidence), tags);

    // Also maintain graph structure for fast traversal
    mind->connect(subject, predicate, object, confidence);

    json result = {
        {"id", id.to_string()},
        {"content", content},
        {"subject", subject},
        {"predicate", predicate},
        {"object", object}
    };

    std::ostringstream ss;
    ss << "Connected: " << content;
    return ToolResult::ok(ss.str(), result);
}

// Query: search triplet relationships
inline ToolResult query(Mind* mind, const json& params) {
    std::string subject = params.value("subject", "");
    std::string predicate = params.value("predicate", "");
    std::string object = params.value("object", "");

    auto triplets = mind->query_triplets(subject, predicate, object);

    if (triplets.empty()) {
        return ToolResult::ok("No triplets found", {{"triplets", json::array()}});
    }

    json triplets_array = json::array();
    std::ostringstream ss;
    ss << "Found " << triplets.size() << " triplet(s):\n";

    for (const auto& t : triplets) {
        triplets_array.push_back({
            {"subject", t.subject.to_string()},
            {"predicate", t.predicate},
            {"object", t.object.to_string()},
            {"weight", t.weight}
        });
        ss << "  (" << t.subject.to_string().substr(0, 8) << ") --["
           << t.predicate << "]--> (" << t.object.to_string().substr(0, 8) << ")\n";
    }

    return ToolResult::ok(ss.str(), {{"triplets", triplets_array}});
}

// Register all learning tool handlers
inline void register_handlers(Mind* mind,
                               std::unordered_map<std::string, ToolHandler>& handlers) {
    handlers["grow"] = [mind](const json& p) { return grow(mind, p); };
    handlers["observe"] = [mind](const json& p) { return observe(mind, p); };
    handlers["feedback"] = [mind](const json& p) { return feedback(mind, p); };
    handlers["update"] = [mind](const json& p) { return update(mind, p); };
    handlers["connect"] = [mind](const json& p) { return connect(mind, p); };
    handlers["query"] = [mind](const json& p) { return query(mind, p); };
}

} // namespace chitta::rpc::tools::learning
