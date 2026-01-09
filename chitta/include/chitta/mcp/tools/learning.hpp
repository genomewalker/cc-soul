#pragma once
// MCP Learning Tools: grow, observe, feedback
//
// Tools for adding knowledge to the soul and providing feedback
// on existing memories to strengthen or weaken them.

#include "../types.hpp"
#include "../../mind.hpp"
#include <sstream>

namespace chitta::mcp::tools::learning {

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
                {"confidence", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"default", 0.8}}}
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
                {"tags", {{"type", "string"}, {"description", "Comma-separated tags for filtering"}}}
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
}

// Tool implementations
inline ToolResult grow(Mind* mind, const json& params) {
    std::string type_str = params.at("type");
    std::string content = params.at("content");
    std::string title = params.value("title", "");
    std::string domain = params.value("domain", "");
    float confidence = params.value("confidence", 0.8f);

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

    json result = {
        {"id", id.to_string()},
        {"type", type_str},
        {"title", title},
        {"confidence", confidence}
    };

    std::ostringstream ss;
    ss << "Grew " << type_str << ": " << (title.empty() ? content.substr(0, 50) : title);
    ss << " (id: " << id.to_string() << ")";

    return ToolResult::ok(ss.str(), result);
}

inline ToolResult observe(Mind* mind, const json& params) {
    std::string category = params.at("category");
    std::string title = params.at("title");
    std::string content = params.at("content");
    std::string project = params.value("project", "");
    std::string tags_str = params.value("tags", "");

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

    json result = {
        {"id", id.to_string()},
        {"category", category},
        {"title", title},
        {"decay_rate", decay},
        {"tags", tags_vec}
    };

    return ToolResult::ok("Observed: " + title, result);
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

// Register all learning tool handlers
inline void register_handlers(Mind* mind,
                               std::unordered_map<std::string, ToolHandler>& handlers) {
    handlers["grow"] = [mind](const json& p) { return grow(mind, p); };
    handlers["observe"] = [mind](const json& p) { return observe(mind, p); };
    handlers["feedback"] = [mind](const json& p) { return feedback(mind, p); };
}

} // namespace chitta::mcp::tools::learning
