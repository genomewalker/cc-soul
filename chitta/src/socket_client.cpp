#include <chitta/socket_client.hpp>
#include <chitta/version.hpp>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <iostream>

namespace chitta {


SocketClient::SocketClient()
    : socket_path_(default_socket_path()) {}

SocketClient::SocketClient(std::string socket_path)
    : socket_path_(std::move(socket_path)) {}

SocketClient::~SocketClient() {
    disconnect();
}

bool SocketClient::connect() {
    if (fd_ >= 0) return true;  // Already connected

    fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) {
        last_error_ = std::string("socket() failed: ") + strerror(errno);
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        last_error_ = std::string("connect() failed: ") + strerror(errno);
        close(fd_);
        fd_ = -1;
        return false;
    }

    return true;
}

void SocketClient::disconnect() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

bool SocketClient::ensure_daemon_running() {
    if (!connect()) {
        last_error_ = "Cannot connect to daemon at " + socket_path_ + " - is daemon running?";
        return false;
    }

    auto health = check_health();
    if (!health) {
        last_error_ = "Daemon did not respond to health check";
        disconnect();
        return false;
    }

    bool compatible = chitta::version::protocol_compatible(
        health->protocol_major, health->protocol_minor);

    if (!compatible) {
        last_error_ = "Daemon v" + health->software + " incompatible with client v" +
                      std::string(CHITTA_VERSION);
        disconnect();
        return false;
    }

    return true;
}

std::optional<DaemonVersion> SocketClient::check_version() {
    using json = nlohmann::json;

    // JSON-RPC request for version info
    auto response = request(R"({"jsonrpc":"2.0","id":0,"method":"tools/call","params":{"name":"version_check"}})");
    if (!response) {
        return std::nullopt;
    }

    try {
        auto j = json::parse(*response);

        // Navigate: result.structured.{software_version, protocol_major, protocol_minor}
        if (!j.contains("result") || !j["result"].contains("structured")) {
            return std::nullopt;
        }

        auto& structured = j["result"]["structured"];
        DaemonVersion ver;
        ver.software = structured.value("software_version", "");
        ver.protocol_major = structured.value("protocol_major", 0);
        ver.protocol_minor = structured.value("protocol_minor", 0);

        if (ver.protocol_major > 0 || !ver.software.empty()) {
            return ver;
        }
    } catch (...) {
        // JSON parse failed
    }

    return std::nullopt;
}

std::optional<DaemonHealth> SocketClient::check_health() {
    using json = nlohmann::json;

    auto response = request(R"({"jsonrpc":"2.0","id":0,"method":"tools/call","params":{"name":"health_check"}})");
    if (!response) {
        return std::nullopt;
    }

    try {
        auto j = json::parse(*response);

        if (!j.contains("result") || !j["result"].contains("structured")) {
            return std::nullopt;
        }

        auto& structured = j["result"]["structured"];
        DaemonHealth health;
        health.software = structured.value("software_version", "");
        health.protocol_major = structured.value("protocol_major", 0);
        health.protocol_minor = structured.value("protocol_minor", 0);
        health.pid = structured.value("pid", 0);
        health.uptime_ms = structured.value("uptime_ms", 0);
        health.socket_path = structured.value("socket_path", "");
        health.db_path = structured.value("db_path", "");
        health.status = structured.value("status", "");

        if (health.protocol_major > 0 || !health.software.empty()) {
            return health;
        }
    } catch (...) {
        // JSON parse failed
    }

    return std::nullopt;
}

bool SocketClient::request_shutdown() {
    if (!connected() && !connect()) {
        return false;
    }

    // Use request_internal directly - don't retry on connection close
    // (daemon closing connection after shutdown is expected behavior)
    auto response = request_internal("shutdown");
    disconnect();

    // Success if we got a response OR if connection was closed (daemon shutting down)
    if (response.has_value()) {
        return true;
    }
    // Connection closed = daemon received shutdown and is stopping
    return last_error_.find("Connection closed") != std::string::npos;
}

