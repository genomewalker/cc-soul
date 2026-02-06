#pragma once
// Embedder: Yantra wrapper for text→embedding transformation
//
// Simple interface for embedding text using ONNX models.
// Includes LRU cache for query embeddings (~10x speedup on cache hits).
// Includes circuit breaker for graceful degradation when embedder fails.

#include "../vak.hpp"
#include "../types.hpp"
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <list>
#include <chrono>
#include <atomic>
#include <iostream>

namespace chitta {

// Circuit breaker configuration
struct CircuitBreakerConfig {
    size_t failure_threshold = 3;       // Trip after N consecutive failures
    std::chrono::seconds cooldown{60};  // Wait before retry (half-open state)
};

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
    size_t hits() const { return hits_.load(std::memory_order_relaxed); }
    size_t misses() const { return misses_.load(std::memory_order_relaxed); }
    void record_hit() { hits_.fetch_add(1, std::memory_order_relaxed); }
    void record_miss() { misses_.fetch_add(1, std::memory_order_relaxed); }

private:
    size_t capacity_;
    std::list<K> order_;
    std::unordered_map<K, std::pair<V, typename std::list<K>::iterator>> map_;
    mutable std::atomic<size_t> hits_{0};
    mutable std::atomic<size_t> misses_{0};
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

    // Circuit breaker configuration
    void configure_circuit_breaker(CircuitBreakerConfig cfg) {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        breaker_config_ = cfg;
    }

    // Check if circuit is open (embedder disabled)
    bool is_circuit_open() const {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        return circuit_open_;
    }

    // Record an embedding failure (timeout, error, etc.)
    void record_failure() {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        size_t failures = ++consecutive_failures_;
        if (failures >= breaker_config_.failure_threshold && !circuit_open_) {
            circuit_open_ = true;
            circuit_opened_at_ = std::chrono::steady_clock::now();
            std::cerr << "[Embedder] Circuit breaker OPEN after " << failures
                      << " failures - falling back to BM25\n";
        }
    }

    // Record successful embedding (resets failure count)
    void record_success() {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        consecutive_failures_ = 0;
        if (circuit_open_) {
            circuit_open_ = false;
            std::cerr << "[Embedder] Circuit breaker CLOSED - embeddings restored\n";
        }
    }

    // Force circuit breaker trip (used by watchdog)
    void force_trip() {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        circuit_open_ = true;
        circuit_opened_at_ = std::chrono::steady_clock::now();
        std::cerr << "[Embedder] Circuit breaker FORCED OPEN by watchdog\n";
    }

    // Check if we should skip embedding (circuit open and not in half-open state)
    bool should_skip_embedding() const {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        if (!circuit_open_) return false;

        // Check if cooldown has elapsed (half-open state - allow retry)
        auto elapsed = std::chrono::steady_clock::now() - circuit_opened_at_;
        if (elapsed >= breaker_config_.cooldown) {
            return false;  // Allow retry
        }
        return true;
    }

    // Embed a search query (adds instruction prefix for BGE models)
    Vector embed_query(const std::string& text) {
        if (should_skip_embedding()) return Vector::zeros();

        std::string cache_key = "__q:" + text;
        std::unique_lock lock(mutex_);
        if (auto cached = cache_.get(cache_key)) {
            cache_.record_hit();
            return *cached;
        }
        cache_.record_miss();
        if (!yantra_ || !yantra_->ready()) return Vector::zeros();

        auto artha = yantra_->transform(text, EmbedMode::Query);
        if (artha.nu.is_zero()) { record_failure(); return Vector::zeros(); }
        record_success();
        cache_.put(cache_key, artha.nu);
        return artha.nu;
    }

    // Embed query returning full Artha
    Artha transform_query(const std::string& text) {
        if (should_skip_embedding()) return Artha{Vector::zeros(), 0.0f, text};

        std::string cache_key = "__q:" + text;
        std::unique_lock lock(mutex_);
        if (auto cached = cache_.get(cache_key)) {
            cache_.record_hit();
            return Artha{*cached, 1.0f, text};
        }
        cache_.record_miss();
        if (!yantra_ || !yantra_->ready()) return Artha{};

        auto artha = yantra_->transform(text, EmbedMode::Query);
        if (artha.nu.is_zero()) { record_failure(); return artha; }
        record_success();
        cache_.put(cache_key, artha.nu);
        return artha;
    }

    // Embed single text (with caching and circuit breaker) — document mode
    Vector embed(const std::string& text) {
        // Check circuit breaker first
        if (should_skip_embedding()) {
            return Vector::zeros();
        }

        // Cache lookup requires unique_lock since get() mutates LRU order
        std::unique_lock lock(mutex_);

        if (auto cached = cache_.get(text)) {
            cache_.record_hit();
            return *cached;
        }

        cache_.record_miss();

        if (!yantra_ || !yantra_->ready()) {
            return Vector::zeros();
        }

        auto artha = yantra_->transform(text);

        // Check if embedding failed (zero vector indicates timeout/failure)
        if (artha.nu.is_zero()) {
            record_failure();
            return Vector::zeros();
        }

        record_success();
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

    // Full Artha (embedding + metadata) - with caching and circuit breaker
    Artha transform(const std::string& text) {
        // Check circuit breaker first
        if (should_skip_embedding()) {
            Artha artha;
            artha.nu = Vector::zeros();
            artha.source = text;
            return artha;
        }

        // Cache lookup requires unique_lock since get() mutates LRU order
        std::unique_lock lock(mutex_);

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

        // Check if embedding failed (zero vector indicates timeout/failure)
        if (artha.nu.is_zero()) {
            record_failure();
            return artha;
        }

        record_success();
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

    // Circuit breaker state (protected by cb_mutex_)
    mutable std::mutex cb_mutex_;
    CircuitBreakerConfig breaker_config_;
    bool circuit_open_{false};
    size_t consecutive_failures_{0};
    std::chrono::steady_clock::time_point circuit_opened_at_;
};

}  // namespace chitta
