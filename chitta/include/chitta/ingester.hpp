#pragma once
// Ingester: fetches external content (URL, file, directory), chunks it,
// runs through SSL distillation, stores memories + triplets.

#include "field_store.hpp"
#include "native_distiller.hpp"
#include "llm_http.hpp"
#include "ssl_parser.hpp"
#include "ssl_prompt.hpp"
#include <string>
#include <vector>
#include <functional>
#include <filesystem>

namespace chitta {

enum class SourceType { Auto, Url, File, Directory };

struct IngestConfig {
    std::string model = "gemma4:26b";
    std::string endpoint = "";       // OpenAI-compatible endpoint (auto-discovered if empty)
    // LLM backend is always HTTP to Ollama/vLLM (GPU endpoint auto-discovered)
    int timeout_secs = 180;
    size_t chunk_size_chars = 8000;
    size_t chunk_overlap = 500;
    float dedup_threshold = 0.92f;
    int max_chunks = 30;
    bool verbose = false;
};

struct IngestResult {
    int chunks_processed = 0;
    int learnings_stored = 0;
    int learnings_deduped = 0;
    int triplets_created = 0;
    bool success = false;
    std::string error;
};

class Ingester {
public:
    Ingester(FieldStore& field, EmbedFn embedder, const IngestConfig& config);

    IngestResult ingest(const std::string& source, const std::string& realm,
                        SourceType type = SourceType::Auto);

    using LogCallback = std::function<void(const std::string&)>;
    void set_log_callback(LogCallback cb) { log_callback_ = cb; }

private:
    FieldStore* field_store_;
    EmbedFn embedder_;
    IngestConfig config_;
    SSLParser ssl_parser_;
    LogCallback log_callback_;

    static SourceType detect_type(const std::string& source);
    std::string fetch_url(const std::string& url);
    std::string read_file(const std::string& path);
    std::vector<std::string> read_directory(const std::string& path);
    std::vector<std::string> chunk_text(const std::string& text);
    std::string call_llm(const std::string& prompt);
    std::string cached_endpoint_;
    std::string build_ingest_prompt(const std::string& chunk, const std::string& source);
    void store_learnings(const SSLParser::Result& ssl, const std::string& realm,
                         const std::string& source, IngestResult& result);
    void log(const std::string& msg);
};

} // namespace chitta
