#include <chitta/socket_client.hpp>
#include <chitta/version.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <dirent.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <iostream>
#include <vector>
#include <algorithm>
#include <glob.h>
#include <sstream>

namespace chitta {

namespace {

// Parse semantic version string (e.g., "2.30.0") into comparable tuple
std::tuple<int, int, int> parse_version(const std::string& v) {
    int major = 0, minor = 0, patch = 0;
    sscanf(v.c_str(), "%d.%d.%d", &major, &minor, &patch);
    return {major, minor, patch};
}

// Find all installed plugin versions, sorted newest first
std::vector<std::string> find_installed_versions(const std::string& cache_base) {
    std::vector<std::string> versions;

    DIR* dir = opendir(cache_base.c_str());
    if (!dir) return versions;

    while (struct dirent* entry = readdir(dir)) {
        if (entry->d_type == DT_DIR && entry->d_name[0] != '.') {
            std::string name = entry->d_name;
            // Check if it looks like a version (starts with digit)
            if (!name.empty() && name[0] >= '0' && name[0] <= '9') {
                versions.push_back(name);
            }
        }
    }
    closedir(dir);

    // Sort by version descending (newest first)
    std::sort(versions.begin(), versions.end(), [](const auto& a, const auto& b) {
        return parse_version(a) > parse_version(b);
    });

    return versions;
}

// Find all chitta socket files in /tmp (including versioned ones)
std::vector<std::string> find_chitta_sockets() {
    std::vector<std::string> sockets;
    glob_t globbuf;

    if (glob("/tmp/chitta*.sock", 0, nullptr, &globbuf) == 0) {
        for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
            sockets.push_back(globbuf.gl_pathv[i]);
        }
        globfree(&globbuf);
    }

    return sockets;
}

// Parse JSON field from response (simple parser for {"field": "value"} or {"field": 123})
std::string json_get_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos += search.length();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;

    if (pos < json.size() && json[pos] == '"') {
        ++pos;
        auto end = json.find('"', pos);
        if (end != std::string::npos) {
            return json.substr(pos, end - pos);
        }
    }
    return "";
}

int json_get_int(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return 0;

    pos += search.length();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;

    return std::atoi(json.c_str() + pos);
}

}  // anonymous namespace

SocketClient::SocketClient()
    : socket_path_(SOCKET_PATH) {}

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
    // Clean up old versioned sockets from previous versions
    cleanup_versioned_sockets();

    // Try to connect to any running daemon
    if (try_connect_any_socket()) {
        // Connected - check version compatibility
        auto version = check_version();
        if (version) {
            bool compatible = chitta::version::protocol_compatible(
                version->protocol_major, version->protocol_minor);

            if (compatible) {
                std::cerr << "[socket_client] Connected to compatible daemon v"
                          << version->software << " (protocol "
                          << version->protocol_major << "."
                          << version->protocol_minor << ")\n";
                return true;
            }

            // Incompatible - request graceful shutdown
            std::cerr << "[socket_client] Daemon v" << version->software
                      << " incompatible with client v" << CHITTA_VERSION
                      << " - restarting\n";
            request("shutdown");
            disconnect();

            // Wait for old daemon to stop
            if (!wait_for_socket_gone(3000)) {
                std::cerr << "[socket_client] Old daemon didn't stop, removing socket\n";
                unlink(socket_path_.c_str());
            }
        } else {
            // Version check failed - old daemon without version support
            std::cerr << "[socket_client] Daemon doesn't support version check - restarting\n";
            request("shutdown");
            disconnect();
            wait_for_socket_gone(3000);
        }
    }

    // No compatible daemon running - acquire lock before starting
    int lock_fd = acquire_daemon_lock();

    // Re-check after acquiring lock (another process may have started daemon)
    if (try_connect_any_socket()) {
        auto version = check_version();
        if (version && chitta::version::protocol_compatible(
                version->protocol_major, version->protocol_minor)) {
            release_daemon_lock(lock_fd);
            return true;
        }
        disconnect();
    }

    // Start new daemon
    bool started = start_daemon();
    release_daemon_lock(lock_fd);

    if (!started) {
        return false;
    }

    // Wait for socket to become available
    if (!wait_for_socket(CONNECT_TIMEOUT_MS)) {
        last_error_ = "Daemon started but socket not available after " +
                      std::to_string(CONNECT_TIMEOUT_MS) + "ms";
        return false;
    }

    return connect();
}

