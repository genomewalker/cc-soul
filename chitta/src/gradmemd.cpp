// gradmemd — GradMem write subprocess launcher.
//
// This C++ binary is a thin launcher:
//   1. Reads a JSON job from stdin
//   2. Adds model_path from argv[1] if not already set
//   3. exec's python3 <script> which does the actual ML work
//      (TorchScript export of modern HF models is too fragile for libtorch)
//
// Build: does NOT require CHITTA_WITH_TORCH — pure C++11.
//
// Usage:
//   echo '{"text":...}' | gradmemd [python_script] [model_path]
//
// Or called by GradMemWriter::write_sync/write_async with the job JSON.

#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>

using json = nlohmann::json;

// Default paths (overridden by CLI args or job JSON)
static const char* DEFAULT_SCRIPT =
    "/maps/projects/fernandezguerra/apps/repos/cc-soul/scripts/gradmem/gradmemd.py";

int main(int argc, char* argv[]) {
    // Optional: gradmemd <script_path> <model_path>
    std::string script_path = argc > 1 ? argv[1] : DEFAULT_SCRIPT;
    std::string model_path  = argc > 2 ? argv[2] : "";

    // Read JSON job from stdin
    std::string input_str(
        (std::istreambuf_iterator<char>(std::cin)),
        std::istreambuf_iterator<char>()
    );

    if (input_str.empty()) {
        std::cerr << R"({"error":"empty stdin"})" << "\n";
        return 1;
    }

    // Inject model_path into job if not already set
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
        json err = {{"error", "gradmemd.py not found at: " + script_path}};
        std::cout << err.dump() << "\n";
        return 1;
    }

    // Find python3 with torch — try conda bioinfo env first, then PATH
    const char* python_candidates[] = {
        "/maps/projects/fernandezguerra/apps/opt/conda/envs/bioinfo/bin/python3",
        "/usr/bin/python3",
        "python3",
        nullptr
    };
    std::string python_bin;
    for (int i = 0; python_candidates[i]; i++) {
        struct stat ps{};
        if (stat(python_candidates[i], &ps) == 0 || python_candidates[i][0] != '/') {
            python_bin = python_candidates[i];
            break;
        }
    }
    if (python_bin.empty()) {
        json err = {{"error", "python3 not found"}};
        std::cout << err.dump() << "\n";
        return 1;
    }

    // Write modified job to a temp file (stdin pipe to exec'd process is complex)
    char tmp_path[] = "/tmp/gradmemd_job_XXXXXX";
    int fd = mkstemp(tmp_path);
    if (fd < 0) {
        json err = {{"error", "mkstemp failed"}};
        std::cout << err.dump() << "\n";
        return 1;
    }
    write(fd, input_str.c_str(), input_str.size());
    close(fd);

    // Build command: python3 gradmemd.py < job.json
    // Use sh -c to handle stdin redirection
    std::string cmd = python_bin + " " + script_path + " < " + tmp_path;

    // Ensure torch libs are findable (no CUDA dependency — CPU-only inference)
    std::string torch_lib = "/maps/projects/fernandezguerra/apps/opt/conda/envs/bioinfo"
                            "/lib/python3.12/site-packages/torch/lib";
    const char* existing_ld = getenv("LD_LIBRARY_PATH");
    std::string ld_path = torch_lib;
    if (existing_ld && existing_ld[0]) {
        ld_path += ":";
        ld_path += existing_ld;
    }
    setenv("LD_LIBRARY_PATH", ld_path.c_str(), 1);
    setenv("HF_HUB_OFFLINE", "1", 1);

    int rc = system(cmd.c_str());
    unlink(tmp_path);
    return WEXITSTATUS(rc);
}
