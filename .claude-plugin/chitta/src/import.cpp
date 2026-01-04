// chitta-import: Incrementally import nodes from another chitta database
//
// Usage: chitta_import [OPTIONS]
//
// Options:
//   --source PATH     Path to source chitta (base path, e.g., friend.chitta)
//   --target PATH     Path to target chitta (default: ~/.claude/mind/chitta)
//   --model PATH      Path to ONNX model for embeddings
//   --vocab PATH      Path to vocabulary file
//   --dry-run         Show what would be imported
//   --verbose         Show detailed progress

#include <chitta/mind.hpp>
#ifdef CHITTA_WITH_ONNX
#include <chitta/vak_onnx.hpp>
#endif
#include <iostream>
#include <string>
#include <cstring>
#include <fstream>

using namespace chitta;

struct ImportStats {
    size_t wisdom = 0;
    size_t beliefs = 0;
    size_t failures = 0;
    size_t episodes = 0;
    size_t aspirations = 0;
    size_t terms = 0;
    size_t questions = 0;
    size_t other = 0;
    size_t skipped = 0;

    size_t total() const {
        return wisdom + beliefs + failures + episodes +
               aspirations + terms + questions + other;
    }
};

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --source PATH     Path to source chitta (base path without .hot/.cold)\n"
              << "  --target PATH     Path to target chitta (default: ~/.claude/mind/chitta)\n"
#ifdef CHITTA_WITH_ONNX
              << "  --model PATH      Path to ONNX model for embeddings\n"
              << "  --vocab PATH      Path to vocabulary file\n"
#endif
              << "  --dry-run         Show what would be imported\n"
              << "  --verbose, -v     Show detailed progress\n"
              << "  --help, -h        Show this help\n";
}

std::string default_target() {
    const char* home = std::getenv("HOME");
    if (!home) home = ".";
    return std::string(home) + "/.claude/mind/chitta";
}

const char* node_type_name(NodeType type) {
    switch (type) {
        case NodeType::Wisdom: return "wisdom";
        case NodeType::Belief: return "belief";
        case NodeType::Failure: return "failure";
        case NodeType::Episode: return "episode";
        case NodeType::Aspiration: return "aspiration";
        case NodeType::Term: return "term";
        case NodeType::Question: return "question";
        default: return "other";
    }
}

int main(int argc, char* argv[]) {
    std::string source_path;
    std::string target_path = default_target();
    std::string model_path;
    std::string vocab_path;
    bool dry_run = false;
    bool verbose = false;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--source") == 0 && i + 1 < argc) {
            source_path = argv[++i];
        } else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            target_path = argv[++i];
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

    if (source_path.empty()) {
        std::cerr << "Error: --source is required\n";
        print_usage(argv[0]);
        return 1;
    }

    std::cout << "┌─────────────────────────────────────────┐\n";
    std::cout << "│       chitta-import                     │\n";
    std::cout << "│   Incremental import from chitta        │\n";
    std::cout << "└─────────────────────────────────────────┘\n\n";
    std::cout << "Source:  " << source_path << "\n";
    std::cout << "Target:  " << target_path << "\n";
    if (dry_run) {
        std::cout << "Mode:    DRY RUN (no changes)\n";
    }
    std::cout << "\n";

    // Check source files exist
    std::string source_hot = source_path + ".hot";
    std::ifstream test_hot(source_hot, std::ios::binary);
    if (!test_hot) {
        std::cerr << "Error: Source file not found: " << source_hot << "\n";
        return 1;
    }
    test_hot.close();

    // Open source mind (read-only, no yantra needed)
    MindConfig source_config;
    source_config.path = source_path;
    Mind source_mind(source_config);

    if (!source_mind.open()) {
        std::cerr << "Error: Failed to open source mind at " << source_path << "\n";
        return 1;
    }

    std::cout << "Source has " << (source_mind.hot_size() + source_mind.cold_size())
              << " nodes (" << source_mind.hot_size() << " hot, "
              << source_mind.cold_size() << " cold)\n\n";

    // Open target mind with yantra
    MindConfig target_config;
    target_config.path = target_path;
    Mind target_mind(target_config);

#ifdef CHITTA_WITH_ONNX
    if (!model_path.empty() && !vocab_path.empty()) {
        AntahkaranaYantra::Config yantra_config;
        yantra_config.pooling = PoolingStrategy::Mean;
        yantra_config.normalize_embeddings = true;

        auto yantra = std::make_shared<AntahkaranaYantra>(yantra_config);
        if (yantra->awaken(model_path, vocab_path)) {
            target_mind.attach_yantra(yantra);
            if (verbose) std::cout << "Yantra attached to target\n";
        } else {
            std::cerr << "Warning: Failed to awaken yantra: " << yantra->error() << "\n";
            std::cerr << "Continuing without embeddings (text-only import)...\n\n";
        }
    } else if (!dry_run) {
        std::cerr << "Warning: No model/vocab provided. Target needs yantra for semantic search.\n\n";
    }
#endif

    if (!target_mind.open()) {
        std::cerr << "Error: Failed to open target mind at " << target_path << "\n";
        return 1;
    }

    size_t target_before = target_mind.hot_size() + target_mind.cold_size();
    std::cout << "Target has " << target_before << " nodes before import\n";

    // Import nodes from source to target
    ImportStats stats;

    // Use query_by_type to get all nodes of each type
    std::vector<NodeType> types = {
        NodeType::Wisdom, NodeType::Belief, NodeType::Failure,
        NodeType::Episode, NodeType::Aspiration, NodeType::Term,
        NodeType::Question
    };

    for (auto type : types) {
        auto nodes = source_mind.query_by_type(type);

        for (const auto& node : nodes) {
            std::string text(node.payload.begin(), node.payload.end());

            if (verbose && stats.total() % 100 == 0) {
                std::cout << "  Imported: " << stats.total() << "...\n";
            }

            if (!dry_run && target_mind.has_yantra()) {
                target_mind.remember(text, type, Confidence(node.kappa.mu));
            }

            switch (type) {
                case NodeType::Wisdom: stats.wisdom++; break;
                case NodeType::Belief: stats.beliefs++; break;
                case NodeType::Failure: stats.failures++; break;
                case NodeType::Episode: stats.episodes++; break;
                case NodeType::Aspiration: stats.aspirations++; break;
                case NodeType::Term: stats.terms++; break;
                case NodeType::Question: stats.questions++; break;
                default: stats.other++; break;
            }
        }
    }

    // Close minds
    source_mind.close();
    target_mind.close();

    // Report
    std::cout << "\nImport " << (dry_run ? "would import" : "complete") << ":\n";
    std::cout << "  Wisdom:      " << stats.wisdom << "\n";
    std::cout << "  Beliefs:     " << stats.beliefs << "\n";
    std::cout << "  Failures:    " << stats.failures << "\n";
    std::cout << "  Episodes:    " << stats.episodes << "\n";
    std::cout << "  Aspirations: " << stats.aspirations << "\n";
    std::cout << "  Terms:       " << stats.terms << "\n";
    std::cout << "  Questions:   " << stats.questions << "\n";
    if (stats.other > 0) {
        std::cout << "  Other:       " << stats.other << "\n";
    }
    std::cout << "  ───────────────────\n";
    std::cout << "  Total:       " << stats.total() << " nodes\n";

    if (!dry_run) {
        std::cout << "\nTarget now has " << (target_before + stats.total())
                  << " nodes (added " << stats.total() << ")\n";
    }

    return 0;
}
