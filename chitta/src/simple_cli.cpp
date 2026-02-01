// chitta-cli: Simplified daemon for SimpleMind
//
// Usage: chittad <command> [options]
//
// Commands:
//   daemon     Run background daemon
//   shutdown   Stop running daemon
//   status     Check daemon status
//   stats      Show soul statistics

#include <chitta/mind/duckdb_mind.hpp>
#include <chitta/mind/subconscious.hpp>
#include <chitta/rpc/duckdb_handler.hpp>
#include <chitta/rpc/thread_pool.hpp>
#ifdef CHITTA_WITH_POSTGRES
#include <chitta/mind/postgres_mind.hpp>
#include <chitta/rpc/postgres_handler.hpp>
#endif
#include <chitta/socket_server.hpp>
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
#include <thread>
#include <chrono>
#include <atomic>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

using namespace chitta;
using json = nlohmann::json;

// Global flags
static std::atomic<bool> daemon_running{true};
static std::atomic<bool> verbose_mode{false};

// Distillation configuration
struct DistillConfig {
    int interval_minutes = 5;       // How often to check for batches
    int min_turns = 4;              // Minimum turns before distilling
    std::string script_path;        // Path to distillation script
    std::string model = "github-copilot/gpt-5-mini";  // OpenCode model
    bool enabled = true;
};

// Code enrichment configuration (semantic descriptions for symbols)
struct EnrichConfig {
    int interval_minutes = 10;      // How often to process batches (was 2, increased to reduce load)
    int batch_size = 3;             // Symbols per batch (was 10, reduced to minimize blocking)
    int idle_seconds = 30;          // Only run if no queries for this long
    std::string script_path;        // Path to enrichment script
    std::string model = "github-copilot/gpt-5-mini";  // OpenCode model
    bool enabled = true;
};

void daemon_signal_handler(int sig) {
    std::cerr << "[daemon] Signal " << sig << " received, shutting down\n";
    daemon_running = false;
}

// Daemonize using double-fork
bool daemonize(const std::string& log_path) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid > 0) _exit(0);

    if (setsid() < 0) return false;

    pid = fork();
    if (pid < 0) return false;
    if (pid > 0) _exit(0);

    umask(0);
    chdir("/");

    int null_fd = open("/dev/null", O_RDONLY);
    if (null_fd >= 0) {
        dup2(null_fd, STDIN_FILENO);
        close(null_fd);
    }

    const char* out_path = log_path.empty() ? "/dev/null" : log_path.c_str();
    int log_fd = open(out_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd >= 0) {
        dup2(log_fd, STDOUT_FILENO);
        dup2(log_fd, STDERR_FILENO);
        close(log_fd);
    }

    return true;
}

// Lock management
struct DaemonLock {
    int fd = -1;
    std::string path;
};

bool acquire_lock(const std::string& mind_path, DaemonLock& lock) {
    lock.path = lock_path_for_mind(mind_path);
    lock.fd = open(lock.path.c_str(), O_CREAT | O_RDWR, 0600);
    if (lock.fd < 0) return false;

    struct flock fl;
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    if (fcntl(lock.fd, F_SETLK, &fl) != 0) {
        close(lock.fd);
        lock.fd = -1;
        return false;
    }

    std::string pid = std::to_string(getpid()) + "\n";
    ftruncate(lock.fd, 0);
    write(lock.fd, pid.data(), pid.size());
    return true;
}

void release_lock(DaemonLock& lock) {
    if (lock.fd >= 0) {
        close(lock.fd);
        unlink(lock.path.c_str());
    }
}

std::string default_mind_path() {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.claude/mind";
}

std::string default_model_path() {
    const char* home = std::getenv("HOME");
    std::string home_str = home ? home : ".";

    // Check plugin path first if set
    if (const char* plugin_root = std::getenv("CLAUDE_PLUGIN_ROOT")) {
        std::string plugin_path = std::string(plugin_root) + "/chitta/models/model.onnx";
        if (std::ifstream(plugin_path).good()) {
            return plugin_path;
        }
    }
    // Fall back to ~/.claude/models (where smart-install puts it)
    std::string models_path = home_str + "/.claude/models/model.onnx";
    if (std::ifstream(models_path).good()) {
        return models_path;
    }
    // Legacy path
    return home_str + "/.claude/mind/model.onnx";
}

std::string default_vocab_path() {
    const char* home = std::getenv("HOME");
    std::string home_str = home ? home : ".";

    // Check plugin path first if set
    if (const char* plugin_root = std::getenv("CLAUDE_PLUGIN_ROOT")) {
        std::string plugin_path = std::string(plugin_root) + "/chitta/models/vocab.txt";
        if (std::ifstream(plugin_path).good()) {
            return plugin_path;
        }
    }
    // Fall back to ~/.claude/models (where smart-install puts it)
    std::string models_path = home_str + "/.claude/models/vocab.txt";
    if (std::ifstream(models_path).good()) {
        return models_path;
    }
    // Legacy path
    return home_str + "/.claude/mind/vocab.txt";
}

