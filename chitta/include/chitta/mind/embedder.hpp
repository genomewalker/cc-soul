#pragma once
// Embedder: Yantra wrapper for text→embedding transformation
//
// Simple interface for embedding text using ONNX models.
// Includes LRU cache for query embeddings (~10x speedup on cache hits).

#include "../vak.hpp"
#include "../types.hpp"
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <list>

namespace chitta {

// Simple LRU cache for embeddings
template<typename K, typename V>
class LRUCache {
public:
    explicit LRUCache(size_t capacity) : capacity_(capacity) {}

    std::optional<V> get(const K& key) {
        auto it = map_.find(key);
        if (it == map_.end()) return std::nullopt;

        // Move to front (most recently used)
        order_.splice(order_.begin(), order_, it->second.second);
        return it->second.first;
    }

    void put(const K& key, const V& value) {
        auto it = map_.find(key);
        if (it != map_.end()) {
            // Update existing
            it->second.first = value;
            order_.splice(order_.begin(), order_, it->second.second);
            return;
        }

        // Evict if at capacity
        if (map_.size() >= capacity_) {
            auto last = order_.back();
            order_.pop_back();
            map_.erase(last);
        }

        // Insert new
        order_.push_front(key);
        map_[key] = {value, order_.begin()};
    }

    size_t size() const { return map_.size(); }
    void clear() { map_.clear(); order_.clear(); }

    // Stats
    size_t hits() const { return hits_; }
    size_t misses() const { return misses_; }
    void record_hit() { ++hits_; }
    void record_miss() { ++misses_; }

private:
    size_t capacity_;
    std::list<K> order_;
    std::unordered_map<K, std::pair<V, typename std::list<K>::iterator>> map_;
    mutable size_t hits_ = 0;
    mutable size_t misses_ = 0;
};

class Embedder {
public:
    Embedder() = default;

    void attach(std::shared_ptr<VakYantra> yantra) {
        std::unique_lock lock(mutex_);
        yantra_ = std::move(yantra);
    }

    bool ready() const {
        std::shared_lock lock(mutex_);
        return yantra_ && yantra_->ready();
    }

    // Embed single text (with caching)
    Vector embed(const std::string& text) {
        // Check cache first
        {
            std::shared_lock lock(mutex_);
            if (auto cached = cache_.get(text)) {
                cache_.record_hit();
                return *cached;
            }
        }

        // Cache miss - compute embedding
        std::unique_lock lock(mutex_);

        // Double-check after acquiring write lock
        if (auto cached = cache_.get(text)) {
            cache_.record_hit();
            return *cached;
        }

        cache_.record_miss();

        if (!yantra_ || !yantra_->ready()) {
            return Vector::zeros();
        }

        auto artha = yantra_->transform(text);
        cache_.put(text, artha.nu);
        return artha.nu;
    }

    // Embed batch of texts
    std::vector<Vector> embed_batch(const std::vector<std::string>& texts) {
        std::shared_lock lock(mutex_);
        if (!yantra_ || !yantra_->ready()) {
            std::vector<Vector> zeros(texts.size());
            for (auto& v : zeros) v = Vector::zeros();
            return zeros;
        }

        auto arthas = yantra_->transform_batch(texts);
        std::vector<Vector> results;
        results.reserve(arthas.size());
        for (const auto& artha : arthas) {
            results.push_back(artha.nu);
        }
        return results;
    }

    // Full Artha (embedding + metadata) - with caching
    Artha transform(const std::string& text) {
        // Check cache first
        {
            std::shared_lock lock(mutex_);
            if (auto cached = cache_.get(text)) {
                cache_.record_hit();
                Artha artha;
                artha.nu = *cached;
                artha.source = text;
                return artha;
            }
        }

        // Cache miss - compute embedding
        std::unique_lock lock(mutex_);

        // Double-check after acquiring write lock
        if (auto cached = cache_.get(text)) {
            cache_.record_hit();
            Artha artha;
            artha.nu = *cached;
            artha.source = text;
            return artha;
        }

        cache_.record_miss();

        if (!yantra_ || !yantra_->ready()) {
            return Artha{};
        }

        auto artha = yantra_->transform(text);
        cache_.put(text, artha.nu);
        return artha;
    }

    std::vector<Artha> transform_batch(const std::vector<std::string>& texts) {
        std::shared_lock lock(mutex_);
        if (!yantra_ || !yantra_->ready()) {
            return std::vector<Artha>(texts.size());
        }
        return yantra_->transform_batch(texts);
    }

    // Access underlying yantra for advanced operations
    std::shared_ptr<VakYantra> yantra() const {
        std::shared_lock lock(mutex_);
        return yantra_;
    }

    // Cache statistics
    size_t cache_size() const {
        std::shared_lock lock(mutex_);
        return cache_.size();
    }

    size_t cache_hits() const {
        std::shared_lock lock(mutex_);
        return cache_.hits();
    }

    size_t cache_misses() const {
        std::shared_lock lock(mutex_);
        return cache_.misses();
    }

    void clear_cache() {
        std::unique_lock lock(mutex_);
        cache_.clear();
    }

private:
    mutable std::shared_mutex mutex_;
    std::shared_ptr<VakYantra> yantra_;
    mutable LRUCache<std::string, Vector> cache_{1000};  // Default 1000 entries
};

}  // namespace chitta
