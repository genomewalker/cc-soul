#include "../include/chitta/ssl_parser.hpp"
#include <sstream>
#include <algorithm>

namespace chitta {

std::string SSLParser::type_to_category(const std::string& type) {
    if (type == "SOLUTION") return "solution";
    if (type == "GOTCHA") return "gotcha";
    if (type == "DECISION") return "decision";
    if (type == "PATTERN") return "pattern";
    if (type == "PREFERENCE") return "preference";
    if (type == "FAILURE") return "failure";
    return "wisdom";
}

// Extract @file:line citations from text
std::vector<SSLCitation> SSLParser::extract_inline_citations(const std::string& text) {
    std::vector<SSLCitation> citations;

    // Match @path/file.ext:line or @path/file.ext patterns
    static const std::regex cite_pattern(R"(@([\w./\-]+(?:\.\w+)?):?(\d*))");

    auto begin = std::sregex_iterator(text.begin(), text.end(), cite_pattern);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        SSLCitation cite;
        cite.file = (*it)[1].str();
        std::string line_str = (*it)[2].str();
        cite.line = line_str.empty() ? 0 : std::stoi(line_str);
        citations.push_back(std::move(cite));
    }

    return citations;
}

// Parse explicit [CITE] line: file:line optional context
SSLCitation SSLParser::parse_cite_line(const std::string& line) {
    SSLCitation cite;

    // Format: [CITE] file:line context  OR  [CITE] file context
    static const std::regex cite_line_pattern(R"(^\[CITE\]\s+([\w./\-]+(?:\.\w+)?):?(\d*)\s*(.*)?$)");

    std::smatch match;
    if (std::regex_match(line, match, cite_line_pattern)) {
        cite.file = match[1].str();
        std::string line_str = match[2].str();
        cite.line = line_str.empty() ? 0 : std::stoi(line_str);
        cite.context = match[3].str();

        // Trim context whitespace
        while (!cite.context.empty() && std::isspace(cite.context.back())) {
            cite.context.pop_back();
        }
    }

    return cite;
}

SSLParser::Result SSLParser::parse(const std::string& output) {
    Result result;

    // State machine for parsing
    std::string current_type;
    std::string current_content;
    std::vector<SSLCitation> current_citations;

    // Regex for typed markers
    static const std::regex type_pattern(R"(^\[(SOLUTION|GOTCHA|DECISION|PATTERN|PREFERENCE|FAILURE)\]\s+(.*)$)");
    static const std::regex triplet_pattern(R"(^\[TRIPLET\]\s+(\S+)\s+(\S+)\s+(.+)$)");
    static const std::regex cite_line_pattern(R"(^\[CITE\]\s+)");

    auto store_current = [&]() {
        if (current_type.empty() || current_content.empty()) return;

        SSLLearning learning;
        learning.type = current_type;
        learning.content = current_content;
        learning.category = type_to_category(current_type);

        // Extract title (first line, truncated)
        size_t newline_pos = current_content.find('\n');
        if (newline_pos != std::string::npos) {
            learning.title = current_content.substr(0, std::min(newline_pos, size_t(100)));
        } else {
            learning.title = current_content.substr(0, std::min(current_content.size(), size_t(100)));
        }

        // Add explicit [CITE] citations collected during parsing
        learning.citations = std::move(current_citations);
        current_citations.clear();

        // Also extract inline @file:line citations from content
        auto inline_cites = extract_inline_citations(current_content);
        for (auto& cite : inline_cites) {
            // Avoid duplicates
            bool exists = false;
            for (const auto& existing : learning.citations) {
                if (existing.file == cite.file && existing.line == cite.line) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                learning.citations.push_back(std::move(cite));
            }
        }

        result.learnings.push_back(std::move(learning));
        current_type.clear();
        current_content.clear();
    };

    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        // Skip empty lines and markdown artifacts
        if (line.empty()) continue;
        if (line.substr(0, 3) == "```") continue;
        if (line == "---") continue;

        std::smatch match;

        // Check for typed marker line
        if (std::regex_match(line, match, type_pattern)) {
            store_current();
            current_type = match[1].str();
            current_content = match[2].str();
        }
        // Check for epsilon (verbatim) line
        else if (line.substr(0, 3) == "[ε]" && !current_type.empty()) {
            current_content += "\n" + line;
        }
        // Check for citation line
        else if (std::regex_search(line, cite_line_pattern) && !current_type.empty()) {
            auto cite = parse_cite_line(line);
            if (!cite.file.empty()) {
                current_citations.push_back(std::move(cite));
            }
        }
        // Check for triplet relationship
        else if (std::regex_match(line, match, triplet_pattern)) {
            store_current();

            SSLTriplet triplet;
            triplet.subject = match[1].str();
            triplet.predicate = match[2].str();
            triplet.object = match[3].str();

            // Trim whitespace from object
            while (!triplet.object.empty() && std::isspace(triplet.object.back())) {
                triplet.object.pop_back();
            }

            if (!triplet.subject.empty() && !triplet.predicate.empty() && !triplet.object.empty()) {
                result.triplets.push_back(std::move(triplet));
            }
        }
        // Continuation line for current marker (no prefix)
        else if (!current_type.empty() && line[0] != '[') {
            current_content += "\n" + line;
        }
    }

    // Store final marker if pending
    store_current();

    return result;
}

} // namespace chitta
