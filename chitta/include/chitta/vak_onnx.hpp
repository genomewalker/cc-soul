#pragma once
// VakONNX: the neural yantra - ONNX Runtime implementation
//
// Technically rigorous embedding generation:
// - Proper sentence-transformers compatible pipeline
// - Mean pooling with attention mask weighting
// - L2 normalization for cosine similarity
// - Unicode normalization and proper tokenization
// - Automatic model introspection
// - Batch processing with dynamic shapes

#include "vak.hpp"
#include <onnxruntime_cxx_api.h>
#include <array>
#include <numeric>
#include <cmath>
#include <codecvt>
#include <locale>
#include <iostream>

namespace chitta {

// Pooling strategies for sentence embeddings
enum class PoolingStrategy {
    Mean,       // Mean of all token embeddings (weighted by attention)
    CLS,        // Use [CLS] token embedding
    Max,        // Max pooling across sequence
    MeanSqrt    // Mean with sqrt of length normalization
};

// Model configuration detected from ONNX
struct ModelConfig {
    std::vector<std::string> input_names;
    std::vector<std::string> output_names;
    std::vector<std::vector<int64_t>> input_shapes;
    std::vector<std::vector<int64_t>> output_shapes;
    int64_t hidden_dim = 768;
    int64_t max_seq_length = 512;
    bool has_token_type_ids = false;
    bool outputs_pooled = false;  // Some models output pooled directly
};

// Preprocessing pipeline
class TextPreprocessor {
public:
    // Normalize unicode and clean text
    std::string normalize(const std::string& text) const {
        std::string result;
        result.reserve(text.size());

        for (size_t i = 0; i < text.size(); ++i) {
            unsigned char c = text[i];

            // Handle UTF-8 sequences
            if (c < 0x80) {
                // ASCII
                if (c == '\t' || c == '\n' || c == '\r') {
                    result += ' ';
                } else if (c >= 0x20) {
                    result += c;
                }
            } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
                // 2-byte UTF-8
                result += c;
                result += text[++i];
            } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
                // 3-byte UTF-8
                result += c;
                result += text[++i];
                result += text[++i];
            } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
                // 4-byte UTF-8
                result += c;
                result += text[++i];
                result += text[++i];
                result += text[++i];
            }
        }

        // Collapse multiple spaces
        std::string collapsed;
        bool last_space = true;
        for (char c : result) {
            if (c == ' ') {
                if (!last_space) {
                    collapsed += c;
                    last_space = true;
                }
            } else {
                collapsed += c;
                last_space = false;
            }
        }

        // Trim
        size_t start = collapsed.find_first_not_of(' ');
        size_t end = collapsed.find_last_not_of(' ');
        if (start == std::string::npos) return "";
        return collapsed.substr(start, end - start + 1);
    }

    // Lowercase (ASCII only, preserve unicode)
    std::string lowercase(const std::string& text) const {
        std::string result = text;
        for (char& c : result) {
            if (c >= 'A' && c <= 'Z') {
                c = c - 'A' + 'a';
            }
        }
        return result;
    }
};

// Advanced WordPiece tokenizer
class WordPieceTokenizer {
public:
    bool load(const std::string& vocab_path) {
        std::ifstream file(vocab_path);
        if (!file) return false;

        vocab_.clear();
        id_to_token_.clear();

        std::string line;
        int64_t id = 0;
        while (std::getline(file, line)) {
            // Handle potential BOM or trailing whitespace
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                line.pop_back();
            }
            if (!line.empty()) {
                vocab_[line] = id;
                id_to_token_[id] = line;
                id++;
            }
        }

        // Locate special tokens
        cls_id_ = get_id("[CLS]");
        sep_id_ = get_id("[SEP]");
        pad_id_ = get_id("[PAD]");
        unk_id_ = get_id("[UNK]");
        mask_id_ = get_id("[MASK]");

