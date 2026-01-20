#include "chitta/postgres_store.hpp"
#include "chitta/duckdb_store.hpp"  // For shared types
#include <iostream>
#include <sstream>
#include <cmath>
#include <chrono>
#include <cstring>

namespace chitta {

PostgresStore::~PostgresStore() {
    close();
}

bool PostgresStore::open(const PostgresConfig& config) {
    return open(config.connection_string());
}

bool PostgresStore::open(const std::string& connection_string) {
    std::lock_guard lock(mutex_);

    if (conn_) {
        std::cerr << "[PostgresStore] Already open\n";
        return false;
    }

    conn_ = PQconnectdb(connection_string.c_str());

    if (PQstatus(conn_) != CONNECTION_OK) {
        std::cerr << "[PostgresStore] Connection failed: " << PQerrorMessage(conn_) << "\n";
        PQfinish(conn_);
        conn_ = nullptr;
        return false;
    }

    // Check for extensions
    check_extensions();

    // Create schema
    if (!create_schema()) {
        std::cerr << "[PostgresStore] Failed to create schema\n";
        close();
        return false;
    }

    // Create vector index if pgvector available
    if (pgvector_available_) {
        create_vector_index();
    }

    std::cerr << "[PostgresStore] Connected to PostgreSQL"
              << " (pgvector=" << (pgvector_available_ ? "yes" : "no")
              << ", age=" << (age_available_ ? "yes" : "no") << ")\n";

    return true;
}

void PostgresStore::close() {
    std::lock_guard lock(mutex_);
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

bool PostgresStore::check_extensions() {
    // Check pgvector
    PGresult* res = PQexec(conn_, "SELECT 1 FROM pg_extension WHERE extname = 'vector'");
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        pgvector_available_ = true;
        std::cerr << "[PostgresStore] pgvector extension available\n";
    } else {
        // Try to create it
        PQclear(res);
        res = PQexec(conn_, "CREATE EXTENSION IF NOT EXISTS vector");
        if (PQresultStatus(res) == PGRES_COMMAND_OK) {
            pgvector_available_ = true;
            std::cerr << "[PostgresStore] pgvector extension created\n";
        }
    }
    PQclear(res);

    // Check Apache AGE
    res = PQexec(conn_, "SELECT 1 FROM pg_extension WHERE extname = 'age'");
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        age_available_ = true;
        std::cerr << "[PostgresStore] Apache AGE extension available\n";
    } else {
        // Try to create it
        PQclear(res);
        res = PQexec(conn_, "CREATE EXTENSION IF NOT EXISTS age");
        if (PQresultStatus(res) == PGRES_COMMAND_OK) {
            age_available_ = true;
            std::cerr << "[PostgresStore] Apache AGE extension created\n";
        }
    }
    PQclear(res);

    return true;
}

bool PostgresStore::create_schema() {
    // Memory table with vector column
    std::string memory_sql;
    if (pgvector_available_) {
        memory_sql = R"(
            CREATE TABLE IF NOT EXISTS memory (
                id BIGSERIAL PRIMARY KEY,
                kind VARCHAR(50),
                content TEXT,
                confidence REAL,
                decay_rate REAL,
                created_at BIGINT,
                accessed_at BIGINT,
                embedding vector(384)
            )
        )";
    } else {
        // Fallback without vector type
        memory_sql = R"(
            CREATE TABLE IF NOT EXISTS memory (
                id BIGSERIAL PRIMARY KEY,
                kind VARCHAR(50),
                content TEXT,
                confidence REAL,
                decay_rate REAL,
                created_at BIGINT,
                accessed_at BIGINT,
                embedding REAL[]
            )
        )";
    }

    if (!execute(memory_sql)) return false;

    // Triplet table
    if (!execute(R"(
        CREATE TABLE IF NOT EXISTS triplet (
            id BIGSERIAL PRIMARY KEY,
            subject VARCHAR(255),
            predicate VARCHAR(255),
            object VARCHAR(255),
            weight REAL DEFAULT 1.0,
            created_at BIGINT
        )
    )")) return false;

    // Indexes for triplet queries
    execute("CREATE INDEX IF NOT EXISTS idx_triplet_subject ON triplet(subject)");
    execute("CREATE INDEX IF NOT EXISTS idx_triplet_object ON triplet(object)");
    execute("CREATE INDEX IF NOT EXISTS idx_triplet_predicate ON triplet(predicate)");

    // Symbol table
    std::string symbol_sql;
    if (pgvector_available_) {
        symbol_sql = R"(
            CREATE TABLE IF NOT EXISTS symbol (
                id BIGSERIAL PRIMARY KEY,
                kind VARCHAR(50),
                name VARCHAR(255),
                signature TEXT,
                file_path TEXT,
                line_start INTEGER,
                line_end INTEGER,
                repo_id BIGINT,
                embedding vector(384)
            )
        )";
    } else {
        symbol_sql = R"(
            CREATE TABLE IF NOT EXISTS symbol (
                id BIGSERIAL PRIMARY KEY,
                kind VARCHAR(50),
                name VARCHAR(255),
                signature TEXT,
                file_path TEXT,
                line_start INTEGER,
                line_end INTEGER,
                repo_id BIGINT,
                embedding REAL[]
            )
        )";
    }

    if (!execute(symbol_sql)) return false;

    // Call graph
    if (!execute(R"(
        CREATE TABLE IF NOT EXISTS call_edge (
            caller_id BIGINT,
            callee_id BIGINT,
            PRIMARY KEY (caller_id, callee_id)
        )
    )")) return false;

    // Symbol indexes
    execute("CREATE INDEX IF NOT EXISTS idx_symbol_name ON symbol(name)");
    execute("CREATE INDEX IF NOT EXISTS idx_symbol_kind ON symbol(kind)");

    return true;
}

