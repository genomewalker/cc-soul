#include "../include/chitta/temporal.hpp"
#include <cstring>

namespace chitta {

int64_t TemporalResolver::now_ms() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();
}

std::string TemporalResolver::format_iso_date(int64_t timestamp_ms) {
    std::time_t t = timestamp_ms / 1000;
    std::tm tm;
    gmtime_r(&t, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    return oss.str();
}

int TemporalResolver::month_name_to_index(const std::string& month) {
    static const std::array<std::pair<const char*, int>, 24> months = {{
        {"january", 0}, {"jan", 0},
        {"february", 1}, {"feb", 1},
        {"march", 2}, {"mar", 2},
        {"april", 3}, {"apr", 3},
        {"may", 4},
        {"june", 5}, {"jun", 5},
        {"july", 6}, {"jul", 6},
        {"august", 7}, {"aug", 7},
        {"september", 8}, {"sep", 8},
        {"october", 9}, {"oct", 9},
        {"november", 10}, {"nov", 10},
        {"december", 11}, {"dec", 11}
    }};

    std::string lower;
    lower.reserve(month.size());
    for (char c : month) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    for (const auto& [name, idx] : months) {
        if (lower == name) return idx;
    }
    return -1;
}

bool TemporalResolver::is_relative_expression(const std::string& expr) {
    static const std::regex relative_pattern(
        R"(yesterday|today|last\s+\w+|\d+\s+(day|week|month|year)s?\s+ago)",
        std::regex::icase
    );
    return std::regex_search(expr, relative_pattern);
}

int64_t TemporalResolver::tm_to_ms(const std::tm& tm) {
    std::tm tm_copy = tm;
    std::time_t t = timegm(&tm_copy);  // Use timegm for UTC
    return static_cast<int64_t>(t) * 1000;
}

bool TemporalResolver::parse_to_tm(const std::string& date_str, std::tm& tm_out) {
    std::memset(&tm_out, 0, sizeof(tm_out));

    // Try YYYY-MM-DD
    if (std::regex_match(date_str, std::regex(R"(\d{4}-\d{2}-\d{2})"))) {
        std::istringstream ss(date_str);
        ss >> std::get_time(&tm_out, "%Y-%m-%d");
        if (!ss.fail()) return true;
    }

    // Try DD-MMM-YYYY (e.g., 15-Jan-2024)
    static const std::regex dmy_pattern(R"((\d{1,2})[-/](\w+)[-/](\d{4}))");
    std::smatch match;
    if (std::regex_match(date_str, match, dmy_pattern)) {
        int day = std::stoi(match[1].str());
        int month = month_name_to_index(match[2].str());
        int year = std::stoi(match[3].str());
        if (month >= 0) {
            tm_out.tm_mday = day;
            tm_out.tm_mon = month;
            tm_out.tm_year = year - 1900;
            return true;
        }
    }

    // Try MMM DD, YYYY (e.g., Jan 15, 2024)
    static const std::regex mdy_pattern(R"((\w+)\s+(\d{1,2}),?\s*(\d{4}))");
    if (std::regex_match(date_str, match, mdy_pattern)) {
        int month = month_name_to_index(match[1].str());
        int day = std::stoi(match[2].str());
        int year = std::stoi(match[3].str());
        if (month >= 0) {
            tm_out.tm_mday = day;
            tm_out.tm_mon = month;
            tm_out.tm_year = year - 1900;
            return true;
        }
    }

    // Try YYYYMMDD
    if (date_str.size() == 8 && std::all_of(date_str.begin(), date_str.end(), ::isdigit)) {
        tm_out.tm_year = std::stoi(date_str.substr(0, 4)) - 1900;
        tm_out.tm_mon = std::stoi(date_str.substr(4, 2)) - 1;
        tm_out.tm_mday = std::stoi(date_str.substr(6, 2));
        return true;
    }

    return false;
}

std::optional<int64_t> TemporalResolver::parse_absolute_date(const std::string& date_str) {
    std::tm tm;
    if (parse_to_tm(date_str, tm)) {
        return tm_to_ms(tm);
    }
    return std::nullopt;
}

std::optional<ResolvedDate> TemporalResolver::parse_relative(
    const std::string& expr,
    int64_t context_date_ms
) {
    std::string lower;
    lower.reserve(expr.size());
    for (char c : expr) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    ResolvedDate result;
    result.is_approximate = true;

    // Parse "yesterday"
    if (lower == "yesterday") {
        result.timestamp_ms = context_date_ms - 86400000;  // 24 hours in ms
        result.date_str = format_iso_date(result.timestamp_ms);
        return result;
    }

    // Parse "today"
    if (lower == "today") {
        result.timestamp_ms = context_date_ms;
        result.date_str = format_iso_date(result.timestamp_ms);
        result.is_approximate = false;
        return result;
    }

    // Parse "last N days/weeks/months/years"
    static const std::regex last_n_pattern(R"(last\s+(\d+)\s+(day|week|month|year)s?)");
    std::smatch match;
    if (std::regex_search(lower, match, last_n_pattern)) {
        int count = std::stoi(match[1].str());
        std::string unit = match[2].str();

        int64_t offset_ms = 0;
        if (unit == "day") {
            offset_ms = static_cast<int64_t>(count) * 86400000;
        } else if (unit == "week") {
            offset_ms = static_cast<int64_t>(count) * 7 * 86400000;
        } else if (unit == "month") {
            offset_ms = static_cast<int64_t>(count) * 30 * 86400000;  // Approximate
        } else if (unit == "year") {
            offset_ms = static_cast<int64_t>(count) * 365 * 86400000;  // Approximate
        }

        result.timestamp_ms = context_date_ms - offset_ms;
        result.range_end_ms = context_date_ms;
        result.date_str = format_iso_date(result.timestamp_ms);
        return result;
    }

    // Parse "last week/month/year" (single unit)
    static const std::regex last_unit_pattern(R"(last\s+(week|month|year))");
    if (std::regex_search(lower, match, last_unit_pattern)) {
        std::string unit = match[1].str();

        int64_t offset_ms = 0;
        if (unit == "week") {
            offset_ms = static_cast<int64_t>(7) * 86400000;
        } else if (unit == "month") {
            offset_ms = static_cast<int64_t>(30) * 86400000;
        } else if (unit == "year") {
            offset_ms = static_cast<int64_t>(365) * 86400000;
        }

        result.timestamp_ms = context_date_ms - offset_ms;
        result.range_end_ms = context_date_ms;
        result.date_str = format_iso_date(result.timestamp_ms);
        return result;
    }

    // Parse "N days/weeks/months/years ago"
    static const std::regex ago_pattern(R"((\d+)\s+(day|week|month|year)s?\s+ago)");
    if (std::regex_search(lower, match, ago_pattern)) {
        int count = std::stoi(match[1].str());
        std::string unit = match[2].str();

        int64_t offset_ms = 0;
        if (unit == "day") {
            offset_ms = static_cast<int64_t>(count) * 86400000;
        } else if (unit == "week") {
            offset_ms = static_cast<int64_t>(count) * 7 * 86400000;
        } else if (unit == "month") {
            offset_ms = static_cast<int64_t>(count) * 30 * 86400000;
        } else if (unit == "year") {
            offset_ms = static_cast<int64_t>(count) * 365 * 86400000;
        }

        result.timestamp_ms = context_date_ms - offset_ms;
        result.date_str = format_iso_date(result.timestamp_ms);
        return result;
    }

    return std::nullopt;
}

std::optional<ResolvedDate> TemporalResolver::resolve(
    const std::string& expression,
    int64_t context_date_ms
) {
    // Trim whitespace
    std::string expr = expression;
    while (!expr.empty() && std::isspace(static_cast<unsigned char>(expr.front()))) {
        expr.erase(expr.begin());
    }
    while (!expr.empty() && std::isspace(static_cast<unsigned char>(expr.back()))) {
        expr.pop_back();
    }

    if (expr.empty()) {
        return std::nullopt;
    }

    // Check if it's a relative expression
    if (is_relative_expression(expr)) {
        return parse_relative(expr, context_date_ms);
    }

    // Try parsing as absolute date
    std::tm tm;
    if (parse_to_tm(expr, tm)) {
        ResolvedDate result;
        result.timestamp_ms = tm_to_ms(tm);
        result.date_str = format_iso_date(result.timestamp_ms);
        result.is_approximate = false;
        return result;
    }

    return std::nullopt;
}

std::optional<std::string> TemporalResolver::extract_date_annotation(const std::string& text) {
    // Match @YYYY-MM-DD or @relative_expression
    static const std::regex date_pattern(
        R"(@(\d{4}-\d{2}-\d{2}|yesterday|today|last\s+\d*\s*(?:day|week|month|year)s?|\d+\s+(?:day|week|month|year)s?\s+ago))",
        std::regex::icase
    );

    std::smatch match;
    if (std::regex_search(text, match, date_pattern)) {
        return match[1].str();
    }

    return std::nullopt;
}

} // namespace chitta