std::optional<DaemonVersion> SocketClient::check_version() {
    // JSON-RPC request for version info
    auto response = request(R"({"jsonrpc":"2.0","id":0,"method":"tools/call","params":{"name":"version_check"}})");
    if (!response) {
        return std::nullopt;
    }

    // Parse response - look for result.content[0].text or result directly
    DaemonVersion ver;

    // Try to find version fields in response
    ver.software = json_get_string(*response, "software_version");
    if (ver.software.empty()) {
        ver.software = json_get_string(*response, "version");
    }
    if (ver.software.empty()) {
        // Fallback: try to find in nested result
        auto text_pos = response->find("\"text\":");
        if (text_pos != std::string::npos) {
            ver.software = json_get_string(*response, "software_version");
        }
    }

    ver.protocol_major = json_get_int(*response, "protocol_major");
    ver.protocol_minor = json_get_int(*response, "protocol_minor");

    // If we got at least protocol version, consider it valid
    if (ver.protocol_major > 0 || !ver.software.empty()) {
        return ver;
    }

    return std::nullopt;
}

bool SocketClient::try_connect_any_socket() {
    // First try the standard socket path
    socket_path_ = SOCKET_PATH;
    if (connect()) {
        return true;
    }

    // Then try any other chitta sockets (versioned ones from older versions)
    auto sockets = find_chitta_sockets();
    for (const auto& sock : sockets) {
        if (sock == SOCKET_PATH) continue;

        socket_path_ = sock;
        if (connect()) {
            std::cerr << "[socket_client] Found running daemon at " << sock << "\n";
            return true;
        }
    }

    // Reset to standard path
    socket_path_ = SOCKET_PATH;
    return false;
}

void SocketClient::cleanup_versioned_sockets() {
    // Remove stale versioned sockets (not our main socket)
    auto sockets = find_chitta_sockets();
    for (const auto& sock : sockets) {
        if (sock == SOCKET_PATH) continue;

        // Try to connect - if fails, it's stale
        int test_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (test_fd < 0) continue;

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, sock.c_str(), sizeof(addr.sun_path) - 1);

        if (::connect(test_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            // Stale socket - remove it
            std::cerr << "[socket_client] Removing stale socket: " << sock << "\n";
            unlink(sock.c_str());
        }
        close(test_fd);
    }
}

