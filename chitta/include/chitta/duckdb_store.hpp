#pragma once
// DuckDBStore: Durable storage with vector search and graph queries
//
// Uses DuckDB embedded database with:
// - VSS extension for HNSW vector search
// - DuckPGQ for graph queries (SQL/PGQ)
// - WAL for crash recovery

#include "types.hpp"
#include <duckdb.hpp>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <mutex>

namespace chitta {

// Memory result from vector search
struct MemoryResult {
    int64_t id;
    std::string kind;
    std::string content;
    float confidence;
    float similarity;
    Timestamp created_at;
    Timestamp accessed_at;
};

// String-based triplet for DuckDB (different from NodeId-based Triplet in types.hpp)
struct StringTriplet {
    std::string subject;
    std::string predicate;
    std::string object;
    float weight;
};

// Code symbol for code intelligence
struct Symbol {
    int64_t id = 0;
    std::string kind;       // function, class, file, module
    std::string name;
    std::string signature;
    std::string file_path;
    int32_t line_start = 0;
    int32_t line_end = 0;
    int64_t repo_id = 0;
};

// Health metrics for the store
struct StoreHealth {
    size_t total_memories = 0;
    size_t total_symbols = 0;
    size_t total_triplets = 0;
    float avg_confidence = 0.0f;
    bool is_open = false;
};

// DuckDBStore: unified storage using DuckDB embedded database
class DuckDBStore {
public:
    DuckDBStore() = default;
    ~DuckDBStore();

    // Lifecycle
    bool open(const std::string& path);
    void close();
    bool is_open() const { return db_ != nullptr; }

    // Memory operations
    int64_t remember(
        const std::string& content,
        const std::string& kind,
        const std::vector<float>& embedding,
        float confidence = 0.8f,
        float decay_rate = 0.05f
    );

    std::vector<MemoryResult> recall(
        const std::vector<float>& query_embedding,
        size_t k = 10
    );

    bool strengthen(int64_t id, float amount = 0.1f);
    bool weaken(int64_t id, float amount = 0.1f);
    bool forget(int64_t id);
    bool touch(int64_t id);  // Update accessed_at

    // Get memory by ID
    std::optional<MemoryResult> get_memory(int64_t id);

    // Update memory content
    bool update_content(int64_t id, const std::string& new_content);

    // Tag management
    bool add_tag(int64_t id, const std::string& tag);
    bool remove_tag(int64_t id, const std::string& tag);
    std::vector<std::string> get_tags(int64_t id);

    // Apply decay to all memories (returns count updated)
    size_t apply_decay();

    // Prune weak memories (returns count removed)
    size_t prune(float threshold = 0.1f, float min_age_days = 7.0f);

    // Graph operations (triplets with string entities)
    bool connect(
        const std::string& subject,
        const std::string& predicate,
        const std::string& object,
        float weight = 1.0f
    );

    std::vector<StringTriplet> query_subject(const std::string& subject);
    std::vector<StringTriplet> query_object(const std::string& object);
    std::vector<StringTriplet> query_predicate(const std::string& predicate);

    // Code intelligence
    int64_t add_symbol(const Symbol& sym, const std::vector<float>& embedding = {});
    std::vector<Symbol> find_symbol(const std::string& name, const std::string& kind = "");
    std::vector<int64_t> callers(int64_t symbol_id);
    std::vector<int64_t> callees(int64_t symbol_id);
    bool add_call(int64_t caller_id, int64_t callee_id);

    // Health and stats
    StoreHealth health();
    size_t memory_count();
    size_t triplet_count();
    size_t symbol_count();

private:
    std::unique_ptr<duckdb::DuckDB> db_;
    std::unique_ptr<duckdb::Connection> conn_;
    std::string path_;
    mutable std::mutex mutex_;
    bool vss_loaded_ = false;
    bool pgq_loaded_ = false;

    // Schema creation
    bool create_schema();
    bool load_extensions();
    bool create_vector_index();

    // Helper to execute a query
    bool execute(const std::string& sql);
    std::unique_ptr<duckdb::QueryResult> query(const std::string& sql);

    // Convert between types
    static std::string kind_to_string(NodeType type);
    static NodeType string_to_kind(const std::string& kind);

    // Build array literal for embedding
    static std::string embedding_to_sql(const std::vector<float>& embedding);
};

}  // namespace chitta
