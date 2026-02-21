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
#include <mutex>
#include <functional>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <unistd.h>
#include <poll.h>
#include <sys/stat.h>

namespace chitta {

// djb2 hash - deterministic across platforms (unlike std::hash)
inline uint32_t djb2_hash(const std::string& str) {
    uint32_t hash = 5381;
    for (char c : str) {
        hash = ((hash << 5) + hash) + static_cast<unsigned char>(c);
    }
    return hash;
}

// Get persistent socket directory (immune to /tmp cleanup on HPC systems)
// Priority: $XDG_RUNTIME_DIR > ~/.cache/chitta > /tmp
inline std::string get_socket_dir() {
    // XDG_RUNTIME_DIR is session-scoped and managed by systemd
    const char* xdg_runtime = getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime && access(xdg_runtime, W_OK) == 0) {
        std::string dir = std::string(xdg_runtime) + "/chitta";
        mkdir(dir.c_str(), 0700);
        return dir;
    }
    // Fall back to ~/.cache/chitta (persistent, user-owned)
    const char* home = getenv("HOME");
    if (home) {
        std::string cache_dir = std::string(home) + "/.cache";
        mkdir(cache_dir.c_str(), 0755);
        std::string dir = cache_dir + "/chitta";
        mkdir(dir.c_str(), 0700);
        return dir;
    }
    // Last resort: /tmp (may be cleaned up on HPC systems)
    return "/tmp";
}

// Derive socket/lock/pid paths from mind database path
inline std::string socket_path_for_mind(const std::string& mind_path) {
    return get_socket_dir() + "/chitta-" + std::to_string(djb2_hash(mind_path)) + ".sock";
}

inline std::string lock_path_for_mind(const std::string& mind_path) {
    return get_socket_dir() + "/chitta-" + std::to_string(djb2_hash(mind_path)) + ".lock";
}

inline std::string pid_path_for_mind(const std::string& mind_path) {
    return get_socket_dir() + "/chitta-" + std::to_string(djb2_hash(mind_path)) + ".pid";
}

// Represents a pending request from a client
struct ClientRequest {
    int client_fd;
    pid_t peer_pid;  // Peer process ID from SO_PEERCRED
    std::string data;
};

// Represents a pending response to send back
struct PendingResponse {
    int client_fd;
    std::string data;
};

// Connection state for a single client
struct ClientConnection {
    int fd = -1;
    pid_t peer_pid = 0;  // Peer process ID from SO_PEERCRED
    std::string read_buffer;
    size_t read_offset = 0;  // Offset tracking to avoid O(n) erase
    std::string write_buffer;
    bool wants_close = false;

    bool has_complete_message() const;
    std::string extract_message();
};

// Unix domain socket server for JSON-RPC 2.0
class SocketServer {
public:
    // Mind-scoped socket path (matches SocketClient)
    static std::string default_socket_path() {
        // Use mind path hash to match client
        const char* db_path = std::getenv("CHITTA_DB_PATH");
        std::string mind_path;
        if (db_path) {
            mind_path = db_path;
        } else if (const char* home = std::getenv("HOME")) {
            mind_path = std::string(home) + "/.claude/mind";
        }
        return socket_path_for_mind(mind_path);
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

    // Thread-safe response queue (for async RPC completion)
    void queue_response(int client_fd, std::string data);
    std::vector<PendingResponse> drain_responses();

    // Statistics
    size_t connection_count() const { return connections_.size(); }
    size_t pending_writes() const;

    // Get socket path (for logging/debugging)
    const std::string& socket_path() const { return socket_path_; }

    // Disconnect callback (called when a client FD closes — used for stream cleanup)
    using DisconnectCallback = std::function<void(int fd)>;
    void set_disconnect_callback(DisconnectCallback cb) { disconnect_cb_ = std::move(cb); }

private:
    std::string socket_path_;
    int server_fd_ = -1;
    int wake_pipe_[2] = {-1, -1};  // self-pipe for waking poll() when responses ready
    std::vector<ClientConnection> connections_;

    // Thread-safe response queue for async RPC
    mutable std::mutex response_mutex_;
    std::vector<PendingResponse> response_queue_;

    // Disconnect callback for streaming subscribers
    DisconnectCallback disconnect_cb_;

    // Cached pollfd array - only rebuilt when connections change
    std::vector<struct pollfd> poll_fds_;
    bool fds_dirty_ = true;

    // Internal operations
    bool create_socket();
    void accept_new_connections();
    void read_from_clients(std::vector<ClientRequest>& requests);
    void write_to_clients();
    void cleanup_closed_connections();
};

} // namespace chitta
