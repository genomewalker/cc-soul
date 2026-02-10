#pragma once
// NativeDistiller: C++ distillation replacing distill.sh
//
// Extracts learnings from conversation transcripts using LLM + SSL format:
// 1. Parse JSONL transcript (streaming, handles any file size)
// 2. Build SSL prompt
// 3. Call opencode for extraction
// 4. Parse SSL output
// 5. Store via observe() and connect()
//
// Replaces the shell-based distill.sh for robustness on large files.

#include "transcript_parser.hpp"
#include "ssl_parser.hpp"
#include "mind/duckdb_mind.hpp"
#include <string>
#include <functional>

namespace chitta {

struct NativeDistillConfig {
    std::string model = "gemini-2.0-flash";  // LLM model for distillation
    int timeout_secs = 120;                   // Timeout for opencode call
    int min_turns = 5;                        // Minimum turns for distillation
    bool verbose = false;                     // Enable verbose logging
};

struct DistillResult {
    int learnings_stored = 0;
    int triplets_created = 0;
    int64_t episode_id = 0;
    int64_t last_line = 0;      // Last JSONL line processed (for progress tracking)
    bool success = false;
    std::string error;
};

class NativeDistiller {
public:
    NativeDistiller(DuckDBMind& mind, const NativeDistillConfig& config = {});

    // Main entry point - distill a session transcript
    // Returns result with counts of stored learnings/triplets
    DistillResult distill_session(
        const std::string& session_id,
        const std::string& transcript_path,
        const std::string& realm,
        int64_t skip_lines = 0,
        bool queue_triggered = false  // If true, use min_turns=1
    );

    // Set callback for progress messages
    using LogCallback = std::function<void(const std::string&)>;
    void set_log_callback(LogCallback cb) { log_callback_ = cb; }

private:
    DuckDBMind& mind_;
    NativeDistillConfig config_;
    TranscriptParser parser_;
    SSLParser ssl_parser_;
    LogCallback log_callback_;

    // Call opencode with prompt, return output
    std::string call_opencode(const std::string& prompt);

    // Store learnings via mind_.remember() equivalent
    int store_learnings(
        const SSLParser::Result& result,
        const std::string& session_id,
        const std::string& realm,
        int64_t episode_id
    );

    // Get confidence for category
    static float category_to_confidence(const std::string& category);

    // Log message if callback set
    void log(const std::string& msg);
};

} // namespace chitta
