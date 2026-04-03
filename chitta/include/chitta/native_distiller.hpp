#pragma once
// NativeDistiller: C++ distillation
//
// Extracts learnings from conversation transcripts using LLM + SSL format:
// 1. Parse JSONL transcript (streaming, handles any file size)
// 2. Build SSL prompt
// 3. Call LLM via HTTP (Ollama/vLLM endpoint, auto-discovered)
// 4. Parse SSL output
// 5. Store via FieldStore
//    - Dedup: if a near-identical memory exists (cosine > threshold), strengthen
//      the existing one instead of creating a duplicate

#include "transcript_parser.hpp"
#include "ssl_parser.hpp"
#include "field_store.hpp"
#include "llm_http.hpp"
#include <string>
#include <functional>
#include <vector>

namespace chitta {

struct TranscriptState {
    std::string session_id;
    std::string transcript_path;
    std::string realm = "brahman";
    int64_t last_processed_line = 0;
};

struct NativeDistillConfig {
    std::string model = "gemma4:26b";         // LLM model — overridden by --distill-model
    std::string endpoint = "";                // HTTP endpoint (auto-discovered if empty)
    int timeout_secs = 180;                   // Timeout for HTTP call
    int min_turns = 5;                        // Minimum turns for distillation
    bool verbose = false;                     // Enable verbose logging
    float dedup_threshold = 0.92f;            // Cosine similarity above which we strengthen
                                              // instead of storing a duplicate
};

struct DistillResult {
    int learnings_stored = 0;
    int learnings_deduped = 0;  // Strengthened existing instead of storing new
    int triplets_created = 0;
    int citations_linked = 0;   // Code citations (memory→file:line)
    int64_t episode_id = 0;
    int64_t last_line = 0;      // Last JSONL line processed (for progress tracking)
    bool success = false;
    std::string error;
};

// Embedder function type — takes text, returns 768-dim float vector (or empty on failure)
using EmbedFn = std::function<std::vector<float>(const std::string&)>;

class NativeDistiller {
public:
    NativeDistiller(FieldStore& field, EmbedFn embedder, const NativeDistillConfig& config = {});

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

    // Set cancellation check callback - return true to abort distillation
    using CancelCallback = std::function<bool()>;
    void set_cancel_callback(CancelCallback cb) { cancel_callback_ = cb; }

private:
    FieldStore* field_store_ = nullptr;
    EmbedFn embedder_;

    NativeDistillConfig config_;
    TranscriptParser parser_;
    SSLParser ssl_parser_;
    LogCallback log_callback_;
    CancelCallback cancel_callback_;
    std::string cached_endpoint_;

    // Call LLM via HTTP (Ollama/vLLM)
    std::string call_llm(const std::string& prompt);

    // Store learnings via FieldStore with dedup
    void store_learnings(
        const SSLParser::Result& ssl_result,
        const std::string& realm,
        uint64_t episode_mem_id,
        DistillResult& result
    );

    static float category_to_confidence(const std::string& category);
    static float category_to_decay(const std::string& category);
    void log(const std::string& msg);
};

} // namespace chitta
