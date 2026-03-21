// gradmemd — GradMem write subprocess
//
// Reads a JSON job from stdin, runs K=2 gradient descent steps over memory
// prefix tokens M using a frozen Qwen2.5-1.5B transformer (libtorch),
// writes result JSON to stdout.
//
// Build: requires CHITTA_WITH_TORCH=ON (libtorch + llama.cpp headers)
//
// Input JSON (stdin):
//   {
//     "model_ts":     "/path/to/qwen_gradmem.pt",  // TorchScript model
//     "gguf":         "/path/to/qwen.gguf",          // for tokenization
//     "text":         "session transcript excerpt",
//     "session_id":   "...",
//     "realm":        "brahman",
//     "n_mem_tokens": 8,
//     "K":            2,
//     "inner_lr":     0.04,
//     "max_ctx_tokens": 512
//   }
//
// Output JSON (stdout):
//   {
//     "session_id":       "...",
//     "realm":            "brahman",
//     "M_fp16_b64":       "<base64>",
//     "write_loss":       1.23,
//     "n_mem_tokens":     8,
//     "d_model":          1536,
//     "K":                2,
//     "inner_lr":         0.04,
//     "proxy_embedding":  [0.1, -0.2, ...]  // mean(M) for HNSW
//   }

#include <torch/torch.h>
#include <torch/script.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

#ifdef CHITTA_GRADMEM_WITH_LLAMA
#include "llama.h"
#endif

using json = nlohmann::json;

// ── Base64 encode ─────────────────────────────────────────────────────────────

static const char B64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16;
        if (i + 1 < len) v |= (uint32_t)data[i+1] << 8;
        if (i + 2 < len) v |= (uint32_t)data[i+2];
        out += B64_CHARS[(v >> 18) & 63];
        out += B64_CHARS[(v >> 12) & 63];
        out += (i + 1 < len) ? B64_CHARS[(v >>  6) & 63] : '=';
        out += (i + 2 < len) ? B64_CHARS[(v      ) & 63] : '=';
    }
    return out;
}

// ── Tokenization ──────────────────────────────────────────────────────────────

#ifdef CHITTA_GRADMEM_WITH_LLAMA

std::vector<int64_t> tokenize_llama(
    const std::string& gguf_path,
    const std::string& text,
    int max_tokens
) {
    llama_model_params mparams = llama_model_default_params();
    mparams.vocab_only = true;
    llama_model* model = llama_model_load_from_file(gguf_path.c_str(), mparams);
    if (!model) return {};

    const llama_vocab* vocab = llama_model_get_vocab(model);
    std::vector<llama_token> toks(max_tokens);
    int n = llama_tokenize(vocab, text.c_str(), (int)text.size(),
                           toks.data(), max_tokens, /*add_bos=*/true, /*special=*/false);
    llama_model_free(model);
    if (n <= 0) return {};
    toks.resize(n);
    return std::vector<int64_t>(toks.begin(), toks.end());
}

#else

// Fallback: whitespace tokenizer (for testing without llama.cpp)
// Returns fake token IDs (word hashes modulo vocab_size).
// Replace with real tokenizer before production.
std::vector<int64_t> tokenize_fallback(const std::string& text, int max_tokens, int vocab_size = 32000) {
    std::vector<int64_t> ids;
    ids.push_back(1);  // BOS
    std::istringstream ss(text);
    std::string word;
    while (ss >> word && (int)ids.size() < max_tokens) {
        // Very rough hash — replace with proper BPE in production
        uint64_t h = 5381;
        for (char c : word) h = ((h << 5) + h) + (uint8_t)c;
        ids.push_back((int64_t)(h % (uint64_t)vocab_size) + 100);
    }
    return ids;
}

#endif

// ── GradMem inner loop ────────────────────────────────────────────────────────

struct GradMemJob {
    std::string model_ts_path;
    std::string gguf_path;
    std::string text;
    std::string session_id;
    std::string realm;
    int    n_mem_tokens  = 8;
    int    K             = 2;
    float  inner_lr      = 0.04f;
    int    max_ctx_tokens = 512;
};

