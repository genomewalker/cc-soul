#pragma once
// EmbeddingExporter: export memory pairs for BGE embedding fine-tuning.
// Generates query-passage JSONL suitable for sentence-transformers training.

#include "field_store.hpp"
#include <string>
#include <vector>
#include <functional>

namespace chitta {

struct EmbeddingExportConfig {
    std::string output_path;  // default: ~/.claude/training/pairs.jsonl
    std::string realm = "";   // empty = all realms
    size_t max_pairs = 10000;
    float min_confidence = 0.5f;
    float min_strength = 0.1f;
    bool include_negatives = true;  // generate hard negatives
};

struct EmbeddingExportResult {
    int pairs_exported = 0;
    int negatives_generated = 0;
    bool success = false;
    std::string output_path;
    std::string error;
};

class EmbeddingExporter {
public:
    using EmbedFn = std::function<std::vector<float>(const std::string&)>;

    EmbeddingExporter(FieldStore& field, EmbedFn embedder,
                      const EmbeddingExportConfig& config);

    EmbeddingExportResult export_pairs();

    using LogCallback = std::function<void(const std::string&)>;
    void set_log_callback(LogCallback cb) { log_callback_ = cb; }

private:
    FieldStore* field_store_;
    EmbedFn embedder_;
    EmbeddingExportConfig config_;
    LogCallback log_callback_;

    void log(const std::string& msg);
    void write_jsonl(const std::string& path, const std::vector<std::string>& lines);
    std::string make_query(const std::string& content);
};

} // namespace chitta
