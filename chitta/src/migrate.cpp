// chitta-migrate: Import soul from cc-soul SQLite database
//
// Usage: chitta_migrate [OPTIONS]
//
// Options:
//   --soul-db PATH    Path to soul.db (default: ~/.claude/mind/soul.db)
//   --output PATH     Path to output mind storage (default: ./mind)
//   --model PATH      Path to ONNX model for embeddings
//   --vocab PATH      Path to vocabulary file
//   --dry-run         Show what would be migrated
//   --verbose         Show detailed progress

#include <chitta/mind.hpp>
#ifdef CHITTA_WITH_ONNX
#include <chitta/vak_onnx.hpp>
#endif
#include <sqlite3.h>
#include <iostream>
#include <string>
#include <cstring>
#include <unordered_map>

using namespace chitta;

struct MigrationStats {
    size_t wisdom = 0;
    size_t beliefs = 0;
    size_t episodes = 0;
    size_t aspirations = 0;
    size_t vocabulary = 0;
    size_t failures = 0;
    size_t total() const {
        return wisdom + beliefs + episodes + aspirations + vocabulary + failures;
    }
};

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --soul-db PATH    Path to soul.db (default: ~/.claude/mind/soul.db)\n"
              << "  --output PATH     Path to output mind storage (default: ./mind)\n"
#ifdef CHITTA_WITH_ONNX
              << "  --model PATH      Path to ONNX model for embeddings\n"
              << "  --vocab PATH      Path to vocabulary file\n"
#endif
              << "  --dry-run         Show what would be migrated\n"
              << "  --verbose, -v     Show detailed progress\n"
              << "  --help, -h        Show this help\n";
}

std::string default_soul_db() {
    const char* home = std::getenv("HOME");
    if (!home) home = ".";
    return std::string(home) + "/.claude/mind/soul.db";
}

bool table_exists(sqlite3* db, const char* table) {
    std::string sql = "SELECT 1 FROM sqlite_master WHERE type='table' AND name='" + std::string(table) + "'";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    bool exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return exists;
}

int migrate_wisdom(sqlite3* db, Mind& mind, MigrationStats& stats, bool verbose, bool dry_run) {
    if (!table_exists(db, "wisdom")) {
        if (verbose) std::cerr << "  No wisdom table found\n";
        return 0;
    }

    const char* sql = "SELECT id, type, title, content, domain, confidence FROM wisdom";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error preparing wisdom query: " << sqlite3_errmsg(db) << "\n";
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* id = (const char*)sqlite3_column_text(stmt, 0);
        const char* type = (const char*)sqlite3_column_text(stmt, 1);
        const char* title = (const char*)sqlite3_column_text(stmt, 2);
        const char* content = (const char*)sqlite3_column_text(stmt, 3);
        const char* domain = (const char*)sqlite3_column_text(stmt, 4);
        float confidence = sqlite3_column_double(stmt, 5);

        std::string full_text;
        if (domain && strlen(domain) > 0) {
            full_text = std::string("[") + domain + "] ";
        }
        if (title) full_text += std::string(title) + ": ";
        if (content) full_text += content;

        if (verbose && stats.wisdom % 100 == 0) {
            std::cerr << "  Wisdom: " << stats.wisdom << "...\n";
        }

        if (!dry_run && mind.has_yantra()) {
            NodeType node_type = NodeType::Wisdom;
            if (type && strcmp(type, "failure") == 0) {
                node_type = NodeType::Failure;
                stats.failures++;
            }
            mind.remember(full_text, node_type, Confidence(confidence));
        }
        stats.wisdom++;
    }

    sqlite3_finalize(stmt);
    return 0;
}

