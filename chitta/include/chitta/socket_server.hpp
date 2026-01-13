#pragma once
// Socket Server: Unix domain socket server for daemon IPC
//
// Provides JSON-RPC 2.0 over Unix socket for multi-client access
// to the soul daemon. Uses poll() for non-blocking multiplexed I/O.
//
// Mind-scoped: Socket path derived from mind database path hash.
// Each mind gets its own daemon. Version compatibility checked via handshake.

#include <chitta/version.hpp>
#include <string>
#include <vector>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <unistd.h>

namespace chitta {

// djb2 hash - deterministic across platforms (unlike std::hash)
inline uint32_t djb2_hash(const std::string& str) {
    uint32_t hash = 5381;
    for (char c : str) {
        hash = ((hash << 5) + hash) + static_cast<unsigned char>(c);
    }
    return hash;
}

// Derive socket/lock/pid paths from mind database path
inline std::string socket_path_for_mind(const std::string& mind_path) {
    return "/tmp/chitta-" + std::to_string(djb2_hash(mind_path)) + ".sock";
}

inline std::string lock_path_for_mind(const std::string& mind_path) {
    return "/tmp/chitta-" + std::to_string(djb2_hash(mind_path)) + ".lock";
}

inline std::string pid_path_for_mind(const std::string& mind_path) {
    return "/tmp/chitta-" + std::to_string(djb2_hash(mind_path)) + ".pid";
}

// Represents a pending request from a client
struct ClientRequest {
    int client_fd;
    std::string data;
};

// Connection state for a single client
struct ClientConnection {
    int fd = -1;
    std::string read_buffer;
    std::string write_buffer;
    bool wants_close = false;

    bool has_complete_message() const;
    std::string extract_message();
};

// Unix domain socket server for JSON-RPC 2.0
class SocketServer {
public:
    // UID-scoped socket path for multi-user safety
    static std::string default_socket_path() {
        return "/tmp/chitta-" + std::to_string(getuid()) + ".sock";
    }
    static constexpr int MAX_CONNECTIONS = 32;
    static constexpr size_t MAX_MESSAGE_SIZE = 16 * 1024 * 1024;  // 16MB

    // Default constructor uses UID-scoped socket path
    SocketServer();
    explicit SocketServer(std::string socket_path);
    ~SocketServer();

    // Non-copyable, non-movable (owns file descriptor)
    SocketServer(const SocketServer&) = delete;
    SocketServer& operator=(const SocketServer&) = delete;
    SocketServer(SocketServer&&) = delete;
    SocketServer& operator=(SocketServer&&) = delete;

    // Lifecycle
    bool start();
    void stop();
    bool running() const { return server_fd_ >= 0; }

    // Main poll loop - call from daemon
    // Returns completed requests ready for processing
    // timeout_ms: -1 = block forever, 0 = non-blocking, >0 = wait up to N ms
    std::vector<ClientRequest> poll(int timeout_ms = 100);

    // Send response back to client (queues for async write)
    void respond(int client_fd, const std::string& response);

    // Statistics
    size_t connection_count() const { return connections_.size(); }
    size_t pending_writes() const;

    // Get socket path (for logging/debugging)
    const std::string& socket_path() const { return socket_path_; }

private:
    std::string socket_path_;
    int server_fd_ = -1;
    std::vector<ClientConnection> connections_;

    // Internal operations
    bool create_socket();
    void accept_new_connections();
    void read_from_clients(std::vector<ClientRequest>& requests);
    void write_to_clients();
    void cleanup_closed_connections();
};

} // namespace chitta
