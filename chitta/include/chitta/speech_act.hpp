#pragma once
#include <optional>
#include <string>
#include <regex>

namespace chitta {

// Heuristic speech-act classifier. Returns a refined memory kind for episode
// memories so chitta can apply per-kind scoring boosts without LLM overhead.
// Returns nullopt if no pattern matches (caller keeps "episode").
inline std::optional<std::string> classify_speech_act(const std::string& content) {
    struct Rule { std::regex re; std::string kind; };
    static const std::vector<Rule> rules = {
        { std::regex(R"(\bwe\s+decided\b|\bdecision\s*:|\bchose\b.{0,40}\bover\b|\bgoing\s+with\b)",
                     std::regex::icase), "decision" },
        { std::regex(R"(\bactually\b.{0,40}\bshould\b|\bcorrect(?:ion)?\s*:|\bwas\s+wrong\b|\bmistake\b)",
                     std::regex::icase), "correction" },
        { std::regex(R"(\bI\s+prefer\b|\bI(?:'d| would)\s+rather\b|\bprefer(?:ence)?\s*:|\bfavou?r(?:ite)?\b)",
                     std::regex::icase), "preference" },
        { std::regex(R"(\bTODO\b|\bnext\s+step\b|\bwe\s+need\s+to\b|\baction\s+item\b|\btask\s*:)",
                     std::regex::icase), "task" },
        { std::regex(R"(\bworks?\s+now\b|\bfixed\b|\b✓\b|\bsolved\b|\bcompleted\b|\bsuccessfully\b)",
                     std::regex::icase), "result" },
        { std::regex(R"(\bfailed?\b|\berror\s*:|\bcrash(?:ed)?\b|\bdoes\s+not\s+work\b|\bbroken\b)",
                     std::regex::icase), "failure" },
        { std::regex(R"(\bwhy\b.{0,60}\?|\bhow\s+does\b|\bwhat\s+is\b|\bwhere\s+is\b|\bwhen\s+did\b)",
                     std::regex::icase), "question" },
        { std::regex(R"(\bhypothes[ie]s\b|\bassume\b|\bperhaps\b|\bmaybe\b.{0,40}\bif\b|\bconjecture\b)",
                     std::regex::icase), "hypothesis" },
    };

    for (const auto& rule : rules) {
        if (std::regex_search(content, rule.re)) return rule.kind;
    }
    return std::nullopt;
}

} // namespace chitta