        // Validate
        if (unk_id_ < 0) {
            return false;  // Must have UNK token
        }

        // Find max token length for optimization
        max_token_len_ = 0;
        for (const auto& [token, _] : vocab_) {
            if (token.length() > max_token_len_) {
                max_token_len_ = token.length();
            }
        }

        return true;
    }

    struct TokenizedOutput {
        std::vector<int64_t> input_ids;
        std::vector<int64_t> attention_mask;
        std::vector<int64_t> token_type_ids;
        size_t original_length;  // Before padding
    };

    TokenizedOutput encode(const std::string& text, size_t max_length = 512,
                           bool add_special_tokens = true) const {
        TokenizedOutput output;

        std::vector<int64_t> tokens;

        // Add CLS
        if (add_special_tokens && cls_id_ >= 0) {
            tokens.push_back(cls_id_);
        }

        // Tokenize
        auto words = split_into_words(text);
        for (const auto& word : words) {
            auto word_tokens = tokenize_word(word);
            for (int64_t tok : word_tokens) {
                tokens.push_back(tok);
                if (tokens.size() >= max_length - 1) break;
            }
            if (tokens.size() >= max_length - 1) break;
        }

        // Add SEP
        if (add_special_tokens && sep_id_ >= 0) {
            tokens.push_back(sep_id_);
        }

        output.original_length = tokens.size();

        // Create attention mask and pad
        output.input_ids = tokens;
        output.attention_mask.resize(tokens.size(), 1);
        output.token_type_ids.resize(tokens.size(), 0);

        // Pad to max_length
        while (output.input_ids.size() < max_length) {
            output.input_ids.push_back(pad_id_);
            output.attention_mask.push_back(0);
            output.token_type_ids.push_back(0);
        }

        return output;
    }

    int64_t get_id(const std::string& token) const {
        auto it = vocab_.find(token);
        return it != vocab_.end() ? it->second : -1;
    }

    size_t vocab_size() const { return vocab_.size(); }

private:
    std::vector<std::string> split_into_words(const std::string& text) const {
        std::vector<std::string> words;
        std::string current;

        for (size_t i = 0; i < text.size(); ) {
            unsigned char c = text[i];

            if (c < 0x80) {
                // ASCII
                if (std::isspace(static_cast<unsigned char>(c))) {
                    if (!current.empty()) {
                        words.push_back(current);
                        current.clear();
                    }
                    i++;
                } else if (std::ispunct(static_cast<unsigned char>(c))) {
                    if (!current.empty()) {
                        words.push_back(current);
                        current.clear();
                    }
                    words.push_back(std::string(1, static_cast<char>(c)));
                    i++;
                } else {
                    current += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    i++;
                }
            } else {
                // UTF-8 multi-byte
                size_t char_len = 1;
                if ((c & 0xE0) == 0xC0) char_len = 2;
                else if ((c & 0xF0) == 0xE0) char_len = 3;
                else if ((c & 0xF8) == 0xF0) char_len = 4;

                if (!current.empty()) {
                    words.push_back(current);
                    current.clear();
                }
                words.push_back(text.substr(i, char_len));
                i += char_len;
            }
        }

        if (!current.empty()) {
            words.push_back(current);
        }

        return words;
    }

    std::vector<int64_t> tokenize_word(const std::string& word) const {
        std::vector<int64_t> tokens;
        if (word.empty()) return tokens;

        // Check if whole word exists (common case - fast path)
        auto it = vocab_.find(word);
        if (it != vocab_.end()) {
            tokens.push_back(it->second);
            return tokens;
        }

        // WordPiece algorithm - optimized to reduce allocations
        size_t start = 0;
        std::string buffer;
        buffer.reserve(max_token_len_ + 2);  // Pre-allocate for "##" prefix

        while (start < word.length()) {
            // Limit search to max possible token length
            size_t prefix_len = (start > 0) ? 2 : 0;  // "##" prefix
            size_t max_len = std::min(word.length() - start, max_token_len_ - prefix_len);
            size_t end = start + max_len;
            int64_t cur_id = -1;
            size_t found_end = start;

            while (end > start) {
                // Reuse buffer to avoid allocations
                buffer.clear();
                if (start > 0) {
                    buffer = "##";
                }
                buffer.append(word, start, end - start);

                auto it = vocab_.find(buffer);
                if (it != vocab_.end()) {
                    cur_id = it->second;
                    found_end = end;
                    break;
                }
                end--;
            }

            if (cur_id < 0) {
                tokens.push_back(unk_id_);
                start++;
            } else {
                tokens.push_back(cur_id);
                start = found_end;
            }
        }

        return tokens;
    }

    std::unordered_map<std::string, int64_t> vocab_;
    std::unordered_map<int64_t, std::string> id_to_token_;
    int64_t cls_id_ = -1;
    int64_t sep_id_ = -1;
    int64_t pad_id_ = 0;
    int64_t unk_id_ = -1;
    int64_t mask_id_ = -1;
    size_t max_token_len_ = 20;  // Max subword length in vocab (optimization)
};