int migrate_beliefs(sqlite3* db, Mind& mind, MigrationStats& stats, bool verbose, bool dry_run) {
    if (!table_exists(db, "beliefs")) {
        if (verbose) std::cerr << "  No beliefs table found\n";
        return 0;
    }

    const char* sql = "SELECT id, belief, rationale, strength FROM beliefs";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error preparing beliefs query: " << sqlite3_errmsg(db) << "\n";
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* belief = (const char*)sqlite3_column_text(stmt, 1);
        const char* rationale = (const char*)sqlite3_column_text(stmt, 2);
        float strength = sqlite3_column_double(stmt, 3);

        std::string full_text = "BELIEF: ";
        if (belief) full_text += belief;
        if (rationale && strlen(rationale) > 0) {
            full_text += " (because: " + std::string(rationale) + ")";
        }

        if (!dry_run && mind.has_yantra()) {
            mind.remember(full_text, NodeType::Belief, Confidence(strength));
        }
        stats.beliefs++;
    }

    sqlite3_finalize(stmt);
    return 0;
}

int migrate_episodes(sqlite3* db, Mind& mind, MigrationStats& stats, bool verbose, bool dry_run) {
    if (!table_exists(db, "episodes")) {
        if (verbose) std::cerr << "  No episodes table found\n";
        return 0;
    }

    const char* sql = "SELECT id, title, summary, episode_type, lessons FROM episodes";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error preparing episodes query: " << sqlite3_errmsg(db) << "\n";
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* title = (const char*)sqlite3_column_text(stmt, 1);
        const char* summary = (const char*)sqlite3_column_text(stmt, 2);
        const char* episode_type = (const char*)sqlite3_column_text(stmt, 3);
        const char* lessons = (const char*)sqlite3_column_text(stmt, 4);

        std::string full_text;
        if (episode_type && strlen(episode_type) > 0) {
            full_text = std::string("[") + episode_type + "] ";
        }
        if (title) full_text += std::string(title) + "\n";
        if (summary) full_text += summary;
        if (lessons && strlen(lessons) > 0) {
            full_text += "\nLessons: " + std::string(lessons);
        }

        if (verbose && stats.episodes % 100 == 0) {
            std::cerr << "  Episodes: " << stats.episodes << "...\n";
        }

        if (!dry_run && mind.has_yantra()) {
            mind.remember(full_text, NodeType::Episode);
        }
        stats.episodes++;
    }

    sqlite3_finalize(stmt);
    return 0;
}

int migrate_aspirations(sqlite3* db, Mind& mind, MigrationStats& stats, bool verbose, bool dry_run) {
    if (!table_exists(db, "aspirations")) {
        if (verbose) std::cerr << "  No aspirations table found\n";
        return 0;
    }

    const char* sql = "SELECT id, direction, why, state FROM aspirations WHERE state='active'";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error preparing aspirations query: " << sqlite3_errmsg(db) << "\n";
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* direction = (const char*)sqlite3_column_text(stmt, 1);
        const char* why = (const char*)sqlite3_column_text(stmt, 2);

        std::string full_text = "ASPIRATION: ";
        if (direction) full_text += direction;
        if (why && strlen(why) > 0) {
            full_text += " (because: " + std::string(why) + ")";
        }

        if (!dry_run && mind.has_yantra()) {
            mind.remember(full_text, NodeType::Aspiration, Confidence(0.9f));
        }
        stats.aspirations++;
    }

    sqlite3_finalize(stmt);
    return 0;
}

int migrate_vocabulary(sqlite3* db, Mind& mind, MigrationStats& stats, bool verbose, bool dry_run) {
    if (!table_exists(db, "vocabulary")) {
        if (verbose) std::cerr << "  No vocabulary table found\n";
        return 0;
    }

    const char* sql = "SELECT term, meaning, context FROM vocabulary";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error preparing vocabulary query: " << sqlite3_errmsg(db) << "\n";
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* term = (const char*)sqlite3_column_text(stmt, 0);
        const char* meaning = (const char*)sqlite3_column_text(stmt, 1);
        const char* context = (const char*)sqlite3_column_text(stmt, 2);

        std::string full_text = "TERM: ";
        if (term) full_text += std::string(term) + " = ";
        if (meaning) full_text += meaning;
        if (context && strlen(context) > 0) {
            full_text += " (context: " + std::string(context) + ")";
        }

        if (!dry_run && mind.has_yantra()) {
            mind.remember(full_text, NodeType::Term);
        }
        stats.vocabulary++;
    }

    sqlite3_finalize(stmt);
    return 0;
}

