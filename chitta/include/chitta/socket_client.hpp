#pragma once
// Socket Client: Unix domain socket client for daemon IPC
//
// Connects to the soul daemon via Unix socket, with auto-start
// capability if daemon is not running.
//
// UID-scoped: Socket path includes user ID for multi-user safety.
// Version compatibility checked via handshake.

#include <chitta/version.hpp>
#include <string>
#include <optional>
#include <unistd.h>

namespace chitta {

// Version info returned by daemon
struct DaemonVersion {
    std::string software;      // e.g., "2.36.0"
    int protocol_major = 0;
    int protocol_minor = 0;
};

class SocketClient {
public:
    // UID-scoped paths for multi-user safety
    static std::string default_socket_path() {
        return "/tmp/chitta-" + std::to_string(getuid()) + ".sock";
    }
    static std::string default_lock_path() {
        return "/tmp/chitta-daemon-" + std::to_string(getuid()) + ".lock";
    }
    static std::string default_pid_path() {
        return "/tmp/chitta-daemon-" + std::to_string(getuid()) + ".pid";
    }
    static constexpr int CONNECT_TIMEOUT_MS = 5000;
    static constexpr int RESPONSE_TIMEOUT_MS = 30000;

    // Default constructor uses UID-scoped socket path
    SocketClient();
    explicit SocketClient(std::string socket_path);
    ~SocketClient();

    // Non-copyable, non-movable (owns file descriptor)
    SocketClient(const SocketClient&) = delete;
    SocketClient& operator=(const SocketClient&) = delete;
    SocketClient(SocketClient&&) = delete;
    SocketClient& operator=(SocketClient&&) = delete;

    // Connection management
    bool connect();
    void disconnect();
    bool connected() const { return fd_ >= 0; }

    // Auto-start daemon if not running, verify version compatibility
    // Flow: find daemon → version check → compatible? use : restart
    // WARNING: This may kill existing daemons on version mismatch!
    bool ensure_daemon_running();

    // Safe connect: only connect to existing daemon, never kill/restart
    // Returns false with error if daemon not running or version mismatch
    // Use this for parallel agents to avoid killing shared daemon
    bool connect_only();

    // Check daemon version (must be connected)
    std::optional<DaemonVersion> check_version();

    // Request graceful daemon shutdown (for upgrades)
    bool request_shutdown();

    // Send JSON-RPC request, wait for response
    std::optional<std::string> request(const std::string& json_rpc);

    // Error message from last failed operation
    const std::string& last_error() const { return last_error_; }

    // Get socket path (for logging/debugging)
    const std::string& socket_path() const { return socket_path_; }

    // Wait for socket to disappear (for shutdown verification)
    bool wait_for_socket_gone(int timeout_ms);

private:
    std::string socket_path_;
    int fd_ = -1;
    std::string last_error_;

    bool start_daemon();
    bool wait_for_socket(int timeout_ms);
    int acquire_daemon_lock();
    void release_daemon_lock(int lock_fd);

    // Internal request without auto-reconnect (used by request())
    std::optional<std::string> request_internal(const std::string& json_rpc);
};

} // namespace chitta
