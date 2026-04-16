#pragma once
// Worktree sandbox guard for write-class RPCs (CONTRACTS.md §8).
// When CHITTA_SANDBOX=1, write-class RPCs are diverted to the dead-letter
// queue instead of mutating the live FieldStore.

#include <nlohmann/json.hpp>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>

namespace chitta {
struct ToolResult;
namespace sandbox {

using json = nlohmann::json;

inline bool is_sandboxed() {
    const char* v = std::getenv("CHITTA_SANDBOX");
    return v != nullptr && std::strcmp(v, "1") == 0;
}

// Append a single line with O_APPEND so concurrent writers don't interleave.
// On Linux regular files, write() with O_APPEND is atomic when the payload
// fits in the filesystem's atomic write unit; for typical ext4/xfs this is
// safely larger than a dead-letter/queue line, but we must not rely on any
// single-syscall guarantee either. A short write or EINTR loops until the
// full buffer is on disk; on hard error the partial state is what it is and
// we return false so callers can surface it.
// Returns true on success; false on open/write error.
inline bool append_line_atomic(const std::string& path, const std::string& line) {
    if (path.empty()) return false;
    int fd = ::open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    std::string buf = line;
    if (buf.empty() || buf.back() != '\n') buf.push_back('\n');
    const char* p = buf.data();
    size_t remaining = buf.size();
    bool ok = true;
    while (remaining > 0) {
        ssize_t n = ::write(fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            ok = false;
            break;
        }
        if (n == 0) { ok = false; break; }
        p += n;
        remaining -= static_cast<size_t>(n);
    }
    ::close(fd);
    return ok;
}

// Build the dead-letter JSON payload and append it to the failed queue.
// Returns the synthesized dead-letter id so callers can embed it in a
// ToolResult using their own ToolResult type.
inline std::string dead_letter_write(const std::string& failed_queue_path,
                                     std::atomic<size_t>* fail_count,
                                     const std::string& tool,
                                     const json& args) {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string dead_id = "sandbox-" + std::to_string(now_ms);

    if (failed_queue_path.empty()) return dead_id;

    json entry = {
        {"error", "sandbox intercept"},
        {"retry_count", 0},
        {"timestamp", now_ms},
        {"tool", tool},
        {"args", args},
        {"reason", "sandbox"},
        {"dead_lettered_id", dead_id}
    };

    append_line_atomic(failed_queue_path,
                       entry.dump(-1, ' ', false, json::error_handler_t::replace));
    if (fail_count) fail_count->fetch_add(1);

    return dead_id;
}

} // namespace sandbox
} // namespace chitta
