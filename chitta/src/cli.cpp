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
#include <chitta/socket_server.hpp>
#include <chitta/socket_client.hpp>
#include <chitta/rpc/handler.hpp>
#ifdef CHITTA_WITH_ONNX
#include <chitta/vak_onnx.hpp>
#endif
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <thread>
#include <chrono>
#include <atomic>
#include <fstream>
#include <sstream>

using namespace chitta;

// Generate stats JSON for daemon socket endpoint and CLI
std::string generate_stats_json(Mind& mind) {
    auto coherence = mind.coherence();
    auto health = mind.health();

    std::ostringstream oss;
    oss << "{"
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
        << "}";
    return oss.str();
}

void print_usage(const char* prog) {
    std::cerr << "chitta " << CHITTA_VERSION << "\n\n"
              << "Usage: " << prog << " <command> [options]\n\n"
              << "Commands:\n"
              << "  stats              Show soul statistics\n"
              << "  recall <query>     Semantic search\n"
              << "  resonate <query>   Full resonance (all phases)\n"
              << "  observe            Store observation: --category C --title T --content X\n"
              << "  grow               Store wisdom: --level L --title T --content X\n"
              << "  ledger             Save/load ledger: --action save|load [--session S]\n"
              << "  connect            Create relationship: --from A --rel R --to B\n"
              << "  query              Query triplets: --subj A --pred R --obj B\n"
              << "  cycle              Run maintenance cycle\n"
              << "  daemon             Run subconscious daemon (background processing)\n"
              << "  shutdown           Gracefully stop the running daemon\n"
              << "  status             Check if daemon is running\n"
              << "  upgrade            Upgrade database to current version\n"
              << "  convert <format>   Convert to new storage format (unified|segments)\n"
              << "  help               Show this help\n\n"
              << "Global options:\n"
              << "  --path PATH        Mind storage path (default: ~/.claude/mind/chitta)\n"
              << "  --json             Output as JSON\n"
              << "  --fast             Skip BM25 loading (for quick stats)\n"
              << "  --interval SECS    Daemon cycle interval (default: 60)\n"
              << "  --pid-file PATH    Write PID to file (for daemon mode)\n"
              << "  --socket           Enable socket server mode (daemon becomes IPC server)\n"
              << "  --socket-path PATH Unix socket path (default: /tmp/chitta-VERSION.sock)\n"
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
    if (json_output) {
        std::cout << generate_stats_json(mind) << "\n";
    } else {
        auto coherence = mind.coherence();
        auto health = mind.health();

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

int cmd_resonate(Mind& mind, const std::string& query, int limit, bool json_output) {
    if (!mind.has_yantra()) {
        std::cerr << "Error: Yantra not attached, semantic search unavailable\n";
        return 1;
    }

    auto results = mind.full_resonate(query, limit);

    if (json_output) {
        std::cout << "{\"query\":" << "\"" << query << "\",\"results\":[";
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            if (i > 0) std::cout << ",";
            std::cout << "{\"relevance\":" << r.relevance
                      << ",\"similarity\":" << r.similarity
                      << ",\"text\":\"";
            // Simple JSON escape for text
            for (char c : r.text) {
                if (c == '"') std::cout << "\\\"";
                else if (c == '\\') std::cout << "\\\\";
                else if (c == '\n') std::cout << "\\n";
                else if (c == '\r') std::cout << "\\r";
                else if (c == '\t') std::cout << "\\t";
                else std::cout << c;
            }
            std::cout << "\"}";
        }
        std::cout << "]}\n";
    } else {
        if (results.empty()) {
            std::cout << "No resonant memories for: " << query << "\n";
            return 0;
        }

        for (const auto& r : results) {
            // Truncate long text for hook output
            std::string text = r.text;
            if (text.length() > 200) {
                text = text.substr(0, 200) + "...";
            }
            std::cout << text << "\n";
        }
    }

    return 0;
}

int cmd_connect(Mind& mind, const std::string& from, const std::string& rel,
                const std::string& to, float weight) {
    if (from.empty() || rel.empty() || to.empty()) {
        std::cerr << "Usage: chitta connect --from SUBJECT --rel PREDICATE --to OBJECT [--weight W]\n";
        return 1;
    }

    mind.connect(from, rel, to, weight);
    std::cout << "Connected: (" << from << ") --[" << rel << "]--> (" << to << ")\n";
    return 0;
}

int cmd_query(Mind& mind, const std::string& subj, const std::string& pred,
              const std::string& obj, bool json_output) {
    // Use dictionary-encoded GraphStore (returns entity names, not NodeIds)
    auto triplets = mind.query_graph(subj, pred, obj);

    if (triplets.empty()) {
        std::cout << "No triplets found.\n";
        return 0;
    }

    if (json_output) {
        std::cout << "[";
        bool first = true;
        for (const auto& [s, p, o, w] : triplets) {
            if (!first) std::cout << ",";
            first = false;
            std::cout << "{\"subject\":\"" << s << "\","
                      << "\"predicate\":\"" << p << "\","
                      << "\"object\":\"" << o << "\","
                      << "\"weight\":" << w << "}";
        }
        std::cout << "]\n";
    } else {
        std::cout << "Found " << triplets.size() << " triplet(s):\n";
        for (const auto& [s, p, o, w] : triplets) {
            std::cout << "  " << s << " --[" << p << "]--> " << o
                      << " [w=" << w << "]\n";
        }
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

// Global flag for daemon shutdown
static std::atomic<bool> daemon_running{true};

void daemon_signal_handler(int sig) {
    (void)sig;
    daemon_running = false;
}

int cmd_daemon(Mind& mind, int interval_seconds, const std::string& pid_file) {
    // Write PID file
    if (!pid_file.empty()) {
        std::ofstream pf(pid_file);
        if (pf) {
            pf << getpid() << "\n";
            pf.close();
        }
    }

    // Setup signal handlers for graceful shutdown
    std::signal(SIGTERM, daemon_signal_handler);
    std::signal(SIGINT, daemon_signal_handler);

    std::cerr << "[subconscious] Daemon started (interval=" << interval_seconds << "s, pid=" << getpid() << ")\n";

    size_t cycle_count = 0;
    size_t total_synthesized = 0;
    size_t total_settled = 0;

    while (daemon_running) {
        // Sleep in small intervals to check for shutdown
        for (int i = 0; i < interval_seconds && daemon_running; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (!daemon_running) break;

        cycle_count++;
        auto start = std::chrono::steady_clock::now();

        // Subconscious processing cycle
        // 1. Apply decay and basic maintenance
        auto report = mind.tick();

        // 2. Synthesize wisdom from episode clusters
        size_t synthesized = mind.synthesize_wisdom();
        total_synthesized += synthesized;

        // 3. Apply pending feedback (Hebbian learning from usage)
        size_t feedback = mind.apply_feedback();

        // 4. Run attractor dynamics - settle nodes toward conceptual gravity wells
        auto attractor_report = mind.run_attractor_dynamics(5, 0.01f);
        total_settled += attractor_report.nodes_settled;

        // 5. Save state
        mind.snapshot();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        // Log activity (sparse - only when something happened)
        if (synthesized > 0 || feedback > 0 || attractor_report.nodes_settled > 0) {
            std::cerr << "[subconscious] Cycle " << cycle_count << ": "
                      << "synth=" << synthesized << " feedback=" << feedback
                      << " settled=" << attractor_report.nodes_settled
                      << " (" << elapsed << "ms)\n";
        }
    }

    // Cleanup
    if (!pid_file.empty()) {
        std::remove(pid_file.c_str());
    }

    std::cerr << "[subconscious] Daemon stopped (cycles=" << cycle_count
              << " synthesized=" << total_synthesized
              << " settled=" << total_settled << ")\n";

    return 0;
}

// Socket server mode: daemon + RPC handler over Unix socket
int cmd_daemon_with_socket(Mind& mind, int interval_seconds,
                           const std::string& pid_file,
                           const std::string& socket_path) {
    // Write PID file
    if (!pid_file.empty()) {
        std::ofstream pf(pid_file);
        if (pf) {
            pf << getpid() << "\n";
            pf.close();
        }
    }

    // Start socket server
    SocketServer server(socket_path);
    if (!server.start()) {
        std::cerr << "[daemon] Failed to start socket server on " << socket_path << "\n";
        return 1;
    }

    // Create RPC request handler
    rpc::Handler handler(&mind);

    // Setup signal handlers
    std::signal(SIGTERM, daemon_signal_handler);
    std::signal(SIGINT, daemon_signal_handler);

    std::cerr << "[daemon] Started (socket=" << socket_path
              << ", interval=" << interval_seconds << "s, pid=" << getpid() << ")\n";

    size_t cycle_count = 0;
    auto last_maintenance = std::chrono::steady_clock::now();
    auto maintenance_interval = std::chrono::seconds(interval_seconds);

    while (daemon_running) {
        // Poll for socket activity (100ms timeout for responsiveness)
        auto requests = server.poll(100);

        // Process all pending requests
        for (const auto& req : requests) {
            // Handle special "stats" request (for cc-status integration)
            if (req.data == "stats") {
                server.respond(req.client_fd, generate_stats_json(mind));
                continue;
            }

            // Handle graceful shutdown request (for version upgrades)
            if (req.data == "shutdown") {
                std::cerr << "[daemon] Shutdown requested, saving state...\n";
                server.respond(req.client_fd, R"({"status":"shutting_down","version":")" + std::string(CHITTA_VERSION) + R"("})");
                mind.snapshot();
                daemon_running = false;
                continue;
            }

            try {
                auto response = handler.handle(req.data);
                server.respond(req.client_fd, response);
            } catch (const std::exception& e) {
                std::string error = R"({"jsonrpc":"2.0","error":{"code":-32603,"message":")"
                                  + std::string(e.what()) + R"("},"id":null})";
                server.respond(req.client_fd, error);
            }
        }

        // Check if it's time for maintenance
        auto now_time = std::chrono::steady_clock::now();
        if (now_time - last_maintenance >= maintenance_interval) {
            last_maintenance = now_time;
            cycle_count++;

            auto start = std::chrono::steady_clock::now();

            // Subconscious processing
            auto report = mind.tick();
            size_t synthesized = mind.synthesize_wisdom();
            size_t feedback = mind.apply_feedback();
            auto attractor_report = mind.run_attractor_dynamics(5, 0.01f);
            mind.snapshot();

            // Coherence monitoring (webhook-ready)
            auto coherence = mind.coherence();
            static float last_tau = 1.0f;
            float tau = coherence.tau_k();

            // Alert on significant coherence drop
            if (tau < 0.5f && last_tau >= 0.5f) {
                std::cerr << "[daemon] WARNING: Coherence dropped below 50% (tau="
                          << static_cast<int>(tau * 100) << "%)\n";
                // Future: webhook call here
            } else if (tau < 0.3f) {
                std::cerr << "[daemon] CRITICAL: Coherence very low (tau="
                          << static_cast<int>(tau * 100) << "%)\n";
            }
            last_tau = tau;

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

            // Log activity (sparse)
            if (synthesized > 0 || feedback > 0 || attractor_report.nodes_settled > 0) {
                std::cerr << "[daemon] Cycle " << cycle_count << ": "
                          << "synth=" << synthesized << " feedback=" << feedback
                          << " settled=" << attractor_report.nodes_settled
                          << " tau=" << static_cast<int>(tau * 100) << "%"
                          << " clients=" << server.connection_count()
                          << " (" << elapsed << "ms)\n";
            }
        }
    }

    // Cleanup
    server.stop();

    if (!pid_file.empty()) {
        std::remove(pid_file.c_str());
    }

    std::cerr << "[daemon] Stopped (cycles=" << cycle_count << ")\n";
    return 0;
}

