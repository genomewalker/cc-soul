#pragma once
// Mind TagIndex: lightweight in-memory tag index
//
// Extracted from mind.hpp for modularity.
// Simple tag-to-node mapping for exact-match filtering.
// Note: This is separate from the roaring-based SlotTagIndex in tag_index.hpp

#include "../types.hpp"
#include <vector>
#include <string>
#include <set>
#include <unordered_map>
#include <algorithm>

namespace chitta {

// Tag index for exact-match filtering
// Enables reliable inter-agent communication via thread tags
class TagIndex {
public:
    // Add node with tags
    void add(NodeId id, const std::vector<std::string>& tags) {
        for (const auto& tag : tags) {
            index_[tag].insert(id);
        }
        node_tags_[id] = tags;
    }

    // Remove node from index
    void remove(NodeId id) {
        auto it = node_tags_.find(id);
        if (it != node_tags_.end()) {
            for (const auto& tag : it->second) {
                auto idx_it = index_.find(tag);
                if (idx_it != index_.end()) {
                    idx_it->second.erase(id);
                    if (idx_it->second.empty()) {
                        index_.erase(idx_it);
                    }
                }
            }
            node_tags_.erase(it);
        }
    }

    // Find all nodes with a specific tag
    std::vector<NodeId> find(const std::string& tag) const {
        auto it = index_.find(tag);
        if (it != index_.end()) {
            return std::vector<NodeId>(it->second.begin(), it->second.end());
        }
        return {};
    }

    // Find nodes matching ALL given tags (AND)
    std::vector<NodeId> find_all(const std::vector<std::string>& tags) const {
        if (tags.empty()) return {};

        std::set<NodeId> result;
        bool first = true;

        for (const auto& tag : tags) {
            auto it = index_.find(tag);
            if (it == index_.end()) {
                return {};  // Tag not found, no matches
            }

            if (first) {
                result = it->second;
                first = false;
            } else {
                std::set<NodeId> intersection;
                std::set_intersection(
                    result.begin(), result.end(),
                    it->second.begin(), it->second.end(),
                    std::inserter(intersection, intersection.begin())
                );
                result = std::move(intersection);
            }
        }

        return std::vector<NodeId>(result.begin(), result.end());
    }

    // Find nodes matching ANY of the given tags (OR)
    std::vector<NodeId> find_any(const std::vector<std::string>& tags) const {
        std::set<NodeId> result;
        for (const auto& tag : tags) {
            auto it = index_.find(tag);
            if (it != index_.end()) {
                result.insert(it->second.begin(), it->second.end());
            }
        }
        return std::vector<NodeId>(result.begin(), result.end());
    }

    // Get tags for a node
    std::vector<std::string> tags_for(NodeId id) const {
        auto it = node_tags_.find(id);
        return it != node_tags_.end() ? it->second : std::vector<std::string>{};
    }

    // Get all unique tags
    std::vector<std::string> all_tags() const {
        std::vector<std::string> result;
        result.reserve(index_.size());
        for (const auto& [tag, _] : index_) {
            result.push_back(tag);
        }
        return result;
    }

    // Stats
    size_t tag_count() const { return index_.size(); }
    size_t node_count() const { return node_tags_.size(); }

private:
    std::unordered_map<std::string, std::set<NodeId>> index_;
    std::unordered_map<NodeId, std::vector<std::string>, NodeIdHash> node_tags_;
};

} // namespace chitta