std::string default_distill_script() {
    // Check plugin root first if set and file exists
    if (const char* plugin_root = std::getenv("CLAUDE_PLUGIN_ROOT")) {
        std::string plugin_path = std::string(plugin_root) + "/scripts/distill.sh";
        if (std::ifstream(plugin_path).good()) {
            return plugin_path;
        }
    }
    // Fall back to user hooks directory
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.claude/hooks/distill.sh";
}

std::string default_enrich_script() {
    // Check plugin root first if set and file exists
    if (const char* plugin_root = std::getenv("CLAUDE_PLUGIN_ROOT")) {
        std::string plugin_path = std::string(plugin_root) + "/scripts/enrich-code.sh";
        if (std::ifstream(plugin_path).good()) {
            return plugin_path;
        }
    }
    // Fall back to user hooks directory
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.claude/hooks/enrich-code.sh";
}

// Run distillation for a single transcript
// Returns true if distillation was successful
bool run_distillation(DuckDBMind& mind, const TranscriptState& state,
                      const DistillConfig& config) {
    // Parse transcript for new turns
    std::ifstream file(state.transcript_path);
    if (!file) {
        if (verbose_mode) {
            std::cerr << "[distill] Cannot open: " << state.transcript_path << "\n";
        }
        return false;
    }

    // Skip to last processed line
    std::string line;
    int64_t current_line = 0;
    while (current_line < state.last_processed_line && std::getline(file, line)) {
        current_line++;
    }

    // Parse new turns
    std::vector<std::pair<std::string, std::string>> turns;  // (role, content)
    int64_t last_line = state.last_processed_line;

    while (std::getline(file, line)) {
        current_line++;
        if (line.empty()) continue;

        try {
            auto entry = json::parse(line);
            std::string type = entry.value("type", "");
            if (type != "user" && type != "assistant") continue;

            std::string content;
            if (entry.contains("message")) {
                auto& msg = entry["message"];
                if (msg.contains("content")) {
                    auto& msg_content = msg["content"];
                    if (msg_content.is_string()) {
                        content = msg_content.get<std::string>();
                    } else if (msg_content.is_array()) {
                        for (const auto& block : msg_content) {
                            std::string block_type = block.value("type", "");
                            // Extract text blocks
                            if (block_type == "text" && block.contains("text")) {
                                std::string text = block["text"].get<std::string>();
                                // Filter out system-reminder noise
                                size_t pos;
                                while ((pos = text.find("<system-reminder>")) != std::string::npos) {
                                    size_t end = text.find("</system-reminder>", pos);
                                    if (end != std::string::npos) {
                                        text.erase(pos, end - pos + 18);
                                    } else {
                                        text.erase(pos);
                                    }
                                }
                                // Skip if only whitespace remains
                                if (text.find_first_not_of(" \t\n\r") == std::string::npos) continue;
                                if (!content.empty()) content += "\n";
                                content += text;
                            }
                            // Extract thinking blocks (valuable reasoning)
                            else if (block_type == "thinking" && block.contains("thinking")) {
                                std::string thinking = block["thinking"].get<std::string>();
                                // Only include substantial thinking (>100 chars)
                                if (thinking.size() > 100) {
                                    if (!content.empty()) content += "\n";
                                    content += "<thinking>\n" + thinking + "\n</thinking>";
                                }
                            }
                            // Skip tool_use, tool_result - just noise for distillation
                        }
                    }
                }
            }

            if (!content.empty()) {
                turns.emplace_back(type, content);
                last_line = current_line;
            }
        } catch (...) {
            continue;
        }
    }

    // Check if we have enough turns
    if (static_cast<int>(turns.size()) < config.min_turns) {
        return false;
    }

    if (verbose_mode) {
        std::cerr << "[distill] Session " << state.session_id
                  << ": " << turns.size() << " new turns\n";
    }

    // Build conversation text for distillation
    std::ostringstream conversation;
    for (const auto& [role, content] : turns) {
        conversation << "[" << role << "]\n" << content << "\n\n";
    }

    // Call distillation script if configured
    if (!config.script_path.empty()) {
        // Write conversation to temp file
        std::string temp_path = "/tmp/distill-" + state.session_id + ".txt";
        {
            std::ofstream temp(temp_path);
            if (temp) {
                temp << "SESSION_ID=" << state.session_id << "\n";
                temp << "REALM=" << state.realm << "\n";
                temp << "MODEL=" << config.model << "\n";
                temp << "---\n";
                temp << conversation.str();
            }
        }

        // Execute script non-blocking (fire and forget)
        // Script is responsible for cleanup of temp file
        pid_t pid = fork();
        if (pid == 0) {
            // Child process - detach and run script
            setsid();
            std::string cmd = config.script_path + " " + temp_path + " >/dev/null 2>&1; rm -f " + temp_path;
            execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
            _exit(1);
        } else if (pid < 0) {
            // Fork failed, cleanup
            std::remove(temp_path.c_str());
            if (verbose_mode) {
                std::cerr << "[distill] Fork failed for " << state.session_id << "\n";
            }
            return false;
        }
        // Parent continues immediately - don't wait
    }

    // Update progress (using thread-safe methods)
    mind.update_transcript_progress(state.session_id, last_line);
    mind.mark_transcript_distilled(state.session_id);

    if (verbose_mode) {
        std::cerr << "[distill] Completed " << state.session_id
                  << " (line " << last_line << ")\n";
    }

    return true;
}

