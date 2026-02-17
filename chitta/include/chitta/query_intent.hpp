#pragma once
// Query Intent Classification
//
// Classifies user queries into intent categories for optimized recall:
// - Aspect: "show me all preferences", "what corrections exist"
// - Entity: "what do I know about cmake", "recall exponential_backoff"
// - Temporal: "what happened last week", "memories from January"
// - Exploratory: "browse memories", "show me recent context"
// - Relationship: "what connects X to Y", "how does A relate to B"
// - Code: "find function foo", "where is class Bar defined"
// - Meta: "how many memories", "memory health"

#include <string>
#include <optional>
#include <chrono>
#include <vector>
#include <regex>
#include <unordered_map>
#include <unordered_set>

namespace chitta {

enum class QueryIntentType {
    Aspect,       // "show me all preferences", "what corrections exist"
    Entity,       // "what do I know about cmake", "recall exponential_backoff"
    Temporal,     // "what happened last week", "memories from January"
    Exploratory,  // "browse memories", "show me recent context"
    Relationship, // "what connects X to Y", "how does A relate to B"
    Code,         // "find function foo", "where is class Bar defined"
    Meta          // "how many memories", "memory health"
};

// Finer-grained temporal intent for routing to optimal retrieval path
enum class TemporalSubtype {
    None,           // Not a temporal query
    When,           // "when did X happen" - lookup event timestamp
    Duration,       // "how long has X been" - compute interval
    Sequence,       // "what happened before/after X" - timeline query
    AsOf,           // "what was X in July" - point-in-time lookup
    FirstLast       // "first/last time X" - min/max timestamp
};

inline const char* query_intent_type_to_string(QueryIntentType type) {
    switch (type) {
        case QueryIntentType::Aspect: return "aspect";
        case QueryIntentType::Entity: return "entity";
        case QueryIntentType::Temporal: return "temporal";
        case QueryIntentType::Exploratory: return "exploratory";
        case QueryIntentType::Relationship: return "relationship";
        case QueryIntentType::Code: return "code";
        case QueryIntentType::Meta: return "meta";
    }
    return "unknown";
}

struct TimeRange {
    std::optional<std::chrono::system_clock::time_point> start;
    std::optional<std::chrono::system_clock::time_point> end;

    bool valid() const {
        return start.has_value() || end.has_value();
    }
};

struct QueryIntent {
    QueryIntentType type;
    TemporalSubtype temporal_subtype = TemporalSubtype::None;  // finer temporal routing
    std::string original_query;
    std::optional<std::string> aspect;      // for Aspect queries
    std::optional<std::string> entity;      // for Entity queries
    std::optional<TimeRange> time_range;    // for Temporal queries
    std::optional<std::string> subject;     // for Relationship queries
    std::optional<std::string> object;      // for Relationship queries
    std::vector<std::string> entities;      // extracted entity names
    float confidence;

    QueryIntent()
        : type(QueryIntentType::Entity)
        , temporal_subtype(TemporalSubtype::None)
        , confidence(0.5f)
    {}
};

inline const char* temporal_subtype_to_string(TemporalSubtype st) {
    switch (st) {
        case TemporalSubtype::None: return "none";
        case TemporalSubtype::When: return "when";
        case TemporalSubtype::Duration: return "duration";
        case TemporalSubtype::Sequence: return "sequence";
        case TemporalSubtype::AsOf: return "as_of";
        case TemporalSubtype::FirstLast: return "first_last";
    }
    return "unknown";
}

class QueryIntentClassifier {
public:
    QueryIntentClassifier();

    QueryIntent classify(const std::string& query);

    // Detect temporal subtype for finer routing
    TemporalSubtype detect_temporal_subtype(const std::string& query);

    // Extract named entities (capitalized words, excluding question words)
    std::vector<std::string> extract_entities(const std::string& query);

private:
    bool is_aspect_query(const std::string& query, std::string& aspect);
    bool is_temporal_query(const std::string& query, TimeRange& range);
    bool is_relationship_query(const std::string& query, std::string& subject, std::string& object);
    bool is_code_query(const std::string& query);
    bool is_meta_query(const std::string& query);
    bool is_exploratory_query(const std::string& query);

    TimeRange parse_time_expression(const std::string& expr);
    std::string normalize_query(const std::string& query);
    std::string extract_entity(const std::string& query);

    // Aspect keyword mappings
    std::unordered_map<std::string, std::string> aspect_keywords_;

    // Meta query patterns
    std::unordered_set<std::string> meta_keywords_;

    // Code query patterns
    std::unordered_set<std::string> code_keywords_;

    // Exploratory query patterns
    std::unordered_set<std::string> exploratory_keywords_;

    // Relationship patterns (compiled regex)
    std::vector<std::regex> relationship_patterns_;

    // Temporal patterns (compiled regex)
    std::vector<std::regex> temporal_patterns_;
};

} // namespace chitta