// Execution provider type for status reporting
enum class ExecutionProvider {
    CPU,
    CoreML,   // Apple Neural Engine + GPU
    CUDA,     // NVIDIA GPU
    Unknown
};

// The main ONNX embedding engine
class AntahkaranaYantra : public VakYantra {
public:
    struct Config {
        PoolingStrategy pooling = PoolingStrategy::CLS;  // CLS for BGE models
        size_t max_seq_length = 256;  // Balance between quality and speed
        size_t batch_size = 32;       // Max batch for efficiency
        bool normalize_embeddings = true;
        int num_threads = 4;          // Default to 4 threads (not auto)
        // nomic-embed-text-v1 prefixes (empty = no prefix, e.g. for BGE)
        std::string query_prefix = "";
        std::string doc_prefix   = "";
    };

    AntahkaranaYantra() : env_(ORT_LOGGING_LEVEL_WARNING, "chitta") {}

    // Get current execution provider (for status reporting)
    ExecutionProvider execution_provider() const { return exec_provider_; }

    // Human-readable execution provider name (overrides VakYantra)
    std::string execution_provider_name() const override {
        switch (exec_provider_) {
            case ExecutionProvider::CoreML: return "CoreML (GPU+ANE)";
            case ExecutionProvider::CUDA: return "CUDA";
            case ExecutionProvider::CPU: return "CPU";
            default: return "Unknown";
        }
    }

    explicit AntahkaranaYantra(Config config)
        : env_(ORT_LOGGING_LEVEL_WARNING, "chitta"), config_(config) {}

    // Initialize the yantra
    bool awaken(const std::string& model_path, const std::string& vocab_path) {
        try {
            // Load tokenizer
            if (!tokenizer_.load(vocab_path)) {
                error_ = "Failed to load vocabulary from: " + vocab_path;
                return false;
            }

            // Session options with strict thread limits
            // Use per-session pools (not global) to avoid deadlocks
            Ort::SessionOptions opts;

            // Limit intra-op threads (parallelism within operators)
            opts.SetIntraOpNumThreads(config_.num_threads > 0 ? config_.num_threads : 4);

            // Limit inter-op threads to 1 (no parallelism between operators)
            opts.SetInterOpNumThreads(1);

            // Force sequential execution to prevent thread explosion on high-core systems
            opts.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            // CPU execution - CoreML tested but no speedup for single-sentence embeddings
            // (tokenization is the bottleneck, not inference)
            exec_provider_ = ExecutionProvider::CPU;

            // Create session
            session_ = std::make_unique<Ort::Session>(env_, model_path.c_str(), opts);

            // Introspect model
            if (!introspect_model()) {
                return false;
            }

            ready_ = true;
            return true;

        } catch (const Ort::Exception& e) {
            error_ = std::string("ONNX error: ") + e.what();
            return false;
        } catch (const std::exception& e) {
            error_ = std::string("Error: ") + e.what();
            return false;
        }
    }

