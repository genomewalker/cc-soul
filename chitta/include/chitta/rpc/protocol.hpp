#pragma once
// RPC Protocol: JSON-RPC 2.0 helpers and error codes
//
// Provides utilities for building JSON-RPC requests and responses
// compliant with the JSON-RPC protocol specification.

#include <nlohmann/json.hpp>
#include <string>

namespace chitta::rpc {

using json = nlohmann::json;

// Sanitize string to valid UTF-8, replacing invalid bytes with replacement char
inline std::string sanitize_utf8(const std::string& input) {
    std::string output;
    output.reserve(input.size());

    size_t i = 0;
    while (i < input.size()) {
        unsigned char c = static_cast<unsigned char>(input[i]);

        if (c < 0x80) {
            // ASCII: valid single byte
            output += input[i];
            ++i;
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte sequence
            if (i + 1 < input.size() && (input[i + 1] & 0xC0) == 0x80) {
                output += input[i];
                output += input[i + 1];
                i += 2;
            } else {
                output += '\xEF'; output += '\xBF'; output += '\xBD'; // replacement char
                ++i;
            }
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte sequence
            if (i + 2 < input.size() &&
                (input[i + 1] & 0xC0) == 0x80 &&
                (input[i + 2] & 0xC0) == 0x80) {
                output += input[i];
                output += input[i + 1];
                output += input[i + 2];
                i += 3;
            } else {
                output += '\xEF'; output += '\xBF'; output += '\xBD';
                ++i;
            }
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte sequence
            if (i + 3 < input.size() &&
                (input[i + 1] & 0xC0) == 0x80 &&
                (input[i + 2] & 0xC0) == 0x80 &&
                (input[i + 3] & 0xC0) == 0x80) {
                output += input[i];
                output += input[i + 1];
                output += input[i + 2];
                output += input[i + 3];
                i += 4;
            } else {
                output += '\xEF'; output += '\xBF'; output += '\xBD';
                ++i;
            }
        } else {
            // Invalid leading byte
            output += '\xEF'; output += '\xBF'; output += '\xBD';
            ++i;
        }
    }

    return output;
}

// JSON-RPC 2.0 error codes
namespace error {
    constexpr int PARSE_ERROR = -32700;
    constexpr int INVALID_REQUEST = -32600;
    constexpr int METHOD_NOT_FOUND = -32601;
    constexpr int INVALID_PARAMS = -32602;
    constexpr int INTERNAL_ERROR = -32603;
    // RPC-specific errors
    constexpr int TOOL_NOT_FOUND = -32001;
    constexpr int TOOL_EXECUTION_ERROR = -32002;
}

// Build a JSON-RPC 2.0 success response
inline json make_result(const json& id, const json& result) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
}

// Build a JSON-RPC 2.0 error response
inline json make_error(const json& id, int code, const std::string& message) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {
            {"code", code},
            {"message", message}
        }}
    };
}

// Build a tool call response (RPC content format)
inline json make_tool_response(const std::string& text, bool is_error = false,
                                const json& structured = json()) {
    json content = json::array();
    content.push_back({
        {"type", "text"},
        {"text", sanitize_utf8(text)}
    });

    json response = {
        {"content", content},
        {"isError", is_error}
    };

    if (!structured.is_null()) {
        response["structured"] = structured;
    }

    return response;
}

// Validate JSON-RPC 2.0 request
inline bool validate_request(const json& request, std::string& error_msg) {
    if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0") {
        error_msg = "Missing or invalid jsonrpc version";
        return false;
    }
    if (!request.contains("method") || !request["method"].is_string()) {
        error_msg = "Missing or invalid method";
        return false;
    }
    return true;
}

// Extract request components
struct RequestInfo {
    std::string method;
    json params;
    json id;
};

inline RequestInfo parse_request(const json& request) {
    return {
        request["method"].get<std::string>(),
        request.value("params", json::object()),
        request.value("id", json())
    };
}

} // namespace chitta::rpc
