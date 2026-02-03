#include "chitta/duckdb_store.hpp"
#include <iostream>
#include <sstream>
#include <cmath>
#include <chrono>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace chitta {

DuckDBStore::~DuckDBStore() {
    close();
}

bool DuckDBStore::open(const std::string& path) {
    // No lock here - write_execute/write_query handle their own locking
    // open() is single-threaded (called once at startup)

    if (db_) {
        std::cerr << "[DuckDBStore] Already open\n";
        return false;
    }

    try {
        path_ = path;

        // Create database with WAL enabled and aggressive thread limits
        // CRITICAL: Limit threads to prevent explosion on high-core systems (96+ cores)
        duckdb::DBConfig config;
        config.SetOption("threads", duckdb::Value(2));  // Minimal threads
        config.SetOption("external_threads", duckdb::Value(0));  // No external thread spawning
        config.SetOption("enable_external_access", duckdb::Value(true));

        db_ = std::make_unique<duckdb::DuckDB>(path_, &config);
        write_conn_ = std::make_unique<duckdb::Connection>(*db_);

        // Force thread limit via PRAGMA as well (belt and suspenders)
        write_conn_->Query("PRAGMA threads=2");

        // Initialize connection pool for concurrent reads (minimal pool)
        read_pool_ = std::make_unique<ConnectionPool>(*db_, 2);

        // Load extensions
        if (!load_extensions()) {
            std::cerr << "[DuckDBStore] Warning: Some extensions failed to load\n";
        }

        // Create schema
        if (!create_schema()) {
            std::cerr << "[DuckDBStore] Failed to create schema\n";
            close();
            return false;
        }

        // Create vector index (if VSS available)
        if (vss_loaded_) {
            create_vector_index();
        }

        // Fix sequences if out of sync (e.g., after restore or manual modification)
        fix_sequences();

        // Open separate embeddings database (for contention-free embedding writes)
        std::string emb_path = path_;
        size_t dot_pos = emb_path.rfind('.');
        if (dot_pos != std::string::npos) {
            emb_path = emb_path.substr(0, dot_pos) + "_embeddings.duckdb";
        } else {
            emb_path += "_embeddings.duckdb";
        }
        if (!open_embeddings_db(emb_path)) {
            std::cerr << "[DuckDBStore] Warning: Embeddings DB failed, using main DB\n";
        }

        std::cerr << "[DuckDBStore] Opened database at " << path_ << "\n";
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[DuckDBStore] Failed to open: " << e.what() << "\n";
        db_.reset();
        write_conn_.reset();
        return false;
    }
}

void DuckDBStore::close() {
    std::lock_guard lock(write_mutex_);
    std::lock_guard emb_lock(emb_mutex_);

    // Close embeddings database first
    emb_conn_.reset();
    emb_db_.reset();
    emb_attached_ = false;

    // Close main database
    read_pool_.reset();
    write_conn_.reset();
    db_.reset();
    vss_loaded_ = false;
    pgq_loaded_ = false;
    fts_loaded_ = false;
}

void DuckDBStore::fix_sequences() {
    // Helper to fix a sequence from a table
    auto fix_seq = [this](const std::string& table, const std::string& seq_name) {
        auto result = read_query("SELECT COALESCE(MAX(id), 0) as max_id FROM " + table);
        if (result && !result->HasError()) {
            auto chunk = result->Fetch();
            if (chunk && chunk->size() > 0) {
                int64_t max_id = chunk->GetValue(0, 0).GetValue<int64_t>();
                write_execute("DROP SEQUENCE IF EXISTS " + seq_name);
                write_execute("CREATE SEQUENCE " + seq_name + " START " + std::to_string(max_id + 1));
                std::cerr << "[DuckDBStore] Fixed " << seq_name << " to start at " << (max_id + 1) << "\n";
            }
        }
    };

    fix_seq("memory", "memory_seq");
    fix_seq("symbols", "symbol_seq");
    fix_seq("ledger", "ledger_seq");
    fix_seq("long_task", "task_seq");
    fix_seq("task_event", "event_seq");
}

bool DuckDBStore::open_embeddings_db(const std::string& path) {
    try {
        // Aggressive thread limits for embeddings DB
        duckdb::DBConfig config;
        config.SetOption("threads", duckdb::Value(2));
        config.SetOption("external_threads", duckdb::Value(0));
        config.SetOption("enable_external_access", duckdb::Value(true));

        emb_db_ = std::make_unique<duckdb::DuckDB>(path, &config);
        emb_conn_ = std::make_unique<duckdb::Connection>(*emb_db_);

        // Force thread limit via PRAGMA
        emb_conn_->Query("PRAGMA threads=2");

        // Load VSS extension for embeddings DB too
        try {
            emb_conn_->Query("INSTALL vss; LOAD vss;");
        } catch (...) {
            // VSS not required for embeddings DB
        }

        if (!create_embeddings_schema()) {
            emb_conn_.reset();
            emb_db_.reset();
            return false;
        }

        // Attach to main database for queries
        if (!attach_embeddings_db()) {
            std::cerr << "[DuckDBStore] Warning: Could not attach embeddings DB\n";
        }

        std::cerr << "[DuckDBStore] Embeddings DB opened at " << path << "\n";
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[DuckDBStore] Embeddings DB failed: " << e.what() << "\n";
        emb_conn_.reset();
        emb_db_.reset();
        return false;
    }
}

bool DuckDBStore::create_embeddings_schema() {
    if (!emb_conn_) return false;

    try {
        // Enable experimental persistence for HNSW in embeddings DB
        emb_conn_->Query("SET hnsw_enable_experimental_persistence = true");

        // Symbol embeddings table
        emb_conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS symbol_embeddings (
                symbol_id INTEGER PRIMARY KEY,
                embedding FLOAT[384],
                created_at TIMESTAMP DEFAULT current_timestamp
            )
        )");

        // Memory embeddings table - HNSW index lives here (isolated from main DB)
        emb_conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS memory_embeddings (
                memory_id INTEGER PRIMARY KEY,
                embedding FLOAT[384],
                created_at TIMESTAMP DEFAULT current_timestamp
            )
        )");

        // HNSW index creation deferred to rebuild_vector_index() to avoid startup hang
        // The index will be created during maintenance cycle
        std::cerr << "[DuckDBStore] HNSW index deferred to maintenance\n";

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[DuckDBStore] Embeddings schema error: " << e.what() << "\n";
        return false;
    }
}

bool DuckDBStore::attach_embeddings_db() {
    if (!db_ || !emb_db_) return false;

    try {
        // Get embeddings DB path from connection
        auto result = emb_conn_->Query("SELECT current_database()");
        // We'll use the path we stored - for now, just mark as attached
        // Actual attachment would use: ATTACH 'path' AS emb;
        emb_attached_ = true;
        return true;
    } catch (...) {
        return false;
    }
}

bool DuckDBStore::load_extensions() {
    // Try to load VSS extension for vector search
    try {
        write_conn_->Query("INSTALL vss; LOAD vss;");
        vss_loaded_ = true;
        std::cerr << "[DuckDBStore] VSS extension loaded\n";
    } catch (...) {
        std::cerr << "[DuckDBStore] VSS extension not available (will use brute force)\n";
    }

    // Try to load DuckPGQ for graph queries
    try {
        write_conn_->Query("INSTALL duckpgq FROM community; LOAD duckpgq;");
        pgq_loaded_ = true;
        std::cerr << "[DuckDBStore] DuckPGQ extension loaded\n";
    } catch (...) {
        std::cerr << "[DuckDBStore] DuckPGQ not available (will use SQL joins)\n";
    }

    // Try to load FTS extension for full-text search with BM25
    try {
        write_conn_->Query("INSTALL fts; LOAD fts;");
        fts_loaded_ = true;
        std::cerr << "[DuckDBStore] FTS extension loaded\n";
    } catch (...) {
        std::cerr << "[DuckDBStore] FTS not available (will use LIKE)\n";
    }

    return true;
}

bool DuckDBStore::create_schema() {
    // Memory table with ARRAY for embeddings
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS memory (
            id BIGINT PRIMARY KEY,
            kind VARCHAR,
            content VARCHAR,
            confidence FLOAT,
            decay_rate FLOAT,
            created_at BIGINT,
            accessed_at BIGINT,
            embedding FLOAT[384],
            realm VARCHAR DEFAULT 'brahman',
            visibility INTEGER DEFAULT 0
        )
    )")) {
        return false;
    }

    // Realm membership table for multi-realm support
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS realm_membership (
            memory_id BIGINT,
            realm VARCHAR,
            PRIMARY KEY (memory_id, realm)
        )
    )")) {
        return false;
    }

    // Index for realm filtering
    write_execute("CREATE INDEX IF NOT EXISTS idx_memory_realm ON memory(realm)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_realm_membership ON realm_membership(realm)");

    // Triplet table for graph relationships
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS triplet (
            id BIGINT PRIMARY KEY,
            subject VARCHAR,
            predicate VARCHAR,
            object VARCHAR,
            weight FLOAT DEFAULT 1.0,
            created_at BIGINT,
            source_file VARCHAR DEFAULT ''
        )
    )")) {
        return false;
    }

    // Migration: add source_file column if missing (for databases created before v3.3.0)
    write_execute("ALTER TABLE triplet ADD COLUMN IF NOT EXISTS source_file VARCHAR DEFAULT ''");

    // Indexes for triplet queries
    write_execute("CREATE INDEX IF NOT EXISTS idx_triplet_subject ON triplet(subject)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_triplet_object ON triplet(object)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_triplet_predicate ON triplet(predicate)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_triplet_source_file ON triplet(source_file)");

    // Symbol table for code intelligence
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS symbol (
            id BIGINT PRIMARY KEY,
            kind VARCHAR,
            name VARCHAR,
            signature VARCHAR,
            file_path VARCHAR,
            line_start INTEGER,
            line_end INTEGER,
            repo_id BIGINT,
            embedding FLOAT[384]
        )
    )")) {
        return false;
    }

    // Migration: add memory_id for semantic descriptions (v3.7.5+)
    write_execute("ALTER TABLE symbol ADD COLUMN IF NOT EXISTS memory_id BIGINT DEFAULT NULL");
    write_execute("ALTER TABLE symbol ADD COLUMN IF NOT EXISTS described_at BIGINT DEFAULT 0");

    // Migration: add description column to store descriptions directly in symbol table (v3.24+)
    // This replaces the previous approach of creating separate wisdom memories
    write_execute("ALTER TABLE symbol ADD COLUMN IF NOT EXISTS description VARCHAR DEFAULT NULL");

    // Call graph table
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS call_edge (
            caller_id BIGINT,
            callee_id BIGINT,
            PRIMARY KEY (caller_id, callee_id)
        )
    )")) {
        return false;
    }

    // Index for symbol lookup
    write_execute("CREATE INDEX IF NOT EXISTS idx_symbol_name ON symbol(name)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_symbol_kind ON symbol(kind)");

    // FTS index for BM25 search on symbols (if FTS extension loaded)
    if (fts_loaded_) {
        try {
            // Create FTS index on symbol name and signature for keyword search
            write_execute("PRAGMA create_fts_index('symbol', 'id', 'name', 'signature', 'file_path', overwrite=1)");
            std::cerr << "[DuckDBStore] Created FTS index on symbol table\n";
        } catch (...) {
            std::cerr << "[DuckDBStore] FTS index creation failed\n";
        }

        // FTS index on memory content for hybrid recall (semantic + keyword)
        try {
            write_execute("PRAGMA create_fts_index('memory', 'id', 'content', overwrite=1)");
            std::cerr << "[DuckDBStore] Created FTS index on memory table\n";
        } catch (...) {
            std::cerr << "[DuckDBStore] Memory FTS index creation failed\n";
        }
    }

    // Ledger table for session continuity
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS ledger (
            id BIGINT PRIMARY KEY,
            session_id VARCHAR NOT NULL,
            project VARCHAR DEFAULT 'default',
            created_at BIGINT NOT NULL,
            mood VARCHAR,
            coherence FLOAT,
            confidence FLOAT,
            todos TEXT,
            active_files TEXT,
            decisions TEXT,
            next_steps TEXT,
            blockers TEXT,
            discoveries TEXT,
            snapshot TEXT
        )
    )")) {
        return false;
    }

    // Indexes for ledger queries
    write_execute("CREATE INDEX IF NOT EXISTS idx_ledger_session ON ledger(session_id)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_ledger_project ON ledger(project)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_ledger_created ON ledger(created_at DESC)");

    // Code files table for incremental indexing
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS code_file (
            path VARCHAR PRIMARY KEY,
            project VARCHAR NOT NULL,
            mtime BIGINT NOT NULL,
            indexed_at BIGINT NOT NULL,
            symbols_count INTEGER DEFAULT 0,
            callsites_count INTEGER DEFAULT 0,
            file_hash VARCHAR
        )
    )")) {
        return false;
    }

    // Indexes for code file queries
    write_execute("CREATE INDEX IF NOT EXISTS idx_code_file_project ON code_file(project)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_code_file_mtime ON code_file(mtime)");

    // Transcript state table for distillation (reads JSONL directly)
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS transcript_state (
            session_id VARCHAR PRIMARY KEY,
            transcript_path VARCHAR NOT NULL,
            realm VARCHAR DEFAULT 'default',
            last_processed_line BIGINT DEFAULT 0,
            last_distilled_at BIGINT DEFAULT 0,
            created_at BIGINT NOT NULL
        )
    )")) {
        return false;
    }

    // Migration: Ensure transcript_state has PRIMARY KEY on session_id
    // Old tables might exist without the constraint
    {
        auto result = read_query(
            "SELECT constraint_type FROM information_schema.table_constraints "
            "WHERE table_name = 'transcript_state' AND constraint_type = 'PRIMARY KEY'"
        );
        bool has_pk = false;
        if (result && !result->HasError()) {
            auto chunk = result->Fetch();
            has_pk = chunk && chunk->size() > 0;
        }
        if (!has_pk) {
            std::cerr << "[DuckDBStore] Migrating transcript_state to add PRIMARY KEY\n";
            // Backup existing data
            write_execute("CREATE TABLE transcript_state_backup AS SELECT * FROM transcript_state");
            // Drop old table
            write_execute("DROP TABLE transcript_state");
            // Recreate with PRIMARY KEY
            write_execute(R"(
                CREATE TABLE transcript_state (
                    session_id VARCHAR PRIMARY KEY,
                    transcript_path VARCHAR NOT NULL,
                    realm VARCHAR DEFAULT 'default',
                    last_processed_line BIGINT DEFAULT 0,
                    last_distilled_at BIGINT DEFAULT 0,
                    created_at BIGINT NOT NULL
                )
            )");
            // Restore data
            write_execute("INSERT INTO transcript_state SELECT * FROM transcript_state_backup");
            write_execute("DROP TABLE transcript_state_backup");
            std::cerr << "[DuckDBStore] Migration complete\n";
        }
    }

    // Indexes for transcript queries
    write_execute("CREATE INDEX IF NOT EXISTS idx_transcript_realm ON transcript_state(realm)");

    // Long-running tasks (mind-powered Ralph Wiggum)
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS long_task (
            id BIGINT PRIMARY KEY,
            task_id VARCHAR UNIQUE NOT NULL,
            goal TEXT NOT NULL,
            realm VARCHAR DEFAULT 'brahman',
            status VARCHAR DEFAULT 'active',
            hard_checks TEXT,
            soft_checks TEXT,
            work_items TEXT,
            completed_summary TEXT,
            blockers TEXT,
            agent_id VARCHAR,
            lease_until BIGINT DEFAULT 0,
            iterations INTEGER DEFAULT 0,
            started_at BIGINT NOT NULL,
            updated_at BIGINT NOT NULL,
            completed_at BIGINT DEFAULT 0,
            outcome TEXT
        )
    )")) {
        return false;
    }

    // Indexes for task queries
    write_execute("CREATE INDEX IF NOT EXISTS idx_task_realm ON long_task(realm)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_task_status ON long_task(status)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_task_agent ON long_task(agent_id)");

    // Task events (append-only log)
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS task_event (
            id BIGINT PRIMARY KEY,
            task_id VARCHAR NOT NULL,
            kind VARCHAR NOT NULL,
            payload TEXT,
            tags TEXT,
            related_entities TEXT,
            created_at BIGINT NOT NULL
        )
    )")) {
        return false;
    }

    // Indexes for event queries
    write_execute("CREATE INDEX IF NOT EXISTS idx_event_task ON task_event(task_id)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_event_kind ON task_event(kind)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_event_created ON task_event(created_at DESC)");

    // Suggestion tracking for loop closure
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS suggestion (
            id BIGINT PRIMARY KEY,
            content TEXT NOT NULL,
            context TEXT,
            realm VARCHAR DEFAULT 'brahman',
            status VARCHAR DEFAULT 'pending',
            helped BOOLEAN DEFAULT FALSE,
            outcome_details TEXT,
            memory_id BIGINT DEFAULT 0,
            suggested_at BIGINT NOT NULL,
            resolved_at BIGINT DEFAULT 0
        )
    )")) {
        return false;
    }

    // Indexes for suggestion queries
    write_execute("CREATE INDEX IF NOT EXISTS idx_suggestion_status ON suggestion(status)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_suggestion_realm ON suggestion(realm)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_suggestion_suggested ON suggestion(suggested_at DESC)");

    // Anticipation table: context→action pattern learning
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS anticipation (
            id BIGINT PRIMARY KEY,
            context TEXT NOT NULL,
            action TEXT NOT NULL,
            frequency INTEGER DEFAULT 1,
            success_count INTEGER DEFAULT 0,
            last_triggered BIGINT NOT NULL,
            realm VARCHAR DEFAULT 'brahman',
            created_at BIGINT NOT NULL,
            UNIQUE(context, action, realm)
        )
    )")) {
        return false;
    }

    // Indexes for anticipation queries
    write_execute("CREATE INDEX IF NOT EXISTS idx_anticipation_context ON anticipation(context)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_anticipation_realm ON anticipation(realm)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_anticipation_freq ON anticipation(frequency DESC)");

    // Habit table: repeated patterns that strengthen with use
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS habit (
            id BIGINT PRIMARY KEY,
            trigger_pattern TEXT NOT NULL,
            response TEXT NOT NULL,
            strength FLOAT DEFAULT 0.1,
            frequency INTEGER DEFAULT 1,
            last_activated BIGINT NOT NULL,
            realm VARCHAR DEFAULT 'brahman',
            created_at BIGINT NOT NULL,
            UNIQUE(trigger_pattern, response, realm)
        )
    )")) {
        return false;
    }

    // Indexes for habit queries
    write_execute("CREATE INDEX IF NOT EXISTS idx_habit_trigger ON habit(trigger_pattern)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_habit_realm ON habit(realm)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_habit_strength ON habit(strength DESC)");

    // Background task table: daemon-level processing
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS background_task (
            id BIGINT PRIMARY KEY,
            task_type VARCHAR NOT NULL,
            status VARCHAR DEFAULT 'pending',
            scheduled_at BIGINT NOT NULL,
            started_at BIGINT DEFAULT 0,
            completed_at BIGINT DEFAULT 0,
            result TEXT,
            error TEXT,
            realm VARCHAR DEFAULT 'brahman'
        )
    )")) {
        return false;
    }

    // Indexes for background task queries
    write_execute("CREATE INDEX IF NOT EXISTS idx_bg_status ON background_task(status)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_bg_type ON background_task(task_type)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_bg_scheduled ON background_task(scheduled_at)");

    // User profile table: structured understanding of partner
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS user_profile (
            user_id VARCHAR PRIMARY KEY DEFAULT 'default',
            expertise_json TEXT DEFAULT '[]',
            style_json TEXT DEFAULT '{}',
            patterns_json TEXT DEFAULT '{}',
            preferences_json TEXT DEFAULT '{}',
            updated_at BIGINT NOT NULL
        )
    )")) {
        return false;
    }

    // Goal table: long-term objectives spanning weeks/months
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS goal (
            id BIGINT PRIMARY KEY,
            title TEXT NOT NULL,
            description TEXT,
            milestones_json TEXT DEFAULT '[]',
            status VARCHAR DEFAULT 'active',
            progress FLOAT DEFAULT 0.0,
            deadline BIGINT DEFAULT 0,
            outcome TEXT,
            realm VARCHAR DEFAULT 'brahman',
            created_at BIGINT NOT NULL,
            updated_at BIGINT NOT NULL
        )
    )")) {
        return false;
    }

    // Indexes for goal queries
    write_execute("CREATE INDEX IF NOT EXISTS idx_goal_status ON goal(status)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_goal_realm ON goal(realm)");
    write_execute("CREATE SEQUENCE IF NOT EXISTS goal_seq START 1");

    // Calibration table: tracking prediction accuracy by domain
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS calibration (
            domain VARCHAR PRIMARY KEY,
            predictions INTEGER DEFAULT 0,
            successes INTEGER DEFAULT 0,
            failures INTEGER DEFAULT 0,
            updated_at BIGINT NOT NULL
        )
    )")) {
        return false;
    }

    // Usage outcomes table: tracks whether surfaced memories actually helped
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS usage_outcomes (
            id BIGINT PRIMARY KEY,
            memory_id BIGINT NOT NULL,
            session_id VARCHAR,
            outcome VARCHAR NOT NULL,
            context VARCHAR,
            created_at BIGINT NOT NULL
        )
    )")) {
        return false;
    }
    write_execute("CREATE INDEX IF NOT EXISTS idx_usage_memory ON usage_outcomes(memory_id)");
    write_execute("CREATE INDEX IF NOT EXISTS idx_usage_outcome ON usage_outcomes(outcome)");
    write_execute("CREATE SEQUENCE IF NOT EXISTS usage_outcomes_seq START 1");

    // Provenance table: tracks where knowledge came from
    if (!write_execute(R"(
        CREATE TABLE IF NOT EXISTS provenance (
            node_id BIGINT PRIMARY KEY,
            session_id VARCHAR,
            tool_name VARCHAR,
            trust_score FLOAT DEFAULT 0.8,
            derived_from BIGINT DEFAULT 0
        )
    )")) {
        return false;
    }

    // Sequence for IDs
    write_execute("CREATE SEQUENCE IF NOT EXISTS memory_seq START 1");
    write_execute("CREATE SEQUENCE IF NOT EXISTS triplet_seq START 1");
    write_execute("CREATE SEQUENCE IF NOT EXISTS symbol_seq START 1");
    write_execute("CREATE SEQUENCE IF NOT EXISTS ledger_seq START 1");
    write_execute("CREATE SEQUENCE IF NOT EXISTS task_seq START 1");
    write_execute("CREATE SEQUENCE IF NOT EXISTS event_seq START 1");
    write_execute("CREATE SEQUENCE IF NOT EXISTS suggestion_seq START 1");
    write_execute("CREATE SEQUENCE IF NOT EXISTS anticipation_seq START 1");
    write_execute("CREATE SEQUENCE IF NOT EXISTS habit_seq START 1");
    write_execute("CREATE SEQUENCE IF NOT EXISTS background_seq START 1");

    return true;
}

