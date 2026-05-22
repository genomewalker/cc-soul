#pragma once
// LlamaYantra — VakYantra backed by llama.cpp running a GGUF embedding model in-process.
// Model: nomic-embed-text-v1.5 (768-d). No external server. GPU auto-used if built with CUDA.
// Compile-time guard: only active when CHITTA_WITH_LLAMA_CPP is defined.
#include "chitta/vak.hpp"
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>
#include <filesystem>

#ifdef CHITTA_WITH_LLAMA_CPP
#include <llama.h>

namespace chitta {

class LlamaYantra : public VakYantra {
public:
    static constexpr int N_CTX      = 8192;
    static constexpr int MAX_TOKENS = 8000;   // truncate below n_ctx
    static constexpr int N_GPU_LAYERS = 99;   // offload all if CUDA present; no-op on CPU build

    // model_path: if empty, discover via env/home. Pass mind_path to enable third search location.
    explicit LlamaYantra(std::string model_path = "", const std::string& mind_path = "")
        : model_path_(model_path.empty() ? discover_model_path(mind_path) : std::move(model_path)) {
        if (model_path_.empty()) {
            log("[llama-embed] no GGUF model found — will fall back to Ollama");
            return;
        }
        if (!load()) {
            log("[llama-embed] WARNING: failed to load model at " + model_path_);
            cleanup();
            return;
        }
        ready_ = true;
        log("[llama-embed] ready: " + model_path_ +
            " (n_embd=" + std::to_string(n_embd_) +
            ", gpu=" + (gpu_ ? "yes" : "no") + ")");
    }

    ~LlamaYantra() override { cleanup(); }

    LlamaYantra(const LlamaYantra&) = delete;
    LlamaYantra& operator=(const LlamaYantra&) = delete;

    size_t dimension() const override { return EMBED_DIM; }
    bool   ready()     const override { return ready_; }

    Artha transform(const std::string& vak) override {
        return transform(vak, EmbedMode::Document);
    }

    Artha transform(const std::string& vak, EmbedMode mode) override {
        Vector v = embed_one(add_prefix(vak, mode));
        return Artha{std::move(v), ready_ ? 1.0f : 0.0f, vak};
    }

    std::vector<Artha> transform_batch(const std::vector<std::string>& vaks) override {
        return transform_batch(vaks, EmbedMode::Document);
    }

    std::vector<Artha> transform_batch(const std::vector<std::string>& vaks, EmbedMode mode) {
        std::vector<Artha> out;
        out.reserve(vaks.size());
        for (const auto& vak : vaks) out.push_back(transform(vak, mode));
        return out;
    }

private:
    std::string    model_path_;
    bool           ready_  = false;
    bool           gpu_    = false;
    int            n_embd_ = 0;
    llama_model*   model_  = nullptr;
    llama_context* ctx_    = nullptr;
    mutable std::mutex mtx_;  // llama_context is NOT thread-safe

    static std::string add_prefix(const std::string& text, EmbedMode mode) {
        if (mode == EmbedMode::Query) return "search_query: "    + text;
        return                               "search_document: " + text;
    }

    static std::string discover_model_path(const std::string& mind_path = "") {
        namespace fs = std::filesystem;
        static constexpr auto MODEL_FILE = "nomic-embed-text-v1.5.gguf";

        // 1) env override
        if (const char* env = std::getenv("CHITTA_EMBED_MODEL"))
            if (env[0] && fs::exists(env)) return env;

        // 2) ~/.claude/models/
        if (const char* home = std::getenv("HOME")) {
            fs::path p = fs::path(home) / ".claude" / "models" / MODEL_FILE;
            if (fs::exists(p)) return p.string();
        }

        // 3) $mind_path/../../models/ (sibling of the mind dir)
        if (!mind_path.empty()) {
            fs::path p = fs::path(mind_path) / ".." / ".." / "models" / MODEL_FILE;
            if (fs::exists(p)) return fs::canonical(p).string();
        }

        return "";
    }

    bool load() {
        llama_backend_init();

        llama_model_params mparams = llama_model_default_params();
        mparams.n_gpu_layers = N_GPU_LAYERS;
        model_ = llama_model_load_from_file(model_path_.c_str(), mparams);
        if (!model_) return false;

        n_embd_ = llama_model_n_embd(model_);
        if (static_cast<size_t>(n_embd_) != EMBED_DIM) {
            log("[llama-embed] ERROR: model n_embd=" + std::to_string(n_embd_) +
                " != EMBED_DIM=" + std::to_string(EMBED_DIM) + " — rejecting model");
            return false;
        }

        llama_context_params cparams = llama_context_default_params();
        cparams.n_ctx        = N_CTX;
        cparams.n_batch      = N_CTX;
        cparams.embeddings   = true;
        cparams.pooling_type = LLAMA_POOLING_TYPE_MEAN;
        ctx_ = llama_init_from_model(model_, cparams);
        if (!ctx_) return false;

        gpu_ = llama_supports_gpu_offload() && (N_GPU_LAYERS > 0);
        return true;
    }

    void cleanup() {
        if (ctx_)   { llama_free(ctx_);         ctx_   = nullptr; }
        if (model_) { llama_model_free(model_); model_ = nullptr; }
        // Do NOT call llama_backend_free() — it's process-global; freeing here risks
        // double-free if the yantra is replaced/reconstructed during the daemon lifetime.
        ready_ = false;
    }

    Vector embed_one(const std::string& text) {
        std::lock_guard<std::mutex> lock(mtx_);
        Vector v;  // default: zero vector
        if (!ready_ || !ctx_ || !model_) return v;

        const llama_vocab* vocab = llama_model_get_vocab(model_);

        // Tokenize — add_special=true so BOS/EOS are added per model config.
        std::vector<llama_token> toks(N_CTX);
        int n = llama_tokenize(vocab, text.c_str(), (int)text.size(),
                               toks.data(), (int)toks.size(),
                               /*add_special=*/true, /*parse_special=*/false);
        if (n < 0) {
            // Buffer too small: -n is required size; resize and retry once.
            toks.resize(-n);
            n = llama_tokenize(vocab, text.c_str(), (int)text.size(),
                               toks.data(), (int)toks.size(), true, false);
        }
        if (n <= 0) return v;
        if (n > MAX_TOKENS) n = MAX_TOKENS;  // truncate to context limit
        toks.resize(n);

        // Clear KV cache between sequences — essential for pooled embeddings.
        llama_memory_clear(llama_get_memory(ctx_), true);

        llama_batch batch = llama_batch_get_one(toks.data(), (int)toks.size());
        if (llama_decode(ctx_, batch) != 0) {
            log("[llama-embed] decode failed for text len=" + std::to_string(text.size()));
            return v;
        }

        // Prefer pooled sequence embedding; fall back to per-token mean if unavailable.
        const float* emb = llama_get_embeddings_seq(ctx_, 0);
        if (!emb) emb = llama_get_embeddings(ctx_);
        if (!emb) return v;

        v.data.assign(emb, emb + EMBED_DIM);
        l2_normalize(v);
        return v;
    }

    static void l2_normalize(Vector& v) {
        float norm = 0.0f;
        for (float x : v.data) norm += x * x;
        norm = std::sqrt(norm);
        if (norm < 1e-8f) return;
        for (float& x : v.data) x /= norm;
    }

    static void log(const std::string& msg) { fprintf(stderr, "%s\n", msg.c_str()); }
};

} // namespace chitta

#endif // CHITTA_WITH_LLAMA_CPP
