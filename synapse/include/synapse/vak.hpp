#pragma once
// Vāk: the sacred word becoming meaning
//
// वाक् (Vāk) - Speech, the goddess of utterance
// शब्द (Śabda) - Sound-form, the vehicle of meaning
// अर्थ (Artha) - Meaning, what the word points to
// पदार्थ (Padārtha) - The referent, position in semantic space
//
// The journey: Vāk → Śabda → Artha → Geometry
// Text is not tokens. It's utterance becoming understanding.

#include "types.hpp"
#include "quantized.hpp"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace synapse {

// Forward declarations
class VakYantra;

// Pada: a parsed unit of meaning (not just a token)
struct Pada {
    std::string text;      // The original text
    int64_t id;            // Vocabulary ID
    float weight;          // Attention weight (some words matter more)

    Pada(std::string t, int64_t i, float w = 1.0f)
        : text(std::move(t)), id(i), weight(w) {}
};

// Shabda: the sound-form, a sequence of Padas ready for transformation
struct Shabda {
    std::vector<Pada> padas;
    std::vector<int64_t> input_ids;
    std::vector<int64_t> attention_mask;
    std::string original;

    size_t length() const { return padas.size(); }
    bool empty() const { return padas.empty(); }
};

// Artha: meaning as geometry - the embedding
struct Artha {
    Vector nu;              // The semantic position
    float certainty;        // How confident are we in this meaning?
    std::string source;     // What utterance produced this?

    Artha() : certainty(1.0f) {}
    Artha(Vector v, float c = 1.0f, std::string s = "")
        : nu(std::move(v)), certainty(c), source(std::move(s)) {}

    // Artha can be quantized for storage
    QuantizedVector quantize() const {
        return QuantizedVector::from_float(nu);
    }
};

// VakPatha: the path of speech - tokenization
// WordPiece tokenizer for transformer models
class VakPatha {
public:
    VakPatha() = default;

    // Load vocabulary from file (vocab.txt format)
    bool load_vocabulary(const std::string& path) {
        std::ifstream file(path);
        if (!file) return false;

        std::string line;
        int64_t id = 0;
        while (std::getline(file, line)) {
            if (!line.empty()) {
                vocab_[line] = id;
                id_to_token_[id] = line;
                id++;
            }
        }

        // Find special tokens
        auto find_special = [this](const std::string& token) -> int64_t {
            auto it = vocab_.find(token);
            return it != vocab_.end() ? it->second : -1;
        };

        cls_id_ = find_special("[CLS]");
        sep_id_ = find_special("[SEP]");
        pad_id_ = find_special("[PAD]");
        unk_id_ = find_special("[UNK]");

        return !vocab_.empty();
    }

    // Set vocabulary directly (for embedded vocabularies)
    void set_vocabulary(std::unordered_map<std::string, int64_t> vocab) {
        vocab_ = std::move(vocab);
        for (const auto& [token, id] : vocab_) {
            id_to_token_[id] = token;
        }
    }

    // Parse text into Shabda (sound-form)
    Shabda parse(const std::string& text, size_t max_length = 512) const {
        Shabda shabda;
        shabda.original = text;

        // Add [CLS] token
        if (cls_id_ >= 0) {
            shabda.padas.emplace_back("[CLS]", cls_id_, 0.0f);
        }

        // Tokenize the text
        auto words = split_words(text);
        for (const auto& word : words) {
            auto tokens = wordpiece_tokenize(word);
            for (const auto& token : tokens) {
                auto it = vocab_.find(token);
                int64_t id = (it != vocab_.end()) ? it->second : unk_id_;
                shabda.padas.emplace_back(token, id);

                if (shabda.padas.size() >= max_length - 1) break;
            }
            if (shabda.padas.size() >= max_length - 1) break;
        }

        // Add [SEP] token
        if (sep_id_ >= 0) {
            shabda.padas.emplace_back("[SEP]", sep_id_, 0.0f);
        }

        // Build input tensors
        for (const auto& pada : shabda.padas) {
            shabda.input_ids.push_back(pada.id);
            shabda.attention_mask.push_back(1);
        }

        // Pad to max_length if needed
        while (shabda.input_ids.size() < max_length) {
            shabda.input_ids.push_back(pad_id_);
            shabda.attention_mask.push_back(0);
        }

        return shabda;
    }

    size_t vocab_size() const { return vocab_.size(); }
    bool loaded() const { return !vocab_.empty(); }

private:
    std::vector<std::string> split_words(const std::string& text) const {
        std::vector<std::string> words;
        std::string current;

        for (char c : text) {
            if (std::isspace(c) || std::ispunct(c)) {
                if (!current.empty()) {
                    words.push_back(current);
                    current.clear();
                }
                if (std::ispunct(c)) {
                    words.push_back(std::string(1, c));
                }
            } else {
                current += std::tolower(c);
            }
        }

        if (!current.empty()) {
            words.push_back(current);
        }

        return words;
    }