bool PostgresStore::create_vector_index() {
    if (!pgvector_available_) return false;

    // Create HNSW index on memory embeddings
    return execute(R"(
        CREATE INDEX IF NOT EXISTS memory_embedding_idx
        ON memory USING hnsw (embedding vector_cosine_ops)
        WITH (m = 16, ef_construction = 64)
    )");
}

bool PostgresStore::execute(const std::string& sql) {
    PGresult* res = PQexec(conn_, sql.c_str());
    ExecStatusType status = PQresultStatus(res);

    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        std::cerr << "[PostgresStore] Query error: " << PQerrorMessage(conn_) << "\n";
        std::cerr << "[PostgresStore] SQL: " << sql.substr(0, 200) << "\n";
        PQclear(res);
        return false;
    }

    PQclear(res);
    return true;
}

PGresult* PostgresStore::query(const std::string& sql) {
    return PQexec(conn_, sql.c_str());
}

std::string PostgresStore::escape(const std::string& str) {
    char* escaped = PQescapeLiteral(conn_, str.c_str(), str.length());
    if (!escaped) return "''";
    std::string result(escaped);
    PQfreemem(escaped);
    return result;
}

std::string PostgresStore::embedding_to_pgvector(const std::vector<float>& embedding) {
    std::ostringstream ss;
    ss << "'[";
    for (size_t i = 0; i < embedding.size(); ++i) {
        if (i > 0) ss << ",";
        ss << embedding[i];
    }
    ss << "]'";
    return ss.str();
}

int64_t PostgresStore::remember(
    const std::string& content,
    const std::string& kind,
    const std::vector<float>& embedding,
    float confidence,
    float decay_rate
) {
    std::lock_guard lock(mutex_);
    if (!conn_) return -1;

    Timestamp now_ts = now();

    std::ostringstream sql;
    sql << "INSERT INTO memory (kind, content, confidence, decay_rate, created_at, accessed_at, embedding) "
        << "VALUES (" << escape(kind) << ", " << escape(content) << ", "
        << confidence << ", " << decay_rate << ", " << now_ts << ", " << now_ts << ", "
        << embedding_to_pgvector(embedding) << ") RETURNING id";

    PGresult* res = query(sql.str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        std::cerr << "[PostgresStore] Insert failed: " << PQerrorMessage(conn_) << "\n";
        PQclear(res);
        return -1;
    }

    int64_t id = std::stoll(PQgetvalue(res, 0, 0));
    PQclear(res);
    return id;
}

