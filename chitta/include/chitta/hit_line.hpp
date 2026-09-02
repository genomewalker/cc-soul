// The one definition of the recall hit line. Lives in its own header so the
// daemon and hit_line_test compile the SAME function — the prefilter block
// needs a sync script precisely because it is duplicated; this does not.
//
// Frozen contract, parsed by hooks/prompt-core.sh (SUS exposure block) and by
// the outcome-ledger injected tap:
//
//   #<id> [<pct>%][ [<type>]][ (on: YYYY-MM-DD)] <text>
//
// Both parse `^#([0-9]+)` and the bracketed percent. Dropping the id prefix
// silently kills exposure/credit tracking — it happened once. Lanes vary only
// in whether the type and date fields appear.
#pragma once

#include <algorithm>
#include <ctime>
#include <string>

namespace chitta {

// Text is truncated to this many characters on every lane.
inline constexpr size_t kHitTextChars = 400;

inline std::string hit_line(const std::string& id, int pct, const std::string& type, int64_t ts_ms,
                            const std::string& text, bool show_type, bool show_date) {
    std::string out;
    out.reserve(64 + std::min(text.size(), kHitTextChars));
    out += '#';
    out += id;
    out += " [";
    out += std::to_string(pct);
    out += "%]";
    if (show_type) {
        out += " [";
        out += type;
        out += ']';
    }
    if (show_date && ts_ms > 0) {
        std::time_t t = static_cast<std::time_t>(ts_ms / 1000);
        std::tm tm{};
        char buf[16];
        if (gmtime_r(&t, &tm) && std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm)) {
            out += " (on: ";
            out += buf;
            out += ')';
        }
    }
    out += ' ';
    out += text.substr(0, kHitTextChars);
    out += '\n';
    return out;
}

} // namespace chitta
