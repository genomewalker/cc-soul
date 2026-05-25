#pragma once
// EmbedQueue — single-owner async embed queue for LlamaYantra.
//
// Architecture (from GPT-5.5 + Opus room, 2026-05-25):
//   - One worker thread exclusively owns LlamaYantra (not thread-safe)
//   - Two lanes: READ jobs (await ≤2s, fall to BM25 on miss) and WRITE jobs
//     (fire-and-forget, warm cache for future reads)
//   - LRU cache keyed by text hash; checked synchronously before submit
//   - Inflight coalescing: identical texts share one future
//   - Deadline-skip for READ jobs only; WRITE jobs always run to warm cache

#include "vak.hpp"
#include <functional>
#include <future>
#include <queue>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <unordered_map>
#include <list>
#include <optional>
#include <chrono>
#include <atomic>

namespace chitta {

class EmbedQueue {
public:
    using Vec = std::vector<float>;
    using Clock = std::chrono::steady_clock;

    static constexpr size_t kReadQueueCap  = 8;
    static constexpr size_t kWriteQueueCap = 64;
    static constexpr size_t kCacheSize     = 512;

    explicit EmbedQueue(std::shared_ptr<VakYantra> inner)
        : inner_(std::move(inner)) {
        worker_ = std::thread([this]{ run(); });
    }

    ~EmbedQueue() {
        { std::lock_guard lk(mu_); stop_ = true; }
        cv_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

    // Synchronous cache lookup + async submit for READ path.
    // Returns empty vector if the slot is full or deadline passes.
    Vec query(const std::string& text, std::chrono::milliseconds wait = std::chrono::milliseconds(2000)) {
        auto key = hash(text);

        // 1. Cache hit (synchronous, ~0ms)
        if (auto v = cache_get(key)) return *v;

        // 2. Coalesce with in-flight job if one exists
        std::shared_future<Vec> fut;
        {
            std::lock_guard lk(mu_);
            auto it = inflight_.find(key);
            if (it != inflight_.end()) {
                fut = it->second;
            } else {
                if (read_q_.size() >= kReadQueueCap) return {};
                auto job = std::make_shared<Job>(text, key, Clock::now() + wait, /*write=*/false);
                fut = job->prom.get_future().share();
                inflight_[key] = fut;
                read_q_.push_back(std::move(job));
                cv_.notify_one();
            }
        }

        auto status = fut.wait_for(wait);
        if (status != std::future_status::ready) return {};
        return fut.get();
    }

    // Fire-and-forget enqueue for WRITE path (backfill, learn_codebase, etc.)
    void enqueue_write(const std::string& text) {
        auto key = hash(text);
        if (cache_get(key)) return; // already cached
        std::lock_guard lk(mu_);
        if (inflight_.count(key)) return; // already in-flight
        if (write_q_.size() >= kWriteQueueCap) return;
        auto job = std::make_shared<Job>(text, key, Clock::time_point::max(), /*write=*/true);
        write_q_.push_back(std::move(job));
        cv_.notify_one();
    }

    size_t cache_size() const { std::lock_guard lk(cache_mu_); return cache_map_.size(); }
    size_t inflight_count() { std::lock_guard lk(mu_); return inflight_.size(); }

private:
    struct Job {
        std::string text;
        size_t key;
        Clock::time_point deadline;
        bool write;
        std::promise<Vec> prom;
        Job(std::string t, size_t k, Clock::time_point d, bool w)
            : text(std::move(t)), key(k), deadline(d), write(w) {}
    };

    void run() {
        while (true) {
            std::shared_ptr<Job> job;
            {
                std::unique_lock lk(mu_);
                cv_.wait(lk, [&]{ return stop_ || !read_q_.empty() || !write_q_.empty(); });
                if (stop_ && read_q_.empty() && write_q_.empty()) return;
                // Drain reads first (latency-sensitive)
                if (!read_q_.empty()) { job = read_q_.front(); read_q_.pop_front(); }
                else                  { job = write_q_.front(); write_q_.pop_front(); }
                inflight_.erase(job->key);
            }

            // Skip stale READ jobs; WRITE jobs always run (cache warming)
            if (!job->write && Clock::now() > job->deadline) {
                job->prom.set_value({});
                continue;
            }

            Vec vec;
            try {
                Artha a = inner_->transform(job->text);
                vec = (a.certainty > 0.0f) ? a.nu.data : Vec{};
            } catch (...) { vec = {}; }

            if (!vec.empty()) cache_put(job->key, vec);
            job->prom.set_value(std::move(vec));
        }
    }

    // LRU cache (mutex-separate from job queue for read concurrency)
    std::optional<Vec> cache_get(size_t key) const {
        std::lock_guard lk(cache_mu_);
        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) return std::nullopt;
        cache_lru_.splice(cache_lru_.begin(), cache_lru_, it->second.lru_it);
        return it->second.vec;
    }

    void cache_put(size_t key, Vec vec) {
        std::lock_guard lk(cache_mu_);
        if (cache_map_.size() >= kCacheSize) {
            cache_map_.erase(cache_lru_.back());
            cache_lru_.pop_back();
        }
        auto lit = cache_lru_.insert(cache_lru_.begin(), key);
        cache_map_[key] = {std::move(vec), lit};
    }

    static size_t hash(const std::string& s) {
        return std::hash<std::string>{}(s);
    }

    std::shared_ptr<VakYantra> inner_;
    std::thread worker_;

    mutable std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::deque<std::shared_ptr<Job>> read_q_;
    std::deque<std::shared_ptr<Job>> write_q_;
    std::unordered_map<size_t, std::shared_future<Vec>> inflight_;

    mutable std::mutex cache_mu_;
    struct CacheEntry { Vec vec; std::list<size_t>::iterator lru_it; };
    mutable std::unordered_map<size_t, CacheEntry> cache_map_;
    mutable std::list<size_t> cache_lru_;
};

} // namespace chitta
