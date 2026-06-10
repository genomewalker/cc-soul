// ThreadPool back-pressure tests: queue-cap rejection semantics.
// NDEBUG-proof (no assert): CHECK aborts with file:line on failure.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <future>
#include <string>
#include <thread>
#include "chitta/rpc/thread_pool.hpp"

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__,         \
                         __LINE__, #cond);                                     \
            std::abort();                                                      \
        }                                                                      \
    } while (0)

namespace {

void wait_until(const std::function<bool()>& cond) {
    for (int i = 0; i < 2500 && !cond(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(cond());
}

void test_submit_returns_id_and_completes() {
    chitta::ThreadPool pool(1, 1, 4);
    std::promise<std::string> done;
    auto fut = done.get_future();
    auto id = pool.submit(7, "echo",
        [] { return std::string("ok"); },
        [&done](int fd, std::string resp) {
            CHECK(fd == 7);
            done.set_value(std::move(resp));
        });
    CHECK(id.has_value());
    CHECK(fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(fut.get() == "ok");
    std::printf("  submit completes: PASS\n");
}

void test_overload_rejects_without_callback() {
    chitta::ThreadPool pool(1, 1, 2);
    std::promise<void> release;
    std::shared_future<void> gate(release.get_future());
    std::atomic<int> completed{0};
    auto count_done = [&completed](int, std::string) { ++completed; };

    // Occupy the single worker, then wait until it has dequeued the blocker.
    CHECK(pool.submit(1, "block",
        [gate] { gate.wait(); return std::string("{}"); }, count_done).has_value());
    wait_until([&pool] { return pool.pending() == 0; });

    // Fill the queue to the cap.
    CHECK(pool.submit(2, "q1", [] { return std::string("{}"); }, count_done).has_value());
    CHECK(pool.submit(3, "q2", [] { return std::string("{}"); }, count_done).has_value());
    CHECK(pool.pending() == 2);

    // Cap reached: rejected, callback not invoked, no trace entry left behind.
    std::atomic<bool> rejected_cb{false};
    auto rejected = pool.submit(4, "q3",
        [] { return std::string("{}"); },
        [&rejected_cb](int, std::string) { rejected_cb = true; });
    CHECK(!rejected.has_value());
    CHECK(!rejected_cb.load());
    CHECK(pool.active_count() == 3);  // blocker + q1 + q2, not q3

    // Drain; the pool must accept work again after rejection.
    release.set_value();
    wait_until([&completed] { return completed.load() == 3; });
    CHECK(pool.submit(5, "after", [] { return std::string("{}"); }, count_done).has_value());
    wait_until([&completed] { return completed.load() == 4; });
    CHECK(!rejected_cb.load());
    std::printf("  overload rejects without callback: PASS\n");
}

}  // namespace

int main() {
    std::printf("ThreadPool tests:\n");
    test_submit_returns_id_and_completes();
    test_overload_rejects_without_callback();
    std::printf("All ThreadPool tests passed.\n");
    return 0;
}