int cmd_shutdown(const std::string& socket_path) {
    // Use the socket client to request graceful shutdown
    SocketClient client(socket_path);

    if (!client.connect()) {
        std::cerr << "No daemon running (could not connect to socket)\n";
        return 1;
    }

    if (client.request_shutdown()) {
        std::cout << "Daemon shutdown requested\n";
        // Wait for socket to disappear (indicates daemon stopped)
        if (client.wait_for_socket_gone(5000)) {
            std::cout << "Daemon stopped\n";
            return 0;
        } else {
            std::cerr << "Warning: shutdown requested but socket still exists\n";
            return 0;  // Still consider success since we sent the request
        }
    } else {
        std::cerr << "Failed to request shutdown\n";
        return 1;
    }
}

int cmd_status(const std::string& socket_path) {
    SocketClient client(socket_path);

    if (!client.connect()) {
        std::cout << "Daemon: not running\n";
        std::cout << "Socket: " << socket_path << " (not found)\n";
        return 1;
    }

    auto version = client.check_version();
    if (version) {
        std::cout << "Daemon: running\n";
        std::cout << "Socket: " << socket_path << "\n";
        std::cout << "Version: " << version->software << "\n";
        std::cout << "Protocol: " << version->protocol_major << "." << version->protocol_minor << "\n";
        return 0;
    }
    std::cout << "Daemon: running (version unknown)\n";
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
    std::string pid_file;  // For daemon mode
    std::string socket_path = SocketServer::SOCKET_PATH;

    // Connect/query args
    std::string conn_from, conn_rel, conn_to;  // connect --from --rel --to
    std::string q_subj, q_pred, q_obj;          // query --subj --pred --obj
    float conn_weight = 1.0f;

    int limit = 5;
    int daemon_interval = 60;
    bool json_output = false;
    bool fast_mode = false;
    bool socket_mode = false;

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
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            daemon_interval = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--pid-file") == 0 && i + 1 < argc) {
            pid_file = argv[++i];
        } else if (strcmp(argv[i], "--json") == 0) {
            json_output = true;
        } else if (strcmp(argv[i], "--fast") == 0) {
            fast_mode = true;
        } else if (strcmp(argv[i], "--socket") == 0) {
            socket_mode = true;
        } else if (strcmp(argv[i], "--socket-path") == 0 && i + 1 < argc) {
            socket_path = argv[++i];
            socket_mode = true;  // Implies socket mode
        // Connect command args
        } else if (strcmp(argv[i], "--from") == 0 && i + 1 < argc) {
            conn_from = argv[++i];
        } else if (strcmp(argv[i], "--rel") == 0 && i + 1 < argc) {
            conn_rel = argv[++i];
        } else if (strcmp(argv[i], "--to") == 0 && i + 1 < argc) {
            conn_to = argv[++i];
        } else if (strcmp(argv[i], "--weight") == 0 && i + 1 < argc) {
            conn_weight = std::stof(argv[++i]);
        // Query command args
        } else if (strcmp(argv[i], "--subj") == 0 && i + 1 < argc) {
            q_subj = argv[++i];
        } else if (strcmp(argv[i], "--pred") == 0 && i + 1 < argc) {
            q_pred = argv[++i];
        } else if (strcmp(argv[i], "--obj") == 0 && i + 1 < argc) {
            q_obj = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            std::cout << "chitta " << CHITTA_VERSION << "\n";
            return 0;
        } else if (argv[i][0] != '-') {
            if (command.empty()) {
                command = argv[i];
            } else if ((command == "recall" || command == "resonate") && query.empty()) {
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
    } else if (command == "resonate") {
        if (query.empty()) {
            std::cerr << "Usage: chitta_cli resonate <query>\n";
            result = 1;
        } else {
            result = cmd_resonate(mind, query, limit, json_output);
        }
    } else if (command == "cycle") {
        result = cmd_cycle(mind);
    } else if (command == "connect") {
        result = cmd_connect(mind, conn_from, conn_rel, conn_to, conn_weight);
    } else if (command == "query") {
        result = cmd_query(mind, q_subj, q_pred, q_obj, json_output);
    } else if (command == "daemon") {
        if (socket_mode) {
            result = cmd_daemon_with_socket(mind, daemon_interval, pid_file, socket_path);
        } else {
            result = cmd_daemon(mind, daemon_interval, pid_file);
        }
    } else if (command == "shutdown") {
        // Shutdown doesn't need mind - just connects to daemon socket
        mind.close();
        return cmd_shutdown(socket_path);
    } else if (command == "status") {
        // Status doesn't need mind - just connects to daemon socket
        mind.close();
        return cmd_status(socket_path);
    } else {
        std::cerr << "Unknown command: " << command << "\n";
        print_usage(argv[0]);
        result = 1;
    }

    mind.close();
    return result;
}
