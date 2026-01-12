#pragma once
// Query Compass Router: Intent classification for optimal retrieval path
//
// Routes queries to the most efficient retrieval method:
// - Triplet queries: exact subject/predicate/object lookups (O(1))
// - Tag queries: tag-based filtering (O(tags))
// - Embedding queries: vector similarity search (O(log N))
// - Hybrid queries: combination of above
//
// This prevents expensive embedding search when simpler methods suffice.

#include "types.hpp"
#include <string>
#include <vector>
#include <regex>
#include <algorithm>
#include <cctype>

namespace chitta {

// Query intent classification
enum class QueryIntent {
    TripletLookup,    // "what does X relate_to?" -> triplet query
    TagFilter,        // "#domain:ml" -> tag lookup
    SemanticSearch,   // "how does authentication work?" -> embedding
    ExactMatch,       // "function_name" -> exact text match
    Hybrid,           // combination of above
    Unknown           // fall back to semantic
};

// Routing decision with confidence
struct RoutingDecision {
    QueryIntent primary_intent;
    float confidence;           // 0-1, how confident in classification

    // Extracted query components
    std::string subject;        // for triplet queries
    std::string predicate;
    std::string object;
    std::vector<std::string> tags;    // for tag queries
    std::string semantic_query; // for embedding search

    // Fallback chain (if primary fails, try these)
    std::vector<QueryIntent> fallbacks;
};

// Query pattern matchers
struct QueryPatterns {
    // Triplet patterns: "X relates_to Y", "what does X do", "how is X connected to Y"
    std::regex triplet_subject_pattern{"^what\\s+(?:does|is|are)\\s+(.+?)\\s+(?:do|relate|connect)"};
    std::regex triplet_relation_pattern{"(.+?)\\s+(relates?_?to|causes?|depends?_?on|uses?|calls?|contains?)\\s+(.+)"};

    // Tag patterns: "#tag", "tag:value", "[tag]"
    std::regex tag_pattern{"(?:#|\\[)([a-zA-Z0-9_:-]+)(?:\\])?|([a-zA-Z0-9_-]+):([a-zA-Z0-9_-]+)"};

    // Code patterns: function_name, ClassName, file.ext
    std::regex code_pattern{"^[a-zA-Z_][a-zA-Z0-9_]*(?:\\.[a-zA-Z]+)?$"};
    std::regex qualified_name_pattern{"^[a-zA-Z_][a-zA-Z0-9_]*(?:::[a-zA-Z_][a-zA-Z0-9_]*)+$"};
};

class QueryRouter {
public:
    QueryRouter() = default;

    // Classify query and determine optimal routing
    RoutingDecision route(const std::string& query) const {
        RoutingDecision decision;
        decision.semantic_query = query;  // Always keep original for fallback
        decision.confidence = 0.0f;
        decision.primary_intent = QueryIntent::Unknown;

        if (query.empty()) {
            return decision;
        }

        // Try each classifier in order of specificity

        // 1. Check for tag patterns (highest specificity)
        auto tags = extract_tags(query);
        if (!tags.empty()) {
            decision.tags = tags;
            if (is_pure_tag_query(query, tags)) {
                decision.primary_intent = QueryIntent::TagFilter;
                decision.confidence = 0.95f;
                decision.fallbacks = {QueryIntent::SemanticSearch};
                return decision;
            }
        }

        // 2. Check for triplet patterns
        auto [subject, predicate, object] = extract_triplet(query);
        if (!predicate.empty()) {
            decision.subject = subject;
            decision.predicate = predicate;
            decision.object = object;
            decision.primary_intent = QueryIntent::TripletLookup;
            decision.confidence = 0.85f;
            decision.fallbacks = {QueryIntent::SemanticSearch};
            return decision;
        }

        // 3. Check for exact code/identifier match
        if (looks_like_identifier(query)) {
            decision.primary_intent = QueryIntent::ExactMatch;
            decision.confidence = 0.80f;
            decision.fallbacks = {QueryIntent::TagFilter, QueryIntent::SemanticSearch};
            return decision;
        }

        // 4. Check if it's a hybrid query (has tags + natural language)
        if (!tags.empty()) {
            decision.tags = tags;
            decision.semantic_query = remove_tags(query, tags);
            decision.primary_intent = QueryIntent::Hybrid;
            decision.confidence = 0.75f;
            decision.fallbacks = {QueryIntent::SemanticSearch};
            return decision;
        }

        // 5. Default to semantic search
        decision.primary_intent = QueryIntent::SemanticSearch;
        decision.confidence = 0.60f;  // Lower confidence = try fallbacks if poor results
        decision.fallbacks = {QueryIntent::TagFilter};  // Try tag extraction as fallback

        return decision;
    }

