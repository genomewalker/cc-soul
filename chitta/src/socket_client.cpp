#include <chitta/socket_client.hpp>
#include <chitta/version.hpp>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
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
#include <fstream>
#include <vector>
#include <algorithm>
#include <glob.h>
#include <sstream>

namespace chitta {

namespace {

// PID file location for daemon process tracking
constexpr const char* DAEMON_PID_FILE = "/tmp/chitta-daemon.pid";

// Find all chitta daemon processes (returns PIDs)
std::vector<pid_t> find_daemon_pids() {
    std::vector<pid_t> pids;

    // Read from PID file first
    std::ifstream pid_file(DAEMON_PID_FILE);
    if (pid_file) {
        pid_t pid;
        if (pid_file >> pid) {
            // Verify process is actually chitta
            std::string cmdline_path = "/proc/" + std::to_string(pid) + "/cmdline";
            std::ifstream cmdline(cmdline_path);
            if (cmdline) {
                std::string cmd;
                std::getline(cmdline, cmd, '\0');
                if (cmd.find("chitta") != std::string::npos) {
                    pids.push_back(pid);
                }
            }
        }
    }

    // Also check /proc for any chittad daemon processes we might have missed
    DIR* proc = opendir("/proc");
    if (proc) {
        while (struct dirent* entry = readdir(proc)) {
            if (entry->d_type != DT_DIR) continue;

            // Check if directory name is a number (PID)
            char* end;
            pid_t pid = strtol(entry->d_name, &end, 10);
            if (*end != '\0' || pid <= 0) continue;

            // Skip if already found
            if (std::find(pids.begin(), pids.end(), pid) != pids.end()) continue;

            // Check cmdline
            std::string cmdline_path = "/proc/" + std::string(entry->d_name) + "/cmdline";
            std::ifstream cmdline(cmdline_path);
            if (cmdline) {
                std::string cmd;
                std::getline(cmdline, cmd, '\0');
                if (cmd.find("chittad") != std::string::npos) {
                    // Check if it's running as daemon
                    std::string full_cmdline;
                    cmdline.seekg(0);
                    char c;
                    while (cmdline.get(c)) {
                        full_cmdline += (c == '\0') ? ' ' : c;
                    }
                    if (full_cmdline.find("daemon") != std::string::npos) {
                        pids.push_back(pid);
                    }
                }
            }
        }
        closedir(proc);
    }

    return pids;
}

// Kill a process gracefully, then forcefully if needed
bool kill_process(pid_t pid, int timeout_ms = 3000) {
    // Send SIGTERM first
    if (kill(pid, SIGTERM) != 0) {
        return errno == ESRCH;  // Already dead
    }

    // Wait for process to exit
    auto start = std::chrono::steady_clock::now();
    while (true) {
        if (kill(pid, 0) != 0 && errno == ESRCH) {
            return true;  // Process exited
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_ms) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Force kill
    std::cerr << "[socket_client] Process " << pid << " didn't respond to SIGTERM, sending SIGKILL\n";
    kill(pid, SIGKILL);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return kill(pid, 0) != 0;
}

// Terminate all running chitta daemons and clean up sockets
void terminate_all_daemons() {
    // Kill all daemon processes
    auto pids = find_daemon_pids();
    for (pid_t pid : pids) {
        std::cerr << "[socket_client] Terminating old daemon (pid=" << pid << ")\n";
        kill_process(pid);
    }

    // Remove PID file
    unlink(DAEMON_PID_FILE);

    // Remove all chitta sockets
    glob_t globbuf;
    if (glob("/tmp/chitta*.sock", 0, nullptr, &globbuf) == 0) {
        for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
            std::cerr << "[socket_client] Removing socket: " << globbuf.gl_pathv[i] << "\n";
            unlink(globbuf.gl_pathv[i]);
        }
        globfree(&globbuf);
    }

    // Small delay to ensure cleanup is complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// Get the daemon binary path - stable symlink only
std::string get_versioned_daemon_path() {
    const char* home = getenv("HOME");
    if (!home) return "";

    std::string path = std::string(home) + "/.claude/bin/chittad";
    if (access(path.c_str(), X_OK) == 0) {
        return path;
    }

    return "";
}

// Get model/vocab paths - stable location only
std::pair<std::string, std::string> get_model_paths() {
    const char* home = getenv("HOME");
    if (!home) return {"", ""};

    std::string base = std::string(home) + "/.claude/models";
    std::string model = base + "/model.onnx";
    std::string vocab = base + "/vocab.txt";

    if (access(model.c_str(), R_OK) == 0 && access(vocab.c_str(), R_OK) == 0) {
        return {model, vocab};
    }

    return {"", ""};
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

}  // anonymous namespace

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
    // First, try to connect to existing daemon at the standard path
    socket_path_ = default_socket_path();
    if (connect()) {
        // Connected - check version compatibility
        auto version = check_version();
        if (version) {
            bool compatible = chitta::version::protocol_compatible(
                version->protocol_major, version->protocol_minor);

            if (compatible) {
                // Successfully connected - no verbose output needed
                return true;
            }

            // Incompatible version - need to restart
            std::cerr << "[socket_client] Daemon v" << version->software
                      << " incompatible with client v" << CHITTA_VERSION
                      << " - forcing restart\n";
            disconnect();
        } else {
            // Version check failed - old daemon without version support
            std::cerr << "[socket_client] Daemon doesn't support version check - forcing restart\n";
            disconnect();
        }

        // Force terminate old daemon(s) and clean up all sockets
        terminate_all_daemons();
    }

    // Also check for old versioned sockets and clean them up
    auto old_sockets = find_chitta_sockets();
    if (!old_sockets.empty()) {
        // Silently clean up old sockets
        terminate_all_daemons();
    }

    // Acquire lock before starting daemon (prevents race with other clients)
    int lock_fd = acquire_daemon_lock();

    // Re-check after acquiring lock (another process may have started daemon)
    socket_path_ = default_socket_path();
    if (connect()) {
        auto version = check_version();
        if (version && chitta::version::protocol_compatible(
                version->protocol_major, version->protocol_minor)) {
            release_daemon_lock(lock_fd);
            return true;
        }
        disconnect();
        // Still incompatible - clean up again
        terminate_all_daemons();
    }

    // Start new daemon with version-matched binary
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
    // Get version-matched daemon binary
    std::string daemon_path = get_versioned_daemon_path();
    if (daemon_path.empty()) {
        last_error_ = "Could not find chitta daemon binary for version " + std::string(CHITTA_VERSION);
        std::cerr << "[socket_client] " << last_error_ << "\n";
        return false;
    }

    pid_t pid = fork();

    if (pid < 0) {
        last_error_ = std::string("fork() failed: ") + strerror(errno);
        return false;
    }

    if (pid == 0) {
        // Child process - exec daemon

        // Detach from parent process group (become daemon)
        setsid();

        // Redirect stdout/stderr to log file
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

        // Find mind path
        std::string mind_path;
        if (const char* db_path = getenv("CHITTA_DB_PATH")) {
            mind_path = db_path;
        } else if (home) {
            mind_path = std::string(home) + "/.claude/mind/chitta";
        }

        // Get model paths (version-matched)
        auto [model_path, vocab_path] = get_model_paths();

        // Build argument list
        std::vector<const char*> args;
        args.push_back(daemon_path.c_str());
        args.push_back("daemon");
        args.push_back("--socket");

        // PID file for tracking
        args.push_back("--pid-file");
        args.push_back(DAEMON_PID_FILE);

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

        // If execv returns, it failed
        std::cerr << "[socket_client] execv failed: " << strerror(errno) << "\n";
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

int SocketClient::acquire_daemon_lock() {
    int lock_fd = open(default_lock_path().c_str(), O_CREAT | O_RDWR, 0600);
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

bool SocketClient::connect_only() {
    // Safe connect: never kill or start daemons
    // Use this for parallel agents to avoid killing shared daemon

    socket_path_ = default_socket_path();

    if (!connect()) {
        last_error_ = "Cannot connect to daemon at " + socket_path_ + " - is daemon running?";
        return false;
    }

    // Check version compatibility
    auto version = check_version();
    if (!version) {
        last_error_ = "Daemon does not support version check - may need restart (but connect_only won't do it)";
        disconnect();
        return false;
    }

    bool compatible = chitta::version::protocol_compatible(
        version->protocol_major, version->protocol_minor);

    if (!compatible) {
        last_error_ = "Daemon v" + version->software + " incompatible with client v" +
                      std::string(CHITTA_VERSION) + " - restart daemon manually";
        disconnect();
        return false;
    }

    return true;
}

} // namespace chitta