json run_gradmem(const GradMemJob& job) {
    // ── 1. Load TorchScript model ─────────────────────────────────────────────
    torch::jit::Module module;
    try {
        module = torch::jit::load(job.model_ts_path);
        module.eval();
    } catch (const c10::Error& e) {
        return {{"error", std::string("load model: ") + e.what()}};
    }

    // Freeze all model parameters — only M gets gradients
    for (auto& param : module.parameters()) {
        param.requires_grad_(false);
    }

    // ── 2. Tokenize ───────────────────────────────────────────────────────────
    std::vector<int64_t> token_ids;
#ifdef CHITTA_GRADMEM_WITH_LLAMA
    token_ids = tokenize_llama(job.gguf_path, job.text, job.max_ctx_tokens);
#else
    token_ids = tokenize_fallback(job.text, job.max_ctx_tokens);
#endif
    if (token_ids.empty()) {
        return {{"error", "tokenization returned empty"}};
    }
    // Cap context
    if ((int)token_ids.size() > job.max_ctx_tokens)
        token_ids.resize(job.max_ctx_tokens);

    auto input_ids = torch::tensor(token_ids).unsqueeze(0);  // [1, seq_len]

    // ── 3. Get context embeddings (no grad) ───────────────────────────────────
    torch::Tensor ctx_embeds;
    int64_t d_model = 0;
    int64_t vocab_size = 0;
    try {
        torch::NoGradGuard ng;
        ctx_embeds = module.run_method("embed", input_ids).toTensor();  // [1, seq, d]
        d_model    = ctx_embeds.size(-1);
        vocab_size = module.run_method("vocab_size").toInt();
    } catch (const c10::Error& e) {
        return {{"error", std::string("embed: ") + e.what()}};
    }

    // ── 4. Initialize M (learnable prefix tokens) ─────────────────────────────
    auto M_opts = torch::TensorOptions()
        .dtype(torch::kFloat32)
        .requires_grad(true);
    auto M = (torch::randn({1, job.n_mem_tokens, d_model}, M_opts) * 0.02f);

    // ── 5. Inner loop: K SGD steps over M ────────────────────────────────────
    float last_loss = 0.f;
    for (int k = 0; k < job.K; k++) {
        if (M.grad().defined()) M.grad().zero_();

        // Concat [M; ctx_embeds] → [1, n_mem + seq_len, d]
        auto x_write = torch::cat({M, ctx_embeds}, /*dim=*/1);

        // Forward pass through frozen transformer
        torch::Tensor logits;
        try {
            logits = module.run_method("forward_from_embeds", x_write).toTensor();
        } catch (const c10::Error& e) {
            return {{"error", std::string("forward: ") + e.what()}};
        }

        // Reconstruction loss: predict next context token
        // logits[:, n_mem:-1, :] → target input_ids[:, 1:]
        int64_t n_mem = job.n_mem_tokens;
        int64_t seq   = (int64_t)token_ids.size();
        if (logits.size(1) < n_mem + seq - 1) {
            return {{"error", "logits shorter than expected"}};
        }
        auto target_logits = logits.slice(1, n_mem, n_mem + seq - 1);  // [1, seq-1, vocab]
        auto targets       = input_ids.slice(1, 1);                     // [1, seq-1]

        auto loss = torch::nn::functional::cross_entropy(
            target_logits.reshape({-1, vocab_size}),
            targets.reshape({-1})
        );
        last_loss = loss.item<float>();

        loss.backward();

        // Manual SGD step (avoids optimizer overhead)
        {
            torch::NoGradGuard ng;
            M.data().add_(M.grad().data(), -job.inner_lr);
        }
    }

    // ── 6. Serialize M to fp16 ────────────────────────────────────────────────
    auto M_squeezed = M.squeeze(0).detach();                      // [n_mem, d]
    auto M_fp16     = M_squeezed.to(torch::kFloat16).contiguous();
    const auto* bytes = reinterpret_cast<const uint8_t*>(M_fp16.data_ptr());
    size_t n_bytes    = (size_t)M_fp16.numel() * sizeof(uint16_t);
    std::string M_b64 = base64_encode(bytes, n_bytes);

    // ── 7. Proxy embedding: mean(M) in fp32 for HNSW ─────────────────────────
    auto proxy    = M_squeezed.to(torch::kFloat32).mean(0).contiguous();
    std::vector<float> proxy_vec(
        proxy.data_ptr<float>(),
        proxy.data_ptr<float>() + d_model
    );

    return {
        {"session_id",      job.session_id},
        {"realm",           job.realm},
        {"M_fp16_b64",      M_b64},
        {"write_loss",      last_loss},
        {"n_mem_tokens",    job.n_mem_tokens},
        {"d_model",         d_model},
        {"K",               job.K},
        {"inner_lr",        job.inner_lr},
        {"proxy_embedding", proxy_vec},
    };
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    // Read JSON job from stdin
    std::string input_str(
        (std::istreambuf_iterator<char>(std::cin)),
        std::istreambuf_iterator<char>()
    );

    if (input_str.empty()) {
        std::cout << R"({"error":"empty stdin"})" << "\n";
        return 1;
    }

    GradMemJob job;
    try {
        auto j          = json::parse(input_str);
        job.model_ts_path  = j.value("model_ts",      "");
        job.gguf_path      = j.value("gguf",           "");
        job.text           = j.value("text",           "");
        job.session_id     = j.value("session_id",     "");
        job.realm          = j.value("realm",          "brahman");
        job.n_mem_tokens   = j.value("n_mem_tokens",   8);
        job.K              = j.value("K",              2);
        job.inner_lr       = j.value("inner_lr",       0.04f);
        job.max_ctx_tokens = j.value("max_ctx_tokens", 512);
    } catch (const std::exception& e) {
        std::cout << json{{"error", std::string("parse: ") + e.what()}}.dump() << "\n";
        return 1;
    }

    if (job.model_ts_path.empty() || job.text.empty() || job.session_id.empty()) {
        std::cout << R"({"error":"missing required fields: model_ts, text, session_id"})" << "\n";
        return 1;
    }

    auto result = run_gradmem(job);
    std::cout << result.dump() << "\n";
    return result.contains("error") ? 1 : 0;
}
