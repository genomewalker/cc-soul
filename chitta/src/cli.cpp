// chitta-cli: Command-line interface for chitta soul operations
//
// Usage: chitta_cli <command> [options]
//
// Commands:
//   stats      Show soul statistics
//   recall     Semantic search
//   cycle      Run maintenance cycle
//   upgrade    Upgrade database to current version
//   help       Show this help

#include <chitta/mind.hpp>
#include <chitta/migrations.hpp>
#include <chitta/version.hpp>
#ifdef CHITTA_WITH_ONNX
#include <chitta/vak_onnx.hpp>
#endif
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>

using namespace chitta;

void print_usage(const char* prog) {
    std::cerr << "chitta " << CHITTA_VERSION << "\n\n"
              << "Usage: " << prog << " <command> [options]\n\n"
              << "Commands:\n"
              << "  stats              Show soul statistics\n"
              << "  recall <query>     Semantic search\n"
              << "  cycle              Run maintenance cycle\n"
              << "  upgrade            Upgrade database to current version\n"
              << "  convert <format>   Convert to new storage format (unified|segments)\n"
              << "  help               Show this help\n\n"
              << "Global options:\n"
              << "  --path PATH        Mind storage path (default: ~/.claude/mind/chitta)\n"
              << "  --json             Output as JSON\n"
              << "  --fast             Skip BM25 loading (for quick stats)\n"
              << "  -v, --version      Show version\n"
#ifdef CHITTA_WITH_ONNX
              << "  --model PATH       ONNX model path\n"
              << "  --vocab PATH       Vocabulary file path\n"
#endif
              ;
}

std::string default_mind_path() {
    const char* home = std::getenv("HOME");
    if (!home) home = ".";
    return std::string(home) + "/.claude/mind/chitta";
}

std::string default_model_path() {
    // Check CLAUDE_PLUGIN_ROOT first (when running as plugin)
    if (const char* plugin_root = std::getenv("CLAUDE_PLUGIN_ROOT")) {
        return std::string(plugin_root) + "/chitta/models/model.onnx";
    }
    // Fallback: look relative to binary location or cwd
    const char* home = std::getenv("HOME");
    if (!home) home = ".";
    return std::string(home) + "/.claude/mind/model.onnx";
}

std::string default_vocab_path() {
    // Check CLAUDE_PLUGIN_ROOT first (when running as plugin)
    if (const char* plugin_root = std::getenv("CLAUDE_PLUGIN_ROOT")) {
        return std::string(plugin_root) + "/chitta/models/vocab.txt";
    }
    // Fallback
    const char* home = std::getenv("HOME");
    if (!home) home = ".";
    return std::string(home) + "/.claude/mind/vocab.txt";
}

