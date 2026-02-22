#pragma once
// TimeoutYantra: Wraps any VakYantra with async timeout protection
//
// If transform() takes longer than timeout, returns zero embedding
// and records the failure (for circuit breaker integration).
//
// Uses a bounded worker pool to prevent unlimited thread spawning.
// Max concurrent operations is controlled by max_concurrent_ (default: 4).

#include "vak.hpp"
#include <future>
#include <chrono>
#include <atomic>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <semaphore>

namespace chitta {

class TimeoutYantra : public VakYantra {
public:
    TimeoutYantra(std::shared_ptr<VakYantra> inner,
                  std::chrono::milliseconds timeout = std::chrono::milliseconds(5000),
                  size_t max_concurrent = 4)
        : inner_(std::move(inner)), timeout_(timeout), max_concurrent_(max_concurrent),
          concurrent_semaphore_(max_concurrent) {}

    Artha transform(const std::string& vak) override {
        // Try to acquire a slot (non-blocking) to prevent unbounded thread spawning
        if (!concurrent_semaphore_.try_acquire()) {
            // All slots busy - fail fast rather than queue indefinitely
            rejected_++;
            if (rejected_ == 1 || rejected_ % 100 == 0) {
                std::cerr << "[TimeoutYantra] transform() rejected: all " << max_concurrent_
                          << " slots busy (total rejected: " << rejected_ << ")\n";
            }
            Artha rejected_result;
            rejected_result.nu = Vector::zeros();
            rejected_result.certainty = 0.0f;
            rejected_result.source = vak;
            return rejected_result;
        }

        // Use shared state for result
        auto result_ptr = std::make_shared<std::optional<Artha>>();
        auto done = std::make_shared<std::atomic<bool>>(false);
        auto mtx = std::make_shared<std::mutex>();
        auto cv = std::make_shared<std::condition_variable>();

        // Copy vak for thread safety
        std::string vak_copy = vak;

        // Capture inner_ by value (shared_ptr copy) to prevent use-after-free
        // if TimeoutYantra is destroyed while the thread is still running
        auto inner_copy = inner_;

        // Capture semaphore pointer for release
        auto sem_ptr = &concurrent_semaphore_;

        // Launch worker thread
        std::thread worker([inner_copy, vak_copy, result_ptr, done, mtx, cv, sem_ptr]() {
            try {
                Artha result = inner_copy->transform(vak_copy);
                {
                    std::lock_guard<std::mutex> lock(*mtx);
                    *result_ptr = std::move(result);
                    *done = true;
                }
                cv->notify_one();
            } catch (...) {
                std::lock_guard<std::mutex> lock(*mtx);
                *done = true;
                cv->notify_one();
            }
            sem_ptr->release();  // Release slot when done
        });
        worker.detach();

        // Wait with timeout
        {
            std::unique_lock<std::mutex> lock(*mtx);
            if (cv->wait_for(lock, timeout_, [done] { return done->load(); })) {
                // Completed in time
                if (*result_ptr) {
                    consecutive_failures_ = 0;
                    return std::move(**result_ptr);
                }
            }
        }

        // Timeout or error (semaphore will be released by worker thread when it finishes)
        consecutive_failures_++;
        total_timeouts_++;

        if (total_timeouts_ == 1 || total_timeouts_ % 10 == 0) {
            std::cerr << "[TimeoutYantra] transform() timeout after "
                      << timeout_.count() << "ms (total: " << total_timeouts_ << ")\n";
        }

        Artha timeout_result;
        timeout_result.nu = Vector::zeros();
        timeout_result.certainty = 0.0f;
        timeout_result.source = vak_copy;
        return timeout_result;
    }

