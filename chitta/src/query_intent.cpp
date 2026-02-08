#include "chitta/query_intent.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <ctime>

namespace chitta {

QueryIntentClassifier::QueryIntentClassifier() {
    // Initialize aspect keyword mappings
    // Each keyword maps to an aspect name for filtering
    aspect_keywords_ = {
        // Preferences
        {"preferences", "preference"},
        {"prefer", "preference"},
        {"i prefer", "preference"},
        {"i like", "preference"},
        {"likes", "preference"},

        // Corrections
        {"corrections", "correction"},
        {"correction", "correction"},
        {"mistakes", "correction"},
        {"mistake", "correction"},
        {"wrong", "correction"},
        {"errors", "correction"},

        // Insights
        {"insights", "insight"},
        {"insight", "insight"},
        {"learned", "insight"},
        {"patterns", "insight"},
        {"pattern", "insight"},

        // Failures
        {"failures", "failure"},
        {"failure", "failure"},
        {"didn't work", "failure"},
        {"didnt work", "failure"},
        {"failed", "failure"},

        // Decisions
        {"decisions", "decision"},
        {"decision", "decision"},
        {"decided", "decision"},
        {"chose", "decision"},
        {"choice", "decision"},

        // Approaches
        {"approaches", "approach"},
        {"approach", "approach"},
        {"strategies", "approach"},
        {"strategy", "approach"},
        {"methods", "approach"},
        {"method", "approach"},

        // Milestones
        {"milestones", "milestone"},
        {"milestone", "milestone"},
        {"achievements", "milestone"},
        {"achievement", "milestone"},
        {"completed", "milestone"},

        // Goals
        {"goals", "goal"},
        {"goal", "goal"},
        {"objectives", "goal"},
        {"objective", "goal"},
        {"targets", "goal"},
        {"target", "goal"},

        // Habits
        {"habits", "habit"},
        {"habit", "habit"},
        {"routines", "habit"},
        {"routine", "habit"},
        {"always do", "habit"},

        // Beliefs
        {"beliefs", "belief"},
        {"belief", "belief"},
        {"values", "belief"},
        {"value", "belief"},
        {"principles", "belief"},
        {"principle", "belief"},

        // Wisdom
        {"wisdom", "wisdom"},
        {"knowledge", "wisdom"},
        {"know", "wisdom"},

        // Code (as aspect)
        {"code", "code"},
        {"function", "code"},
        {"functions", "code"},
        {"class", "code"},
        {"classes", "code"},
        {"symbol", "code"},
        {"symbols", "code"},

        // Gaps
        {"gaps", "gap"},
        {"gap", "gap"},
        {"questions", "gap"},
        {"question", "gap"},
        {"unknown", "gap"},
        {"unknowns", "gap"},
    };

    // Meta query keywords
    meta_keywords_ = {
        "how many",
        "count",
        "stats",
        "statistics",
        "health",
        "memory health",
        "status",
        "memory status",
        "total memories",
        "memory count",
    };

    // Code query keywords
    code_keywords_ = {
        "find function",
        "find class",
        "find symbol",
        "where is",
        "definition of",
        "defined",
        "callers of",
        "callees of",
        "who calls",
        "what calls",
        "implementations of",
        "usages of",
        "references to",
    };

    // Exploratory query keywords
    exploratory_keywords_ = {
        "browse",
        "explore",
        "show me",
        "list",
        "recent",
        "latest",
        "all",
        "everything",
    };

    // Relationship patterns
    relationship_patterns_ = {
        std::regex(R"(how\s+does?\s+(.+?)\s+relate\s+to\s+(.+))", std::regex::icase),
        std::regex(R"(what\s+connects?\s+(.+?)\s+(?:and|to|with)\s+(.+))", std::regex::icase),
        std::regex(R"(relationship\s+between\s+(.+?)\s+(?:and|to|with)\s+(.+))", std::regex::icase),
        std::regex(R"(relation\s+between\s+(.+?)\s+(?:and|to|with)\s+(.+))", std::regex::icase),
        std::regex(R"((.+?)\s+to\s+(.+?)\s+connection)", std::regex::icase),
        std::regex(R"(link\s+between\s+(.+?)\s+(?:and|to|with)\s+(.+))", std::regex::icase),
    };

    // Temporal patterns
    temporal_patterns_ = {
        std::regex(R"(last\s+(\d+)\s+(day|days|week|weeks|month|months|year|years))", std::regex::icase),
        std::regex(R"((\d+)\s+(day|days|week|weeks|month|months|year|years)\s+ago)", std::regex::icase),
        std::regex(R"(yesterday)", std::regex::icase),
        std::regex(R"(today)", std::regex::icase),
        std::regex(R"(this\s+(morning|afternoon|evening|week|month|year))", std::regex::icase),
        std::regex(R"(last\s+(week|month|year))", std::regex::icase),
        std::regex(R"((january|february|march|april|may|june|july|august|september|october|november|december))", std::regex::icase),
        std::regex(R"((\d{4}))", std::regex::icase),  // Year like 2024
        std::regex(R"(since\s+(.+))", std::regex::icase),
        std::regex(R"(before\s+(.+))", std::regex::icase),
        std::regex(R"(after\s+(.+))", std::regex::icase),
        std::regex(R"(from\s+(.+?)\s+to\s+(.+))", std::regex::icase),
    };
}

std::string QueryIntentClassifier::normalize_query(const std::string& query) {
    std::string normalized;
    normalized.reserve(query.size());

    for (char c : query) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ') {
            normalized += std::tolower(static_cast<unsigned char>(c));
        } else {
            normalized += ' ';
        }
    }

    // Collapse multiple spaces
    std::string result;
    bool prev_space = true;
    for (char c : normalized) {
        if (c == ' ') {
            if (!prev_space) {
                result += c;
                prev_space = true;
            }
        } else {
            result += c;
            prev_space = false;
        }
    }

    // Trim trailing space
    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }

    return result;
}

