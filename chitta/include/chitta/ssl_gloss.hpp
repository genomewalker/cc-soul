#pragma once
// ssl_gloss: NL gloss generation and query expansion for SSL v0.3 triplet format.
//
// Closes the representation gap between compact SSL triplets and natural-language
// BGE queries. Three entry points:
//   gloss_ssl_line()      — one SSL line → NL sentence
//   gloss_ssl_content()   — full SSL memory body → NL paragraph
//   ssl_query_variants()  — NL query → SSL-shaped expansion variants for RRF recall

#include "ssl_parser.hpp"
#include <algorithm>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chitta::ssl {

namespace detail {

// Predicate→NL verb phrase map (hyphenated SSL predicates → readable phrases)
inline const std::unordered_map<std::string, std::string>& predicate_nl_map() {
    static const std::unordered_map<std::string, std::string> m = {
        {"do-not-require",   "does not require"},
        {"does-not-require", "does not require"},
        {"not-require",      "does not require"},
        {"require",          "requires"},
        {"requires",         "requires"},
        {"uses",             "uses"},
        {"use",              "uses"},
        {"speeds",           "speeds up"},
        {"speeds-up",        "speeds up"},
        {"connect",          "connects to"},
        {"connects-to",      "connects to"},
        {"ssh-to",           "connects via SSH to"},
        {"access",           "accesses"},
        {"accesses",         "accesses"},
        {"enables",          "enables"},
        {"enable",           "enables"},
        {"disables",         "disables"},
        {"causes",           "causes"},
        {"fixes",            "fixes"},
        {"fix",              "fixes"},
        {"solves",           "solves"},
        {"improve",          "improves"},
        {"improves",         "improves"},
        {"replaces",         "replaces"},
        {"replace",          "replaces"},
        {"is-a",             "is a type of"},
        {"is",               "is"},
        {"are",              "are"},
        {"has",              "has"},
        {"have",             "have"},
        {"run",              "runs on"},
        {"runs-on",          "runs on"},
        {"runs",             "runs on"},
        {"defined-in",       "is defined in"},
        {"located-in",       "is located in"},
        {"needs",            "needs"},
        {"need",             "needs"},
        {"avoid",            "should avoid"},
        {"avoids",           "avoids"},
        {"supports",         "supports"},
        {"support",          "supports"},
        {"contains",         "contains"},
        {"prefer",           "is preferred over"},
        {"prefers",          "is preferred over"},
    };
    return m;
}

// Domain tag → human-readable suffix
inline const std::unordered_map<std::string, std::string>& domain_suffix_map() {
    static const std::unordered_map<std::string, std::string> m = {
        {"cluster",   "on the cluster"},
        {"build",     "in the build system"},
        {"solution",  ""},
        {"gotcha",    ""},
        {"decision",  ""},
        {"pattern",   ""},
        {"code",      "in the codebase"},
        {"ops",       "in operations"},
        {"ml",        "in the ML pipeline"},
    };
    return m;
}

// Domain keyword → [TAG] for query expansion
inline const std::vector<std::pair<std::vector<std::string>, std::string>>& domain_tag_rules() {
    static const std::vector<std::pair<std::vector<std::string>, std::string>> r = {
        {{"cluster", "node", "ssh", "kerberos", "slurm", "sbatch", "login", "hpc", "compute",
          "dandycmpn", "dandygpun", "chaos"}, "[cluster]"},
        {{"cmake", "build", "compile", "make", "ninja", "linker", "gcc", "clang", "cargo",
          "rustc", "link", "object"}, "[build]"},
        {{"fix", "fixed", "solution", "solve", "workaround", "error", "bug", "crash",
          "segfault", "undefined"}, "[SOLUTION]"},
        {{"prefer", "prefer", "better", "recommend", "use-this", "instead"}, "[PREFERENCE]"},
        {{"decided", "decision", "chose", "choice", "pick", "selected"}, "[DECISION]"},
        {{"gotcha", "warning", "careful", "trap", "footgun", "watch-out"}, "[GOTCHA]"},
        {{"pattern", "approach", "idiom", "convention", "practice"}, "[PATTERN]"},
    };
    return r;
}

// Hyphen-normalise a raw SSL token: "chaos-nodes" → {"chaos-nodes", "chaos nodes"}
inline std::vector<std::string> hyphen_variants(const std::string& token) {
    std::vector<std::string> out = {token};
    if (token.find('-') != std::string::npos) {
        std::string spaced = token;
        std::replace(spaced.begin(), spaced.end(), '-', ' ');
        out.push_back(spaced);
    } else {
        // also add hyphenated form for multi-word tokens
        bool has_space = token.find(' ') != std::string::npos;
        if (has_space) {
            std::string hyphenated = token;
            std::replace(hyphenated.begin(), hyphenated.end(), ' ', '-');
            out.push_back(hyphenated);
        }
    }
    return out;
}

inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// Translate a raw predicate token to NL verb phrase
inline std::string predicate_to_nl(const std::string& pred) {
    // Leading '!' encodes negation (SKILL.md: !validate = "does not validate").
    // Without this the bang falls through untranslated and the gloss embeds next to
    // the positive predicate, silently inverting every negated memory.
    if (!pred.empty() && pred.front() == '!')
        return "does not " + predicate_to_nl(pred.substr(1));
    auto lp = to_lower(pred);
    const auto& m = predicate_nl_map();
    auto it = m.find(lp);
    if (it != m.end()) return it->second;
    // generic: replace hyphens and arrows with spaces
    std::string out = pred;
    for (char& c : out) if (c == '-' || c == '_') c = ' ';
    // strip UTF-8 arrow sequences if present
    std::string clean;
    for (size_t i = 0; i < out.size(); ) {
        unsigned char c = out[i];
        if (c == 0xe2 && i+2 < out.size() && (unsigned char)out[i+1] == 0x86) {
            clean += ' '; i += 3;
        } else {
            clean += out[i++];
        }
    }
    return clean;
}

// Strip brackets from domain tag like "[cluster]" → "cluster"
inline std::string strip_brackets(const std::string& s) {
    if (s.size() >= 2 && s.front() == '[' && s.back() == ']')
        return s.substr(1, s.size()-2);
    return s;
}

// Build a gloss sentence from subject, predicate, object, domain
inline std::string build_sentence(const std::string& subj, const std::string& pred_raw,
                                  const std::string& obj, const std::string& domain) {
    auto subj_nl = subj; // keep as-is (entity names)
    auto pred_nl = predicate_to_nl(pred_raw);
    auto obj_nl  = obj;

    // replace hyphens in subject/object for readability
    auto humanise = [](std::string s) {
        for (char& c : s) if (c == '-') c = ' ';
        return s;
    };

    std::string sentence = humanise(subj_nl) + " " + pred_nl;
    if (!obj_nl.empty() && obj_nl != pred_raw) sentence += " " + humanise(obj_nl);

    if (!domain.empty()) {
        auto dl = to_lower(strip_brackets(domain));
        const auto& dsm = domain_suffix_map();
        auto it = dsm.find(dl);
        if (it != dsm.end() && !it->second.empty())
            sentence += " " + it->second;
    }
    return sentence;
}

// Parse a single SSL line with arrow separator into (domain, subj, pred, obj)
// Returns false if line doesn't look like an SSL triplet
inline bool parse_arrow_triplet(const std::string& line,
                                 std::string& domain_out,
                                 std::string& subj_out,
                                 std::string& pred_out,
                                 std::string& obj_out)
{
    // UTF-8 arrow: \xe2\x86\x92
    static const std::string arrow = "\xe2\x86\x92";
    size_t a1 = line.find(arrow);
    if (a1 == std::string::npos) return false;
    size_t a2 = line.find(arrow, a1 + arrow.size());

    // Extract optional [domain] prefix and type tag
    // Pattern: [TYPE] [domain] subject→pred→obj  OR  [TYPE] subject→pred
    std::string rest = line;
    domain_out = "";

    static const std::regex tag_re(R"(\[([A-Za-z][A-Za-z0-9_\-]+)\]\s*)");
    std::smatch m;
    while (std::regex_search(rest, m, tag_re)) {
        std::string tag = m[1].str();
        std::string ltag = to_lower(tag);
        // domain tags are lowercase-ish; type tags are ALL_CAPS
        if (ltag == tag || ltag.size() == tag.size()) {
            if (domain_out.empty()) domain_out = "[" + tag + "]";
        }
        rest = m.suffix().str();
    }
    // re-find arrows in cleaned rest
    a1 = rest.find(arrow);
    if (a1 == std::string::npos) return false;
    a2 = rest.find(arrow, a1 + arrow.size());

    subj_out = rest.substr(0, a1);
    // trim
    while (!subj_out.empty() && (subj_out.back() == ' ' || subj_out.back() == '\t'))
        subj_out.pop_back();

    if (a2 == std::string::npos) {
        pred_out = rest.substr(a1 + arrow.size());
        obj_out  = "";
    } else {
        pred_out = rest.substr(a1 + arrow.size(), a2 - (a1 + arrow.size()));
        obj_out  = rest.substr(a2 + arrow.size());
    }
    // trim
    auto trim = [](std::string& s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
        while (!s.empty() && (s.back()  == ' ' || s.back()  == '\t')) s.pop_back();
    };
    trim(subj_out); trim(pred_out); trim(obj_out);

    // strip F:FLAG suffix from object
    static const std::regex flag_re(R"(\s+F:[A-Z]+\s*$)");
    obj_out = std::regex_replace(obj_out, flag_re, "");
    trim(obj_out);

    return !subj_out.empty();
}

} // namespace detail

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Gloss a single SSL line into a human-readable NL sentence.
// Returns empty string if line doesn't look like SSL.
inline std::string gloss_ssl_line(const std::string& line) {
    std::string domain, subj, pred, obj;
    if (!detail::parse_arrow_triplet(line, domain, subj, pred, obj))
        return "";
    return detail::build_sentence(subj, pred, obj.empty() ? pred : obj, domain);
}

