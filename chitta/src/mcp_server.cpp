// Chitta MCP Server
// Model Context Protocol server for soul integration with Claude
//
// Usage:
//   chitta_mcp [--path /path/to/mind] [--model /path/to/model.onnx] [--vocab /path/to/vocab.txt]
//
// Reads JSON-RPC 2.0 requests from stdin, writes responses to stdout.

#include <chitta/mcp.hpp>
#include <chitta/version.hpp>
#ifdef CHITTA_WITH_ONNX
#include <chitta/vak_onnx.hpp>
#endif
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --path PATH    Path to mind storage (default: ./mind)\n"
#ifdef CHITTA_WITH_ONNX
              << "  --model PATH   Path to ONNX model file\n"
              << "  --vocab PATH   Path to vocabulary file\n"
#endif
              << "  --help         Show this help message\n"
              << "\n"
              << "Reads JSON-RPC 2.0 MCP requests from stdin, writes responses to stdout.\n";
}

int main(int argc, char* argv[]) {
    std::string mind_path = "./mind";
    std::string model_path;
    std::string vocab_path;

    // Honor CHITTA_DB_PATH env var (can be overridden by --path arg)
    if (const char* env_path = std::getenv("CHITTA_DB_PATH")) {
        mind_path = env_path;
    }

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            mind_path = argv[++i];
        } else if (std::strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            model_path = argv[++i];
        } else if (std::strcmp(argv[i], "--vocab") == 0 && i + 1 < argc) {
            vocab_path = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // Create mind
    chitta::MindConfig config;
    config.path = mind_path;
    auto mind = std::make_shared<chitta::Mind>(config);

    // Attach ONNX yantra if available
#ifdef CHITTA_WITH_ONNX
    if (!model_path.empty() && !vocab_path.empty()) {
        try {
            chitta::AntahkaranaYantra::Config config;
            config.pooling = chitta::PoolingStrategy::Mean;
            config.normalize_embeddings = true;

            auto yantra = std::make_shared<chitta::AntahkaranaYantra>(config);
            if (yantra->awaken(model_path, vocab_path)) {
                mind->attach_yantra(yantra);
                std::cerr << "[chitta_mcp] Yantra attached: " << model_path << "\n";
            } else {
                std::cerr << "[chitta_mcp] Warning: Failed to awaken yantra: "
                          << yantra->error() << "\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "[chitta_mcp] Warning: Failed to load yantra: " << e.what() << "\n";
        }
    }
#else
    (void)model_path;
    (void)vocab_path;
#endif

    // Open mind
    if (!mind->open()) {
        std::cerr << "[chitta_mcp] Error: Failed to open mind at " << mind_path << "\n";
        return 1;
    }

    std::cerr << "[chitta_mcp] Mind opened: " << mind->size() << " nodes\n";
    std::cerr << "[chitta_mcp] Yantra ready: " << (mind->has_yantra() ? "yes" : "no") << "\n";
    std::cerr << "[chitta_mcp] Listening on stdin...\n";

    // Run MCP server
    chitta::MCPServer server(mind, "chitta");
    server.run();

    // Cleanup
    mind->close();
    std::cerr << "[chitta_mcp] Shutdown complete\n";

    return 0;
}
