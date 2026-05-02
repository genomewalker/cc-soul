#pragma once
// Daemon configuration structs and default path resolution.
// Extracted from simple_cli.cpp to allow other translation units to reference
// daemon config types without pulling in the full daemon implementation.

#include <string>
#include <fstream>
#include <cstdlib>
#include <cstdint>

namespace chitta {

/// Source trust hierarchy — defines confidence bounds per event source.
/// Enforced at write time in the queue processor.
/// Any violation is logged as a dead-letter entry and the event is adjusted.
struct SourcePolicy {
    float max_confidence = 1.0f;   ///< Cap: event confidence must be <= this
    float min_confidence = 0.0f;   ///< Floor: event confidence must be >= this
    bool  allow_durable  = true;   ///< If false, confidence is capped at 0.74 (provisional tier)
};

/// Return the policy for a given source string.
/// Sources not in the table default to: max=0.7, min=0.0, durable=false
inline SourcePolicy source_policy(const std::string& source) {
    if (source == "hook_regex")       return {0.70f, 0.00f, false};  // provisional, decays
    if (source == "hook_compliance")  return {0.90f, 0.50f, false};  // high-signal corrections but still provisional
    if (source == "distillation")     return {1.00f, 0.75f, true};   // durable: min 0.75
    if (source == "mcp_tool")         return {1.00f, 0.60f, true};   // explicit human intent: durable
    if (source == "system")           return {1.00f, 0.80f, true};   // internal system ops
    return {0.70f, 0.00f, false};  // unknown source → provisional
}

struct DistillConfig {
    int interval_minutes = 15;
    int min_turns = 4;
    std::string script_path;
    std::string model = "gemma4:26b";
    bool enabled = true;
    int64_t token_trigger_chars = 120000;
    int cooldown_seconds = 180;
    size_t max_context_chars = 0;  // 0 = no limit (full transcript)
    int max_tokens = 8192;         // LLM output token limit
};

struct EnrichConfig {
    int interval_minutes = 10;
    int batch_size = 3;
    int idle_seconds = 30;
    std::string script_path;
    std::string model = "gemma4:26b";
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

// Returns epistemic status code (0=UserStated,1=ToolDerived,2=ModelInferred,3=AutonomousSynthesis)
inline uint8_t epistemic_status_for_source(const std::string& source) {
    if (source == "mcp_tool")     return 0;  // UserStated
    if (source == "distillation") return 2;  // ModelInferred
    if (source == "system")       return 2;  // ModelInferred
    return 1;  // ToolDerived (hook_regex, hook_compliance, unknown)
}

// Returns initial MemoryStatus code for source (4=Proposed, 5=Observed, 0=Active)
inline uint8_t initial_status_for_source(const std::string& source) {
    if (source == "hook_regex" || source == "hook_compliance") return 4; // Proposed
    if (source == "distillation") return 0; // Active (already verified)
    if (source == "mcp_tool")    return 0;  // Active (explicit user intent)
    return 5; // Observed (other sources)
}

} // namespace chitta
