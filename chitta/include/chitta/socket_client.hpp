#pragma once
// Socket Client: Unix domain socket client for daemon IPC
//
// Connects to the soul daemon via Unix socket, with auto-start
// capability if daemon is not running.

#include <string>
#include <optional>

namespace chitta {

class SocketClient {
public:
    static constexpr const char* DEFAULT_SOCKET_PATH = "/tmp/chitta.sock";
    static constexpr int CONNECT_TIMEOUT_MS = 5000;
    static constexpr int RESPONSE_TIMEOUT_MS = 30000;

    explicit SocketClient(std::string socket_path = DEFAULT_SOCKET_PATH);
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

    // Auto-start daemon if not running
    // Returns true if daemon is running (started or was already running)
    bool ensure_daemon_running();

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
};

} // namespace chitta
