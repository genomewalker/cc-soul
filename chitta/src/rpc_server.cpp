// Chitta CLI - Multi-mode memory operations
// Command-line interface for soul integration
//
// Modes:
//   CLI mode:    chitta <tool> [args...]  - Direct tool invocation
//   Thin client: chitta                   - Forward JSON-RPC to daemon
//   Direct mode: chitta --direct          - Standalone mode (legacy)
//
// CLI Examples:
//   chitta recall "query"
//   chitta recall "query" --zoom sparse
//   chitta soul_context
//   chitta observe --category decision --title "..." --content "..."
//   chitta grow --type wisdom --title "..." --content "..."
//
// Options:
//   --socket-path PATH  Unix socket path (default: /tmp/chitta.sock)
//   --direct            Direct mode: open storage locally (legacy)
//   --path PATH         Path to mind storage (direct mode only)
//   --model PATH        Path to ONNX model file (direct mode only)
//   --vocab PATH        Path to vocabulary file (direct mode only)
//   --json              CLI mode: output raw JSON instead of text

#include <chitta/rpc.hpp>
#include <chitta/socket_client.hpp>
#include <chitta/version.hpp>
#include <set>
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
        std::cerr << "[chitta] Signal received, state saved\n";
    }
    std::_Exit(0);
}

// Known tool names for CLI mode detection
static const std::set<std::string> KNOWN_TOOLS = {
    "recall", "resonate", "full_resonate", "recall_by_tag", "proactive_surface",
    "detect_contradictions",
    "grow", "observe", "update", "remove", "feedback", "connect", "query",
    "soul_context", "attractors", "lens", "lens_harmony",
    "intend", "wonder", "answer",
    "narrate", "ledger", "cycle", "version_check",
    // Phase 2 Core
    "multi_hop", "timeline", "causal_chain", "consolidate",
    // Phase 3 Analysis
    "propagate", "forget", "epistemic_state", "bias_scan",
    // Phase 3 Advanced
    "competence", "cross_project"
};

void print_usage(const char* prog) {
    std::cerr << "Usage:\n"
              << "  " << prog << " <tool> [args...]     Invoke tool directly\n"
              << "  " << prog << " [options]            Interactive mode (JSON-RPC)\n"
              << "\n"
              << "Examples:\n"
              << "  " << prog << " recall \"query\"\n"
              << "  " << prog << " recall \"query\" --zoom sparse\n"
              << "  " << prog << " soul_context\n"
              << "  " << prog << " observe --category decision --title \"...\" --content \"...\"\n"
              << "  " << prog << " grow --type wisdom --title \"...\" --content \"...\"\n"
              << "\n"
              << "Tools: recall, resonate, full_resonate, grow, observe, update,\n"
              << "       soul_context, attractors, lens, intend, wonder, answer,\n"
              << "       narrate, ledger, cycle, feedback, connect\n"
              << "\n"
              << "Options:\n"
              << "  --socket-path PATH  Unix socket path (default: /tmp/chitta-VERSION.sock)\n"
              << "  --direct            Direct mode: open storage locally (legacy)\n"
              << "  --path PATH         Path to mind storage (direct mode only)\n"
#ifdef CHITTA_WITH_ONNX
              << "  --model PATH        Path to ONNX model file (direct mode only)\n"
              << "  --vocab PATH        Path to vocabulary file (direct mode only)\n"
#endif
              << "  --json              Output raw JSON instead of text\n"
              << "  --help              Show this help message\n";
}