bool DuckDBStore::create_vector_index() {
    // HNSW index now lives in separate embeddings DB (created in create_embeddings_schema)
    // This prevents corruption issues with main database
    if (emb_conn_) {
        index_exists_.store(true);  // Index is in embeddings DB
        needs_reindex_.store(false);
        std::cerr << "[DuckDBStore] HNSW index managed by embeddings DB\n";
        return true;
    }

    // Fallback: no index (brute-force will be used)
    index_exists_.store(false);
    needs_reindex_.store(false);
    std::cerr << "[DuckDBStore] No HNSW index (embeddings DB not available)\n";
    return false;
}

bool DuckDBStore::rebuild_vector_index() {
    // Rebuild HNSW index in embeddings DB (isolated from main DB)
    if (!emb_conn_) {
        std::cerr << "[DuckDBStore] Cannot rebuild index: embeddings DB not available\n";
        return false;
    }

    std::cerr << "[DuckDBStore] Rebuilding HNSW index in embeddings DB...\n";
    auto start = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(emb_mutex_);
    try {
        // Drop and recreate index in embeddings DB
        emb_conn_->Query("DROP INDEX IF EXISTS memory_emb_hnsw_idx");
        emb_conn_->Query("SET hnsw_enable_experimental_persistence = true");
        emb_conn_->Query(R"(
            CREATE INDEX memory_emb_hnsw_idx
            ON memory_embeddings USING HNSW (embedding)
            WITH (metric = 'cosine')
        )");

        // Compact to remove deleted entries
        emb_conn_->Query("PRAGMA hnsw_compact_index('memory_emb_hnsw_idx')");

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        index_exists_.store(true);
        needs_reindex_.store(false);
        std::cerr << "[DuckDBStore] HNSW index rebuilt in " << elapsed << "ms\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[DuckDBStore] HNSW rebuild failed: " << e.what() << "\n";
        index_exists_.store(false);
        return false;
    }
}

size_t DuckDBStore::migrate_embeddings_to_vss() {
    // Copy embeddings from main DB to VSS DB (for existing memories)
    if (!emb_conn_ || !db_) {
        std::cerr << "[DuckDBStore] Cannot migrate: embeddings DB not available\n";
        return 0;
    }

    std::cerr << "[DuckDBStore] Migrating embeddings to VSS DB...\n";
    auto start = std::chrono::steady_clock::now();
    size_t migrated = 0;

    try {
        // Get all memories with embeddings from main DB
        auto result = read_query(R"(
            SELECT id, embedding FROM memory
            WHERE embedding IS NOT NULL
            AND array_length(embedding) = 384
        )");

        if (!result || result->HasError()) {
            std::cerr << "[DuckDBStore] Migration query failed\n";
            return 0;
        }

        std::lock_guard<std::mutex> lock(emb_mutex_);

        while (true) {
            auto chunk = result->Fetch();
            if (!chunk || chunk->size() == 0) break;

            for (size_t i = 0; i < chunk->size(); ++i) {
                int64_t mem_id = chunk->GetValue(0, i).GetValue<int64_t>();
                auto emb_val = chunk->GetValue(1, i);

                // Extract embedding values
                std::vector<float> embedding;
                auto list = duckdb::ListValue::GetChildren(emb_val);
                for (const auto& v : list) {
                    embedding.push_back(v.GetValue<float>());
                }

                if (embedding.size() == 384) {
                    // Delete first to avoid HNSW duplicate key errors
                    std::ostringstream del_sql;
                    del_sql << "DELETE FROM memory_embeddings WHERE memory_id = " << mem_id;
                    emb_conn_->Query(del_sql.str());

                    std::ostringstream sql;
                    sql << "INSERT INTO memory_embeddings (memory_id, embedding, created_at) VALUES ("
                        << mem_id << ", " << embedding_to_sql(embedding) << ", current_timestamp)";
                    emb_conn_->Query(sql.str());
                    migrated++;
                }
            }
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        std::cerr << "[DuckDBStore] Migrated " << migrated << " embeddings in " << elapsed << "ms\n";

    } catch (const std::exception& e) {
        std::cerr << "[DuckDBStore] Migration error: " << e.what() << "\n";
    }

    return migrated;
}

// Write operations - use dedicated connection with mutex (serialized)
bool DuckDBStore::write_execute(const std::string& sql) {
    std::lock_guard lock(write_mutex_);
    try {
        if (!write_conn_) {
            std::cerr << "[DuckDBStore] write_conn_ is null!\n";
            return false;
        }
        auto result = write_conn_->Query(sql);
        if (result->HasError()) {
            std::cerr << "[DuckDBStore] Query error: " << result->GetError() << "\n";
            std::cerr << "[DuckDBStore] SQL: " << sql.substr(0, 200) << "\n";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[DuckDBStore] Exception: " << e.what() << "\n";
        std::cerr << "[DuckDBStore] SQL: " << sql.substr(0, 200) << "\n";
        return false;
    }
}

std::unique_ptr<duckdb::QueryResult> DuckDBStore::write_query(const std::string& sql) {
    std::lock_guard lock(write_mutex_);
    try {
        return write_conn_->Query(sql);
    } catch (const std::exception& e) {
        std::cerr << "[DuckDBStore] Query exception: " << e.what() << "\n";
        return nullptr;
    }
}

// Read operations - use connection pool for concurrency (no mutex needed)
std::unique_ptr<duckdb::QueryResult> DuckDBStore::read_query(const std::string& sql) const {
    try {
        auto conn = read_pool_->acquire();
        return conn->Query(sql);
    } catch (const std::exception& e) {
        std::cerr << "[DuckDBStore] Read query exception: " << e.what() << "\n";
        return nullptr;
    }
}

std::string DuckDBStore::embedding_to_sql(const std::vector<float>& embedding) {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < embedding.size(); ++i) {
        if (i > 0) ss << ",";
        ss << embedding[i];
    }
    ss << "]::FLOAT[384]";
    return ss.str();
}

int64_t DuckDBStore::remember(
    const std::string& content,
    const std::string& kind,
    const std::vector<float>& embedding,
    float confidence,
    float decay_rate,
    const std::string& realm,
    RealmVisibility visibility,
    const std::vector<std::string>& shared_realms
) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return -1;

    Timestamp now_ts = now();

    // Escape content for SQL
    std::string escaped_content;
    for (char c : content) {
        if (c == '\'') escaped_content += "''";
        else escaped_content += c;
    }

    std::string escaped_realm;
    for (char c : realm) {
        if (c == '\'') escaped_realm += "''";
        else escaped_realm += c;
    }

    std::ostringstream sql;
    sql << "INSERT INTO memory (id, kind, content, confidence, decay_rate, created_at, accessed_at, embedding, realm, visibility) "
        << "VALUES (nextval('memory_seq'), '" << kind << "', '" << escaped_content << "', "
        << confidence << ", " << decay_rate << ", " << now_ts << ", " << now_ts << ", "
        << embedding_to_sql(embedding) << ", '" << escaped_realm << "', "
        << static_cast<int>(visibility) << ") RETURNING id";

    auto result = read_query(sql.str());
    if (!result) {
        last_error_ = "No result from INSERT query";
        return -1;
    }
    if (result->HasError()) {
        last_error_ = "INSERT error: " + result->GetError();
        return -1;
    }

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) {
        return -1;
    }

    int64_t id = chunk->GetValue(0, 0).GetValue<int64_t>();

    // Add shared realm memberships
    for (const auto& shared : shared_realms) {
        std::string escaped_shared;
        for (char c : shared) {
            if (c == '\'') escaped_shared += "''";
            else escaped_shared += c;
        }
        std::ostringstream membership_sql;
        membership_sql << "INSERT INTO realm_membership (memory_id, realm) VALUES ("
                       << id << ", '" << escaped_shared << "')";
        write_execute(membership_sql.str());
    }

    // Store embedding in separate VSS database (for HNSW index isolation)
    if (emb_conn_ && !embedding.empty()) {
        std::lock_guard<std::mutex> lock(emb_mutex_);
        try {
            // Delete first to avoid HNSW duplicate key errors
            std::ostringstream del_sql;
            del_sql << "DELETE FROM memory_embeddings WHERE memory_id = " << id;
            emb_conn_->Query(del_sql.str());

            std::ostringstream emb_sql;
            emb_sql << "INSERT INTO memory_embeddings (memory_id, embedding, created_at) VALUES ("
                    << id << ", " << embedding_to_sql(embedding) << ", current_timestamp)";
            emb_conn_->Query(emb_sql.str());
        } catch (const std::exception& e) {
            // Non-fatal - main DB still has embedding as backup
            std::cerr << "[DuckDBStore] VSS DB insert warning: " << e.what() << "\n";
        }
    }

    return id;
}

std::vector<MemoryResult> DuckDBStore::recall(
    const std::vector<float>& query_embedding,
    size_t k,
    const std::string& realm,
    bool include_global,
    const std::vector<std::string>& exclude_kinds
) {
    std::vector<MemoryResult> results;
    if (!db_) return results;

    // Escape realm for SQL
    std::string escaped_realm;
    for (char c : realm) {
        if (c == '\'') escaped_realm += "''";
        else escaped_realm += c;
    }

    // Strategy: Use embeddings DB (with HNSW) if available, else fall back to main DB brute-force
    std::vector<std::pair<int64_t, float>> candidate_ids;  // (memory_id, similarity)

    // Phase 1: Get candidate IDs from embeddings DB (fast HNSW lookup)
    if (emb_conn_) {
        std::lock_guard<std::mutex> lock(emb_mutex_);
        try {
            // Query embeddings DB for top candidates (get more than k to allow filtering)
            std::ostringstream emb_sql;
            emb_sql << "SELECT memory_id, "
                    << "array_cosine_similarity(embedding, " << embedding_to_sql(query_embedding) << ") AS similarity "
                    << "FROM memory_embeddings "
                    << "ORDER BY array_distance(embedding, " << embedding_to_sql(query_embedding) << ") "
                    << "LIMIT " << (k * 3);  // Get extra candidates for realm filtering

            auto emb_result = emb_conn_->Query(emb_sql.str());
            if (emb_result && !emb_result->HasError()) {
                while (true) {
                    auto chunk = emb_result->Fetch();
                    if (!chunk || chunk->size() == 0) break;
                    for (size_t i = 0; i < chunk->size(); ++i) {
                        int64_t mem_id = chunk->GetValue(0, i).GetValue<int64_t>();
                        float sim = chunk->GetValue(1, i).GetValue<float>();
                        candidate_ids.push_back({mem_id, sim});
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[DuckDBStore] Embeddings DB query failed: " << e.what() << "\n";
            candidate_ids.clear();  // Fall back to main DB
        }
    }

    // Phase 2: Fetch full memory data from main DB
    if (!candidate_ids.empty()) {
        // Build IN clause with candidate IDs
        std::ostringstream id_list;
        for (size_t i = 0; i < candidate_ids.size(); ++i) {
            if (i > 0) id_list << ",";
            id_list << candidate_ids[i].first;
        }

        // Build realm filter
        std::ostringstream where_clause;
        where_clause << "WHERE m.id IN (" << id_list.str() << ") ";
        if (!realm.empty()) {
            where_clause << "AND (m.realm = '" << escaped_realm << "' ";
            where_clause << "OR m.id IN (SELECT memory_id FROM realm_membership WHERE realm = '" << escaped_realm << "') ";
            if (include_global) {
                where_clause << "OR m.visibility = 2 ";
            }
            where_clause << ") ";
        }

        // Add kind exclusion filter (for partnership-only queries)
        if (!exclude_kinds.empty()) {
            where_clause << "AND m.kind NOT IN (";
            for (size_t i = 0; i < exclude_kinds.size(); ++i) {
                if (i > 0) where_clause << ",";
                // Escape single quotes in kind names
                std::string escaped_kind;
                for (char c : exclude_kinds[i]) {
                    if (c == '\'') escaped_kind += "''";
                    else escaped_kind += c;
                }
                where_clause << "'" << escaped_kind << "'";
            }
            where_clause << ") ";
        }

        std::ostringstream sql;
        sql << "SELECT m.id, m.kind, m.content, m.confidence, m.created_at, m.accessed_at, "
            << "m.realm, m.visibility "
            << "FROM memory m "
            << where_clause.str();

        auto result = read_query(sql.str());
        if (result && !result->HasError()) {
            // Build id->similarity map
            std::unordered_map<int64_t, float> sim_map;
            for (const auto& [id, sim] : candidate_ids) {
                sim_map[id] = sim;
            }

            while (true) {
                auto chunk = result->Fetch();
                if (!chunk || chunk->size() == 0) break;

                for (size_t i = 0; i < chunk->size(); ++i) {
                    MemoryResult r;
                    r.id = chunk->GetValue(0, i).GetValue<int64_t>();
                    r.kind = chunk->GetValue(1, i).ToString();
                    r.content = chunk->GetValue(2, i).ToString();
                    r.confidence = chunk->GetValue(3, i).GetValue<float>();
                    r.created_at = chunk->GetValue(4, i).GetValue<int64_t>();
                    r.accessed_at = chunk->GetValue(5, i).GetValue<int64_t>();
                    r.realm = chunk->GetValue(6, i).ToString();
                    r.visibility = static_cast<RealmVisibility>(chunk->GetValue(7, i).GetValue<int32_t>());
                    r.similarity = sim_map[r.id];
                    results.push_back(r);
                }
            }

            // Sort by similarity and limit to k
            std::sort(results.begin(), results.end(),
                [](const MemoryResult& a, const MemoryResult& b) {
                    return a.similarity > b.similarity;
                });
            if (results.size() > k) {
                results.resize(k);
            }
        }
    } else {
        // Fallback: brute-force on main DB (when embeddings DB not available)
        std::ostringstream where_clause;
        bool has_where = false;
        if (!realm.empty()) {
            where_clause << "WHERE (m.realm = '" << escaped_realm << "' ";
            where_clause << "OR m.id IN (SELECT memory_id FROM realm_membership WHERE realm = '" << escaped_realm << "') ";
            if (include_global) {
                where_clause << "OR m.visibility = 2 ";
            }
            where_clause << ") ";
            has_where = true;
        }

        // Add kind exclusion filter (for partnership-only queries)
        if (!exclude_kinds.empty()) {
            where_clause << (has_where ? "AND " : "WHERE ");
            where_clause << "m.kind NOT IN (";
            for (size_t i = 0; i < exclude_kinds.size(); ++i) {
                if (i > 0) where_clause << ",";
                // Escape single quotes in kind names
                std::string escaped_kind;
                for (char c : exclude_kinds[i]) {
                    if (c == '\'') escaped_kind += "''";
                    else escaped_kind += c;
                }
                where_clause << "'" << escaped_kind << "'";
            }
            where_clause << ") ";
        }

        std::ostringstream sql;
        sql << "SELECT m.id, m.kind, m.content, m.confidence, m.created_at, m.accessed_at, "
            << "m.realm, m.visibility, "
            << "array_cosine_similarity(m.embedding, " << embedding_to_sql(query_embedding) << ") AS similarity "
            << "FROM memory m "
            << where_clause.str()
            << "ORDER BY similarity DESC "
            << "LIMIT " << k;

        auto result = read_query(sql.str());
        if (result && !result->HasError()) {
            while (true) {
                auto chunk = result->Fetch();
                if (!chunk || chunk->size() == 0) break;

                for (size_t i = 0; i < chunk->size(); ++i) {
                    MemoryResult r;
                    r.id = chunk->GetValue(0, i).GetValue<int64_t>();
                    r.kind = chunk->GetValue(1, i).ToString();
                    r.content = chunk->GetValue(2, i).ToString();
                    r.confidence = chunk->GetValue(3, i).GetValue<float>();
                    r.created_at = chunk->GetValue(4, i).GetValue<int64_t>();
                    r.accessed_at = chunk->GetValue(5, i).GetValue<int64_t>();
                    r.realm = chunk->GetValue(6, i).ToString();
                    r.visibility = static_cast<RealmVisibility>(chunk->GetValue(7, i).GetValue<int32_t>());
                    r.similarity = chunk->GetValue(8, i).GetValue<float>();
                    results.push_back(r);
                }
            }
        }
    }

    // Load shared realms for each result
    for (auto& r : results) {
        std::ostringstream membership_sql;
        membership_sql << "SELECT realm FROM realm_membership WHERE memory_id = " << r.id;
        auto membership_result = read_query(membership_sql.str());
        if (membership_result && !membership_result->HasError()) {
            while (true) {
                auto chunk = membership_result->Fetch();
                if (!chunk || chunk->size() == 0) break;
                for (size_t i = 0; i < chunk->size(); ++i) {
                    r.shared_realms.push_back(chunk->GetValue(0, i).ToString());
                }
            }
        }
    }

    return results;
}

bool DuckDBStore::strengthen(int64_t id, float amount) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    std::ostringstream sql;
    sql << "UPDATE memory SET confidence = LEAST(confidence + " << amount << ", 1.0), "
        << "accessed_at = " << now() << " WHERE id = " << id;

    return write_execute(sql.str());
}

bool DuckDBStore::weaken(int64_t id, float amount) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    std::ostringstream sql;
    sql << "UPDATE memory SET confidence = GREATEST(confidence - " << amount << ", 0.0) "
        << "WHERE id = " << id;

    return write_execute(sql.str());
}

bool DuckDBStore::forget(int64_t id) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    // First verify the row exists
    std::ostringstream check_sql;
    check_sql << "SELECT COUNT(*) FROM memory WHERE id = " << id;
    auto check_result = read_query(check_sql.str());
    int64_t count_before = 0;
    if (check_result && !check_result->HasError()) {
        auto chunk = check_result->Fetch();
        if (chunk && chunk->size() > 0) {
            count_before = chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }
    std::cerr << "[DuckDBStore::forget] id=" << id << " count_before=" << count_before << "\n";

    // Delete from memory table
    std::ostringstream sql;
    sql << "DELETE FROM memory WHERE id = " << id;
    bool ok = write_execute(sql.str());
    std::cerr << "[DuckDBStore::forget] DELETE result=" << (ok ? "success" : "failed") << "\n";

    // Also delete from realm_membership
    std::ostringstream membership_sql;
    membership_sql << "DELETE FROM realm_membership WHERE memory_id = " << id;
    write_execute(membership_sql.str());

    // Verify deletion
    auto verify_result = read_query(check_sql.str());
    int64_t count_after = 0;
    if (verify_result && !verify_result->HasError()) {
        auto chunk = verify_result->Fetch();
        if (chunk && chunk->size() > 0) {
            count_after = chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }
    std::cerr << "[DuckDBStore::forget] count_after=" << count_after << "\n";

    return ok && (count_before > 0) && (count_after == 0);
}

bool DuckDBStore::touch(int64_t id) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    std::ostringstream sql;
    sql << "UPDATE memory SET accessed_at = " << now() << " WHERE id = " << id;

    return write_execute(sql.str());
}

std::optional<MemoryResult> DuckDBStore::get_memory(int64_t id) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return std::nullopt;

    std::ostringstream sql;
    sql << "SELECT id, kind, content, confidence, created_at, accessed_at, realm, visibility "
        << "FROM memory WHERE id = " << id;

    auto result = read_query(sql.str());
    if (!result || result->HasError()) {
        return std::nullopt;
    }

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) {
        return std::nullopt;
    }

    MemoryResult r;
    r.id = chunk->GetValue(0, 0).GetValue<int64_t>();
    r.kind = chunk->GetValue(1, 0).ToString();
    r.content = chunk->GetValue(2, 0).ToString();
    r.confidence = chunk->GetValue(3, 0).GetValue<float>();
    r.created_at = chunk->GetValue(4, 0).GetValue<int64_t>();
    r.accessed_at = chunk->GetValue(5, 0).GetValue<int64_t>();
    r.realm = chunk->GetValue(6, 0).ToString();
    r.visibility = static_cast<RealmVisibility>(chunk->GetValue(7, 0).GetValue<int32_t>());
    r.similarity = 1.0f;

    // Load shared realms
    std::ostringstream membership_sql;
    membership_sql << "SELECT realm FROM realm_membership WHERE memory_id = " << id;
    auto membership_result = read_query(membership_sql.str());
    if (membership_result && !membership_result->HasError()) {
        while (true) {
            auto mchunk = membership_result->Fetch();
            if (!mchunk || mchunk->size() == 0) break;
            for (size_t i = 0; i < mchunk->size(); ++i) {
                r.shared_realms.push_back(mchunk->GetValue(0, i).ToString());
            }
        }
    }

    return r;
}

