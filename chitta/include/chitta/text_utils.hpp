#pragma once
// Shared text utilities used across RPC handlers.

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace chitta {

// Whitespace-delimited word count × 1.3 — rough token estimator.
// Used by compact, trajectory_compact, and other budget-driven handlers.
inline std::size_t estimate_tokens(std::string_view text) {
    if (text.empty()) return 0;
    std::size_t words = 0;
    bool in_word = false;
    for (unsigned char ch : text) {
        if (std::isspace(ch)) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            ++words;
        }
    }
    return static_cast<std::size_t>(words * 1.3f);
}

// Truncate a UTF-8 string at a valid character boundary (never mid multibyte
// sequence). Content previews built with raw substr() can split a multibyte
// char; nlohmann::json::dump() then throws on the invalid UTF-8 and the whole
// RPC reply is lost. Use this for ANY truncated text that can reach JSON.
inline std::string utf8_trunc(const std::string& s, std::size_t max_bytes) {
    if (s.size() <= max_bytes) return s;
    std::size_t i = max_bytes;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    return s.substr(0, i);
}

} // namespace chitta
