#pragma once
// SSLParser: Parse SSL v0.2 formatted output from LLM distillation
//
// Extracts:
// - Typed learnings ([SOLUTION], [GOTCHA], etc.) with [ε] verbatim lines
// - Triplet relationships ([TRIPLET] subject predicate object)

#include <string>
#include <vector>
#include <regex>

namespace chitta {

struct SSLCitation {
    std::string file;       // File path (e.g., "src/main.cpp")
    int line = 0;           // Line number (0 if not specified)
    std::string context;    // Optional context/reason for citation
};

struct SSLLearning {
    std::string type;       // SOLUTION, GOTCHA, DECISION, PATTERN, PREFERENCE, FAILURE
    std::string content;    // Full content including [ε] lines
    std::string title;      // First line (truncated to 100 chars)
    std::string category;   // Lowercase type for observe() category
    std::vector<SSLCitation> citations;  // Code locations referenced
};

struct SSLTriplet {
    std::string subject;
    std::string predicate;
    std::string object;
    std::string date_annotation;  // @YYYY-MM-DD or relative date from SSL
    int64_t valid_from_ms = 0;    // Resolved timestamp (0 = not resolved)
};

class SSLParser {
public:
    struct Result {
        std::vector<SSLLearning> learnings;
        std::vector<SSLTriplet> triplets;
    };

    // Parse SSL-formatted output from LLM
    Result parse(const std::string& output);

    // Parse SSL output with context date for resolving @date annotations
    // context_date_ms: reference timestamp for relative dates (e.g., session start)
    Result parse_with_context(const std::string& output, int64_t context_date_ms);

    // Map SSL type to observe() category
    static std::string type_to_category(const std::string& type);

    // Extract @file:line citations from text
    static std::vector<SSLCitation> extract_inline_citations(const std::string& text);

    // Parse explicit [CITE] line
    static SSLCitation parse_cite_line(const std::string& line);

    // Extract @date annotation from triplet object
    // Returns (object_without_date, date_expression) or (original, "")
    static std::pair<std::string, std::string> extract_date_from_object(const std::string& object);
};

} // namespace chitta
