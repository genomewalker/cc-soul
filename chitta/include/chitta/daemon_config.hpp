#pragma once
// Daemon configuration structs and default path resolution.
// Extracted from simple_cli.cpp to allow other translation units to reference
// daemon config types without pulling in the full daemon implementation.

#include <string>
#include <fstream>
#include <cstdlib>
#include <cstdint>

namespace chitta {

struct DistillConfig {
    int interval_minutes = 15;
    int min_turns = 4;
    std::string script_path;
    std::string model = "github-copilot/gpt-5-mini";
    std::string local_model_path;
    bool enabled = true;
    int64_t token_trigger_chars = 120000;
    int cooldown_seconds = 180;
};

struct EnrichConfig {
    int interval_minutes = 10;
    int batch_size = 3;
    int idle_seconds = 30;
    std::string script_path;
    std::string model = "github-copilot/gpt-5-mini";
    bool enabled = true;
};

inline std::string default_mind_path() {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.claude/mind";
}

inline std::string default_model_path() {
    const char* home = std::getenv("HOME");
    std::string home_str = home ? home : ".";
    if (const char* pr = std::getenv("CLAUDE_PLUGIN_ROOT")) {
        std::string p = std::string(pr) + "/chitta/models/model.onnx";
        if (std::ifstream(p).good()) return p;
    }
    std::string models_path = home_str + "/.claude/models/model.onnx";
    if (std::ifstream(models_path).good()) return models_path;
    return home_str + "/.claude/mind/model.onnx";
}

inline std::string default_vocab_path() {
    const char* home = std::getenv("HOME");
    std::string home_str = home ? home : ".";
    if (const char* pr = std::getenv("CLAUDE_PLUGIN_ROOT")) {
        std::string p = std::string(pr) + "/chitta/models/vocab.txt";
        if (std::ifstream(p).good()) return p;
    }
    std::string models_path = home_str + "/.claude/models/vocab.txt";
    if (std::ifstream(models_path).good()) return models_path;
    return home_str + "/.claude/mind/vocab.txt";
}

inline std::string default_distill_script() {
    if (const char* pr = std::getenv("CLAUDE_PLUGIN_ROOT")) {
        std::string p = std::string(pr) + "/scripts/distill.sh";
        if (std::ifstream(p).good()) return p;
    }
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.claude/hooks/distill.sh";
}

inline std::string default_enrich_script() {
    if (const char* pr = std::getenv("CLAUDE_PLUGIN_ROOT")) {
        std::string p = std::string(pr) + "/scripts/enrich.sh";
        if (std::ifstream(p).good()) return p;
    }
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.claude/hooks/enrich.sh";
}

} // namespace chitta
