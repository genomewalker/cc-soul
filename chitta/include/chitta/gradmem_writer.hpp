#pragma once
// GradMemWriter: launches gradmemd subprocess for session-end GradMem write.
//
// GradMem compresses a session transcript into M prefix memory tokens via
// K gradient descent steps on a frozen transformer (Qwen2.5-1.5B).
// Result (M fp16 blob + proxy embedding) is stored via FieldStore as a
// gradmem_snapshot memory.
//
// Architecture:
//   chittad (queue thread)
//     └── GradMemWriter::write_async()
//           └── fork → gradmemd [JSON on stdin]
//                 └── tokenize + libtorch inner loop
//                     └── JSON result on stdout → FieldStore

#include "field_store.hpp"
#include <string>
#include <functional>

namespace chitta {

struct GradMemConfig {
    std::string gradmemd_path;     // Path to gradmemd binary (~/.claude/bin/gradmemd)
    std::string model_ts_path;     // TorchScript Qwen model (~/.claude/bin/qwen_gradmem.pt)
    std::string gguf_path;         // GGUF for tokenization (~/.claude/bin/ssl_distiller_dpo.gguf)
    int n_mem_tokens  = 8;         // Number of memory prefix tokens
    int K             = 2;         // Inner loop gradient steps
    float inner_lr    = 0.04f;     // SGD learning rate for M
    int max_ctx_tokens = 512;      // Maximum context tokens (bounds write time)
    bool enabled      = false;     // Off by default until model is prepared
};

struct GradMemResult {
    std::string session_id;
    std::string M_fp16_b64;        // base64-encoded fp16 tensor (n_mem × d_model)
    float       write_loss  = 0.f;
    int         n_mem_tokens = 0;
    int64_t     d_model      = 0;
    int         K            = 0;
    std::vector<float> proxy_embedding;  // mean(M) — used as HNSW vector
    bool        success      = false;
    std::string error;
};

class GradMemWriter {
public:
    GradMemWriter(FieldStore& field, const GradMemConfig& config = {})
        : field_store_(&field), config_(config) {}

    // Launch gradmemd as a detached background subprocess.
    // Returns immediately — result is persisted asynchronously.
    // transcript_text: bounded excerpt (caller selects relevant portion).
    void write_async(
        const std::string& session_id,
        const std::string& transcript_text,
        const std::string& realm
    );

    // Synchronous write — blocks until gradmemd exits.
    // For testing / distill_trigger path.
    GradMemResult write_sync(
        const std::string& session_id,
        const std::string& transcript_text,
        const std::string& realm
    );

    // Store a completed GradMemResult into FieldStore.
    // Called after write_sync or from the async child's result pipe.
    void store_result(const GradMemResult& result);

    using LogCallback = std::function<void(const std::string&)>;
    void set_log_callback(LogCallback cb) { log_cb_ = cb; }

    bool is_enabled() const { return config_.enabled && !config_.gradmemd_path.empty(); }
    const GradMemConfig& config() const { return config_; }

private:
    FieldStore*    field_store_ = nullptr;
    GradMemConfig  config_;
    LogCallback    log_cb_;

    // Build JSON job for stdin
    std::string build_job_json(
        const std::string& session_id,
        const std::string& transcript_text,
        const std::string& realm
    ) const;

    // Parse JSON result from gradmemd stdout
    GradMemResult parse_result(const std::string& json_str) const;

    void log(const std::string& msg) { if (log_cb_) log_cb_(msg); }
};

} // namespace chitta
