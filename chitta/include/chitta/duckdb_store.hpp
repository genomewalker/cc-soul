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
#include <queue>
#include <condition_variable>

namespace chitta {

// Connection pool for concurrent reads
// DuckDB Database is thread-safe, but Connection is not
// Multiple connections can execute concurrent reads via MVCC
class ConnectionPool {
public:
    ConnectionPool(duckdb::DuckDB& db, size_t max_size = 8)
        : db_(db), max_size_(max_size), created_(0) {}

    // RAII wrapper for borrowed connection
    class ScopedConnection {
    public:
        ScopedConnection(ConnectionPool& pool, std::unique_ptr<duckdb::Connection> conn)
            : pool_(pool), conn_(std::move(conn)) {}

        ~ScopedConnection() {
            if (conn_) pool_.release(std::move(conn_));
        }

        // Non-copyable, movable
        ScopedConnection(const ScopedConnection&) = delete;
        ScopedConnection& operator=(const ScopedConnection&) = delete;
        ScopedConnection(ScopedConnection&& other) noexcept
            : pool_(other.pool_), conn_(std::move(other.conn_)) {}

        duckdb::Connection& operator*() { return *conn_; }
        duckdb::Connection* operator->() { return conn_.get(); }

    private:
        ConnectionPool& pool_;
        std::unique_ptr<duckdb::Connection> conn_;
    };

    // Acquire a connection (creates new if pool empty and under limit)
    ScopedConnection acquire() {
        std::unique_lock lock(mutex_);

        if (!pool_.empty()) {
            auto conn = std::move(pool_.front());
            pool_.pop();
            return ScopedConnection(*this, std::move(conn));
        }

        if (created_ < max_size_) {
            created_++;
            lock.unlock();
            return ScopedConnection(*this, std::make_unique<duckdb::Connection>(db_));
        }

        // Wait for a connection to be returned
        cv_.wait(lock, [this] { return !pool_.empty(); });
        auto conn = std::move(pool_.front());
        pool_.pop();
        return ScopedConnection(*this, std::move(conn));
    }

private:
    void release(std::unique_ptr<duckdb::Connection> conn) {
        std::lock_guard lock(mutex_);
        pool_.push(std::move(conn));
        cv_.notify_one();
    }

    duckdb::DuckDB& db_;
    size_t max_size_;
    size_t created_;
    std::queue<std::unique_ptr<duckdb::Connection>> pool_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

// Realm visibility levels
enum class RealmVisibility : uint8_t {
    Private = 0,   // Only visible in primary realm
    Shared = 1,    // Visible in primary + shared realms
    Global = 2,    // Visible everywhere (brahman)
};

// Memory result from vector search
struct MemoryResult {
    int64_t id;
    std::string kind;
    std::string content;
    float confidence;
    float similarity;
    Timestamp created_at;
    Timestamp accessed_at;
    std::string realm;                      // Primary realm
    RealmVisibility visibility = RealmVisibility::Private;
    std::vector<std::string> shared_realms; // For Shared visibility
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

// Ledger entry for session continuity
struct LedgerEntry {
    int64_t id = 0;
    std::string session_id;
    std::string project;
    int64_t created_at = 0;

    // Soul state
    std::string mood;
    float coherence = 0.0f;
    float confidence = 0.0f;

    // Work state (JSON strings)
    std::string todos;           // JSON array of {content, status}
    std::string active_files;    // JSON array of file paths
    std::string decisions;       // JSON array of key decisions

    // Continuation (JSON strings)
    std::string next_steps;      // JSON array of next steps
    std::string blockers;        // JSON array of blockers
    std::string discoveries;     // JSON array of discoveries

    // Full snapshot for reconstruction
    std::string snapshot;
};

// Code file metadata for incremental indexing
struct CodeFile {
    std::string path;           // Full file path (primary key)
    std::string project;        // Project identifier
    int64_t mtime = 0;          // Last modification time
    int64_t indexed_at = 0;     // When we last indexed
    int32_t symbols_count = 0;  // Number of symbols extracted
    int32_t callsites_count = 0;// Number of callsites extracted
    std::string file_hash;      // Optional content hash
};

// Transcript state for distillation (reads JSONL directly)
struct TranscriptState {
    std::string session_id;         // Claude session ID (primary key)
    std::string transcript_path;    // Path to .jsonl file
    std::string realm;              // Project/realm isolation
    int64_t last_processed_line = 0;// Last line number processed
    int64_t last_distilled_at = 0;  // When we last distilled
    int64_t created_at = 0;
};

// Parsed conversation turn from JSONL
struct TranscriptTurn {
    std::string role;       // "user" or "assistant"
    std::string content;    // Message content
    int64_t line_number;    // Line in JSONL file
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
        float decay_rate = 0.05f,
        const std::string& realm = "brahman",
        RealmVisibility visibility = RealmVisibility::Private,
        const std::vector<std::string>& shared_realms = {}
    );

    std::vector<MemoryResult> recall(
        const std::vector<float>& query_embedding,
        size_t k = 10,
        const std::string& realm = "",      // Empty = all realms
        bool include_global = true          // Include brahman/global memories
    );

