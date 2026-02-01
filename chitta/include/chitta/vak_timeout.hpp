#pragma once
// TimeoutYantra: Wraps any VakYantra with async timeout protection
//
// If transform() takes longer than timeout, returns zero embedding
// and records the failure (for circuit breaker integration).
//
// Uses std::async with deferred policy and wait_for() for true timeout.

#include "vak.hpp"
#include <future>
#include <chrono>
#include <atomic>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <optional>

namespace chitta {

class TimeoutYantra : public VakYantra {
public:
    TimeoutYantra(std::shared_ptr<VakYantra> inner,
                  std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
        : inner_(std::move(inner)), timeout_(timeout) {}

    Artha transform(const std::string& vak) override {
        // Use shared state for result
        auto result_ptr = std::make_shared<std::optional<Artha>>();
        auto done = std::make_shared<std::atomic<bool>>(false);
        auto mtx = std::make_shared<std::mutex>();
        auto cv = std::make_shared<std::condition_variable>();

        // Copy vak for thread safety
        std::string vak_copy = vak;

        // Launch worker thread
        std::thread worker([this, vak_copy, result_ptr, done, mtx, cv]() {
            try {
                Artha result = inner_->transform(vak_copy);
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

        // Timeout or error
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

        // Use shared state for result
        auto result_ptr = std::make_shared<std::optional<std::vector<Artha>>>();
        auto done = std::make_shared<std::atomic<bool>>(false);
        auto mtx = std::make_shared<std::mutex>();
        auto cv = std::make_shared<std::condition_variable>();

        // Copy for thread safety
        std::vector<std::string> vaks_copy = vaks;
        auto batch_timeout = timeout_ + std::chrono::milliseconds(1000 * vaks_copy.size());

        // Launch worker thread
        std::thread worker([this, vaks_copy, result_ptr, done, mtx, cv]() {
            try {
                auto result = inner_->transform_batch(vaks_copy);
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

        // Timeout or error
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

    // For circuit breaker integration
    size_t consecutive_failures() const { return consecutive_failures_.load(); }
    size_t total_timeouts() const { return total_timeouts_.load(); }
    void reset_failures() { consecutive_failures_ = 0; }

    void set_timeout(std::chrono::milliseconds t) { timeout_ = t; }
    std::chrono::milliseconds timeout() const { return timeout_; }

private:
    std::shared_ptr<VakYantra> inner_;
    std::chrono::milliseconds timeout_;
    std::atomic<size_t> consecutive_failures_{0};
    std::atomic<size_t> total_timeouts_{0};
};

} // namespace chitta
