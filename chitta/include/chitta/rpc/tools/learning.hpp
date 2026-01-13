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
                            {"description", "Epiplexity: reconstructability from title (Claude-assessed, 0-1)"}}},
                {"triplets", {{"type", "array"},
                             {"items", {{"type", "array"}, {"items", {{"type", "string"}}}, {"minItems", 3}, {"maxItems", 3}}},
                             {"description", "Related triplets: [[subject,predicate,object], ...]"}}}
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
                            {"description", "Epiplexity: reconstructability from title (Claude-assessed, 0-1)"}}},
                {"triplets", {{"type", "array"},
                             {"items", {{"type", "array"}, {"items", {{"type", "string"}}}, {"minItems", 3}, {"maxItems", 3}}},
                             {"description", "Related triplets: [[subject,predicate,object], ...]"}}}
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
        "record_outcome",
        "Record task outcome for utility learning (MemRL-inspired). "
        "Updates learned effectiveness of memories based on task success.",
        {
            {"type", "object"},
            {"properties", {
                {"memory_ids", {{"type", "array"}, {"items", {{"type", "string"}}},
                               {"description", "UUIDs of memories that were injected for this task"}}},
                {"success", {{"type", "number"}, {"minimum", 0}, {"maximum", 1},
                            {"description", "Task success score (0=failed, 1=succeeded)"}}},
                {"context", {{"type", "string"}, {"description", "Task description (optional)"}}},
                {"learning_rate", {{"type", "number"}, {"minimum", 0.01}, {"maximum", 0.5}, {"default", 0.1},
                                  {"description", "How quickly utility updates (default 0.1)"}}}
            }},
            {"required", {"memory_ids", "success"}}
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

    tools.push_back({
        "import_soul",
        "Import a .soul file into the mind. Soul files use SSL format for high-ε knowledge. "
        "Deduplicates by checking if similar content already exists.",
        {
            {"type", "object"},
            {"properties", {
                {"file", {{"type", "string"}, {"description", "Path to the .soul file"}}},
                {"replace", {{"type", "boolean"}, {"default", false},
                            {"description", "Remove existing vessel/codebase nodes before import (full rewiring)"}}}
            }},
            {"required", {"file"}}
        }
    });

    tools.push_back({
        "export_soul",
        "Export knowledge from the mind to a .soul file. Extracts nodes by tag in SSL format.",
        {
            {"type", "object"},
            {"properties", {
                {"file", {{"type", "string"}, {"description", "Path to output .soul file"}}},
                {"tag", {{"type", "string"}, {"description", "Tag to filter nodes (e.g., 'vessel', 'codebase', 'symbol')"}}},
                {"include_triplets", {{"type", "boolean"}, {"default", true},
                                     {"description", "Include related triplets in output"}}}
            }},
            {"required", {"file", "tag"}}
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

    // Create triplets if provided
    size_t triplet_count = 0;
    if (params.contains("triplets") && params["triplets"].is_array()) {
        for (const auto& t : params["triplets"]) {
            if (t.is_array() && t.size() == 3) {
                std::string subj = t[0].get<std::string>();
                std::string pred = t[1].get<std::string>();
                std::string obj = t[2].get<std::string>();
                mind->connect(subj, pred, obj, 0.7f);  // Default weight 0.7
                triplet_count++;
            }
        }
    }

    json result = {
        {"id", id.to_string()},
        {"type", type_str},
        {"title", title},
        {"confidence", confidence},
        {"epsilon", epsilon},
        {"triplets", triplet_count}
    };

    std::ostringstream ss;
    ss << "Grew " << type_str << ": " << (title.empty() ? content.substr(0, 50) : title);
    ss << " (id: " << id.to_string() << ", ε=" << static_cast<int>(epsilon * 100) << "%";
    if (triplet_count > 0) {
        ss << ", " << triplet_count << " triplet" << (triplet_count > 1 ? "s" : "");
    }
    ss << ")";

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

    // Create triplets if provided
    size_t triplet_count = 0;
    if (params.contains("triplets") && params["triplets"].is_array()) {
        for (const auto& t : params["triplets"]) {
            if (t.is_array() && t.size() == 3) {
                std::string subj = t[0].get<std::string>();
                std::string pred = t[1].get<std::string>();
                std::string obj = t[2].get<std::string>();
                mind->connect(subj, pred, obj, 0.7f);
                triplet_count++;
            }
        }
    }

    json result = {
        {"id", id.to_string()},
        {"category", category},
        {"title", title},
        {"decay_rate", decay},
        {"epsilon", epsilon},
        {"tags", tags_vec},
        {"triplets", triplet_count}
    };

    std::string msg = "Observed: " + title + " (ε=" + std::to_string(static_cast<int>(epsilon * 100)) + "%";
    if (triplet_count > 0) {
        msg += ", " + std::to_string(triplet_count) + " triplet" + (triplet_count > 1 ? "s" : "");
    }
    msg += ")";
    return ToolResult::ok(msg, result);
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

// Record task outcome for utility learning (MemRL-inspired)
// Updates learned effectiveness of memories based on task success
inline ToolResult record_outcome(Mind* mind, const json& params) {
    auto memory_ids = params.at("memory_ids").get<std::vector<std::string>>();
    float success = params.at("success").get<float>();
    std::string context = params.value("context", "");
    float learning_rate = params.value("learning_rate", 0.1f);

    // Validate
    if (success < 0.0f || success > 1.0f) {
        return ToolResult::error("Success must be between 0 and 1");
    }
    if (memory_ids.empty()) {
        return ToolResult::error("No memory IDs provided");
    }

    size_t updated = 0;
    json updated_ids = json::array();

    for (const auto& id_str : memory_ids) {
        NodeId id = NodeId::from_string(id_str);
        if (mind->get(id)) {
            mind->record_outcome(id, success, learning_rate);
            updated_ids.push_back(id_str);
            updated++;
        }
    }

    json result = {
        {"updated", updated},
        {"memory_ids", updated_ids},
        {"success", success},
        {"learning_rate", learning_rate}
    };

    std::string msg = "Recorded outcome (" + std::to_string(static_cast<int>(success * 100)) +
                      "% success) for " + std::to_string(updated) + " memories";
    return ToolResult::ok(msg, result);
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

// Remove: delete a node by ID
inline ToolResult remove(Mind* mind, const json& params) {
    std::string id_str = params.at("id");
    NodeId id = NodeId::from_string(id_str);

    if (!mind->remove_node(id)) {
        return ToolResult::error("Failed to remove node: " + id_str);
    }

    return ToolResult::ok("Removed: " + id_str, {{"id", id_str}, {"removed", true}});
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
// Uses persistent GraphStore (dictionary-encoded) for reliable persistence
inline ToolResult query(Mind* mind, const json& params) {
    std::string subject = params.value("subject", "");
    std::string predicate = params.value("predicate", "");
    std::string object = params.value("object", "");

    // Use query_graph which queries the persistent GraphStore
    auto triplets = mind->query_graph(subject, predicate, object);

    if (triplets.empty()) {
        return ToolResult::ok("No triplets found", {{"triplets", json::array()}});
    }

    json triplets_array = json::array();
    std::ostringstream ss;
    ss << "Found " << triplets.size() << " triplet(s):\n";

    for (const auto& [subj, pred, obj, weight] : triplets) {
        triplets_array.push_back({
            {"subject", subj},
            {"predicate", pred},
            {"object", obj},
            {"weight", weight}
        });
        ss << "  " << subj << " --[" << pred << "]--> " << obj << "\n";
    }

    return ToolResult::ok(ss.str(), {{"triplets", triplets_array}});
}

// Import soul file: parse .soul format and populate mind
inline ToolResult import_soul(Mind* mind, const json& params) {
    std::string file_path = params.at("file");
    bool replace = params.value("replace", false);

    std::ifstream file(file_path);
    if (!file.is_open()) {
        return ToolResult::error("Cannot open soul file: " + file_path);
    }

    int nodes_removed = 0;

    // Replace mode: remove all existing vessel/codebase nodes first
    if (replace) {
        auto vessel_nodes = mind->recall_by_tag("vessel", 1000);
        for (const auto& node : vessel_nodes) {
            mind->remove_node(node.id);
            nodes_removed++;
        }
        // Also remove codebase nodes without vessel tag
        auto codebase_nodes = mind->recall_by_tag("codebase", 1000);
        for (const auto& node : codebase_nodes) {
            if (!mind->has_tag(node.id, "vessel")) {
                mind->remove_node(node.id);
                nodes_removed++;
            }
        }
    }

    // Smart deduplication using embeddings (fast semantic similarity)
    constexpr float SIMILARITY_THRESHOLD = 0.95f;
    auto content_exists = [&mind, SIMILARITY_THRESHOLD](const std::string& text) -> bool {
        if (!mind->has_yantra()) return false;  // No embeddings = no dedup

        // Use vector search to find semantically similar content
        auto results = mind->recall(text, 1, true);  // Top 1 with vector search
        if (results.empty()) return false;

        // Check embedding similarity directly
        return results[0].similarity >= SIMILARITY_THRESHOLD;
    };

    // Triplet deduplication using graph overlap
    auto triplet_exists = [&mind](const std::string& subj, const std::string& pred, const std::string& obj) -> bool {
        auto existing = mind->query_graph(subj, pred, obj);
        return !existing.empty();
    };

    std::string line;
    std::string current_domain;
    std::string current_title;
    std::string current_location;
    bool vessel_mode = false;
    int nodes_created = 0;
    int nodes_skipped = 0;
    int triplets_created = 0;
    int triplets_skipped = 0;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        if (line.find("@vessel") == 0) {
            vessel_mode = true;
            continue;
        }

        if (line[0] == '[' && line.find(']') != std::string::npos) {
            size_t bracket_end = line.find(']');
            std::string bracket_content = line.substr(1, bracket_end - 1);

            if (bracket_content == "TRIPLET") {
                std::string triplet = line.substr(bracket_end + 1);
                start = triplet.find_first_not_of(" \t");
                if (start != std::string::npos) {
                    triplet = triplet.substr(start);
                    std::istringstream iss(triplet);
                    std::string subj, pred, obj;
                    if (iss >> subj >> pred) {
                        std::getline(iss, obj);
                        start = obj.find_first_not_of(" \t");
                        if (start != std::string::npos) obj = obj.substr(start);
                        if (!obj.empty()) {
                            if (triplet_exists(subj, pred, obj)) {
                                triplets_skipped++;
                            } else {
                                mind->connect(subj, pred, obj, vessel_mode ? 1.0f : 0.8f);
                                triplets_created++;
                            }
                        }
                    }
                }
                continue;
            } else if (bracket_content == "high-ε" || bracket_content == "high-e") {
                if (!current_title.empty()) {
                    std::string content = line.substr(bracket_end + 1);
                    start = content.find_first_not_of(" \t");
                    if (start != std::string::npos) content = content.substr(start);

                    if (!current_location.empty()) {
                        content += " @" + current_location;
                    }

                    std::string full_text;
                    if (!current_domain.empty()) {
                        full_text = "[" + current_domain + "] ";
                    }
                    full_text += current_title + ": " + content;

                    // Deduplication: skip if similar content exists
                    if (!replace && content_exists(full_text)) {
                        nodes_skipped++;
                    } else {
                        float confidence = vessel_mode ? 1.0f : 0.7f;
                        std::vector<std::string> tags = {"codebase", "architecture"};
                        if (!current_domain.empty()) {
                            tags.push_back("project:" + current_domain);
                        }
                        if (vessel_mode) {
                            tags.push_back("vessel");
                        }

                        NodeId id = mind->remember(full_text, NodeType::Wisdom, Confidence(confidence), tags);

                        if (auto node_opt = mind->get(id)) {
                            Node node = *node_opt;
                            node.epsilon = 0.8f;
                            mind->update_node(id, node);
                        }

                        nodes_created++;
                    }
                    current_title.clear();
                    current_location.clear();
                }
                continue;
            } else {
                // Save any pending title that didn't have a [high-ε] line
                if (!current_title.empty()) {
                    std::string full_text;
                    if (!current_domain.empty()) {
                        full_text = "[" + current_domain + "] ";
                    }
                    full_text += current_title;
                    if (!current_location.empty()) {
                        full_text += " @" + current_location;
                    }

                    // Deduplication: skip if similar content exists
                    if (!replace && content_exists(full_text)) {
                        nodes_skipped++;
                    } else {
                        float confidence = vessel_mode ? 1.0f : 0.6f;
                        std::vector<std::string> tags = {"codebase"};
                        if (!current_domain.empty()) {
                            tags.push_back("project:" + current_domain);
                        }
                        if (!current_location.empty()) {
                            tags.push_back("symbol");
                        }
                        if (vessel_mode) {
                            tags.push_back("vessel");
                        }

                        mind->remember(full_text, NodeType::Term, Confidence(confidence), tags);
                        nodes_created++;
                    }
                }

                current_domain = bracket_content;
                std::string rest = line.substr(bracket_end + 1);
                start = rest.find_first_not_of(" \t");
                if (start != std::string::npos) rest = rest.substr(start);

                size_t loc_pos = rest.rfind(" @");
                if (loc_pos != std::string::npos) {
                    current_location = rest.substr(loc_pos + 2);
                    rest = rest.substr(0, loc_pos);
                } else {
                    current_location.clear();
                }
                current_title = rest;
            }
        }
    }

    // Don't forget the last pending title
    if (!current_title.empty()) {
        std::string full_text;
        if (!current_domain.empty()) {
            full_text = "[" + current_domain + "] ";
        }
        full_text += current_title;
        if (!current_location.empty()) {
            full_text += " @" + current_location;
        }

        // Deduplication: skip if similar content exists
        if (!replace && content_exists(full_text)) {
            nodes_skipped++;
        } else {
            float confidence = vessel_mode ? 1.0f : 0.6f;
            std::vector<std::string> tags = {"codebase"};
            if (!current_domain.empty()) {
                tags.push_back("project:" + current_domain);
            }
            if (!current_location.empty()) {
                tags.push_back("symbol");
            }
            if (vessel_mode) {
                tags.push_back("vessel");
            }

            mind->remember(full_text, NodeType::Term, Confidence(confidence), tags);
            nodes_created++;
        }
    }

    json result = {
        {"file", file_path},
        {"nodes_removed", nodes_removed},
        {"nodes_created", nodes_created},
        {"nodes_skipped", nodes_skipped},
        {"triplets_created", triplets_created},
        {"triplets_skipped", triplets_skipped},
        {"vessel_mode", vessel_mode},
        {"replace_mode", replace}
    };

    std::ostringstream ss;
    if (replace && nodes_removed > 0) {
        ss << "Rewired: removed " << nodes_removed << " old nodes, ";
    }
    ss << "imported " << nodes_created << " nodes";
    if (nodes_skipped > 0) {
        ss << " (skipped " << nodes_skipped << " existing)";
    }
    ss << ", " << triplets_created << " triplets";
    if (triplets_skipped > 0) {
        ss << " (skipped " << triplets_skipped << " existing)";
    }
    return ToolResult::ok(ss.str(), result);
}

inline ToolResult export_soul(Mind* mind, const json& params) {
    std::string file_path = params.at("file");
    std::string tag = params.at("tag");
    bool include_triplets = params.value("include_triplets", true);

    std::ofstream file(file_path);
    if (!file.is_open()) {
        return ToolResult::error("Cannot open output file: " + file_path);
    }

    // Header
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    file << "# Soul export - tag: " << tag << "\n";
    file << "# Generated: " << std::ctime(&time_t);
    file << "\n@vessel\n\n";

    // Get nodes by tag
    auto nodes = mind->recall_by_tag(tag, 10000);
    int nodes_exported = 0;

    for (const auto& node : nodes) {
        std::string content = node.text;
        if (content.empty() && !node.payload.empty()) {
            content = std::string(node.payload.begin(), node.payload.end());
        }
        if (content.empty()) continue;

        // Parse domain from [domain] prefix
        std::string domain;
        std::string rest = content;
        if (content[0] == '[') {
            size_t bracket_end = content.find(']');
            if (bracket_end != std::string::npos) {
                domain = content.substr(1, bracket_end - 1);
                rest = content.substr(bracket_end + 1);
                size_t start = rest.find_first_not_of(" \t");
                if (start != std::string::npos) rest = rest.substr(start);
            }
        }

        // Output in SSL format
        if (!domain.empty()) {
            file << "[" << domain << "] " << rest << "\n";
        } else {
            file << rest << "\n";
        }
        nodes_exported++;
    }

    // Export triplets if requested
    int triplets_exported = 0;
    if (include_triplets) {
        file << "\n# Triplets\n";
        auto triplets = mind->all_triplets();
        for (const auto& t : triplets) {
            file << "[TRIPLET] " << t.subject << " " << t.predicate << " " << t.object << "\n";
            triplets_exported++;
        }
    }

    json result = {
        {"file", file_path},
        {"tag", tag},
        {"nodes_exported", nodes_exported},
        {"triplets_exported", triplets_exported}
    };

    std::ostringstream ss;
    ss << "Exported " << nodes_exported << " nodes";
    if (triplets_exported > 0) {
        ss << ", " << triplets_exported << " triplets";
    }
    ss << " to " << file_path;
    return ToolResult::ok(ss.str(), result);
}

// Resolve entity name to NodeId via EntityIndex (O(1))
inline ToolResult resolve_entity(Mind* mind, const json& params) {
    std::string entity = params.at("entity");

    auto node_id = mind->resolve_entity(entity);
    if (!node_id) {
        return ToolResult::ok("Entity not linked", json{{"entity", entity}, {"linked", false}});
    }

    // Fetch node details
    auto node = mind->get_node(*node_id);
    json result = {
        {"entity", entity},
        {"linked", true},
        {"node_id", node_id->to_string()}
    };

    if (node) {
        result["node_type"] = static_cast<int>(node->node_type);
        auto text = mind->payload_to_text(node->payload);
        if (text) {
            result["preview"] = text->substr(0, 200);
        }
    }

    return ToolResult::ok("Entity resolved to node " + node_id->to_string(), result);
}

// Link entity name to existing node
inline ToolResult link_entity(Mind* mind, const json& params) {
    std::string entity = params.at("entity");
    std::string node_id_str = params.at("node_id");

    NodeId node_id = NodeId::from_string(node_id_str);
    mind->link_entity(entity, node_id);

    return ToolResult::ok("Linked '" + entity + "' to " + node_id_str,
        json{{"entity", entity}, {"node_id", node_id_str}});
}

// Bootstrap EntityIndex from existing triplets/nodes
inline ToolResult bootstrap_entity_index(Mind* mind, const json& /*params*/) {
    size_t linked = mind->bootstrap_entity_index();
    size_t total = mind->linked_entity_count();

    std::ostringstream ss;
    ss << "Bootstrapped " << linked << " new entity links (total: " << total << ")";
    return ToolResult::ok(ss.str(), json{{"new_links", linked}, {"total_links", total}});
}

// List all linked entities
inline ToolResult list_entities(Mind* mind, const json& /*params*/) {
    auto entities = mind->linked_entities();

    json items = json::array();
    for (const auto& [entity, node_id] : entities) {
        items.push_back({{"entity", entity}, {"node_id", node_id.to_string()}});
    }

    std::ostringstream ss;
    ss << entities.size() << " linked entities";
    return ToolResult::ok(ss.str(), json{{"count", entities.size()}, {"entities", items}});
}

// Register all learning tool handlers
inline void register_handlers(Mind* mind,
                               std::unordered_map<std::string, ToolHandler>& handlers) {
    handlers["grow"] = [mind](const json& p) { return grow(mind, p); };
    handlers["observe"] = [mind](const json& p) { return observe(mind, p); };
    handlers["feedback"] = [mind](const json& p) { return feedback(mind, p); };
    handlers["record_outcome"] = [mind](const json& p) { return record_outcome(mind, p); };
    handlers["update"] = [mind](const json& p) { return update(mind, p); };
    handlers["remove"] = [mind](const json& p) { return remove(mind, p); };
    handlers["connect"] = [mind](const json& p) { return connect(mind, p); };
    handlers["query"] = [mind](const json& p) { return query(mind, p); };
    handlers["import_soul"] = [mind](const json& p) { return import_soul(mind, p); };
    handlers["export_soul"] = [mind](const json& p) { return export_soul(mind, p); };
    handlers["resolve_entity"] = [mind](const json& p) { return resolve_entity(mind, p); };
    handlers["link_entity"] = [mind](const json& p) { return link_entity(mind, p); };
    handlers["bootstrap_entity_index"] = [mind](const json& p) { return bootstrap_entity_index(mind, p); };
    handlers["list_entities"] = [mind](const json& p) { return list_entities(mind, p); };
}

} // namespace chitta::rpc::tools::learning