    // Realm management
    bool set_realm(int64_t id, const std::string& realm);
    bool set_visibility(int64_t id, RealmVisibility visibility);
    bool add_to_realm(int64_t id, const std::string& realm);      // Multi-realm membership
    bool remove_from_realm(int64_t id, const std::string& realm);
    std::vector<std::string> get_realms(int64_t id);              // Get all realms for a memory
    std::vector<std::string> list_realms();                        // List all known realms

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

    // Connect with source file tracking (for fast file-based deletion)
    bool connect_with_source(
        const std::string& subject,
        const std::string& predicate,
        const std::string& object,
        const std::string& source_file,
        float weight = 1.0f
    );

    // Batch connect: (subject, predicate, object, source_file) tuples
    // Uses DuckDB Appender for maximum speed (bypasses SQL parser)
    size_t connect_batch(
        const std::vector<std::tuple<std::string, std::string, std::string, std::string>>& triplets,
        float weight = 1.0f
    );

    // Fallback SQL-based batch insert (if Appender fails)
    size_t connect_batch_sql(
        const std::vector<std::tuple<std::string, std::string, std::string, std::string>>& triplets,
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

    // Efficient attractor finding (excludes code intel triplets)
    std::vector<std::pair<std::string, size_t>> get_top_connected_entities(size_t limit = 20);

    // BM25 full-text search on symbols (no embeddings needed)
    std::vector<Symbol> bm25_search_symbols(const std::string& query, size_t limit = 10);
    bool has_fts() const;

    // Ledger operations (session continuity)
    int64_t save_ledger(const LedgerEntry& entry);
    std::optional<LedgerEntry> load_ledger(const std::string& session_id = "", const std::string& project = "");
    std::vector<LedgerEntry> list_ledgers(const std::string& project = "", size_t limit = 10);
    std::optional<LedgerEntry> get_ledger(int64_t id);
    bool delete_ledger(int64_t id);

    // Code file tracking (incremental indexing)
    bool set_file_metadata(const CodeFile& file);
    std::optional<CodeFile> get_file_metadata(const std::string& path);
    std::vector<CodeFile> list_project_files(const std::string& project);
    bool delete_file_metadata(const std::string& path);
    bool delete_project_files(const std::string& project);  // Clear all files for project

    // Delete symbols/triplets for a file (before re-indexing)
    size_t delete_file_symbols(const std::string& file_path);
    size_t delete_file_triplets(const std::string& file_path);

    // Clear entire project codebase (symbols, triplets, file metadata)
    struct ClearProjectResult {
        size_t files_deleted = 0;
        size_t symbols_deleted = 0;
        size_t triplets_deleted = 0;
    };
    ClearProjectResult clear_project_codebase(const std::string& project);

    // Delete triplets by subject pattern (SQL LIKE)
    size_t count_triplets_by_pattern(const std::string& pattern);
    size_t delete_triplets_by_pattern(const std::string& pattern);

    // Semantic enrichment for code symbols
    struct UndescribedSymbol {
        int64_t id;
        std::string kind;
        std::string name;
        std::string signature;
        std::string file_path;
        int line_start;
        int line_end;
        int priority;  // Lower = more important (class=0, function=1, method=2)
    };
    std::vector<UndescribedSymbol> get_undescribed_symbols(size_t limit = 10);
    bool set_symbol_memory(int64_t symbol_id, int64_t memory_id);
    size_t count_undescribed_symbols();
    size_t count_total_symbols();

    // Transcript state operations (for distillation)
    bool register_transcript(const std::string& session_id, const std::string& transcript_path,
                             const std::string& realm = "default");
    std::optional<TranscriptState> get_transcript(const std::string& session_id);
    std::vector<TranscriptState> get_pending_transcripts();  // Transcripts with new content
    bool update_transcript_progress(const std::string& session_id, int64_t last_line);
    bool mark_transcript_distilled(const std::string& session_id);
    bool remove_transcript(const std::string& session_id);
    size_t transcript_count();

private:
    std::unique_ptr<duckdb::DuckDB> db_;
    std::unique_ptr<duckdb::Connection> write_conn_;  // Dedicated write connection
    std::unique_ptr<ConnectionPool> read_pool_;       // Pool for concurrent reads
    std::string path_;
    mutable std::mutex write_mutex_;                  // Only for write operations
    bool vss_loaded_ = false;
    bool pgq_loaded_ = false;
    bool fts_loaded_ = false;

    // Schema creation
    bool create_schema();
    bool load_extensions();
    bool create_vector_index();

    // Helper to execute queries
    // write_execute/write_query: use write connection with mutex (for INSERT/UPDATE/DELETE)
    // read_query: use pool connection (for SELECT) - concurrent safe
    bool write_execute(const std::string& sql);
    std::unique_ptr<duckdb::QueryResult> write_query(const std::string& sql);
    std::unique_ptr<duckdb::QueryResult> read_query(const std::string& sql) const;

    // Convert between types
    static std::string kind_to_string(NodeType type);
    static NodeType string_to_kind(const std::string& kind);

    // Build array literal for embedding
    static std::string embedding_to_sql(const std::vector<float>& embedding);
};

}  // namespace chitta