bool DuckDBStore::update_content(int64_t id, const std::string& new_content) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    // Escape content for SQL
    std::string escaped = new_content;
    size_t pos = 0;
    while ((pos = escaped.find('\'', pos)) != std::string::npos) {
        escaped.replace(pos, 1, "''");
        pos += 2;
    }

    std::ostringstream sql;
    sql << "UPDATE memory SET content = '" << escaped << "', "
        << "accessed_at = " << now() << " WHERE id = " << id;

    return write_execute(sql.str());
}

bool DuckDBStore::update_visibility(int64_t id, RealmVisibility visibility) {
    if (!db_) return false;

    std::ostringstream sql;
    sql << "UPDATE memory SET visibility = " << static_cast<int>(visibility)
        << ", accessed_at = " << now() << " WHERE id = " << id;

    return write_execute(sql.str());
}

bool DuckDBStore::set_memory_embedding(int64_t id, const std::vector<float>& embedding) {
    if (!db_) return false;
    if (embedding.size() != 384) return false;

    // Store in embeddings DB (for HNSW index) if available
    if (emb_conn_) {
        std::lock_guard<std::mutex> lock(emb_mutex_);
        try {
            // Delete first to avoid HNSW duplicate key errors
            std::ostringstream del_sql;
            del_sql << "DELETE FROM memory_embeddings WHERE memory_id = " << id;
            emb_conn_->Query(del_sql.str());

            std::ostringstream emb_sql;
            emb_sql << "INSERT INTO memory_embeddings (memory_id, embedding, created_at) VALUES ("
                    << id << ", " << embedding_to_sql(embedding) << ", current_timestamp)";
            emb_conn_->Query(emb_sql.str());
        } catch (const std::exception& e) {
            std::cerr << "[DuckDBStore] Embeddings DB insert failed: " << e.what() << "\n";
        }
    }

    // Also update main DB (for backwards compatibility during migration)
    std::ostringstream sql;
    sql << "UPDATE memory SET embedding = " << embedding_to_sql(embedding)
        << ", accessed_at = " << now() << " WHERE id = " << id;

    return write_execute(sql.str());
}

std::vector<MemoryResult> DuckDBStore::list_global_memories(size_t limit, const std::string& kind) {
    std::vector<MemoryResult> results;
    if (!db_) return results;

    std::ostringstream sql;
    sql << "SELECT id, content, kind, confidence, realm, visibility, created_at, accessed_at "
        << "FROM memory WHERE visibility = " << static_cast<int>(RealmVisibility::Global);

    if (!kind.empty()) {
        // Escape kind
        std::string escaped = kind;
        size_t pos = 0;
        while ((pos = escaped.find('\'', pos)) != std::string::npos) {
            escaped.replace(pos, 1, "''");
            pos += 2;
        }
        sql << " AND kind = '" << escaped << "'";
    }

    sql << " ORDER BY confidence DESC, accessed_at DESC LIMIT " << limit;

    auto result = read_query(sql.str());
    if (!result) return results;

    while (auto chunk = result->Fetch()) {
        for (size_t i = 0; i < chunk->size(); i++) {
            MemoryResult mem;
            mem.id = chunk->GetValue(0, i).GetValue<int64_t>();
            mem.content = chunk->GetValue(1, i).GetValue<std::string>();
            mem.kind = chunk->GetValue(2, i).GetValue<std::string>();
            mem.confidence = chunk->GetValue(3, i).GetValue<float>();
            mem.realm = chunk->GetValue(4, i).GetValue<std::string>();
            results.push_back(mem);
        }
    }

    return results;
}

bool DuckDBStore::add_tag(int64_t id, const std::string& tag) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    // Check if tags table exists, create if not
    write_execute("CREATE TABLE IF NOT EXISTS memory_tags ("
            "memory_id BIGINT, tag VARCHAR, "
            "PRIMARY KEY (memory_id, tag))");

    // Escape tag
    std::string escaped = tag;
    size_t pos = 0;
    while ((pos = escaped.find('\'', pos)) != std::string::npos) {
        escaped.replace(pos, 1, "''");
        pos += 2;
    }

    std::ostringstream sql;
    sql << "INSERT INTO memory_tags (memory_id, tag) VALUES ("
        << id << ", '" << escaped << "') ON CONFLICT DO NOTHING";

    return write_execute(sql.str());
}

bool DuckDBStore::remove_tag(int64_t id, const std::string& tag) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    std::string escaped = tag;
    size_t pos = 0;
    while ((pos = escaped.find('\'', pos)) != std::string::npos) {
        escaped.replace(pos, 1, "''");
        pos += 2;
    }

    std::ostringstream sql;
    sql << "DELETE FROM memory_tags WHERE memory_id = " << id
        << " AND tag = '" << escaped << "'";

    return write_execute(sql.str());
}

std::vector<std::string> DuckDBStore::get_tags(int64_t id) {
    // Lock handled in write_execute/write_query/read_query
    std::vector<std::string> tags;
    if (!db_) return tags;

    std::ostringstream sql;
    sql << "SELECT tag FROM memory_tags WHERE memory_id = " << id;

    auto result = read_query(sql.str());
    if (!result || result->HasError()) return tags;

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); i++) {
            tags.push_back(chunk->GetValue(0, i).ToString());
        }
    }

    return tags;
}

// Realm management methods

bool DuckDBStore::set_realm(int64_t id, const std::string& realm) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    std::string escaped;
    for (char c : realm) {
        if (c == '\'') escaped += "''";
        else escaped += c;
    }

    std::ostringstream sql;
    sql << "UPDATE memory SET realm = '" << escaped << "' WHERE id = " << id;
    return write_execute(sql.str());
}

bool DuckDBStore::set_visibility(int64_t id, RealmVisibility visibility) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    std::ostringstream sql;
    sql << "UPDATE memory SET visibility = " << static_cast<int>(visibility)
        << " WHERE id = " << id;
    return write_execute(sql.str());
}

bool DuckDBStore::add_to_realm(int64_t id, const std::string& realm) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    std::string escaped;
    for (char c : realm) {
        if (c == '\'') escaped += "''";
        else escaped += c;
    }

    std::ostringstream sql;
    sql << "INSERT OR IGNORE INTO realm_membership (memory_id, realm) VALUES ("
        << id << ", '" << escaped << "')";
    return write_execute(sql.str());
}

bool DuckDBStore::remove_from_realm(int64_t id, const std::string& realm) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    std::string escaped;
    for (char c : realm) {
        if (c == '\'') escaped += "''";
        else escaped += c;
    }

    std::ostringstream sql;
    sql << "DELETE FROM realm_membership WHERE memory_id = " << id
        << " AND realm = '" << escaped << "'";
    return write_execute(sql.str());
}

std::vector<std::string> DuckDBStore::get_realms(int64_t id) {
    // Lock handled in write_execute/write_query/read_query
    std::vector<std::string> realms;
    if (!db_) return realms;

    // First get primary realm
    std::ostringstream primary_sql;
    primary_sql << "SELECT realm FROM memory WHERE id = " << id;
    auto result = read_query(primary_sql.str());
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            realms.push_back(chunk->GetValue(0, 0).ToString());
        }
    }

    // Then get shared realms
    std::ostringstream shared_sql;
    shared_sql << "SELECT realm FROM realm_membership WHERE memory_id = " << id;
    result = read_query(shared_sql.str());
    if (result && !result->HasError()) {
        while (true) {
            auto chunk = result->Fetch();
            if (!chunk || chunk->size() == 0) break;
            for (size_t i = 0; i < chunk->size(); ++i) {
                realms.push_back(chunk->GetValue(0, i).ToString());
            }
        }
    }

    return realms;
}

std::vector<std::string> DuckDBStore::list_realms() {
    // Lock handled in write_execute/write_query/read_query
    std::vector<std::string> realms;
    if (!db_) return realms;

    // Get all unique realms from both memory and realm_membership tables
    auto result = read_query(
        "SELECT DISTINCT realm FROM ("
        "  SELECT realm FROM memory "
        "  UNION "
        "  SELECT realm FROM realm_membership"
        ") ORDER BY realm"
    );

    if (result && !result->HasError()) {
        while (true) {
            auto chunk = result->Fetch();
            if (!chunk || chunk->size() == 0) break;
            for (size_t i = 0; i < chunk->size(); ++i) {
                realms.push_back(chunk->GetValue(0, i).ToString());
            }
        }
    }

    return realms;
}

size_t DuckDBStore::apply_decay() {
    // No application lock needed - DuckDB handles concurrency via MVCC
    if (!db_) return 0;

    Timestamp current = now();

    // Apply exponential decay based on time since last access
    // Single atomic UPDATE - DuckDB handles locking internally
    std::ostringstream sql;
    sql << "UPDATE memory SET confidence = confidence * exp(-decay_rate * "
        << "(" << current << " - accessed_at) / 86400000.0) "
        << "WHERE decay_rate > 0 AND accessed_at < " << (current - 60000);  // Only decay if >1min since access

    write_execute(sql.str());

    // Return count of memories with decay
    auto result = read_query("SELECT COUNT(*) FROM memory WHERE decay_rate > 0");
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            return chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }
    return 0;
}

size_t DuckDBStore::prune(float threshold, float min_age_days) {
    // No application lock needed - DuckDB handles concurrency via MVCC
    if (!db_) return 0;

    Timestamp current = now();
    Timestamp min_created = current - static_cast<int64_t>(min_age_days * 86400000.0);

    // Protected types: code intelligence memories should never be pruned
    // These have structural value and don't lose relevance over time
    std::string protected_kinds =
        "'symbol', 'projectessence', 'modulestate', 'patternstate'";

    // Get count before delete (approximate is fine for maintenance)
    std::ostringstream count_sql;
    count_sql << "SELECT COUNT(*) FROM memory WHERE confidence < " << threshold
              << " AND created_at < " << min_created
              << " AND kind NOT IN (" << protected_kinds << ")";

    size_t count = 0;
    auto result = read_query(count_sql.str());
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            count = chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }

    if (count > 0) {
        // Only execute delete if there's something to prune
        std::ostringstream del_sql;
        del_sql << "DELETE FROM memory WHERE confidence < " << threshold
                << " AND created_at < " << min_created
                << " AND kind NOT IN (" << protected_kinds << ")";
        write_execute(del_sql.str());
    }

    return count;
}

bool DuckDBStore::connect(
    const std::string& subject,
    const std::string& predicate,
    const std::string& object,
    float weight
) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    // Normalize to lowercase for consistent querying
    auto to_lower = [](const std::string& s) {
        std::string result;
        for (char c : s) result += std::tolower(c);
        return result;
    };

    // Escape strings
    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    std::string norm_subject = to_lower(subject);
    std::string norm_object = to_lower(object);

    std::ostringstream sql;
    sql << "INSERT INTO triplet (id, subject, predicate, object, weight, created_at, source_file) VALUES ("
        << "nextval('triplet_seq'), "
        << "'" << escape(norm_subject) << "', "
        << "'" << escape(predicate) << "', "
        << "'" << escape(norm_object) << "', "
        << weight << ", " << now() << ", '')";

    return write_execute(sql.str());
}

bool DuckDBStore::connect_with_source(
    const std::string& subject,
    const std::string& predicate,
    const std::string& object,
    const std::string& source_file,
    float weight
) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    auto to_lower = [](const std::string& s) {
        std::string result;
        for (char c : s) result += std::tolower(c);
        return result;
    };

    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    std::string norm_subject = to_lower(subject);
    std::string norm_object = to_lower(object);

    std::ostringstream sql;
    sql << "INSERT INTO triplet (id, subject, predicate, object, weight, created_at, source_file) VALUES ("
        << "nextval('triplet_seq'), "
        << "'" << escape(norm_subject) << "', "
        << "'" << escape(predicate) << "', "
        << "'" << escape(norm_object) << "', "
        << weight << ", " << now() << ", "
        << "'" << escape(source_file) << "')";

    return write_execute(sql.str());
}

size_t DuckDBStore::connect_batch(
    const std::vector<std::tuple<std::string, std::string, std::string, std::string>>& triplets,
    float weight
) {
    // Use SQL batch insert (Appender has issues with locks)
    // Lock handled in write_execute/write_query/read_query
    if (!db_ || triplets.empty()) return 0;
    return connect_batch_sql(triplets, weight);
}

// Fallback SQL-based batch insert
size_t DuckDBStore::connect_batch_sql(
    const std::vector<std::tuple<std::string, std::string, std::string, std::string>>& triplets,
    float weight
) {
    std::string sql;
    sql.reserve(triplets.size() * 200);
    std::string temp;
    temp.reserve(256);

    auto append_escaped_lower = [&sql, &temp](const std::string& s) {
        temp.clear();
        for (char c : s) {
            char lc = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
            if (lc == '\'') { temp += '\''; temp += '\''; }
            else temp += lc;
        }
        sql += temp;
    };

    auto append_escaped = [&sql, &temp](const std::string& s) {
        temp.clear();
        for (char c : s) {
            if (c == '\'') { temp += '\''; temp += '\''; }
            else temp += c;
        }
        sql += temp;
    };

    write_execute("BEGIN TRANSACTION");

    int64_t ts = now();
    char ts_buf[32];
    int ts_len = snprintf(ts_buf, sizeof(ts_buf), "%ld", ts);
    char weight_buf[32];
    int weight_len = snprintf(weight_buf, sizeof(weight_buf), "%.2f", weight);

    static constexpr size_t BATCH_SIZE = 500;
    size_t batch_count = 0;
    size_t inserted = 0;

    for (const auto& [subject, predicate, object, source_file] : triplets) {
        if (batch_count == 0) {
            sql.clear();
            sql += "INSERT INTO triplet (id, subject, predicate, object, weight, created_at, source_file) VALUES ";
        } else {
            sql += ", ";
        }

        sql += "(nextval('triplet_seq'), '";
        append_escaped_lower(subject);
        sql += "', '";
        append_escaped(predicate);
        sql += "', '";
        append_escaped_lower(object);
        sql += "', ";
        sql.append(weight_buf, weight_len);
        sql += ", ";
        sql.append(ts_buf, ts_len);
        sql += ", '";
        append_escaped(source_file);
        sql += "')";

        batch_count++;
        inserted++;

        if (batch_count >= BATCH_SIZE) {
            write_execute(sql);
            batch_count = 0;
        }
    }

    if (batch_count > 0) {
        write_execute(sql);
    }

    write_execute("COMMIT");
    return inserted;
}

std::vector<StringTriplet> DuckDBStore::query_subject(const std::string& subject) {
    // Lock handled in write_execute/write_query/read_query
    std::vector<StringTriplet> results;
    if (!db_) return results;

    // Normalize to lowercase
    std::string escaped;
    for (char c : subject) {
        char lc = std::tolower(c);
        if (lc == '\'') escaped += "''";
        else escaped += lc;
    }

    std::ostringstream sql;
    sql << "SELECT subject, predicate, object, weight FROM triplet "
        << "WHERE subject = '" << escaped << "'";

    auto result = read_query(sql.str());
    if (!result || result->HasError()) return results;

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); ++i) {
            StringTriplet t;
            t.subject = chunk->GetValue(0, i).ToString();
            t.predicate = chunk->GetValue(1, i).ToString();
            t.object = chunk->GetValue(2, i).ToString();
            t.weight = chunk->GetValue(3, i).GetValue<float>();
            results.push_back(t);
        }
    }

    return results;
}

std::vector<StringTriplet> DuckDBStore::query_object(const std::string& object) {
    // Lock handled in write_execute/write_query/read_query
    std::vector<StringTriplet> results;
    if (!db_) return results;

    // Normalize to lowercase
    std::string escaped;
    for (char c : object) {
        char lc = std::tolower(c);
        if (lc == '\'') escaped += "''";
        else escaped += lc;
    }

    std::ostringstream sql;
    sql << "SELECT subject, predicate, object, weight FROM triplet "
        << "WHERE object = '" << escaped << "'";

    auto result = read_query(sql.str());
    if (!result || result->HasError()) return results;

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); ++i) {
            StringTriplet t;
            t.subject = chunk->GetValue(0, i).ToString();
            t.predicate = chunk->GetValue(1, i).ToString();
            t.object = chunk->GetValue(2, i).ToString();
            t.weight = chunk->GetValue(3, i).GetValue<float>();
            results.push_back(t);
        }
    }

    return results;
}

std::vector<StringTriplet> DuckDBStore::query_predicate(const std::string& predicate) {
    // Lock handled in write_execute/write_query/read_query
    std::vector<StringTriplet> results;
    if (!db_) return results;

    std::string escaped;
    for (char c : predicate) {
        if (c == '\'') escaped += "''";
        else escaped += c;
    }

    std::ostringstream sql;
    sql << "SELECT subject, predicate, object, weight FROM triplet "
        << "WHERE predicate = '" << escaped << "'";

    auto result = read_query(sql.str());
    if (!result || result->HasError()) return results;

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); ++i) {
            StringTriplet t;
            t.subject = chunk->GetValue(0, i).ToString();
            t.predicate = chunk->GetValue(1, i).ToString();
            t.object = chunk->GetValue(2, i).ToString();
            t.weight = chunk->GetValue(3, i).GetValue<float>();
            results.push_back(t);
        }
    }

    return results;
}

int64_t DuckDBStore::add_symbol(const Symbol& sym, const std::vector<float>& embedding) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return -1;

    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    // Use zero vector if no embedding provided
    std::vector<float> embed = embedding;
    if (embed.empty()) {
        embed.resize(EMBED_DIM, 0.0f);
    }

    std::ostringstream sql;
    sql << "INSERT INTO symbol (id, kind, name, signature, file_path, line_start, line_end, repo_id, embedding) "
        << "VALUES (nextval('symbol_seq'), "
        << "'" << escape(sym.kind) << "', "
        << "'" << escape(sym.name) << "', "
        << "'" << escape(sym.signature) << "', "
        << "'" << escape(sym.file_path) << "', "
        << sym.line_start << ", "
        << sym.line_end << ", "
        << sym.repo_id << ", "
        << embedding_to_sql(embed) << ") RETURNING id";

    auto result = read_query(sql.str());
    if (!result || result->HasError()) {
        return -1;
    }

    auto chunk = result->Fetch();
    if (chunk && chunk->size() > 0) {
        return chunk->GetValue(0, 0).GetValue<int64_t>();
    }

    return -1;
}

std::vector<Symbol> DuckDBStore::find_symbol(const std::string& name, const std::string& kind) {
    // Lock handled in write_execute/write_query/read_query
    std::vector<Symbol> results;
    if (!db_) return results;

    std::string escaped_name;
    for (char c : name) {
        if (c == '\'') escaped_name += "''";
        else escaped_name += c;
    }

    std::ostringstream sql;
    sql << "SELECT id, kind, name, signature, file_path, line_start, line_end, repo_id "
        << "FROM symbol WHERE name LIKE '%" << escaped_name << "%'";

    if (!kind.empty()) {
        std::string escaped_kind;
        for (char c : kind) {
            if (c == '\'') escaped_kind += "''";
            else escaped_kind += c;
        }
        sql << " AND kind = '" << escaped_kind << "'";
    }

    auto result = read_query(sql.str());
    if (!result || result->HasError()) return results;

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); ++i) {
            Symbol s;
            s.id = chunk->GetValue(0, i).GetValue<int64_t>();
            s.kind = chunk->GetValue(1, i).ToString();
            s.name = chunk->GetValue(2, i).ToString();
            s.signature = chunk->GetValue(3, i).ToString();
            s.file_path = chunk->GetValue(4, i).ToString();
            s.line_start = chunk->GetValue(5, i).GetValue<int32_t>();
            s.line_end = chunk->GetValue(6, i).GetValue<int32_t>();
            s.repo_id = chunk->GetValue(7, i).GetValue<int64_t>();
            results.push_back(s);
        }
    }

    return results;
}

std::vector<int64_t> DuckDBStore::callers(int64_t symbol_id) {
    // Lock handled in write_execute/write_query/read_query
    std::vector<int64_t> results;
    if (!db_) return results;

    std::ostringstream sql;
    sql << "SELECT caller_id FROM call_edge WHERE callee_id = " << symbol_id;

    auto result = read_query(sql.str());
    if (!result || result->HasError()) return results;

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); ++i) {
            results.push_back(chunk->GetValue(0, i).GetValue<int64_t>());
        }
    }

    return results;
}

std::vector<int64_t> DuckDBStore::callees(int64_t symbol_id) {
    // Lock handled in write_execute/write_query/read_query
    std::vector<int64_t> results;
    if (!db_) return results;

    std::ostringstream sql;
    sql << "SELECT callee_id FROM call_edge WHERE caller_id = " << symbol_id;

    auto result = read_query(sql.str());
    if (!result || result->HasError()) return results;

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); ++i) {
            results.push_back(chunk->GetValue(0, i).GetValue<int64_t>());
        }
    }

    return results;
}

