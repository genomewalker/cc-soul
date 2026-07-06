#pragma once
// Queue processor: handles fire-and-forget writes from hooks.
// Extracted from simple_cli.cpp — identical behavior.

#include <chitta/field_store.hpp>
#include <chitta/daemon_config.hpp>
#include <chitta/distillation.hpp>
#include <chitta/native_distiller.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <fstream>
#include <sstream>
#include <cstdio>

namespace chitta {

class VakYantra;
class FieldRpcHandler;

class QueueProcessor {
public:
    QueueProcessor(FieldStore& field_store,
                   VakYantra* yantra,
                   const DistillConfig& distill_config,
                   FieldRpcHandler& handler,
                   const std::string& queue_path,
                   const std::string& failed_queue_path,
                   std::atomic<size_t>& queue_count,
                   std::atomic<size_t>& queue_distill_count,
                   std::atomic<size_t>& queue_fail_count);

    void start();
    void stop();

    // Accessors for the counters (owned externally)
    std::atomic<size_t>& count() { return queue_count_; }
    std::atomic<size_t>& distill_count() { return queue_distill_count_; }
    std::atomic<size_t>& fail_count() { return queue_fail_count_; }

    // Live depth of the claimed batch still being processed (queue_status).
    const std::atomic<size_t>& batch_remaining() const { return batch_remaining_; }
    const std::string& queue_path() const { return queue_path_; }

private:
    void run();

    // Helper methods (were lambdas in simple_cli.cpp)
    static float category_to_confidence(const std::string& category);
    std::vector<float> embed_text(const std::string& text);
    static std::string category_to_kind(const std::string& cat);
    void write_failed_item(const std::string& line, const std::exception& e);
    static void write_checkpoint(const std::string& ckpt_path, size_t processed);

    FieldStore& field_store_;
    VakYantra* yantra_;
    const DistillConfig& distill_config_;
    FieldRpcHandler& handler_;
    std::string queue_path_;
    std::string failed_queue_path_;
    std::atomic<size_t>& queue_count_;
    std::atomic<size_t>& queue_distill_count_;
    std::atomic<size_t>& queue_fail_count_;
    std::atomic<size_t> batch_remaining_{0};

    std::thread thread_;
};

} // namespace chitta