bool SocketClient::wait_for_socket_gone(int timeout_ms) {
    auto start = std::chrono::steady_clock::now();

    while (access(socket_path_.c_str(), F_OK) == 0) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_ms) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return true;
}


std::optional<std::string> SocketClient::request_internal(const std::string& json_rpc) {
    if (fd_ < 0) {
        last_error_ = "Not connected";
        return std::nullopt;
    }

    // Send request (newline-delimited)
    std::string msg = json_rpc + "\n";
    size_t sent = 0;
    while (sent < msg.size()) {
        ssize_t n = write(fd_, msg.data() + sent, msg.size() - sent);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // Would block - wait for writable
                pollfd pfd = {fd_, POLLOUT, 0};
                poll(&pfd, 1, 1000);
                continue;
            }
            last_error_ = std::string("write() failed: ") + strerror(errno);
            return std::nullopt;
        }
        sent += static_cast<size_t>(n);
    }

    // Wait for response
    std::string response;
    pollfd pfd = {fd_, POLLIN, 0};

    while (true) {
        int ret = poll(&pfd, 1, RESPONSE_TIMEOUT_MS);

        if (ret < 0) {
            if (errno == EINTR) continue;
            last_error_ = std::string("poll() failed: ") + strerror(errno);
            return std::nullopt;
        }

        if (ret == 0) {
            last_error_ = "Response timeout";
            return std::nullopt;
        }

        char buf[4096];
        ssize_t n = read(fd_, buf, sizeof(buf));

        if (n <= 0) {
            last_error_ = n == 0 ? "Connection closed" :
                          std::string("read() failed: ") + strerror(errno);
            return std::nullopt;
        }

        response.append(buf, static_cast<size_t>(n));
        if (response.size() > SocketClient::MAX_RESPONSE_SIZE) {
            last_error_ = "Response too large";
            return std::nullopt;
        }

        // Check for complete message (newline)
        size_t pos = response.find('\n');
        if (pos != std::string::npos) {
            return response.substr(0, pos);
        }
    }
}

std::optional<std::string> SocketClient::request(const std::string& json_rpc) {
    // Try the request
    auto result = request_internal(json_rpc);
    if (result) {
        return result;
    }

    // Request failed - check if it's a connection issue that warrants reconnect
    bool connection_lost = (last_error_.find("Connection closed") != std::string::npos ||
                           last_error_.find("write() failed") != std::string::npos ||
                           last_error_.find("Broken pipe") != std::string::npos ||
                           last_error_.find("Connection reset") != std::string::npos);

    if (!connection_lost) {
        return std::nullopt;  // Some other error, don't retry
    }

    // Connection lost - try simple reconnect (don't start daemon)
    std::cerr << "[socket_client] Connection lost (" << last_error_ << "), attempting reconnect...\n";
    disconnect();

    // Just try to reconnect - don't auto-start daemon
    if (!connect()) {
        std::cerr << "[socket_client] Reconnect failed: " << last_error_ << "\n";
        return std::nullopt;
    }

    std::cerr << "[socket_client] Reconnected, retrying request\n";

    // Retry the request once
    return request_internal(json_rpc);
}

bool SocketClient::connect_only() {
    // Safe connect: never kill or start daemons
    // Use this for parallel agents to avoid killing shared daemon

    // Only set default if not already set by constructor
    if (socket_path_.empty()) {
        socket_path_ = default_socket_path();
    }

    if (!connect()) {
        last_error_ = "Cannot connect to daemon at " + socket_path_ + " - is daemon running?";
        return false;
    }

    auto health = check_health();
    if (!health) {
        last_error_ = "Daemon did not respond to health check";
        disconnect();
        return false;
    }

    bool compatible = chitta::version::protocol_compatible(
        health->protocol_major, health->protocol_minor);

    if (!compatible) {
        last_error_ = "Daemon v" + health->software + " incompatible with client v" +
                      std::string(CHITTA_VERSION) + " - restart daemon manually";
        disconnect();
        return false;
    }

    return true;
}

} // namespace chitta
