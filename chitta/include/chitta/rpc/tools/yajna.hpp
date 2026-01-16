#pragma once
// RPC Yajna Tools: yajna_list, yajna_inspect, tag, yajna_mark_processed, batch_remove, batch_tag
//
// Tools for the epsilon-yajna ceremony - compressing verbose nodes to
// high-epiplexity patterns using the Oracle architecture.

#include "../types.hpp"
#include "../protocol.hpp"
#include "../../mind.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <fstream>

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
        case EdgeType::AppliedIn: return "applied_in";
        case EdgeType::Contradicts: return "contradicts";
        case EdgeType::Supports: return "supports";
        case EdgeType::EvolvedFrom: return "evolved_from";
        case EdgeType::PartOf: return "part_of";
        case EdgeType::TriggeredBy: return "triggered_by";
        case EdgeType::CreatedBy: return "created_by";
        case EdgeType::ScopedTo: return "scoped_to";
        case EdgeType::Answers: return "answers";
        case EdgeType::Addresses: return "addresses";
        case EdgeType::Continues: return "continues";
        case EdgeType::Mentions: return "mentions";
        case EdgeType::IsA: return "is_a";
        case EdgeType::RelatesTo: return "relates_to";
        case EdgeType::Uses: return "uses";
        case EdgeType::Implements: return "implements";
        case EdgeType::Contains: return "contains";
        case EdgeType::Causes: return "causes";
        case EdgeType::Requires: return "requires";
        default: return "unknown";
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
        "node_edges",
        "Get edges for a node by name or ID. Shows all connections with type, weight, and "
        "target preview. Finds node by semantic search if name given.",
        {
            {"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}, {"description", "Node name or ID to find edges for"}}},
                {"direction", {{"type", "string"}, {"enum", {"outgoing", "incoming", "both"}},
                             {"default", "both"}, {"description", "Edge direction to show"}}}
            }},
            {"required", {"query"}}
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

    tools.push_back({
        "yajna_mark_processed",
        "Batch mark nodes as ε-processed. Processes all unprocessed nodes meeting criteria: "
        "SSL format (has → arrow) OR epsilon >= threshold. Efficient C++ batch operation.",
        {
            {"type", "object"},
            {"properties", {
                {"epsilon_threshold", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0},
                                      {"default", 0.8}, {"description", "Min epsilon to auto-mark (0.8 = 80%)"}}},
                {"dry_run", {{"type", "boolean"}, {"default", true},
                           {"description", "Preview only, don't actually tag"}}},
                {"filter", {{"type", "string"}, {"default", ""},
                          {"description", "Only process nodes matching this text filter"}}}
            }}
        }
    });

    tools.push_back({
        "batch_remove",
        "Remove multiple nodes from a file of IDs. One UUID per line. Efficient C++ batch.",
        {
            {"type", "object"},
            {"properties", {
                {"file", {{"type", "string"}, {"description", "Path to file with UUIDs (one per line)"}}},
                {"dry_run", {{"type", "boolean"}, {"default", true}, {"description", "Preview only"}}}
            }},
            {"required", {"file"}}
        }
    });

    tools.push_back({
        "batch_tag",
        "Tag multiple nodes from a file of IDs. One UUID per line. Efficient C++ batch.",
        {
            {"type", "object"},
            {"properties", {
                {"file", {{"type", "string"}, {"description", "Path to file with UUIDs (one per line)"}}},
                {"add", {{"type", "string"}, {"description", "Tag to add to all nodes"}}},
                {"dry_run", {{"type", "boolean"}, {"default", true}, {"description", "Preview only"}}}
            }},
            {"required", {"file", "add"}}
        }
    });

    tools.push_back({
        "backfill_triplet_edges",
        "Backfill edges for existing triplets. Creates bidirectional edges between entity nodes "
        "for all triplets in the graph store that don't have corresponding edges. Use after "
        "upgrading to unified triplet-node system.",
        {
            {"type", "object"},
            {"properties", {
                {"dry_run", {{"type", "boolean"}, {"default", true},
                           {"description", "Preview only, don't actually create edges"}}}
            }}
        }
    });

    tools.push_back({
        "import_soul",
        "Import a .soul file into the mind. Parses SSL format with [domain] patterns, "
        "[TRIPLET] relationships, and [high-ε] content. Supports @vessel directive for "
        "protected nodes.",
        {
            {"type", "object"},
            {"properties", {
                {"file", {{"type", "string"}, {"description", "Path to .soul file"}}},
                {"update", {{"type", "boolean"}, {"default", false},
                          {"description", "Update existing nodes if they match"}}}
            }},
            {"required", {"file"}}
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

            // Skip if already processed (get tags from index, not node.tags which may be empty during iteration)
            auto tags = mind->get_tags(id);
            if (std::find(tags.begin(), tags.end(), "ε-processed") != tags.end()) return;
            if (std::find(tags.begin(), tags.end(), "epsilon-processed") != tags.end()) return;

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

    // node_edges: Get edges for a node by name or ID
    handlers["node_edges"] = [mind](const json& params) -> ToolResult {
        std::string query = params.at("query");
        std::string direction = params.value("direction", "both");

        // Try to parse as UUID first
        NodeId target_id;
        bool found = false;
        std::string node_name;

        if (query.length() == 36 && query[8] == '-') {
            // Looks like UUID
            target_id = NodeId::from_string(query);
            if (auto node = mind->get(target_id)) {
                found = true;
                node_name = std::string(node->payload.begin(), node->payload.end());
                if (node_name.length() > 60) node_name = node_name.substr(0, 57) + "...";
            }
        }

        // If not UUID or not found, search by name
        if (!found) {
            auto results = mind->recall(query, 1);
            if (results.empty()) {
                return ToolResult::error("No node found for: " + query);
            }
            target_id = results[0].id;
            node_name = results[0].text;
            if (node_name.length() > 60) node_name = node_name.substr(0, 57) + "...";
        }

        auto node = mind->get(target_id);
        if (!node) {
            return ToolResult::error("Node not found");
        }

        json result;
        result["id"] = target_id.to_string();
        result["name"] = node_name;
        result["type"] = node_type_to_string(node->node_type);

        std::ostringstream ss;
        ss << "═══ " << node_name << " ═══\n";
        ss << "ID: " << target_id.to_string() << "\n";
        ss << "Type: " << node_type_to_string(node->node_type) << "\n\n";

        // Outgoing edges (from this node)
        json outgoing = json::array();
        if (direction == "outgoing" || direction == "both") {
            ss << "── Outgoing Edges (" << node->edges.size() << ") ──\n";
            for (const auto& edge : node->edges) {
                std::string target_text;
                std::string target_type;
                if (auto target = mind->get(edge.target)) {
                    target_text = std::string(target->payload.begin(), target->payload.end());
                    if (target_text.length() > 50) target_text = target_text.substr(0, 47) + "...";
                    target_type = node_type_to_string(target->node_type);
                } else {
                    target_text = "(deleted)";
                    target_type = "?";
                }

                json e;
                e["target_id"] = edge.target.to_string();
                e["target"] = target_text;
                e["target_type"] = target_type;
                e["type"] = edge_type_str(edge.type);
                e["weight"] = edge.weight;
                outgoing.push_back(e);

                ss << "  ──[" << edge_type_str(edge.type) << ": "
                   << std::fixed << std::setprecision(2) << edge.weight
                   << "]──► " << target_text << " (" << target_type << ")\n";
            }
            if (node->edges.empty()) {
                ss << "  (none)\n";
            }
        }
        result["outgoing"] = outgoing;

        // Incoming edges (to this node) - scan all nodes
        json incoming = json::array();
        if (direction == "incoming" || direction == "both") {
            ss << "\n── Incoming Edges ──\n";
            size_t incoming_count = 0;

            mind->for_each_node([&](const NodeId& id, const Node& n) {
                if (id == target_id) return;
                for (const auto& edge : n.edges) {
                    if (edge.target == target_id) {
                        std::string source_text(n.payload.begin(), n.payload.end());
                        if (source_text.length() > 50) source_text = source_text.substr(0, 47) + "...";

                        json e;
                        e["source_id"] = id.to_string();
                        e["source"] = source_text;
                        e["source_type"] = node_type_to_string(n.node_type);
                        e["type"] = edge_type_str(edge.type);
                        e["weight"] = edge.weight;
                        incoming.push_back(e);

                        ss << "  " << source_text << " (" << node_type_to_string(n.node_type) << ")\n"
                           << "    ──[" << edge_type_str(edge.type) << ": "
                           << std::fixed << std::setprecision(2) << edge.weight << "]──►\n";
                        incoming_count++;
                    }
                }
            });

            if (incoming_count == 0) {
                ss << "  (none)\n";
            }
        }
        result["incoming"] = incoming;

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

    // yajna_mark_processed: Batch mark high-epsilon nodes as processed
    handlers["yajna_mark_processed"] = [mind](const json& params) -> ToolResult {
        float epsilon_threshold = params.value("epsilon_threshold", 0.8f);
        bool dry_run = params.value("dry_run", true);
        std::string filter = params.value("filter", "");

        struct Candidate {
            NodeId id;
            std::string title;
            float epsilon;
            bool has_arrow;
        };
        std::vector<Candidate> candidates;

        // Scan all unprocessed nodes
        mind->for_each_node([&](const NodeId& id, const Node& node) {
            if (node.node_type == NodeType::Entity || node.node_type == NodeType::Triplet) return;

            // Skip already processed (get tags from index, not node.tags which may be empty during iteration)
            auto tags = mind->get_tags(id);
            if (std::find(tags.begin(), tags.end(), "ε-processed") != tags.end()) return;
            if (std::find(tags.begin(), tags.end(), "epsilon-processed") != tags.end()) return;

            std::string text(node.payload.begin(), node.payload.end());
            if (!filter.empty() && text.find(filter) == std::string::npos) return;

            std::string title = extract_title(text, 80);
            float epsilon = text.length() > 0
                ? std::min(1.0f, static_cast<float>(title.length()) / text.length() * 10.0f)
                : 1.0f;

            // Check for SSL format (has → arrow) - this is UTF-8 for →
            bool has_arrow = text.find("\xe2\x86\x92") != std::string::npos;

            // Accept if: has SSL arrow OR meets epsilon threshold
            if (has_arrow || epsilon >= epsilon_threshold) {
                candidates.push_back({id, title, epsilon, has_arrow});
            }
        });

        // Sort by epsilon descending
        std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b) { return a.epsilon > b.epsilon; });

        size_t tagged = 0;
        if (!dry_run) {
            for (const auto& c : candidates) {
                mind->add_tag(c.id, "ε-processed");
                tagged++;
            }
        }

        json result;
        result["candidates"] = candidates.size();
        result["tagged"] = tagged;
        result["dry_run"] = dry_run;

        std::ostringstream ss;
        if (dry_run) {
            ss << "Would mark " << candidates.size() << " nodes as ε-processed:\n";
            size_t shown = 0;
            for (const auto& c : candidates) {
                if (shown++ >= 20) { ss << "... and " << (candidates.size() - 20) << " more\n"; break; }
                ss << "  [" << (c.has_arrow ? "→" : std::to_string(static_cast<int>(c.epsilon * 100)) + "%")
                   << "] " << c.title << "\n";
            }
        } else {
            ss << "Marked " << tagged << " nodes as ε-processed";
        }
        return ToolResult::ok(ss.str(), result);
    };

    // batch_remove: Remove nodes from file of IDs
    handlers["batch_remove"] = [mind](const json& params) -> ToolResult {
        std::string file_path = params.at("file");
        bool dry_run = params.value("dry_run", true);

        std::ifstream file(file_path);
        if (!file.is_open()) {
            return ToolResult::error("Cannot open file: " + file_path);
        }

        std::vector<std::string> ids;
        std::string line;
        while (std::getline(file, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') continue;
            // Trim whitespace
            size_t start = line.find_first_not_of(" \t\r\n");
            size_t end = line.find_last_not_of(" \t\r\n");
            if (start != std::string::npos) {
                ids.push_back(line.substr(start, end - start + 1));
            }
        }

        size_t removed = 0;
        size_t not_found = 0;
        if (!dry_run) {
            for (const auto& id_str : ids) {
                NodeId id = NodeId::from_string(id_str);
                if (mind->get(id)) {
                    mind->remove_node(id);
                    removed++;
                } else {
                    not_found++;
                }
            }
        }

        json result;
        result["file"] = file_path;
        result["ids_in_file"] = ids.size();
        result["removed"] = removed;
        result["not_found"] = not_found;
        result["dry_run"] = dry_run;

        std::ostringstream ss;
        if (dry_run) {
            ss << "Would remove " << ids.size() << " nodes from " << file_path;
        } else {
            ss << "Removed " << removed << " nodes";
            if (not_found > 0) ss << " (" << not_found << " not found)";
        }
        return ToolResult::ok(ss.str(), result);
    };

    // batch_tag: Tag nodes from file of IDs
    handlers["batch_tag"] = [mind](const json& params) -> ToolResult {
        std::string file_path = params.at("file");
        std::string add_tag = params.at("add");
        bool dry_run = params.value("dry_run", true);

        std::ifstream file(file_path);
        if (!file.is_open()) {
            return ToolResult::error("Cannot open file: " + file_path);
        }

        std::vector<std::string> ids;
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t start = line.find_first_not_of(" \t\r\n");
            size_t end = line.find_last_not_of(" \t\r\n");
            if (start != std::string::npos) {
                ids.push_back(line.substr(start, end - start + 1));
            }
        }

        size_t tagged = 0;
        size_t not_found = 0;
        if (!dry_run) {
            for (const auto& id_str : ids) {
                NodeId id = NodeId::from_string(id_str);
                if (mind->get(id)) {
                    mind->add_tag(id, add_tag);
                    tagged++;
                } else {
                    not_found++;
                }
            }
        }

        json result;
        result["file"] = file_path;
        result["tag"] = add_tag;
        result["ids_in_file"] = ids.size();
        result["tagged"] = tagged;
        result["not_found"] = not_found;
        result["dry_run"] = dry_run;

        std::ostringstream ss;
        if (dry_run) {
            ss << "Would tag " << ids.size() << " nodes with '" << add_tag << "'";
        } else {
            ss << "Tagged " << tagged << " nodes with '" << add_tag << "'";
            if (not_found > 0) ss << " (" << not_found << " not found)";
        }
        return ToolResult::ok(ss.str(), result);
    };

    // backfill_triplet_edges: Create edges for existing triplets
    handlers["backfill_triplet_edges"] = [mind](const json& params) -> ToolResult {
        bool dry_run = params.value("dry_run", true);

        // Get all triplets from graph store
        auto triplets = mind->query_graph("", "", "");  // All triplets

        struct EdgeToCreate {
            std::string subject;
            std::string predicate;
            std::string object;
            std::optional<NodeId> subj_id;
            std::optional<NodeId> obj_id;
            bool has_forward_edge;
            bool has_reverse_edge;
        };
        std::vector<EdgeToCreate> edges_needed;

        for (const auto& [subject, predicate, object, weight] : triplets) {
            // Find entity nodes for subject and object
            auto subj_id = mind->find_entity(subject);
            auto obj_id = mind->find_entity(object);

            if (!subj_id || !obj_id) continue;  // Skip if entities don't exist

            // Check if edges already exist
            auto subj_node = mind->get(*subj_id);
            auto obj_node = mind->get(*obj_id);
            if (!subj_node || !obj_node) continue;

            bool has_forward = false;
            bool has_reverse = false;

            for (const auto& edge : subj_node->edges) {
                if (edge.target == *obj_id) {
                    has_forward = true;
                    break;
                }
            }

            for (const auto& edge : obj_node->edges) {
                if (edge.target == *subj_id) {
                    has_reverse = true;
                    break;
                }
            }

            if (!has_forward || !has_reverse) {
                edges_needed.push_back({
                    subject, predicate, object,
                    subj_id, obj_id,
                    has_forward, has_reverse
                });
            }
        }

        size_t forward_created = 0;
        size_t reverse_created = 0;

        if (!dry_run) {
            for (const auto& e : edges_needed) {
                EdgeType edge_type = predicate_to_edge_type(e.predicate);
                EdgeType reverse_type = reverse_edge_type(edge_type);

                if (!e.has_forward_edge && e.subj_id && e.obj_id) {
                    mind->connect(*e.subj_id, *e.obj_id, edge_type, 1.0f);
                    forward_created++;
                }

                if (!e.has_reverse_edge && e.subj_id && e.obj_id) {
                    mind->connect(*e.obj_id, *e.subj_id, reverse_type, 1.0f);
                    reverse_created++;
                }
            }
        }

        json result;
        result["triplets_total"] = triplets.size();
        result["edges_needed"] = edges_needed.size();
        result["forward_created"] = forward_created;
        result["reverse_created"] = reverse_created;
        result["dry_run"] = dry_run;

        std::ostringstream ss;
        if (dry_run) {
            ss << "Would create edges for " << edges_needed.size() << " triplets:\n";
            size_t shown = 0;
            for (const auto& e : edges_needed) {
                if (shown++ >= 20) {
                    ss << "  ... and " << (edges_needed.size() - 20) << " more\n";
                    break;
                }
                ss << "  " << e.subject << " --[" << e.predicate << "]--> " << e.object;
                if (!e.has_forward_edge) ss << " [+fwd]";
                if (!e.has_reverse_edge) ss << " [+rev]";
                ss << "\n";
            }
            ss << "\nTotal: " << triplets.size() << " triplets, "
               << edges_needed.size() << " need edges";
        } else {
            ss << "Created " << forward_created << " forward edges and "
               << reverse_created << " reverse edges for " << edges_needed.size() << " triplets";
        }
        return ToolResult::ok(ss.str(), result);
    };

    // import_soul: Import .soul file into mind
    handlers["import_soul"] = [mind](const json& params) -> ToolResult {
        std::string soul_file = params.value("file", "");
        bool update_mode = params.value("update", false);
        (void)update_mode;  // Reserved for future use

        if (soul_file.empty()) {
            return ToolResult::error("Missing required parameter: file");
        }

        std::ifstream file(soul_file);
        if (!file.is_open()) {
            return ToolResult::error("Cannot open soul file: " + soul_file);
        }

        std::string line;
        std::string current_domain;
        std::string current_title;
        std::string current_location;
        bool vessel_mode = false;
        int nodes_created = 0;
        int triplets_created = 0;

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
                                mind->connect(subj, pred, obj, vessel_mode ? 1.0f : 0.8f);
                                triplets_created++;
                            }
                        }
                    }
                    continue;
                } else if (bracket_content == "high-ε" || bracket_content == "high-e" || bracket_content == "ε") {
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

                        float confidence = vessel_mode ? 1.0f : 0.7f;
                        NodeId id;
                        if (mind->has_yantra()) {
                            id = mind->remember(full_text, NodeType::Wisdom, Confidence(confidence));
                        } else {
                            id = mind->remember(NodeType::Wisdom, Vector::zeros(), Confidence(confidence),
                                               std::vector<uint8_t>(full_text.begin(), full_text.end()));
                        }

                        mind->add_tag(id, "codebase");
                        mind->add_tag(id, "architecture");
                        if (!current_domain.empty()) {
                            mind->add_tag(id, "project:" + current_domain);
                        }
                        if (vessel_mode) {
                            mind->add_tag(id, "vessel");
                        }

                        if (auto node_opt = mind->get(id)) {
                            Node node = *node_opt;
                            node.epsilon = 0.8f;
                            mind->update_node(id, node);
                        }

                        // Auto-link wisdom to subject entity
                        // Extract subject from title (before first →)
                        size_t arrow_pos = current_title.find("→");
                        if (arrow_pos != std::string::npos) {
                            std::string subject = current_title.substr(0, arrow_pos);
                            // Trim whitespace
                            while (!subject.empty() && subject.back() == ' ') subject.pop_back();
                            if (!subject.empty()) {
                                // Find or create entity, link wisdom to it
                                auto entity_id = mind->find_or_create_entity(subject);
                                mind->connect(id, entity_id, EdgeType::Mentions, 1.0f);
                                mind->connect(entity_id, id, EdgeType::Mentions, 1.0f);
                            }
                        }

                        nodes_created++;
                        current_title.clear();
                        current_location.clear();
                    }
                    continue;
                } else {
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

        json result;
        result["nodes_created"] = nodes_created;
        result["triplets_created"] = triplets_created;
        result["vessel_mode"] = vessel_mode;
        result["file"] = soul_file;

        std::ostringstream ss;
        ss << "Soul import complete:\n";
        ss << "  Nodes created: " << nodes_created << "\n";
        ss << "  Triplets created: " << triplets_created << "\n";
        ss << "  Vessel mode: " << (vessel_mode ? "yes" : "no");

        return ToolResult::ok(ss.str(), result);
    };
}

} // namespace chitta::rpc::tools::yajna
