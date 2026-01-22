#include "chitta/duckdb_store.hpp"
#include <iostream>
#include <sstream>
#include <cmath>
#include <chrono>

namespace chitta {

DuckDBStore::~DuckDBStore() {
    close();
}

bool DuckDBStore::open(const std::string& path) {
    std::lock_guard lock(mutex_);

    if (db_) {
        std::cerr << "[DuckDBStore] Already open\n";
        return false;
    }

    try {
        path_ = path;

        // Create database with WAL enabled
        duckdb::DBConfig config;
        config.SetOption("enable_external_access", duckdb::Value(true));

        db_ = std::make_unique<duckdb::DuckDB>(path_, &config);
        conn_ = std::make_unique<duckdb::Connection>(*db_);

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

        std::cerr << "[DuckDBStore] Opened database at " << path_ << "\n";
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[DuckDBStore] Failed to open: " << e.what() << "\n";
        db_.reset();
        conn_.reset();
        return false;
    }
}

void DuckDBStore::close() {
    std::lock_guard lock(mutex_);
    conn_.reset();
    db_.reset();
    vss_loaded_ = false;
    pgq_loaded_ = false;
}

bool DuckDBStore::load_extensions() {
    // Try to load VSS extension for vector search
    try {
        conn_->Query("INSTALL vss; LOAD vss;");
        vss_loaded_ = true;
        std::cerr << "[DuckDBStore] VSS extension loaded\n";
    } catch (...) {
        std::cerr << "[DuckDBStore] VSS extension not available (will use brute force)\n";
    }

    // Try to load DuckPGQ for graph queries
    try {
        conn_->Query("INSTALL duckpgq FROM community; LOAD duckpgq;");
        pgq_loaded_ = true;
        std::cerr << "[DuckDBStore] DuckPGQ extension loaded\n";
    } catch (...) {
        std::cerr << "[DuckDBStore] DuckPGQ not available (will use SQL joins)\n";
    }

    // Try to load FTS extension for full-text search with BM25
    try {
        conn_->Query("INSTALL fts; LOAD fts;");
        fts_loaded_ = true;
        std::cerr << "[DuckDBStore] FTS extension loaded\n";
    } catch (...) {
        std::cerr << "[DuckDBStore] FTS not available (will use LIKE)\n";
    }

    return true;
}

bool DuckDBStore::create_schema() {
    // Memory table with ARRAY for embeddings
    if (!execute(R"(
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
    if (!execute(R"(
        CREATE TABLE IF NOT EXISTS realm_membership (
            memory_id BIGINT,
            realm VARCHAR,
            PRIMARY KEY (memory_id, realm)
        )
    )")) {
        return false;
    }

    // Index for realm filtering
    execute("CREATE INDEX IF NOT EXISTS idx_memory_realm ON memory(realm)");
    execute("CREATE INDEX IF NOT EXISTS idx_realm_membership ON realm_membership(realm)");

    // Triplet table for graph relationships
    if (!execute(R"(
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
    execute("ALTER TABLE triplet ADD COLUMN IF NOT EXISTS source_file VARCHAR DEFAULT ''");

    // Indexes for triplet queries
    execute("CREATE INDEX IF NOT EXISTS idx_triplet_subject ON triplet(subject)");
    execute("CREATE INDEX IF NOT EXISTS idx_triplet_object ON triplet(object)");
    execute("CREATE INDEX IF NOT EXISTS idx_triplet_predicate ON triplet(predicate)");
    execute("CREATE INDEX IF NOT EXISTS idx_triplet_source_file ON triplet(source_file)");

    // Symbol table for code intelligence
    if (!execute(R"(
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

    // Call graph table
    if (!execute(R"(
        CREATE TABLE IF NOT EXISTS call_edge (
            caller_id BIGINT,
            callee_id BIGINT,
            PRIMARY KEY (caller_id, callee_id)
        )
    )")) {
        return false;
    }

    // Index for symbol lookup
    execute("CREATE INDEX IF NOT EXISTS idx_symbol_name ON symbol(name)");
    execute("CREATE INDEX IF NOT EXISTS idx_symbol_kind ON symbol(kind)");

    // FTS index for BM25 search on symbols (if FTS extension loaded)
    if (fts_loaded_) {
        try {
            // Create FTS index on symbol name and signature for keyword search
            execute("PRAGMA create_fts_index('symbol', 'id', 'name', 'signature', 'file_path', overwrite=1)");
            std::cerr << "[DuckDBStore] Created FTS index on symbol table\n";
        } catch (...) {
            std::cerr << "[DuckDBStore] FTS index creation failed\n";
        }
    }

    // Ledger table for session continuity
    if (!execute(R"(
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
    execute("CREATE INDEX IF NOT EXISTS idx_ledger_session ON ledger(session_id)");
    execute("CREATE INDEX IF NOT EXISTS idx_ledger_project ON ledger(project)");
    execute("CREATE INDEX IF NOT EXISTS idx_ledger_created ON ledger(created_at DESC)");

    // Code files table for incremental indexing
    if (!execute(R"(
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
    execute("CREATE INDEX IF NOT EXISTS idx_code_file_project ON code_file(project)");
    execute("CREATE INDEX IF NOT EXISTS idx_code_file_mtime ON code_file(mtime)");

    // Transcript state table for distillation (reads JSONL directly)
    if (!execute(R"(
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

    // Indexes for transcript queries
    execute("CREATE INDEX IF NOT EXISTS idx_transcript_realm ON transcript_state(realm)");

    // Sequence for IDs
    execute("CREATE SEQUENCE IF NOT EXISTS memory_seq START 1");
    execute("CREATE SEQUENCE IF NOT EXISTS triplet_seq START 1");
    execute("CREATE SEQUENCE IF NOT EXISTS symbol_seq START 1");
    execute("CREATE SEQUENCE IF NOT EXISTS ledger_seq START 1");

    return true;
}

bool DuckDBStore::create_vector_index() {
    if (!vss_loaded_) return false;

    // Enable experimental persistence for HNSW
    execute("SET hnsw_enable_experimental_persistence = true");

    // Create HNSW index on memory embeddings
    try {
        execute(R"(
            CREATE INDEX IF NOT EXISTS memory_embedding_idx
            ON memory USING HNSW (embedding)
            WITH (metric = 'cosine')
        )");
        std::cerr << "[DuckDBStore] Created HNSW index on memory.embedding\n";
        return true;
    } catch (...) {
        std::cerr << "[DuckDBStore] Failed to create HNSW index\n";
        return false;
    }
}

bool DuckDBStore::execute(const std::string& sql) {
    try {
        auto result = conn_->Query(sql);
        if (result->HasError()) {
            std::cerr << "[DuckDBStore] Query error: " << result->GetError() << "\n";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[DuckDBStore] Exception: " << e.what() << "\n";
        return false;
    }
}

std::unique_ptr<duckdb::QueryResult> DuckDBStore::query(const std::string& sql) {
    try {
        return conn_->Query(sql);
    } catch (const std::exception& e) {
        std::cerr << "[DuckDBStore] Query exception: " << e.what() << "\n";
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
    std::lock_guard lock(mutex_);
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

    auto result = query(sql.str());
    if (!result || result->HasError()) {
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
        execute(membership_sql.str());
    }

    return id;
}

std::vector<MemoryResult> DuckDBStore::recall(
    const std::vector<float>& query_embedding,
    size_t k,
    const std::string& realm,
    bool include_global
) {
    std::lock_guard lock(mutex_);
    std::vector<MemoryResult> results;
    if (!db_) return results;

    // Escape realm for SQL
    std::string escaped_realm;
    for (char c : realm) {
        if (c == '\'') escaped_realm += "''";
        else escaped_realm += c;
    }

    // Build WHERE clause for realm filtering
    std::ostringstream where_clause;
    if (!realm.empty()) {
        // Filter by realm: primary realm OR shared via membership OR global
        where_clause << "WHERE (m.realm = '" << escaped_realm << "' ";
        where_clause << "OR m.id IN (SELECT memory_id FROM realm_membership WHERE realm = '" << escaped_realm << "') ";
        if (include_global) {
            where_clause << "OR m.visibility = 2 ";  // Global visibility
        }
        where_clause << ") ";
    }

    std::ostringstream sql;
    sql << "SELECT m.id, m.kind, m.content, m.confidence, m.created_at, m.accessed_at, "
        << "m.realm, m.visibility, "
        << "array_cosine_similarity(m.embedding, " << embedding_to_sql(query_embedding) << ") AS similarity "
        << "FROM memory m "
        << where_clause.str();

    if (vss_loaded_) {
        sql << "ORDER BY array_distance(m.embedding, " << embedding_to_sql(query_embedding) << ") ";
    } else {
        sql << "ORDER BY similarity DESC ";
    }
    sql << "LIMIT " << k;

    auto result = query(sql.str());
    if (!result || result->HasError()) {
        return results;
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
            r.similarity = chunk->GetValue(8, i).GetValue<float>();
            results.push_back(r);
        }
    }

    // Load shared realms for each result
    for (auto& r : results) {
        std::ostringstream membership_sql;
        membership_sql << "SELECT realm FROM realm_membership WHERE memory_id = " << r.id;
        auto membership_result = query(membership_sql.str());
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
    std::lock_guard lock(mutex_);
    if (!db_) return false;

    std::ostringstream sql;
    sql << "UPDATE memory SET confidence = LEAST(confidence + " << amount << ", 1.0), "
        << "accessed_at = " << now() << " WHERE id = " << id;

    return execute(sql.str());
}

bool DuckDBStore::weaken(int64_t id, float amount) {
    std::lock_guard lock(mutex_);
    if (!db_) return false;

    std::ostringstream sql;
    sql << "UPDATE memory SET confidence = GREATEST(confidence - " << amount << ", 0.0) "
        << "WHERE id = " << id;

    return execute(sql.str());
}

bool DuckDBStore::forget(int64_t id) {
    std::lock_guard lock(mutex_);
    if (!db_) return false;

    std::ostringstream sql;
    sql << "DELETE FROM memory WHERE id = " << id;

    return execute(sql.str());
}

bool DuckDBStore::touch(int64_t id) {
    std::lock_guard lock(mutex_);
    if (!db_) return false;

    std::ostringstream sql;
    sql << "UPDATE memory SET accessed_at = " << now() << " WHERE id = " << id;

    return execute(sql.str());
}

std::optional<MemoryResult> DuckDBStore::get_memory(int64_t id) {
    std::lock_guard lock(mutex_);
    if (!db_) return std::nullopt;

    std::ostringstream sql;
    sql << "SELECT id, kind, content, confidence, created_at, accessed_at, realm, visibility "
        << "FROM memory WHERE id = " << id;

    auto result = query(sql.str());
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
    auto membership_result = query(membership_sql.str());
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
    std::lock_guard lock(mutex_);
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

    return execute(sql.str());
}

bool DuckDBStore::add_tag(int64_t id, const std::string& tag) {
    std::lock_guard lock(mutex_);
    if (!db_) return false;

    // Check if tags table exists, create if not
    execute("CREATE TABLE IF NOT EXISTS memory_tags ("
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
    sql << "INSERT OR IGNORE INTO memory_tags (memory_id, tag) VALUES ("
        << id << ", '" << escaped << "')";

    return execute(sql.str());
}

bool DuckDBStore::remove_tag(int64_t id, const std::string& tag) {
    std::lock_guard lock(mutex_);
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

    return execute(sql.str());
}

std::vector<std::string> DuckDBStore::get_tags(int64_t id) {
    std::lock_guard lock(mutex_);
    std::vector<std::string> tags;
    if (!db_) return tags;

    std::ostringstream sql;
    sql << "SELECT tag FROM memory_tags WHERE memory_id = " << id;

    auto result = query(sql.str());
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
    std::lock_guard lock(mutex_);
    if (!db_) return false;

    std::string escaped;
    for (char c : realm) {
        if (c == '\'') escaped += "''";
        else escaped += c;
    }

    std::ostringstream sql;
    sql << "UPDATE memory SET realm = '" << escaped << "' WHERE id = " << id;
    return execute(sql.str());
}

bool DuckDBStore::set_visibility(int64_t id, RealmVisibility visibility) {
    std::lock_guard lock(mutex_);
    if (!db_) return false;

    std::ostringstream sql;
    sql << "UPDATE memory SET visibility = " << static_cast<int>(visibility)
        << " WHERE id = " << id;
    return execute(sql.str());
}

bool DuckDBStore::add_to_realm(int64_t id, const std::string& realm) {
    std::lock_guard lock(mutex_);
    if (!db_) return false;

    std::string escaped;
    for (char c : realm) {
        if (c == '\'') escaped += "''";
        else escaped += c;
    }

    std::ostringstream sql;
    sql << "INSERT OR IGNORE INTO realm_membership (memory_id, realm) VALUES ("
        << id << ", '" << escaped << "')";
    return execute(sql.str());
}

bool DuckDBStore::remove_from_realm(int64_t id, const std::string& realm) {
    std::lock_guard lock(mutex_);
    if (!db_) return false;

    std::string escaped;
    for (char c : realm) {
        if (c == '\'') escaped += "''";
        else escaped += c;
    }

    std::ostringstream sql;
    sql << "DELETE FROM realm_membership WHERE memory_id = " << id
        << " AND realm = '" << escaped << "'";
    return execute(sql.str());
}

std::vector<std::string> DuckDBStore::get_realms(int64_t id) {
    std::lock_guard lock(mutex_);
    std::vector<std::string> realms;
    if (!db_) return realms;

    // First get primary realm
    std::ostringstream primary_sql;
    primary_sql << "SELECT realm FROM memory WHERE id = " << id;
    auto result = query(primary_sql.str());
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            realms.push_back(chunk->GetValue(0, 0).ToString());
        }
    }

    // Then get shared realms
    std::ostringstream shared_sql;
    shared_sql << "SELECT realm FROM realm_membership WHERE memory_id = " << id;
    result = query(shared_sql.str());
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
    std::lock_guard lock(mutex_);
    std::vector<std::string> realms;
    if (!db_) return realms;

    // Get all unique realms from both memory and realm_membership tables
    auto result = query(
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

    execute(sql.str());

    // Return count of memories with decay
    auto result = query("SELECT COUNT(*) FROM memory WHERE decay_rate > 0");
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

    // Use a single transaction for count + delete to ensure consistency
    std::ostringstream sql;
    sql << "BEGIN TRANSACTION; "
        << "DELETE FROM memory WHERE confidence < " << threshold
        << " AND created_at < " << min_created << "; "
        << "COMMIT;";

    // Get count before delete (approximate is fine for maintenance)
    std::ostringstream count_sql;
    count_sql << "SELECT COUNT(*) FROM memory WHERE confidence < " << threshold
              << " AND created_at < " << min_created;

    size_t count = 0;
    auto result = query(count_sql.str());
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
                << " AND created_at < " << min_created;
        execute(del_sql.str());
    }

    return count;
}

bool DuckDBStore::connect(
    const std::string& subject,
    const std::string& predicate,
    const std::string& object,
    float weight
) {
    std::lock_guard lock(mutex_);
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

    return execute(sql.str());
}

bool DuckDBStore::connect_with_source(
    const std::string& subject,
    const std::string& predicate,
    const std::string& object,
    const std::string& source_file,
    float weight
) {
    std::lock_guard lock(mutex_);
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

    return execute(sql.str());
}

size_t DuckDBStore::connect_batch(
    const std::vector<std::tuple<std::string, std::string, std::string, std::string>>& triplets,
    float weight
) {
    // Use SQL batch insert (Appender has issues with locks)
    std::lock_guard lock(mutex_);
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

    execute("BEGIN TRANSACTION");

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
            execute(sql);
            batch_count = 0;
        }
    }

    if (batch_count > 0) {
        execute(sql);
    }

    execute("COMMIT");
    return inserted;
}

std::vector<StringTriplet> DuckDBStore::query_subject(const std::string& subject) {
    std::lock_guard lock(mutex_);
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

    auto result = query(sql.str());
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
    std::lock_guard lock(mutex_);
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

    auto result = query(sql.str());
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
    std::lock_guard lock(mutex_);
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

    auto result = query(sql.str());
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
    std::lock_guard lock(mutex_);
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

    auto result = query(sql.str());
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
    std::lock_guard lock(mutex_);
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

    auto result = query(sql.str());
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
    std::lock_guard lock(mutex_);
    std::vector<int64_t> results;
    if (!db_) return results;

    std::ostringstream sql;
    sql << "SELECT caller_id FROM call_edge WHERE callee_id = " << symbol_id;

    auto result = query(sql.str());
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
    std::lock_guard lock(mutex_);
    std::vector<int64_t> results;
    if (!db_) return results;

    std::ostringstream sql;
    sql << "SELECT callee_id FROM call_edge WHERE caller_id = " << symbol_id;

    auto result = query(sql.str());
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
    std::lock_guard lock(mutex_);
    if (!db_) return false;

    std::ostringstream sql;
    sql << "INSERT OR IGNORE INTO call_edge (caller_id, callee_id) VALUES ("
        << caller_id << ", " << callee_id << ")";

    return execute(sql.str());
}

StoreHealth DuckDBStore::health() {
    std::lock_guard lock(mutex_);
    StoreHealth h;
    h.is_open = (db_ != nullptr);

    if (!db_) return h;

    // Count memories and avg confidence
    auto result = query("SELECT COUNT(*), AVG(confidence) FROM memory");
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
    result = query("SELECT COUNT(*) FROM symbol");
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            h.total_symbols = chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }

    // Count triplets
    result = query("SELECT COUNT(*) FROM triplet");
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            h.total_triplets = chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }

    return h;
}

size_t DuckDBStore::memory_count() {
    std::lock_guard lock(mutex_);
    if (!db_) return 0;

    auto result = query("SELECT COUNT(*) FROM memory");
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            return chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }
    return 0;
}

size_t DuckDBStore::triplet_count() {
    std::lock_guard lock(mutex_);
    if (!db_) return 0;

    auto result = query("SELECT COUNT(*) FROM triplet");
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            return chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }
    return 0;
}

size_t DuckDBStore::symbol_count() {
    std::lock_guard lock(mutex_);
    if (!db_) return 0;

    auto result = query("SELECT COUNT(*) FROM symbol");
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            return chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }
    return 0;
}

std::vector<std::pair<std::string, size_t>> DuckDBStore::get_top_connected_entities(size_t limit) {
    std::lock_guard lock(mutex_);
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

    auto result = query(sql.str());
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
    std::lock_guard lock(mutex_);
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

    auto result = query(sql.str());
    if (!result || result->HasError()) {
        // Fallback to LIKE search if FTS fails
        std::ostringstream fallback;
        fallback << "SELECT id, kind, name, signature, file_path, "
                 << "line_start, line_end, repo_id "
                 << "FROM symbol WHERE name ILIKE '%" << escaped << "%' "
                 << "OR signature ILIKE '%" << escaped << "%' "
                 << "LIMIT " << limit;
        result = this->query(fallback.str());
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
    std::lock_guard lock(mutex_);
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

    auto result = query(sql.str());
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
    std::lock_guard lock(mutex_);
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

    auto result = query(sql.str());
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
    std::lock_guard lock(mutex_);
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

    auto result = query(sql.str());
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
    std::lock_guard lock(mutex_);
    if (!db_) return std::nullopt;

    std::ostringstream sql;
    sql << "SELECT id, session_id, project, created_at, mood, coherence, confidence, "
        << "todos, active_files, decisions, next_steps, blockers, discoveries, snapshot "
        << "FROM ledger WHERE id = " << id;

    auto result = query(sql.str());
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
    std::lock_guard lock(mutex_);
    if (!db_) return false;

    std::ostringstream sql;
    sql << "DELETE FROM ledger WHERE id = " << id;

    return execute(sql.str());
}

// ============================================================================
// Code File Tracking (Incremental Indexing)
// ============================================================================

bool DuckDBStore::set_file_metadata(const CodeFile& file) {
    std::lock_guard lock(mutex_);
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

    return execute(sql.str());
}

std::optional<CodeFile> DuckDBStore::get_file_metadata(const std::string& path) {
    std::lock_guard lock(mutex_);
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

    auto result = query(sql.str());
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
    std::lock_guard lock(mutex_);
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

    auto result = query(sql.str());
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
    std::lock_guard lock(mutex_);
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

    return execute(sql.str());
}

bool DuckDBStore::delete_project_files(const std::string& project) {
    std::lock_guard lock(mutex_);
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

    return execute(sql.str());
}

size_t DuckDBStore::delete_file_symbols(const std::string& file_path) {
    std::lock_guard lock(mutex_);
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
    auto result = query(count_sql.str());
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
    execute(sql.str());

    return count;
}

size_t DuckDBStore::delete_file_triplets(const std::string& file_path) {
    std::lock_guard lock(mutex_);
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
    auto result = query(count_sql.str());
    size_t count = 0;
    if (result && !result->HasError()) {
        auto chunk = result->Fetch();
        if (chunk && chunk->size() > 0) {
            count = chunk->GetValue(0, 0).GetValue<int64_t>();
        }
    }

    std::ostringstream sql;
    sql << "DELETE FROM triplet WHERE source_file = '" << escape(file_path) << "'";
    execute(sql.str());

    return count;
}

// ═══════════════════════════════════════════════════════════════════════════
// Transcript State Operations (for distillation)
// ═══════════════════════════════════════════════════════════════════════════

bool DuckDBStore::register_transcript(const std::string& session_id, const std::string& transcript_path,
                                       const std::string& realm) {
    std::lock_guard lock(mutex_);
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

    // Use INSERT OR REPLACE to update if exists
    std::ostringstream sql;
    sql << "INSERT OR REPLACE INTO transcript_state "
        << "(session_id, transcript_path, realm, last_processed_line, last_distilled_at, created_at) "
        << "VALUES ("
        << "'" << escape(session_id) << "', "
        << "'" << escape(transcript_path) << "', "
        << "'" << escape(realm.empty() ? "default" : realm) << "', "
        << "0, 0, " << now_ts << ")";

    return execute(sql.str());
}

std::optional<TranscriptState> DuckDBStore::get_transcript(const std::string& session_id) {
    std::lock_guard lock(mutex_);
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

    auto result = query(sql.str());
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
    std::lock_guard lock(mutex_);
    std::vector<TranscriptState> states;
    if (!db_) return states;

    // Get all transcripts - daemon will check file sizes
    auto result = query(
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
    std::lock_guard lock(mutex_);
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

    return execute(sql.str());
}

bool DuckDBStore::mark_transcript_distilled(const std::string& session_id) {
    std::lock_guard lock(mutex_);
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

    return execute(sql.str());
}

bool DuckDBStore::remove_transcript(const std::string& session_id) {
    std::lock_guard lock(mutex_);
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

    return execute(sql.str());
}

size_t DuckDBStore::transcript_count() {
    std::lock_guard lock(mutex_);
    if (!db_) return 0;

    auto result = query("SELECT COUNT(*) FROM transcript_state");
    if (!result || result->HasError()) {
        return 0;
    }

    auto chunk = result->Fetch();
    if (!chunk || chunk->size() == 0) {
        return 0;
    }

    return chunk->GetValue(0, 0).GetValue<int64_t>();
}

}  // namespace chitta
