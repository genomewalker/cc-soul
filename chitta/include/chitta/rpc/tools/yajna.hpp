#pragma once
// RPC Yajna Tools: yajna_list, yajna_inspect, tag
//
// Tools for the epsilon-yajna ceremony - compressing verbose nodes to
// high-epiplexity patterns using the Oracle architecture.

#include "../types.hpp"
#include "../protocol.hpp"
#include "../../mind.hpp"
#include <sstream>
#include <algorithm>

namespace chitta::rpc::tools::yajna {

using json = nlohmann::json;

// Helper: extract title from text
inline std::string extract_title(const std::string& text, size_t max_len = 80) {
    size_t newline = text.find('\n');
    std::string title = (newline != std::string::npos && newline < max_len)
        ? text.substr(0, newline)
        : text.substr(0, std::min(text.length(), max_len));
    if (title.length() < text.length()) {
        while (!title.empty() && (title.back() == ' ' || title.back() == '\n')) {
            title.pop_back();
        }
        if (title.length() < text.length()) title += "...";
    }
    return title;
}

// Helper: convert edge type to string
inline std::string edge_type_str(EdgeType type) {
    switch (type) {
        case EdgeType::Similar: return "similar";
        case EdgeType::Supports: return "supports";
        case EdgeType::Contradicts: return "contradicts";
        case EdgeType::PartOf: return "part_of";
        case EdgeType::IsA: return "is_a";
        case EdgeType::Mentions: return "mentions";
        default: return "relates_to";
    }
}

// Register yajna tool schemas
inline void register_schemas(std::vector<ToolSchema>& tools) {
    tools.push_back({
        "yajna_list",
        "List ALL nodes for epsilon-yajna SSL+triplet conversion. Scans entire storage, "
        "excludes already-processed nodes (tag: epsilon-processed or ε-processed). "
        "Returns nodes sorted by length (longest first).",
        {
            {"type", "object"},
            {"properties", {
                {"filter", {{"type", "string"},
                          {"description", "Optional domain filter (e.g., 'cc-soul', 'architecture')"},
                          {"default", ""}}},
                {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 5000}, {"default", 100}}}
            }}
        }
    });

    tools.push_back({
        "yajna_inspect",
        "Get complete node content by ID for epsilon-yajna analysis. Returns full text, "
        "tags, edges, and computed epsilon for compression planning.",
        {
            {"type", "object"},
            {"properties", {
                {"id", {{"type", "string"}, {"description", "Node ID to inspect"}}}
            }},
            {"required", {"id"}}
        }
    });

    tools.push_back({
        "tag",
        "Add or remove tags from a node. Used for epsilon-yajna tracking (mark nodes as "
        "processed with 'epsilon-processed' tag) and organizing memories by categories.",
        {
            {"type", "object"},
            {"properties", {
                {"id", {{"type", "string"}, {"description", "Node ID to tag"}}},
                {"add", {{"type", "string"}, {"description", "Tag to add"}}},
                {"remove", {{"type", "string"}, {"description", "Tag to remove"}}}
            }},
            {"required", {"id"}}
        }
    });
}

