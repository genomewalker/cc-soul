#include <chitta/socket_client.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
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

}  // anonymous namespace

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
    // Try to connect first
    if (connect()) {
        return true;
    }

    // Socket doesn't exist or daemon not running - start it
    if (!start_daemon()) {
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

} // namespace chitta
