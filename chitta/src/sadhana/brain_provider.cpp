// BrainProvider: LLM interface implementations
//
// Uses fork/exec to run CLI tools with timeout handling.

#include <chitta/sadhana/brain_provider.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <chrono>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>

namespace chitta {

namespace {

// Execute a command with timeout, capturing stdout/stderr
BrainResult execute_with_timeout(
    const std::vector<std::string>& args,
    const std::string& stdin_data,
    int timeout_ms,
    const std::string& working_dir = "")
{
    BrainResult result;
    auto start_time = std::chrono::steady_clock::now();

    // Create pipes for stdin, stdout, stderr
    int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        result.error = "Failed to create pipes: " + std::string(strerror(errno));
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        result.error = "Fork failed: " + std::string(strerror(errno));
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return result;
    }

    if (pid == 0) {
        // Child process
        close(stdin_pipe[1]);  // Close write end of stdin
        close(stdout_pipe[0]); // Close read end of stdout
        close(stderr_pipe[0]); // Close read end of stderr

        // Redirect stdin/stdout/stderr
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        // Change directory if specified
        if (!working_dir.empty()) {
            if (chdir(working_dir.c_str()) < 0) {
                std::cerr << "chdir failed: " << strerror(errno) << "\n";
                _exit(1);
            }
        }

        // Unset CLAUDECODE to allow nested Claude Code sessions
        unsetenv("CLAUDECODE");

        // Build argv
        std::vector<char*> argv;
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        // Execute
        execvp(argv[0], argv.data());
        std::cerr << "execvp failed: " << strerror(errno) << "\n";
        _exit(1);
    }

    // Parent process
    close(stdin_pipe[0]);  // Close read end of stdin
    close(stdout_pipe[1]); // Close write end of stdout
    close(stderr_pipe[1]); // Close write end of stderr

    // Write stdin data
    if (!stdin_data.empty()) {
        ssize_t written = write(stdin_pipe[1], stdin_data.c_str(), stdin_data.size());
        if (written < 0) {
            std::cerr << "[brain] stdin write error: " << strerror(errno) << "\n";
        }
    }
    close(stdin_pipe[1]);  // Signal EOF

    // Set non-blocking on stdout/stderr
    fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

    // Read output with timeout
    std::string stdout_data, stderr_data;
    char buffer[4096];
    bool timed_out = false;

    struct pollfd fds[2] = {
        {stdout_pipe[0], POLLIN, 0},
        {stderr_pipe[0], POLLIN, 0}
    };

    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();

        if (elapsed >= timeout_ms) {
            timed_out = true;
            break;
        }

        int remaining = timeout_ms - static_cast<int>(elapsed);
        int ret = poll(fds, 2, remaining);

        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (ret == 0) {
            timed_out = true;
            break;
        }

        // Read stdout
        if (fds[0].revents & POLLIN) {
            ssize_t n = read(stdout_pipe[0], buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = '\0';
                stdout_data += buffer;
            }
        }

        // Read stderr
        if (fds[1].revents & POLLIN) {
            ssize_t n = read(stderr_pipe[0], buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = '\0';
                stderr_data += buffer;
            }
        }

        // Check if both pipes closed
        if ((fds[0].revents & POLLHUP) && (fds[1].revents & POLLHUP)) {
            // Drain remaining data
            while (true) {
                ssize_t n = read(stdout_pipe[0], buffer, sizeof(buffer) - 1);
                if (n <= 0) break;
                buffer[n] = '\0';
                stdout_data += buffer;
            }
            while (true) {
                ssize_t n = read(stderr_pipe[0], buffer, sizeof(buffer) - 1);
                if (n <= 0) break;
                buffer[n] = '\0';
                stderr_data += buffer;
            }
            break;
        }
    }

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    // Handle process termination
    int status = 0;
    if (timed_out) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        result.error = "Timeout after " + std::to_string(timeout_ms) + "ms";
        result.exit_code = -1;
    } else {
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            result.exit_code = WEXITSTATUS(status);
            result.success = (result.exit_code == 0 ||
                              result.exit_code == 10 ||   // achieved
                              result.exit_code == 20);    // blocked (not an error)
        } else if (WIFSIGNALED(status)) {
            result.exit_code = -WTERMSIG(status);
            result.error = "Killed by signal " + std::to_string(WTERMSIG(status));
        }
    }

    result.output = stdout_data;
    if (!stderr_data.empty() && result.error.empty()) {
        result.error = stderr_data;
    }

    auto end_time = std::chrono::steady_clock::now();
    result.duration_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count()
    );

    return result;
}

}  // anonymous namespace

// ClaudeBrain implementation
// Runs: claude --dangerously-skip-permissions --model <model> --max-turns N [--system <sys>] -p <prompt>
BrainResult ClaudeBrain::think(const std::string& prompt, const BrainConfig& config) {
    int turns = config.max_turns > 0 ? config.max_turns : 20;

    std::vector<std::string> args = {
        claude_path_,
        "--allowedTools", "Bash,Edit,Write,Read,Glob,Grep",
        "--model", model_,
        "--max-turns", std::to_string(turns),
        "--output-format", "json"
    };

    if (!config.system_prompt.empty()) {
        args.push_back("--system-prompt");
        args.push_back(config.system_prompt);
    }

    for (const auto& arg : config.extra_args) {
        args.push_back(arg);
    }

    // -p triggers non-interactive print mode with full tool access
    args.push_back("-p");
    args.push_back(prompt);

    auto result = execute_with_timeout(args, "", config.timeout_ms, config.working_dir);

    // Parse the JSON wrapper from --output-format json to extract cost/turns,
    // then replace output with the inner result text for downstream parsing.
    if (!result.output.empty()) {
        try {
            auto j = nlohmann::json::parse(result.output);
            result.cost_usd  = j.value("cost_usd",  0.0);
            result.num_turns = j.value("num_turns",  0);
            if (j.contains("result") && j["result"].is_string()) {
                result.output = j["result"].get<std::string>();
            }
            if (j.value("is_error", false) && result.exit_code == 0) {
                result.exit_code = 1;
            }
        } catch (...) {
            // Not a JSON wrapper (old claude version or crash) — use raw output as-is
        }
    }

    return result;
}

// OpenCodeBrain implementation
BrainResult OpenCodeBrain::think(const std::string& prompt, const BrainConfig& config) {
    std::vector<std::string> args = {
        opencode_path_,
        "run",
        "-m", model_,
        "--print-logs"
    };

    for (const auto& arg : config.extra_args) {
        args.push_back(arg);
    }

    args.push_back(prompt);

    return execute_with_timeout(args, "", config.timeout_ms, config.working_dir);
}

}  // namespace chitta
