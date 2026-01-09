#pragma once
// MCP Types: Tool schema and result types
//
// Defines the data structures used for MCP tool registration
// and execution results.

#include "../types.hpp"  // For NodeType
#include <nlohmann/json.hpp>
#include <string>
#include <functional>

namespace chitta::mcp {

using json = nlohmann::json;

// Tool schema definition for MCP tools/list
struct ToolSchema {
    std::string name;
    std::string description;
    json input_schema;
};

// Tool execution result
struct ToolResult {
    bool is_error = false;
    std::string content;      // Human-readable text response
    json structured;          // Optional structured JSON data

    // Convenience constructors
    static ToolResult ok(const std::string& text, const json& data = json()) {
        return {false, text, data};
    }

    static ToolResult error(const std::string& message) {
        return {true, message, json()};
    }
};

// Tool handler function type
using ToolHandler = std::function<ToolResult(const json&)>;

// NodeType to string conversion (used by multiple tools)
inline std::string node_type_to_string_impl(int type) {
    switch (type) {
        case 0: return "wisdom";
        case 1: return "belief";
        case 2: return "intention";
        case 3: return "aspiration";
        case 4: return "episode";
        case 5: return "operation";
        case 6: return "invariant";
        case 7: return "identity";
        case 8: return "term";
        case 9: return "failure";
        case 10: return "dream";
        case 11: return "voice";
        case 12: return "meta";
        case 13: return "gap";
        case 14: return "question";
        case 15: return "story_thread";
        case 16: return "ledger";
        case 17: return "entity";
        default: return "unknown";
    }
}

inline std::string node_type_to_string(NodeType type) {
    return node_type_to_string_impl(static_cast<int>(type));
}

inline std::string node_type_to_string(int type) {
    return node_type_to_string_impl(type);
}

} // namespace chitta::mcp
