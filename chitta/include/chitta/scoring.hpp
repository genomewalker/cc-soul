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

// Posting entry: document ID with pre-computed term frequency
struct Posting {
    NodeId doc_id;
    float tf;  // Term frequency (raw count)

    Posting(NodeId id, float freq) : doc_id(id), tf(freq) {}
};

// BM25 index with inverted posting lists for O(query_terms × avg_posting_length) search
class BM25Index {
public:
    explicit BM25Index(BM25Config config = {}) : config_(config) {}

    // Add a document - O(tokens)
    void add(NodeId id, const std::string& text) {
        auto tokens = tokenize(text);
        if (tokens.empty()) return;

        // Remove if already exists (update case)
        if (doc_lengths_.count(id)) {
            remove(id);
        }

        doc_lengths_[id] = tokens.size();
        total_length_ += tokens.size();
        doc_count_++;

        // Count term frequencies
        std::unordered_map<std::string, size_t> term_freq;
        for (const auto& token : tokens) {
            term_freq[token]++;
        }

        // Add to inverted index (posting lists)
        for (const auto& [term, freq] : term_freq) {
            postings_[term].emplace_back(id, static_cast<float>(freq));
            doc_freqs_[term]++;
        }

        // Store terms for this doc (needed for removal)
        doc_terms_[id] = std::move(term_freq);

        // Invalidate IDF cache
        idf_dirty_ = true;
    }

    // Remove a document - O(terms in doc)
    void remove(NodeId id) {
        auto it = doc_terms_.find(id);
        if (it == doc_terms_.end()) return;

        // Remove from posting lists
        for (const auto& [term, freq] : it->second) {
            auto pit = postings_.find(term);
            if (pit != postings_.end()) {
                auto& list = pit->second;
                list.erase(
                    std::remove_if(list.begin(), list.end(),
                        [&id](const Posting& p) { return p.doc_id == id; }),
                    list.end());
                if (list.empty()) {
                    postings_.erase(pit);
                }
            }

            if (--doc_freqs_[term] == 0) {
                doc_freqs_.erase(term);
            }
        }

        total_length_ -= doc_lengths_[id];
        doc_count_--;

        doc_terms_.erase(it);
        doc_lengths_.erase(id);

        // Invalidate IDF cache
        idf_dirty_ = true;
    }

    // Search with BM25 scoring - O(query_terms × avg_posting_length)
    std::vector<std::pair<NodeId, float>> search(
        const std::string& query, size_t limit) const
    {
        auto query_tokens = tokenize(query);
        if (query_tokens.empty() || doc_count_ == 0) return {};

        // Refresh IDF cache if needed
        if (idf_dirty_) {
            refresh_idf_cache();
        }

        float avg_dl = static_cast<float>(total_length_) / doc_count_;

        // Accumulate scores by document
        std::unordered_map<NodeId, float, NodeIdHash> scores;

        for (const auto& qt : query_tokens) {
            auto pit = postings_.find(qt);
            if (pit == postings_.end()) continue;

            auto idf_it = idf_cache_.find(qt);
            if (idf_it == idf_cache_.end()) continue;
            float idf = idf_it->second;

            // Iterate only documents containing this term
            for (const auto& posting : pit->second) {
                auto dl_it = doc_lengths_.find(posting.doc_id);
                if (dl_it == doc_lengths_.end()) continue;
                float dl = static_cast<float>(dl_it->second);

                // BM25 term score
                float numerator = posting.tf * (config_.k1 + 1.0f);
                float denominator = posting.tf + config_.k1 * (1.0f - config_.b +
                                    config_.b * dl / avg_dl);

                scores[posting.doc_id] += idf * numerator / denominator;
            }
        }

        // Convert to vector and sort
        std::vector<std::pair<NodeId, float>> results;
        results.reserve(scores.size());
        for (const auto& [id, score] : scores) {
            results.emplace_back(id, score);
        }

        // Partial sort for top-k
        if (results.size() > limit) {
            std::partial_sort(results.begin(), results.begin() + limit, results.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });
            results.resize(limit);
        } else {
            std::sort(results.begin(), results.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });
        }

        return results;
    }

    size_t size() const { return doc_count_; }

    // Statistics for debugging
    size_t vocab_size() const { return postings_.size(); }
    size_t total_postings() const {
        size_t total = 0;
        for (const auto& [_, list] : postings_) {
            total += list.size();
        }
        return total;
    }

private:
    // Refresh IDF cache - called lazily before search
    void refresh_idf_cache() const {
        idf_cache_.clear();
        for (const auto& [term, df] : doc_freqs_) {
            float df_f = static_cast<float>(df);
            // IDF with BM25 smoothing
            idf_cache_[term] = std::log((doc_count_ - df_f + 0.5f) / (df_f + 0.5f) + 1.0f);
        }
        idf_dirty_ = false;
    }

    BM25Config config_;
    size_t doc_count_ = 0;
    size_t total_length_ = 0;

    // Inverted index: term -> posting list (documents containing term)
    std::unordered_map<std::string, std::vector<Posting>> postings_;

    // Forward index: doc -> terms (needed for removal)
    std::unordered_map<NodeId, std::unordered_map<std::string, size_t>, NodeIdHash> doc_terms_;

    // Document lengths for BM25 normalization
    std::unordered_map<NodeId, size_t, NodeIdHash> doc_lengths_;

    // Document frequencies: term -> count of docs containing term
    std::unordered_map<std::string, size_t> doc_freqs_;

    // Cached IDF values (refreshed lazily)
    mutable std::unordered_map<std::string, float> idf_cache_;
    mutable bool idf_dirty_ = true;
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
