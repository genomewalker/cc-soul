#include <chitta/socket_server.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <algorithm>

namespace chitta {

namespace {

bool socket_is_active(const std::string& path) {
    if (access(path.c_str(), F_OK) != 0) {
        return false;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return true;
    }

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    bool active = (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
    int err = errno;
    close(fd);

    if (active) {
        return true;
    }

    if (err == ECONNREFUSED || err == ENOENT) {
        return false;
    }

    // EACCES often indicates a stale socket from a crashed daemon with different permissions.
    // Treat as stale and attempt cleanup rather than falsely claiming "active".
    if (err == EACCES) {
        std::cerr << "[socket_server] EACCES on " << path << ", treating as stale\n";
        if (unlink(path.c_str()) == 0) {
            std::cerr << "[socket_server] Removed stale socket\n";
        }
        return false;
    }

    return false;
}

}

// Message framing: newline-delimited JSON (same as RPC stdio)
bool ClientConnection::has_complete_message() const {
    return read_buffer.find('\n', read_offset) != std::string::npos;
}

std::string ClientConnection::extract_message() {
    size_t pos = read_buffer.find('\n', read_offset);
    if (pos == std::string::npos) return "";

    std::string msg = read_buffer.substr(read_offset, pos - read_offset);
    read_offset = pos + 1;

    // Compact when offset exceeds half the buffer size
    if (read_offset > read_buffer.size() / 2) {
        read_buffer.erase(0, read_offset);
        read_offset = 0;
    }
    return msg;
}

SocketServer::SocketServer()
    : socket_path_(default_socket_path()) {}

SocketServer::SocketServer(std::string socket_path)
    : socket_path_(std::move(socket_path)) {}

SocketServer::~SocketServer() {
    stop();
}

bool SocketServer::start() {
    if (server_fd_ >= 0) return true;  // Already running

    // Create self-pipe for waking poll() when async responses are ready
    if (pipe(wake_pipe_) < 0) {
        std::cerr << "[socket_server] pipe() failed: " << strerror(errno) << "\n";
        return false;
    }
    // Set both ends non-blocking
    for (int i = 0; i < 2; ++i) {
        int flags = fcntl(wake_pipe_[i], F_GETFL, 0);
        if (flags >= 0) fcntl(wake_pipe_[i], F_SETFL, flags | O_NONBLOCK);
    }

    if (!create_socket()) {
        close(wake_pipe_[0]); close(wake_pipe_[1]);
        wake_pipe_[0] = wake_pipe_[1] = -1;
        return false;
    }

    std::cerr << "[socket_server] Listening on " << socket_path_ << "\n";
    return true;
}

void SocketServer::stop() {
    // Close all client connections
    for (auto& conn : connections_) {
        if (conn.fd >= 0) {
            close(conn.fd);
        }
    }
    connections_.clear();

    // Close server socket
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }

    // Close wake pipe
    for (int i = 0; i < 2; ++i) {
        if (wake_pipe_[i] >= 0) {
            close(wake_pipe_[i]);
            wake_pipe_[i] = -1;
        }
    }

    // Remove socket file
    unlink(socket_path_.c_str());
}

bool SocketServer::create_socket() {
    if (socket_is_active(socket_path_)) {
        std::cerr << "[socket_server] Socket already active at " << socket_path_ << "\n";
        return false;
    }

    if (access(socket_path_.c_str(), F_OK) == 0) {
        if (unlink(socket_path_.c_str()) != 0) {
            std::cerr << "[socket_server] Failed to remove stale socket: " << strerror(errno) << "\n";
            return false;
        }
    }

    // Create socket
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "[socket_server] socket() failed: " << strerror(errno) << "\n";
        return false;
    }

    // Set non-blocking
    int flags = fcntl(server_fd_, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);
    }

    // Bind to path
    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(server_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[socket_server] bind() failed: " << strerror(errno) << "\n";
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // Set permissions (user read/write only)
    chmod(socket_path_.c_str(), 0600);

    // Listen
    if (listen(server_fd_, MAX_CONNECTIONS) < 0) {
        std::cerr << "[socket_server] listen() failed: " << strerror(errno) << "\n";
        close(server_fd_);
        server_fd_ = -1;
        unlink(socket_path_.c_str());
        return false;
    }

    return true;
}

