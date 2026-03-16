#pragma once
#ifdef CHITTA_FIELD_AVAILABLE

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "field_store.hpp"

namespace chitta {

// Fencing token: monotone increasing epoch for detecting stale writes.
using FencingToken = uint64_t;

// Two-tier op system:
// - Intent/Report ops: any instance can append (fencing_token = 0).
// - Authoritative ops: only the leader emits (fencing_token > 0).
enum class OpTier { Intent, Authoritative };

// SoulController: NFS-safe leader election via atomic mkdir lease.
//
// One SoulController per workspace instance. The leader wins by being the
// first to successfully mkdir(lease_dir/). On expiry or removal of that
// directory another instance can take over. Fencing tokens are incremented
// on every leadership change to guard against stale-leader writes.
class SoulController {
public:
    struct Config {
        std::string lease_dir;          // e.g. /nfs/workspace/.soul_lease
        std::string instance_id;        // unique per-process: hostname + ":" + pid
        std::chrono::milliseconds tick_interval{250};
        std::chrono::seconds lease_ttl{10};
        std::function<void(FencingToken)> on_became_leader;
        std::function<void()> on_lost_leadership;
    };

    explicit SoulController(Config config, FieldStore* field);
    ~SoulController();

    SoulController(const SoulController&) = delete;
    SoulController& operator=(const SoulController&) = delete;

    void start();
    void stop();

    bool is_leader() const { return is_leader_.load(); }
    FencingToken current_token() const { return fencing_token_.load(); }

    // Emit an authoritative op. Only valid when is_leader(). Returns false if not leader.
    bool emit_authoritative(const std::string& domain, const std::string& kind,
                            const std::string& entity_id, const std::string& payload_json);

    // Emit an intent/report op. Any instance may call this.
    void emit_intent(const std::string& domain, const std::string& kind,
                     const std::string& entity_id, const std::string& payload_json);

private:
    Config config_;
    FieldStore* field_;
    std::atomic<bool> is_leader_{false};
    std::atomic<FencingToken> fencing_token_{0};
    std::atomic<bool> running_{false};
    std::thread tick_thread_;

    void tick_loop();
    bool try_acquire_lease();
    void renew_lease();
    void release_lease();
    bool lease_expired() const;

    std::filesystem::path lease_marker() const;
    int64_t now_ms() const;
};

} // namespace chitta
#endif // CHITTA_FIELD_AVAILABLE