bool QueryIntentClassifier::is_aspect_query(const std::string& query, std::string& aspect) {
    std::string normalized = normalize_query(query);

    // Check for aspect keywords
    for (const auto& [keyword, aspect_name] : aspect_keywords_) {
        if (normalized.find(keyword) != std::string::npos) {
            // Look for "show me all X" or "what X exist" patterns
            bool has_list_intent =
                normalized.find("show") != std::string::npos ||
                normalized.find("list") != std::string::npos ||
                normalized.find("all") != std::string::npos ||
                normalized.find("what") != std::string::npos ||
                normalized.find("any") != std::string::npos;

            if (has_list_intent) {
                aspect = aspect_name;
                return true;
            }
        }
    }

    return false;
}

bool QueryIntentClassifier::is_temporal_query(const std::string& query, TimeRange& range) {
    for (const auto& pattern : temporal_patterns_) {
        std::smatch match;
        if (std::regex_search(query, match, pattern)) {
            range = parse_time_expression(match[0].str());
            if (range.valid()) {
                return true;
            }
        }
    }
    return false;
}

bool QueryIntentClassifier::is_relationship_query(const std::string& query, std::string& subject, std::string& object) {
    for (const auto& pattern : relationship_patterns_) {
        std::smatch match;
        if (std::regex_search(query, match, pattern)) {
            if (match.size() >= 3) {
                subject = match[1].str();
                object = match[2].str();

                // Trim whitespace
                auto trim = [](std::string& s) {
                    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(0, 1);
                    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
                };
                trim(subject);
                trim(object);

                return !subject.empty() && !object.empty();
            }
        }
    }
    return false;
}

bool QueryIntentClassifier::is_code_query(const std::string& query) {
    std::string normalized = normalize_query(query);

    for (const auto& keyword : code_keywords_) {
        if (normalized.find(keyword) != std::string::npos) {
            return true;
        }
    }

    return false;
}

bool QueryIntentClassifier::is_meta_query(const std::string& query) {
    std::string normalized = normalize_query(query);

    for (const auto& keyword : meta_keywords_) {
        if (normalized.find(keyword) != std::string::npos) {
            return true;
        }
    }

    return false;
}

bool QueryIntentClassifier::is_exploratory_query(const std::string& query) {
    std::string normalized = normalize_query(query);

    int exploratory_signals = 0;
    for (const auto& keyword : exploratory_keywords_) {
        if (normalized.find(keyword) != std::string::npos) {
            exploratory_signals++;
        }
    }

    // Need at least 2 signals or "browse"/"explore" alone
    if (normalized.find("browse") != std::string::npos ||
        normalized.find("explore") != std::string::npos) {
        return true;
    }

    return exploratory_signals >= 2;
}

