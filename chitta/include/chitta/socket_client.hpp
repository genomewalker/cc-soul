#pragma once
// Socket Client: Unix domain socket client for daemon IPC
//
// Connects to the soul daemon via Unix socket, with auto-start
// capability if daemon is not running.
//
// Version-aware: Socket path includes version to ensure clients
// always connect to compatible daemon. Old daemons are auto-restarted.

#include <chitta/version.hpp>
#include <string>
#include <optional>

namespace chitta {

class SocketClient {
public:
    // Socket path includes version for automatic upgrade handling
    static std::string default_socket_path() {
        return std::string("/tmp/chitta-") + CHITTA_VERSION + ".sock";
    }
    // Legacy socket path for backwards compatibility during transition
    static constexpr const char* LEGACY_SOCKET_PATH = "/tmp/chitta.sock";
    static constexpr const char* DAEMON_LOCK_PATH = "/tmp/chitta-daemon.lock";
    static constexpr int CONNECT_TIMEOUT_MS = 5000;
    static constexpr int RESPONSE_TIMEOUT_MS = 30000;

    // Default constructor uses versioned socket path
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
    // Returns true if daemon is running with compatible version
    bool ensure_daemon_running();

    // Request graceful daemon shutdown (for upgrades)
    bool request_shutdown();

    // Send JSON-RPC request, wait for response
    // Returns nullopt on error
    std::optional<std::string> request(const std::string& json_rpc);

    // Error message from last failed operation
    const std::string& last_error() const { return last_error_; }

    // Get socket path (for logging/debugging)
    const std::string& socket_path() const { return socket_path_; }

private:
    std::string socket_path_;
    int fd_ = -1;
    std::string last_error_;

    bool start_daemon();
    bool wait_for_socket(int timeout_ms);
    bool wait_for_socket_gone(int timeout_ms);
    void cleanup_legacy_daemon();
    int acquire_daemon_lock();  // Returns lock fd or -1 on failure
    void release_daemon_lock(int lock_fd);
};

} // namespace chitta
