#pragma once
// Always-on operational log for per-cycle background summaries.
//
// The daemon's std::cerr goes to journald's stdout socket, but the user journal
// is often unreadable (not in systemd-journal group; volatile storage) and
// systemd < 240 cannot redirect a unit's output to a file. So per-cycle summaries
// (distillation / maintenance / backfill) would leave no readable trace of a
// runaway. This sink writes them to <mind>/chittad-ops.log — always readable,
// independent of foreground/journal/systemd version. Thread-safe append; a no-op
// until init() (so the shared CLI path stays silent).
#include <fstream>
#include <mutex>
#include <string>
#include <chrono>
#include <ctime>

namespace chitta {

class OpsLog {
public:
    static OpsLog& instance() { static OpsLog inst; return inst; }

    void init(const std::string& mind_path) {
        std::lock_guard<std::mutex> lk(mu_);
        if (path_.empty()) {
            path_ = mind_path + "/chittad-ops.log";
            out_.open(path_, std::ios::app);
        }
    }

    void write(const std::string& msg) {
        std::lock_guard<std::mutex> lk(mu_);
        if (!out_.is_open()) return;
        out_ << ts() << ' ' << msg << '\n';
        out_.flush();
    }

private:
    static std::string ts() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_r(&t, &tm);
        char buf[20];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        return buf;
    }

    std::mutex mu_;
    std::ofstream out_;
    std::string path_;
};

inline void ops_log(const std::string& msg) { OpsLog::instance().write(msg); }

}  // namespace chitta