int main(int argc, char* argv[]) {
    std::string soul_db_path = default_soul_db();
    std::string output_path = "./mind";
    std::string model_path;
    std::string vocab_path;
    bool dry_run = false;
    bool verbose = false;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--soul-db") == 0 && i + 1 < argc) {
            soul_db_path = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            model_path = argv[++i];
        } else if (strcmp(argv[i], "--vocab") == 0 && i + 1 < argc) {
            vocab_path = argv[++i];
        } else if (strcmp(argv[i], "--dry-run") == 0) {
            dry_run = true;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    std::cout << "┌─────────────────────────────────────────┐\n";
    std::cout << "│       chitta-migrate                   │\n";
    std::cout << "│   Import soul from cc-soul/cc-memory    │\n";
    std::cout << "└─────────────────────────────────────────┘\n\n";
    std::cout << "Source:  " << soul_db_path << "\n";
    std::cout << "Output:  " << output_path << "\n";
    if (dry_run) {
        std::cout << "Mode:    DRY RUN (no changes)\n";
    }
    std::cout << "\n";

    // Open SQLite database
    sqlite3* db;
    if (sqlite3_open(soul_db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Error opening database: " << sqlite3_errmsg(db) << "\n";
        return 1;
    }

    // Create mind
    MindConfig config;
    config.path = output_path;
    Mind mind(config);

    // Attach ONNX yantra if available
#ifdef CHITTA_WITH_ONNX
    if (!model_path.empty() && !vocab_path.empty()) {
        AntahkaranaYantra::Config yantra_config;
        yantra_config.pooling = PoolingStrategy::Mean;
        yantra_config.normalize_embeddings = true;

        auto yantra = std::make_shared<AntahkaranaYantra>(yantra_config);
        if (yantra->awaken(model_path, vocab_path)) {
            mind.attach_yantra(yantra);
            std::cout << "Yantra attached: " << model_path << "\n\n";
        } else {
            std::cerr << "Warning: Failed to awaken yantra: " << yantra->error() << "\n";
            std::cerr << "Continuing without embeddings...\n\n";
        }
    } else if (!dry_run) {
        std::cerr << "Warning: No model/vocab provided. Embeddings will not be generated.\n";
        std::cerr << "Use --model and --vocab to enable semantic search.\n\n";
    }
#endif

    if (!mind.open()) {
        std::cerr << "Error: Failed to open mind at " << output_path << "\n";
        sqlite3_close(db);
        return 1;
    }

    // Run migrations
    MigrationStats stats;

    if (verbose) std::cout << "Migrating wisdom...\n";
    migrate_wisdom(db, mind, stats, verbose, dry_run);

    if (verbose) std::cout << "Migrating beliefs...\n";
    migrate_beliefs(db, mind, stats, verbose, dry_run);

    if (verbose) std::cout << "Migrating episodes...\n";
    migrate_episodes(db, mind, stats, verbose, dry_run);

    if (verbose) std::cout << "Migrating aspirations...\n";
    migrate_aspirations(db, mind, stats, verbose, dry_run);

    if (verbose) std::cout << "Migrating vocabulary...\n";
    migrate_vocabulary(db, mind, stats, verbose, dry_run);

    // Close
    sqlite3_close(db);
    mind.close();

    // Report
    std::cout << "Migration " << (dry_run ? "would migrate" : "complete") << ":\n";
    std::cout << "  Wisdom:      " << stats.wisdom << "\n";
    std::cout << "  Beliefs:     " << stats.beliefs << "\n";
    std::cout << "  Episodes:    " << stats.episodes << "\n";
    std::cout << "  Aspirations: " << stats.aspirations << "\n";
    std::cout << "  Vocabulary:  " << stats.vocabulary << "\n";
    std::cout << "  ───────────────────\n";
    std::cout << "  Total:       " << stats.total() << " nodes\n";

    if (!dry_run) {
        std::cout << "\nSaved to: " << output_path << "\n";
    }

    return 0;
}
