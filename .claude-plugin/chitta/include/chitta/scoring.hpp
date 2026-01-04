#pragma once
// Scoring: soul-aware relevance ranking
//
// Not just similarity. Relevance = f(similarity, confidence, recency, type).
// The soul knows what matters.

#include "types.hpp"
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace chitta {

// ═══════════════════════════════════════════════════════════════════════════
// 1. Soul-Aware Scoring
// ═══════════════════════════════════════════════════════════════════════════

struct ScoringConfig {
    float confidence_weight = 0.5f;   // How much confidence matters (0-1)
    float recency_weight = 0.3f;      // How much recency matters (0-1)
    float recency_halflife_days = 30.0f;  // Days until recency boost halves

    // Type boosts (multiplicative)
    float failure_boost = 1.2f;   // Failures are gold
    float belief_boost = 1.1f;    // Beliefs are foundational
    float wisdom_boost = 1.0f;    // Wisdom is baseline
    float episode_boost = 0.9f;   // Episodes are contextual
};

inline float type_boost(NodeType type, const ScoringConfig& config) {
    switch (type) {
        case NodeType::Failure:   return config.failure_boost;
        case NodeType::Belief:    return config.belief_boost;
        case NodeType::Invariant: return config.belief_boost;
        case NodeType::Wisdom:    return config.wisdom_boost;
        case NodeType::Episode:   return config.episode_boost;
        default: return 1.0f;
    }
}

// Soul-aware relevance score
// Combines semantic similarity with confidence, recency, and type
inline float soul_relevance(
    float similarity,
    const Node& node,
    Timestamp now,
    const ScoringConfig& config = {})
{
    // Confidence factor: 0.5 + 0.5 * effective_confidence
    // High confidence → up to 1.0x, low confidence → down to 0.5x
    float conf_effective = node.kappa.effective();
    float conf_factor = (1.0f - config.confidence_weight) +
                        config.confidence_weight * conf_effective;

    // Recency factor: exponential decay from last access
    // Recently accessed → boost, old → neutral
    float days_ago = static_cast<float>(now - node.tau_accessed) / 86400000.0f;
    float recency_decay = std::exp(-days_ago * 0.693f / config.recency_halflife_days);
    float recency_factor = 1.0f + config.recency_weight * recency_decay;

    // Type boost
    float type_factor = type_boost(node.node_type, config);

    // Combined score
    return similarity * conf_factor * recency_factor * type_factor;
}


// ═══════════════════════════════════════════════════════════════════════════
// 2. BM25 Sparse Retrieval
// ═══════════════════════════════════════════════════════════════════════════

// Simple tokenizer for BM25
inline std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;

    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            current += std::tolower(static_cast<unsigned char>(c));
        } else if (!current.empty()) {
            if (current.length() >= 2) {  // Skip single chars
                tokens.push_back(current);
            }
            current.clear();
        }
    }
    if (!current.empty() && current.length() >= 2) {
        tokens.push_back(current);
    }
    return tokens;
}

// BM25 parameters
struct BM25Config {
    float k1 = 1.5f;   // Term frequency saturation
    float b = 0.75f;   // Length normalization
};

// BM25 index for sparse retrieval
class BM25Index {
public:
    explicit BM25Index(BM25Config config = {}) : config_(config) {}

    // Add a document
    void add(NodeId id, const std::string& text) {
        auto tokens = tokenize(text);
        if (tokens.empty()) return;

        doc_lengths_[id] = tokens.size();
        total_length_ += tokens.size();
        doc_count_++;

        // Update term frequencies
        std::unordered_map<std::string, size_t> term_freq;
        for (const auto& token : tokens) {
            term_freq[token]++;
        }

        doc_terms_[id] = term_freq;

        // Update document frequencies
        for (const auto& [term, freq] : term_freq) {
            doc_freqs_[term]++;
        }
    }

    // Remove a document
    void remove(NodeId id) {
        auto it = doc_terms_.find(id);
        if (it == doc_terms_.end()) return;

        // Update doc freqs
        for (const auto& [term, freq] : it->second) {
            if (--doc_freqs_[term] == 0) {
                doc_freqs_.erase(term);
            }
        }

        total_length_ -= doc_lengths_[id];
        doc_count_--;

        doc_terms_.erase(it);
        doc_lengths_.erase(id);
    }