bool SocketClient::request_shutdown() {
    if (!connected() && !connect()) {
        return false;
    }

    auto response = request("shutdown");
    disconnect();
    return response.has_value();
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


bool SocketClient::start_daemon() {
    std::cerr << "[socket_client] Starting daemon...\n";

    pid_t pid = fork();

    if (pid < 0) {
        last_error_ = std::string("fork() failed: ") + strerror(errno);
        return false;
    }

    if (pid == 0) {
        // Child process - exec daemon

        // Detach from parent process group (become daemon)
        setsid();

        // Redirect stdout/stderr to log file or /dev/null
        const char* home = getenv("HOME");
        std::string log_path;
        if (home) {
            log_path = std::string(home) + "/.claude/mind/.daemon.log";
        }

        int log_fd = -1;
        if (!log_path.empty()) {
            log_fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
        }
        if (log_fd < 0) {
            log_fd = open("/dev/null", O_RDWR);
        }
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }

        // Close stdin
        close(STDIN_FILENO);

        // Find daemon binary - try multiple locations
        std::vector<std::string> daemon_paths;

        // Check CLAUDE_PLUGIN_ROOT first
        if (const char* plugin_root = getenv("CLAUDE_PLUGIN_ROOT")) {
            daemon_paths.push_back(std::string(plugin_root) + "/bin/chitta_cli");
        }

        // Standard plugin cache location - discover installed versions
        if (home) {
            std::string cache_base = std::string(home) +
                "/.claude/plugins/cache/genomewalker-cc-soul/cc-soul";
            auto versions = find_installed_versions(cache_base);
            for (const auto& ver : versions) {
                daemon_paths.push_back(cache_base + "/" + ver + "/bin/chitta_cli");
            }

            // Marketplace location
            daemon_paths.push_back(std::string(home) +
                "/.claude/plugins/marketplaces/genomewalker-cc-soul/bin/chitta_cli");

            // Development location
            daemon_paths.push_back(std::string(home) +
                "/.claude/bin/chitta_cli");
        }

        // Find mind path
        std::string mind_path;
        if (const char* db_path = getenv("CHITTA_DB_PATH")) {
            mind_path = db_path;
        } else if (home) {
            mind_path = std::string(home) + "/.claude/mind/chitta";
        }

        // Find model path - discover from installed versions
        std::string model_path, vocab_path;
        if (home) {
            std::string base = std::string(home) +
                "/.claude/plugins/cache/genomewalker-cc-soul/cc-soul";
            auto versions = find_installed_versions(base);
            for (const auto& ver : versions) {
                std::string model = base + "/" + ver + "/chitta/models/model.onnx";
                std::string vocab = base + "/" + ver + "/chitta/models/vocab.txt";
                if (access(model.c_str(), R_OK) == 0 && access(vocab.c_str(), R_OK) == 0) {
                    model_path = model;
                    vocab_path = vocab;
                    break;
                }
            }
        }

        // Try each daemon path
        for (const auto& daemon_path : daemon_paths) {
            if (access(daemon_path.c_str(), X_OK) == 0) {
                // Build argument list
                std::vector<const char*> args;
                args.push_back(daemon_path.c_str());
                args.push_back("daemon");
                args.push_back("--socket");

                std::string path_arg, model_arg, vocab_arg;
                if (!mind_path.empty()) {
                    args.push_back("--path");
                    path_arg = mind_path;
                    args.push_back(path_arg.c_str());
                }
                if (!model_path.empty() && !vocab_path.empty()) {
                    args.push_back("--model");
                    model_arg = model_path;
                    args.push_back(model_arg.c_str());
                    args.push_back("--vocab");
                    vocab_arg = vocab_path;
                    args.push_back(vocab_arg.c_str());
                }
                args.push_back(nullptr);

                execv(daemon_path.c_str(), const_cast<char* const*>(args.data()));
                // If execv returns, it failed - try next path
            }
        }

        // All exec attempts failed
        _exit(1);
    }

    // Parent process - don't wait for child (it's a daemon)
    // Small delay to let daemon start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return true;
}

bool SocketClient::wait_for_socket(int timeout_ms) {
    auto start = std::chrono::steady_clock::now();

    while (true) {
        // Check if socket file exists
        if (access(socket_path_.c_str(), F_OK) == 0) {
            // Try to connect
            if (connect()) {
                disconnect();  // Will reconnect in caller
                return true;
            }
        }

        // Check timeout
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_ms) {
            return false;
        }

        // Wait a bit before retrying
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

std::optional<std::string> SocketClient::request(const std::string& json_rpc) {
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

        // Check for complete message (newline)
        size_t pos = response.find('\n');
        if (pos != std::string::npos) {
            return response.substr(0, pos);
        }
    }
}

int SocketClient::acquire_daemon_lock() {
    int lock_fd = open(DAEMON_LOCK_PATH, O_CREAT | O_RDWR, 0600);
    if (lock_fd < 0) {
        std::cerr << "[socket_client] Failed to open lock file: " << strerror(errno) << "\n";
        return -1;
    }

    // Try to acquire exclusive lock with timeout
    struct flock fl;
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;  // Lock entire file

    // Try non-blocking first
    if (fcntl(lock_fd, F_SETLK, &fl) == 0) {
        return lock_fd;  // Got lock immediately
    }

    // Lock held by another process - wait briefly (another process is starting daemon)
    std::cerr << "[socket_client] Waiting for daemon lock...\n";

    // Use blocking lock with timeout via alarm (POSIX way)
    // Or just sleep and retry a few times
    for (int i = 0; i < 50; ++i) {  // 5 seconds max (50 * 100ms)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (fcntl(lock_fd, F_SETLK, &fl) == 0) {
            return lock_fd;  // Got lock
        }

        // Check if socket appeared (another process started daemon)
        if (access(socket_path_.c_str(), F_OK) == 0) {
            close(lock_fd);
            return -1;  // Daemon was started by another process
        }
    }

    std::cerr << "[socket_client] Lock timeout - proceeding anyway\n";
    close(lock_fd);
    return -1;
}

void SocketClient::release_daemon_lock(int lock_fd) {
    if (lock_fd >= 0) {
        // Release lock (closing file releases the lock)
        close(lock_fd);
    }
}

} // namespace chitta
