// SoulController: NFS-safe leader election via atomic mkdir lease.
//
// The lease_dir directory itself is the lock. mkdir() is atomic on NFS (POSIX),
// so the instance that successfully creates the directory wins leadership.
// A leader.json file inside records instance_id, timestamp, and epoch so that
// non-leaders can detect expiry and attempt a takeover.

#ifdef CHITTA_FIELD_AVAILABLE

#include <chitta/soul_controller.hpp>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <sys/stat.h>

namespace chitta {

namespace {

// Write JSON atomically: write to a temp file then rename into place.
// On NFS, rename() is atomic for replacing an existing file.
void write_json_atomic(const std::filesystem::path& target, const nlohmann::json& j) {
    auto tmp = target;
    tmp += ".tmp";
    {
        std::ofstream f(tmp);
        if (!f) throw std::runtime_error("cannot open " + tmp.string() + " for writing");
        f << j.dump();
    }
    std::filesystem::rename(tmp, target);
}

} // namespace

SoulController::SoulController(Config config, FieldStore* field)
    : config_(std::move(config))
    , field_(field)
{}

SoulController::~SoulController() {
    stop();
}

void SoulController::start() {
    running_.store(true);
    tick_thread_ = std::thread([this] { tick_loop(); });
}

void SoulController::stop() {
    running_.store(false);
    if (tick_thread_.joinable()) tick_thread_.join();
    if (is_leader_.load()) release_lease();
}

// ── Lease helpers ─────────────────────────────────────────────────────────────

std::filesystem::path SoulController::lease_marker() const {
    return std::filesystem::path(config_.lease_dir) / "leader.json";
}

int64_t SoulController::now_ms() const {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()
    ).count();
}

// Try to acquire the NFS lease.
// Uses ::mkdir() directly for atomic NFS semantics (EEXIST on collision).
// Returns true if we now hold the lease.
bool SoulController::try_acquire_lease() {
    const std::string dir = config_.lease_dir;

    // Attempt atomic directory creation — this is the NFS lock primitive.
    if (::mkdir(dir.c_str(), 0755) == 0) {
        // We created the directory: we are the new leader.
        nlohmann::json j = {
            {"instance_id", config_.instance_id},
            {"ts_ms",       now_ms()},
            {"epoch",       fencing_token_.load() + 1}
        };
        try {
            write_json_atomic(lease_marker(), j);
        } catch (...) {
            // If we can't write the marker, abandon the lease so others don't
            // get stuck waiting for an unreadable expiry.
            release_lease();
            return false;
        }
        return true;
    }

    if (errno != EEXIST) {
        // Unexpected error (permissions, etc.) — cannot acquire.
        return false;
    }

    // Directory already exists: check whether it belongs to us or has expired.
    if (lease_expired()) {
        // Remove the stale lease directory and retry on the next tick.
        // We do it in two steps: remove marker then rmdir (only works if empty).
        std::error_code ec;
        std::filesystem::remove(lease_marker(), ec);
        std::filesystem::remove(std::filesystem::path(config_.lease_dir), ec);
        // Don't claim leadership in this tick; let the next tick win cleanly.
    }

    return false;
}

// Renew our lease by updating the timestamp in leader.json.
void SoulController::renew_lease() {
    nlohmann::json j = {
        {"instance_id", config_.instance_id},
        {"ts_ms",       now_ms()},
        {"epoch",       fencing_token_.load()}
    };
    try {
        write_json_atomic(lease_marker(), j);
    } catch (...) {
        // Write failure means we've lost the lease (NFS mount problem, etc.).
        is_leader_.store(false);
        fencing_token_.fetch_add(1);
        if (config_.on_lost_leadership) config_.on_lost_leadership();
    }
}

// Release the lease by removing leader.json then the directory.
void SoulController::release_lease() {
    std::error_code ec;
    std::filesystem::remove(lease_marker(), ec);
    std::filesystem::remove(std::filesystem::path(config_.lease_dir), ec);
}

// Check whether the lease held by whoever created lease_dir has expired.
bool SoulController::lease_expired() const {
    std::ifstream f(lease_marker());
    if (!f) return true; // Unreadable → treat as expired.

    nlohmann::json j;
    try {
        f >> j;
    } catch (...) {
        return true;
    }

    if (!j.contains("ts_ms")) return true;
    int64_t ts = j["ts_ms"].get<int64_t>();
    int64_t ttl_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        config_.lease_ttl).count();

    return (now_ms() - ts) > ttl_ms;
}

// ── Tick loop ─────────────────────────────────────────────────────────────────

void SoulController::tick_loop() {
    while (running_.load()) {
        if (is_leader_.load()) {
            renew_lease();
            // renew_lease() updates is_leader_ on failure; nothing else to do.
        } else {
            if (try_acquire_lease()) {
                FencingToken tok = fencing_token_.fetch_add(1) + 1;
                is_leader_.store(true);
                if (config_.on_became_leader) config_.on_became_leader(tok);
            }
        }

        std::this_thread::sleep_for(config_.tick_interval);
    }
}

// ── Public emit API ───────────────────────────────────────────────────────────

bool SoulController::emit_authoritative(const std::string& domain, const std::string& kind,
                                        const std::string& entity_id,
                                        const std::string& payload_json) {
    if (!is_leader_.load()) return false;
    field_->emit_event(domain, kind, entity_id, payload_json, fencing_token_.load());
    return true;
}

void SoulController::emit_intent(const std::string& domain, const std::string& kind,
                                 const std::string& entity_id,
                                 const std::string& payload_json) {
    field_->emit_event(domain, kind, entity_id, payload_json, 0);
}

} // namespace chitta

#endif // CHITTA_FIELD_AVAILABLE
