#pragma once
// Embedder: Yantra wrapper for text→embedding transformation
//
// Simple interface for embedding text using ONNX models.

#include "../vak.hpp"
#include "../types.hpp"
#include <memory>
#include <shared_mutex>

namespace chitta {

class Embedder {
public:
    Embedder() = default;

    void attach(std::shared_ptr<VakYantra> yantra) {
        std::unique_lock lock(mutex_);
        yantra_ = std::move(yantra);
    }

    bool ready() const {
        std::shared_lock lock(mutex_);
        return yantra_ && yantra_->ready();
    }

    // Embed single text
    Vector embed(const std::string& text) {
        std::shared_lock lock(mutex_);
        if (!yantra_ || !yantra_->ready()) {
            return Vector::zeros();
        }
        auto artha = yantra_->transform(text);
        return artha.nu;
    }

    // Embed batch of texts
    std::vector<Vector> embed_batch(const std::vector<std::string>& texts) {
        std::shared_lock lock(mutex_);
        if (!yantra_ || !yantra_->ready()) {
            std::vector<Vector> zeros(texts.size());
            for (auto& v : zeros) v = Vector::zeros();
            return zeros;
        }

        auto arthas = yantra_->transform_batch(texts);
        std::vector<Vector> results;
        results.reserve(arthas.size());
        for (const auto& artha : arthas) {
            results.push_back(artha.nu);
        }
        return results;
    }

    // Full Artha (embedding + metadata)
    Artha transform(const std::string& text) {
        std::shared_lock lock(mutex_);
        if (!yantra_ || !yantra_->ready()) {
            return Artha{};
        }
        return yantra_->transform(text);
    }

    std::vector<Artha> transform_batch(const std::vector<std::string>& texts) {
        std::shared_lock lock(mutex_);
        if (!yantra_ || !yantra_->ready()) {
            return std::vector<Artha>(texts.size());
        }
        return yantra_->transform_batch(texts);
    }

    // Access underlying yantra for advanced operations
    std::shared_ptr<VakYantra> yantra() const {
        std::shared_lock lock(mutex_);
        return yantra_;
    }

private:
    mutable std::shared_mutex mutex_;
    std::shared_ptr<VakYantra> yantra_;
};

}  // namespace chitta
