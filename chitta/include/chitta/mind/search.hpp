#pragma once
// Search: Dense vector search with tag filtering
//
// Simple interface for semantic recall with relevance scoring.

#include "../types.hpp"
#include "types.hpp"  // For Recall struct
#include "../scoring.hpp"
#include "../quantized.hpp"
#include "storage_facade.hpp"
#include "embedder.hpp"
#include "payload.hpp"
#include <algorithm>
#include <string>
#include <vector>
#include <unordered_set>

namespace chitta {

class Search {
public:
    Search(StorageFacade& storage, Embedder& embedder)
        : storage_(storage), embedder_(embedder) {}

    // Configure scoring
    void set_scoring_config(const ScoringConfig& config) {
        scoring_config_ = config;
    }

    // Recall by text query
    std::vector<Recall> recall(const std::string& query, size_t limit = 10, float threshold = 0.0f) {
        if (!embedder_.ready()) {
            return {};
        }

        Vector query_vec = embedder_.embed(query);
        return recall(query_vec, limit, threshold);
    }

    // Recall by vector
    std::vector<Recall> recall(const Vector& query_vec, size_t limit = 10, float threshold = 0.0f) {
        if (query_vec.is_zero()) {
            return {};
        }

        QuantizedVector qvec = QuantizedVector::from_float(query_vec);
        auto raw_results = storage_.search(qvec, limit * 2);  // Overfetch for filtering

        Timestamp current = now();
        std::vector<Recall> results;
        results.reserve(raw_results.size());

        for (const auto& [id, similarity] : raw_results) {
            if (similarity < threshold) continue;

            Node* node = storage_.get(id);
            if (!node) continue;

            auto text = payload_to_text(node->payload);
            if (!text) continue;

            float relevance = soul_relevance(similarity, *node, current, scoring_config_);

            Recall r;
            r.id = id;
            r.similarity = similarity;
            r.relevance = relevance;
            r.type = node->node_type;
            r.confidence = node->kappa;
            r.created = node->tau_created;
            r.accessed = node->tau_accessed;
            r.payload = node->payload;
            r.text = *text;
            results.push_back(std::move(r));
        }

        // Sort by relevance
        std::sort(results.begin(), results.end(),
            [](const Recall& a, const Recall& b) {
                return a.relevance > b.relevance;
            });

        if (results.size() > limit) {
            results.resize(limit);
        }

        return results;
    }

    // Recall by tag
    std::vector<Recall> recall_by_tag(const std::string& tag, size_t limit = 10) {
        auto node_ids = storage_.find_by_tag(tag);
        return build_recalls(node_ids, limit);
    }

    // Recall by multiple tags (AND)
    std::vector<Recall> recall_by_tags(const std::vector<std::string>& tags, size_t limit = 10) {
        auto node_ids = storage_.find_by_tags(tags);
        return build_recalls(node_ids, limit);
    }

    // Recall with tag filter (semantic + tag intersection)
    std::vector<Recall> recall_with_tag_filter(
        const std::string& query,
        const std::string& tag,
        size_t limit = 10)
    {
        if (!embedder_.ready()) {
            return {};
        }

        Vector query_vec = embedder_.embed(query);
        QuantizedVector qvec = QuantizedVector::from_float(query_vec);

        // Get tag-filtered node IDs
        auto tag_ids = storage_.find_by_tag(tag);
        if (tag_ids.empty()) {
            return {};
        }

        // Search semantically
        auto raw_results = storage_.search(qvec, limit * 5);

        // Intersect with tag filter
        std::unordered_set<NodeId, NodeIdHash> tag_set(tag_ids.begin(), tag_ids.end());

        Timestamp current = now();
        std::vector<Recall> results;

        for (const auto& [id, similarity] : raw_results) {
            if (tag_set.find(id) == tag_set.end()) continue;

            Node* node = storage_.get(id);
            if (!node) continue;

            auto text = payload_to_text(node->payload);
            if (!text) continue;

            float relevance = soul_relevance(similarity, *node, current, scoring_config_);

            Recall r;
            r.id = id;
            r.similarity = similarity;
            r.relevance = relevance;
            r.type = node->node_type;
            r.confidence = node->kappa;
            r.created = node->tau_created;
            r.accessed = node->tau_accessed;
            r.payload = node->payload;
            r.text = *text;
            results.push_back(std::move(r));
        }

        std::sort(results.begin(), results.end(),
            [](const Recall& a, const Recall& b) {
                return a.relevance > b.relevance;
            });

        if (results.size() > limit) {
            results.resize(limit);
        }

        return results;
    }

    // Get tags for a node
    std::vector<std::string> get_tags(NodeId id) const {
        return storage_.tags_for_node(id);
    }

private:
    StorageFacade& storage_;
    Embedder& embedder_;
    ScoringConfig scoring_config_;

    std::vector<Recall> build_recalls(const std::vector<NodeId>& node_ids, size_t limit) {
        Timestamp current = now();
        std::vector<Recall> results;
        results.reserve(std::min(node_ids.size(), limit));

        for (const auto& id : node_ids) {
            if (results.size() >= limit) break;

            Node* node = storage_.get(id);
            if (!node) continue;

            auto text = payload_to_text(node->payload);
            if (!text) continue;

            Recall r;
            r.id = id;
            r.similarity = 1.0f;  // Tag match = perfect similarity
            r.relevance = node->kappa.effective();  // Use confidence as relevance
            r.type = node->node_type;
            r.confidence = node->kappa;
            r.created = node->tau_created;
            r.accessed = node->tau_accessed;
            r.payload = node->payload;
            r.text = *text;
            results.push_back(std::move(r));
        }

        // Sort by relevance (confidence)
        std::sort(results.begin(), results.end(),
            [](const Recall& a, const Recall& b) {
                return a.relevance > b.relevance;
            });

        return results;
    }
};

}  // namespace chitta
