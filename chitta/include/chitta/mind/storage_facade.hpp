#pragma once
// StorageFacade: Simplified storage using UnifiedIndex
//
// Single backend, no tiering complexity.
// WAL is handled internally by UnifiedIndex.

#include "../types.hpp"
#include "../unified_index.hpp"
#include <functional>
#include <mutex>
#include <unordered_map>
#include <iostream>
#include <cerrno>
#include <cstring>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace chitta {

class StorageFacade {
public:
    struct Config {
        std::string base_path;
        size_t initial_capacity = 100000;
    };

    explicit StorageFacade(Config config)
        : config_(std::move(config))
        , lock_fd_(-1) {}

    ~StorageFacade() {
        release_lock();
    }

    StorageFacade(const StorageFacade&) = delete;
    StorageFacade& operator=(const StorageFacade&) = delete;

    bool open() {
        if (!acquire_lock()) {
            return false;
        }

        std::string unified_path = config_.base_path + ".unified";
        struct stat st;
        bool exists = (stat(unified_path.c_str(), &st) == 0 && st.st_size > 0);

        if (exists) {
            if (!unified_.open(config_.base_path)) {
                std::cerr << "[Storage] Failed to open unified index\n";
                return false;
            }
        } else {
            if (!unified_.create(config_.base_path, config_.initial_capacity)) {
                std::cerr << "[Storage] Failed to create unified index\n";
                return false;
            }
        }

        return true;
    }

    void close() {
        sync();
        release_lock();
    }

    void sync() {
        unified_.sync();
    }

    const std::string& base_path() const { return config_.base_path; }

    // Insert
    bool insert(NodeId id, Node node) {
        auto slot = unified_.insert(id, node);
        if (slot.valid()) {
            cache_[id] = std::move(node);
        }
        return slot.valid();
    }

    size_t insert_batch(std::vector<Node> nodes) {
        if (nodes.empty()) return 0;

        size_t inserted = 0;
        for (auto& node : nodes) {
            if (unified_.insert(node.id, node).valid()) {
                inserted++;
            }
        }
        return inserted;
    }

    // Get - returns pointer to cached node
    Node* get(NodeId id) {
        // Check cache first
        auto it = cache_.find(id);
        if (it != cache_.end()) {
            return &it->second;
        }

        // Load from unified index
        auto slot = unified_.lookup(id);
        if (!slot.valid()) return nullptr;

        // Reconstruct node from components
        auto* meta = unified_.meta(slot);
        auto* qvec = unified_.vector(slot);
        if (!meta || !qvec) return nullptr;

        Node node;
        node.id = meta->id;
        node.node_type = meta->node_type;
        node.nu = qvec->to_float();
        node.tau_created = meta->tau_created;
        node.tau_accessed = meta->tau_accessed;
        node.delta = meta->decay_rate;
        node.kappa.mu = meta->confidence_mu;
        node.kappa.sigma_sq = meta->confidence_sigma;

        // Load payload
        node.payload = unified_.payload(slot);

        // Load tags
        node.tags = unified_.slot_tag_index().tags_for_slot(slot.value);

        cache_[id] = std::move(node);
        return &cache_[id];
    }

    bool contains(NodeId id) const {
        return unified_.lookup(id).valid();
    }

    // Update
    bool update(NodeId id, const Node& node) {
        bool result = unified_.update(id, node);
        if (result) {
            cache_[id] = node;
        }
        return result;
    }

    bool update_confidence(NodeId id, const Confidence& kappa) {
        auto slot = unified_.lookup(id);
        if (!slot.valid()) return false;

        bool result = unified_.update_confidence(slot, kappa);
        if (result) {
            auto it = cache_.find(id);
            if (it != cache_.end()) {
                it->second.kappa = kappa;
            }
        }
        return result;
    }

    // Remove
    bool remove(NodeId id) {
        unified_.remove(id);
        cache_.erase(id);
        return true;
    }

    // Search
    std::vector<std::pair<NodeId, float>> search(const QuantizedVector& query, size_t k) const {
        auto slot_results = unified_.search(query, k);
        std::vector<std::pair<NodeId, float>> results;
        results.reserve(slot_results.size());

        for (const auto& [slot, score] : slot_results) {
            auto* indexed = unified_.get_slot(slot);
            if (indexed) {
                results.emplace_back(indexed->id, 1.0f - score);
            }
        }
        return results;
    }

    // Tags
    std::vector<NodeId> find_by_tag(const std::string& tag) const {
        auto slots = unified_.slot_tag_index().slots_with_tag(tag);
        return slots_to_ids(slots);
    }

    std::vector<NodeId> find_by_tags(const std::vector<std::string>& tags) const {
        auto slots = unified_.slot_tag_index().slots_with_all_tags(tags);
        return slots_to_ids(slots);
    }

    std::vector<std::string> tags_for_node(NodeId id) const {
        auto slot = unified_.lookup(id);
        if (!slot.valid()) return {};
        return unified_.slot_tag_index().tags_for_slot(slot.value);
    }

    // Iteration
    template<typename Fn>
    void for_each(Fn&& fn) const {
        unified_.for_each([&fn](const NodeId& id, const Node& node) {
            fn(id, node);
        });
    }

    // Stats
    size_t size() const { return unified_.count(); }

    // Direct access for advanced operations
    UnifiedIndex& unified() { return unified_; }
    const UnifiedIndex& unified() const { return unified_; }

private:
    Config config_;
    UnifiedIndex unified_;
    int lock_fd_;
    mutable std::unordered_map<NodeId, Node, NodeIdHash> cache_;

    std::vector<NodeId> slots_to_ids(const std::vector<uint32_t>& slots) const {
        std::vector<NodeId> result;
        result.reserve(slots.size());
        for (uint32_t slot : slots) {
            auto* indexed = unified_.get_slot(SlotId(slot));
            if (indexed) {
                result.push_back(indexed->id);
            }
        }
        return result;
    }

    bool acquire_lock() {
        std::string lock_path = config_.base_path + ".lock";
        lock_fd_ = ::open(lock_path.c_str(), O_CREAT | O_RDWR, 0644);
        if (lock_fd_ < 0) {
            std::cerr << "[Storage] Cannot open lock file: " << strerror(errno) << "\n";
            return false;
        }

        if (flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) {
            std::cerr << "[Storage] Database locked by another process\n";
            ::close(lock_fd_);
            lock_fd_ = -1;
            return false;
        }

        return true;
    }

    void release_lock() {
        if (lock_fd_ >= 0) {
            flock(lock_fd_, LOCK_UN);
            ::close(lock_fd_);
            lock_fd_ = -1;
        }
    }
};

}  // namespace chitta