bool DuckDBStore::add_call(int64_t caller_id, int64_t callee_id) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    std::ostringstream sql;
    sql << "INSERT OR IGNORE INTO call_edge (caller_id, callee_id) VALUES ("
        << caller_id << ", " << callee_id << ")";

    return write_execute(sql.str());
}

StoreHealth DuckDBStore::health() {
    // Lock handled in write_execute/write_query/read_query
    StoreHealth h;
    h.is_open = (db_ != nullptr);

    if (!db_) return h;

    // Count memories and avg confidence
    auto result = read_query("SELECT COUNT(*), AVG(confidence) FROM memory");
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            h.total_memories = chunk->GetValue(0, 0).GetValue<int64_t>();
            if (!chunk->GetValue(1, 0).IsNull()) {
                h.avg_confidence = chunk->GetValue(1, 0).GetValue<double>();
            }
        }
    }

    // Count symbols
    result = read_query("SELECT COUNT(*) FROM symbol");
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            h.total_symbols = chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }

    // Count triplets
    result = read_query("SELECT COUNT(*) FROM triplet");
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            h.total_triplets = chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }

    // Update cache
    {
        std::lock_guard<std::mutex> lock(health_cache_mutex_);
        h.cached_at = now();
        cached_health_ = h;
    }

    return h;
}

StoreHealth DuckDBStore::cached_health() {
    std::lock_guard<std::mutex> lock(health_cache_mutex_);
    // If cache is empty, just return minimal info
    if (cached_health_.cached_at == 0) {
        cached_health_.is_open = (db_ != nullptr);
    }
    return cached_health_;
}

void DuckDBStore::update_health_cache() {
    // Run health() which updates the cache
    health();
}

size_t DuckDBStore::memory_count() {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return 0;

    auto result = read_query("SELECT COUNT(*) FROM memory");
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            return chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }
    return 0;
}

size_t DuckDBStore::triplet_count() {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return 0;

    auto result = read_query("SELECT COUNT(*) FROM triplet");
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            return chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }
    return 0;
}

size_t DuckDBStore::symbol_count() {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return 0;

    auto result = read_query("SELECT COUNT(*) FROM symbol");
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            return chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }
    return 0;
}

std::vector<std::pair<std::string, size_t>> DuckDBStore::get_top_connected_entities(size_t limit) {
    // Lock handled in write_execute/write_query/read_query
    std::vector<std::pair<std::string, size_t>> results;
    if (!db_) return results;

    // Use SQL to count connections per entity efficiently
    // Combines subjects and objects, excludes code intel predicates for performance
    std::ostringstream sql;
    sql << "WITH entity_counts AS ("
        << "  SELECT entity, SUM(cnt) as total FROM ("
        << "    SELECT subject as entity, COUNT(*) as cnt FROM triplet "
        << "    WHERE predicate NOT IN ('contains', 'calls_text', 'callee_leaf', 'in_symbol') "
        << "    GROUP BY subject "
        << "    UNION ALL "
        << "    SELECT object as entity, COUNT(*) as cnt FROM triplet "
        << "    WHERE predicate NOT IN ('contains', 'calls_text', 'callee_leaf', 'in_symbol') "
        << "    GROUP BY object"
        << "  ) GROUP BY entity"
        << ") SELECT entity, total FROM entity_counts "
        << "WHERE total >= 3 ORDER BY total DESC LIMIT " << limit;

    auto result = read_query(sql.str());
    if (!result || result->HasError()) return results;

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); i++) {
            std::string entity = chunk->GetValue(0, i).ToString();
            size_t count = static_cast<size_t>(chunk->GetValue(1, i).GetValue<int64_t>());
            results.emplace_back(entity, count);
        }
    }
    return results;
}

std::vector<Symbol> DuckDBStore::bm25_search_symbols(const std::string& search_query, size_t limit) {
    // Lock handled in write_execute/write_query/read_query
    std::vector<Symbol> results;
    if (!db_ || !fts_loaded_) return results;

    // Escape search_query for SQL
    std::string escaped;
    for (char c : search_query) {
        if (c == '\'') escaped += "''";
        else escaped += c;
    }

    // Use FTS match_bm25 for ranked search
    std::ostringstream sql;
    sql << "SELECT s.id, s.kind, s.name, s.signature, s.file_path, "
        << "s.line_start, s.line_end, s.repo_id, "
        << "fts_main_symbol.match_bm25(s.id, '" << escaped << "') as score "
        << "FROM symbol s "
        << "WHERE score IS NOT NULL "
        << "ORDER BY score DESC "
        << "LIMIT " << limit;

    auto result = read_query(sql.str());
    if (!result || result->HasError()) {
        // Fallback to LIKE search if FTS fails
        std::ostringstream fallback;
        fallback << "SELECT id, kind, name, signature, file_path, "
                 << "line_start, line_end, repo_id "
                 << "FROM symbol WHERE name ILIKE '%" << escaped << "%' "
                 << "OR signature ILIKE '%" << escaped << "%' "
                 << "LIMIT " << limit;
        result = this->read_query(fallback.str());
        if (!result || result->HasError()) return results;
    }

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); i++) {
            Symbol sym;
            sym.id = chunk->GetValue(0, i).GetValue<int64_t>();
            sym.kind = chunk->GetValue(1, i).ToString();
            sym.name = chunk->GetValue(2, i).ToString();
            sym.signature = chunk->GetValue(3, i).ToString();
            sym.file_path = chunk->GetValue(4, i).ToString();
            sym.line_start = chunk->GetValue(5, i).GetValue<int32_t>();
            sym.line_end = chunk->GetValue(6, i).GetValue<int32_t>();
            sym.repo_id = chunk->GetValue(7, i).GetValue<int64_t>();
            results.push_back(std::move(sym));
        }
    }
    return results;
}

bool DuckDBStore::has_fts() const {
    return fts_loaded_;
}

std::vector<std::pair<int64_t, float>> DuckDBStore::bm25_search_memory(
    const std::string& query,
    size_t limit,
    const std::string& realm,
    bool include_global,
    const std::vector<std::string>& exclude_kinds) const {

    std::vector<std::pair<int64_t, float>> results;
    if (!db_ || !fts_loaded_ || query.empty()) return results;

    // Escape query for SQL
    std::string escaped;
    for (char c : query) {
        if (c == '\'') escaped += "''";
        else escaped += c;
    }

    // Build SQL with FTS match_bm25
    std::ostringstream sql;
    sql << "SELECT m.id, fts_main_memory.match_bm25(m.id, '" << escaped << "') as score "
        << "FROM memory m "
        << "WHERE score IS NOT NULL ";

    // Realm filter
    if (!realm.empty()) {
        std::string escaped_realm;
        for (char c : realm) {
            if (c == '\'') escaped_realm += "''";
            else escaped_realm += c;
        }
        if (include_global) {
            sql << "AND (m.realm = '" << escaped_realm << "' OR m.visibility = 2 "
                << "OR m.id IN (SELECT memory_id FROM realm_membership WHERE realm = '" << escaped_realm << "')) ";
        } else {
            sql << "AND (m.realm = '" << escaped_realm << "' "
                << "OR m.id IN (SELECT memory_id FROM realm_membership WHERE realm = '" << escaped_realm << "')) ";
        }
    }

    // Exclude kinds
    if (!exclude_kinds.empty()) {
        sql << "AND m.kind NOT IN (";
        for (size_t i = 0; i < exclude_kinds.size(); ++i) {
            if (i > 0) sql << ", ";
            sql << "'" << exclude_kinds[i] << "'";
        }
        sql << ") ";
    }

    sql << "ORDER BY score DESC LIMIT " << limit;

    auto result = read_query(sql.str());
    if (!result || result->HasError()) {
        // Fallback to LIKE search if FTS fails
        std::ostringstream fallback;
        fallback << "SELECT id, 0.5 as score FROM memory "
                 << "WHERE content ILIKE '%" << escaped << "%' "
                 << "LIMIT " << limit;
        result = const_cast<DuckDBStore*>(this)->read_query(fallback.str());
        if (!result || result->HasError()) return results;
    }

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); i++) {
            int64_t id = chunk->GetValue(0, i).GetValue<int64_t>();
            float score = chunk->GetValue(1, i).GetValue<float>();
            results.emplace_back(id, score);
        }
    }
    return results;
}

std::unordered_set<int64_t> DuckDBStore::tag_hits(const std::vector<std::string>& terms) const {
    std::unordered_set<int64_t> results;
    if (!db_ || terms.empty()) return results;

    // Build IN clause with escaped terms
    std::ostringstream sql;
    sql << "SELECT DISTINCT memory_id FROM memory_tags WHERE tag IN (";
    for (size_t i = 0; i < terms.size(); ++i) {
        if (i > 0) sql << ", ";
        sql << "'";
        for (char c : terms[i]) {
            if (c == '\'') sql << "''";
            else sql << c;
        }
        sql << "'";
    }
    sql << ")";

    auto result = read_query(sql.str());
    if (!result || result->HasError()) return results;

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); i++) {
            results.insert(chunk->GetValue(0, i).GetValue<int64_t>());
        }
    }
    return results;
}

std::string DuckDBStore::kind_to_string(NodeType type) {
    switch (type) {
        case NodeType::Wisdom: return "wisdom";
        case NodeType::Belief: return "belief";
        case NodeType::Intention: return "intention";
        case NodeType::Episode: return "episode";
        case NodeType::Symbol: return "symbol";
        case NodeType::Dream: return "dream";
        default: return "unknown";
    }
}

NodeType DuckDBStore::string_to_kind(const std::string& kind) {
    if (kind == "wisdom") return NodeType::Wisdom;
    if (kind == "belief") return NodeType::Belief;
    if (kind == "intention") return NodeType::Intention;
    if (kind == "episode") return NodeType::Episode;
    if (kind == "symbol") return NodeType::Symbol;
    if (kind == "dream") return NodeType::Dream;
    return NodeType::Episode;
}

// Ledger operations

int64_t DuckDBStore::save_ledger(const LedgerEntry& entry) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return -1;

    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    Timestamp now_ts = entry.created_at > 0 ? entry.created_at : now();

    std::ostringstream sql;
    sql << "INSERT INTO ledger (id, session_id, project, created_at, mood, coherence, confidence, "
        << "todos, active_files, decisions, next_steps, blockers, discoveries, snapshot) "
        << "VALUES (nextval('ledger_seq'), "
        << "'" << escape(entry.session_id) << "', "
        << "'" << escape(entry.project.empty() ? "default" : entry.project) << "', "
        << now_ts << ", "
        << "'" << escape(entry.mood) << "', "
        << entry.coherence << ", "
        << entry.confidence << ", "
        << "'" << escape(entry.todos) << "', "
        << "'" << escape(entry.active_files) << "', "
        << "'" << escape(entry.decisions) << "', "
        << "'" << escape(entry.next_steps) << "', "
        << "'" << escape(entry.blockers) << "', "
        << "'" << escape(entry.discoveries) << "', "
        << "'" << escape(entry.snapshot) << "') RETURNING id";

    // Use write_query for INSERT (not read_query) to ensure proper transaction handling
    auto result = write_query(sql.str());
    if (!result || result->HasError()) {
        return -1;
    }

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) {
        return -1;
    }

    return chunk->GetValue(0, 0).GetValue<int64_t>();
}

std::optional<LedgerEntry> DuckDBStore::load_ledger(const std::string& session_id, const std::string& project) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return std::nullopt;

    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    std::ostringstream sql;
    sql << "SELECT id, session_id, project, created_at, mood, coherence, confidence, "
        << "todos, active_files, decisions, next_steps, blockers, discoveries, snapshot "
        << "FROM ledger WHERE 1=1 ";

    if (!session_id.empty()) {
        sql << "AND session_id = '" << escape(session_id) << "' ";
    }
    if (!project.empty()) {
        sql << "AND project = '" << escape(project) << "' ";
    }

    sql << "ORDER BY created_at DESC LIMIT 1";

    auto result = read_query(sql.str());
    if (!result || result->HasError()) {
        return std::nullopt;
    }

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) {
        return std::nullopt;
    }

    LedgerEntry entry;
    entry.id = chunk->GetValue(0, 0).GetValue<int64_t>();
    entry.session_id = chunk->GetValue(1, 0).ToString();
    entry.project = chunk->GetValue(2, 0).ToString();
    entry.created_at = chunk->GetValue(3, 0).GetValue<int64_t>();
    entry.mood = chunk->GetValue(4, 0).ToString();
    entry.coherence = chunk->GetValue(5, 0).IsNull() ? 0.0f : chunk->GetValue(5, 0).GetValue<float>();
    entry.confidence = chunk->GetValue(6, 0).IsNull() ? 0.0f : chunk->GetValue(6, 0).GetValue<float>();
    entry.todos = chunk->GetValue(7, 0).ToString();
    entry.active_files = chunk->GetValue(8, 0).ToString();
    entry.decisions = chunk->GetValue(9, 0).ToString();
    entry.next_steps = chunk->GetValue(10, 0).ToString();
    entry.blockers = chunk->GetValue(11, 0).ToString();
    entry.discoveries = chunk->GetValue(12, 0).ToString();
    entry.snapshot = chunk->GetValue(13, 0).ToString();

    return entry;
}

std::vector<LedgerEntry> DuckDBStore::list_ledgers(const std::string& project, size_t limit) {
    // Lock handled in write_execute/write_query/read_query
    std::vector<LedgerEntry> entries;
    if (!db_) return entries;

    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    std::ostringstream sql;
    sql << "SELECT id, session_id, project, created_at, mood, coherence, confidence, "
        << "todos, active_files, decisions, next_steps, blockers, discoveries, snapshot "
        << "FROM ledger ";

    if (!project.empty()) {
        sql << "WHERE project = '" << escape(project) << "' ";
    }

    sql << "ORDER BY created_at DESC LIMIT " << limit;

    auto result = read_query(sql.str());
    if (!result || result->HasError()) {
        return entries;
    }

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); ++i) {
            LedgerEntry entry;
            entry.id = chunk->GetValue(0, i).GetValue<int64_t>();
            entry.session_id = chunk->GetValue(1, i).ToString();
            entry.project = chunk->GetValue(2, i).ToString();
            entry.created_at = chunk->GetValue(3, i).GetValue<int64_t>();
            entry.mood = chunk->GetValue(4, i).ToString();
            entry.coherence = chunk->GetValue(5, i).IsNull() ? 0.0f : chunk->GetValue(5, i).GetValue<float>();
            entry.confidence = chunk->GetValue(6, i).IsNull() ? 0.0f : chunk->GetValue(6, i).GetValue<float>();
            entry.todos = chunk->GetValue(7, i).ToString();
            entry.active_files = chunk->GetValue(8, i).ToString();
            entry.decisions = chunk->GetValue(9, i).ToString();
            entry.next_steps = chunk->GetValue(10, i).ToString();
            entry.blockers = chunk->GetValue(11, i).ToString();
            entry.discoveries = chunk->GetValue(12, i).ToString();
            entry.snapshot = chunk->GetValue(13, i).ToString();
            entries.push_back(entry);
        }
    }

    return entries;
}

std::optional<LedgerEntry> DuckDBStore::get_ledger(int64_t id) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return std::nullopt;

    std::ostringstream sql;
    sql << "SELECT id, session_id, project, created_at, mood, coherence, confidence, "
        << "todos, active_files, decisions, next_steps, blockers, discoveries, snapshot "
        << "FROM ledger WHERE id = " << id;

    auto result = read_query(sql.str());
    if (!result || result->HasError()) {
        return std::nullopt;
    }

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) {
        return std::nullopt;
    }

    LedgerEntry entry;
    entry.id = chunk->GetValue(0, 0).GetValue<int64_t>();
    entry.session_id = chunk->GetValue(1, 0).ToString();
    entry.project = chunk->GetValue(2, 0).ToString();
    entry.created_at = chunk->GetValue(3, 0).GetValue<int64_t>();
    entry.mood = chunk->GetValue(4, 0).ToString();
    entry.coherence = chunk->GetValue(5, 0).IsNull() ? 0.0f : chunk->GetValue(5, 0).GetValue<float>();
    entry.confidence = chunk->GetValue(6, 0).IsNull() ? 0.0f : chunk->GetValue(6, 0).GetValue<float>();
    entry.todos = chunk->GetValue(7, 0).ToString();
    entry.active_files = chunk->GetValue(8, 0).ToString();
    entry.decisions = chunk->GetValue(9, 0).ToString();
    entry.next_steps = chunk->GetValue(10, 0).ToString();
    entry.blockers = chunk->GetValue(11, 0).ToString();
    entry.discoveries = chunk->GetValue(12, 0).ToString();
    entry.snapshot = chunk->GetValue(13, 0).ToString();

    return entry;
}

bool DuckDBStore::delete_ledger(int64_t id) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    std::ostringstream sql;
    sql << "DELETE FROM ledger WHERE id = " << id;

    return write_execute(sql.str());
}

// ============================================================================
// Code File Tracking (Incremental Indexing)
// ============================================================================

bool DuckDBStore::set_file_metadata(const CodeFile& file) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    // Escape strings
    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    // UPSERT: insert or replace on conflict
    std::ostringstream sql;
    sql << "INSERT OR REPLACE INTO code_file "
        << "(path, project, mtime, indexed_at, symbols_count, callsites_count, file_hash) "
        << "VALUES ('"
        << escape(file.path) << "', '"
        << escape(file.project) << "', "
        << file.mtime << ", "
        << file.indexed_at << ", "
        << file.symbols_count << ", "
        << file.callsites_count << ", '"
        << escape(file.file_hash) << "')";

    return write_execute(sql.str());
}

std::optional<CodeFile> DuckDBStore::get_file_metadata(const std::string& path) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return std::nullopt;

    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    std::ostringstream sql;
    sql << "SELECT path, project, mtime, indexed_at, symbols_count, callsites_count, file_hash "
        << "FROM code_file WHERE path = '" << escape(path) << "'";

    auto result = read_query(sql.str());
    if (!result || result->HasError()) return std::nullopt;

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) return std::nullopt;

    CodeFile file;
    file.path = chunk->GetValue(0, 0).ToString();
    file.project = chunk->GetValue(1, 0).ToString();
    file.mtime = chunk->GetValue(2, 0).GetValue<int64_t>();
    file.indexed_at = chunk->GetValue(3, 0).GetValue<int64_t>();
    file.symbols_count = chunk->GetValue(4, 0).GetValue<int32_t>();
    file.callsites_count = chunk->GetValue(5, 0).GetValue<int32_t>();
    file.file_hash = chunk->GetValue(6, 0).ToString();

    return file;
}

std::vector<CodeFile> DuckDBStore::list_project_files(const std::string& project) {
    // Lock handled in write_execute/write_query/read_query
    std::vector<CodeFile> files;
    if (!db_) return files;

    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    std::ostringstream sql;
    sql << "SELECT path, project, mtime, indexed_at, symbols_count, callsites_count, file_hash "
        << "FROM code_file WHERE project = '" << escape(project) << "' "
        << "ORDER BY path";

    auto result = read_query(sql.str());
    if (!result || result->HasError()) return files;

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); ++i) {
            CodeFile file;
            file.path = chunk->GetValue(0, i).ToString();
            file.project = chunk->GetValue(1, i).ToString();
            file.mtime = chunk->GetValue(2, i).GetValue<int64_t>();
            file.indexed_at = chunk->GetValue(3, i).GetValue<int64_t>();
            file.symbols_count = chunk->GetValue(4, i).GetValue<int32_t>();
            file.callsites_count = chunk->GetValue(5, i).GetValue<int32_t>();
            file.file_hash = chunk->GetValue(6, i).ToString();
            files.push_back(file);
        }
    }

    return files;
}

bool DuckDBStore::delete_file_metadata(const std::string& path) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    std::ostringstream sql;
    sql << "DELETE FROM code_file WHERE path = '" << escape(path) << "'";

    return write_execute(sql.str());
}

bool DuckDBStore::delete_project_files(const std::string& project) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    std::ostringstream sql;
    sql << "DELETE FROM code_file WHERE project = '" << escape(project) << "'";

    return write_execute(sql.str());
}

size_t DuckDBStore::delete_file_symbols(const std::string& file_path) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return 0;

    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    // Get count first
    std::ostringstream count_sql;
    count_sql << "SELECT COUNT(*) FROM symbol WHERE file_path = '" << escape(file_path) << "'";
    auto result = read_query(count_sql.str());
    size_t count = 0;
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            count = chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }

    // Delete
    std::ostringstream sql;
    sql << "DELETE FROM symbol WHERE file_path = '" << escape(file_path) << "'";
    write_execute(sql.str());

    return count;
}

size_t DuckDBStore::delete_file_triplets(const std::string& file_path) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return 0;

    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    // Fast deletion using indexed source_file column
    std::ostringstream count_sql;
    count_sql << "SELECT COUNT(*) FROM triplet WHERE source_file = '" << escape(file_path) << "'";
    auto result = read_query(count_sql.str());
    size_t count = 0;
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            count = chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }

    std::ostringstream sql;
    sql << "DELETE FROM triplet WHERE source_file = '" << escape(file_path) << "'";
    write_execute(sql.str());

    return count;
}

