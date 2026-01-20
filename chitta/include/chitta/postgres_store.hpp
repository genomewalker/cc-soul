#pragma once
// PostgresStore: Multi-writer storage with vector search and graph queries
//
// Uses PostgreSQL with:
// - pgvector extension for HNSW vector search
// - Apache AGE extension for graph queries (openCypher)
// - True MVCC for concurrent writers (ideal for HPC)
//
// Each HPC node can run its own daemon, all sharing the same PostgreSQL database.

#include "types.hpp"
#include <libpq-fe.h>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <mutex>

namespace chitta {

// Reuse types from duckdb_store.hpp
struct MemoryResult;
struct StringTriplet;
struct Symbol;
struct StoreHealth;

// PostgreSQL connection configuration
struct PostgresConfig {
    std::string host = "localhost";
    int port = 5432;
    std::string dbname = "soul";
    std::string user = "soul";
    std::string password = "";

    // Connection string format: "host=localhost port=5432 dbname=soul user=soul"
    std::string connection_string() const {
        std::string conn = "host=" + host + " port=" + std::to_string(port) +
                          " dbname=" + dbname + " user=" + user;
        if (!password.empty()) {
            conn += " password=" + password;
        }
        return conn;
    }
};

// PostgresStore: Same interface as DuckDBStore but uses PostgreSQL
class PostgresStore {
public:
    PostgresStore() = default;
    ~PostgresStore();

    // Lifecycle
    bool open(const PostgresConfig& config);
    bool open(const std::string& connection_string);
    void close();
    bool is_open() const { return conn_ != nullptr; }

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
    bool touch(int64_t id);

    std::optional<MemoryResult> get_memory(int64_t id);
    size_t apply_decay();
    size_t prune(float threshold = 0.1f, float min_age_days = 7.0f);

    // Graph operations (triplets)
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

    // Check if extensions are available
    bool has_pgvector() const { return pgvector_available_; }
    bool has_age() const { return age_available_; }

private:
    PGconn* conn_ = nullptr;
    mutable std::mutex mutex_;
    bool pgvector_available_ = false;
    bool age_available_ = false;

    // Schema creation
    bool create_schema();
    bool check_extensions();
    bool create_vector_index();

    // Helper to execute a query
    bool execute(const std::string& sql);
    PGresult* query(const std::string& sql);

    // Build vector literal for PostgreSQL
    static std::string embedding_to_pgvector(const std::vector<float>& embedding);

    // Escape string for SQL
    std::string escape(const std::string& str);
};

}  // namespace chitta