std::vector<MemoryResult> PostgresStore::recall(
    const std::vector<float>& query_embedding,
    size_t k
) {
    std::lock_guard lock(mutex_);
    std::vector<MemoryResult> results;
    if (!conn_) return results;

    std::ostringstream sql;
    if (pgvector_available_) {
        // Use pgvector cosine distance (1 - similarity)
        sql << "SELECT id, kind, content, confidence, created_at, accessed_at, "
            << "1 - (embedding <=> " << embedding_to_pgvector(query_embedding) << ") AS similarity "
            << "FROM memory "
            << "ORDER BY embedding <=> " << embedding_to_pgvector(query_embedding) << " "
            << "LIMIT " << k;
    } else {
        // Fallback: no vector search, just return recent
        sql << "SELECT id, kind, content, confidence, created_at, accessed_at, 0.5 AS similarity "
            << "FROM memory ORDER BY accessed_at DESC LIMIT " << k;
    }

    PGresult* res = query(sql.str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::cerr << "[PostgresStore] Query failed: " << PQerrorMessage(conn_) << "\n";
        PQclear(res);
        return results;
    }

    int nrows = PQntuples(res);
    for (int i = 0; i < nrows; ++i) {
        MemoryResult r;
        r.id = std::stoll(PQgetvalue(res, i, 0));
        r.kind = PQgetvalue(res, i, 1);
        r.content = PQgetvalue(res, i, 2);
        r.confidence = std::stof(PQgetvalue(res, i, 3));
        r.created_at = std::stoll(PQgetvalue(res, i, 4));
        r.accessed_at = std::stoll(PQgetvalue(res, i, 5));
        r.similarity = std::stof(PQgetvalue(res, i, 6));
        results.push_back(r);
    }

    PQclear(res);
    return results;
}

bool PostgresStore::strengthen(int64_t id, float amount) {
    std::lock_guard lock(mutex_);
    if (!conn_) return false;

    std::ostringstream sql;
    sql << "UPDATE memory SET confidence = LEAST(confidence + " << amount << ", 1.0), "
        << "accessed_at = " << now() << " WHERE id = " << id;

    return execute(sql.str());
}

bool PostgresStore::weaken(int64_t id, float amount) {
    std::lock_guard lock(mutex_);
    if (!conn_) return false;

    std::ostringstream sql;
    sql << "UPDATE memory SET confidence = GREATEST(confidence - " << amount << ", 0.0) "
        << "WHERE id = " << id;

    return execute(sql.str());
}

bool PostgresStore::forget(int64_t id) {
    std::lock_guard lock(mutex_);
    if (!conn_) return false;

    std::ostringstream sql;
    sql << "DELETE FROM memory WHERE id = " << id;

    return execute(sql.str());
}

bool PostgresStore::touch(int64_t id) {
    std::lock_guard lock(mutex_);
    if (!conn_) return false;

    std::ostringstream sql;
    sql << "UPDATE memory SET accessed_at = " << now() << " WHERE id = " << id;

    return execute(sql.str());
}

std::optional<MemoryResult> PostgresStore::get_memory(int64_t id) {
    std::lock_guard lock(mutex_);
    if (!conn_) return std::nullopt;

    std::ostringstream sql;
    sql << "SELECT id, kind, content, confidence, created_at, accessed_at "
        << "FROM memory WHERE id = " << id;

    PGresult* res = query(sql.str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }

    MemoryResult r;
    r.id = std::stoll(PQgetvalue(res, 0, 0));
    r.kind = PQgetvalue(res, 0, 1);
    r.content = PQgetvalue(res, 0, 2);
    r.confidence = std::stof(PQgetvalue(res, 0, 3));
    r.created_at = std::stoll(PQgetvalue(res, 0, 4));
    r.accessed_at = std::stoll(PQgetvalue(res, 0, 5));
    r.similarity = 1.0f;

    PQclear(res);
    return r;
}

size_t PostgresStore::apply_decay() {
    std::lock_guard lock(mutex_);
    if (!conn_) return 0;

    Timestamp current = now();

    std::ostringstream sql;
    sql << "UPDATE memory SET confidence = confidence * exp(-decay_rate * "
        << "(" << current << " - accessed_at) / 86400000.0) "
        << "WHERE decay_rate > 0";

    execute(sql.str());

    // Return count
    PGresult* res = query("SELECT COUNT(*) FROM memory WHERE decay_rate > 0");
    size_t count = 0;
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        count = std::stoll(PQgetvalue(res, 0, 0));
    }
    PQclear(res);
    return count;
}

size_t PostgresStore::prune(float threshold, float min_age_days) {
    std::lock_guard lock(mutex_);
    if (!conn_) return 0;

    Timestamp current = now();
    Timestamp min_created = current - static_cast<int64_t>(min_age_days * 86400000.0);

    // Count first
    std::ostringstream count_sql;
    count_sql << "SELECT COUNT(*) FROM memory WHERE confidence < " << threshold
              << " AND created_at < " << min_created;

    PGresult* res = query(count_sql.str());
    size_t count = 0;
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        count = std::stoll(PQgetvalue(res, 0, 0));
    }
    PQclear(res);

    // Delete
    std::ostringstream del_sql;
    del_sql << "DELETE FROM memory WHERE confidence < " << threshold
            << " AND created_at < " << min_created;
    execute(del_sql.str());

    return count;
}