// Register yajna tool handlers
inline void register_handlers(Mind* mind, std::unordered_map<std::string, ToolHandler>& handlers) {

    // yajna_list: List ALL nodes for SSL + triplet conversion
    handlers["yajna_list"] = [mind](const json& params) -> ToolResult {
        size_t limit = params.value("limit", 100);
        std::string filter = params.value("filter", "");  // Optional domain filter

        // Collect ALL unprocessed nodes by scanning entire storage
        struct YajnaNode {
            NodeId id;
            NodeType type;
            std::string title;
            size_t length;
            float epsilon;
        };
        std::vector<YajnaNode> nodes;

        mind->for_each_node([&](const NodeId& id, const Node& node) {
            // Skip triplets and entities (already in target format)
            if (node.node_type == NodeType::Entity || node.node_type == NodeType::Triplet) return;

            std::string text(node.payload.begin(), node.payload.end());

            // Skip if already processed (check tags directly to avoid lock)
            if (std::find(node.tags.begin(), node.tags.end(), "ε-processed") != node.tags.end()) return;
            if (std::find(node.tags.begin(), node.tags.end(), "epsilon-processed") != node.tags.end()) return;

            // Apply optional domain filter
            if (!filter.empty() && text.find(filter) == std::string::npos) return;

            std::string title = extract_title(text, 80);
            float epsilon = text.length() > 0
                ? std::min(1.0f, static_cast<float>(title.length()) / text.length() * 10.0f)
                : 1.0f;

            YajnaNode yn;
            yn.id = id;
            yn.type = node.node_type;
            yn.title = title;
            yn.length = text.length();
            yn.epsilon = epsilon;
            nodes.push_back(yn);
        });

        // Sort by length descending (longest first - most to compress)
        std::sort(nodes.begin(), nodes.end(),
            [](const YajnaNode& a, const YajnaNode& b) {
                return a.length > b.length;
            });

        json results = json::array();
        std::ostringstream ss;
        ss << "Nodes for epsilon-yajna (SSL + triplet conversion):\n";

        size_t count = 0;
        for (const auto& yn : nodes) {
            if (count >= limit) break;

            json node_json;
            node_json["id"] = yn.id.to_string();
            node_json["type"] = node_type_to_string(yn.type);
            node_json["title"] = yn.title;
            node_json["length"] = yn.length;
            node_json["epsilon"] = yn.epsilon;
            results.push_back(node_json);

            ss << "\n[" << yn.id.to_string() << "] " << yn.title;
            ss << " (" << yn.length << " chars, epsilon=" << static_cast<int>(yn.epsilon * 100) << "%)";

            count++;
        }

        ss << "\n\nTotal: " << nodes.size() << " nodes need processing";
        if (nodes.size() > limit) {
            ss << " (showing " << count << ")";
        }
        return ToolResult::ok(ss.str(), results);
    };

    // yajna_inspect: Get full node content by ID
    handlers["yajna_inspect"] = [mind](const json& params) -> ToolResult {
        std::string id_str = params.at("id");
        NodeId id = NodeId::from_string(id_str);

        auto node = mind->get(id);
        if (!node) {
            return ToolResult::error("Node not found: " + id_str);
        }

        std::string text(node->payload.begin(), node->payload.end());
        auto tags = mind->get_tags(id);

        // Get edges
        json edges = json::array();
        for (const auto& edge : node->edges) {
            if (auto target = mind->get(edge.target)) {
                std::string target_text(target->payload.begin(), target->payload.end());
                json edge_json;
                edge_json["target_id"] = edge.target.to_string();
                edge_json["type"] = edge_type_str(edge.type);
                edge_json["weight"] = edge.weight;
                edge_json["preview"] = extract_title(target_text, 60);
                edges.push_back(edge_json);
            }
        }

        // Compute epsilon estimate
        std::string title = extract_title(text, 80);
        float epsilon = std::min(1.0f, static_cast<float>(title.length()) / text.length() * 10.0f);

        json result;
        result["id"] = id_str;
        result["type"] = node_type_to_string(node->node_type);
        result["text"] = text;
        result["length"] = text.length();
        result["title"] = title;
        result["epsilon"] = epsilon;
        result["confidence"] = node->kappa.mu;
        result["tags"] = tags;
        result["edges"] = edges;
        result["created"] = node->tau_created;
        result["accessed"] = node->tau_accessed;

        std::ostringstream ss;
        ss << "=== Node " << id_str << " ===\n";
        ss << "Type: " << node_type_to_string(node->node_type) << "\n";
        ss << "Length: " << text.length() << " chars\n";
        ss << "Epsilon estimate: " << static_cast<int>(epsilon * 100) << "%\n";
        ss << "Tags: ";
        for (size_t i = 0; i < tags.size(); i++) {
            if (i > 0) ss << ", ";
            ss << tags[i];
        }
        ss << "\n\n--- Content ---\n" << text;

        if (!edges.empty()) {
            ss << "\n\n--- Edges (" << edges.size() << ") ---";
            for (const auto& e : edges) {
                ss << "\n  -> " << e["preview"].get<std::string>();
            }
        }

        return ToolResult::ok(ss.str(), result);
    };

    // tag: Add or remove tags from nodes
    handlers["tag"] = [mind](const json& params) -> ToolResult {
        std::string id_str = params.at("id");
        std::string add_tag = params.value("add", "");
        std::string remove_tag = params.value("remove", "");

        NodeId id = NodeId::from_string(id_str);

        auto node = mind->get(id);
        if (!node) {
            return ToolResult::error("Node not found: " + id_str);
        }

        json result;
        result["id"] = id_str;

        if (!add_tag.empty()) {
            mind->add_tag(id, add_tag);
            result["added"] = add_tag;
        }

        if (!remove_tag.empty()) {
            mind->remove_tag(id, remove_tag);
            result["removed"] = remove_tag;
        }

        if (add_tag.empty() && remove_tag.empty()) {
            result["tags"] = node->tags;
            return ToolResult::ok("Current tags", result);
        }

        // Tags are persisted via mind->add_tag/remove_tag which calls storage_.update_node()
        return ToolResult::ok("Tags updated", result);
    };
}

} // namespace chitta::rpc::tools::yajna