// CLI mode: invoke tool directly
int run_cli(const std::string& socket_path, const std::string& tool,
            int argc, char* argv[], int arg_start, bool json_output) {
    using json = nlohmann::json;

    // Build arguments JSON from command line
    json args = json::object();
    std::string positional_key;  // First positional arg goes to "query" for recall, etc.

    // Determine the primary positional key based on tool
    if (tool == "recall" || tool == "resonate" || tool == "full_resonate" || tool == "multi_hop") {
        positional_key = "query";
    } else if (tool == "grow") {
        positional_key = "title";
    } else if (tool == "observe") {
        positional_key = "title";
    } else if (tool == "lens") {
        positional_key = "query";
    } else if (tool == "wonder") {
        positional_key = "question";
    } else if (tool == "causal_chain") {
        positional_key = "effect_id";
    }

    bool found_positional = false;
    for (int i = arg_start; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg.rfind("--", 0) == 0) {
            // Named argument: --key value
            std::string key = arg.substr(2);
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                std::string value = argv[++i];
                // Try to parse as JSON object/array, number, or boolean
                if (value == "true") {
                    args[key] = true;
                } else if (value == "false") {
                    args[key] = false;
                } else if (!value.empty() && (value[0] == '{' || value[0] == '[')) {
                    // Try to parse as JSON object or array
                    try {
                        args[key] = json::parse(value);
                    } catch (...) {
                        args[key] = value;  // Fall back to string
                    }
                } else {
                    // Only parse as number if entire string is numeric
                    bool is_numeric = !value.empty();
                    bool has_dot = false;
                    for (size_t j = 0; j < value.size(); ++j) {
                        char c = value[j];
                        if (c == '-' && j == 0) continue;  // Leading minus OK
                        if (c == '.' && !has_dot) { has_dot = true; continue; }
                        if (!std::isdigit(c)) { is_numeric = false; break; }
                    }
                    if (is_numeric && !value.empty()) {
                        try {
                            if (has_dot) {
                                args[key] = std::stod(value);
                            } else {
                                args[key] = std::stoll(value);
                            }
                        } catch (...) {
                            args[key] = value;
                        }
                    } else {
                        args[key] = value;
                    }
                }
            } else {
                args[key] = true;  // Flag without value
            }
        } else if (!found_positional && !positional_key.empty()) {
            // First positional argument
            args[positional_key] = arg;
            found_positional = true;
        }
    }

    // Connect to daemon
    chitta::SocketClient client(socket_path);
    if (!client.ensure_daemon_running()) {
        std::cerr << "Error: Cannot connect to daemon: " << client.last_error() << "\n";
        return 1;
    }

    // Send initialize
    json init_req = {
        {"jsonrpc", "2.0"},
        {"method", "initialize"},
        {"params", {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", json::object()},
            {"clientInfo", {{"name", "chitta_cli"}, {"version", CHITTA_VERSION}}}
        }},
        {"id", 0}
    };
    auto init_resp = client.request(init_req.dump());
    if (!init_resp) {
        std::cerr << "Error: Initialize failed: " << client.last_error() << "\n";
        return 1;
    }

    // Send tool call
    json tool_req = {
        {"jsonrpc", "2.0"},
        {"method", "tools/call"},
        {"params", {
            {"name", tool},
            {"arguments", args}
        }},
        {"id", 1}
    };

    auto resp = client.request(tool_req.dump());
    if (!resp) {
        std::cerr << "Error: Tool call failed: " << client.last_error() << "\n";
        return 1;
    }

    // Parse and output result
    try {
        auto result = json::parse(*resp);

        if (result.contains("error")) {
            std::cerr << "Error: " << result["error"]["message"].get<std::string>() << "\n";
            return 1;
        }

        if (json_output) {
            // Raw JSON output
            if (result.contains("result") && result["result"].contains("structured")) {
                std::cout << result["result"]["structured"].dump(2) << "\n";
            } else {
                std::cout << result.dump(2) << "\n";
            }
        } else {
            // Text output
            if (result.contains("result") && result["result"].contains("content")) {
                auto& content = result["result"]["content"];
                if (content.is_array() && !content.empty() && content[0].contains("text")) {
                    std::cout << content[0]["text"].get<std::string>() << "\n";
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing response: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

// Thin client mode: forward stdin → daemon → stdout
int run_thin_client(const std::string& socket_path) {
    chitta::SocketClient client(socket_path);

    // Try to connect, auto-start daemon if needed
    if (!client.ensure_daemon_running()) {
        std::cerr << "[chitta] Failed to connect to daemon: " << client.last_error() << "\n";
        return 1;
    }

    std::cerr << "[chitta] Connected to daemon at " << socket_path << "\n";
    std::cerr << "[chitta] Listening on stdin...\n";

    // Forward requests
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        auto response = client.request(line);
        if (response) {
            std::cout << *response << "\n";
            std::cout.flush();
        } else {
            std::cerr << "[chitta] Request failed: " << client.last_error() << "\n";

            // Try to reconnect
            client.disconnect();
            if (!client.ensure_daemon_running()) {
                std::cerr << "[chitta] Reconnection failed, exiting\n";
                return 1;
            }
            std::cerr << "[chitta] Reconnected to daemon\n";

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

    std::cerr << "[chitta] Shutdown complete\n";
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
                std::cerr << "[chitta] Yantra attached: " << model_path << "\n";
            } else {
                std::cerr << "[chitta] Warning: Failed to awaken yantra: "
                          << yantra->error() << "\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "[chitta] Warning: Failed to load yantra: " << e.what() << "\n";
        }
    }
#else
    (void)model_path;
    (void)vocab_path;
#endif

    // Open mind
    if (!mind->open()) {
        std::cerr << "[chitta] Error: Failed to open mind at " << mind_path << "\n";
        return 1;
    }

    // Set up signal handling for graceful shutdown
    g_mind = mind;
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGHUP, signal_handler);

    std::cerr << "[chitta] Direct mode: Mind opened: " << mind->size() << " nodes\n";
    std::cerr << "[chitta] Yantra ready: " << (mind->has_yantra() ? "yes" : "no") << "\n";
    std::cerr << "[chitta] Listening on stdin...\n";

    // Run server (JSON-RPC protocol)
    chitta::RpcServer server(mind, "chitta");
    server.run();

    // Cleanup
    mind->close();
    std::cerr << "[chitta] Shutdown complete\n";

    return 0;
}

int main(int argc, char* argv[]) {
    std::string socket_path = chitta::SocketClient::SOCKET_PATH;
    std::string mind_path = "./mind";
    std::string model_path;
    std::string vocab_path;
    bool direct_mode = false;
    bool json_output = false;

    // Honor CHITTA_DB_PATH env var
    if (const char* env_path = std::getenv("CHITTA_DB_PATH")) {
        mind_path = env_path;
    }

    // Handle shutdown command specially (not a tool, direct daemon control)
    if (argc > 1 && std::strcmp(argv[1], "shutdown") == 0) {
        chitta::SocketClient client(socket_path);
        if (!client.connect()) {
            std::cerr << "No daemon running\n";
            return 1;
        }
        if (client.request_shutdown()) {
            std::cout << "Daemon shutdown requested\n";
            if (client.wait_for_socket_gone(5000)) {
                std::cout << "Daemon stopped\n";
            }
            return 0;
        }
        std::cerr << "Failed to request shutdown\n";
        return 1;
    }

    // Check for CLI mode: first arg is a known tool name
    if (argc > 1 && KNOWN_TOOLS.count(argv[1])) {
        std::string tool = argv[1];

        // Check for --json flag anywhere in args
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--json") == 0) {
                json_output = true;
            }
        }

        return run_cli(socket_path, tool, argc, argv, 2, json_output);
    }

    // Parse arguments for interactive mode
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
        } else if (std::strcmp(argv[i], "--json") == 0) {
            json_output = true;  // Ignored in RPC mode
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
