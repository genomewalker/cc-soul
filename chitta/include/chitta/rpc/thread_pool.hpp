#pragma once
// Thread Pool: Async RPC request handling for daemon
//
// Allows main thread to stay responsive (poll/accept/write) while
// slow operations (semantic search, embedding) run on worker threads.
//
// Features:
// - Configurable worker count
// - Request tracing with timing
// - Watchdog for slow request detection
// - Thread-safe response queue integration

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <vector>
#include <string>
#include <iostream>

namespace chitta {

struct RequestTrace {
    uint64_t id;
    std::string method;
    std::chrono::steady_clock::time_point start;
};

class ThreadPool {
public:
    explicit ThreadPool(size_t min_threads = 2, size_t max_threads = 16)
        : stop_(false), min_workers_(min_threads), max_workers_(max_threads) {
        for (size_t i = 0; i < min_threads; ++i) {
            spawn_worker();
        }
        watchdog_ = std::thread([this] { watchdog_loop(); });
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        watchdog_cv_.notify_all();

        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
        if (watchdog_.joinable()) watchdog_.join();
    }

    // Non-copyable, non-movable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // Submit work, returns request ID for tracking
    uint64_t submit(int client_fd, const std::string& method,
                    std::function<std::string()> task,
                    std::function<void(int, std::string)> on_complete) {
        uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(trace_mutex_);
            active_[id] = {id, method, std::chrono::steady_clock::now()};
        }

        size_t queue_size;
        size_t worker_count;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            tasks_.push({id, client_fd, std::move(task), std::move(on_complete)});
            queue_size = tasks_.size();
            worker_count = num_workers_.load();
        }
        condition_.notify_one();

        // Scale up if queue is growing (more pending than workers)
        if (queue_size > worker_count && worker_count < max_workers_) {
            spawn_worker();
        }

        return id;
    }

    // Get active requests (for diagnostics)
    std::vector<std::pair<std::string, int64_t>> get_active() const {
        std::lock_guard<std::mutex> lock(trace_mutex_);
        std::vector<std::pair<std::string, int64_t>> result;
        auto now = std::chrono::steady_clock::now();
        for (const auto& [id, trace] : active_) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - trace.start).count();
            result.push_back({trace.method, ms});
        }
        return result;
    }

    // Stats
    size_t pending() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return tasks_.size();
    }

    size_t active_count() const {
        std::lock_guard<std::mutex> lock(trace_mutex_);
        return active_.size();
    }

    size_t worker_count() const {
        return num_workers_.load();
    }

    size_t max_workers() const {
        return max_workers_;
    }

private:
    void spawn_worker() {
        std::lock_guard<std::mutex> lock(workers_mutex_);
        if (num_workers_.load() >= max_workers_) return;

        workers_.emplace_back([this] { worker_loop(); });
        num_workers_.fetch_add(1);
    }
    struct Task {
        uint64_t id;
        int client_fd;
        std::function<std::string()> work;
        std::function<void(int, std::string)> on_complete;
    };

    void worker_loop() {
        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                condition_.wait(lock, [this] {
                    return stop_ || !tasks_.empty();
                });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }

            // Execute work
            std::string result;
            try {
                result = task.work();
            } catch (const std::exception& e) {
                result = R"({"jsonrpc":"2.0","error":{"code":-32603,"message":")" +
                         std::string(e.what()) + R"("},"id":null})";
            }

            // Mark completed and remove from active
            {
                std::lock_guard<std::mutex> lock(trace_mutex_);
                active_.erase(task.id);
            }

            // Deliver result
            if (task.on_complete) {
                task.on_complete(task.client_fd, std::move(result));
            }
        }
    }

    void watchdog_loop() {
        while (true) {
            {
                std::unique_lock<std::mutex> lock(watchdog_mutex_);
                watchdog_cv_.wait_for(lock, std::chrono::seconds(5), [this] {
                    return stop_.load();
                });
                if (stop_) return;
            }

            std::lock_guard<std::mutex> lock(trace_mutex_);
            auto now = std::chrono::steady_clock::now();

            for (const auto& [id, trace] : active_) {
                auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                    now - trace.start).count();
                if (secs > 10) {
                    std::cerr << "[watchdog] SLOW: " << trace.method
                              << " running for " << secs << "s\n";
                }
            }
        }
    }

    std::vector<std::thread> workers_;
    std::thread watchdog_;
    std::queue<Task> tasks_;
    mutable std::mutex queue_mutex_;
    mutable std::mutex workers_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;
    std::atomic<uint64_t> next_id_{1};
    std::atomic<size_t> num_workers_{0};
    size_t min_workers_;
    size_t max_workers_;

    mutable std::mutex trace_mutex_;
    std::unordered_map<uint64_t, RequestTrace> active_;

    std::mutex watchdog_mutex_;
    std::condition_variable watchdog_cv_;
};

} // namespace chitta