TimeRange QueryIntentClassifier::parse_time_expression(const std::string& expr) {
    TimeRange range;
    auto now = std::chrono::system_clock::now();

    std::string lower_expr;
    lower_expr.reserve(expr.size());
    for (char c : expr) {
        lower_expr += std::tolower(static_cast<unsigned char>(c));
    }

    // Parse "last N days/weeks/months/years"
    std::regex last_n_pattern(R"(last\s+(\d+)\s+(day|days|week|weeks|month|months|year|years))");
    std::smatch match;
    if (std::regex_search(lower_expr, match, last_n_pattern)) {
        int count = std::stoi(match[1].str());
        std::string unit = match[2].str();

        std::chrono::hours offset{0};
        if (unit.find("day") != std::string::npos) {
            offset = std::chrono::hours(24 * count);
        } else if (unit.find("week") != std::string::npos) {
            offset = std::chrono::hours(24 * 7 * count);
        } else if (unit.find("month") != std::string::npos) {
            offset = std::chrono::hours(24 * 30 * count);
        } else if (unit.find("year") != std::string::npos) {
            offset = std::chrono::hours(24 * 365 * count);
        }

        range.start = now - offset;
        range.end = now;
        return range;
    }

    // Parse "N days/weeks/months/years ago"
    std::regex ago_pattern(R"((\d+)\s+(day|days|week|weeks|month|months|year|years)\s+ago)");
    if (std::regex_search(lower_expr, match, ago_pattern)) {
        int count = std::stoi(match[1].str());
        std::string unit = match[2].str();

        std::chrono::hours offset{0};
        if (unit.find("day") != std::string::npos) {
            offset = std::chrono::hours(24 * count);
        } else if (unit.find("week") != std::string::npos) {
            offset = std::chrono::hours(24 * 7 * count);
        } else if (unit.find("month") != std::string::npos) {
            offset = std::chrono::hours(24 * 30 * count);
        } else if (unit.find("year") != std::string::npos) {
            offset = std::chrono::hours(24 * 365 * count);
        }

        range.start = now - offset;
        range.end = now;
        return range;
    }

    // Parse "yesterday"
    if (lower_expr.find("yesterday") != std::string::npos) {
        range.start = now - std::chrono::hours(24);
        range.end = now;
        return range;
    }

    // Parse "today"
    if (lower_expr.find("today") != std::string::npos) {
        // Start of today (midnight)
        auto now_t = std::chrono::system_clock::to_time_t(now);
        std::tm* tm_now = std::localtime(&now_t);
        tm_now->tm_hour = 0;
        tm_now->tm_min = 0;
        tm_now->tm_sec = 0;
        range.start = std::chrono::system_clock::from_time_t(std::mktime(tm_now));
        range.end = now;
        return range;
    }

    // Parse "this morning/afternoon/evening"
    std::regex this_pattern(R"(this\s+(morning|afternoon|evening|week|month|year))");
    if (std::regex_search(lower_expr, match, this_pattern)) {
        std::string period = match[1].str();

        auto now_t = std::chrono::system_clock::to_time_t(now);
        std::tm* tm_now = std::localtime(&now_t);

        if (period == "morning") {
            tm_now->tm_hour = 0;
            tm_now->tm_min = 0;
            tm_now->tm_sec = 0;
            range.start = std::chrono::system_clock::from_time_t(std::mktime(tm_now));
            range.end = now;
        } else if (period == "afternoon") {
            tm_now->tm_hour = 12;
            tm_now->tm_min = 0;
            tm_now->tm_sec = 0;
            range.start = std::chrono::system_clock::from_time_t(std::mktime(tm_now));
            range.end = now;
        } else if (period == "evening") {
            tm_now->tm_hour = 17;
            tm_now->tm_min = 0;
            tm_now->tm_sec = 0;
            range.start = std::chrono::system_clock::from_time_t(std::mktime(tm_now));
            range.end = now;
        } else if (period == "week") {
            // Start of this week (assuming Sunday)
            int days_since_sunday = tm_now->tm_wday;
            tm_now->tm_hour = 0;
            tm_now->tm_min = 0;
            tm_now->tm_sec = 0;
            tm_now->tm_mday -= days_since_sunday;
            range.start = std::chrono::system_clock::from_time_t(std::mktime(tm_now));
            range.end = now;
        } else if (period == "month") {
            tm_now->tm_mday = 1;
            tm_now->tm_hour = 0;
            tm_now->tm_min = 0;
            tm_now->tm_sec = 0;
            range.start = std::chrono::system_clock::from_time_t(std::mktime(tm_now));
            range.end = now;
        } else if (period == "year") {
            tm_now->tm_mon = 0;
            tm_now->tm_mday = 1;
            tm_now->tm_hour = 0;
            tm_now->tm_min = 0;
            tm_now->tm_sec = 0;
            range.start = std::chrono::system_clock::from_time_t(std::mktime(tm_now));
            range.end = now;
        }
        return range;
    }

    // Parse "last week/month/year"
    std::regex last_pattern(R"(last\s+(week|month|year))");
    if (std::regex_search(lower_expr, match, last_pattern)) {
        std::string period = match[1].str();

        if (period == "week") {
            range.start = now - std::chrono::hours(24 * 7);
            range.end = now;
        } else if (period == "month") {
            range.start = now - std::chrono::hours(24 * 30);
            range.end = now;
        } else if (period == "year") {
            range.start = now - std::chrono::hours(24 * 365);
            range.end = now;
        }
        return range;
    }

    // Parse month names
    static const std::unordered_map<std::string, int> month_map = {
        {"january", 0}, {"february", 1}, {"march", 2}, {"april", 3},
        {"may", 4}, {"june", 5}, {"july", 6}, {"august", 7},
        {"september", 8}, {"october", 9}, {"november", 10}, {"december", 11}
    };

    for (const auto& [month_name, month_num] : month_map) {
        if (lower_expr.find(month_name) != std::string::npos) {
            auto now_t = std::chrono::system_clock::to_time_t(now);
            std::tm* tm_now = std::localtime(&now_t);

            // Start of month
            std::tm tm_start = {};
            tm_start.tm_year = tm_now->tm_year;
            tm_start.tm_mon = month_num;
            tm_start.tm_mday = 1;
            tm_start.tm_hour = 0;
            tm_start.tm_min = 0;
            tm_start.tm_sec = 0;
            tm_start.tm_isdst = -1;

            // End of month
            std::tm tm_end = tm_start;
            tm_end.tm_mon = month_num + 1;
            tm_end.tm_mday = 1;
            tm_end.tm_isdst = -1;

            range.start = std::chrono::system_clock::from_time_t(std::mktime(&tm_start));
            range.end = std::chrono::system_clock::from_time_t(std::mktime(&tm_end));
            return range;
        }
    }

    // Parse year (4 digits)
    std::regex year_pattern(R"(\b(20\d{2})\b)");
    if (std::regex_search(lower_expr, match, year_pattern)) {
        int year = std::stoi(match[1].str());

        std::tm tm_start = {};
        tm_start.tm_year = year - 1900;
        tm_start.tm_mon = 0;
        tm_start.tm_mday = 1;
        tm_start.tm_hour = 0;
        tm_start.tm_min = 0;
        tm_start.tm_sec = 0;
        tm_start.tm_isdst = -1;

        std::tm tm_end = tm_start;
        tm_end.tm_year = year - 1900 + 1;
        tm_end.tm_isdst = -1;

        range.start = std::chrono::system_clock::from_time_t(std::mktime(&tm_start));
        range.end = std::chrono::system_clock::from_time_t(std::mktime(&tm_end));
        return range;
    }

    return range;
}