int cmd_stats(Mind& mind, bool json_output) {
    auto coherence = mind.coherence();
    auto health = mind.health();

    if (json_output) {
        std::cout << "{"
                  << "\"version\":\"" << CHITTA_VERSION << "\","
                  << "\"hot\":" << mind.hot_size() << ","
                  << "\"warm\":" << mind.warm_size() << ","
                  << "\"cold\":" << mind.cold_size() << ","
                  << "\"total\":" << mind.size() << ","
                  << "\"coherence\":{"
                  << "\"global\":" << coherence.global << ","
                  << "\"local\":" << coherence.local << ","
                  << "\"structural\":" << coherence.structural << ","
                  << "\"temporal\":" << coherence.temporal << ","
                  << "\"tau\":" << coherence.tau_k()
                  << "},"
                  << "\"ojas\":{"
                  << "\"structural\":" << health.structural << ","
                  << "\"semantic\":" << health.semantic << ","
                  << "\"temporal\":" << health.temporal << ","
                  << "\"capacity\":" << health.capacity << ","
                  << "\"psi\":" << health.psi() << ","
                  << "\"status\":\"" << health.status_string() << "\""
                  << "},"
                  << "\"yantra\":" << (mind.has_yantra() ? "true" : "false")
                  << "}\n";
    } else {
        std::cout << "Soul Statistics\n";
        std::cout << "═══════════════════════════════\n";
        std::cout << "Nodes:\n";
        std::cout << "  Hot:    " << mind.hot_size() << "\n";
        std::cout << "  Warm:   " << mind.warm_size() << "\n";
        std::cout << "  Cold:   " << mind.cold_size() << "\n";
        std::cout << "  Total:  " << mind.size() << "\n";
        std::cout << "\nSāmarasya (Coherence):\n";
        std::cout << "  Global:     " << coherence.global << "\n";
        std::cout << "  Local:      " << coherence.local << "\n";
        std::cout << "  Structural: " << coherence.structural << "\n";
        std::cout << "  Temporal:   " << coherence.temporal << "\n";
        std::cout << "  τ (tau):    " << coherence.tau_k() << "\n";
        std::cout << "\nOjas (Vitality):\n";
        std::cout << "  Structural: " << health.structural << "\n";
        std::cout << "  Semantic:   " << health.semantic << "\n";
        std::cout << "  Temporal:   " << health.temporal << "\n";
        std::cout << "  Capacity:   " << health.capacity << "\n";
        std::cout << "  ψ (psi):    " << health.psi() << " [" << health.status_string() << "]\n";
        std::cout << "\nYantra: " << (mind.has_yantra() ? "ready" : "not attached") << "\n";
    }

    return 0;
}

int cmd_recall(Mind& mind, const std::string& query, int limit) {
    if (!mind.has_yantra()) {
        std::cerr << "Error: Yantra not attached, semantic search unavailable\n";
        return 1;
    }

    auto results = mind.recall(query, limit);

    if (results.empty()) {
        std::cout << "No results found for: " << query << "\n";
        return 0;
    }

    std::cout << "Results for: " << query << "\n";
    std::cout << "═══════════════════════════════\n";

    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        std::cout << "\n[" << (i + 1) << "] (score: " << r.similarity << ")\n";
        std::cout << r.text << "\n";
    }

    return 0;
}

int cmd_cycle(Mind& mind) {
    std::cout << "Running maintenance cycle...\n";

    size_t before = mind.size();
    auto report = mind.tick();
    size_t after = mind.size();

    std::cout << "Cycle complete.\n";
    std::cout << "  Before: " << before << " nodes\n";
    std::cout << "  After:  " << after << " nodes\n";
    std::cout << "  Decay applied: " << (report.decay_applied ? "yes" : "no") << "\n";

    if (before != after) {
        std::cout << "  Changed: " << (before > after ? before - after : after - before) << " nodes\n";
    }

    return 0;
}

int cmd_upgrade(const std::string& db_path) {
    std::string hot_path = db_path + ".hot";

    // Check current version
    uint32_t version = migrations::detect_version(hot_path);

    if (version == 0) {
        std::cerr << "No database found at: " << hot_path << "\n";
        return 1;
    }

    std::cout << "Database: " << hot_path << "\n";
    std::cout << "Current version: " << version << "\n";
    std::cout << "Target version: " << migrations::CURRENT_VERSION << "\n";

    if (version == migrations::CURRENT_VERSION) {
        std::cout << "Already at current version. No upgrade needed.\n";
        return 0;
    }

    if (version > migrations::CURRENT_VERSION) {
        std::cerr << "Database version " << version
                  << " is newer than supported " << migrations::CURRENT_VERSION << "\n";
        std::cerr << "Update chitta to read this database.\n";
        return 1;
    }

    std::cout << "\nUpgrading...\n";

    auto result = migrations::upgrade(hot_path);

    if (result.success) {
        std::cout << "Upgrade complete: v" << result.from_version
                  << " → v" << result.to_version << "\n";
        if (!result.backup_path.empty()) {
            std::cout << "Backup saved: " << result.backup_path << "\n";
        }
        return 0;
    } else {
        std::cerr << "Upgrade failed: " << result.error << "\n";
        return 1;
    }
}

