#pragma once
// Daemon lifecycle: process daemonization, PID/lock file management,
// stale process detection and cleanup.
// Extracted from simple_cli.cpp.

#include <string>
#include <atomic>

namespace chitta {

struct DaemonLock {
    int fd = -1;
    std::string path;
};

bool daemonize(const std::string& log_path);
bool acquire_lock(const std::string& mind_path, DaemonLock& lock);
void release_lock(DaemonLock& lock);
bool is_pid_alive(pid_t pid);
bool socket_responds(const std::string& path);
bool cleanup_stale_daemon(const std::string& mind_path);

} // namespace chitta
