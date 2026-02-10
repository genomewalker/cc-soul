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

struct SSLLearning {
    std::string type;       // SOLUTION, GOTCHA, DECISION, PATTERN, PREFERENCE, FAILURE
    std::string content;    // Full content including [ε] lines
    std::string title;      // First line (truncated to 100 chars)
    std::string category;   // Lowercase type for observe() category
};

struct SSLTriplet {
    std::string subject;
    std::string predicate;
    std::string object;
};

class SSLParser {
public:
    struct Result {
        std::vector<SSLLearning> learnings;
        std::vector<SSLTriplet> triplets;
    };

    // Parse SSL-formatted output from LLM
    Result parse(const std::string& output);

    // Map SSL type to observe() category
    static std::string type_to_category(const std::string& type);
};

} // namespace chitta