    Artha transform(const std::string& vak) override {
        auto results = transform_batch({vak});
        return results.empty() ? Artha(Vector::zeros(), 0.0f, vak) : results[0];
    }

    Artha transform(const std::string& vak, EmbedMode mode) override {
        const std::string& prefix = (mode == EmbedMode::Query)
            ? config_.query_prefix : config_.doc_prefix;
        if (!prefix.empty()) {
            auto results = run_inference({prefix + vak});
            if (results.empty()) return Artha(Vector::zeros(), 0.0f, vak);
            results[0].source = vak;
            return results[0];
        }
        return transform(vak);
    }

    std::vector<Artha> transform_batch(const std::vector<std::string>& vaks) override {
        if (!ready_ || vaks.empty()) {
            std::vector<Artha> results;
            for (const auto& vak : vaks) {
                results.emplace_back(Vector::zeros(), 0.0f, vak);
            }
            return results;
        }

        try {
            return run_inference(vaks);
        } catch (const Ort::Exception& e) {
            error_ = std::string("Inference error: ") + e.what();
            std::vector<Artha> results;
            for (const auto& vak : vaks) {
                results.emplace_back(Vector::zeros(), 0.0f, vak);
            }
            return results;
        }
    }

    size_t dimension() const override { return model_config_.hidden_dim; }
    bool ready() const override { return ready_; }
    const std::string& error() const { return error_; }
    const ModelConfig& model_config() const { return model_config_; }

private:
    bool introspect_model() {
        Ort::AllocatorWithDefaultOptions allocator;

        // Get input info
        size_t num_inputs = session_->GetInputCount();
        for (size_t i = 0; i < num_inputs; ++i) {
            auto name_ptr = session_->GetInputNameAllocated(i, allocator);
            std::string name = name_ptr.get();
            model_config_.input_names.push_back(name);
            input_names_cstr_.push_back(nullptr);  // Will set later

            auto type_info = session_->GetInputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            model_config_.input_shapes.push_back(tensor_info.GetShape());

            if (name == "token_type_ids") {
                model_config_.has_token_type_ids = true;
            }
        }

        // Set up C-string pointers
        input_names_storage_ = model_config_.input_names;
        input_names_cstr_.clear();
        for (const auto& name : input_names_storage_) {
            input_names_cstr_.push_back(name.c_str());
        }

        // Get output info
        size_t num_outputs = session_->GetOutputCount();
        for (size_t i = 0; i < num_outputs; ++i) {
            auto name_ptr = session_->GetOutputNameAllocated(i, allocator);
            model_config_.output_names.push_back(name_ptr.get());

            auto type_info = session_->GetOutputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            auto shape = tensor_info.GetShape();
            model_config_.output_shapes.push_back(shape);

            // Detect output type
            if (shape.size() == 2) {
                model_config_.outputs_pooled = true;
                model_config_.hidden_dim = shape[1];
            } else if (shape.size() == 3) {
                model_config_.hidden_dim = shape[2];
            }
        }

        output_names_storage_ = model_config_.output_names;
        output_names_cstr_.clear();
        for (const auto& name : output_names_storage_) {
            output_names_cstr_.push_back(name.c_str());
        }

        return true;
    }