DuckDBStore::ClearProjectResult DuckDBStore::clear_project_codebase(const std::string& project) {
    ClearProjectResult result;
    if (!db_ || project.empty()) return result;

    // Get all files for this project
    auto files = list_project_files(project);
    result.files_deleted = files.size();

    // Delete symbols and triplets for each file
    for (const auto& file : files) {
        result.symbols_deleted += delete_file_symbols(file.path);
        result.triplets_deleted += delete_file_triplets(file.path);
    }

    // Delete file metadata
    delete_project_files(project);

    return result;
}

size_t DuckDBStore::count_triplets_by_pattern(const std::string& pattern) {
    if (!db_ || pattern.empty()) return 0;

    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    std::ostringstream sql;
    sql << "SELECT COUNT(*) FROM triplet WHERE subject LIKE '" << escape(pattern) << "'";

    auto result = read_query(sql.str());
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            return chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }
    return 0;
}

size_t DuckDBStore::delete_triplets_by_pattern(const std::string& pattern) {
    if (!db_ || pattern.empty()) return 0;

    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    // Count first
    size_t count = count_triplets_by_pattern(pattern);
    if (count == 0) return 0;

    // Delete
    std::ostringstream sql;
    sql << "DELETE FROM triplet WHERE subject LIKE '" << escape(pattern) << "'";
    write_execute(sql.str());

    return count;
}

// ═══════════════════════════════════════════════════════════════════════════
// Semantic Enrichment for Code Symbols
// ═══════════════════════════════════════════════════════════════════════════

std::vector<DuckDBStore::UndescribedSymbol> DuckDBStore::get_undescribed_symbols(size_t limit) {
    std::vector<UndescribedSymbol> result;
    if (!db_) return result;

    // Priority: class=0, function=1, method=2, other=3
    // Get symbols without description (new) or memory_id (legacy), ordered by priority
    std::ostringstream sql;
    sql << "SELECT id, kind, name, signature, file_path, line_start, line_end, "
        << "CASE kind "
        << "  WHEN 'class' THEN 0 "
        << "  WHEN 'struct' THEN 0 "
        << "  WHEN 'function' THEN 1 "
        << "  WHEN 'method' THEN 2 "
        << "  ELSE 3 "
        << "END as priority "
        << "FROM symbol "
        << "WHERE description IS NULL AND memory_id IS NULL "
        << "ORDER BY priority, id "
        << "LIMIT " << limit;

    auto query_result = read_query(sql.str());
    if (!query_result || query_result->HasError()) return result;

    while (true) {
        auto chunk = query_result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); ++i) {
            UndescribedSymbol sym;
            sym.id = chunk->GetValue(0, i).GetValue<int64_t>();
            sym.kind = chunk->GetValue(1, i).ToString();
            sym.name = chunk->GetValue(2, i).ToString();
            sym.signature = chunk->GetValue(3, i).ToString();
            sym.file_path = chunk->GetValue(4, i).ToString();
            sym.line_start = chunk->GetValue(5, i).GetValue<int32_t>();
            sym.line_end = chunk->GetValue(6, i).GetValue<int32_t>();
            sym.priority = chunk->GetValue(7, i).GetValue<int32_t>();
            result.push_back(sym);
        }
    }

    return result;
}

bool DuckDBStore::set_symbol_memory(int64_t symbol_id, int64_t memory_id) {
    if (!db_) return false;

    std::ostringstream sql;
    sql << "UPDATE symbol SET memory_id = " << memory_id
        << ", described_at = " << now()
        << " WHERE id = " << symbol_id;

    return write_execute(sql.str());
}

bool DuckDBStore::set_symbol_description(int64_t symbol_id, const std::string& description) {
    if (!db_) return false;

    // Escape single quotes in description
    std::string escaped;
    for (char c : description) {
        if (c == '\'') escaped += "''";
        else escaped += c;
    }

    std::ostringstream sql;
    sql << "UPDATE symbol SET description = '" << escaped << "'"
        << ", described_at = " << now()
        << " WHERE id = " << symbol_id;

    return write_execute(sql.str());
}

size_t DuckDBStore::count_undescribed_symbols() {
    if (!db_) return 0;

    // Count symbols without description (new) or memory_id (legacy)
    auto result = read_query("SELECT COUNT(*) FROM symbol WHERE description IS NULL AND memory_id IS NULL");
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            return chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }
    return 0;
}

size_t DuckDBStore::count_total_symbols() {
    if (!db_) return 0;

    auto result = read_query("SELECT COUNT(*) FROM symbol");
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            return chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }
    return 0;
}

bool DuckDBStore::set_symbol_embedding(int64_t symbol_id, const std::vector<float>& embedding) {
    if (!db_ || embedding.size() != 384) return false;

    // Build embedding array
    std::ostringstream emb_sql;
    emb_sql << "[";
    for (size_t i = 0; i < embedding.size(); ++i) {
        if (i > 0) emb_sql << ",";
        emb_sql << embedding[i];
    }
    emb_sql << "]";
    std::string emb_array = emb_sql.str();

    // If embeddings DB is available, write there (contention-free)
    if (emb_conn_) {
        std::lock_guard<std::mutex> lock(emb_mutex_);
        try {
            // Delete first to avoid HNSW duplicate key errors
            std::ostringstream del_sql;
            del_sql << "DELETE FROM symbol_embeddings WHERE symbol_id = " << symbol_id;
            emb_conn_->Query(del_sql.str());

            std::ostringstream sql;
            sql << "INSERT INTO symbol_embeddings (symbol_id, embedding) VALUES ("
                << symbol_id << ", " << emb_array << ")";
            emb_conn_->Query(sql.str());

            // Also update described_at in main DB (fast, no embedding blob)
            write_execute("UPDATE symbol SET described_at = " + std::to_string(now()) +
                         " WHERE id = " + std::to_string(symbol_id));
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[DuckDBStore] Embeddings DB write failed: " << e.what() << "\n";
            // Fall through to main DB
        }
    }

    // Fallback: write to main DB (original behavior)
    std::ostringstream sql;
    sql << "UPDATE symbol SET embedding = " << emb_array
        << ", described_at = " << now() << " WHERE id = " << symbol_id;
    return write_execute(sql.str());
}

std::vector<DuckDBStore::UndescribedSymbol> DuckDBStore::get_unembedded_symbols(size_t limit) {
    std::vector<UndescribedSymbol> result;
    if (!db_) return result;

    // If using separate embeddings DB, check which symbols don't have embeddings there
    std::unordered_set<int64_t> embedded_ids;
    if (emb_conn_) {
        std::lock_guard<std::mutex> lock(emb_mutex_);
        try {
            auto emb_result = emb_conn_->Query("SELECT symbol_id FROM symbol_embeddings");
            if (emb_result && !emb_result->HasError()) {
                while (auto chunk = emb_result->Fetch()) {
                    if (!chunk || chunk->size() == 0) break;
                    for (size_t i = 0; i < chunk->size(); ++i) {
                        embedded_ids.insert(chunk->GetValue(0, i).GetValue<int64_t>());
                    }
                }
            }
        } catch (...) {}
    }

    // Get symbols with NULL or zero embedding
    std::ostringstream sql;
    sql << "SELECT id, kind, name, signature, file_path, line_start, line_end, "
        << "CASE kind "
        << "  WHEN 'class' THEN 0 "
        << "  WHEN 'struct' THEN 0 "
        << "  WHEN 'function' THEN 1 "
        << "  WHEN 'method' THEN 2 "
        << "  ELSE 3 "
        << "END as priority "
        << "FROM symbol "
        << "WHERE embedding IS NULL OR described_at = 0 "
        << "ORDER BY priority, id "
        << "LIMIT " << (limit * 2);  // Over-fetch to account for filtering

    auto query_result = read_query(sql.str());
    if (!query_result || query_result->HasError()) return result;

    while (true) {
        auto chunk = query_result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); ++i) {
            int64_t id = chunk->GetValue(0, i).GetValue<int64_t>();

            // Skip if already embedded in separate embeddings DB
            if (!embedded_ids.empty() && embedded_ids.count(id)) continue;

            UndescribedSymbol sym;
            sym.id = id;
            sym.kind = chunk->GetValue(1, i).ToString();
            sym.name = chunk->GetValue(2, i).ToString();
            sym.signature = chunk->GetValue(3, i).ToString();
            sym.file_path = chunk->GetValue(4, i).ToString();
            sym.line_start = chunk->GetValue(5, i).GetValue<int32_t>();
            sym.line_end = chunk->GetValue(6, i).GetValue<int32_t>();
            sym.priority = chunk->GetValue(7, i).GetValue<int32_t>();
            result.push_back(sym);

            if (result.size() >= limit) break;
        }
        if (result.size() >= limit) break;
    }

    return result;
}

size_t DuckDBStore::count_unembedded_symbols() {
    if (!db_) return 0;

    auto result = read_query("SELECT COUNT(*) FROM symbol WHERE embedding IS NULL OR described_at = 0");
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            return chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }
    return 0;
}

std::vector<DuckDBStore::SymbolMatch> DuckDBStore::search_symbols_by_embedding(
    const std::vector<float>& query_embedding, size_t limit, const std::string& kind_filter) {

    std::vector<SymbolMatch> results;
    if (!db_ || query_embedding.size() != 384) return results;

    // Build query embedding array literal
    std::ostringstream emb_str;
    emb_str << "[";
    for (size_t i = 0; i < query_embedding.size(); ++i) {
        if (i > 0) emb_str << ",";
        emb_str << query_embedding[i];
    }
    emb_str << "]::FLOAT[384]";

    // Use cosine similarity: 1 - (a <=> b) gives similarity score
    std::ostringstream sql;
    sql << "SELECT id, kind, name, signature, file_path, line_start, line_end, repo_id, "
        << "(1 - list_cosine_distance(embedding, " << emb_str.str() << ")) as score "
        << "FROM symbol "
        << "WHERE embedding IS NOT NULL AND described_at > 0 ";

    if (!kind_filter.empty()) {
        sql << "AND kind = '" << kind_filter << "' ";
    }

    sql << "ORDER BY score DESC "
        << "LIMIT " << limit;

    auto result = read_query(sql.str());
    if (!result || result->HasError()) return results;

    while (auto chunk = result->Fetch()) {
        for (size_t i = 0; i < chunk->size(); ++i) {
            SymbolMatch match;
            match.symbol.id = chunk->GetValue(0, i).GetValue<int64_t>();
            match.symbol.kind = chunk->GetValue(1, i).ToString();
            match.symbol.name = chunk->GetValue(2, i).ToString();
            match.symbol.signature = chunk->GetValue(3, i).IsNull() ? "" : chunk->GetValue(3, i).ToString();
            match.symbol.file_path = chunk->GetValue(4, i).ToString();
            match.symbol.line_start = chunk->GetValue(5, i).GetValue<int32_t>();
            match.symbol.line_end = chunk->GetValue(6, i).GetValue<int32_t>();
            match.symbol.repo_id = chunk->GetValue(7, i).IsNull() ? 0 : chunk->GetValue(7, i).GetValue<int64_t>();
            match.score = chunk->GetValue(8, i).GetValue<float>();
            results.push_back(match);
        }
    }

    return results;
}

bool DuckDBStore::execute_raw(const std::string& sql) {
    return write_execute(sql);
}

// ═══════════════════════════════════════════════════════════════════════════
// Transcript State Operations (for distillation)
// ═══════════════════════════════════════════════════════════════════════════

bool DuckDBStore::register_transcript(const std::string& session_id, const std::string& transcript_path,
                                       const std::string& realm) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    auto escape = [](const std::string& s) {
        std::string result;
        result.reserve(s.size() + 10);
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    Timestamp now_ts = now();

    // Use ON CONFLICT for upsert (more reliable than INSERT OR REPLACE)
    std::ostringstream sql;
    sql << "INSERT INTO transcript_state "
        << "(session_id, transcript_path, realm, last_processed_line, last_distilled_at, created_at) "
        << "VALUES ("
        << "'" << escape(session_id) << "', "
        << "'" << escape(transcript_path) << "', "
        << "'" << escape(realm.empty() ? "default" : realm) << "', "
        << "0, 0, " << now_ts << ") "
        << "ON CONFLICT (session_id) DO UPDATE SET "
        << "transcript_path = EXCLUDED.transcript_path, "
        << "realm = EXCLUDED.realm";

    return write_execute(sql.str());
}

std::optional<TranscriptState> DuckDBStore::get_transcript(const std::string& session_id) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return std::nullopt;

    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    std::ostringstream sql;
    sql << "SELECT session_id, transcript_path, realm, last_processed_line, last_distilled_at, created_at "
        << "FROM transcript_state WHERE session_id = '" << escape(session_id) << "'";

    auto result = read_query(sql.str());
    if (!result || result->HasError()) {
        return std::nullopt;
    }

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) {
        return std::nullopt;
    }

    TranscriptState state;
    state.session_id = chunk->GetValue(0, 0).ToString();
    state.transcript_path = chunk->GetValue(1, 0).ToString();
    state.realm = chunk->GetValue(2, 0).ToString();
    state.last_processed_line = chunk->GetValue(3, 0).GetValue<int64_t>();
    state.last_distilled_at = chunk->GetValue(4, 0).GetValue<int64_t>();
    state.created_at = chunk->GetValue(5, 0).GetValue<int64_t>();

    return state;
}

std::vector<TranscriptState> DuckDBStore::get_pending_transcripts() {
    // Lock handled in write_execute/write_query/read_query
    std::vector<TranscriptState> states;
    if (!db_) return states;

    // Get all transcripts - daemon will check file sizes
    auto result = read_query(
        "SELECT session_id, transcript_path, realm, last_processed_line, last_distilled_at, created_at "
        "FROM transcript_state ORDER BY created_at"
    );

    if (!result || result->HasError()) {
        return states;
    }

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); ++i) {
            TranscriptState state;
            state.session_id = chunk->GetValue(0, i).ToString();
            state.transcript_path = chunk->GetValue(1, i).ToString();
            state.realm = chunk->GetValue(2, i).ToString();
            state.last_processed_line = chunk->GetValue(3, i).GetValue<int64_t>();
            state.last_distilled_at = chunk->GetValue(4, i).GetValue<int64_t>();
            state.created_at = chunk->GetValue(5, i).GetValue<int64_t>();
            states.push_back(std::move(state));
        }
    }

    return states;
}

bool DuckDBStore::update_transcript_progress(const std::string& session_id, int64_t last_line) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    std::ostringstream sql;
    sql << "UPDATE transcript_state SET last_processed_line = " << last_line
        << " WHERE session_id = '" << escape(session_id) << "'";

    return write_execute(sql.str());
}

bool DuckDBStore::mark_transcript_distilled(const std::string& session_id) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    Timestamp now_ts = now();

    std::ostringstream sql;
    sql << "UPDATE transcript_state SET last_distilled_at = " << now_ts
        << " WHERE session_id = '" << escape(session_id) << "'";

    return write_execute(sql.str());
}

bool DuckDBStore::remove_transcript(const std::string& session_id) {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return false;

    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    std::ostringstream sql;
    sql << "DELETE FROM transcript_state WHERE session_id = '" << escape(session_id) << "'";

    return write_execute(sql.str());
}

size_t DuckDBStore::transcript_count() {
    // Lock handled in write_execute/write_query/read_query
    if (!db_) return 0;

    auto result = read_query("SELECT COUNT(*) FROM transcript_state");
    if (!result || result->HasError()) {
        return 0;
    }

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) {
        return 0;
    }

    return chunk->GetValue(0, 0).GetValue<int64_t>();
}

// ============================================================================
// Long-running tasks (mind-powered Ralph Wiggum)
// ============================================================================

int64_t DuckDBStore::task_start(const LongTask& task) {
    if (!db_ || task.task_id.empty() || task.goal.empty()) return -1;

    Timestamp now_ts = now();

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    std::ostringstream sql;
    sql << "INSERT INTO long_task (id, task_id, goal, realm, status, "
        << "hard_checks, soft_checks, work_items, completed_summary, blockers, "
        << "agent_id, lease_until, iterations, started_at, updated_at, completed_at, outcome) "
        << "VALUES (nextval('task_seq'), '" << escape(task.task_id) << "', "
        << "'" << escape(task.goal) << "', "
        << "'" << escape(task.realm.empty() ? "brahman" : task.realm) << "', "
        << "'active', "
        << "'" << escape(task.hard_checks) << "', "
        << "'" << escape(task.soft_checks) << "', "
        << "'" << escape(task.work_items) << "', "
        << "'', '', '', 0, 0, " << now_ts << ", " << now_ts << ", 0, '') "
        << "RETURNING id";

    auto result = read_query(sql.str());
    if (!result || result->HasError()) {
        last_error_ = result ? result->GetError() : "No result";
        return -1;
    }

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) return -1;

    return chunk->GetValue(0, 0).GetValue<int64_t>();
}

bool DuckDBStore::task_update(const std::string& task_id, const LongTask& updates) {
    if (!db_ || task_id.empty()) return false;

    Timestamp now_ts = now();

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    std::ostringstream sql;
    sql << "UPDATE long_task SET updated_at = " << now_ts;

    if (!updates.status.empty()) sql << ", status = '" << escape(updates.status) << "'";
    if (!updates.work_items.empty()) sql << ", work_items = '" << escape(updates.work_items) << "'";
    if (!updates.completed_summary.empty()) sql << ", completed_summary = '" << escape(updates.completed_summary) << "'";
    if (!updates.blockers.empty()) sql << ", blockers = '" << escape(updates.blockers) << "'";
    if (!updates.agent_id.empty()) sql << ", agent_id = '" << escape(updates.agent_id) << "'";
    if (updates.lease_until > 0) sql << ", lease_until = " << updates.lease_until;
    if (updates.iterations > 0) sql << ", iterations = iterations + 1";

    sql << " WHERE task_id = '" << escape(task_id) << "'";

    return write_execute(sql.str());
}

std::optional<LongTask> DuckDBStore::task_get(const std::string& task_id) {
    if (!db_ || task_id.empty()) return std::nullopt;

    std::string escaped;
    for (char c : task_id) {
        if (c == '\'') escaped += "''";
        else escaped += c;
    }

    auto result = read_query(
        "SELECT id, task_id, goal, realm, status, hard_checks, soft_checks, "
        "work_items, completed_summary, blockers, agent_id, lease_until, "
        "iterations, started_at, updated_at, completed_at, outcome "
        "FROM long_task WHERE task_id = '" + escaped + "'"
    );

    if (!result || result->HasError()) return std::nullopt;

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) return std::nullopt;

    LongTask task;
    task.id = chunk->GetValue(0, 0).GetValue<int64_t>();
    task.task_id = chunk->GetValue(1, 0).ToString();
    task.goal = chunk->GetValue(2, 0).ToString();
    task.realm = chunk->GetValue(3, 0).ToString();
    task.status = chunk->GetValue(4, 0).ToString();
    task.hard_checks = chunk->GetValue(5, 0).ToString();
    task.soft_checks = chunk->GetValue(6, 0).ToString();
    task.work_items = chunk->GetValue(7, 0).ToString();
    task.completed_summary = chunk->GetValue(8, 0).ToString();
    task.blockers = chunk->GetValue(9, 0).ToString();
    task.agent_id = chunk->GetValue(10, 0).ToString();
    task.lease_until = chunk->GetValue(11, 0).GetValue<int64_t>();
    task.iterations = chunk->GetValue(12, 0).GetValue<int32_t>();
    task.started_at = chunk->GetValue(13, 0).GetValue<int64_t>();
    task.updated_at = chunk->GetValue(14, 0).GetValue<int64_t>();
    task.completed_at = chunk->GetValue(15, 0).GetValue<int64_t>();
    task.outcome = chunk->GetValue(16, 0).ToString();

    return task;
}

std::optional<LongTask> DuckDBStore::task_get_active(const std::string& realm) {
    if (!db_) return std::nullopt;

    std::string sql = "SELECT id, task_id, goal, realm, status, hard_checks, soft_checks, "
        "work_items, completed_summary, blockers, agent_id, lease_until, "
        "iterations, started_at, updated_at, completed_at, outcome "
        "FROM long_task WHERE status = 'active'";

    if (!realm.empty()) {
        std::string escaped;
        for (char c : realm) {
            if (c == '\'') escaped += "''";
            else escaped += c;
        }
        sql += " AND realm = '" + escaped + "'";
    }

    sql += " ORDER BY updated_at DESC LIMIT 1";

    auto result = read_query(sql);
    if (!result || result->HasError()) return std::nullopt;

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) return std::nullopt;

    LongTask task;
    task.id = chunk->GetValue(0, 0).GetValue<int64_t>();
    task.task_id = chunk->GetValue(1, 0).ToString();
    task.goal = chunk->GetValue(2, 0).ToString();
    task.realm = chunk->GetValue(3, 0).ToString();
    task.status = chunk->GetValue(4, 0).ToString();
    task.hard_checks = chunk->GetValue(5, 0).ToString();
    task.soft_checks = chunk->GetValue(6, 0).ToString();
    task.work_items = chunk->GetValue(7, 0).ToString();
    task.completed_summary = chunk->GetValue(8, 0).ToString();
    task.blockers = chunk->GetValue(9, 0).ToString();
    task.agent_id = chunk->GetValue(10, 0).ToString();
    task.lease_until = chunk->GetValue(11, 0).GetValue<int64_t>();
    task.iterations = chunk->GetValue(12, 0).GetValue<int32_t>();
    task.started_at = chunk->GetValue(13, 0).GetValue<int64_t>();
    task.updated_at = chunk->GetValue(14, 0).GetValue<int64_t>();
    task.completed_at = chunk->GetValue(15, 0).GetValue<int64_t>();
    task.outcome = chunk->GetValue(16, 0).ToString();

    return task;
}