std::string QueryIntentClassifier::extract_entity(const std::string& query) {
    std::string normalized = normalize_query(query);

    // Remove common query prefixes
    static const std::vector<std::string> prefixes = {
        "what do i know about ",
        "what do you know about ",
        "tell me about ",
        "recall ",
        "remember ",
        "find ",
        "search for ",
        "look for ",
        "what is ",
        "what are ",
        "show me ",
    };

    std::string entity = normalized;
    for (const auto& prefix : prefixes) {
        if (entity.find(prefix) == 0) {
            entity = entity.substr(prefix.size());
            break;
        }
    }

    // Trim
    while (!entity.empty() && std::isspace(static_cast<unsigned char>(entity.front()))) {
        entity.erase(0, 1);
    }
    while (!entity.empty() && std::isspace(static_cast<unsigned char>(entity.back()))) {
        entity.pop_back();
    }

    return entity;
}

QueryIntent QueryIntentClassifier::classify(const std::string& query) {
    QueryIntent intent;
    intent.original_query = query;

    // Check for meta queries first (highest priority)
    if (is_meta_query(query)) {
        intent.type = QueryIntentType::Meta;
        intent.confidence = 0.9f;
        return intent;
    }

    // Check for code queries
    if (is_code_query(query)) {
        intent.type = QueryIntentType::Code;
        intent.entity = extract_entity(query);
        intent.confidence = 0.85f;
        return intent;
    }

    // Check for relationship queries
    std::string subject, object;
    if (is_relationship_query(query, subject, object)) {
        intent.type = QueryIntentType::Relationship;
        intent.subject = subject;
        intent.object = object;
        intent.confidence = 0.85f;
        return intent;
    }

    // Check for aspect queries
    std::string aspect;
    if (is_aspect_query(query, aspect)) {
        intent.type = QueryIntentType::Aspect;
        intent.aspect = aspect;
        intent.confidence = 0.8f;
        return intent;
    }

    // Check for temporal queries
    TimeRange time_range;
    if (is_temporal_query(query, time_range)) {
        intent.type = QueryIntentType::Temporal;
        intent.time_range = time_range;
        intent.entity = extract_entity(query);  // May still have an entity constraint
        intent.confidence = 0.8f;
        return intent;
    }

    // Check for exploratory queries
    if (is_exploratory_query(query)) {
        intent.type = QueryIntentType::Exploratory;
        intent.confidence = 0.7f;
        return intent;
    }

    // Default to entity query
    intent.type = QueryIntentType::Entity;
    intent.entity = extract_entity(query);
    intent.confidence = 0.6f;

    return intent;
}

} // namespace chitta