    // Search with BM25 scoring
    std::vector<std::pair<NodeId, float>> search(
        const std::string& query, size_t limit) const
    {
        auto query_tokens = tokenize(query);
        if (query_tokens.empty() || doc_count_ == 0) return {};

        float avg_dl = static_cast<float>(total_length_) / doc_count_;

        std::vector<std::pair<NodeId, float>> scores;

        for (const auto& [id, terms] : doc_terms_) {
            float score = 0.0f;
            float dl = static_cast<float>(doc_lengths_.at(id));

            for (const auto& qt : query_tokens) {
                auto tf_it = terms.find(qt);
                if (tf_it == terms.end()) continue;

                float tf = static_cast<float>(tf_it->second);

                auto df_it = doc_freqs_.find(qt);
                if (df_it == doc_freqs_.end()) continue;

                float df = static_cast<float>(df_it->second);

                // IDF with smoothing
                float idf = std::log((doc_count_ - df + 0.5f) / (df + 0.5f) + 1.0f);

                // BM25 term score
                float numerator = tf * (config_.k1 + 1.0f);
                float denominator = tf + config_.k1 * (1.0f - config_.b +
                                    config_.b * dl / avg_dl);

                score += idf * numerator / denominator;
            }

            if (score > 0.0f) {
                scores.emplace_back(id, score);
            }
        }

        // Sort by score descending
        std::sort(scores.begin(), scores.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        if (scores.size() > limit) {
            scores.resize(limit);
        }

        return scores;
    }

    size_t size() const { return doc_count_; }

private:
    BM25Config config_;
    size_t doc_count_ = 0;
    size_t total_length_ = 0;

    // NodeId -> {term -> frequency}
    std::unordered_map<NodeId, std::unordered_map<std::string, size_t>, NodeIdHash> doc_terms_;
    // NodeId -> document length
    std::unordered_map<NodeId, size_t, NodeIdHash> doc_lengths_;
    // term -> number of documents containing term
    std::unordered_map<std::string, size_t> doc_freqs_;
};


// ═══════════════════════════════════════════════════════════════════════════
// 3. Hybrid Retrieval with RRF
// ═══════════════════════════════════════════════════════════════════════════

// Reciprocal Rank Fusion - combines multiple ranked lists
inline std::vector<std::pair<NodeId, float>> rrf_fusion(
    const std::vector<std::pair<NodeId, float>>& dense_results,
    const std::vector<std::pair<NodeId, float>>& sparse_results,
    float k = 60.0f,  // RRF constant
    float dense_weight = 0.7f)
{
    std::unordered_map<NodeId, float, NodeIdHash> combined;

    // Add dense results
    for (size_t i = 0; i < dense_results.size(); ++i) {
        float rrf_score = dense_weight / (k + i + 1);
        combined[dense_results[i].first] += rrf_score;
    }

    // Add sparse results
    float sparse_weight = 1.0f - dense_weight;
    for (size_t i = 0; i < sparse_results.size(); ++i) {
        float rrf_score = sparse_weight / (k + i + 1);
        combined[sparse_results[i].first] += rrf_score;
    }

    // Convert to sorted vector
    std::vector<std::pair<NodeId, float>> results;
    for (const auto& [id, score] : combined) {
        results.emplace_back(id, score);
    }

    std::sort(results.begin(), results.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    return results;
}


// ═══════════════════════════════════════════════════════════════════════════
// 4. Cross-Encoder Re-ranking (placeholder for ONNX model)
// ═══════════════════════════════════════════════════════════════════════════

// Cross-encoder scores query-document pairs directly
// Much more accurate than bi-encoder but slower (O(n) inference vs O(1) lookup)
class CrossEncoder {
public:
    // In a full implementation, this would load an ONNX cross-encoder model
    // For now, we provide a simple heuristic-based approximation

    float score(const std::string& query, const std::string& document) const {
        // Heuristic: exact phrase matching + term overlap
        auto query_tokens = tokenize(query);
        auto doc_tokens = tokenize(document);

        if (query_tokens.empty() || doc_tokens.empty()) return 0.0f;

        std::unordered_set<std::string> doc_set(doc_tokens.begin(), doc_tokens.end());

        size_t matches = 0;
        for (const auto& qt : query_tokens) {
            if (doc_set.count(qt)) matches++;
        }

        // Jaccard-like overlap
        float overlap = static_cast<float>(matches) / query_tokens.size();

        // Boost for query appearing as substring
        std::string query_lower, doc_lower;
        for (char c : query) query_lower += std::tolower(static_cast<unsigned char>(c));
        for (char c : document) doc_lower += std::tolower(static_cast<unsigned char>(c));

        if (doc_lower.find(query_lower) != std::string::npos) {
            overlap = std::min(1.0f, overlap + 0.3f);
        }

        return overlap;
    }

    // Re-rank candidates
    std::vector<std::pair<NodeId, float>> rerank(
        const std::string& query,
        const std::vector<std::pair<NodeId, std::string>>& candidates,
        size_t top_k) const
    {
        std::vector<std::pair<NodeId, float>> scored;

        for (const auto& [id, text] : candidates) {
            float s = score(query, text);
            scored.emplace_back(id, s);
        }

        std::sort(scored.begin(), scored.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        if (scored.size() > top_k) {
            scored.resize(top_k);
        }

        return scored;
    }
};

} // namespace chitta