std::vector<LongTask> DuckDBStore::task_list(const std::string& realm, const std::string& status) {
    std::vector<LongTask> tasks;
    if (!db_) return tasks;

    std::string sql = "SELECT id, task_id, goal, realm, status, hard_checks, soft_checks, "
        "work_items, completed_summary, blockers, agent_id, lease_until, "
        "iterations, started_at, updated_at, completed_at, outcome "
        "FROM long_task WHERE 1=1";

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    if (!realm.empty()) sql += " AND realm = '" + escape(realm) + "'";
    if (!status.empty()) sql += " AND status = '" + escape(status) + "'";

    sql += " ORDER BY updated_at DESC LIMIT 50";

    auto result = read_query(sql);
    if (!result || result->HasError()) return tasks;

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); i++) {
            LongTask task;
            task.id = chunk->GetValue(0, i).GetValue<int64_t>();
            task.task_id = chunk->GetValue(1, i).ToString();
            task.goal = chunk->GetValue(2, i).ToString();
            task.realm = chunk->GetValue(3, i).ToString();
            task.status = chunk->GetValue(4, i).ToString();
            task.hard_checks = chunk->GetValue(5, i).ToString();
            task.soft_checks = chunk->GetValue(6, i).ToString();
            task.work_items = chunk->GetValue(7, i).ToString();
            task.completed_summary = chunk->GetValue(8, i).ToString();
            task.blockers = chunk->GetValue(9, i).ToString();
            task.agent_id = chunk->GetValue(10, i).ToString();
            task.lease_until = chunk->GetValue(11, i).GetValue<int64_t>();
            task.iterations = chunk->GetValue(12, i).GetValue<int32_t>();
            task.started_at = chunk->GetValue(13, i).GetValue<int64_t>();
            task.updated_at = chunk->GetValue(14, i).GetValue<int64_t>();
            task.completed_at = chunk->GetValue(15, i).GetValue<int64_t>();
            task.outcome = chunk->GetValue(16, i).ToString();
            tasks.push_back(task);
        }
    }

    return tasks;
}

bool DuckDBStore::task_complete(const std::string& task_id, const std::string& outcome) {
    if (!db_ || task_id.empty()) return false;

    Timestamp now_ts = now();

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    std::ostringstream sql;
    sql << "UPDATE long_task SET status = 'completed', "
        << "completed_at = " << now_ts << ", "
        << "updated_at = " << now_ts << ", "
        << "outcome = '" << escape(outcome) << "' "
        << "WHERE task_id = '" << escape(task_id) << "'";

    return write_execute(sql.str());
}

bool DuckDBStore::task_abandon(const std::string& task_id, const std::string& reason) {
    if (!db_ || task_id.empty()) return false;

    Timestamp now_ts = now();

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    std::ostringstream sql;
    sql << "UPDATE long_task SET status = 'abandoned', "
        << "updated_at = " << now_ts << ", "
        << "outcome = '" << escape(reason) << "' "
        << "WHERE task_id = '" << escape(task_id) << "'";

    return write_execute(sql.str());
}

bool DuckDBStore::task_claim(const std::string& task_id, const std::string& agent_id, int64_t lease_seconds) {
    if (!db_ || task_id.empty() || agent_id.empty()) return false;

    Timestamp now_ts = now();
    int64_t lease_until = now_ts + (lease_seconds * 1000);  // Convert to ms

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    // Claim only if unclaimed or lease expired
    std::ostringstream sql;
    sql << "UPDATE long_task SET "
        << "agent_id = '" << escape(agent_id) << "', "
        << "lease_until = " << lease_until << ", "
        << "updated_at = " << now_ts
        << " WHERE task_id = '" << escape(task_id) << "' "
        << "AND status = 'active' "
        << "AND (agent_id = '' OR agent_id IS NULL OR lease_until < " << now_ts << ")";

    return write_execute(sql.str());
}

bool DuckDBStore::task_heartbeat(const std::string& task_id, const std::string& agent_id) {
    if (!db_ || task_id.empty() || agent_id.empty()) return false;

    Timestamp now_ts = now();
    int64_t lease_until = now_ts + 300000;  // 5 minutes

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    // Only extend if we own the lease
    std::ostringstream sql;
    sql << "UPDATE long_task SET "
        << "lease_until = " << lease_until << ", "
        << "updated_at = " << now_ts
        << " WHERE task_id = '" << escape(task_id) << "' "
        << "AND agent_id = '" << escape(agent_id) << "'";

    return write_execute(sql.str());
}

// ============================================================================
// Task events (append-only log)
// ============================================================================

int64_t DuckDBStore::event_append(const TaskEvent& event) {
    if (!db_ || event.task_id.empty() || event.kind.empty()) return -1;

    Timestamp now_ts = now();

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    std::ostringstream sql;
    sql << "INSERT INTO task_event (id, task_id, kind, payload, tags, related_entities, created_at) "
        << "VALUES (nextval('event_seq'), "
        << "'" << escape(event.task_id) << "', "
        << "'" << escape(event.kind) << "', "
        << "'" << escape(event.payload) << "', "
        << "'" << escape(event.tags) << "', "
        << "'" << escape(event.related_entities) << "', "
        << now_ts << ") RETURNING id";

    auto result = read_query(sql.str());
    if (!result || result->HasError()) {
        last_error_ = result ? result->GetError() : "No result";
        return -1;
    }

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) return -1;

    return chunk->GetValue(0, 0).GetValue<int64_t>();
}

std::vector<TaskEvent> DuckDBStore::event_list(const std::string& task_id, size_t limit) {
    std::vector<TaskEvent> events;
    if (!db_ || task_id.empty()) return events;

    std::string escaped;
    for (char c : task_id) {
        if (c == '\'') escaped += "''";
        else escaped += c;
    }

    std::ostringstream sql;
    sql << "SELECT id, task_id, kind, payload, tags, related_entities, created_at "
        << "FROM task_event WHERE task_id = '" << escaped << "' "
        << "ORDER BY created_at ASC LIMIT " << limit;

    auto result = read_query(sql.str());
    if (!result || result->HasError()) return events;

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); i++) {
            TaskEvent event;
            event.id = chunk->GetValue(0, i).GetValue<int64_t>();
            event.task_id = chunk->GetValue(1, i).ToString();
            event.kind = chunk->GetValue(2, i).ToString();
            event.payload = chunk->GetValue(3, i).ToString();
            event.tags = chunk->GetValue(4, i).ToString();
            event.related_entities = chunk->GetValue(5, i).ToString();
            event.created_at = chunk->GetValue(6, i).GetValue<int64_t>();
            events.push_back(event);
        }
    }

    return events;
}

std::vector<TaskEvent> DuckDBStore::event_get_recent(const std::string& task_id, const std::string& kind, size_t limit) {
    std::vector<TaskEvent> events;
    if (!db_ || task_id.empty()) return events;

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    std::ostringstream sql;
    sql << "SELECT id, task_id, kind, payload, tags, related_entities, created_at "
        << "FROM task_event WHERE task_id = '" << escape(task_id) << "'";

    if (!kind.empty()) {
        sql << " AND kind = '" << escape(kind) << "'";
    }

    sql << " ORDER BY created_at DESC LIMIT " << limit;

    auto result = read_query(sql.str());
    if (!result || result->HasError()) return events;

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); i++) {
            TaskEvent event;
            event.id = chunk->GetValue(0, i).GetValue<int64_t>();
            event.task_id = chunk->GetValue(1, i).ToString();
            event.kind = chunk->GetValue(2, i).ToString();
            event.payload = chunk->GetValue(3, i).ToString();
            event.tags = chunk->GetValue(4, i).ToString();
            event.related_entities = chunk->GetValue(5, i).ToString();
            event.created_at = chunk->GetValue(6, i).GetValue<int64_t>();
            events.push_back(event);
        }
    }

    return events;
}

// ============================================================================
// Suggestion tracking (loop closure)
// ============================================================================

int64_t DuckDBStore::suggestion_track(const Suggestion& suggestion) {
    if (!db_ || suggestion.content.empty()) return -1;

    Timestamp now_ts = now();

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    std::ostringstream sql;
    sql << "INSERT INTO suggestion (id, content, context, realm, status, helped, "
        << "outcome_details, memory_id, suggested_at, resolved_at) "
        << "VALUES (nextval('suggestion_seq'), '" << escape(suggestion.content) << "', "
        << "'" << escape(suggestion.context) << "', "
        << "'" << escape(suggestion.realm.empty() ? "brahman" : suggestion.realm) << "', "
        << "'pending', FALSE, '', 0, " << now_ts << ", 0) "
        << "RETURNING id";

    auto result = read_query(sql.str());
    if (!result || result->HasError()) {
        last_error_ = result ? result->GetError() : "No result";
        return -1;
    }

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) return -1;

    return chunk->GetValue(0, 0).GetValue<int64_t>();
}

std::vector<Suggestion> DuckDBStore::suggestion_list_pending(const std::string& realm, size_t limit) {
    std::vector<Suggestion> suggestions;
    if (!db_) return suggestions;

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    std::string sql = "SELECT id, content, context, realm, status, helped, "
                      "outcome_details, memory_id, suggested_at, resolved_at "
                      "FROM suggestion WHERE status = 'pending'";

    if (!realm.empty()) sql += " AND realm = '" + escape(realm) + "'";
    sql += " ORDER BY suggested_at DESC LIMIT " + std::to_string(limit);

    auto result = read_query(sql);
    if (!result || result->HasError()) return suggestions;

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); i++) {
            Suggestion s;
            s.id = chunk->GetValue(0, i).GetValue<int64_t>();
            s.content = chunk->GetValue(1, i).ToString();
            s.context = chunk->GetValue(2, i).ToString();
            s.realm = chunk->GetValue(3, i).ToString();
            s.status = chunk->GetValue(4, i).ToString();
            s.helped = chunk->GetValue(5, i).GetValue<bool>();
            s.outcome_details = chunk->GetValue(6, i).ToString();
            s.memory_id = chunk->GetValue(7, i).GetValue<int64_t>();
            s.suggested_at = chunk->GetValue(8, i).GetValue<int64_t>();
            s.resolved_at = chunk->GetValue(9, i).GetValue<int64_t>();
            suggestions.push_back(s);
        }
    }

    return suggestions;
}

bool DuckDBStore::suggestion_resolve(int64_t id, bool helped, const std::string& details, int64_t memory_id) {
    if (!db_ || id <= 0) return false;

    Timestamp now_ts = now();

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    std::ostringstream sql;
    sql << "UPDATE suggestion SET status = 'resolved', helped = " << (helped ? "TRUE" : "FALSE")
        << ", outcome_details = '" << escape(details) << "'"
        << ", memory_id = " << memory_id
        << ", resolved_at = " << now_ts
        << " WHERE id = " << id;

    return write_execute(sql.str());
}

std::optional<Suggestion> DuckDBStore::suggestion_get(int64_t id) {
    if (!db_ || id <= 0) return std::nullopt;

    std::string sql = "SELECT id, content, context, realm, status, helped, "
                      "outcome_details, memory_id, suggested_at, resolved_at "
                      "FROM suggestion WHERE id = " + std::to_string(id);

    auto result = read_query(sql);
    if (!result || result->HasError()) return std::nullopt;

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) return std::nullopt;

    Suggestion s;
    s.id = chunk->GetValue(0, 0).GetValue<int64_t>();
    s.content = chunk->GetValue(1, 0).ToString();
    s.context = chunk->GetValue(2, 0).ToString();
    s.realm = chunk->GetValue(3, 0).ToString();
    s.status = chunk->GetValue(4, 0).ToString();
    s.helped = chunk->GetValue(5, 0).GetValue<bool>();
    s.outcome_details = chunk->GetValue(6, 0).ToString();
    s.memory_id = chunk->GetValue(7, 0).GetValue<int64_t>();
    s.suggested_at = chunk->GetValue(8, 0).GetValue<int64_t>();
    s.resolved_at = chunk->GetValue(9, 0).GetValue<int64_t>();

    return s;
}

size_t DuckDBStore::suggestion_count_pending(const std::string& realm) {
    if (!db_) return 0;

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    std::string sql = "SELECT COUNT(*) FROM suggestion WHERE status = 'pending'";
    if (!realm.empty()) sql += " AND realm = '" + escape(realm) + "'";

    auto result = read_query(sql);
    if (!result || result->HasError()) return 0;

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) return 0;

    return static_cast<size_t>(chunk->GetValue(0, 0).GetValue<int64_t>());
}

// ============================================================================
// Memory consolidation (merge similar memories)
// ============================================================================

std::vector<DuckDBStore::ConsolidationCandidate> DuckDBStore::consolidation_scan(
    float similarity_threshold, size_t limit, const std::string& realm) {

    std::vector<ConsolidationCandidate> candidates;
    if (!db_ || !vss_loaded_) return candidates;

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    // Self-join to find similar memory pairs
    // Use cosine similarity via array_cosine_similarity
    std::ostringstream sql;
    sql << "WITH pairs AS ("
        << "  SELECT m1.id as id1, m2.id as id2, "
        << "    m1.content as content1, m2.content as content2, "
        << "    m1.confidence as conf1, m2.confidence as conf2, "
        << "    array_cosine_similarity(m1.embedding, m2.embedding) as sim "
        << "  FROM memory m1 "
        << "  JOIN memory m2 ON m1.id < m2.id "  // Only compare once
        << "  WHERE m1.embedding IS NOT NULL AND m2.embedding IS NOT NULL "
        << "    AND len(m1.embedding) > 0 AND len(m2.embedding) > 0 ";

    if (!realm.empty()) {
        sql << "    AND m1.realm = '" << escape(realm) << "' "
            << "    AND m2.realm = '" << escape(realm) << "' ";
    }

    sql << ") "
        << "SELECT id1, id2, content1, content2, sim "
        << "FROM pairs "
        << "WHERE sim >= " << similarity_threshold << " "
        << "ORDER BY sim DESC "
        << "LIMIT " << limit;

    auto result = read_query(sql.str());
    if (!result || result->HasError()) {
        last_error_ = result ? result->GetError() : "No result";
        return candidates;
    }

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); i++) {
            ConsolidationCandidate c;
            c.primary_id = chunk->GetValue(0, i).GetValue<int64_t>();
            c.secondary_id = chunk->GetValue(1, i).GetValue<int64_t>();
            c.primary_content = chunk->GetValue(2, i).ToString();
            c.secondary_content = chunk->GetValue(3, i).ToString();
            c.similarity = chunk->GetValue(4, i).GetValue<float>();
            candidates.push_back(c);
        }
    }

    return candidates;
}

bool DuckDBStore::consolidation_merge(int64_t primary_id, int64_t secondary_id, const std::string& merged_content) {
    if (!db_ || primary_id <= 0 || secondary_id <= 0) return false;

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    // Get secondary memory info for merging
    std::string sql = "SELECT content, confidence FROM memory WHERE id = " + std::to_string(secondary_id);
    auto result = read_query(sql);
    if (!result || result->HasError()) return false;

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) return false;

    std::string secondary_content = chunk->GetValue(0, 0).ToString();
    float secondary_conf = chunk->GetValue(1, 0).GetValue<float>();

    // Update primary if merged_content provided
    if (!merged_content.empty()) {
        update_content(primary_id, merged_content);
    }

    // Strengthen primary by absorbing secondary's confidence
    strengthen(primary_id, secondary_conf * 0.5f);

    // Create triplet recording the merge
    connect(std::to_string(primary_id), "absorbed", std::to_string(secondary_id));

    // Soft-delete secondary by reducing confidence to near-zero
    // It will be pruned later by the prune operation
    std::ostringstream update_sql;
    update_sql << "UPDATE memory SET confidence = 0.01, "
               << "content = '[MERGED into #" << primary_id << "] ' || content "
               << "WHERE id = " << secondary_id;
    write_execute(update_sql.str());

    return true;
}

size_t DuckDBStore::consolidation_auto(float similarity_threshold, size_t max_merges) {
    if (!db_ || !vss_loaded_) return 0;

    auto candidates = consolidation_scan(similarity_threshold, max_merges);
    size_t merged = 0;

    // Track which IDs have been involved in merges to avoid conflicts
    std::set<int64_t> merged_ids;

    for (const auto& c : candidates) {
        // Skip if either ID was already merged
        if (merged_ids.count(c.primary_id) || merged_ids.count(c.secondary_id)) {
            continue;
        }

        // Merge
        if (consolidation_merge(c.primary_id, c.secondary_id)) {
            merged_ids.insert(c.primary_id);
            merged_ids.insert(c.secondary_id);
            merged++;
        }

        if (merged >= max_merges) break;
    }

    return merged;
}

// ============================================================================
// Anticipation: context→action pattern learning
// ============================================================================

int64_t DuckDBStore::anticipation_observe(const std::string& context, const std::string& action,
                                           const std::string& realm) {
    if (!db_ || context.empty() || action.empty()) return 0;

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    // Try to update existing pattern first (using ON CONFLICT)
    std::ostringstream sql;
    sql << "INSERT INTO anticipation (id, context, action, frequency, success_count, last_triggered, realm, created_at) "
        << "VALUES (nextval('anticipation_seq'), '" << escape(context) << "', '" << escape(action) << "', "
        << "1, 0, " << now << ", '" << escape(realm) << "', " << now << ") "
        << "ON CONFLICT (context, action, realm) DO UPDATE SET "
        << "frequency = anticipation.frequency + 1, "
        << "last_triggered = " << now
        << " RETURNING id";

    auto result = write_query(sql.str());
    if (!result || result->HasError()) {
        last_error_ = result ? result->GetError() : "No result";
        return 0;
    }

    auto chunk = result->Fetch();
    if (chunk && chunk->size() > 0) {
        return chunk->GetValue(0, 0).GetValue<int64_t>();
    }
    return 0;
}

std::vector<AnticipationPattern> DuckDBStore::anticipation_predict(const std::string& context,
                                                                    size_t limit,
                                                                    const std::string& realm) {
    std::vector<AnticipationPattern> patterns;
    if (!db_ || context.empty()) return patterns;

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    // Find patterns with matching or similar context
    // Use LIKE for substring matching - could be improved with embeddings
    std::ostringstream sql;
    sql << "SELECT id, context, action, frequency, success_count, last_triggered, realm, created_at "
        << "FROM anticipation WHERE context LIKE '%" << escape(context) << "%'";

    if (!realm.empty()) {
        sql << " AND (realm = '" << escape(realm) << "' OR realm = 'brahman')";
    }

    sql << " ORDER BY frequency DESC, success_count DESC LIMIT " << limit;

    auto result = read_query(sql.str());
    if (!result || result->HasError()) {
        last_error_ = result ? result->GetError() : "No result";
        return patterns;
    }

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); i++) {
            AnticipationPattern p;
            p.id = chunk->GetValue(0, i).GetValue<int64_t>();
            p.context = chunk->GetValue(1, i).ToString();
            p.action = chunk->GetValue(2, i).ToString();
            p.frequency = chunk->GetValue(3, i).GetValue<int32_t>();
            p.success_count = chunk->GetValue(4, i).GetValue<int32_t>();
            p.last_triggered = chunk->GetValue(5, i).GetValue<int64_t>();
            p.realm = chunk->GetValue(6, i).ToString();
            p.created_at = chunk->GetValue(7, i).GetValue<int64_t>();
            patterns.push_back(p);
        }
    }

    return patterns;
}

bool DuckDBStore::anticipation_success(int64_t id) {
    if (!db_ || id <= 0) return false;

    std::ostringstream sql;
    sql << "UPDATE anticipation SET success_count = success_count + 1 WHERE id = " << id;
    return write_execute(sql.str());
}

std::vector<AnticipationPattern> DuckDBStore::anticipation_list(const std::string& realm, size_t limit) {
    std::vector<AnticipationPattern> patterns;
    if (!db_) return patterns;

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    std::ostringstream sql;
    sql << "SELECT id, context, action, frequency, success_count, last_triggered, realm, created_at "
        << "FROM anticipation";

    if (!realm.empty()) {
        sql << " WHERE realm = '" << escape(realm) << "'";
    }

    sql << " ORDER BY frequency DESC, last_triggered DESC LIMIT " << limit;

    auto result = read_query(sql.str());
    if (!result || result->HasError()) {
        last_error_ = result ? result->GetError() : "No result";
        return patterns;
    }

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); i++) {
            AnticipationPattern p;
            p.id = chunk->GetValue(0, i).GetValue<int64_t>();
            p.context = chunk->GetValue(1, i).ToString();
            p.action = chunk->GetValue(2, i).ToString();
            p.frequency = chunk->GetValue(3, i).GetValue<int32_t>();
            p.success_count = chunk->GetValue(4, i).GetValue<int32_t>();
            p.last_triggered = chunk->GetValue(5, i).GetValue<int64_t>();
            p.realm = chunk->GetValue(6, i).ToString();
            p.created_at = chunk->GetValue(7, i).GetValue<int64_t>();
            patterns.push_back(p);
        }
    }

    return patterns;
}