    std::vector<Artha> run_inference(const std::vector<std::string>& vaks) {
        size_t batch_size = vaks.size();
        size_t seq_len = config_.max_seq_length;

        // Preprocess and tokenize
        TextPreprocessor preprocessor;
        std::vector<WordPieceTokenizer::TokenizedOutput> encodings;
        encodings.reserve(batch_size);

        for (const auto& vak : vaks) {
            std::string normalized = preprocessor.normalize(vak);
            encodings.push_back(tokenizer_.encode(normalized, seq_len));
        }

        // Prepare input tensors
        std::vector<int64_t> flat_input_ids;
        std::vector<int64_t> flat_attention_mask;
        std::vector<int64_t> flat_token_type_ids;

        flat_input_ids.reserve(batch_size * seq_len);
        flat_attention_mask.reserve(batch_size * seq_len);
        flat_token_type_ids.reserve(batch_size * seq_len);

        for (const auto& enc : encodings) {
            flat_input_ids.insert(flat_input_ids.end(),
                enc.input_ids.begin(), enc.input_ids.end());
            flat_attention_mask.insert(flat_attention_mask.end(),
                enc.attention_mask.begin(), enc.attention_mask.end());
            flat_token_type_ids.insert(flat_token_type_ids.end(),
                enc.token_type_ids.begin(), enc.token_type_ids.end());
        }

        // Create ONNX tensors
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::array<int64_t, 2> shape = {
            static_cast<int64_t>(batch_size),
            static_cast<int64_t>(seq_len)
        };

        std::vector<Ort::Value> inputs;

        // Add inputs in order expected by model
        for (const auto& name : input_names_storage_) {
            if (name == "input_ids") {
                inputs.push_back(Ort::Value::CreateTensor<int64_t>(
                    memory_info, flat_input_ids.data(), flat_input_ids.size(),
                    shape.data(), shape.size()));
            } else if (name == "attention_mask") {
                inputs.push_back(Ort::Value::CreateTensor<int64_t>(
                    memory_info, flat_attention_mask.data(), flat_attention_mask.size(),
                    shape.data(), shape.size()));
            } else if (name == "token_type_ids") {
                inputs.push_back(Ort::Value::CreateTensor<int64_t>(
                    memory_info, flat_token_type_ids.data(), flat_token_type_ids.size(),
                    shape.data(), shape.size()));
            }
        }

        // Run model
        auto outputs = session_->Run(
            Ort::RunOptions{nullptr},
            input_names_cstr_.data(), inputs.data(), inputs.size(),
            output_names_cstr_.data(), output_names_cstr_.size());

        // Extract embeddings
        return extract_embeddings(outputs, encodings, vaks);
    }

    std::vector<Artha> extract_embeddings(
        std::vector<Ort::Value>& outputs,
        const std::vector<WordPieceTokenizer::TokenizedOutput>& encodings,
        const std::vector<std::string>& vaks)
    {
        std::vector<Artha> results;
        size_t batch_size = vaks.size();

        // Get output tensor
        auto& output = outputs[0];
        auto shape = output.GetTensorTypeAndShapeInfo().GetShape();
        float* data = output.GetTensorMutableData<float>();

        if (model_config_.outputs_pooled || shape.size() == 2) {
            // Already pooled: [batch, hidden_dim]
            int64_t hidden_dim = shape[1];

            for (size_t b = 0; b < batch_size; ++b) {
                Vector nu = extract_vector(data + b * hidden_dim, hidden_dim);
                if (config_.normalize_embeddings) {
                    nu.normalize();
                }
                results.emplace_back(std::move(nu), 1.0f, vaks[b]);
            }
        } else {
            // Token embeddings: [batch, seq_len, hidden_dim]
            int64_t seq_len = shape[1];
            int64_t hidden_dim = shape[2];

            for (size_t b = 0; b < batch_size; ++b) {
                Vector nu = pool_embeddings(
                    data + b * seq_len * hidden_dim,
                    seq_len, hidden_dim,
                    encodings[b].attention_mask);

                if (config_.normalize_embeddings) {
                    nu.normalize();
                }
                results.emplace_back(std::move(nu), 1.0f, vaks[b]);
            }
        }

        return results;
    }