    std::vector<std::string> wordpiece_tokenize(const std::string& word) const {
        std::vector<std::string> tokens;

        if (word.empty()) return tokens;

        // Check if whole word is in vocabulary
        if (vocab_.count(word)) {
            tokens.push_back(word);
            return tokens;
        }

        // WordPiece: try to find longest matching subwords
        size_t start = 0;
        while (start < word.length()) {
            size_t end = word.length();
            std::string substr;
            bool found = false;

            while (start < end) {
                substr = word.substr(start, end - start);
                if (start > 0) {
                    substr = "##" + substr;  // Continuation token
                }

                if (vocab_.count(substr)) {
                    tokens.push_back(substr);
                    found = true;
                    break;
                }
                end--;
            }

            if (!found) {
                // Character not in vocab, use [UNK]
                tokens.push_back("[UNK]");
                start++;
            } else {
                start = (start > 0) ? start + (end - start) : end;
            }
        }

        return tokens;
    }

    std::unordered_map<std::string, int64_t> vocab_;
    std::unordered_map<int64_t, std::string> id_to_token_;
    int64_t cls_id_ = -1;
    int64_t sep_id_ = -1;
    int64_t pad_id_ = -1;
    int64_t unk_id_ = -1;
};

// SmritiKosha: the treasury of memory - embedding cache
// (Smriti = memory, Kosha = treasury/sheath)
class SmritiKosha {
public:
    SmritiKosha(size_t max_size = 10000) : max_size_(max_size) {}

    // Remember an utterance and its meaning
    void remember(const std::string& vak, Artha artha) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Evict oldest if at capacity
        if (cache_.size() >= max_size_ && access_order_.size() > 0) {
            auto oldest = access_order_.front();
            access_order_.erase(access_order_.begin());
            cache_.erase(oldest);
        }

        cache_[vak] = std::move(artha);
        access_order_.push_back(vak);
    }

    // Recall a remembered meaning
    const Artha* recall(const std::string& vak) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(vak);
        return it != cache_.end() ? &it->second : nullptr;
    }

    // Forget everything
    void forget() {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
        access_order_.clear();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_.size();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Artha> cache_;
    std::vector<std::string> access_order_;
    size_t max_size_;
};

// VakYantra: the machine of speech - abstract embedder interface
// (Yantra = instrument/machine)
class VakYantra {
public:
    virtual ~VakYantra() = default;

    // Transform utterance into meaning
    virtual Artha transform(const std::string& vak) = 0;

    // Transform multiple utterances (batch for efficiency)
    virtual std::vector<Artha> transform_batch(const std::vector<std::string>& vaks) {
        std::vector<Artha> results;
        results.reserve(vaks.size());
        for (const auto& vak : vaks) {
            results.push_back(transform(vak));
        }
        return results;
    }

    // Get the dimension of the semantic space
    virtual size_t dimension() const = 0;

    // Is the yantra ready?
    virtual bool ready() const = 0;
};

// ShantaYantra: the silent machine - returns zeros
// (Shanta = peaceful/silent) - used when embeddings come from elsewhere
class ShantaYantra : public VakYantra {
public:
    Artha transform(const std::string& vak) override {
        Artha artha;
        artha.nu = Vector::zeros();
        artha.certainty = 0.0f;  // No certainty - we didn't compute this
        artha.source = vak;
        return artha;
    }

    size_t dimension() const override { return EMBED_DIM; }
    bool ready() const override { return true; }
};

// SmritiYantra: the memory machine - wraps any yantra with caching
// (Smriti = memory)
class SmritiYantra : public VakYantra {
public:
    SmritiYantra(std::shared_ptr<VakYantra> inner, size_t cache_size = 10000)
        : inner_(std::move(inner)), kosha_(cache_size) {}

    Artha transform(const std::string& vak) override {
        // Try to recall from memory
        if (const Artha* remembered = kosha_.recall(vak)) {
            return *remembered;
        }

        // Transform and remember
        Artha artha = inner_->transform(vak);
        kosha_.remember(vak, artha);
        return artha;
    }

    std::vector<Artha> transform_batch(const std::vector<std::string>& vaks) override {
        std::vector<Artha> results;
        std::vector<std::string> to_compute;
        std::vector<size_t> compute_indices;

        results.resize(vaks.size());

        // Check cache first
        for (size_t i = 0; i < vaks.size(); ++i) {
            if (const Artha* remembered = kosha_.recall(vaks[i])) {
                results[i] = *remembered;
            } else {
                to_compute.push_back(vaks[i]);
                compute_indices.push_back(i);
            }
        }

        // Compute missing
        if (!to_compute.empty()) {
            auto computed = inner_->transform_batch(to_compute);
            for (size_t i = 0; i < computed.size(); ++i) {
                results[compute_indices[i]] = computed[i];
                kosha_.remember(to_compute[i], computed[i]);
            }
        }

        return results;
    }

    size_t dimension() const override { return inner_->dimension(); }
    bool ready() const override { return inner_->ready(); }

    // Direct access to memory
    SmritiKosha& kosha() { return kosha_; }
    const SmritiKosha& kosha() const { return kosha_; }

    // Pre-load a meaning into memory
    void implant(const std::string& vak, Artha artha) {
        kosha_.remember(vak, std::move(artha));
    }

private:
    std::shared_ptr<VakYantra> inner_;
    SmritiKosha kosha_;
};

} // namespace synapse