// Generate stats JSON
std::string generate_stats(DuckDBMind& mind) {
    std::ostringstream oss;
    oss << "{"
        << "\"version\":\"" << CHITTA_VERSION << "\","
        << "\"nodes\":" << mind.size() << ","
        << "\"triplets\":" << mind.triplet_count() << ","
        << "\"yantra\":" << (mind.has_yantra() ? "true" : "false")
        << "}";
    return oss.str();
}

int cmd_daemon(DuckDBMind& mind, int interval, const std::string& socket_path,
               const std::string& mind_path, const std::string& pid_file,
               const DistillConfig& distill_config, EnrichConfig& enrich_config) {
    DaemonLock lock;
    if (!acquire_lock(mind_path, lock)) {
        std::cerr << "[daemon] Another daemon is running\n";
        return 1;
    }

    // Check if opencode is available for distillation
    if (distill_config.enabled) {
        int result = std::system("command -v opencode >/dev/null 2>&1");
        if (result != 0) {
            std::cerr << "[daemon] ERROR: opencode not found in PATH\n";
            std::cerr << "[daemon] Distillation requires opencode. Install it or use --no-distill\n";
            release_lock(lock);
            return 1;
        }
        std::cerr << "[daemon] Distillation enabled (interval=" << distill_config.interval_minutes
                  << "m, min_turns=" << distill_config.min_turns
                  << ", model=" << distill_config.model << ")\n";
    }

    // Check for enrichment (uses same opencode)
    if (enrich_config.enabled) {
        // opencode already checked above if distillation enabled; check here if only enrichment
        if (!distill_config.enabled) {
            int result = std::system("command -v opencode >/dev/null 2>&1");
            if (result != 0) {
                std::cerr << "[daemon] WARNING: opencode not found, disabling code enrichment\n";
                enrich_config.enabled = false;
            }
        }
        if (enrich_config.enabled) {
            size_t pending = mind.store().count_undescribed_symbols();
            std::cerr << "[daemon] Code enrichment enabled (interval=" << enrich_config.interval_minutes
                      << "m, batch=" << enrich_config.batch_size
                      << ", idle=" << enrich_config.idle_seconds << "s"
                      << ", pending=" << pending << ")\n";
        }
    }

    if (!pid_file.empty()) {
        std::ofstream pf(pid_file);
        if (pf) pf << getpid() << "\n";
    }

    SocketServer server(socket_path);
    if (!server.start()) {
        std::cerr << "[daemon] Failed to start socket server\n";
        release_lock(lock);
        return 1;
    }

    DuckDBRpcHandler handler(&mind);

    // Start subconscious background processor
    Subconscious subconscious(&mind);
    subconscious.start();
    handler.set_subconscious(&subconscious);

    std::signal(SIGTERM, daemon_signal_handler);
    std::signal(SIGINT, daemon_signal_handler);
    std::signal(SIGPIPE, SIG_IGN);

    std::cerr << "[daemon] Started (socket=" << socket_path
              << ", interval=" << interval << "s, pid=" << getpid() << ")\n";

    // Initial health check to populate cache
    auto initial_health = mind.health();
    std::cerr << "[daemon] Initial stats: " << initial_health.total_nodes << " memories, "
              << mind.triplet_count() << " triplets\n";

    // Maintenance thread - sync and apply decay periodically
    std::atomic<size_t> cycle_count{0};
    std::thread maintenance([&]() {
        auto interval_secs = std::chrono::seconds(interval);
        auto last_sync = std::chrono::steady_clock::now();
        auto last_embedding_flush = std::chrono::steady_clock::now();
        auto embedding_flush_interval = std::chrono::seconds(5);  // Flush queued embeddings every 5s

        while (daemon_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            auto now_time = std::chrono::steady_clock::now();

            // Flush embedding queue frequently (non-blocking if empty)
            if (now_time - last_embedding_flush >= embedding_flush_interval) {
                last_embedding_flush = now_time;
                try {
                    subconscious.flush_embedding_queue();
                } catch (const std::exception& e) {
                    std::cerr << "[maint] Embedding flush failed: " << e.what() << "\n";
                }
            }

            if (now_time - last_sync >= interval_secs) {
                last_sync = now_time;
                cycle_count++;

                try {
                    // Apply decay and prune weak nodes
                    size_t decayed = mind.tick();

                    // Sync to disk
                    mind.sync();

                    // Rebuild vector index if needed (deferred rebuild for stability)
                    if (mind.store().needs_reindex()) {
                        std::cerr << "[maint] Rebuilding vector index...\n";
                        mind.store().rebuild_vector_index();
                    }

                    // Update health cache (for fast health_check/soul_context)
                    auto health = mind.health();

                    if (verbose_mode) {
                        std::cerr << "[maint] Cycle " << cycle_count
                                  << ": " << health.total_nodes << " nodes"
                                  << ", " << health.active_nodes << " active"
                                  << ", " << decayed << " decayed"
                                  << ", status=" << health.status() << "\n";
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[maint] Cycle failed: " << e.what() << "\n";
                }
            }
        }
    });

    // Distillation thread - process transcripts periodically
    std::atomic<size_t> distill_count{0};
    std::thread distillation([&]() {
        if (!distill_config.enabled) return;

        auto interval_mins = std::chrono::minutes(distill_config.interval_minutes);
        auto last_distill = std::chrono::steady_clock::now();

        // Initial delay to let things settle
        std::this_thread::sleep_for(std::chrono::seconds(30));

        while (daemon_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            auto now_time = std::chrono::steady_clock::now();
            if (now_time - last_distill >= interval_mins) {
                // Skip if daemon is actively handling queries (prevent blocking)
                if (!subconscious.is_idle()) {
                    if (verbose_mode) {
                        std::cerr << "[distill] Skipping - daemon is busy\n";
                    }
                    continue;  // Don't update last_distill, try again next second
                }

                last_distill = now_time;

                try {
                    // Get all registered transcripts (thread-safe method)
                    auto transcripts = mind.get_pending_transcripts();

                    size_t processed = 0;
                    for (const auto& state : transcripts) {
                        if (!daemon_running) break;

                        if (run_distillation(mind, state, distill_config)) {
                            processed++;
                            distill_count++;
                        }
                    }

                    if (verbose_mode && processed > 0) {
                        std::cerr << "[distill] Processed " << processed
                                  << " transcript(s), total=" << distill_count << "\n";
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[distill] Error: " << e.what() << "\n";
                }
            }
        }
    });

    // Code enrichment thread - generate semantic descriptions for symbols
    std::atomic<size_t> enrich_count{0};
    std::thread enrichment([&]() {
        if (!enrich_config.enabled) return;

        auto interval_mins = std::chrono::minutes(enrich_config.interval_minutes);
        auto last_enrich = std::chrono::steady_clock::now();

        // Initial delay to let things settle (after distillation starts)
        std::this_thread::sleep_for(std::chrono::seconds(60));

        while (daemon_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            auto now_time = std::chrono::steady_clock::now();
            if (now_time - last_enrich >= interval_mins) {
                // Skip if daemon is actively handling queries (prevent blocking)
                if (!subconscious.is_idle()) {
                    if (verbose_mode) {
                        std::cerr << "[enrich] Skipping - daemon is busy\n";
                    }
                    continue;  // Don't update last_enrich, try again next second
                }

                last_enrich = now_time;

                try {
                    // Get undescribed symbols (prioritized by importance)
                    auto symbols = mind.store().get_undescribed_symbols(enrich_config.batch_size);

                    if (symbols.empty()) {
                        if (verbose_mode) {
                            std::cerr << "[enrich] No symbols pending description\n";
                        }
                        continue;
                    }

                    size_t processed = 0;
                    for (const auto& sym : symbols) {
                        if (!daemon_running) break;

                        // Create temp file with symbol info
                        std::string tmp_file = "/tmp/enrich-" + std::to_string(getpid())
                                             + "-" + std::to_string(sym.id) + ".txt";
                        {
                            std::ofstream ofs(tmp_file);
                            ofs << "SYMBOL_ID=" << sym.id << "\n";
                            ofs << "KIND=" << sym.kind << "\n";
                            ofs << "NAME=" << sym.name << "\n";
                            ofs << "FILE_PATH=" << sym.file_path << "\n";
                            ofs << "LINE_START=" << sym.line_start << "\n";
                            ofs << "LINE_END=" << sym.line_end << "\n";
                            ofs << "MODEL=" << enrich_config.model << "\n";
                        }

                        // Run enrichment script non-blocking with timeout
                        std::string out_file = tmp_file + ".out";
                        pid_t pid = fork();
                        if (pid == 0) {
                            // Child - run script with output capture
                            std::string cmd = enrich_config.script_path + " " + tmp_file + " > " + out_file + " 2>&1";
                            execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
                            _exit(1);
                        } else if (pid > 0) {
                            // Parent - wait with timeout (30 seconds max)
                            int status = 0;
                            bool finished = false;
                            for (int i = 0; i < 30 && daemon_running; i++) {
                                int result = waitpid(pid, &status, WNOHANG);
                                if (result > 0) {
                                    finished = true;
                                    break;
                                }
                                std::this_thread::sleep_for(std::chrono::seconds(1));
                            }

                            if (!finished) {
                                // Timeout - kill child
                                kill(pid, SIGKILL);
                                waitpid(pid, &status, 0);
                                if (verbose_mode) {
                                    std::cerr << "[enrich] Timeout for symbol " << sym.id << "\n";
                                }
                            } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                                // Read output file
                                std::ifstream ifs(out_file);
                                if (ifs) {
                                    std::string output((std::istreambuf_iterator<char>(ifs)),
                                                       std::istreambuf_iterator<char>());

                                    size_t pos = output.find("MEMORY_ID=");
                                    if (pos != std::string::npos) {
                                        std::string id_str = output.substr(pos + 10);
                                        size_t nl = id_str.find('\n');
                                        if (nl != std::string::npos) id_str = id_str.substr(0, nl);

                                        try {
                                            int64_t memory_id = std::stoll(id_str);
                                            mind.store().set_symbol_memory(sym.id, memory_id);
                                            processed++;
                                            enrich_count++;
                                        } catch (...) {}
                                    }

                                    if (verbose_mode) {
                                        std::cerr << output;
                                    }
                                }
                            }

                            // Cleanup
                            std::remove(out_file.c_str());
                        }
                        std::remove(tmp_file.c_str());
                    }

                    if (processed > 0) {
                        std::cerr << "[enrich] Described " << processed << " symbol(s), total="
                                  << enrich_count << "\n";
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[enrich] Error: " << e.what() << "\n";
                }
            }
        }
    });

    // Queue processor thread - handles fire-and-forget writes from hooks
    std::atomic<size_t> queue_count{0};
    std::string queue_path = "/tmp/chitta-queue.jsonl";
    std::thread queue_processor([&]() {
        while (daemon_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // Check if queue file exists
            std::ifstream check(queue_path);
            if (!check.good()) continue;
            check.close();

            // Atomically read and truncate queue
            std::vector<std::string> lines;
            {
                std::ifstream in(queue_path);
                std::string line;
                while (std::getline(in, line)) {
                    if (!line.empty()) lines.push_back(line);
                }
            }

            if (lines.empty()) continue;

            // Truncate file
            std::ofstream(queue_path, std::ios::trunc).close();

            // Process each queued request
            for (const auto& line : lines) {
                if (!daemon_running) break;

                try {
                    auto j = json::parse(line);
                    std::string tool = j.value("tool", "");
                    auto args = j.value("args", json::object());

                    if (tool == "observe") {
                        std::string category = args.value("category", "wisdom");
                        std::string title = args.value("title", "");
                        std::string content = args.value("content", "");
                        if (!content.empty()) {
                            NodeType type = NodeType::Wisdom;
                            if (category == "episode") type = NodeType::Episode;
                            else if (category == "belief") type = NodeType::Belief;
                            std::string full_text = title.empty() ? content : title + "\n" + content;
                            mind.remember(full_text, type);
                            queue_count++;
                        }
                    } else if (tool == "strengthen") {
                        std::string id_str = args.value("id", "");
                        double amount = args.value("amount", 0.1);
                        if (!id_str.empty()) {
                            try {
                                int64_t db_id = std::stoll(id_str);
                                mind.store().strengthen(db_id, static_cast<float>(amount));
                                queue_count++;
                            } catch (...) {
                                // Skip invalid IDs (UUIDs not yet supported in queue)
                            }
                        }
                    } else if (tool == "ledger_save") {
                        std::string session_id = args.value("session_id", "");
                        std::string project = args.value("project", "");
                        std::string mood = args.value("mood", "working");
                        std::string snapshot = args.value("snapshot", "");
                        if (!session_id.empty()) {
                            LedgerEntry entry;
                            entry.session_id = session_id;
                            entry.project = project;
                            entry.mood = mood;
                            entry.snapshot = snapshot;
                            // Extract all session state fields (JSON arrays → strings)
                            if (args.contains("active_files")) {
                                entry.active_files = args["active_files"].dump();
                            }
                            if (args.contains("decisions")) {
                                entry.decisions = args["decisions"].dump();
                            }
                            if (args.contains("todos")) {
                                entry.todos = args["todos"].dump();
                            }
                            if (args.contains("blockers")) {
                                entry.blockers = args["blockers"].dump();
                            }
                            if (args.contains("discoveries")) {
                                entry.discoveries = args["discoveries"].dump();
                            }
                            if (args.contains("next_steps")) {
                                entry.next_steps = args["next_steps"].dump();
                            }
                            mind.store().save_ledger(entry);
                            queue_count++;
                        }
                    } else if (tool == "connect") {
                        std::string subj = args.value("subject", "");
                        std::string pred = args.value("predicate", "");
                        std::string obj = args.value("object", "");
                        if (!subj.empty() && !pred.empty() && !obj.empty()) {
                            mind.connect(subj, pred, obj);
                            queue_count++;
                        }
                    } else if (tool == "transcript_register") {
                        std::string session_id = args.value("session_id", "");
                        std::string path = args.value("transcript_path", "");
                        std::string realm = args.value("realm", "brahman");
                        if (!path.empty()) {
                            mind.store().register_transcript(session_id, path, realm);
                            queue_count++;
                        }
                    }
                } catch (const std::exception& e) {
                    if (verbose_mode) {
                        std::cerr << "[queue] Error processing: " << e.what() << "\n";
                    }
                }
            }

            if (verbose_mode && !lines.empty()) {
                std::cerr << "[queue] Processed " << lines.size() << " items, total=" << queue_count << "\n";
            }
        }
    });

    // Thread pool for async RPC handling (scales 2-16 workers based on load)
    ThreadPool pool(2, 16);
    std::cerr << "[daemon] Thread pool started (" << pool.worker_count() << " workers)\n";
    std::cerr << "[daemon] Queue processor started (path=" << queue_path << ")\n";

    // Main loop - handle socket I/O (never blocks on RPC)
    auto last_stats = std::chrono::steady_clock::now();
    while (daemon_running) {
        // 1. Poll for I/O (fast, non-blocking)
        auto requests = server.poll(50);  // 50ms timeout for responsiveness

        // 2. Dispatch RPC requests to thread pool
        for (const auto& req : requests) {
            // Special commands (fast, handle directly)
            if (req.data == "stats") {
                server.respond(req.client_fd, generate_stats(mind));
                continue;
            }
            if (req.data == "shutdown") {
                server.respond(req.client_fd, R"({"status":"shutting_down"})");
                mind.sync();
                daemon_running = false;
                continue;
            }

            // Parse request to extract method for tracing
            auto parsed = json::parse(req.data, nullptr, false);
            if (parsed.is_discarded()) {
                std::string error = R"({"jsonrpc":"2.0","error":{"code":-32700,"message":"Parse error"},"id":null})";
                server.respond(req.client_fd, error);
                continue;
            }

            std::string method = parsed.value("method", "unknown");
            std::string tool_name = method;

            // Extract tool name for better tracing
            if (method == "tools/call" && parsed.contains("params")) {
                tool_name = parsed["params"].value("name", "unknown");
            }

            // Fast-path: health_check stays on main thread (always responsive)
            if (tool_name == "health_check") {
                auto response = handler.handle(parsed);
                // Add pool stats to health_check
                if (response.contains("result") && response["result"].contains("structured")) {
                    response["result"]["structured"]["pool_workers"] = pool.worker_count();
                    response["result"]["structured"]["pool_active"] = pool.active_count();
                    response["result"]["structured"]["pool_pending"] = pool.pending();
                }
                server.respond(req.client_fd, response.dump());
                continue;
            }

            // All other requests go to thread pool
            pool.submit(req.client_fd, tool_name,
                [&handler, data = req.data]() {
                    auto request = json::parse(data);
                    auto response = handler.handle(request);
                    return response.dump();
                },
                [&server](int fd, std::string response) {
                    server.queue_response(fd, std::move(response));
                }
            );
        }

        // 3. Write queued responses from thread pool (non-blocking)
        for (const auto& resp : server.drain_responses()) {
            server.respond(resp.client_fd, resp.data);
        }

        // 4. Log pool stats periodically (every 30s if there's activity)
        auto now = std::chrono::steady_clock::now();
        if (now - last_stats > std::chrono::seconds(30)) {
            last_stats = now;
            auto active = pool.get_active();
            if (!active.empty() || pool.pending() > 0) {
                std::cerr << "[pool] " << active.size() << " active, "
                          << pool.pending() << " pending\n";
                for (const auto& [method, ms] : active) {
                    if (ms > 1000) {
                        std::cerr << "[pool]   " << method << ": " << ms << "ms\n";
                    }
                }
            }
        }
    }

    maintenance.join();
    if (distillation.joinable()) distillation.join();
    if (enrichment.joinable()) enrichment.join();
    if (queue_processor.joinable()) queue_processor.join();
    subconscious.stop();
    server.stop();

    if (!pid_file.empty()) std::remove(pid_file.c_str());
    release_lock(lock);

    const auto& sc_stats = subconscious.stats();
    std::cerr << "[daemon] Stopped (cycles=" << cycle_count
              << ", distilled=" << distill_count
              << ", enriched=" << enrich_count
              << ", queued=" << queue_count
              << ", subconscious_events=" << sc_stats.events_processed.load()
              << ", corrections=" << sc_stats.corrections_detected.load()
              << ", preferences=" << sc_stats.preferences_detected.load() << ")\n";
    return 0;
}

int cmd_shutdown(const std::string& socket_path) {
    SocketClient client(socket_path);
    if (!client.connect()) {
        std::cerr << "No daemon running\n";
        return 1;
    }
    if (client.request_shutdown()) {
        std::cout << "Daemon shutdown requested\n";
        client.wait_for_socket_gone(5000);
        return 0;
    }
    return 1;
}

int cmd_status(const std::string& socket_path) {
    SocketClient client(socket_path);
    if (!client.connect()) {
        std::cout << "Daemon: not running\n";
        return 1;
    }
    auto version = client.check_version();
    std::cout << "Daemon: running\n";
    std::cout << "Socket: " << socket_path << "\n";
    if (version) std::cout << "Version: " << version->software << "\n";
    return 0;
}

int cmd_stats(DuckDBMind& mind) {
    std::cout << "Soul Statistics (DuckDB)\n";
    std::cout << "═══════════════════════════════\n";
    std::cout << "  Nodes:    " << mind.size() << "\n";
    std::cout << "  Triplets: " << mind.triplet_count() << "\n";
    std::cout << "  Yantra:   " << (mind.has_yantra() ? "ready" : "not attached") << "\n";
    return 0;
}

void print_usage(const char* prog) {
    std::cerr << "chittad " << CHITTA_VERSION << " - Soul daemon\n\n"
              << "Usage: " << prog << " <command> [options]\n\n"
              << "Commands:\n"
              << "  daemon     Run background daemon\n"
              << "  shutdown   Stop running daemon\n"
              << "  status     Check daemon status\n"
              << "  stats      Show soul statistics\n"
              << "  help       Show this help\n\n"
              << "Options:\n"
              << "  --path PATH        Mind storage path (DuckDB)\n"
              << "  --interval SECS    Sync interval (default: 60)\n"
              << "  -f, --foreground   Run in foreground\n"
              << "  --verbose          Verbose logging\n"
              << "  -v, --version      Show version\n"
              << "\nDistillation:\n"
              << "  --distill-interval MINS  Distillation interval (default: 5)\n"
              << "  --distill-min-turns N    Min turns before distilling (default: 4)\n"
              << "  --distill-script PATH    Distillation script path\n"
              << "  --distill-model MODEL    OpenCode model (default: github-copilot/gpt-5-mini)\n"
              << "  --no-distill             Disable automatic distillation\n"
              << "\nCode Enrichment (semantic descriptions):\n"
              << "  --enrich-interval MINS   Enrichment interval (default: 2)\n"
              << "  --enrich-batch N         Symbols per batch (default: 10)\n"
              << "  --enrich-model MODEL     OpenCode model (default: github-copilot/gpt-5-mini)\n"
              << "  --no-enrich              Disable code enrichment\n"
#ifdef CHITTA_WITH_POSTGRES
              << "\nPostgreSQL backend (for HPC multi-writer):\n"
              << "  --backend postgres Use PostgreSQL instead of DuckDB\n"
              << "  --pg-host HOST     PostgreSQL host (default: localhost)\n"
              << "  --pg-port PORT     PostgreSQL port (default: 5432)\n"
              << "  --pg-db NAME       Database name (default: soul)\n"
              << "  --pg-user USER     Database user (default: soul)\n"
              << "  --pg-pass PASS     Database password\n"
#endif
              ;
}

int main(int argc, char* argv[]) {
    std::string mind_path = default_mind_path();
    std::string command;
    int interval = 60;
    bool foreground = false;
    std::string backend = "duckdb";

    // Distillation config
    DistillConfig distill_config;
    distill_config.script_path = default_distill_script();

    // Code enrichment config
    EnrichConfig enrich_config;
    enrich_config.script_path = default_enrich_script();

#ifdef CHITTA_WITH_POSTGRES
    // PostgreSQL options
    std::string pg_host = "localhost";
    int pg_port = 5432;
    std::string pg_db = "soul";
    std::string pg_user = "soul";
    std::string pg_pass = "";
#endif

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            mind_path = argv[++i];
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            interval = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--foreground") == 0) {
            foreground = true;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose_mode = true;
        } else if (strcmp(argv[i], "--backend") == 0 && i + 1 < argc) {
            backend = argv[++i];
#ifdef CHITTA_WITH_POSTGRES
        } else if (strcmp(argv[i], "--pg-host") == 0 && i + 1 < argc) {
            pg_host = argv[++i];
        } else if (strcmp(argv[i], "--pg-port") == 0 && i + 1 < argc) {
            pg_port = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--pg-db") == 0 && i + 1 < argc) {
            pg_db = argv[++i];
        } else if (strcmp(argv[i], "--pg-user") == 0 && i + 1 < argc) {
            pg_user = argv[++i];
        } else if (strcmp(argv[i], "--pg-pass") == 0 && i + 1 < argc) {
            pg_pass = argv[++i];
#endif
        } else if (strcmp(argv[i], "--distill-interval") == 0 && i + 1 < argc) {
            distill_config.interval_minutes = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--distill-min-turns") == 0 && i + 1 < argc) {
            distill_config.min_turns = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--distill-script") == 0 && i + 1 < argc) {
            distill_config.script_path = argv[++i];
        } else if (strcmp(argv[i], "--distill-model") == 0 && i + 1 < argc) {
            distill_config.model = argv[++i];
        } else if (strcmp(argv[i], "--no-distill") == 0) {
            distill_config.enabled = false;
        } else if (strcmp(argv[i], "--enrich-interval") == 0 && i + 1 < argc) {
            enrich_config.interval_minutes = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--enrich-batch") == 0 && i + 1 < argc) {
            enrich_config.batch_size = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--enrich-idle") == 0 && i + 1 < argc) {
            enrich_config.idle_seconds = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--enrich-model") == 0 && i + 1 < argc) {
            enrich_config.model = argv[++i];
        } else if (strcmp(argv[i], "--no-enrich") == 0) {
            enrich_config.enabled = false;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            std::cout << "chittad " << CHITTA_VERSION << "\n";
            return 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-' && command.empty()) {
            command = argv[i];
        }
    }

    std::string sock_path = socket_path_for_mind(mind_path);
    std::string pid_file = pid_path_for_mind(mind_path);

    if (command.empty() || command == "help") {
        print_usage(argv[0]);
        return 0;
    }

    if (command == "shutdown") return cmd_shutdown(sock_path);
    if (command == "status") return cmd_status(sock_path);

    // Commands that need Mind - daemonize first if needed
    if (command == "daemon") {
        if (!foreground) {
            const char* home = getenv("HOME");
            std::string log_path = std::string(home ? home : ".") + "/.claude/mind/.subconscious.log";
            if (!daemonize(log_path)) {
                std::cerr << "Failed to daemonize\n";
                return 1;
            }
        }
    }

    // Create yantra for embeddings
#ifdef CHITTA_WITH_ONNX
    std::string model_path = default_model_path();
    std::string vocab_path = default_vocab_path();
    AntahkaranaYantra::Config yantra_config;
    yantra_config.pooling = PoolingStrategy::Mean;
    yantra_config.normalize_embeddings = true;
    auto yantra = std::make_shared<AntahkaranaYantra>(yantra_config);
    if (yantra->awaken(model_path, vocab_path)) {
        std::cerr << "[Yantra] Awakened\n";
    } else {
        std::cerr << "[Yantra] Failed: " << yantra->error() << "\n";
        yantra.reset();
    }
#endif

#ifdef CHITTA_WITH_POSTGRES
    // PostgreSQL backend
    if (backend == "postgres" || backend == "postgresql" || backend == "pg") {
        PostgresMindConfig config;
        config.host = pg_host;
        config.port = pg_port;
        config.dbname = pg_db;
        config.user = pg_user;
        config.password = pg_pass;

        PostgresMind mind(config);

#ifdef CHITTA_WITH_ONNX
        if (yantra) mind.attach_yantra(yantra);
#endif

        if (!mind.open()) {
            std::cerr << "Failed to connect to PostgreSQL at " << pg_host << ":" << pg_port << "\n";
            return 1;
        }

        std::cerr << "[Backend] PostgreSQL (" << pg_host << ":" << pg_port << "/" << pg_db << ")\n";

        int result = 0;
        if (command == "daemon") {
            // Use PostgresRpcHandler
            DaemonLock lock;
            if (!acquire_lock(mind_path, lock)) {
                std::cerr << "[daemon] Another daemon is running\n";
                return 1;
            }

            if (!pid_file.empty()) {
                std::ofstream pf(pid_file);
                if (pf) pf << getpid() << "\n";
            }

            SocketServer server(sock_path);
            if (!server.start()) {
                std::cerr << "[daemon] Failed to start socket server\n";
                release_lock(lock);
                return 1;
            }

            PostgresRpcHandler handler(&mind);

            std::signal(SIGTERM, daemon_signal_handler);
            std::signal(SIGINT, daemon_signal_handler);
            std::signal(SIGPIPE, SIG_IGN);

            std::cerr << "[daemon] Started PostgreSQL backend (socket=" << sock_path << ")\n";

            std::atomic<size_t> cycle_count{0};
            std::thread maintenance([&]() {
                auto interval_secs = std::chrono::seconds(interval);
                auto last_sync = std::chrono::steady_clock::now();
                while (daemon_running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    auto now_time = std::chrono::steady_clock::now();
                    if (now_time - last_sync >= interval_secs) {
                        last_sync = now_time;
                        cycle_count++;
                        try {
                            mind.tick();
                        } catch (const std::exception& e) {
                            std::cerr << "[maint] Cycle failed: " << e.what() << "\n";
                        }
                    }
                }
            });

            while (daemon_running) {
                auto requests = server.poll(100);
                for (const auto& req : requests) {
                    if (req.data == "stats") {
                        std::ostringstream oss;
                        oss << "{\"version\":\"" << CHITTA_VERSION << "\","
                            << "\"backend\":\"postgres\","
                            << "\"nodes\":" << mind.size() << ","
                            << "\"triplets\":" << mind.triplet_count() << "}";
                        server.respond(req.client_fd, oss.str());
                        continue;
                    }
                    if (req.data == "shutdown") {
                        server.respond(req.client_fd, R"({"status":"shutting_down"})");
                        daemon_running = false;
                        continue;
                    }
                    try {
                        auto request = json::parse(req.data);
                        auto response = handler.handle(request);
                        server.respond(req.client_fd, response.dump());
                    } catch (const std::exception& e) {
                        std::string error = R"({"jsonrpc":"2.0","error":{"code":-32700,"message":")"
                                          + std::string(e.what()) + R"("},"id":null})";
                        server.respond(req.client_fd, error);
                    }
                }
            }

            maintenance.join();
            server.stop();
            if (!pid_file.empty()) std::remove(pid_file.c_str());
            release_lock(lock);
            std::cerr << "[daemon] Stopped\n";
        } else if (command == "stats") {
            auto h = mind.health();
            std::cout << "Soul Statistics (PostgreSQL)\n";
            std::cout << "═══════════════════════════════\n";
            std::cout << "  Backend:  PostgreSQL\n";
            std::cout << "  Host:     " << pg_host << ":" << pg_port << "\n";
            std::cout << "  Database: " << pg_db << "\n";
            std::cout << "  Nodes:    " << mind.size() << "\n";
            std::cout << "  Triplets: " << mind.triplet_count() << "\n";
            std::cout << "  Yantra:   " << (mind.has_yantra() ? "ready" : "not attached") << "\n";
        } else {
            std::cerr << "Unknown command: " << command << "\n";
            result = 1;
        }

        mind.close();
        return result;
    }
#endif

    // Default: DuckDB backend
    DuckDBMindConfig config;
    config.path = mind_path;
    DuckDBMind mind(config);

#ifdef CHITTA_WITH_ONNX
    if (yantra) mind.attach_yantra(yantra);
#endif

    if (!mind.open()) {
        std::cerr << "Failed to open mind at " << mind_path << "\n";
        return 1;
    }

    std::cerr << "[Backend] DuckDB (" << mind_path << ")\n";

    int result = 0;
    if (command == "daemon") {
        result = cmd_daemon(mind, interval, sock_path, mind_path, pid_file, distill_config, enrich_config);
    } else if (command == "stats") {
        result = cmd_stats(mind);
    } else {
        std::cerr << "Unknown command: " << command << "\n";
        print_usage(argv[0]);
        result = 1;
    }

    mind.close();
    return result;
}