    Vector extract_vector(const float* data, int64_t dim) {
        std::vector<float> vec(EMBED_DIM, 0.0f);
        size_t copy_dim = std::min(static_cast<size_t>(dim), EMBED_DIM);
        for (size_t i = 0; i < copy_dim; ++i) {
            vec[i] = data[i];
        }
        return Vector(std::move(vec));
    }

    Vector pool_embeddings(const float* token_embeddings,
                           int64_t seq_len, int64_t hidden_dim,
                           const std::vector<int64_t>& attention_mask) {
        std::vector<float> pooled(EMBED_DIM, 0.0f);
        size_t dim = std::min(static_cast<size_t>(hidden_dim), EMBED_DIM);

        switch (config_.pooling) {
            case PoolingStrategy::CLS: {
                // Use first token ([CLS])
                for (size_t d = 0; d < dim; ++d) {
                    pooled[d] = token_embeddings[d];
                }
                break;
            }

            case PoolingStrategy::Mean: {
                // Mean pooling with attention mask
                float sum_mask = 0.0f;
                for (int64_t t = 0; t < seq_len; ++t) {
                    if (attention_mask[t] == 1) {
                        sum_mask += 1.0f;
                        for (size_t d = 0; d < dim; ++d) {
                            pooled[d] += token_embeddings[t * hidden_dim + d];
                        }
                    }
                }
                if (sum_mask > 0) {
                    for (size_t d = 0; d < dim; ++d) {
                        pooled[d] /= sum_mask;
                    }
                }
                break;
            }

            case PoolingStrategy::Max: {
                // Max pooling
                for (size_t d = 0; d < dim; ++d) {
                    pooled[d] = -std::numeric_limits<float>::infinity();
                }
                for (int64_t t = 0; t < seq_len; ++t) {
                    if (attention_mask[t] == 1) {
                        for (size_t d = 0; d < dim; ++d) {
                            pooled[d] = std::max(pooled[d],
                                token_embeddings[t * hidden_dim + d]);
                        }
                    }
                }
                break;
            }

            case PoolingStrategy::MeanSqrt: {
                // Mean with sqrt(length) normalization
                float sum_mask = 0.0f;
                for (int64_t t = 0; t < seq_len; ++t) {
                    if (attention_mask[t] == 1) {
                        sum_mask += 1.0f;
                        for (size_t d = 0; d < dim; ++d) {
                            pooled[d] += token_embeddings[t * hidden_dim + d];
                        }
                    }
                }
                if (sum_mask > 0) {
                    float norm_factor = std::sqrt(sum_mask);
                    for (size_t d = 0; d < dim; ++d) {
                        pooled[d] /= norm_factor;
                    }
                }
                break;
            }
        }

        return Vector(std::move(pooled));
    }

    // ONNX Runtime objects
    Ort::Env env_;
    std::unique_ptr<Ort::Session> session_;

    // Tokenizer
    WordPieceTokenizer tokenizer_;

    // Configuration
    Config config_;
    ModelConfig model_config_;

    // Name storage (ONNX needs stable C-strings)
    std::vector<std::string> input_names_storage_;
    std::vector<std::string> output_names_storage_;
    std::vector<const char*> input_names_cstr_;
    std::vector<const char*> output_names_cstr_;

    bool ready_ = false;
    std::string error_;
    ExecutionProvider exec_provider_ = ExecutionProvider::CPU;
};

// Factory function with sensible defaults
inline std::shared_ptr<VakYantra> create_yantra(
    const std::string& model_path,
    const std::string& vocab_path,
    size_t cache_size = 10000)
{
    AntahkaranaYantra::Config config;
    config.pooling = PoolingStrategy::CLS;  // CLS pooling for BGE models
    config.normalize_embeddings = true;

    auto inner = std::make_shared<AntahkaranaYantra>(config);
    if (!inner->awaken(model_path, vocab_path)) {
        return nullptr;
    }

    return std::make_shared<SmritiYantra>(inner, cache_size);
}

} // namespace chitta