// Gloss a full SSL memory body (may contain multiple lines).
// Returns concatenated NL sentences separated by spaces.
inline std::string gloss_ssl_content(const std::string& content) {
    std::istringstream ss(content);
    std::string line, result;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        auto g = gloss_ssl_line(line);
        if (!g.empty()) {
            if (!result.empty()) result += ' ';
            result += g;
        }
    }
    return result;
}

// Single source of truth for the text an SSL memory is embedded as:
// canonical content with its NL gloss appended (when one exists). Every embed
// path — live backfill, re_embed, ingester, distiller, recall-time query embed —
// must build retrieval text this way so document and query vectors share a space.
// The embedder-specific "search_document: " / "search_query: " prefix is applied
// separately by each embedder (llama adds it internally; ONNX prepends manually).
inline std::string retrieval_text(const std::string& content) {
    auto gloss = gloss_ssl_content(content);
    return gloss.empty() ? content : content + "\n" + gloss;
}

// Expand a natural-language query into SSL-shaped variants for RRF recall.
// Returns up to 6 unique, non-trivial variants.
inline std::vector<std::string> ssl_query_variants(const std::string& query) {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    seen.insert(query);

    auto add = [&](const std::string& v) {
        if (v.empty() || v.size() < 3) return;
        if (seen.count(v)) return;
        seen.insert(v);
        out.push_back(v);
    };

    std::string ql = detail::to_lower(query);
    std::string intent_noun; // noun phrase extracted from intent pattern

    // --- Intent synonym expansion ---
    // "what is X" / "define X" → add hyphen variants of noun phrase
    if (ql.find("what is") != std::string::npos ||
        ql.find("what are") != std::string::npos ||
        ql.find("define ")  != std::string::npos ||
        ql.find("meaning of") != std::string::npos) {
        static const std::regex intent_re(
            R"((?:what is|what are|define|definition of|meaning of)\s+(?:a\s+)?(.+))",
            std::regex::icase);
        std::smatch m;
        if (std::regex_search(query, m, intent_re)) {
            intent_noun = m[1].str();
            while (!intent_noun.empty() && (intent_noun.back() == '?' || intent_noun.back() == ' '))
                intent_noun.pop_back();
            for (auto& v : detail::hyphen_variants(intent_noun)) add(v);
            // also add plural (SSL tokens often end in 's': chaos-nodes, login-nodes)
            for (auto& v : detail::hyphen_variants(intent_noun)) {
                if (!v.empty() && v.back() != 's') add(v + "s");
            }
        }
    }

    // "how do I SSH" / "how to connect" → add "login connect access"
    if (ql.find("how do") != std::string::npos ||
        ql.find("how to") != std::string::npos ||
        ql.find("ssh ") != std::string::npos ||
        ql.find("connect") != std::string::npos) {
        add("login");
        add("SSH access");
        add("connect");
    }

    // --- Domain tag injection ---
    std::string inferred_tag;
    for (auto& [keywords, tag] : detail::domain_tag_rules()) {
        for (auto& kw : keywords) {
            if (ql.find(kw) != std::string::npos) {
                inferred_tag = tag;
                break;
            }
        }
        if (!inferred_tag.empty()) break;
    }

    // --- Entity extraction: capitalised words, hyphenated tokens ---
    static const std::regex entity_re(R"([A-Z][a-zA-Z0-9\-_]+|[a-z][a-zA-Z0-9]*-[a-zA-Z0-9\-]+)");
    std::vector<std::string> entities;
    {
        auto it  = std::sregex_iterator(query.begin(), query.end(), entity_re);
        auto end = std::sregex_iterator();
        for (; it != end; ++it) {
            std::string e = (*it).str();
            if (e.size() < 3) continue;
            entities.push_back(e);
            for (auto& v : detail::hyphen_variants(e)) add(v);
        }
    }

    // --- [domain] prefix + first entity (or intent noun fallback) ---
    if (!inferred_tag.empty()) {
        // prefer entity from regex; fall back to intent_noun if no entities found
        std::string first_entity = !entities.empty() ? entities[0]
                                 : !intent_noun.empty() ? intent_noun : "";
        if (!first_entity.empty()) {
            for (auto& v : detail::hyphen_variants(first_entity)) {
                add(inferred_tag + " " + v);
                // also plural under tag
                if (!v.empty() && v.back() != 's') add(inferred_tag + " " + v + "s");
            }
        }
    }

    // --- SSL-tokenized form: entity→verb → e.g. "chaos-nodes→SSH" ---
    if (entities.size() >= 2) {
        static const std::string arrow = "\xe2\x86\x92";
        add(entities[0] + arrow + entities[1]);
    }

    // Cap at 6 variants
    if (out.size() > 6) out.resize(6);
    return out;
}

} // namespace chitta::ssl