    // Intent to string for debugging
    static std::string intent_name(QueryIntent intent) {
        switch (intent) {
            case QueryIntent::TripletLookup: return "triplet";
            case QueryIntent::TagFilter: return "tag";
            case QueryIntent::SemanticSearch: return "semantic";
            case QueryIntent::ExactMatch: return "exact";
            case QueryIntent::Hybrid: return "hybrid";
            default: return "unknown";
        }
    }

private:
    QueryPatterns patterns_;

    // Extract hashtags and colon-tags from query
    std::vector<std::string> extract_tags(const std::string& query) const {
        std::vector<std::string> tags;

        // Match #tag patterns
        std::sregex_iterator it(query.begin(), query.end(), patterns_.tag_pattern);
        std::sregex_iterator end;

        while (it != end) {
            std::smatch match = *it;
            if (match[1].matched) {
                tags.push_back(match[1].str());
            } else if (match[2].matched && match[3].matched) {
                tags.push_back(match[2].str() + ":" + match[3].str());
            }
            ++it;
        }

        return tags;
    }

    // Check if query is purely tags (no other content)
    bool is_pure_tag_query(const std::string& query,
                          const std::vector<std::string>& tags) const {
        std::string remaining = query;
        for (const auto& tag : tags) {
            // Remove tag occurrences
            size_t pos;
            std::string patterns[] = {"#" + tag, "[" + tag + "]"};
            for (const auto& p : patterns) {
                while ((pos = remaining.find(p)) != std::string::npos) {
                    remaining.erase(pos, p.length());
                }
            }
            // Also remove colon form (already extracted)
            size_t colon = tag.find(':');
            if (colon != std::string::npos) {
                std::string colon_form = tag.substr(0, colon) + ":" + tag.substr(colon + 1);
                while ((pos = remaining.find(colon_form)) != std::string::npos) {
                    remaining.erase(pos, colon_form.length());
                }
            }
        }
        // Check if remaining is just whitespace
        return std::all_of(remaining.begin(), remaining.end(),
            [](char c) { return std::isspace(static_cast<unsigned char>(c)); });
    }

    // Extract triplet (subject, predicate, object) from query
    std::tuple<std::string, std::string, std::string>
    extract_triplet(const std::string& query) const {
        std::smatch match;

        // Try explicit relation pattern first
        if (std::regex_search(query, match, patterns_.triplet_relation_pattern)) {
            return {
                trim(match[1].str()),
                normalize_predicate(match[2].str()),
                trim(match[3].str())
            };
        }

        // Try subject query pattern
        if (std::regex_search(query, match, patterns_.triplet_subject_pattern)) {
            return {trim(match[1].str()), "", ""};
        }

        return {"", "", ""};
    }

    // Check if query looks like a code identifier
    bool looks_like_identifier(const std::string& query) const {
        // Must be single word, no spaces
        if (query.find(' ') != std::string::npos) return false;

        return std::regex_match(query, patterns_.code_pattern) ||
               std::regex_match(query, patterns_.qualified_name_pattern);
    }

    // Remove tags from query, leaving semantic content
    std::string remove_tags(const std::string& query,
                           const std::vector<std::string>& tags) const {
        std::string result = query;
        for (const auto& tag : tags) {
            size_t pos;
            std::string patterns[] = {"#" + tag + " ", "#" + tag,
                                      "[" + tag + "] ", "[" + tag + "]"};
            for (const auto& p : patterns) {
                while ((pos = result.find(p)) != std::string::npos) {
                    result.erase(pos, p.length());
                }
            }
        }
        return trim(result);
    }

    // Normalize predicate names (relates_to -> relates_to, "relates to" -> relates_to)
    static std::string normalize_predicate(const std::string& pred) {
        std::string result;
        for (char c : pred) {
            if (c == ' ' || c == '-') {
                result += '_';
            } else {
                result += std::tolower(static_cast<unsigned char>(c));
            }
        }
        return result;
    }

    // Trim whitespace
    static std::string trim(const std::string& s) {
        size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
            start++;
        }
        size_t end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
            end--;
        }
        return s.substr(start, end - start);
    }
};

} // namespace chitta
