// gradmemd — GradMem write subprocess launcher.
//
// Reads a JSON job from stdin, injects model_path if provided via argv[2],
// exec's python3 <script> (argv[1]) with stdin piped from the job JSON.
//
// Paths are configurable via env vars (set by GradMemWriter before exec):
//   GRADMEM_PYTHON   — python3 binary path
//   GRADMEM_TORCH_LIB — extra LD_LIBRARY_PATH prepend for torch libs
//   HF_HUB_OFFLINE   — set to 1 to prevent HF network calls
//
// Build: no libtorch dependency — pure C++20.

#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>

using json = nlohmann::json;

static const char* DEFAULT_SCRIPT_BASENAME = "gradmemd.py";

static std::string find_python() {
    // 1. Honour env var (set by GradMemWriter from GradMemConfig::python_bin)
    if (const char* p = getenv("GRADMEM_PYTHON")) {
        struct stat st{};
        if (stat(p, &st) == 0) return p;
    }
    // 2. Common installation paths
    const char* candidates[] = {
        "/maps/projects/fernandezguerra/apps/opt/conda/envs/bioinfo/bin/python3",
        "/usr/bin/python3",
        nullptr
    };
    for (int i = 0; candidates[i]; i++) {
        struct stat st{};
        if (stat(candidates[i], &st) == 0) return candidates[i];
    }
    // 3. Fall back to PATH lookup
    return "python3";
}

int main(int argc, char* argv[]) {
    std::string script_path = argc > 1 ? argv[1] : "";
    std::string model_path  = argc > 2 ? argv[2] : "";

    // If no script_path given, look next to this binary
    if (script_path.empty()) {
        // derive from argv[0] sibling
        std::string self = argv[0];
        auto slash = self.rfind('/');
        std::string dir = (slash != std::string::npos) ? self.substr(0, slash + 1) : "./";
        script_path = dir + DEFAULT_SCRIPT_BASENAME;
    }

    // Read JSON job from stdin
    std::string input_str(
        (std::istreambuf_iterator<char>(std::cin)),
        std::istreambuf_iterator<char>()
    );
    if (input_str.empty()) {
        std::cout << R"({"error":"empty stdin"})" << "\n";
        return 1;
    }

    // Inject model_path into job if provided and missing
    if (!model_path.empty()) {
        try {
            auto job = json::parse(input_str);
            if (!job.contains("model_path") || job["model_path"].empty()) {
                job["model_path"] = model_path;
                input_str = job.dump();
            }
        } catch (...) {}
    }

    // Verify script exists
    struct stat st{};
    if (stat(script_path.c_str(), &st) != 0) {
        json err = {{"error", "gradmemd.py not found: " + script_path}};
        std::cout << err.dump() << "\n";
        return 1;
    }

    std::string python_bin = find_python();

    // Set up LD_LIBRARY_PATH for torch libs
    if (const char* tl = getenv("GRADMEM_TORCH_LIB")) {
        std::string ld_path = tl;
        if (const char* existing = getenv("LD_LIBRARY_PATH")) {
            ld_path += ":";
            ld_path += existing;
        }
        setenv("LD_LIBRARY_PATH", ld_path.c_str(), 1);
    }
    setenv("HF_HUB_OFFLINE", "1", 0);  // 0 = don't overwrite if already set

    // Create a pipe to feed job JSON as stdin to python3
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        json err = {{"error", "pipe() failed"}};
        std::cout << err.dump() << "\n";
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        json err = {{"error", "fork() failed"}};
        std::cout << err.dump() << "\n";
        return 1;
    }

    if (pid == 0) {
        // Child: read end of pipe becomes stdin
        dup2(pipe_fds[0], STDIN_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        execl(python_bin.c_str(), "python3", script_path.c_str(), nullptr);
        // execl only returns on failure
        const char* err_msg = R"({"error":"execl python3 failed"})";
        write(STDOUT_FILENO, err_msg, strlen(err_msg));
        _exit(1);
    }

    // Parent: write job JSON to write end, then close it (signals EOF to child)
    close(pipe_fds[0]);
    const char* data = input_str.c_str();
    size_t remaining = input_str.size();
    while (remaining > 0) {
        ssize_t n = write(pipe_fds[1], data, remaining);
        if (n <= 0) break;
        data += n;
        remaining -= n;
    }
    close(pipe_fds[1]);

    // Wait for child and forward its exit code
    int wstatus = 0;
    waitpid(pid, &wstatus, 0);
    return WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 1;
}
