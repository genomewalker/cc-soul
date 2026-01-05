// chitta-cli: Command-line interface for chitta soul operations
//
// Usage: chitta_cli <command> [options]
//
// Commands:
//   stats      Show soul statistics
//   recall     Semantic search
//   cycle      Run maintenance cycle
//   help       Show this help

#include <chitta/mind.hpp>
#ifdef CHITTA_WITH_ONNX
#include <chitta/vak_onnx.hpp>
#endif
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>

using namespace chitta;

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <command> [options]\n\n"
              << "Commands:\n"
              << "  stats              Show soul statistics\n"
              << "  recall <query>     Semantic search\n"
              << "  cycle              Run maintenance cycle\n"
              << "  help               Show this help\n\n"
              << "Global options:\n"
              << "  --path PATH        Mind storage path (default: ~/.claude/mind/chitta)\n"
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

int cmd_stats(Mind& mind) {
    auto coherence = mind.coherence();

    std::cout << "Soul Statistics\n";
    std::cout << "═══════════════════════════════\n";
    std::cout << "Nodes:\n";
    std::cout << "  Hot:    " << mind.hot_size() << "\n";
    std::cout << "  Warm:   " << mind.warm_size() << "\n";
    std::cout << "  Cold:   " << mind.cold_size() << "\n";
    std::cout << "  Total:  " << mind.size() << "\n";
    std::cout << "\nCoherence:\n";
    std::cout << "  Global:     " << coherence.global << "\n";
    std::cout << "  Local:      " << coherence.local << "\n";
    std::cout << "  Structural: " << coherence.structural << "\n";
    std::cout << "  Temporal:   " << coherence.temporal << "\n";
    std::cout << "  Tau-k:      " << coherence.tau_k() << "\n";
    std::cout << "\nYantra: " << (mind.has_yantra() ? "ready" : "not attached") << "\n";

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

int main(int argc, char* argv[]) {
    std::string mind_path = default_mind_path();
    std::string model_path;
    std::string vocab_path;
    std::string command;
    std::string query;
    int limit = 5;

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
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            if (command.empty()) {
                command = argv[i];
            } else if (command == "recall" && query.empty()) {
                query = argv[i];
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

    // Create and open mind
    MindConfig config;
    config.path = mind_path;
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
        result = cmd_stats(mind);
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
