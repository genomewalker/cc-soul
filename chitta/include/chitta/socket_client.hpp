#pragma once
// Socket Client: Unix domain socket client for daemon IPC
//
// Connects to the soul daemon via Unix socket, with auto-start
// capability if daemon is not running.
//
// Protocol-versioned: Uses a single socket path. Version compatibility
// is checked via handshake. Incompatible daemons are gracefully restarted.

#include <chitta/version.hpp>
#include <string>
#include <optional>

namespace chitta {

// Version info returned by daemon
struct DaemonVersion {
    std::string software;      // e.g., "2.36.0"
    int protocol_major = 0;
    int protocol_minor = 0;
};

class SocketClient {
public:
    // Single socket path - version checked via protocol handshake
    static constexpr const char* SOCKET_PATH = "/tmp/chitta.sock";
    static constexpr const char* DAEMON_LOCK_PATH = "/tmp/chitta-daemon.lock";
    static constexpr int CONNECT_TIMEOUT_MS = 5000;
    static constexpr int RESPONSE_TIMEOUT_MS = 30000;

    // Default constructor uses standard socket path
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
    bool ensure_daemon_running();

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
