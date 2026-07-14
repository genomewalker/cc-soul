// Daemon lifecycle implementation.
// Extracted from simple_cli.cpp.

#include <chitta/daemon_lifecycle.hpp>
#include <chitta/socket_server.hpp>  // socket_path_for_mind, pid_path_for_mind, lock_path_for_mind
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cerrno>

namespace chitta {

bool daemonize(const std::string& log_path) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid > 0) _exit(0);

    if (setsid() < 0) return false;

    pid = fork();
    if (pid < 0) return false;
    if (pid > 0) _exit(0);

    umask(077);
    chdir("/");

    int null_fd = open("/dev/null", O_RDONLY);
    if (null_fd >= 0) {
        if (dup2(null_fd, STDIN_FILENO) < 0) {
            int err_fd = open("/tmp/chittad-daemonize.err", O_WRONLY | O_CREAT | O_APPEND, 0600);
            if (err_fd >= 0) {
                const char msg[] = "dup2(STDIN) failed\n";
                [[maybe_unused]] auto _ = write(err_fd, msg, sizeof(msg) - 1);
                close(err_fd);
            }
            close(null_fd);
            return false;
        }
        close(null_fd);
    }

    const char* out_path = log_path.empty() ? "/dev/null" : log_path.c_str();
    int log_fd = open(out_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd >= 0) {
        if (dup2(log_fd, STDOUT_FILENO) < 0 || dup2(log_fd, STDERR_FILENO) < 0) {
            int err_fd = open("/tmp/chittad-daemonize.err", O_WRONLY | O_CREAT | O_APPEND, 0600);
            if (err_fd >= 0) {
                const char msg[] = "dup2(STDOUT/STDERR) failed\n";
                [[maybe_unused]] auto _ = write(err_fd, msg, sizeof(msg) - 1);
                close(err_fd);
            }
            close(log_fd);
            return false;
        }
        close(log_fd);
    }
    return true;
}

bool acquire_lock(const std::string& mind_path, DaemonLock& lock) {
    lock.path = lock_path_for_mind(mind_path);
    lock.fd = open(lock.path.c_str(), O_CREAT | O_RDWR, 0600);
    if (lock.fd < 0) return false;

    struct flock fl{};
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    if (fcntl(lock.fd, F_SETLK, &fl) != 0) {
        close(lock.fd);
        lock.fd = -1;
        return false;
    }

    std::string pid_str = std::to_string(getpid()) + "\n";
    if (ftruncate(lock.fd, 0) < 0)
        std::cerr << "[daemon] Warning: ftruncate lock file failed: " << strerror(errno) << "\n";
    if (write(lock.fd, pid_str.data(), pid_str.size()) < 0)
        std::cerr << "[daemon] Warning: write PID to lock file failed: " << strerror(errno) << "\n";
    return true;
}

void release_lock(DaemonLock& lock) {
    if (lock.fd >= 0) {
        // Close, never unlink. An fcntl lock lives on the INODE, not the path: deleting the
        // file lets the next daemon open a fresh inode and lock it successfully while this
        // one still holds its own — two "exclusive" owners of one store, whose janitors then
        // prune each other's snapshot sidecars (the 2026-07-14 store loss). The kernel drops
        // the lock on close and on death, so a leftover lock file is harmless: the next
        // daemon re-locks the same inode cleanly.
        close(lock.fd);
        lock.fd = -1;
    }
}

bool is_pid_alive(pid_t pid) {
    if (kill(pid, 0) != 0 && errno != EPERM) return false;
    std::string status_path = "/proc/" + std::to_string(pid) + "/status";
    std::ifstream sf(status_path);
    if (!sf) return false;
    std::string line;
    while (std::getline(sf, line)) {
        if (line.rfind("State:", 0) == 0)
            return line.find(" Z ") == std::string::npos && line.find("\tZ ") == std::string::npos;
    }
    return true;
}

bool socket_responds(const std::string& path) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return true;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    bool active = (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
    int err = errno;
    ::close(fd);
    if (active) return true;
    return err != ECONNREFUSED && err != ENOENT && err != EACCES;
}

bool cleanup_stale_daemon(const std::string& mind_path) {
    std::string pid_path  = pid_path_for_mind(mind_path);
    std::string sock_path = socket_path_for_mind(mind_path);
    std::string lk_path   = lock_path_for_mind(mind_path);

    std::ifstream pf(pid_path);
    if (!pf) return true;
    pid_t old_pid;
    if (!(pf >> old_pid)) return true;
    pf.close();

    if (is_pid_alive(old_pid)) {
        if (socket_responds(sock_path)) return false;
        std::cerr << "[daemon] Hung daemon (PID " << old_pid << "), sending SIGKILL\n";
        kill(old_pid, SIGKILL);
        for (int i = 0; i < 20; ++i) {
            usleep(100000);
            if (!is_pid_alive(old_pid)) break;
        }
    }

    std::cerr << "[daemon] Cleaning stale files from dead PID " << old_pid << "\n";
    // Socket and PID file are safe to remove: both are recreated on start, and the socket
    // MUST be unlinked to rebind. The lock file is NOT — it is the single-writer mutex, and
    // it is keyed by inode. Unlinking it here (before acquire_lock, simple_cli.cpp:1588) let
    // a second daemon create a fresh inode, lock that, and run alongside a live daemon that
    // still holds the old one. Leave the inode alone; fcntl already releases on death.
    unlink(sock_path.c_str());
    unlink(pid_path.c_str());
    (void)lk_path;
    return true;
}

} // namespace chitta
