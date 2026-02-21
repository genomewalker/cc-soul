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
#include <chitta/sadhana/sadhana_manager.hpp>
#include <chitta/rpc/duckdb_handler.hpp>
#include <chitta/rpc/thread_pool.hpp>
#include <chitta/socket_server.hpp>
#include <chitta/socket_client.hpp>
#include <chitta/native_distiller.hpp>
#include <chitta/version.hpp>
#ifdef CHITTA_WITH_ONNX
#include <chitta/vak_onnx.hpp>
#include <chitta/vak_timeout.hpp>
#endif
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <csignal>
#include <thread>
#include <chrono>
#include <atomic>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include <set>
#include <unordered_map>
#include <mutex>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

using namespace chitta;
using json = nlohmann::json;

// Global flags
// Verify lock-free for async-signal-safety in daemon_signal_handler
static_assert(std::atomic<bool>::is_always_lock_free, "atomic<bool> must be lock-free for signal handler");
static std::atomic<bool> daemon_running{true};
static std::atomic<bool> verbose_mode{false};

// Distillation configuration
struct DistillConfig {
    int interval_minutes = 15;      // Timer-based check interval (safety net, reduced from 5)
    int min_turns = 4;              // Minimum turns before distilling
    std::string script_path;        // Path to distillation script
    std::string model = "github-copilot/gpt-5-mini";  // OpenCode model
    bool enabled = true;
    int64_t token_trigger_chars = 120000;  // Token-triggered: ~30k tokens (chars/4), 0 = disabled
    int cooldown_seconds = 180;     // Minimum seconds between token-triggered distillations per session
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

    umask(077);
    chdir("/");

    int null_fd = open("/dev/null", O_RDONLY);
    if (null_fd >= 0) {
        if (dup2(null_fd, STDIN_FILENO) < 0) {
            // Log to file since stderr may not be available
            int err_fd = open("/tmp/chittad-daemonize.err", O_WRONLY | O_CREAT | O_APPEND, 0600);
            if (err_fd >= 0) {
                const char msg[] = "dup2(STDIN) failed\n";
                [[maybe_unused]] auto _ = write(err_fd, msg, sizeof(msg) - 1);
                close(err_fd);
            }
            close(null_fd);
            return false;
        }
        close(null_fd);
    }