// ============================================================================
// Habit Formation: repeated patterns that strengthen with use
// ============================================================================

int64_t DuckDBStore::habit_observe(const std::string& trigger, const std::string& response,
                                    const std::string& realm) {
    if (!db_ || trigger.empty() || response.empty()) return 0;

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    // Insert or update with strengthening
    // Each observation increases strength by 0.1 (capped at 1.0)
    std::ostringstream sql;
    sql << "INSERT INTO habit (id, trigger_pattern, response, strength, frequency, last_activated, realm, created_at) "
        << "VALUES (nextval('habit_seq'), '" << escape(trigger) << "', '" << escape(response) << "', "
        << "0.1, 1, " << now << ", '" << escape(realm) << "', " << now << ") "
        << "ON CONFLICT (trigger_pattern, response, realm) DO UPDATE SET "
        << "strength = LEAST(1.0, habit.strength + 0.1), "
        << "frequency = habit.frequency + 1, "
        << "last_activated = " << now
        << " RETURNING id";

    auto result = write_query(sql.str());
    if (!result || result->HasError()) {
        last_error_ = result ? result->GetError() : "No result";
        return 0;
    }

    auto chunk = result->Fetch();
    if (chunk && chunk->size() > 0) {
        return chunk->GetValue(0, 0).GetValue<int64_t>();
    }
    return 0;
}

std::vector<Habit> DuckDBStore::habit_match(const std::string& context, float min_strength,
                                             const std::string& realm) {
    std::vector<Habit> habits;
    if (!db_ || context.empty()) return habits;

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    // Find habits where the trigger matches the context
    std::ostringstream sql;
    sql << "SELECT id, trigger_pattern, response, strength, frequency, last_activated, realm, created_at "
        << "FROM habit WHERE strength >= " << min_strength
        << " AND '" << escape(context) << "' LIKE '%' || trigger_pattern || '%'";

    if (!realm.empty()) {
        sql << " AND (realm = '" << escape(realm) << "' OR realm = 'brahman')";
    }

    sql << " ORDER BY strength DESC, frequency DESC";

    auto result = read_query(sql.str());
    if (!result || result->HasError()) {
        last_error_ = result ? result->GetError() : "No result";
        return habits;
    }

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); i++) {
            Habit h;
            h.id = chunk->GetValue(0, i).GetValue<int64_t>();
            h.trigger_pattern = chunk->GetValue(1, i).ToString();
            h.response = chunk->GetValue(2, i).ToString();
            h.strength = chunk->GetValue(3, i).GetValue<float>();
            h.frequency = chunk->GetValue(4, i).GetValue<int32_t>();
            h.last_activated = chunk->GetValue(5, i).GetValue<int64_t>();
            h.realm = chunk->GetValue(6, i).ToString();
            h.created_at = chunk->GetValue(7, i).GetValue<int64_t>();
            habits.push_back(h);
        }
    }

    return habits;
}

bool DuckDBStore::habit_strengthen(int64_t id, float amount) {
    if (!db_ || id <= 0) return false;

    std::ostringstream sql;
    sql << "UPDATE habit SET strength = LEAST(1.0, strength + " << amount << ") WHERE id = " << id;
    return write_execute(sql.str());
}

bool DuckDBStore::habit_weaken(int64_t id, float amount) {
    if (!db_ || id <= 0) return false;

    std::ostringstream sql;
    sql << "UPDATE habit SET strength = GREATEST(0.0, strength - " << amount << ") WHERE id = " << id;
    return write_execute(sql.str());
}

std::vector<Habit> DuckDBStore::habit_list(const std::string& realm, float min_strength, size_t limit) {
    std::vector<Habit> habits;
    if (!db_) return habits;

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    std::ostringstream sql;
    sql << "SELECT id, trigger_pattern, response, strength, frequency, last_activated, realm, created_at "
        << "FROM habit WHERE strength >= " << min_strength;

    if (!realm.empty()) {
        sql << " AND realm = '" << escape(realm) << "'";
    }

    sql << " ORDER BY strength DESC, frequency DESC LIMIT " << limit;

    auto result = read_query(sql.str());
    if (!result || result->HasError()) {
        last_error_ = result ? result->GetError() : "No result";
        return habits;
    }

    while (true) {
        auto chunk = result->Fetch();
        if (!chunk || chunk->size() == 0) break;

        for (size_t i = 0; i < chunk->size(); i++) {
            Habit h;
            h.id = chunk->GetValue(0, i).GetValue<int64_t>();
            h.trigger_pattern = chunk->GetValue(1, i).ToString();
            h.response = chunk->GetValue(2, i).ToString();
            h.strength = chunk->GetValue(3, i).GetValue<float>();
            h.frequency = chunk->GetValue(4, i).GetValue<int32_t>();
            h.last_activated = chunk->GetValue(5, i).GetValue<int64_t>();
            h.realm = chunk->GetValue(6, i).ToString();
            h.created_at = chunk->GetValue(7, i).GetValue<int64_t>();
            habits.push_back(h);
        }
    }

    return habits;
}

// ============================================================================
// Background Processing: daemon-level tasks
// ============================================================================

int64_t DuckDBStore::background_schedule(const std::string& task_type, const std::string& realm) {
    if (!db_ || task_type.empty()) return 0;

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    std::ostringstream sql;
    sql << "INSERT INTO background_task (id, task_type, status, scheduled_at, realm) "
        << "VALUES (nextval('background_seq'), '" << escape(task_type) << "', 'pending', "
        << now << ", '" << escape(realm) << "') RETURNING id";

    auto result = write_query(sql.str());
    if (!result || result->HasError()) {
        last_error_ = result ? result->GetError() : "No result";
        return 0;
    }

    auto chunk = result->Fetch();
    if (chunk && chunk->size() > 0) {
        return chunk->GetValue(0, 0).GetValue<int64_t>();
    }
    return 0;
}

std::optional<BackgroundTask> DuckDBStore::background_claim(const std::string& task_type) {
    if (!db_) return std::nullopt;

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    // Atomically claim the oldest pending task
    std::ostringstream sql;
    sql << "UPDATE background_task SET status = 'running', started_at = " << now
        << " WHERE id = (SELECT id FROM background_task WHERE status = 'pending'";

    if (!task_type.empty()) {
        sql << " AND task_type = '" << escape(task_type) << "'";
    }

    sql << " ORDER BY scheduled_at ASC LIMIT 1) RETURNING *";

    auto result = write_query(sql.str());
    if (!result || result->HasError()) {
        last_error_ = result ? result->GetError() : "No result";
        return std::nullopt;
    }

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) return std::nullopt;

    BackgroundTask t;
    t.id = chunk->GetValue(0, 0).GetValue<int64_t>();
    t.task_type = chunk->GetValue(1, 0).ToString();
    t.status = chunk->GetValue(2, 0).ToString();
    t.scheduled_at = chunk->GetValue(3, 0).GetValue<int64_t>();
    t.started_at = chunk->GetValue(4, 0).GetValue<int64_t>();
    t.completed_at = chunk->GetValue(5, 0).GetValue<int64_t>();
    t.result = chunk->GetValue(6, 0).IsNull() ? "" : chunk->GetValue(6, 0).ToString();
    t.error = chunk->GetValue(7, 0).IsNull() ? "" : chunk->GetValue(7, 0).ToString();
    t.realm = chunk->GetValue(8, 0).ToString();

    return t;
}

bool DuckDBStore::background_complete(int64_t id, const std::string& result) {
    if (!db_ || id <= 0) return false;

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    std::ostringstream sql;
    sql << "UPDATE background_task SET status = 'completed', completed_at = " << now
        << ", result = '" << escape(result) << "' WHERE id = " << id;

    return write_execute(sql.str());
}

bool DuckDBStore::background_fail(int64_t id, const std::string& error) {
    if (!db_ || id <= 0) return false;

    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    std::ostringstream sql;
    sql << "UPDATE background_task SET status = 'failed', completed_at = " << now
        << ", error = '" << escape(error) << "' WHERE id = " << id;

    return write_execute(sql.str());
}

DuckDBStore::BackgroundStatus DuckDBStore::background_status() {
    BackgroundStatus status;
    if (!db_) return status;

    // Get today's start timestamp
    auto now = std::chrono::system_clock::now();
    auto today_start = std::chrono::floor<std::chrono::days>(now);
    auto today_ts = std::chrono::duration_cast<std::chrono::seconds>(
        today_start.time_since_epoch()
    ).count();

    // Count by status
    auto result = read_query(
        "SELECT status, COUNT(*) FROM background_task "
        "WHERE status IN ('pending', 'running') OR completed_at >= " + std::to_string(today_ts) +
        " GROUP BY status"
    );

    if (result && !result->HasError()) {
        while (true) {
            auto chunk = result->Fetch();
            if (!chunk || chunk->size() == 0) break;

            for (size_t i = 0; i < chunk->size(); i++) {
                std::string s = chunk->GetValue(0, i).ToString();
                size_t count = chunk->GetValue(1, i).GetValue<int64_t>();

                if (s == "pending") status.pending = count;
                else if (s == "running") status.running = count;
                else if (s == "completed") status.completed_today = count;
                else if (s == "failed") status.failed_today = count;
            }
        }
    }

    return status;
}

size_t DuckDBStore::background_run_cycle() {
    if (!db_) return 0;

    size_t processed = 0;

    // Process one task of each type
    std::vector<std::string> task_types = {"consolidation", "decay", "pruning"};

    for (const auto& type : task_types) {
        auto task = background_claim(type);
        if (!task) continue;

        try {
            std::string result;

            if (type == "consolidation") {
                size_t merged = consolidation_auto(0.90f, 10);
                result = "{\"merged\": " + std::to_string(merged) + "}";
            } else if (type == "decay") {
                size_t decayed = apply_decay();
                result = "{\"decayed\": " + std::to_string(decayed) + "}";
            } else if (type == "pruning") {
                size_t pruned = prune(0.1f, 7.0f);
                result = "{\"pruned\": " + std::to_string(pruned) + "}";
            }

            background_complete(task->id, result);
            processed++;
        } catch (const std::exception& e) {
            background_fail(task->id, e.what());
        }
    }

    return processed;
}

// ============================================================================
// User Profile Methods
// ============================================================================

bool DuckDBStore::profile_update(const std::string& user_id, const std::string& field, const std::string& value) {
    if (!db_) return false;

    // Validate field name
    if (field != "expertise_json" && field != "style_json" &&
        field != "patterns_json" && field != "preferences_json") {
        last_error_ = "Invalid profile field: " + field;
        return false;
    }

    // Escape values
    std::string escaped_id = user_id;
    std::string escaped_val = value;
    size_t pos = 0;
    while ((pos = escaped_id.find('\'', pos)) != std::string::npos) {
        escaped_id.replace(pos, 1, "''");
        pos += 2;
    }
    pos = 0;
    while ((pos = escaped_val.find('\'', pos)) != std::string::npos) {
        escaped_val.replace(pos, 1, "''");
        pos += 2;
    }

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Upsert: insert or update
    std::ostringstream sql;
    sql << "INSERT INTO user_profile (user_id, " << field << ", updated_at) VALUES ('"
        << escaped_id << "', '" << escaped_val << "', " << now << ") "
        << "ON CONFLICT (user_id) DO UPDATE SET " << field << " = '" << escaped_val
        << "', updated_at = " << now;

    return write_execute(sql.str());
}

std::optional<UserProfile> DuckDBStore::profile_get(const std::string& user_id) {
    if (!db_) return std::nullopt;

    std::string escaped_id = user_id;
    size_t pos = 0;
    while ((pos = escaped_id.find('\'', pos)) != std::string::npos) {
        escaped_id.replace(pos, 1, "''");
        pos += 2;
    }

    std::string sql = "SELECT user_id, expertise_json, style_json, patterns_json, "
                      "preferences_json, updated_at FROM user_profile WHERE user_id = '" + escaped_id + "'";

    auto result = read_query(sql);
    if (!result) return std::nullopt;

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) return std::nullopt;

    UserProfile profile;
    profile.user_id = chunk->GetValue(0, 0).ToString();
    profile.expertise_json = chunk->GetValue(1, 0).ToString();
    profile.style_json = chunk->GetValue(2, 0).ToString();
    profile.patterns_json = chunk->GetValue(3, 0).ToString();
    profile.preferences_json = chunk->GetValue(4, 0).ToString();
    profile.updated_at = chunk->GetValue(5, 0).GetValue<int64_t>();

    return profile;
}

bool DuckDBStore::profile_observe(const std::string& observation_type, const std::string& value,
                                   const std::string& user_id) {
    if (!db_) return false;

    // Get current profile or create default
    auto profile = profile_get(user_id);

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (observation_type == "expertise") {
        // Parse value as "domain:level" (e.g., "python:0.8")
        auto colon = value.find(':');
        if (colon == std::string::npos) return false;

        std::string domain = value.substr(0, colon);
        float level = std::stof(value.substr(colon + 1));

        // Update expertise JSON array using proper JSON parsing
        std::string expertise_str = profile ? profile->expertise_json : "[]";

        nlohmann::json expertise_array;
        try {
            expertise_array = nlohmann::json::parse(expertise_str);
            if (!expertise_array.is_array()) {
                expertise_array = nlohmann::json::array();
            }
        } catch (...) {
            expertise_array = nlohmann::json::array();
        }

        // Search for existing domain entry
        bool found = false;
        for (auto& entry : expertise_array) {
            if (entry.is_object() && entry.contains("domain") && entry["domain"] == domain) {
                // Domain exists - update level to max of old and new
                float old_level = entry.value("level", 0.0f);
                entry["level"] = std::max(old_level, level);
                found = true;
                break;
            }
        }

        // If domain doesn't exist, append new entry
        if (!found) {
            nlohmann::json new_entry;
            new_entry["domain"] = domain;
            new_entry["level"] = level;
            expertise_array.push_back(new_entry);
        }

        // Serialize back to string
        std::string updated_expertise = expertise_array.dump();
        return profile_update(user_id, "expertise_json", updated_expertise);

    } else if (observation_type == "style") {
        // Value should be JSON object like {"tone":"direct"}
        return profile_update(user_id, "style_json", value);

    } else if (observation_type == "pattern") {
        // Value should be JSON object like {"active_hours":"9-17"}
        return profile_update(user_id, "patterns_json", value);

    } else if (observation_type == "preference") {
        // Value should be JSON object like {"no_emojis":true}
        return profile_update(user_id, "preferences_json", value);
    }

    last_error_ = "Unknown observation type: " + observation_type;
    return false;
}

// ============================================================================
// Goal Methods
// ============================================================================

int64_t DuckDBStore::goal_set(const std::string& title, const std::string& description,
                               const std::string& milestones_json, int64_t deadline,
                               const std::string& realm) {
    if (!db_) return -1;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Get next ID
    auto id_result = write_query("SELECT nextval('goal_seq')");
    if (!id_result) return -1;
    auto chunk = id_result->Fetch();
    if (!chunk || chunk->size() == 0) return -1;
    int64_t id = chunk->GetValue(0, 0).GetValue<int64_t>();

    // Escape strings
    auto escape = [](const std::string& s) {
        std::string escaped = s;
        size_t pos = 0;
        while ((pos = escaped.find('\'', pos)) != std::string::npos) {
            escaped.replace(pos, 1, "''");
            pos += 2;
        }
        return escaped;
    };

    std::ostringstream sql;
    sql << "INSERT INTO goal (id, title, description, milestones_json, status, progress, "
        << "deadline, realm, created_at, updated_at) VALUES ("
        << id << ", '" << escape(title) << "', '" << escape(description) << "', '"
        << escape(milestones_json) << "', 'active', 0.0, " << deadline << ", '"
        << escape(realm) << "', " << now << ", " << now << ")";

    if (!write_execute(sql.str())) return -1;
    return id;
}

std::optional<Goal> DuckDBStore::goal_get(int64_t id) {
    if (!db_) return std::nullopt;

    std::string sql = "SELECT id, title, description, milestones_json, status, progress, "
                      "deadline, outcome, realm, created_at, updated_at FROM goal WHERE id = " +
                      std::to_string(id);

    auto result = read_query(sql);
    if (!result) return std::nullopt;

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) return std::nullopt;

    Goal goal;
    goal.id = chunk->GetValue(0, 0).GetValue<int64_t>();
    goal.title = chunk->GetValue(1, 0).ToString();
    goal.description = chunk->GetValue(2, 0).ToString();
    goal.milestones_json = chunk->GetValue(3, 0).ToString();
    goal.status = chunk->GetValue(4, 0).ToString();
    goal.progress = chunk->GetValue(5, 0).GetValue<float>();
    goal.deadline = chunk->GetValue(6, 0).GetValue<int64_t>();
    goal.outcome = chunk->GetValue(7, 0).ToString();
    goal.realm = chunk->GetValue(8, 0).ToString();
    goal.created_at = chunk->GetValue(9, 0).GetValue<int64_t>();
    goal.updated_at = chunk->GetValue(10, 0).GetValue<int64_t>();

    return goal;
}

std::vector<Goal> DuckDBStore::goal_list(const std::string& status, const std::string& realm,
                                          size_t limit) {
    std::vector<Goal> goals;
    if (!db_) return goals;

    std::ostringstream sql;
    sql << "SELECT id, title, description, milestones_json, status, progress, "
        << "deadline, outcome, realm, created_at, updated_at FROM goal WHERE 1=1";

    if (!status.empty()) {
        sql << " AND status = '" << status << "'";
    }
    if (!realm.empty()) {
        sql << " AND realm = '" << realm << "'";
    }
    sql << " ORDER BY created_at DESC LIMIT " << limit;

    auto result = read_query(sql.str());
    if (!result) return goals;

    auto chunk = result->Fetch();
    while (chunk && chunk->size() > 0) {
        for (size_t i = 0; i < chunk->size(); i++) {
            Goal goal;
            goal.id = chunk->GetValue(0, i).GetValue<int64_t>();
            goal.title = chunk->GetValue(1, i).ToString();
            goal.description = chunk->GetValue(2, i).ToString();
            goal.milestones_json = chunk->GetValue(3, i).ToString();
            goal.status = chunk->GetValue(4, i).ToString();
            goal.progress = chunk->GetValue(5, i).GetValue<float>();
            goal.deadline = chunk->GetValue(6, i).GetValue<int64_t>();
            goal.outcome = chunk->GetValue(7, i).ToString();
            goal.realm = chunk->GetValue(8, i).ToString();
            goal.created_at = chunk->GetValue(9, i).GetValue<int64_t>();
            goal.updated_at = chunk->GetValue(10, i).GetValue<int64_t>();
            goals.push_back(goal);
        }
        chunk = result->Fetch();
    }

    return goals;
}

bool DuckDBStore::goal_progress(int64_t id, float progress, const std::string& milestone_completed) {
    if (!db_) return false;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Clamp progress to 0-1
    progress = std::max(0.0f, std::min(1.0f, progress));

    std::ostringstream sql;
    sql << "UPDATE goal SET progress = " << progress << ", updated_at = " << now
        << " WHERE id = " << id;

    if (!write_execute(sql.str())) return false;

    // If milestone specified, mark it as done in the JSON
    // This is a simple approach - for robustness, use JSON functions
    if (!milestone_completed.empty()) {
        auto goal = goal_get(id);
        if (goal) {
            std::string milestones = goal->milestones_json;
            // Simple string replacement: "name":"X","done":false -> "name":"X","done":true
            std::string search = "\"name\":\"" + milestone_completed + "\",\"done\":false";
            std::string replace = "\"name\":\"" + milestone_completed + "\",\"done\":true";
            size_t pos = milestones.find(search);
            if (pos != std::string::npos) {
                milestones.replace(pos, search.length(), replace);
                std::string escaped = milestones;
                size_t p = 0;
                while ((p = escaped.find('\'', p)) != std::string::npos) {
                    escaped.replace(p, 1, "''");
                    p += 2;
                }
                write_execute("UPDATE goal SET milestones_json = '" + escaped +
                             "', updated_at = " + std::to_string(now) + " WHERE id = " + std::to_string(id));
            }
        }
    }

    return true;
}

bool DuckDBStore::goal_complete(int64_t id, const std::string& outcome) {
    if (!db_) return false;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::string escaped = outcome;
    size_t pos = 0;
    while ((pos = escaped.find('\'', pos)) != std::string::npos) {
        escaped.replace(pos, 1, "''");
        pos += 2;
    }

    std::ostringstream sql;
    sql << "UPDATE goal SET status = 'completed', progress = 1.0, outcome = '"
        << escaped << "', updated_at = " << now << " WHERE id = " << id;

    return write_execute(sql.str());
}

bool DuckDBStore::goal_update_status(int64_t id, const std::string& status) {
    if (!db_) return false;

    // Validate status
    if (status != "active" && status != "paused" && status != "completed" && status != "abandoned") {
        last_error_ = "Invalid status: " + status;
        return false;
    }

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::ostringstream sql;
    sql << "UPDATE goal SET status = '" << status << "', updated_at = " << now
        << " WHERE id = " << id;

    return write_execute(sql.str());
}

// ============================================================================
// Calibration Methods
// ============================================================================