int cmd_convert(const std::string& db_path, const std::string& format) {
    if (format != "unified" && format != "segments") {
        std::cerr << "Unknown format: " << format << "\n";
        std::cerr << "Supported formats: unified, segments\n";
        return 1;
    }

    std::cout << "Converting " << db_path << " to " << format << " format...\n\n";

    migrations::ConversionResult result;

    if (format == "unified") {
        result = migrations::convert_to_unified(db_path);
    } else {
        result = migrations::convert_to_segments(db_path);
    }

    if (result.success) {
        std::cout << "\nConversion complete!\n";
        std::cout << "  Nodes converted: " << result.nodes_converted << "\n";
        if (!result.backup_path.empty()) {
            std::cout << "  Backup saved: " << result.backup_path << "\n";
        }
        std::cout << "\nThe database will now use " << format << " format on next open.\n";
        return 0;
    } else {
        std::cerr << "Conversion failed: " << result.error << "\n";
        return 1;
    }
}

int main(int argc, char* argv[]) {
    std::string mind_path = default_mind_path();
    std::string model_path;
    std::string vocab_path;
    std::string command;
    std::string query;
    std::string format;  // For convert command
    int limit = 5;
    bool json_output = false;
    bool fast_mode = false;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            mind_path = argv[++i];
        } else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            model_path = argv[++i];
        } else if (strcmp(argv[i], "--vocab") == 0 && i + 1 < argc) {
            vocab_path = argv[++i];
        } else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            limit = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--json") == 0) {
            json_output = true;
        } else if (strcmp(argv[i], "--fast") == 0) {
            fast_mode = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            std::cout << "chitta " << CHITTA_VERSION << "\n";
            return 0;
        } else if (argv[i][0] != '-') {
            if (command.empty()) {
                command = argv[i];
            } else if (command == "recall" && query.empty()) {
                query = argv[i];
            } else if (command == "convert" && format.empty()) {
                format = argv[i];
            }
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (command.empty() || command == "help") {
        print_usage(argv[0]);
        return 0;
    }

    // Handle upgrade command separately (doesn't need mind.open())
    if (command == "upgrade") {
        return cmd_upgrade(mind_path);
    }

    // Handle convert command (doesn't need mind.open())
    if (command == "convert") {
        if (format.empty()) {
            std::cerr << "Usage: chitta_cli convert <format>\n";
            std::cerr << "Formats: unified, segments\n";
            return 1;
        }
        return cmd_convert(mind_path, format);
    }

    // Create and open mind
    MindConfig config;
    config.path = mind_path;
    config.skip_bm25 = fast_mode;
    Mind mind(config);

#ifdef CHITTA_WITH_ONNX
    // Try to attach yantra for semantic operations
    if (model_path.empty()) model_path = default_model_path();
    if (vocab_path.empty()) vocab_path = default_vocab_path();

    AntahkaranaYantra::Config yantra_config;
    yantra_config.pooling = PoolingStrategy::Mean;
    yantra_config.normalize_embeddings = true;

    auto yantra = std::make_shared<AntahkaranaYantra>(yantra_config);
    if (yantra->awaken(model_path, vocab_path)) {
        mind.attach_yantra(yantra);
    }
#endif

    if (!mind.open()) {
        std::cerr << "Error: Failed to open mind at " << mind_path << "\n";
        return 1;
    }

    // Execute command
    int result = 0;
    if (command == "stats") {
        result = cmd_stats(mind, json_output);
    } else if (command == "recall") {
        if (query.empty()) {
            std::cerr << "Usage: chitta_cli recall <query>\n";
            result = 1;
        } else {
            result = cmd_recall(mind, query, limit);
        }
    } else if (command == "cycle") {
        result = cmd_cycle(mind);
    } else {
        std::cerr << "Unknown command: " << command << "\n";
        print_usage(argv[0]);
        result = 1;
    }

    mind.close();
    return result;
}
