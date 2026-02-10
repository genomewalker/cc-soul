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

SSLParser::Result SSLParser::parse(const std::string& output) {
    Result result;

    // State machine for parsing
    std::string current_type;
    std::string current_content;

    // Regex for typed markers
    static const std::regex type_pattern(R"(^\[(SOLUTION|GOTCHA|DECISION|PATTERN|PREFERENCE|FAILURE)\]\s+(.*)$)");
    static const std::regex triplet_pattern(R"(^\[TRIPLET\]\s+(\S+)\s+(\S+)\s+(.+)$)");

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
