#pragma once
// llm_http: Shared GPU endpoint discovery + HTTP LLM call utility
//
// Discovery chain (mirrors chitta-bridge gpu_serve.py):
// 1. Cached /tmp/ollama-server-*.url files (written by chitta-gpu start)
// 2. Probe SLURM GPU nodes (squeue → http://<node>:11434)
// 3. Probe localhost:11434
// 4. Invoke `chitta-gpu start <model>` as last resort
//
// HTTP call: POST /v1/chat/completions (OpenAI-compatible)

#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>
#include <array>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <chrono>
#include <thread>

namespace chitta {

using LogFn = std::function<void(const std::string&)>;

// Sanitize invalid UTF-8 bytes for safe JSON transport
inline std::string sanitize_utf8(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (c < 0x80) {
            if (c < 0x20 && c != '\n' && c != '\r' && c != '\t')
                out += ' ';
            else
                out += static_cast<char>(c);
            ++i;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < input.size() &&
                   (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80) {
            out += input[i]; out += input[i+1]; i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < input.size() &&
                   (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80 &&
                   (static_cast<unsigned char>(input[i+2]) & 0xC0) == 0x80) {
            out += input[i]; out += input[i+1]; out += input[i+2]; i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < input.size() &&
                   (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80 &&
                   (static_cast<unsigned char>(input[i+2]) & 0xC0) == 0x80 &&
                   (static_cast<unsigned char>(input[i+3]) & 0xC0) == 0x80) {
            out += input[i]; out += input[i+1]; out += input[i+2]; out += input[i+3]; i += 4;
        } else {
            out += ' ';
            ++i;
        }
    }
    return out;
}

// Fork/exec a command, capture stdout, return output (with timeout)
inline std::string fork_exec_capture(const std::vector<std::string>& args,
                                      int timeout_secs = 5,
                                      const std::string& stdin_data = "") {
    int stdout_pipe[2];
    if (pipe(stdout_pipe) < 0) return "";

    int stdin_pipe[2] = {-1, -1};
    if (!stdin_data.empty() && pipe(stdin_pipe) < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return "";
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        if (stdin_pipe[0] >= 0) { close(stdin_pipe[0]); close(stdin_pipe[1]); }
        return "";
    }

    if (pid == 0) {
        close(stdout_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        close(stdout_pipe[1]);

        if (!stdin_data.empty()) {
            close(stdin_pipe[1]);
            dup2(stdin_pipe[0], STDIN_FILENO);
            close(stdin_pipe[0]);
        }

        std::vector<char*> argv;
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(1);
    }

    close(stdout_pipe[1]);
    if (!stdin_data.empty()) {
        close(stdin_pipe[0]);
        ssize_t written = 0;
        size_t total = stdin_data.size();
        const char* data = stdin_data.data();
        while (written < static_cast<ssize_t>(total)) {
            ssize_t n = write(stdin_pipe[1], data + written, total - written);
            if (n <= 0) break;
            written += n;
        }
        close(stdin_pipe[1]);
    }

    std::string output;
    std::array<char, 4096> buf;
    auto start = std::chrono::steady_clock::now();

    int flags = fcntl(stdout_pipe[0], F_GETFL, 0);
    fcntl(stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);

    bool finished = false;
    while (!finished) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= timeout_secs) {
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
            close(stdout_pipe[0]);
            return "";
        }
        int status;
        int result = waitpid(pid, &status, WNOHANG);
        if (result != 0) finished = true;

        ssize_t n;
        while ((n = read(stdout_pipe[0], buf.data(), buf.size())) > 0)
            output.append(buf.data(), n);

        if (!finished)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ssize_t n;
    while ((n = read(stdout_pipe[0], buf.data(), buf.size())) > 0)
        output.append(buf.data(), n);
    close(stdout_pipe[0]);
    return output;
}

// Probe an Ollama endpoint for /v1/models availability
inline bool probe_endpoint(const std::string& base_url) {
    std::string url = base_url + "/v1/models";
    std::string out = fork_exec_capture(
        {"curl", "-sL", "--max-time", "3", url}, 5);
    return !out.empty() && out.find("data") != std::string::npos;
}

// Discover GPU endpoint using the full chain from gpu_serve.py
// Returns base URL (e.g., "http://node:11434") or empty string
inline std::string discover_gpu_endpoint(const std::string& model = "gemma4:26b",
                                          LogFn log_fn = nullptr) {
    auto log = [&](const std::string& msg) {
        if (log_fn) log_fn(msg);
    };

    // 1. Cached URL files: /tmp/ollama-server-*.url
    namespace fs = std::filesystem;
    for (auto& entry : fs::directory_iterator("/tmp")) {
        std::string fname = entry.path().filename().string();
        if (fname.find("ollama-server-") == 0 && fname.size() > 4 &&
            fname.substr(fname.size() - 4) == ".url") {
            std::ifstream ifs(entry.path());
            std::string url;
            std::getline(ifs, url);
            if (!url.empty()) {
                // Trim trailing whitespace/newline
                while (!url.empty() && (url.back() == '\n' || url.back() == '\r' || url.back() == ' '))
                    url.pop_back();
                if (probe_endpoint(url)) {
                    log("[llm] Found cached endpoint: " + url);
                    return url;
                }
            }
        }
    }

    // 2. Probe SLURM GPU nodes via squeue
    std::string squeue_out = fork_exec_capture(
        {"squeue", "--me", "--noheader", "--format=%j %T %N"}, 5);
    if (!squeue_out.empty()) {
        std::istringstream iss(squeue_out);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.find("ollama-") == std::string::npos) continue;
            // Parse: job_name state node
            std::istringstream parts(line);
            std::string name, state, node;
            parts >> name >> state >> node;
            if (state == "RUNNING" && !node.empty()) {
                std::string url = "http://" + node + ":11434";
                if (probe_endpoint(url)) {
                    log("[llm] Found SLURM endpoint: " + url + " (job " + name + ")");
                    return url;
                }
            }
        }
    }

    // 3. Probe localhost
    if (probe_endpoint("http://localhost:11434")) {
        log("[llm] Found local endpoint: http://localhost:11434");
        return "http://localhost:11434";
    }

    // 4. Last resort: invoke chitta-gpu start
    log("[llm] No endpoint found, trying chitta-gpu start " + model + "...");
    std::string gpu_out = fork_exec_capture(
        {"chitta-gpu", "start", model}, 120);
    if (!gpu_out.empty()) {
        // chitta-gpu prints export lines; parse ANTHROPIC_BASE_URL
        std::istringstream iss(gpu_out);
        std::string line;
        while (std::getline(iss, line)) {
            const std::string prefix = "export ANTHROPIC_BASE_URL=";
            if (line.find(prefix) == 0) {
                std::string url = line.substr(prefix.size());
                while (!url.empty() && (url.back() == '\n' || url.back() == '\r'))
                    url.pop_back();
                if (probe_endpoint(url)) {
                    log("[llm] Started GPU endpoint: " + url);
                    return url;
                }
            }
        }
    }

    return "";
}

// Call an OpenAI-compatible LLM via HTTP
// Returns the response content string, or empty on failure
inline std::string call_llm_http(const std::string& endpoint,
                                  const std::string& model,
                                  const std::string& prompt,
                                  const std::string& system_prompt = "",
                                  int timeout_secs = 180,
                                  float temperature = 0.3f,
                                  int max_tokens = 4096,
                                  LogFn log_fn = nullptr) {
    auto log = [&](const std::string& msg) {
        if (log_fn) log_fn(msg);
    };

    std::string safe_prompt = sanitize_utf8(prompt);
    nlohmann::json messages = nlohmann::json::array();
    if (!system_prompt.empty()) {
        messages.push_back({{"role", "system"}, {"content", system_prompt}});
    }
    messages.push_back({{"role", "user"}, {"content", safe_prompt}});

    nlohmann::json req = {
        {"model", model},
        {"messages", messages},
        {"temperature", temperature},
        {"max_tokens", max_tokens}
    };
    std::string body = req.dump(-1, ' ', true);

    std::string tmp_path = "/tmp/chitta-llm-" + std::to_string(getpid()) + ".json";
    {
        std::ofstream ofs(tmp_path, std::ios::binary);
        ofs << body;
    }

    std::string url = endpoint + "/v1/chat/completions";
    std::string timeout_str = std::to_string(timeout_secs);
    std::string output = fork_exec_capture(
        {"curl", "-sL", "--max-time", timeout_str,
         "-H", "Content-Type: application/json",
         "-d", "@" + tmp_path, url},
        timeout_secs + 5);

    std::remove(tmp_path.c_str());

    try {
        auto resp = nlohmann::json::parse(output);
        if (resp.contains("choices") && !resp["choices"].empty()) {
            return resp["choices"][0]["message"]["content"].get<std::string>();
        }
        if (resp.contains("error")) {
            log("[llm] Error: " + resp["error"].value("message", "unknown"));
        }
    } catch (...) {
        log("[llm] Failed to parse response (" + std::to_string(output.size()) + " bytes)");
    }
    return "";
}

} // namespace chitta