    std::vector<Artha> transform_batch(const std::vector<std::string>& vaks) override {
        if (vaks.empty()) return {};

        // Try to acquire a slot (non-blocking) to prevent unbounded thread spawning
        if (!concurrent_semaphore_.try_acquire()) {
            // All slots busy - fail fast rather than queue indefinitely
            rejected_++;
            if (rejected_ == 1 || rejected_ % 100 == 0) {
                std::cerr << "[TimeoutYantra] transform_batch() rejected: all " << max_concurrent_
                          << " slots busy (total rejected: " << rejected_ << ")\n";
            }
            std::vector<Artha> results;
            results.reserve(vaks.size());
            for (const auto& v : vaks) {
                Artha a;
                a.nu = Vector::zeros();
                a.source = v;
                results.push_back(std::move(a));
            }
            return results;
        }

        // Use shared state for result
        auto result_ptr = std::make_shared<std::optional<std::vector<Artha>>>();
        auto done = std::make_shared<std::atomic<bool>>(false);
        auto mtx = std::make_shared<std::mutex>();
        auto cv = std::make_shared<std::condition_variable>();

        // Copy for thread safety
        std::vector<std::string> vaks_copy = vaks;
        auto batch_timeout = timeout_ + std::chrono::milliseconds(1000 * vaks_copy.size());

        // Capture inner_ by value (shared_ptr copy) to prevent use-after-free
        auto inner_copy = inner_;

        // Capture semaphore pointer for release
        auto sem_ptr = &concurrent_semaphore_;

        // Launch worker thread
        std::thread worker([inner_copy, vaks_copy, result_ptr, done, mtx, cv, sem_ptr]() {
            try {
                auto result = inner_copy->transform_batch(vaks_copy);
                {
                    std::lock_guard<std::mutex> lock(*mtx);
                    *result_ptr = std::move(result);
                    *done = true;
                }
                cv->notify_one();
            } catch (...) {
                std::lock_guard<std::mutex> lock(*mtx);
                *done = true;
                cv->notify_one();
            }
            sem_ptr->release();  // Release slot when done
        });
        worker.detach();

        // Wait with timeout
        {
            std::unique_lock<std::mutex> lock(*mtx);
            if (cv->wait_for(lock, batch_timeout, [done] { return done->load(); })) {
                if (*result_ptr) {
                    consecutive_failures_ = 0;
                    return std::move(**result_ptr);
                }
            }
        }

        // Timeout or error (semaphore will be released by worker thread when it finishes)
        consecutive_failures_++;
        total_timeouts_++;

        std::cerr << "[TimeoutYantra] transform_batch(" << vaks_copy.size()
                  << ") timeout after " << batch_timeout.count() << "ms\n";

        // Return zero embeddings
        std::vector<Artha> results;
        results.reserve(vaks_copy.size());
        for (const auto& v : vaks_copy) {
            Artha a;
            a.nu = Vector::zeros();
            a.source = v;
            results.push_back(std::move(a));
        }
        return results;
    }

    size_t dimension() const override { return inner_->dimension(); }
    bool ready() const override { return inner_->ready(); }
    std::string execution_provider_name() const override { return inner_->execution_provider_name(); }

    // For circuit breaker integration
    size_t consecutive_failures() const { return consecutive_failures_.load(); }
    size_t total_timeouts() const { return total_timeouts_.load(); }
    size_t total_rejected() const { return rejected_.load(); }
    void reset_failures() { consecutive_failures_ = 0; }

    void set_timeout(std::chrono::milliseconds t) { timeout_ = t; }
    std::chrono::milliseconds timeout() const { return timeout_; }
    size_t max_concurrent() const { return max_concurrent_; }

private:
    std::shared_ptr<VakYantra> inner_;
    std::chrono::milliseconds timeout_;
    size_t max_concurrent_;
    mutable std::counting_semaphore<64> concurrent_semaphore_;  // Max 64 concurrent (bounded by template)
    std::atomic<size_t> consecutive_failures_{0};
    std::atomic<size_t> total_timeouts_{0};
    std::atomic<size_t> rejected_{0};
};

} // namespace chitta
