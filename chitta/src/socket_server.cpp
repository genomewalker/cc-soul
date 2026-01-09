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

// Message framing: newline-delimited JSON (same as MCP stdio)
bool ClientConnection::has_complete_message() const {
    return read_buffer.find('\n') != std::string::npos;
}

std::string ClientConnection::extract_message() {
    size_t pos = read_buffer.find('\n');
    if (pos == std::string::npos) return "";

    std::string msg = read_buffer.substr(0, pos);
    read_buffer.erase(0, pos + 1);
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

    if (!create_socket()) {
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

    // Remove socket file
    unlink(socket_path_.c_str());
}

bool SocketServer::create_socket() {
    // Remove stale socket file
    unlink(socket_path_.c_str());

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
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
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

    // Build poll fd array
    std::vector<pollfd> fds;
    fds.reserve(1 + connections_.size());

    // Server socket - watch for new connections
    fds.push_back({server_fd_, POLLIN, 0});

    // Client sockets
    for (const auto& conn : connections_) {
        short events = POLLIN;
        if (!conn.write_buffer.empty()) {
            events |= POLLOUT;
        }
        fds.push_back({conn.fd, events, 0});
    }

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

    // Check client sockets
    for (size_t i = 1; i < fds.size() && i - 1 < connections_.size(); ++i) {
        auto& conn = connections_[i - 1];

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
            requests.push_back({conn.fd, conn.extract_message()});
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

        connections_.push_back({client_fd, "", "", false});
        std::cerr << "[socket_server] Client connected (fd=" << client_fd
                  << ", total=" << connections_.size() << ")\n";
    }
}

void SocketServer::cleanup_closed_connections() {
    auto it = std::remove_if(connections_.begin(), connections_.end(),
        [](const ClientConnection& conn) {
            if (conn.wants_close) {
                std::cerr << "[socket_server] Client disconnected (fd=" << conn.fd << ")\n";
                close(conn.fd);
                return true;
            }
            return false;
        });
    connections_.erase(it, connections_.end());
}

size_t SocketServer::pending_writes() const {
    size_t total = 0;
    for (const auto& conn : connections_) {
        total += conn.write_buffer.size();
    }
    return total;
}

} // namespace chitta