bool DuckDBStore::calibration_record(const std::string& domain, bool success) {
    if (!db_) return false;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::string escaped_domain = domain;
    size_t pos = 0;
    while ((pos = escaped_domain.find('\'', pos)) != std::string::npos) {
        escaped_domain.replace(pos, 1, "''");
        pos += 2;
    }

    // Upsert: insert or update
    std::ostringstream sql;
    if (success) {
        sql << "INSERT INTO calibration (domain, predictions, successes, failures, updated_at) "
            << "VALUES ('" << escaped_domain << "', 1, 1, 0, " << now << ") "
            << "ON CONFLICT (domain) DO UPDATE SET "
            << "predictions = calibration.predictions + 1, "
            << "successes = calibration.successes + 1, "
            << "updated_at = " << now;
    } else {
        sql << "INSERT INTO calibration (domain, predictions, successes, failures, updated_at) "
            << "VALUES ('" << escaped_domain << "', 1, 0, 1, " << now << ") "
            << "ON CONFLICT (domain) DO UPDATE SET "
            << "predictions = calibration.predictions + 1, "
            << "failures = calibration.failures + 1, "
            << "updated_at = " << now;
    }

    return write_execute(sql.str());
}

std::optional<CalibrationScore> DuckDBStore::calibration_get(const std::string& domain) {
    if (!db_) return std::nullopt;

    std::string escaped_domain = domain;
    size_t pos = 0;
    while ((pos = escaped_domain.find('\'', pos)) != std::string::npos) {
        escaped_domain.replace(pos, 1, "''");
        pos += 2;
    }

    std::string sql = "SELECT domain, predictions, successes, failures, updated_at "
                      "FROM calibration WHERE domain = '" + escaped_domain + "'";

    auto result = read_query(sql);
    if (!result) return std::nullopt;

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) return std::nullopt;

    CalibrationScore score;
    score.domain = chunk->GetValue(0, 0).ToString();
    score.predictions = chunk->GetValue(1, 0).GetValue<int32_t>();
    score.successes = chunk->GetValue(2, 0).GetValue<int32_t>();
    score.failures = chunk->GetValue(3, 0).GetValue<int32_t>();
    score.updated_at = chunk->GetValue(4, 0).GetValue<int64_t>();

    // Calculate accuracy and adjustment
    if (score.predictions > 0) {
        score.accuracy = (float)score.successes / score.predictions;
        // Adjustment: if accuracy < 0.5, reduce confidence; if > 0.7, increase
        if (score.accuracy < 0.5f) {
            score.confidence_adjustment = -0.2f * (0.5f - score.accuracy) / 0.5f;
        } else if (score.accuracy > 0.7f) {
            score.confidence_adjustment = 0.2f * (score.accuracy - 0.7f) / 0.3f;
        }
    }

    return score;
}

std::vector<CalibrationScore> DuckDBStore::calibration_all() {
    std::vector<CalibrationScore> scores;
    if (!db_) return scores;

    std::string sql = "SELECT domain, predictions, successes, failures, updated_at "
                      "FROM calibration ORDER BY predictions DESC";

    auto result = read_query(sql);
    if (!result) return scores;

    auto chunk = result->Fetch();
    while (chunk && chunk->size() > 0) {
        for (size_t i = 0; i < chunk->size(); i++) {
            CalibrationScore score;
            score.domain = chunk->GetValue(0, i).ToString();
            score.predictions = chunk->GetValue(1, i).GetValue<int32_t>();
            score.successes = chunk->GetValue(2, i).GetValue<int32_t>();
            score.failures = chunk->GetValue(3, i).GetValue<int32_t>();
            score.updated_at = chunk->GetValue(4, i).GetValue<int64_t>();

            if (score.predictions > 0) {
                score.accuracy = (float)score.successes / score.predictions;
                if (score.accuracy < 0.5f) {
                    score.confidence_adjustment = -0.2f * (0.5f - score.accuracy) / 0.5f;
                } else if (score.accuracy > 0.7f) {
                    score.confidence_adjustment = 0.2f * (score.accuracy - 0.7f) / 0.3f;
                }
            }

            scores.push_back(score);
        }
        chunk = result->Fetch();
    }

    return scores;
}

float DuckDBStore::calibration_adjustment(const std::string& domain) {
    auto score = calibration_get(domain);
    if (!score) return 0.0f;
    return score->confidence_adjustment;
}

// ============================================================================
// Hygiene Methods
// ============================================================================

HygieneStats DuckDBStore::hygiene_stats() {
    HygieneStats stats;
    if (!db_) return stats;

    // Get confidence distribution
    auto result = read_query(
        "SELECT "
        "  COUNT(*) as total, "
        "  SUM(CASE WHEN confidence < 0.3 THEN 1 ELSE 0 END) as low, "
        "  SUM(CASE WHEN confidence >= 0.3 AND confidence <= 0.7 THEN 1 ELSE 0 END) as medium, "
        "  SUM(CASE WHEN confidence > 0.7 THEN 1 ELSE 0 END) as high, "
        "  AVG(confidence) as avg_conf "
        "FROM memory"
    );

    if (result) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            auto total_val = chunk->GetValue(0, 0);
            if (!total_val.IsNull()) {
                stats.total_memories = total_val.GetValue<int64_t>();
            }
            auto low_val = chunk->GetValue(1, 0);
            if (!low_val.IsNull()) {
                stats.low_confidence = low_val.GetValue<int64_t>();
            }
            auto med_val = chunk->GetValue(2, 0);
            if (!med_val.IsNull()) {
                stats.medium_confidence = med_val.GetValue<int64_t>();
            }
            auto high_val = chunk->GetValue(3, 0);
            if (!high_val.IsNull()) {
                stats.high_confidence = high_val.GetValue<int64_t>();
            }
            auto avg_val = chunk->GetValue(4, 0);
            if (!avg_val.IsNull()) {
                stats.avg_confidence = avg_val.GetValue<float>();
            }
        }
    }

    // Get old unaccessed memories (30+ days)
    Timestamp current = now();
    Timestamp thirty_days_ago = current - (30LL * 24 * 60 * 60 * 1000);
    std::ostringstream sql;
    sql << "SELECT COUNT(*) FROM memory WHERE accessed_at < " << thirty_days_ago;
    result = read_query(sql.str());
    if (result) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            stats.old_unaccessed = chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }

    // Get growth rate (memories created in last 7 days / 7)
    Timestamp seven_days_ago = current - (7LL * 24 * 60 * 60 * 1000);
    sql.str("");
    sql << "SELECT COUNT(*) FROM memory WHERE created_at > " << seven_days_ago;
    result = read_query(sql.str());
    if (result) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            size_t recent = chunk->GetValue(0, 0).GetValue<int64_t>();
            stats.growth_rate_per_day = (float)recent / 7.0f;
        }
    }

    // Get consolidation candidates (approximate - would need embedding comparison)
    // For now, use a simple heuristic: count memories with very similar lengths in same realm
    stats.consolidation_candidates = 0;  // TODO: implement proper similarity check

    return stats;
}

DuckDBStore::HygieneResult DuckDBStore::hygiene_run(float prune_threshold, float min_age_days,
                                                     float consolidation_threshold, size_t max_consolidations) {
    HygieneResult result;
    if (!db_) return result;

    // 1. Apply decay
    result.decayed = apply_decay();

    // 2. Prune low-confidence old memories
    result.pruned = prune(prune_threshold, min_age_days);

    // 3. Auto-consolidate similar memories
    result.consolidated = consolidation_auto(consolidation_threshold, max_consolidations);

    return result;
}

DuckDBStore::CodeIntelRestoreResult DuckDBStore::restore_code_intel_confidence(float target_confidence, bool dry_run) {
    CodeIntelRestoreResult result;
    if (!db_) return result;

    // Code intel kinds that should never decay
    std::string protected_kinds = "'symbol', 'projectessence', 'modulestate', 'patternstate'";

    // Get stats before update
    std::ostringstream stats_sql;
    stats_sql << "SELECT kind, COUNT(*) as cnt, AVG(confidence) as avg_conf "
              << "FROM memory WHERE kind IN (" << protected_kinds << ") "
              << "GROUP BY kind";

    auto stats_result = read_query(stats_sql.str());
    if (stats_result && !stats_result->HasError()) {
        auto chunk = stats_result->Fetch();
        while (chunk && chunk->size() > 0) {
            for (size_t i = 0; i < chunk->size(); ++i) {
                std::string kind = chunk->GetValue(0, i).GetValue<std::string>();
                int64_t count = chunk->GetValue(1, i).GetValue<int64_t>();
                float avg = chunk->GetValue(2, i).GetValue<float>();
                result.counts_by_kind.push_back({kind, static_cast<size_t>(count)});
                result.avg_confidence_before.push_back({kind, avg});
            }
            chunk = stats_result->Fetch();
        }
    }

    if (!dry_run) {
        // Update confidence and decay_rate for code intel memories
        std::ostringstream update_sql;
        update_sql << "UPDATE memory SET confidence = " << target_confidence
                   << ", decay_rate = 0.0 "
                   << "WHERE kind IN (" << protected_kinds << ")";
        write_execute(update_sql.str());

        // Count total updated
        std::ostringstream count_sql;
        count_sql << "SELECT COUNT(*) FROM memory WHERE kind IN (" << protected_kinds << ")";
        auto count_result = read_query(count_sql.str());
        if (count_result && !count_result->HasError()) {
            auto chunk = count_result->Fetch();
            if (chunk && chunk->size() > 0) {
                result.total_updated = static_cast<size_t>(chunk->GetValue(0, 0).GetValue<int64_t>());
            }
        }
    }

    return result;
}

DuckDBStore::SqlQueryResult DuckDBStore::execute_sql_query(const std::string& sql) const {
    SqlQueryResult result;
    if (!db_) {
        result.error = "Database not initialized";
        return result;
    }

    auto query_result = read_query(sql);
    if (!query_result || query_result->HasError()) {
        result.error = query_result ? query_result->GetError() : "Query failed";
        return result;
    }

    // Get column names
    for (const auto& name : query_result->names) {
        result.columns.push_back(name);
    }

    // Get rows
    auto chunk = query_result->Fetch();
    while (chunk && chunk->size() > 0) {
        for (size_t row = 0; row < chunk->size(); ++row) {
            std::vector<std::string> row_data;
            for (size_t col = 0; col < chunk->ColumnCount(); ++col) {
                auto val = chunk->GetValue(col, row);
                row_data.push_back(val.IsNull() ? "NULL" : val.ToString());
            }
            result.rows.push_back(std::move(row_data));
        }
        chunk = query_result->Fetch();
    }

    result.success = true;
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Usage Outcome Tracking
// ═══════════════════════════════════════════════════════════════════════════

int64_t DuckDBStore::record_usage_outcome(int64_t memory_id, const std::string& session_id,
                                           const std::string& outcome, const std::string& context) {
    if (!db_) return -1;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Escape strings
    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    std::ostringstream sql;
    sql << "INSERT INTO usage_outcomes (id, memory_id, session_id, outcome, context, created_at) "
        << "VALUES (nextval('usage_outcomes_seq'), " << memory_id
        << ", '" << escape(session_id) << "'"
        << ", '" << escape(outcome) << "'"
        << ", '" << escape(context) << "'"
        << ", " << now << ") RETURNING id";

    auto result = write_query(sql.str());
    if (!result || result->HasError()) {
        return -1;
    }

    auto chunk = result->Fetch();
    if (chunk && chunk->size() > 0) {
        return chunk->GetValue(0, 0).GetValue<int64_t>();
    }
    return -1;
}

std::vector<UsageOutcome> DuckDBStore::get_usage_outcomes(int64_t memory_id, size_t limit) {
    std::vector<UsageOutcome> outcomes;
    if (!db_) return outcomes;

    std::ostringstream sql;
    sql << "SELECT id, memory_id, session_id, outcome, context, created_at "
        << "FROM usage_outcomes WHERE memory_id = " << memory_id
        << " ORDER BY created_at DESC LIMIT " << limit;

    auto result = read_query(sql.str());
    if (!result || result->HasError()) return outcomes;

    auto chunk = result->Fetch();
    while (chunk && chunk->size() > 0) {
        for (size_t i = 0; i < chunk->size(); ++i) {
            UsageOutcome o;
            o.id = chunk->GetValue(0, i).GetValue<int64_t>();
            o.memory_id = chunk->GetValue(1, i).GetValue<int64_t>();
            auto session_val = chunk->GetValue(2, i);
            o.session_id = session_val.IsNull() ? "" : session_val.GetValue<std::string>();
            o.outcome = chunk->GetValue(3, i).GetValue<std::string>();
            auto ctx_val = chunk->GetValue(4, i);
            o.context = ctx_val.IsNull() ? "" : ctx_val.GetValue<std::string>();
            o.created_at = chunk->GetValue(5, i).GetValue<int64_t>();
            outcomes.push_back(std::move(o));
        }
        chunk = result->Fetch();
    }
    return outcomes;
}

DuckDBStore::UsageStats DuckDBStore::get_usage_stats(int64_t memory_id) {
    UsageStats stats;
    if (!db_) return stats;

    std::ostringstream sql;
    sql << "SELECT outcome, COUNT(*) as cnt FROM usage_outcomes "
        << "WHERE memory_id = " << memory_id << " GROUP BY outcome";

    auto result = read_query(sql.str());
    if (!result || result->HasError()) return stats;

    auto chunk = result->Fetch();
    while (chunk && chunk->size() > 0) {
        for (size_t i = 0; i < chunk->size(); ++i) {
            std::string outcome = chunk->GetValue(0, i).GetValue<std::string>();
            int64_t count = chunk->GetValue(1, i).GetValue<int64_t>();
            if (outcome == "positive") stats.positive = count;
            else if (outcome == "negative") stats.negative = count;
            else if (outcome == "neutral") stats.neutral = count;
        }
        chunk = result->Fetch();
    }

    int64_t total = stats.positive + stats.negative + stats.neutral;
    if (total > 0) {
        stats.positive_rate = static_cast<float>(stats.positive) / total;
    }
    return stats;
}

// ═══════════════════════════════════════════════════════════════════════════
// Episode Pattern Detection for Auto-Distillation
// ═══════════════════════════════════════════════════════════════════════════

std::vector<DistillCandidate> DuckDBStore::find_distill_candidates(
    float similarity_threshold, size_t min_occurrences, size_t limit) {

    std::vector<DistillCandidate> candidates;
    if (!db_ || !emb_conn_) return candidates;

    // Step 1: Get all episode memories with embeddings
    auto episodes_result = read_query(
        "SELECT id, content, confidence FROM memory WHERE kind = 'episode' ORDER BY id");
    if (!episodes_result || episodes_result->HasError()) return candidates;

    struct EpisodeInfo {
        int64_t id;
        std::string content;
        float confidence;
    };
    std::vector<EpisodeInfo> episodes;

    auto chunk = episodes_result->Fetch();
    while (chunk && chunk->size() > 0) {
        for (size_t i = 0; i < chunk->size(); ++i) {
            EpisodeInfo ep;
            ep.id = chunk->GetValue(0, i).GetValue<int64_t>();
            ep.content = chunk->GetValue(1, i).GetValue<std::string>();
            ep.confidence = chunk->GetValue(2, i).GetValue<float>();
            episodes.push_back(std::move(ep));
        }
        chunk = episodes_result->Fetch();
    }

    if (episodes.size() < min_occurrences) return candidates;

    // Step 2: Find clusters using vector similarity in embeddings DB
    // For each episode, find similar episodes
    std::vector<bool> clustered(episodes.size(), false);

    for (size_t i = 0; i < episodes.size() && candidates.size() < limit; ++i) {
        if (clustered[i]) continue;

        // Get embedding for this episode
        std::ostringstream emb_sql;
        emb_sql << "SELECT embedding FROM memory_embeddings WHERE memory_id = " << episodes[i].id;

        std::vector<float> anchor_emb;
        {
            std::lock_guard<std::mutex> lock(emb_mutex_);
            auto emb_result = emb_conn_->Query(emb_sql.str());
            if (emb_result && !emb_result->HasError()) {
                auto emb_chunk = emb_result->Fetch();
                if (emb_chunk && emb_chunk->size() > 0) {
                    auto arr = emb_chunk->GetValue(0, 0);
                    if (!arr.IsNull()) {
                        auto& list_val = duckdb::ListValue::GetChildren(arr);
                        anchor_emb.reserve(list_val.size());
                        for (const auto& v : list_val) {
                            anchor_emb.push_back(v.GetValue<float>());
                        }
                    }
                }
            }
        }

        if (anchor_emb.empty()) continue;

        // Find similar episodes using HNSW
        std::vector<int64_t> cluster_ids;
        cluster_ids.push_back(episodes[i].id);
        float total_confidence = episodes[i].confidence;
        float total_similarity = 1.0f;

        // Build embedding literal for query
        std::ostringstream emb_literal;
        emb_literal << "[";
        for (size_t k = 0; k < anchor_emb.size(); ++k) {
            if (k > 0) emb_literal << ",";
            emb_literal << anchor_emb[k];
        }
        emb_literal << "]::FLOAT[384]";

        std::ostringstream sim_sql;
        sim_sql << "SELECT memory_id, array_cosine_similarity(embedding, "
                << emb_literal.str() << ") as sim "
                << "FROM memory_embeddings "
                << "WHERE memory_id != " << episodes[i].id
                << " ORDER BY sim DESC LIMIT 50";

        {
            std::lock_guard<std::mutex> lock(emb_mutex_);
            auto sim_result = emb_conn_->Query(sim_sql.str());
            if (sim_result && !sim_result->HasError()) {
                auto sim_chunk = sim_result->Fetch();
                while (sim_chunk && sim_chunk->size() > 0) {
                    for (size_t j = 0; j < sim_chunk->size(); ++j) {
                        int64_t mem_id = sim_chunk->GetValue(0, j).GetValue<int64_t>();
                        float sim = sim_chunk->GetValue(1, j).GetValue<float>();

                        if (sim < similarity_threshold) break;

                        // Check if this memory is an episode
                        for (size_t k = 0; k < episodes.size(); ++k) {
                            if (episodes[k].id == mem_id && !clustered[k]) {
                                cluster_ids.push_back(mem_id);
                                total_confidence += episodes[k].confidence;
                                total_similarity += sim;
                                clustered[k] = true;
                            }
                        }
                    }
                    sim_chunk = sim_result->Fetch();
                }
            }
        }

        // Only keep clusters with enough members
        if (cluster_ids.size() >= min_occurrences) {
            clustered[i] = true;

            DistillCandidate candidate;
            candidate.episode_ids = std::move(cluster_ids);
            candidate.avg_confidence = total_confidence / candidate.episode_ids.size();
            candidate.avg_similarity = total_similarity / candidate.episode_ids.size();

            // Extract common pattern from content (simplified: use first episode as basis)
            candidate.pattern_content = episodes[i].content;

            candidates.push_back(std::move(candidate));
        }
    }

    return candidates;
}

// ═══════════════════════════════════════════════════════════════════════════
// Provenance Tracking
// ═══════════════════════════════════════════════════════════════════════════

bool DuckDBStore::set_provenance(int64_t memory_id, const std::string& session_id,
                                  const std::string& tool_name, float trust_score,
                                  int64_t derived_from) {
    if (!db_) return false;

    auto escape = [](const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };

    std::ostringstream sql;
    sql << "INSERT INTO provenance (node_id, session_id, tool_name, trust_score, derived_from) "
        << "VALUES (" << memory_id
        << ", '" << escape(session_id) << "'"
        << ", '" << escape(tool_name) << "'"
        << ", " << trust_score
        << ", " << derived_from << ") "
        << "ON CONFLICT (node_id) DO UPDATE SET "
        << "session_id = EXCLUDED.session_id, "
        << "tool_name = EXCLUDED.tool_name, "
        << "trust_score = EXCLUDED.trust_score, "
        << "derived_from = EXCLUDED.derived_from";

    return write_execute(sql.str());
}

bool DuckDBStore::get_provenance(int64_t memory_id, std::string& session_id, std::string& tool_name,
                                  float& trust_score, int64_t& derived_from) {
    if (!db_) return false;

    std::ostringstream sql;
    sql << "SELECT session_id, tool_name, trust_score, derived_from "
        << "FROM provenance WHERE node_id = " << memory_id;

    auto result = read_query(sql.str());
    if (!result || result->HasError()) return false;

    auto chunk = result->Fetch();
    if (chunk && chunk->size() > 0) {
        auto sess_val = chunk->GetValue(0, 0);
        session_id = sess_val.IsNull() ? "" : sess_val.GetValue<std::string>();
        auto tool_val = chunk->GetValue(1, 0);
        tool_name = tool_val.IsNull() ? "" : tool_val.GetValue<std::string>();
        trust_score = chunk->GetValue(2, 0).GetValue<float>();
        derived_from = chunk->GetValue(3, 0).GetValue<int64_t>();
        return true;
    }
    return false;
}

std::vector<MemoryResult> DuckDBStore::recall_with_provenance(
    const std::vector<float>& query_embedding,
    size_t k,
    const std::string& realm,
    bool include_global,
    const std::vector<std::string>& exclude_kinds) {

    // First get base recall results
    auto results = recall(query_embedding, k, realm, include_global, exclude_kinds);

    // Then enrich with provenance data
    for (auto& r : results) {
        std::string session_id, tool_name;
        float trust_score;
        int64_t derived_from;

        if (get_provenance(r.id, session_id, tool_name, trust_score, derived_from)) {
            r.source_session = session_id;
            r.source_tool = tool_name;
            r.trust_score = trust_score;
            if (derived_from > 0) {
                r.derived_from = derived_from;
            }
        }
    }

    return results;
}

}  // namespace chitta
