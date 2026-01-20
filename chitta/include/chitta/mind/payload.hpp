#pragma once
// Payload: Text <-> binary payload conversion
//
// Simple helpers for storing text in node payloads.

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace chitta {

// Convert text to binary payload
inline std::vector<uint8_t> text_to_payload(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

// Convert binary payload to text
inline std::optional<std::string> payload_to_text(const std::vector<uint8_t>& payload) {
    if (payload.empty()) return std::nullopt;
    return std::string(payload.begin(), payload.end());
}

}  // namespace chitta