std::vector<ClientRequest> SocketServer::poll(int timeout_ms) {
    std::vector<ClientRequest> requests;

    if (server_fd_ < 0) return requests;

    // Rebuild poll fd array only when connections change
    if (fds_dirty_) {
        poll_fds_.clear();
        poll_fds_.reserve(2 + connections_.size());

        // Server socket - watch for new connections
        poll_fds_.push_back({server_fd_, POLLIN, 0});

        // Wake pipe read end - allows thread pool to wake us when responses ready
        if (wake_pipe_[0] >= 0) {
            poll_fds_.push_back({wake_pipe_[0], POLLIN, 0});
        }

        // Client sockets
        for (const auto& conn : connections_) {
            poll_fds_.push_back({conn.fd, POLLIN, 0});
        }

        fds_dirty_ = false;
    }

    // Update write interest flags each call (cheap O(n) without allocation)
    size_t client_start = 1 + (wake_pipe_[0] >= 0 ? 1 : 0);
    for (size_t i = 0; i < connections_.size(); ++i) {
        poll_fds_[client_start + i].events = POLLIN;
        if (!connections_[i].write_buffer.empty()) {
            poll_fds_[client_start + i].events |= POLLOUT;
        }
        poll_fds_[client_start + i].revents = 0;
    }
    // Clear revents for server and wake fds
    poll_fds_[0].revents = 0;
    if (wake_pipe_[0] >= 0) poll_fds_[1].revents = 0;

    auto& fds = poll_fds_;

    int ret = ::poll(fds.data(), fds.size(), timeout_ms);
    if (ret < 0) {
        if (errno != EINTR) {
            std::cerr << "[socket_server] poll() error: " << strerror(errno) << "\n";
        }
        return requests;
    }

    if (ret == 0) return requests;  // Timeout

    // Check server socket for new connections
    if (fds[0].revents & POLLIN) {
        accept_new_connections();
    }

    // Check wake pipe - drain to reset
    size_t wake_idx = (wake_pipe_[0] >= 0) ? 1 : 0;
    if (wake_pipe_[0] >= 0 && fds[1].revents & POLLIN) {
        char buf[64];
        while (read(wake_pipe_[0], buf, sizeof(buf)) > 0) {}  // Drain
    }
    size_t client_offset = wake_idx + 1;

    // Check client sockets (offset by server_fd + wake_fd)
    for (size_t i = client_offset; i < fds.size() && i - client_offset < connections_.size(); ++i) {
        auto& conn = connections_[i - client_offset];

        if (fds[i].revents & POLLIN) {
            // Read available data
            char buf[4096];
            ssize_t n = read(conn.fd, buf, sizeof(buf));

            if (n > 0) {
                conn.read_buffer.append(buf, static_cast<size_t>(n));

                // Check for message size limit
                if (conn.read_buffer.size() > MAX_MESSAGE_SIZE) {
                    std::cerr << "[socket_server] Client message too large, closing\n";
                    conn.wants_close = true;
                }
            } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                conn.wants_close = true;
            }
        }

        if (fds[i].revents & POLLOUT) {
            // Write pending data
            if (!conn.write_buffer.empty()) {
                ssize_t n = write(conn.fd, conn.write_buffer.data(), conn.write_buffer.size());
                if (n > 0) {
                    conn.write_buffer.erase(0, static_cast<size_t>(n));
                } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    conn.wants_close = true;
                }
            }
        }

        if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            conn.wants_close = true;
        }
    }

    // Extract complete messages
    for (auto& conn : connections_) {
        while (conn.has_complete_message()) {
            requests.push_back({conn.fd, conn.peer_pid, conn.extract_message()});
        }
    }

    // Cleanup closed connections
    cleanup_closed_connections();

    return requests;
}

void SocketServer::respond(int client_fd, const std::string& response) {
    for (auto& conn : connections_) {
        if (conn.fd == client_fd) {
            conn.write_buffer += response + "\n";
            return;
        }
    }
}

void SocketServer::queue_response(int client_fd, std::string data) {
    {
        std::lock_guard<std::mutex> lock(response_mutex_);
        response_queue_.push_back({client_fd, std::move(data)});
    }
    // Wake up poll() to send response immediately
    if (wake_pipe_[1] >= 0) {
        char byte = 1;
        [[maybe_unused]] auto _ = write(wake_pipe_[1], &byte, 1);
    }
}

std::vector<PendingResponse> SocketServer::drain_responses() {
    std::lock_guard<std::mutex> lock(response_mutex_);
    std::vector<PendingResponse> result;
    result.swap(response_queue_);
    return result;
}

void SocketServer::accept_new_connections() {
    while (true) {
        int client_fd = accept(server_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // No more pending connections
            }
            std::cerr << "[socket_server] accept() error: " << strerror(errno) << "\n";
            break;
        }

        if (connections_.size() >= MAX_CONNECTIONS) {
            std::cerr << "[socket_server] Max connections reached, rejecting\n";
            close(client_fd);
            continue;
        }

        // Set non-blocking
        int flags = fcntl(client_fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
        }

        // Get peer credentials (PID) via SO_PEERCRED
        pid_t peer_pid = 0;
#ifdef SO_PEERCRED
        struct ucred cred;
        socklen_t len = sizeof(cred);
        if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0) {
            peer_pid = cred.pid;
        }
#endif

        connections_.push_back({client_fd, peer_pid, "", 0, "", false});
        fds_dirty_ = true;
        std::cerr << "[socket_server] Client connected (fd=" << client_fd
                  << ", total=" << connections_.size() << ")\n";
    }
}

void SocketServer::cleanup_closed_connections() {
    auto it = std::remove_if(connections_.begin(), connections_.end(),
        [this](const ClientConnection& conn) {
            if (conn.wants_close) {
                std::cerr << "[socket_server] Client disconnected (fd=" << conn.fd << ")\n";
                if (disconnect_cb_) disconnect_cb_(conn.fd);
                close(conn.fd);
                return true;
            }
            return false;
        });
    if (it != connections_.end()) {
        connections_.erase(it, connections_.end());
        fds_dirty_ = true;
    }
}

size_t SocketServer::pending_writes() const {
    size_t total = 0;
    for (const auto& conn : connections_) {
        total += conn.write_buffer.size();
    }
    return total;
}

} // namespace chitta
