#pragma once
// Shared text utilities used across RPC handlers.

#include <cctype>
#include <cstddef>
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

} // namespace chitta