bool PostgresStore::connect(
    const std::string& subject,
    const std::string& predicate,
    const std::string& object,
    float weight
) {
    std::lock_guard lock(mutex_);
    if (!conn_) return false;

    std::ostringstream sql;
    sql << "INSERT INTO triplet (subject, predicate, object, weight, created_at) VALUES ("
        << escape(subject) << ", " << escape(predicate) << ", "
        << escape(object) << ", " << weight << ", " << now() << ")";

    return execute(sql.str());
}

std::vector<StringTriplet> PostgresStore::query_subject(const std::string& subject) {
    std::lock_guard lock(mutex_);
    std::vector<StringTriplet> results;
    if (!conn_) return results;

    std::ostringstream sql;
    sql << "SELECT subject, predicate, object, weight FROM triplet "
        << "WHERE subject = " << escape(subject);

    PGresult* res = query(sql.str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        PQclear(res);
        return results;
    }

    int nrows = PQntuples(res);
    for (int i = 0; i < nrows; ++i) {
        StringTriplet t;
        t.subject = PQgetvalue(res, i, 0);
        t.predicate = PQgetvalue(res, i, 1);
        t.object = PQgetvalue(res, i, 2);
        t.weight = std::stof(PQgetvalue(res, i, 3));
        results.push_back(t);
    }

    PQclear(res);
    return results;
}

std::vector<StringTriplet> PostgresStore::query_object(const std::string& object) {
    std::lock_guard lock(mutex_);
    std::vector<StringTriplet> results;
    if (!conn_) return results;

    std::ostringstream sql;
    sql << "SELECT subject, predicate, object, weight FROM triplet "
        << "WHERE object = " << escape(object);

    PGresult* res = query(sql.str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        PQclear(res);
        return results;
    }

    int nrows = PQntuples(res);
    for (int i = 0; i < nrows; ++i) {
        StringTriplet t;
        t.subject = PQgetvalue(res, i, 0);
        t.predicate = PQgetvalue(res, i, 1);
        t.object = PQgetvalue(res, i, 2);
        t.weight = std::stof(PQgetvalue(res, i, 3));
        results.push_back(t);
    }

    PQclear(res);
    return results;
}

std::vector<StringTriplet> PostgresStore::query_predicate(const std::string& predicate) {
    std::lock_guard lock(mutex_);
    std::vector<StringTriplet> results;
    if (!conn_) return results;

    std::ostringstream sql;
    sql << "SELECT subject, predicate, object, weight FROM triplet "
        << "WHERE predicate = " << escape(predicate);

    PGresult* res = query(sql.str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        PQclear(res);
        return results;
    }

    int nrows = PQntuples(res);
    for (int i = 0; i < nrows; ++i) {
        StringTriplet t;
        t.subject = PQgetvalue(res, i, 0);
        t.predicate = PQgetvalue(res, i, 1);
        t.object = PQgetvalue(res, i, 2);
        t.weight = std::stof(PQgetvalue(res, i, 3));
        results.push_back(t);
    }

    PQclear(res);
    return results;
}

int64_t PostgresStore::add_symbol(const Symbol& sym, const std::vector<float>& embedding) {
    std::lock_guard lock(mutex_);
    if (!conn_) return -1;

    std::vector<float> embed = embedding;
    if (embed.empty()) {
        embed.resize(EMBED_DIM, 0.0f);
    }

    std::ostringstream sql;
    sql << "INSERT INTO symbol (kind, name, signature, file_path, line_start, line_end, repo_id, embedding) "
        << "VALUES (" << escape(sym.kind) << ", " << escape(sym.name) << ", "
        << escape(sym.signature) << ", " << escape(sym.file_path) << ", "
        << sym.line_start << ", " << sym.line_end << ", " << sym.repo_id << ", "
        << embedding_to_pgvector(embed) << ") RETURNING id";

    PGresult* res = query(sql.str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return -1;
    }

    int64_t id = std::stoll(PQgetvalue(res, 0, 0));
    PQclear(res);
    return id;
}

