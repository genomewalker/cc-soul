#pragma once
// BrainProvider: LLM interface for autonomous agents
//
// Provides a uniform interface for interacting with different LLM backends:
// - ClaudeBrain: Uses `claude --dangerously-skip-permissions --max-turns N -p`
// - LocalBrain: Uses HTTP to Ollama/vLLM (auto-discovered GPU endpoint)
//
// Each provider executes the LLM via fork/exec or HTTP with timeout handling.

#include <chitta/llm_http.hpp>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <optional>
#include <functional>

namespace chitta {

// Result from a brain invocation
struct BrainResult {
    bool success = false;
    std::string output;
    std::string error;
    int exit_code = -1;
    int duration_ms = 0;
    double cost_usd = 0.0;   // cost from --output-format json wrapper
    int num_turns = 0;        // conversation turns within the agent session
};

// Configuration for brain invocation
struct BrainConfig {
    int timeout_ms = 600000;        // 10 minute default (agent cycles take time)
    bool capture_stderr = true;
    std::string working_dir;        // If empty, uses current dir
    std::vector<std::string> extra_args;
    std::string system_prompt;      // System prompt for agent identity/constraints
    int max_turns = 20;             // Max agent turns (0 = use provider default)
};

// Abstract base class for LLM providers
class BrainProvider {
public:
    virtual ~BrainProvider() = default;

    // Core thinking operation
    virtual BrainResult think(const std::string& prompt,
                               const BrainConfig& config = {}) = 0;

    // Provider identification
    virtual std::string provider_name() const = 0;
    virtual std::string model_name() const = 0;

    // Model management
    virtual void set_model(const std::string& model) = 0;
    virtual std::vector<std::string> available_models() const = 0;

    // Utility: simple blocking think with default timeout
    std::string think_simple(const std::string& prompt, int timeout_ms = 600000) {
        BrainConfig config;
        config.timeout_ms = timeout_ms;
        auto result = think(prompt, config);
        return result.success ? result.output : "";
    }
};

// Claude CLI brain provider
// Executes: claude --dangerously-skip-permissions --model <model> --max-turns N [-p prompt]
class ClaudeBrain : public BrainProvider {
public:
    explicit ClaudeBrain(const std::string& model = "sonnet")
        : model_(model) {}

    BrainResult think(const std::string& prompt,
                       const BrainConfig& config = {}) override;

    std::string provider_name() const override { return "claude"; }
    std::string model_name() const override { return model_; }

    void set_model(const std::string& model) override { model_ = model; }

    std::vector<std::string> available_models() const override {
        return {"opus", "sonnet", "haiku"};
    }

private:
    std::string model_;
    std::string claude_path_ = "claude";  // Assumes claude is in PATH
};

// Local brain provider (HTTP to Ollama/vLLM)
class LocalBrain : public BrainProvider {
public:
    explicit LocalBrain(const std::string& model = "gemma4:26b")
        : model_(model) {}

    BrainResult think(const std::string& prompt,
                       const BrainConfig& config = {}) override;

    std::string provider_name() const override { return "local"; }
    std::string model_name() const override { return model_; }

    void set_model(const std::string& model) override { model_ = model; }

    std::vector<std::string> available_models() const override {
        return {"gemma4:26b", "gemma4:12b", "qwen3-coder", "llama3.1:8b"};
    }

    // Returns cached endpoint (populated after first think() call)
    std::string endpoint() const { return cached_endpoint_; }

private:
    std::string model_;
    std::string cached_endpoint_;
};

// Factory function to create brain provider by name
inline std::unique_ptr<BrainProvider> create_brain(const std::string& provider,
                                                     const std::string& model = "") {
    if (provider == "claude") {
        return std::make_unique<ClaudeBrain>(model.empty() ? "sonnet" : model);
    } else if (provider == "local") {
        return std::make_unique<LocalBrain>(model.empty() ? "gemma4:26b" : model);
    }
    // Default to local
    return std::make_unique<LocalBrain>(model.empty() ? "gemma4:26b" : model);
}

}  // namespace chitta
