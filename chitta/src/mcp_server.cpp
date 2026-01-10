// Chitta MCP Server - Thin Client Mode
// Model Context Protocol server for soul integration with Claude
//
// Default mode: Thin client that forwards requests to daemon via Unix socket
// Direct mode:  Standalone server that opens storage directly (--direct flag)
//
// Usage:
//   chitta_mcp [options]
//
// Options:
//   --socket-path PATH  Unix socket path (default: /tmp/chitta.sock)
//   --direct            Direct mode: open storage locally (legacy)
//   --path PATH         Path to mind storage (direct mode only)
//   --model PATH        Path to ONNX model file (direct mode only)
//   --vocab PATH        Path to vocabulary file (direct mode only)

#include <chitta/mcp.hpp>
#include <chitta/socket_client.hpp>
#include <chitta/version.hpp>
#ifdef CHITTA_WITH_ONNX
#include <chitta/vak_onnx.hpp>
#endif
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <atomic>

// Global state for signal handler
static std::shared_ptr<chitta::Mind> g_mind;
static std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int sig) {
    (void)sig;
    g_shutdown_requested.store(true);
    if (g_mind) {
        g_mind->close();
        std::cerr << "[chitta_mcp] Signal received, state saved\n";
    }
    std::_Exit(0);
}

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --socket-path PATH  Unix socket path (default: /tmp/chitta-VERSION.sock)\n"
              << "  --direct            Direct mode: open storage locally (legacy)\n"
              << "  --path PATH         Path to mind storage (direct mode only)\n"
#ifdef CHITTA_WITH_ONNX
              << "  --model PATH        Path to ONNX model file (direct mode only)\n"
              << "  --vocab PATH        Path to vocabulary file (direct mode only)\n"
#endif
              << "  --help              Show this help message\n"
              << "\n"
              << "Default: Thin client forwarding to daemon via socket.\n"
              << "Use --direct to open storage locally (legacy standalone mode).\n";
}

// Thin client mode: forward stdin → daemon → stdout
int run_thin_client(const std::string& socket_path) {
    chitta::SocketClient client(socket_path);

    // Try to connect, auto-start daemon if needed
    if (!client.ensure_daemon_running()) {
        std::cerr << "[chitta_mcp] Failed to connect to daemon: " << client.last_error() << "\n";
        return 1;
    }

    std::cerr << "[chitta_mcp] Connected to daemon at " << socket_path << "\n";
    std::cerr << "[chitta_mcp] Listening on stdin...\n";

    // Forward requests
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        auto response = client.request(line);
        if (response) {
            std::cout << *response << "\n";
            std::cout.flush();
        } else {
            std::cerr << "[chitta_mcp] Request failed: " << client.last_error() << "\n";

            // Try to reconnect
            client.disconnect();
            if (!client.ensure_daemon_running()) {
                std::cerr << "[chitta_mcp] Reconnection failed, exiting\n";
                return 1;
            }
            std::cerr << "[chitta_mcp] Reconnected to daemon\n";

            // Retry the request
            response = client.request(line);
            if (response) {
                std::cout << *response << "\n";
                std::cout.flush();
            } else {
                // Return error to client
                std::cout << R"({"jsonrpc":"2.0","error":{"code":-32603,"message":"Daemon connection lost"},"id":null})" << "\n";
                std::cout.flush();
            }
        }
    }

    std::cerr << "[chitta_mcp] Shutdown complete\n";
    return 0;
}

// Direct mode: open storage locally (legacy)
int run_direct(const std::string& mind_path,
               const std::string& model_path,
               const std::string& vocab_path) {
    // Create mind
    chitta::MindConfig config;
    config.path = mind_path;
    auto mind = std::make_shared<chitta::Mind>(config);

    // Attach ONNX yantra if available
#ifdef CHITTA_WITH_ONNX
    if (!model_path.empty() && !vocab_path.empty()) {
        try {
            chitta::AntahkaranaYantra::Config yantra_config;
            yantra_config.pooling = chitta::PoolingStrategy::Mean;
            yantra_config.normalize_embeddings = true;

            auto yantra = std::make_shared<chitta::AntahkaranaYantra>(yantra_config);
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

    // Set up signal handling for graceful shutdown
    g_mind = mind;
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGHUP, signal_handler);

    std::cerr << "[chitta_mcp] Direct mode: Mind opened: " << mind->size() << " nodes\n";
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

int main(int argc, char* argv[]) {
    std::string socket_path = chitta::SocketClient::default_socket_path();
    std::string mind_path = "./mind";
    std::string model_path;
    std::string vocab_path;
    bool direct_mode = false;

    // Honor CHITTA_DB_PATH env var
    if (const char* env_path = std::getenv("CHITTA_DB_PATH")) {
        mind_path = env_path;
    }

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--socket-path") == 0 && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (std::strcmp(argv[i], "--direct") == 0) {
            direct_mode = true;
        } else if (std::strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            mind_path = argv[++i];
            direct_mode = true;  // --path implies direct mode
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

    if (direct_mode) {
        return run_direct(mind_path, model_path, vocab_path);
    } else {
        return run_thin_client(socket_path);
    }
}