std::vector<Symbol> PostgresStore::find_symbol(const std::string& name, const std::string& kind) {
    std::lock_guard lock(mutex_);
    std::vector<Symbol> results;
    if (!conn_) return results;

    std::ostringstream sql;
    sql << "SELECT id, kind, name, signature, file_path, line_start, line_end, repo_id "
        << "FROM symbol WHERE name ILIKE '%" << name << "%'";
    if (!kind.empty()) {
        sql << " AND kind = " << escape(kind);
    }

    PGresult* res = query(sql.str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        PQclear(res);
        return results;
    }

    int nrows = PQntuples(res);
    for (int i = 0; i < nrows; ++i) {
        Symbol s;
        s.id = std::stoll(PQgetvalue(res, i, 0));
        s.kind = PQgetvalue(res, i, 1);
        s.name = PQgetvalue(res, i, 2);
        s.signature = PQgetvalue(res, i, 3);
        s.file_path = PQgetvalue(res, i, 4);
        s.line_start = std::stoi(PQgetvalue(res, i, 5));
        s.line_end = std::stoi(PQgetvalue(res, i, 6));
        s.repo_id = std::stoll(PQgetvalue(res, i, 7));
        results.push_back(s);
    }

    PQclear(res);
    return results;
}

std::vector<int64_t> PostgresStore::callers(int64_t symbol_id) {
    std::lock_guard lock(mutex_);
    std::vector<int64_t> results;
    if (!conn_) return results;

    std::ostringstream sql;
    sql << "SELECT caller_id FROM call_edge WHERE callee_id = " << symbol_id;

    PGresult* res = query(sql.str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        PQclear(res);
        return results;
    }

    int nrows = PQntuples(res);
    for (int i = 0; i < nrows; ++i) {
        results.push_back(std::stoll(PQgetvalue(res, i, 0)));
    }

    PQclear(res);
    return results;
}

std::vector<int64_t> PostgresStore::callees(int64_t symbol_id) {
    std::lock_guard lock(mutex_);
    std::vector<int64_t> results;
    if (!conn_) return results;

    std::ostringstream sql;
    sql << "SELECT callee_id FROM call_edge WHERE caller_id = " << symbol_id;

    PGresult* res = query(sql.str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        PQclear(res);
        return results;
    }

    int nrows = PQntuples(res);
    for (int i = 0; i < nrows; ++i) {
        results.push_back(std::stoll(PQgetvalue(res, i, 0)));
    }

    PQclear(res);
    return results;
}

bool PostgresStore::add_call(int64_t caller_id, int64_t callee_id) {
    std::lock_guard lock(mutex_);
    if (!conn_) return false;

    std::ostringstream sql;
    sql << "INSERT INTO call_edge (caller_id, callee_id) VALUES ("
        << caller_id << ", " << callee_id << ") ON CONFLICT DO NOTHING";

    return execute(sql.str());
}

StoreHealth PostgresStore::health() {
    std::lock_guard lock(mutex_);
    StoreHealth h;
    h.is_open = (conn_ != nullptr);

    if (!conn_) return h;

    // Count memories and avg confidence
    PGresult* res = query("SELECT COUNT(*), AVG(confidence) FROM memory");
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        h.total_memories = std::stoll(PQgetvalue(res, 0, 0));
        const char* avg = PQgetvalue(res, 0, 1);
        if (avg && strlen(avg) > 0) {
            h.avg_confidence = std::stof(avg);
        }
    }
    PQclear(res);

    // Count symbols
    res = query("SELECT COUNT(*) FROM symbol");
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        h.total_symbols = std::stoll(PQgetvalue(res, 0, 0));
    }
    PQclear(res);

    // Count triplets
    res = query("SELECT COUNT(*) FROM triplet");
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        h.total_triplets = std::stoll(PQgetvalue(res, 0, 0));
    }
    PQclear(res);

    return h;
}

size_t PostgresStore::memory_count() {
    std::lock_guard lock(mutex_);
    if (!conn_) return 0;

    PGresult* res = query("SELECT COUNT(*) FROM memory");
    size_t count = 0;
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        count = std::stoll(PQgetvalue(res, 0, 0));
    }
    PQclear(res);
    return count;
}

size_t PostgresStore::triplet_count() {
    std::lock_guard lock(mutex_);
    if (!conn_) return 0;

    PGresult* res = query("SELECT COUNT(*) FROM triplet");
    size_t count = 0;
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        count = std::stoll(PQgetvalue(res, 0, 0));
    }
    PQclear(res);
    return count;
}

size_t PostgresStore::symbol_count() {
    std::lock_guard lock(mutex_);
    if (!conn_) return 0;

    PGresult* res = query("SELECT COUNT(*) FROM symbol");
    size_t count = 0;
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        count = std::stoll(PQgetvalue(res, 0, 0));
    }
    PQclear(res);
    return count;
}

}  // namespace chitta
