#pragma once
// TemporalResolver: Date resolution for temporal fact versioning
//
// Resolves relative dates ("yesterday", "last week") against context dates
// and parses absolute dates in various formats.
//
// Used by:
// - SSL parser: resolve @date annotations in triplets
// - Query API: parse "at_date" parameters for point-in-time queries

#include <string>
#include <optional>
#include <cstdint>
#include <chrono>
#include <regex>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace chitta {

// Result of date resolution
struct ResolvedDate {
    int64_t timestamp_ms = 0;       // Unix timestamp in milliseconds
    std::string date_str;           // ISO format YYYY-MM-DD
    bool is_approximate = false;    // True for ranges or fuzzy dates
    std::optional<int64_t> range_end_ms;  // For date ranges ("last week")
};

// Temporal resolver for date expressions
class TemporalResolver {
public:
    // Resolve a relative or absolute date expression against a context date
    // context_date_ms: the reference point for relative dates (e.g., session date)
    // Returns nullopt if the expression cannot be parsed
    static std::optional<ResolvedDate> resolve(
        const std::string& expression,
        int64_t context_date_ms
    );

    // Parse an absolute date string (no context needed)
    // Supports: YYYY-MM-DD, DD-MMM-YYYY, MMM DD YYYY, etc.
    static std::optional<int64_t> parse_absolute_date(const std::string& date_str);

    // Extract @date annotation from SSL text
    // Returns the date expression without the @ prefix, or nullopt if not found
    static std::optional<std::string> extract_date_annotation(const std::string& text);

    // Format a timestamp as ISO date (YYYY-MM-DD)
    static std::string format_iso_date(int64_t timestamp_ms);

    // Get current time in milliseconds
    static int64_t now_ms();

    // Parse date string to struct tm (helper)
    static bool parse_to_tm(const std::string& date_str, std::tm& tm_out);

    // Convert struct tm to milliseconds since epoch
    static int64_t tm_to_ms(const std::tm& tm);

private:
    // Helper to convert month name to 0-based index
    static int month_name_to_index(const std::string& month);

    // Helper to check if a string looks like a relative date expression
    static bool is_relative_expression(const std::string& expr);

    // Parse relative expressions like "yesterday", "last week", etc.
    static std::optional<ResolvedDate> parse_relative(
        const std::string& expr,
        int64_t context_date_ms
    );
};

} // namespace chitta