    const char* out_path = log_path.empty() ? "/dev/null" : log_path.c_str();
    int log_fd = open(out_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd >= 0) {
        if (dup2(log_fd, STDOUT_FILENO) < 0 || dup2(log_fd, STDERR_FILENO) < 0) {
            int err_fd = open("/tmp/chittad-daemonize.err", O_WRONLY | O_CREAT | O_APPEND, 0600);
            if (err_fd >= 0) {
                const char msg[] = "dup2(STDOUT/STDERR) failed\n";
                [[maybe_unused]] auto _ = write(err_fd, msg, sizeof(msg) - 1);
                close(err_fd);
            }
            close(log_fd);
            return false;
        }
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
    if (ftruncate(lock.fd, 0) < 0) {
        std::cerr << "[daemon] Warning: ftruncate lock file failed: " << strerror(errno) << "\n";
    }
    if (write(lock.fd, pid.data(), pid.size()) < 0) {
        std::cerr << "[daemon] Warning: write PID to lock file failed: " << strerror(errno) << "\n";
    }
    return true;
}

void release_lock(DaemonLock& lock) {
    if (lock.fd >= 0) {
        close(lock.fd);
        unlink(lock.path.c_str());
    }
}

// Check if a process is alive
bool is_pid_alive(pid_t pid) {
    return kill(pid, 0) == 0 || errno == EPERM;
}

// Clean up stale daemon files from crashed daemon
// Returns true if cleanup succeeded or nothing to clean, false if daemon is alive
bool cleanup_stale_daemon(const std::string& mind_path) {
    std::string pid_path = pid_path_for_mind(mind_path);
    std::string sock_path = socket_path_for_mind(mind_path);
    std::string lock_path = lock_path_for_mind(mind_path);

    // Check if PID file exists
    std::ifstream pf(pid_path);
    if (!pf) return true;  // No PID file, nothing to clean

    pid_t old_pid;
    if (!(pf >> old_pid)) return true;  // Invalid PID file
    pf.close();

    // Check if that process is still alive
    if (is_pid_alive(old_pid)) {
        return false;  // Process exists, don't clean
    }

    // Process is dead, clean up stale files
    std::cerr << "[daemon] Cleaning stale files from dead PID " << old_pid << "\n";
    unlink(sock_path.c_str());
    unlink(pid_path.c_str());
    unlink(lock_path.c_str());
    return true;
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

// Run distillation for a single transcript using native C++ distiller
// Returns true if distillation was successful
// queue_triggered: if true, uses min_turns=1 (for pre-compact immediate distillation)
bool run_distillation(DuckDBMind& mind, const TranscriptState& state,
                      const DistillConfig& config, bool queue_triggered = false) {
    // Configure native distiller
    NativeDistillConfig native_config;
    native_config.model = config.model;
    native_config.timeout_secs = 150;  // Same as old shell timeout
    native_config.min_turns = config.min_turns;
    native_config.verbose = verbose_mode;

    NativeDistiller distiller(mind, native_config);

    // Set log callback if verbose
    if (verbose_mode) {
        distiller.set_log_callback([](const std::string& msg) {
            std::cerr << msg << "\n";
        });
    }

    // Run distillation
    auto result = distiller.distill_session(
        state.session_id,
        state.transcript_path,
        state.realm,
        state.last_processed_line,
        queue_triggered
    );

    if (!result.success) {
        if (verbose_mode && !result.error.empty()) {
            std::cerr << "[distill] " << state.session_id << ": " << result.error << "\n";
        }
        return false;
    }

    // Update progress (using thread-safe methods)
    mind.update_transcript_progress(state.session_id, result.last_line);
    mind.mark_transcript_distilled(state.session_id);

    if (verbose_mode) {
        std::cerr << "[distill] Completed " << state.session_id
                  << " (line " << result.last_line << ", +"
                  << result.learnings_stored << " learnings, "
                  << result.triplets_created << " triplets)\n";
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
    // Automatically reap child processes to prevent zombie accumulation
    signal(SIGCHLD, SIG_IGN);

    // Clean up stale files from crashed daemon
    if (!cleanup_stale_daemon(mind_path)) {
        std::cerr << "[daemon] Another daemon is running (PID alive)\n";
        return 1;
    }

    DaemonLock lock;
    if (!acquire_lock(mind_path, lock)) {
        std::cerr << "[daemon] Another daemon is running (lock held)\n";
        return 1;
    }

    // Check if opencode is available for distillation
    // Note: After daemonize(), std::system("command -v ...") may fail because
    // the forked shell doesn't source profile files. Search PATH directly instead.
    auto find_in_path = [](const char* name) -> bool {
        const char* path_env = getenv("PATH");
        if (!path_env) return false;
        std::string path_str(path_env);
        std::istringstream iss(path_str);
        std::string dir;
        while (std::getline(iss, dir, ':')) {
            std::string full = dir + "/" + name;
            if (access(full.c_str(), X_OK) == 0) return true;
        }
        return false;
    };
    if (distill_config.enabled) {
        bool found = find_in_path("opencode");
        if (!found) {
            std::cerr << "[daemon] ERROR: opencode not found in PATH\n";
            std::cerr << "[daemon] Distillation requires opencode. Install it or use --no-distill\n";
            release_lock(lock);
            return 1;
        }
        std::cerr << "[daemon] Distillation enabled (timer=" << distill_config.interval_minutes
                  << "m, min_turns=" << distill_config.min_turns
                  << ", model=" << distill_config.model;
        if (distill_config.token_trigger_chars > 0) {
            std::cerr << ", token_trigger=" << distill_config.token_trigger_chars
                      << " chars, cooldown=" << distill_config.cooldown_seconds << "s";
        }
        std::cerr << ")\n";
    }

    // Check for enrichment (uses same opencode)
    if (enrich_config.enabled) {
        // opencode already checked above if distillation enabled; check here if only enrichment
        if (!distill_config.enabled) {
            bool found = find_in_path("opencode");
            if (!found) {
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

    // Start sadhana manager for autonomous agents
    SadhanaManager sadhana_manager(mind.store());
    handler.set_sadhana_manager(&sadhana_manager);
    std::cerr << "[daemon] Sadhana manager initialized\n";

    // Wire up event streaming: push sadhana events to subscribed clients
    sadhana_manager.set_stream_fn([&server](int fd, std::string line) {
        server.queue_response(fd, std::move(line));
    });
    server.set_disconnect_callback([&sadhana_manager](int fd) {
        sadhana_manager.stream_unsubscribe(fd);
    });

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

            // Tick sadhana manager for autonomous agents (runs every 100ms loop)
            try {
                sadhana_manager.tick();
            } catch (const std::exception& e) {
                std::cerr << "[maint] Sadhana tick failed: " << e.what() << "\n";
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

                    // Auto-distill episode patterns into wisdom (every 10 cycles)
                    if (cycle_count % 10 == 0) {
                        size_t distilled = mind.auto_distill_episodes(5);
                        if (distilled > 0) {
                            std::cerr << "[maint] Auto-distilled " << distilled
                                      << " episode patterns into wisdom\n";
                        }
                    }

                    // Cache break pattern detection (every 10 cycles)
                    if (cycle_count % 10 == 0) {
                        try {
                            auto r = mind.store().execute_sql_query(
                                "SELECT COUNT(*) FROM session_token_usage "
                                "WHERE cache_hit_ratio < 0.5 AND n_messages > 3 "
                                "AND created_at >= " + std::to_string(
                                    std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch()
                                    ).count() - 86400));
                            if (r.success && !r.rows.empty() && !r.rows[0].empty()) {
                                int64_t breaks = 0;
                                try { breaks = std::stoll(r.rows[0][0]); } catch (...) {}
                                if (breaks >= 3) {
                                    std::string content = "[cache:pattern] " + std::to_string(breaks) +
                                        " cache breaks detected in last 24h. Systematic issue likely — "
                                        "review hook outputs, tool registrations, and model switching patterns.";
                                    mind.remember(content, NodeType::Wisdom, "brahman",
                                                  RealmVisibility::Private, 0.9f);
                                }
                            }
                        } catch (...) {}
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

                                    // Check if description was set successfully
                                    // New format: DESCRIBED=true (description stored directly in symbol table)
                                    // Legacy format: MEMORY_ID=<id> (separate wisdom memory)
                                    if (output.find("DESCRIBED=true") != std::string::npos) {
                                        processed++;
                                        enrich_count++;
                                    } else {
                                        // Legacy support: look for MEMORY_ID
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

    // Category to confidence mapping for high-value learnings
    auto category_to_confidence = [](const std::string& category) -> float {
        if (category == "correction") return 0.95f;
        if (category == "preference") return 0.90f;
        if (category == "solution")   return 0.90f;
        if (category == "milestone")  return 0.90f;
        if (category == "decision")   return 0.85f;
        if (category == "failure")    return 0.85f;
        if (category == "gotcha")     return 0.85f;
        if (category == "episode")    return 0.70f;
        return 0.80f;  // wisdom, pattern, insight, belief, etc.
    };

    // Queue processor thread - handles fire-and-forget writes from hooks
    std::atomic<size_t> queue_count{0};
    std::atomic<size_t> queue_distill_count{0};  // Separate counter for pre-compact distillations
    std::string queue_path = "/tmp/chitta-queue.jsonl";

    // Token-triggered distillation: per-session content accumulators
    std::unordered_map<std::string, int64_t> session_content_accum;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> session_last_distill;
    std::mutex session_accum_mutex;  // Protect the maps

    std::thread queue_processor([&]() {
        while (daemon_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // Atomically claim queue file via rename (prevents data loss from concurrent writes)
            std::string processing_path = queue_path + ".processing";
            if (std::rename(queue_path.c_str(), processing_path.c_str()) != 0) continue;

            // Read the claimed file
            std::vector<std::string> lines;
            {
                std::ifstream in(processing_path);
                std::string line;
                while (std::getline(in, line)) {
                    if (!line.empty()) lines.push_back(line);
                }
            }

            // Remove processed file
            std::remove(processing_path.c_str());

            if (lines.empty()) continue;

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
                            // Derive confidence from category (or use explicit override)
                            float confidence = args.contains("confidence")
                                ? args.value("confidence", 0.8f)
                                : category_to_confidence(category);
                            std::string full_text = title.empty() ? content : title + "\n" + content;
                            mind.remember(full_text, type, "brahman", RealmVisibility::Private, confidence);
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
                                // Non-numeric ID: search by content prefix
                                auto results = mind.recall(id_str, 1);
                                if (!results.empty() && results[0].similarity > 0.9f) {
                                    mind.store().strengthen(static_cast<int64_t>(results[0].id.low), static_cast<float>(amount));
                                    queue_count++;
                                }
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
                    } else if (tool == "learn_outcome") {
                        // Handle both hyphen and underscore param names
                        std::string id_str = args.contains("memory-id")
                            ? args.value("memory-id", "")
                            : args.value("memory_id", "");
                        std::string outcome = args.value("outcome", "");
                        std::string context = args.value("context", "");
                        if (!id_str.empty() && !outcome.empty()) {
                            int64_t memory_id = -1;
                            try {
                                memory_id = std::stoll(id_str);
                            } catch (...) {
                                // Non-numeric ID: search by content prefix
                                auto results = mind.recall(id_str, 1);
                                if (!results.empty() && results[0].similarity > 0.9f) {
                                    memory_id = static_cast<int64_t>(results[0].id.low);
                                }
                            }
                            if (memory_id > 0) {
                                mind.store().record_usage_outcome(memory_id, "hook", outcome, context);
                                if (outcome == "positive") {
                                    mind.store().strengthen(memory_id, 0.1f);
                                } else if (outcome == "negative") {
                                    mind.store().weaken(memory_id, 0.15f);
                                }
                                queue_count++;
                            }
                        }
                    } else if (tool == "anticipation_success") {
                        int64_t id = args.value("id", 0);
                        if (id > 0) {
                            mind.store().anticipation_success(id);
                            queue_count++;
                        }
                    } else if (tool == "narrative_log") {
                        std::string session_id = args.value("session_id", "");
                        std::string kind_str = args.value("kind", "user_message");
                        std::string summary = args.value("summary", "");
                        std::string tool_name = args.value("tool_name", "");
                        bool success = args.value("success", true);
                        if (!session_id.empty() && !summary.empty()) {
                            SessionEvent event;
                            event.session_id = session_id;
                            event.kind = string_to_session_event_kind(kind_str);
                            event.summary = summary;
                            event.tool_name = tool_name;
                            event.success = success;
                            mind.store().event_log_append(event);
                            if (mind.narrative()) {
                                mind.narrative()->evaluate(session_id, event);
                            }
                            queue_count++;
                        }
                    } else if (tool == "calibration_record") {
                        std::string domain = args.value("domain", "");
                        bool success = args.value("success", true);
                        if (!domain.empty()) {
                            mind.store().calibration_record(domain, success);
                            queue_count++;
                        }
                    } else if (tool == "curiosity_note_gap") {
                        std::string gap = args.value("gap", "");
                        if (!gap.empty()) {
                            // Create memory with "gap" and "unresolved" tags
                            std::string content = "[curiosity] " + gap;
                            mind.remember(content, NodeType::Episode, "brahman",
                                          RealmVisibility::Private, 0.7f);
                            queue_count++;
                        }
                    } else if (tool == "habit_observe") {
                        std::string trigger = args.value("trigger", "");
                        std::string response = args.value("response", "");
                        std::string realm = args.value("realm", "brahman");
                        if (!trigger.empty() && !response.empty()) {
                            mind.store().habit_observe(trigger, response, realm);
                            queue_count++;
                        }
                    } else if (tool == "session_register") {
                        std::string sid = args.value("session_id", "");
                        std::string realm = args.value("realm", "brahman");
                        int32_t pid = args.value("pid", 0);
                        std::string metadata = args.value("metadata", "{}");
                        if (!sid.empty()) {
                            mind.store().session_register(sid, realm, pid, metadata);
                            queue_count++;
                        }
                    } else if (tool == "session_heartbeat") {
                        std::string sid = args.value("session_id", "");
                        std::string metadata = args.value("metadata", "");
                        if (!sid.empty()) {
                            mind.store().session_heartbeat(sid, metadata);
                            queue_count++;
                        }
                    } else if (tool == "session_deregister") {
                        std::string sid = args.value("session_id", "");
                        if (!sid.empty()) {
                            mind.store().session_deregister(sid);
                            queue_count++;
                        }
                    } else if (tool == "distill_trigger") {
                        // Immediate distillation trigger (used by PreCompact hook)
                        std::string sid = args.value("session_id", "");
                        if (!sid.empty() && distill_config.enabled) {
                            auto state_opt = mind.store().get_transcript(sid);
                            if (state_opt) {
                                std::cerr << "[queue] Pre-compact distillation triggered for " << sid << "\n";
                                // Use queue_triggered=true for min_turns=1
                                bool success = run_distillation(mind, *state_opt, distill_config, true);
                                if (success) {
                                    queue_distill_count++;
                                    std::cerr << "[queue] Pre-compact distillation succeeded for " << sid
                                              << " (total=" << queue_distill_count << ")\n";
                                } else {
                                    std::cerr << "[queue] Pre-compact distillation failed for " << sid
                                              << " (will not retry)\n";
                                }
                                queue_count++;
                            } else {
                                std::cerr << "[queue] No transcript found for session " << sid << "\n";
                            }
                        }
                    } else if (tool == "store_turn") {
                        // Store conversation turn for lossless memory
                        std::string session_id = args.value("session_id", "");
                        std::string role = args.value("role", "");
                        std::string content = args.value("content", "");
                        int turn_index = args.value("turn_index", -1);
                        if (!session_id.empty() && !role.empty() && !content.empty()) {
                            DuckDBStore::ConversationTurn turn;
                            turn.session_id = session_id;
                            turn.role = role;
                            turn.content = content;
                            turn.turn_index = turn_index >= 0 ? turn_index : 0;
                            turn.token_count = static_cast<int>(content.size() / 4);  // Rough token estimate
                            turn.realm = args.value("realm", "brahman");
                            turn.intent_type = args.value("intent_type", "");
                            turn.tools_used = args.value("tools_used", "[]");
                            turn.files_touched = args.value("files_touched", "[]");
                            turn.has_error = args.value("has_error", false);
                            mind.store().store_conversation_turn(turn);
                            queue_count++;

                            // Token-triggered distillation: accumulate content and check threshold
                            if (distill_config.enabled && distill_config.token_trigger_chars > 0) {
                                std::lock_guard<std::mutex> lock(session_accum_mutex);
                                session_content_accum[session_id] += static_cast<int64_t>(content.size());

                                if (session_content_accum[session_id] >= distill_config.token_trigger_chars) {
                                    auto now = std::chrono::steady_clock::now();
                                    auto cooldown = std::chrono::seconds(distill_config.cooldown_seconds);
                                    auto& last = session_last_distill[session_id];

                                    if (now - last >= cooldown) {
                                        // Look up transcript for this session and trigger distillation
                                        auto state_opt = mind.store().get_transcript(session_id);
                                        if (state_opt) {
                                            std::cerr << "[queue] Token-triggered distillation for " << session_id
                                                      << " (" << session_content_accum[session_id] << " chars)\n";
                                            bool success = run_distillation(mind, *state_opt, distill_config, false);
                                            if (success) {
                                                queue_distill_count++;
                                            }
                                        }
                                        last = now;
                                        session_content_accum[session_id] = 0;
                                    }
                                }
                            }
                        }
                    } else if (tool == "store_claim") {
                        // Store semantic claim
                        std::string subject = args.value("subject", "");
                        std::string predicate = args.value("predicate", "");
                        std::string object_norm = args.value("object", "");
                        if (!subject.empty() && !predicate.empty() && !object_norm.empty()) {
                            DuckDBStore::Claim claim;
                            claim.subject = subject;
                            claim.predicate = predicate;
                            claim.object_norm = object_norm;
                            claim.scope_key = args.value("scope", "session");
                            claim.polarity = args.value("polarity", 1);
                            claim.confidence = args.value("confidence", 0.7f);
                            claim.source_class = args.value("source", "hook");
                            mind.store().store_claim(claim);
                            queue_count++;
                        }
                    } else if (tool == "store_policy") {
                        // Store policy memory
                        std::string policy_type = args.value("type", "");
                        std::string content = args.value("content", "");
                        if (!policy_type.empty() && !content.empty()) {
                            DuckDBStore::PolicyMemory policy;
                            policy.policy_type = policy_type;
                            policy.content = content;
                            policy.scope_key = args.value("scope", "session");
                            policy.state = args.value("state", "ephemeral");
                            policy.confidence = args.value("confidence", 0.5f);
                            mind.store().store_policy(policy);
                            queue_count++;
                        }
                    } else if (tool == "store_relationship_event") {
                        // Store relationship event (correction, praise, etc.)
                        std::string event_type = args.value("event_type", "");
                        std::string content = args.value("content", "");
                        std::string session_id = args.value("session_id", "");
                        if (!event_type.empty() && !content.empty()) {
                            DuckDBStore::RelationshipEvent event;
                            event.session_id = session_id;
                            event.event_type = event_type;
                            event.content = content;
                            event.context = args.value("context", "");
                            mind.store().store_relationship_event(event);
                            queue_count++;
                        }
                    } else if (tool == "log_session_tokens") {
                        std::string sid = args.value("session_id", "");
                        int64_t input_tok = args.value("total_input_tokens", (int64_t)0);
                        int64_t output_tok = args.value("total_output_tokens", (int64_t)0);
                        int64_t cache_read = args.value("cache_read_tokens", (int64_t)0);
                        int64_t cache_create = args.value("cache_creation_tokens", (int64_t)0);
                        int n_msgs = args.value("n_messages", 0);

                        if (!sid.empty() && n_msgs > 0) {
                            mind.store().log_session_tokens(
                                sid, input_tok, output_tok,
                                cache_read, cache_create, n_msgs);
                            queue_count++;
                        }
                    } else if (tool == "log_correction_outcome") {
                        std::string sid = args.value("session_id", "");
                        int64_t mem_id = args.value("correction_memory_id", (int64_t)0);
                        bool detected = args.value("correction_detected", false);
                        std::string text = args.value("correction_text", "");
                        if (!sid.empty() && mem_id > 0) {
                            mind.store().log_correction_outcome(sid, mem_id, detected, text);
                            queue_count++;
                        }
                    } else if (tool == "log_exposure") {
                        std::string session_id = args.value("session_id", "");
                        int turn_id = args.value("turn_id", 0);
                        std::string hook_type = args.value("hook_type", "");

                        std::vector<int64_t> memory_ids;
                        if (args.contains("memory_ids") && args["memory_ids"].is_array()) {
                            for (const auto& id : args["memory_ids"]) {
                                memory_ids.push_back(id.get<int64_t>());
                            }
                        }

                        if (!session_id.empty() && !memory_ids.empty()) {
                            std::vector<int> ranks;
                            if (args.contains("ranks") && args["ranks"].is_array()) {
                                for (const auto& r : args["ranks"]) ranks.push_back(r.get<int>());
                            }
                            std::vector<double> resonance_scores;
                            if (args.contains("resonance_scores") && args["resonance_scores"].is_array()) {
                                for (const auto& s : args["resonance_scores"]) resonance_scores.push_back(s.get<double>());
                            }
                            std::vector<int> token_costs;
                            if (args.contains("token_costs") && args["token_costs"].is_array()) {
                                for (const auto& t : args["token_costs"]) token_costs.push_back(t.get<int>());
                            }

                            mind.store().log_exposures_batch(
                                session_id, turn_id, hook_type, memory_ids, ranks,
                                {}, resonance_scores, token_costs);
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

    // Configure circuit breaker for embedder
    mind.embedder().configure_circuit_breaker({
        .failure_threshold = 3,
        .cooldown = std::chrono::seconds(60)
    });

    // Watchdog callback - trip circuit breaker on stuck embedding operations
    pool.set_watchdog_callback([&mind](const std::string& method, int64_t secs) {
        // Only trip circuit breaker for embedding-related operations
        if (method.find("search_symbols") != std::string::npos ||
            method.find("smart_context") != std::string::npos ||
            method.find("recall") != std::string::npos ||
            method.find("full_resonate") != std::string::npos) {
            std::cerr << "[watchdog] Forcing circuit breaker trip due to stuck " << method << "\n";
            mind.embedder().force_trip();
        }
    });
    pool.set_escalation_threshold(std::chrono::seconds(30));  // Trip after 30s

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

                // For session-aware tools, inject peer PID for session lookup if session_id not provided
                // The peer_pid is the CLI process; its parent (PPID) is Claude
                if (parsed["params"].contains("arguments")) {
                    auto& args = parsed["params"]["arguments"];
                    if (!args.contains("session_id") || args["session_id"].get<std::string>().empty()) {
                        if (req.peer_pid > 1) {
                            args["pid"] = static_cast<int64_t>(req.peer_pid);
                        }
                    }
                }
            }

            // Fast-path: sadhana_watch - keep connection open for event streaming
            // Must handle in main loop (not thread pool) to keep the fd as a subscriber
            if (tool_name == "sadhana_watch") {
                int64_t watch_id = 0;
                if (parsed.contains("params") && parsed["params"].contains("arguments")) {
                    watch_id = parsed["params"]["arguments"].value("id", (int64_t)0);
                }
                sadhana_manager.stream_subscribe(req.client_fd, watch_id);
                // Send ack — connection stays open; daemon will push events as lines
                json ack;
                ack["jsonrpc"] = "2.0";
                ack["id"] = parsed.value("id", json());
                ack["result"]["content"] = json::array({
                    json::object({{"type", "text"}, {"text", "watching"}})
                });
                ack["result"]["structured"]["status"] = "watching";
                ack["result"]["structured"]["sadhana_id"] = watch_id;
                server.respond(req.client_fd, ack.dump());
                continue;
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
              << ", queue_distilled=" << queue_distill_count
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

int cmd_metrics(DuckDBMind& mind, int days = 7) {
    auto& store = mind.store();

    auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t cutoff = now_sec - static_cast<int64_t>(days) * 86400;
    int64_t now_30d = now_sec - 30 * 86400;

    // Exposure stats
    auto exp_r = store.execute_sql_query(
        "SELECT COUNT(*), COUNT(DISTINCT session_id) FROM memory_exposed WHERE created_at >= " + std::to_string(cutoff));
    int64_t n_exposures = 0, n_sessions_with_exposures = 0;
    if (exp_r.success && !exp_r.rows.empty() && exp_r.rows[0].size() >= 2) {
        try { n_exposures = std::stoll(exp_r.rows[0][0]); } catch (...) {}
        try { n_sessions_with_exposures = std::stoll(exp_r.rows[0][1]); } catch (...) {}
    }

    // Recall stats
    auto rec_r = store.execute_sql_query(
        "SELECT COUNT(*), COUNT(CASE WHEN returned_memory_ids != '[]' AND returned_memory_ids != '' THEN 1 END) "
        "FROM memory_recall_query WHERE created_at >= " + std::to_string(cutoff));
    int64_t n_recalls = 0, n_recall_hits = 0;
    if (rec_r.success && !rec_r.rows.empty() && rec_r.rows[0].size() >= 2) {
        try { n_recalls = std::stoll(rec_r.rows[0][0]); } catch (...) {}
        try { n_recall_hits = std::stoll(rec_r.rows[0][1]); } catch (...) {}
    }

    // Memory durability
    auto mem_r = store.execute_sql_query(
        "SELECT COUNT(*), COUNT(CASE WHEN accessed_at >= " + std::to_string(now_30d) + " THEN 1 END) "
        "FROM memory WHERE confidence > 0.1");
    int64_t n_memories = 0, n_accessed_30d = 0;
    if (mem_r.success && !mem_r.rows.empty() && mem_r.rows[0].size() >= 2) {
        try { n_memories = std::stoll(mem_r.rows[0][0]); } catch (...) {}
        try { n_accessed_30d = std::stoll(mem_r.rows[0][1]); } catch (...) {}
    }

    // Session count from session_registry (uses started_at)
    auto sess_r = store.execute_sql_query(
        "SELECT COUNT(DISTINCT session_id) FROM session_registry WHERE started_at >= " + std::to_string(cutoff));
    int64_t n_sessions = 0;
    if (sess_r.success && !sess_r.rows.empty() && !sess_r.rows[0].empty()) {
        try { n_sessions = std::stoll(sess_r.rows[0][0]); } catch (...) {}
    }
    // Fallback to memory_exposed if no session_registry data
    if (n_sessions == 0) n_sessions = n_sessions_with_exposures;

    // Compute R (relevance): exposures per session, sigmoid-scaled
    double R = 0.0;
    if (n_sessions > 0 && n_exposures > 0) {
        double exposures_per_session = static_cast<double>(n_exposures) / n_sessions;
        R = 1.0 / (1.0 + std::exp(-0.5 * (exposures_per_session - 5.0)));
    }

    // Compute P (precision): recall hit rate
    double P = 0.0;
    if (n_recalls > 0) {
        P = static_cast<double>(n_recall_hits) / n_recalls;
    }

    // Compute D (durability): fraction accessed in last 30 days
    double D = 0.0;
    if (n_memories > 0) {
        D = static_cast<double>(n_accessed_30d) / n_memories;
    }

    // T: average cache hit ratio across sessions in window
    double T = -1.0;
    {
        std::string cutoff_sql = std::to_string(cutoff);
        auto t_r = store.execute_sql_query(
            "SELECT AVG(cache_hit_ratio) FROM session_token_usage "
            "WHERE created_at >= " + cutoff_sql + " AND n_messages > 3");
        if (t_r.success && !t_r.rows.empty() && !t_r.rows[0].empty()
            && !t_r.rows[0][0].empty()) {
            try { T = std::stod(t_r.rows[0][0]); } catch (...) {}
        }
    }

    // Partial SUS (M not instrumented)
    // Available: R(0.25) P(0.20) T(0.10) D(0.15) = 0.70
    double avail = 0.70;
    double r_w = 0.25 / avail;
    double p_w = 0.20 / avail;
    double t_w = 0.10 / avail;
    double d_w = 0.15 / avail;
    double p_val = (P > 0) ? P : 0.5;
    double t_val = (T >= 0) ? T : 0.5;
    double sus = 100.0 * std::pow(std::max(R, 1e-6), r_w)
                       * std::pow(std::max(p_val, 1e-6), p_w)
                       * std::pow(std::max(t_val, 1e-6), t_w)
                       * std::pow(std::max(D, 1e-6), d_w);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "SUS (" << days << "d): " << static_cast<int>(std::round(sus))
              << "  [partial, n_sessions=" << n_sessions << "]\n\n";
    std::cout << "  R (relevance):   " << R
              << "  (n_exposures=" << n_exposures
              << ", n_sessions_with_exposures=" << n_sessions_with_exposures << ")\n";
    std::cout << "  P (precision):   " << P
              << "  (n_recalls=" << n_recalls << ", hit_rate)\n";
    std::cout << "  M (prevention):  --    (Phase 4)\n";
    if (T >= 0) {
        std::cout << "  T (cache):       " << T
                  << "  (cache hit ratio avg)\n";
    } else {
        std::cout << "  T (cache):       --    (no data yet)\n";
    }
    std::cout << "  D (durability):  " << D
              << "  (n_memories=" << n_memories
              << ", accessed_30d/" << n_memories << ")\n";
    std::cout << "\n  Note: SUS is partial (M not yet instrumented). T=cache_hit_ratio avg.\n";

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
              << "  metrics    Show SUS (Soul Utility Score) component metrics\n"
              << "  distill    Run manual distillation on a transcript\n"
              << "  help       Show this help\n\n"
              << "Options:\n"
              << "  --path PATH        Mind storage path (DuckDB)\n"
              << "  --interval SECS    Sync interval (default: 60)\n"
              << "  -f, --foreground   Run in foreground\n"
              << "  --verbose          Verbose logging\n"
              << "  -v, --version      Show version\n"
              << "\nManual Distillation (distill command):\n"
              << "  --transcript-path PATH   JSONL transcript file to distill (required)\n"
              << "  --session-id ID          Session ID (auto-extracted from path if omitted)\n"
              << "  --realm REALM            Target realm (default: brahman)\n"
              << "\nAutomatic Distillation (daemon):\n"
              << "  --distill-interval MINS  Timer-based interval (default: 15, safety net)\n"
              << "  --distill-min-turns N    Min turns before distilling (default: 4)\n"
              << "  --distill-script PATH    Distillation script path (ignored, uses native)\n"
              << "  --distill-model MODEL    OpenCode model (default: github-copilot/gpt-5-mini)\n"
              << "  --distill-token-trigger N  Token-triggered: chars threshold (default: 120000 ~30k tokens, 0=off)\n"
              << "  --distill-cooldown SECS  Min seconds between token-triggered distillations (default: 180)\n"
              << "  --no-distill             Disable automatic distillation\n"
              << "\nCode Enrichment (semantic descriptions):\n"
              << "  --enrich-interval MINS   Enrichment interval (default: 2)\n"
              << "  --enrich-batch N         Symbols per batch (default: 10)\n"
              << "  --enrich-model MODEL     OpenCode model (default: github-copilot/gpt-5-mini)\n"
              << "  --no-enrich              Disable code enrichment\n"
              ;
}

int main(int argc, char* argv[]) {
    // CRITICAL: Set thread limits BEFORE any ONNX/OpenMP code loads
    // Must be at the very start of main() to take effect
    setenv("OMP_NUM_THREADS", "4", 1);
    setenv("MKL_NUM_THREADS", "4", 1);
    setenv("OPENBLAS_NUM_THREADS", "4", 1);
    setenv("ORT_NUM_THREADS", "4", 1);
    setenv("OMP_WAIT_POLICY", "PASSIVE", 1);  // Reduce busy-waiting
    setenv("KMP_BLOCKTIME", "0", 1);          // Intel OpenMP: don't spin

    std::string mind_path = default_mind_path();
    std::string command;
    int interval = 60;
    bool foreground = false;

    // Distillation config
    DistillConfig distill_config;
    distill_config.script_path = default_distill_script();

    // Code enrichment config
    EnrichConfig enrich_config;
    enrich_config.script_path = default_enrich_script();

    // Manual distill command args
    std::string distill_transcript_path;
    std::string distill_session_id;
    std::string distill_realm = "brahman";

    auto safe_stoi = [&](const char* arg, const char* option_name) -> int {
        try {
            return std::stoi(arg);
        } catch (const std::invalid_argument&) {
            std::cerr << "Error: invalid integer for " << option_name << ": " << arg << "\n";
            print_usage(argv[0]);
            std::exit(1);
        } catch (const std::out_of_range&) {
            std::cerr << "Error: integer out of range for " << option_name << ": " << arg << "\n";
            print_usage(argv[0]);
            std::exit(1);
        }
    };

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            mind_path = argv[++i];
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            interval = safe_stoi(argv[++i], "--interval");
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--foreground") == 0) {
            foreground = true;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose_mode = true;
        } else if (strcmp(argv[i], "--distill-interval") == 0 && i + 1 < argc) {
            distill_config.interval_minutes = safe_stoi(argv[++i], "--distill-interval");
        } else if (strcmp(argv[i], "--distill-min-turns") == 0 && i + 1 < argc) {
            distill_config.min_turns = safe_stoi(argv[++i], "--distill-min-turns");
        } else if (strcmp(argv[i], "--distill-script") == 0 && i + 1 < argc) {
            distill_config.script_path = argv[++i];
        } else if (strcmp(argv[i], "--distill-model") == 0 && i + 1 < argc) {
            distill_config.model = argv[++i];
        } else if (strcmp(argv[i], "--distill-token-trigger") == 0 && i + 1 < argc) {
            distill_config.token_trigger_chars = safe_stoi(argv[++i], "--distill-token-trigger");
        } else if (strcmp(argv[i], "--distill-cooldown") == 0 && i + 1 < argc) {
            distill_config.cooldown_seconds = safe_stoi(argv[++i], "--distill-cooldown");
        } else if (strcmp(argv[i], "--no-distill") == 0) {
            distill_config.enabled = false;
        } else if (strcmp(argv[i], "--enrich-interval") == 0 && i + 1 < argc) {
            enrich_config.interval_minutes = safe_stoi(argv[++i], "--enrich-interval");
        } else if (strcmp(argv[i], "--enrich-batch") == 0 && i + 1 < argc) {
            enrich_config.batch_size = safe_stoi(argv[++i], "--enrich-batch");
        } else if (strcmp(argv[i], "--enrich-idle") == 0 && i + 1 < argc) {
            enrich_config.idle_seconds = safe_stoi(argv[++i], "--enrich-idle");
        } else if (strcmp(argv[i], "--enrich-model") == 0 && i + 1 < argc) {
            enrich_config.model = argv[++i];
        } else if (strcmp(argv[i], "--no-enrich") == 0) {
            enrich_config.enabled = false;
        } else if (strcmp(argv[i], "--transcript-path") == 0 && i + 1 < argc) {
            distill_transcript_path = argv[++i];
        } else if (strcmp(argv[i], "--session-id") == 0 && i + 1 < argc) {
            distill_session_id = argv[++i];
        } else if (strcmp(argv[i], "--realm") == 0 && i + 1 < argc) {
            distill_realm = argv[++i];
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
    // Thread limits already set at main() start
    const int max_onnx_threads = 4;

    std::string model_path = default_model_path();
    std::string vocab_path = default_vocab_path();
    AntahkaranaYantra::Config yantra_config;
    yantra_config.pooling = PoolingStrategy::Mean;
    yantra_config.normalize_embeddings = true;
    yantra_config.num_threads = max_onnx_threads;
    auto inner_yantra = std::make_shared<AntahkaranaYantra>(yantra_config);
    std::shared_ptr<VakYantra> yantra;
    if (inner_yantra->awaken(model_path, vocab_path)) {
        // Wrap with timeout protection (5s default timeout)
        yantra = std::make_shared<TimeoutYantra>(inner_yantra, std::chrono::milliseconds(5000));
        std::cerr << "[Yantra] Awakened (timeout-protected)\n";
    } else {
        std::cerr << "[Yantra] Failed: " << inner_yantra->error() << "\n";
        // yantra stays nullptr
    }
#endif

    // DuckDB backend
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
    } else if (command == "metrics") {
        result = cmd_metrics(mind);
    } else if (command == "distill") {
        // Manual distillation command
        if (distill_transcript_path.empty()) {
            std::cerr << "Error: --transcript-path is required for distill command\n";
            return 1;
        }
        if (distill_session_id.empty()) {
            // Try to extract session ID from path (e.g., /path/to/abc123.jsonl -> abc123)
            size_t last_slash = distill_transcript_path.rfind('/');
            size_t start = (last_slash == std::string::npos) ? 0 : last_slash + 1;
            size_t dot = distill_transcript_path.rfind('.');
            if (dot != std::string::npos && dot > start) {
                distill_session_id = distill_transcript_path.substr(start, dot - start);
            } else {
                std::cerr << "Error: --session-id is required (could not extract from path)\n";
                return 1;
            }
        }

        verbose_mode = true;  // Enable verbose for manual distillation
        std::cerr << "[distill] Transcript: " << distill_transcript_path << "\n";
        std::cerr << "[distill] Session:    " << distill_session_id << "\n";
        std::cerr << "[distill] Realm:      " << distill_realm << "\n";
        std::cerr << "[distill] Model:      " << distill_config.model << "\n";

        // Create TranscriptState for the run_distillation function
        TranscriptState state;
        state.session_id = distill_session_id;
        state.transcript_path = distill_transcript_path;
        state.realm = distill_realm;
        state.last_processed_line = 0;  // Process from beginning

        bool success = run_distillation(mind, state, distill_config, false);
        result = success ? 0 : 1;
    } else {
        std::cerr << "Unknown command: " << command << "\n";
        print_usage(argv[0]);
        result = 1;
    }

    mind.close();
    return result;
}
