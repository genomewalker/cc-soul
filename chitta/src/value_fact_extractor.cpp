#include "../include/chitta/value_fact_extractor.hpp"
#include <regex>
#include <sstream>
#include <unordered_set>
#include <algorithm>
#include <cctype>

namespace chitta {

namespace {

// A precise VALUE token: something a coverage-miss question asks for by name.
//   0xNN byte | sha/hex (>=7) | size+unit (4.2 GB, 180 ms) | version (2.1.0)
//   line N / N-M range | percentage | threshold (>=100) | decimal | multi-digit int
// Ordered longest-first so e.g. "4.2 GB" wins over the bare "4".
const std::regex kValue(
    R"(0x[0-9a-fA-F]+)"
    R"(|\b[0-9a-f]{7,40}\b)"
    R"(|\b\d+(?:\.\d+)?\s?(?:GiB|MiB|KiB|TiB|GB|MB|KB|TB|kB|ms|us|ns|B)\b)"
    R"(|\bv?\d+\.\d+\.\d+\b)"
    R"(|\bline\s+\d+(?:-\d+)?\b)"
    R"(|:\d+(?:-\d+)?\b)"
    R"(|\b\d+-\d+\b)"
    R"(|[<>]=?\s?\d+(?:\.\d+)?)"
    R"(|\b\d+(?:\.\d+)?%)"
    R"(|\b\d+\.\d+\b)"
    R"(|\b\d{2,}\b)",
    std::regex::optimize);

// A distinctive IDENTIFIER anchor: file path, scoped/call symbol, ALL_CAPS config
// key, or snake_case token (>=1 underscore). Generic prose words never match.
// The file-path alternative deliberately stops before any ":line" suffix so that
// "stats.py:150" yields identifier "stats.py" + adjacent value ":150".
const std::regex kIdentifier(
    R"([\w.\-/]*[\w\-]\.(?:cpp|hpp|hh|cc|cxx|h|rs|py|sh|json|toml|yaml|yml|md|txt|sql|js|ts|tsx|go|c|lock|cfg|ini|proto))"
    R"(|\b[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)+\b)"
    R"(|\b[A-Za-z_][A-Za-z0-9_]*\(\))"
    R"(|\b[A-Z][A-Z0-9]{2,}(?:_[A-Z0-9]+)*\b)"
    R"(|\b[a-z][a-z0-9]*(?:_[a-z0-9]+)+\b)"
    R"(|\b[a-z][a-z0-9]*(?:-[a-z0-9]+)+\b)",
    std::regex::optimize);

// Keyword anchors: a value with no distinctive identifier nearby is still precise
// if it follows one of these ("commit e241114", "port 7440", "v2.1.0").
const std::unordered_set<std::string> kKeywordAnchors = {
    "commit", "table", "port", "key", "sha", "md5", "hash", "version",
    "offset", "threshold", "size", "id", "byte", "line", "row", "col",
    "index", "revision", "rev", "tag", "release", "node", "pid", "epoch"};

struct Span { size_t pos; size_t end; std::string text; };

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::vector<Span> matches(const std::string& line, const std::regex& re) {
    std::vector<Span> out;
    for (auto it = std::sregex_iterator(line.begin(), line.end(), re);
         it != std::sregex_iterator(); ++it) {
        out.push_back({static_cast<size_t>(it->position()),
                       static_cast<size_t>(it->position() + it->length()),
                       it->str()});
    }
    return out;
}

// The word immediately to the left of `pos` (skipping spaces/punct) — for keyword anchors.
std::string preceding_word(const std::string& line, size_t pos) {
    if (pos == 0) return "";
    size_t e = pos;
    while (e > 0 && !std::isalnum(static_cast<unsigned char>(line[e - 1]))) --e;
    size_t s = e;
    while (s > 0 && (std::isalnum(static_cast<unsigned char>(line[s - 1])) ||
                     line[s - 1] == '_')) --s;
    return line.substr(s, e - s);
}

} // namespace

std::vector<ValueFact> extract_value_facts(const std::string& conversation) {
    constexpr size_t kWindow = 64;  // max chars between anchor and value
    std::vector<ValueFact> facts;
    std::unordered_set<std::string> seen;  // (identifier|value) intra-extraction dedup

    std::string line;
    std::istringstream stream(conversation);
    while (std::getline(stream, line)) {
        if (line.size() < 4 || line.size() > 4000) continue;
        auto values = matches(line, kValue);
        if (values.empty()) continue;
        auto idents = matches(line, kIdentifier);

        for (const auto& v : values) {
            // Nearest distinctive identifier whose span does not overlap the value
            // and sits within the adjacency window (prefer the left neighbour).
            const Span* best = nullptr;
            size_t best_dist = kWindow + 1;
            for (const auto& id : idents) {
                if (id.pos < v.end && id.end > v.pos) continue;  // overlap
                size_t dist = (id.end <= v.pos) ? (v.pos - id.end)
                                                : (id.pos - v.end);
                if (dist <= kWindow && dist < best_dist) {
                    best = &id;
                    best_dist = dist;
                }
            }

            // A keyword immediately to the left ("commit e241114") is a tighter,
            // more precise anchor than a farther distinctive token — prefer nearest,
            // keyword winning ties.
            std::string ident;
            std::string kw = lower(preceding_word(line, v.pos));
            if (kKeywordAnchors.count(kw)) {
                size_t kw_end = v.pos;
                while (kw_end > 0 && !std::isalnum(static_cast<unsigned char>(line[kw_end - 1]))) --kw_end;
                size_t kw_dist = v.pos - kw_end;
                if (!best || kw_dist <= best_dist) ident = kw;
            }
            if (ident.empty() && best) ident = best->text;
            if (ident.empty()) continue;  // anchorless value → skip

            std::string val = trim(v.text);
            std::string key = ident + "|" + val;
            if (!seen.insert(key).second) continue;

            std::string ctx = trim(line);
            if (ctx.size() > 200) ctx = ctx.substr(0, 197) + "...";
            facts.push_back({ident, val, ident + " " + val + " | " + ctx});
        }
    }
    return facts;
}

} // namespace chitta
