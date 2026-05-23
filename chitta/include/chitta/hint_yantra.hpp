#pragma once
// HintYantra — in-process hint extraction via llama.cpp generation.
// Loads chitta-hint-qwen-q4_k_m.gguf (or f16) on CPU; no GPU, no Ollama server required.
// Always available alongside chittad. Used by `chitta hint_extract`.
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>
#include <filesystem>

#ifdef CHITTA_WITH_LLAMA_CPP
#include <llama.h>

namespace chitta {

class HintYantra {
public:
    static constexpr int N_CTX       = 512;
    static constexpr int MAX_NEW_TOK = 256;
    // CPU-only: hint extraction is lightweight and must be always-available.
    static constexpr int N_GPU_LAYERS = 0;

    static constexpr const char* SYSTEM_PROMPT =
        "Extract a single concise factual hint from the user message. "
        "If no personal fact or preference is present, output nothing.";

    explicit HintYantra(std::string model_path = "", const std::string& mind_path = "")
        : model_path_(model_path.empty() ? discover(mind_path) : std::move(model_path)) {
        if (model_path_.empty()) {
            log("[hint-yantra] no GGUF found — hint_extract will be unavailable");
            return;
        }
        if (!load()) {
            log("[hint-yantra] failed to load " + model_path_);
            cleanup();
            return;
        }
        ready_ = true;
        log("[hint-yantra] ready: " + model_path_);
    }

    ~HintYantra() { cleanup(); }

    HintYantra(const HintYantra&) = delete;
    HintYantra& operator=(const HintYantra&) = delete;

    bool ready() const { return ready_; }

    // Extract a factual hint from a user message. Returns "" if nothing personal found.
    std::string extract(const std::string& user_text) {
        if (!ready_) return "";
        std::lock_guard<std::mutex> lock(mtx_);

        std::string prompt = build_prompt(user_text);
        const llama_vocab* vocab = llama_model_get_vocab(model_);

        std::vector<llama_token> toks(N_CTX);
        int n = llama_tokenize(vocab, prompt.c_str(), (int)prompt.size(),
                               toks.data(), (int)toks.size(),
                               /*add_special=*/true, /*parse_special=*/true);
        if (n < 0 || n == 0) return "";
        toks.resize(n);

        llama_memory_clear(llama_get_memory(ctx_), false);

        // Feed prompt in one batch
        llama_batch batch = llama_batch_get_one(toks.data(), n);
        if (llama_decode(ctx_, batch) != 0) return "";

        auto sparams = llama_sampler_chain_default_params();
        llama_sampler* smpl = llama_sampler_chain_init(sparams);
        llama_sampler_chain_add(smpl, llama_sampler_init_greedy());

        llama_token eot = llama_vocab_eot(vocab);
        llama_token eos = llama_vocab_eos(vocab);

        std::string result;
        char piece_buf[256];

        for (int i = 0; i < MAX_NEW_TOK; ++i) {
            // idx=-1: last output row (n_outputs-1) — correct for llama_batch_get_one
            llama_token tok = llama_sampler_sample(smpl, ctx_, -1);
            llama_sampler_accept(smpl, tok);

            if (tok == eot || tok == eos || tok < 0) break;

            int len = llama_token_to_piece(vocab, tok, piece_buf, sizeof(piece_buf) - 1,
                                           /*lstrip=*/0, /*special=*/false);
            if (len <= 0) break;
            piece_buf[len] = '\0';
            result += piece_buf;

            // Stop if we hit the turn-end marker in text form
            if (result.find("<|im_end|>") != std::string::npos) {
                auto pos = result.find("<|im_end|>");
                result = result.substr(0, pos);
                break;
            }

            // Advance context by one token
            llama_batch next = llama_batch_get_one(&tok, 1);
            if (llama_decode(ctx_, next) != 0) break;
        }

        llama_sampler_free(smpl);

        // Trim whitespace
        size_t s = result.find_first_not_of(" \t\n\r");
        if (s == std::string::npos) return "";
        size_t e = result.find_last_not_of(" \t\n\r");
        result = result.substr(s, e - s + 1);

        // Reject placeholder outputs
        if (result.empty() || result == "-" || result.size() < 5) return "";

        return result;
    }

private:
    std::string    model_path_;
    bool           ready_  = false;
    llama_model*   model_  = nullptr;
    llama_context* ctx_    = nullptr;
    std::mutex     mtx_;

    static std::string discover(const std::string& mind_path) {
        namespace fs = std::filesystem;
        // Prefer smaller Q4_K_M; fall back to F16
        static constexpr const char* NAMES[] = {
            "chitta-hint-qwen-q4_k_m.gguf",
            "chitta-hint-qwen-f16.gguf",
        };

        if (const char* env = std::getenv("CHITTA_HINT_MODEL"))
            if (env[0] && fs::exists(env)) return env;

        for (const char* name : NAMES) {
            if (const char* home = std::getenv("HOME")) {
                fs::path p = fs::path(home) / ".claude" / "models" / name;
                if (fs::exists(p)) return p.string();
            }
            if (!mind_path.empty()) {
                fs::path p = fs::path(mind_path) / ".." / ".." / "models" / name;
                if (fs::exists(p)) return fs::canonical(p).string();
            }
        }
        return "";
    }

    bool load() {
        llama_backend_init();

        llama_model_params mparams = llama_model_default_params();
        mparams.n_gpu_layers = N_GPU_LAYERS;
        model_ = llama_model_load_from_file(model_path_.c_str(), mparams);
        if (!model_) return false;

        llama_context_params cparams = llama_context_default_params();
        cparams.n_ctx = N_CTX;
        // n_batch/n_ubatch: leave at defaults (2048/512); oversizing them to n_ctx
        // causes incorrect graph allocation for short prompts on Qwen3.
        ctx_ = llama_init_from_model(model_, cparams);
        return ctx_ != nullptr;
    }

    void cleanup() {
        if (ctx_)   { llama_free(ctx_);         ctx_   = nullptr; }
        if (model_) { llama_model_free(model_); model_ = nullptr; }
        ready_ = false;
    }

    static std::string build_prompt(const std::string& user_text) {
        std::string t = user_text;
        // Strip role prefixes hint_enricher may have left in
        static const char* PREFIXES[] = {
            "[user] ", "[human] ", "user: ", "human: ", nullptr
        };
        for (int i = 0; PREFIXES[i]; ++i) {
            size_t plen = strlen(PREFIXES[i]);
            if (t.size() >= plen &&
                t.substr(0, plen) == PREFIXES[i]) {
                t = t.substr(plen);
                break;
            }
        }
        // Qwen3 ChatML — plain assistant prefix; model may enter thinking mode
        return std::string("<|im_start|>system\n") + SYSTEM_PROMPT + "<|im_end|>\n"
             + "<|im_start|>user\n" + t + "<|im_end|>\n"
             + "<|im_start|>assistant\n";
    }

    static void log(const std::string& msg) { fprintf(stderr, "%s\n", msg.c_str()); }
};

} // namespace chitta

#endif // CHITTA_WITH_LLAMA_CPP
