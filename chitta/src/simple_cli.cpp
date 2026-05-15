// chitta-cli: Simplified daemon for SimpleMind
//
// Usage: chittad <command> [options]
//
// Commands:
//   daemon     Run background daemon
//   shutdown   Stop running daemon
//   status     Check daemon status
//   stats      Show soul statistics

#ifdef __linux__
#include <sys/inotify.h>
#endif
#include <chitta/field_store.hpp>
#include <chitta/rpc/field_handler.hpp>
#include <chitta/mind/subconscious.hpp>
#include <chitta/sadhana/sadhana_manager.hpp>
#include <chitta/rpc/thread_pool.hpp>
#include <chitta/socket_server.hpp>
#include <chitta/socket_client.hpp>
#include <chitta/native_distiller.hpp>
#include <chitta/daemon_config.hpp>
#include <chitta/daemon_lifecycle.hpp>
#include <chitta/distillation.hpp>
#include <chitta/queue_processor.hpp>
#include <chitta/http_viz.hpp>
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
#include <filesystem>
#include <nlohmann/json.hpp>
#include <set>
#include <unordered_map>
#include <mutex>
#include <unistd.h>
#include <climits>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>

using namespace chitta;
using json = nlohmann::json;

// Global flags
// Verify lock-free for async-signal-safety in daemon_signal_handler
static_assert(std::atomic<bool>::is_always_lock_free, "atomic<bool> must be lock-free for signal handler");
std::atomic<bool> daemon_running{true};
std::atomic<bool> verbose_mode{false};

// Distillation configuration
// Config structs, path resolution, daemon lifecycle, distillation:
// → daemon_config.hpp, daemon_lifecycle.hpp, distillation.hpp

void daemon_signal_handler(int sig) {
    std::cerr << "[daemon] Signal " << sig << " received, shutting down\n";
    daemon_running = false;
}

int cmd_daemon(FieldStore& field_store, VakYantra* yantra, int interval,
               SocketServer& server, const std::string& socket_path, const std::string& mind_path,
               const std::string& pid_file,
               const DistillConfig& distill_config, EnrichConfig& enrich_config,
               const SubconsciousConfig& subconscious_config, bool no_autonomous,
               int http_port, const std::string& http_static_dir) {
    // Automatically reap child processes to prevent zombie accumulation
    signal(SIGCHLD, SIG_IGN);

    DaemonLock lock;
    if (!acquire_lock(mind_path, lock)) {
        std::cerr << "[daemon] Another daemon is running (lock held)\n";
        return 1;
    }

    // Distillation uses HTTP to Ollama/vLLM (endpoint auto-discovered)
    if (distill_config.enabled) {
        std::cerr << "[daemon] Distillation enabled (timer=" << distill_config.interval_minutes
                  << "m, min_turns=" << distill_config.min_turns
                  << ", model=" << distill_config.model;
        if (distill_config.token_trigger_chars > 0) {
            std::cerr << ", token_trigger=" << distill_config.token_trigger_chars
                      << " chars, cooldown=" << distill_config.cooldown_seconds << "s";
        }
        std::cerr << ")\n";
    }

    if (enrich_config.enabled) {
        std::cerr << "[daemon] Code enrichment enabled (interval=" << enrich_config.interval_minutes
                  << "m, batch=" << enrich_config.batch_size
                  << ", idle=" << enrich_config.idle_seconds << "s)\n";
    }

    if (!pid_file.empty()) {
        std::ofstream pf(pid_file);
        if (pf) pf << getpid() << "\n";
    }

    // server already bound and started (early socket in main)

    FieldRpcHandler handler(&field_store, yantra);
    handler.set_distill_model(distill_config.model);
    handler.set_distill_enabled(distill_config.enabled);

    // Start subconscious background processor
    Subconscious subconscious(&field_store, yantra, subconscious_config);

    subconscious.start();
    handler.set_subconscious(&subconscious);
    subconscious.set_rpc_mutex(&handler.rpc_mutex());

    // HTTP visualization server (optional)
    std::unique_ptr<VizServer> viz_server;
    if (http_port > 0) {
        std::string viz_dir = http_static_dir;
        if (viz_dir.empty()) {
            // Derive from executable path: go up to cc-soul/, then docs/mind-viz/
#ifdef __linux__
            auto exe_path = std::filesystem::read_symlink("/proc/self/exe");
            viz_dir = exe_path.parent_path().parent_path().string() + "/docs/mind-viz";
#else
            // Fallback: derive from argv[0] or current working directory
            viz_dir = std::filesystem::current_path().string() + "/docs/mind-viz";
#endif
        }
        viz_server = std::make_unique<VizServer>(http_port, viz_dir, &field_store);
        viz_server->start();
        std::cerr << "[VizServer] listening on :" << http_port
                  << " (static=" << viz_dir << ")\n";
        handler.set_recall_callback(
            [&viz_server](const std::vector<uint64_t>& ids, int passes) {
                if (viz_server) viz_server->push_recall_event(ids, passes);
            });
    }

    // Sadhana manager — deferred until FieldStore is ready
    std::unique_ptr<SadhanaManager> sadhana_manager;
    std::cerr << "[daemon] Sadhana manager will init when chitta-field is ready\n";

    // Wire dream/think callbacks (unless --no-autonomous)
    if (!no_autonomous) {
        // Wire dream callback: auto-explore when soul has been idle for 10+ minutes
        subconscious.set_dream_callback([&]() {
            if (sadhana_manager)
                for (const auto& s : sadhana_manager->list_active())
                    if (s.goal_dsl.value("kind", "") == "dream") return;
            try {
                json req = {
                    {"method", "tools/call"},
                    {"params", {
                        {"name", "dream_wander"},
                        {"arguments", json::object()}
                    }},
                    {"id", nullptr}
                };
                handler.handle(req);
                std::cerr << "[dream] Auto-dream triggered\n";
            } catch (const std::exception& e) {
                std::cerr << "[dream] Auto-dream failed: " << e.what() << "\n";
            }
        });

        // Wire think callback: internal memory synthesis when idle 5+ min, hourly
        subconscious.set_think_callback([&]() {
            if (sadhana_manager)
                for (const auto& s : sadhana_manager->list_active())
                    if (s.goal_dsl.value("kind", "") == "think") return;
            try {
                json req = {
                    {"method", "tools/call"},
                    {"params", {
                        {"name", "think_wander"},
                        {"arguments", json::object()}
                    }},
                    {"id", nullptr}
                };
                handler.handle(req);
                std::cerr << "[think] Auto-think triggered\n";
            } catch (const std::exception& e) {
                std::cerr << "[think] Auto-think failed: " << e.what() << "\n";
            }
        });

        // Wire belief maintenance callback
        subconscious.set_maintenance_callback([&handler]() {
            handler.run_belief_maintenance();
        });
    }

    std::signal(SIGTERM, daemon_signal_handler);
    std::signal(SIGINT, daemon_signal_handler);
    std::signal(SIGPIPE, SIG_IGN);

    std::cerr << "[daemon] Started (socket=" << socket_path
              << ", interval=" << interval << "s, pid=" << getpid() << ")\n";

#ifdef __linux__
    // Record binary mtime at startup for self-update detection.
    // /proc/self/exe resolves to the actual binary path even through symlinks.
    auto startup_binary_mtime = std::filesystem::last_write_time("/proc/self/exe");
#endif

    // Maintenance thread - sync and apply decay periodically
    std::atomic<size_t> cycle_count{0};
    std::thread maintenance([&]() {
        auto interval_secs = std::chrono::seconds(interval);
        auto last_sync = std::chrono::steady_clock::now();
        auto last_embedding_flush = std::chrono::steady_clock::now();
        auto last_foreign_sync = std::chrono::steady_clock::now();
        auto last_binary_check = std::chrono::steady_clock::now();
        auto embedding_flush_interval = std::chrono::seconds(5);   // Flush queued embeddings every 5s
        auto foreign_sync_interval   = std::chrono::seconds(5);   // Ingest peer segment files every 5s
        auto binary_check_interval   = std::chrono::seconds(60);  // Check for updated binary every 60s

#ifdef __linux__
        // inotify watcher on segments/ dir for same-host peer writes
        int inotify_fd = inotify_init1(IN_NONBLOCK);
        std::string seg_dir = mind_path + "/chitta-field/segments";
        if (inotify_fd >= 0) {
            inotify_add_watch(inotify_fd, seg_dir.c_str(),
                              IN_CREATE | IN_MODIFY | IN_CLOSE_WRITE);
        }
#else
        int inotify_fd = -1;  // No inotify on non-Linux; rely on polling fallback
#endif

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

            // Backfill runs in a dedicated thread (backfill_thread below).

            // Tick sadhana manager for autonomous agents (runs every 100ms loop)
            try {
                if (sadhana_manager) sadhana_manager->tick();
            } catch (const std::exception& e) {
                std::cerr << "[maint] Sadhana tick failed: " << e.what() << "\n";
            }

            // Ingest new ops from peer instances.
            // Triggered immediately by inotify (same-host) or every 5s (NFS fallback).
            bool seg_event = false;
#ifdef __linux__
            if (inotify_fd >= 0) {
                alignas(struct inotify_event) char ibuf[4096];
                ssize_t len = ::read(inotify_fd, ibuf, sizeof(ibuf));
                if (len > 0) seg_event = true;
            }
#endif
            if (seg_event || (now_time - last_foreign_sync >= foreign_sync_interval)) {
                last_foreign_sync = now_time;
                try {
                    auto _lk = handler.acquire_shared_lock();
                    int applied = field_store.sync_foreign();
                    if (verbose_mode && applied > 0) {
                        std::cerr << "[maint] sync_foreign: applied " << applied << " peer ops\n";
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[maint] sync_foreign failed: " << e.what() << "\n";
                }
            }

#ifdef __linux__
            // Self-update detection: restart if binary has been replaced on disk.
            // Uses /proc/self/exe and systemctl — Linux only.
            if (now_time - last_binary_check >= binary_check_interval) {
                last_binary_check = now_time;
                try {
                    auto current_mtime = std::filesystem::last_write_time("/proc/self/exe");
                    if (current_mtime != startup_binary_mtime) {
                        std::cerr << "[maint] Binary updated, restarting daemon...\n";
                        { auto _lk = handler.acquire_lock(); field_store.flush(); }
                        daemon_running = false;
                        ::execlp("systemctl", "systemctl", "--user", "restart", "chittad", nullptr);
                        char self_path[PATH_MAX];
                        ssize_t len = ::readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
                        if (len > 0) {
                            self_path[len] = '\0';
                            ::execv(self_path, nullptr);
                        }
                        ::exit(0);
                    }
                } catch (...) {}
            }
#endif

            if (now_time - last_sync >= interval_secs) {
                last_sync = now_time;
                cycle_count++;

                try {
                    auto _lk = handler.acquire_shared_lock();
                    field_store.flush();
                    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    auto [demoted, pruned] = field_store.run_demotion(now_ms);
                    if (verbose_mode && (demoted > 0 || pruned > 0)) {
                        std::cerr << "[maint] Cycle " << cycle_count
                                  << ": demoted=" << demoted
                                  << ", pruned=" << pruned << "\n";
                    }
                    if (verbose_mode) {
                        std::cerr << "[maint] Cycle " << cycle_count << " complete\n";
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[maint] Cycle failed: " << e.what() << "\n";
                }
            }
        }
#ifdef __linux__
        if (inotify_fd >= 0) close(inotify_fd);
#endif
    });

    // Backfill thread — re-embeds memories stored while yantra was unavailable.
    // Runs independently so ONNX calls never block the maintenance thread or RPCs.
    std::thread backfill_thread([&]() {
        const std::string prefix = "search_document: ";
        // Initial delay: let WAL replay and HNSW load finish before hammering ONNX.
        for (int i = 0; i < 150 && daemon_running; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

        while (daemon_running) {
            if (yantra) {
                try {
                    std::vector<uint64_t> pending;
                    std::vector<std::string> contents;
                    {
                        auto _lk = handler.acquire_lock();
                        pending = field_store.pending_embeddings(100);
                        contents.reserve(pending.size());
                        for (uint64_t id : pending)
                            contents.push_back(field_store.get_content(id));
                    }
                    if (!pending.empty()) {
                        std::cerr << "[backfill] Processing " << pending.size() << " memories\n";
                        constexpr size_t SUB_BATCH = 8;
                        for (size_t b = 0; b < pending.size() && daemon_running; b += SUB_BATCH) {
                            size_t end = std::min(b + SUB_BATCH, pending.size());
                            std::vector<std::string> batch_texts;
                            batch_texts.reserve(end - b);
                            for (size_t i = b; i < end; ++i)
                                batch_texts.push_back(contents[i].empty() ? "" : prefix + contents[i]);
                            auto arthas = yantra->transform_batch(batch_texts);
                            for (size_t i = 0; i < arthas.size() && (b + i) < pending.size(); ++i) {
                                if (contents[b + i].empty()) continue;
                                const auto& emb = arthas[i].nu.data;
                                if (emb.size() != EMBED_DIM) continue;
                                auto _lk = handler.acquire_lock();
                                field_store.backfill_embedding(pending[b + i], emb);
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    if (verbose_mode)
                        std::cerr << "[backfill] Error: " << e.what() << "\n";
                }
            }
            // 30s sleep in small increments to stay responsive to shutdown
            for (int i = 0; i < 300 && daemon_running; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    // Distillation thread - process transcripts periodically
    std::atomic<size_t> distill_count{0};
    std::thread distillation([&]() {
        if (!distill_config.enabled) return;

        auto interval_mins = std::chrono::minutes(distill_config.interval_minutes);
        auto last_distill = std::chrono::steady_clock::now();

        // Initial delay to let things settle — interruptible on shutdown
        for (int _i = 0; _i < 30 && daemon_running; ++_i)
            std::this_thread::sleep_for(std::chrono::seconds(1));

        auto last_busy_skip_start = std::chrono::steady_clock::now();

        while (daemon_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            auto now_time = std::chrono::steady_clock::now();
            if (now_time - last_distill >= interval_mins) {
                // Skip if daemon is actively handling queries (prevent blocking)
                if (!subconscious.is_idle()) {
                    auto busy_duration = now_time - last_busy_skip_start;
                    if (busy_duration < std::chrono::minutes(5)) {
                        if (verbose_mode) {
                            std::cerr << "[distill] Skipping - daemon is busy\n";
                        }
                        continue;  // Still within cap, skip
                    }
                    std::cerr << "[distill] Busy-skip cap reached, running distillation anyway\n";
                }
                last_busy_skip_start = now_time;

                last_distill = now_time;

                try {
                    // Scan transcript directory for pending transcripts
                    std::vector<TranscriptState> transcripts;
                    std::string transcript_dir = mind_path + "/transcripts";
                    try {
                        for (const auto& entry : std::filesystem::directory_iterator(transcript_dir)) {
                            if (!entry.is_regular_file()) continue;
                            auto path = entry.path();
                            if (path.extension() != ".jsonl") continue;
                            TranscriptState ts;
                            ts.session_id = path.stem().string();
                            ts.transcript_path = path.string();
                            ts.realm = "brahman";
                            // Look up realm from transcript registry
                            std::optional<std::string> reg, progress;
                            {
                                auto _lk = handler.acquire_lock();
                                reg = field_store.get_latest_event("transcript", "register", ts.session_id);
                                progress = field_store.get_latest_event("transcript", "progress", ts.session_id);
                            }
                            if (reg) {
                                try {
                                    auto r = json::parse(*reg);
                                    ts.realm = r.value("realm", "brahman");
                                } catch (...) {}
                            }
                            ts.last_processed_line = 0;
                            // Check FieldStore for last progress event
                            if (progress) {
                                try {
                                    auto p = json::parse(*progress);
                                    ts.last_processed_line = p.value("last_line", (int64_t)0);
                                } catch (...) {}
                            }
                            transcripts.push_back(ts);
                        }
                    } catch (const std::filesystem::filesystem_error&) {
                        // transcript_dir may not exist yet
                    }

                    size_t processed = 0;
                    for (const auto& state : transcripts) {
                        if (!daemon_running) break;

                        if (run_distillation(field_store, yantra, state, distill_config, &handler)) {
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

    // Code enrichment thread — disabled pending CfSymbolHit description field in FFI
    std::atomic<size_t> enrich_count{0};
    std::thread enrichment([&]() {
        // Enrichment requires CfSymbolHit.description field (not yet in C FFI).
        // Will be re-enabled when FieldRpcHandler is fully migrated.
        (void)enrich_config;
        (void)enrich_count;
    });

    // Queue processor - handles fire-and-forget writes from hooks
    std::atomic<size_t> queue_count{0};
    std::atomic<size_t> queue_distill_count{0};  // Separate counter for pre-compact distillations
    std::atomic<size_t> queue_fail_count{0};
    std::string queue_path = "/tmp/chitta-queue.jsonl";
    std::string failed_queue_path = mind_path + "/.failed_queue.jsonl";

    // Token-triggered distillation: per-session content accumulators
    std::unordered_map<std::string, int64_t> session_content_accum;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> session_last_distill;
    std::mutex session_accum_mutex;  // Protect the maps

    QueueProcessor queue_proc(field_store, yantra, distill_config, handler,
                              queue_path, failed_queue_path,
                              queue_count, queue_distill_count, queue_fail_count);
    queue_proc.start();

    // Thread pool for async RPC handling (scales 2-16 workers based on load)
    ThreadPool pool(2, 16);

    // Watchdog callback - log stuck operations
    pool.set_watchdog_callback([]([[maybe_unused]] const std::string& method, [[maybe_unused]] int64_t secs) {
        std::cerr << "[watchdog] Stuck operation: " << method << " (" << secs << "s)\n";
    });
    pool.set_escalation_threshold(std::chrono::seconds(30));

    std::cerr << "[daemon] Thread pool started (" << pool.worker_count() << " workers)\n";
    std::cerr << "[daemon] Queue processor started (path=" << queue_path << ")\n";

    // SadhanaManager init — FieldStore is ready synchronously
    sadhana_manager = std::make_unique<SadhanaManager>(field_store);
    handler.set_sadhana_manager(sadhana_manager.get());
    handler.set_queue_stats(&queue_count, &queue_fail_count, failed_queue_path);
    sadhana_manager->set_stream_fn([&server](int fd, std::string line) {
        server.queue_response(fd, std::move(line));
    });
    server.set_disconnect_callback([&sadhana_manager](int fd) {
        if (sadhana_manager) sadhana_manager->stream_unsubscribe(fd);
    });
    if (no_autonomous) {
        auto running = sadhana_manager->list("running");
        if (!running.empty()) {
            std::cerr << "[daemon] --no-autonomous: pausing " << running.size() << " running sadhana(s)\n";
            for (const auto& s : running) {
                sadhana_manager->pause(s.id);
            }
        }
    }
    std::cerr << "[daemon] Sadhana manager initialized\n";
    std::cerr << "[daemon] chitta-field active: " << field_store.memory_count()
              << " memories, " << field_store.symbol_count() << " symbols\n";

    // Main loop - handle socket I/O (never blocks on RPC)
    auto last_stats = std::chrono::steady_clock::now();
    while (daemon_running) {
        // 1. Poll for I/O (fast, non-blocking)
        auto requests = server.poll(50);  // 50ms timeout for responsiveness

        // 2. Dispatch RPC requests to thread pool
        for (const auto& req : requests) {
            // Special commands (fast, handle directly)
            if (req.data == "stats") {
                server.respond(req.client_fd, generate_stats(field_store, yantra));
                continue;
            }
            if (req.data == "shutdown") {
                server.respond(req.client_fd, R"({"status":"shutting_down"})");
                { auto _lk = handler.acquire_lock(); field_store.flush(); }
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
                if (sadhana_manager) sadhana_manager->stream_subscribe(req.client_fd, watch_id);
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

            // All other requests go to thread pool (health_check included — main thread must not block on rpc_mutex_)
            pool.submit(req.client_fd, tool_name,
                [&handler, &pool, data = req.data, tname = tool_name]() {
                    auto request = json::parse(data);
                    auto response = handler.handle(request);
                    if (tname == "health_check" && response.contains("result") && response["result"].contains("structured")) {
                        response["result"]["structured"]["pool_workers"] = pool.worker_count();
                        response["result"]["structured"]["pool_active"] = pool.active_count();
                        response["result"]["structured"]["pool_pending"] = pool.pending();
                    }
                    return response.dump(-1, ' ', false, json::error_handler_t::replace);
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

    // Stop socket first — removes file immediately, prevents new connections
    // while threads finish their current work.
    server.stop();
    if (viz_server) viz_server->stop();

    // Clean pid/lock BEFORE joining: distillation/enrichment may be in a long
    // LLM call (>15s) that cannot be interrupted; the alarm below will _Exit
    // the process if joins overrun, and we want the next daemon start to find
    // a clean state without relying on cleanup_stale_daemon().
    if (!pid_file.empty()) std::remove(pid_file.c_str());
    release_lock(lock);

    // Watchdog: force-exit if background threads don't finish within 15s.
    // The pid/lock cleanup above already ran, so a forced _Exit leaves clean
    // state for the next daemon. Detaching threads is unsafe — captures-by-ref
    // outlive main()'s stack frame, risking UAF under heavy concurrency.
    std::signal(SIGALRM, [](int) {
        std::cerr << "[daemon] Shutdown timeout — forcing exit\n";
        std::_Exit(0);
    });
    alarm(15);

    maintenance.join();
    if (backfill_thread.joinable()) backfill_thread.join();
    if (distillation.joinable()) distillation.join();
    if (enrichment.joinable()) enrichment.join();
    queue_proc.stop();
    subconscious.stop();

    alarm(0);  // Cancel watchdog — all threads finished normally

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
        // 30s timeout: daemon may be finishing distillation or other long-running work
        client.wait_for_socket_gone(30000);
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

int cmd_stats(FieldStore& field_store, VakYantra* yantra) {
    std::cout << "Soul Statistics (chitta-field)\n";
    std::cout << "═══════════════════════════════\n";
    std::cout << "  Memories: " << field_store.memory_count() << "\n";
    std::cout << "  Symbols:  " << field_store.symbol_count() << "\n";
    std::cout << "  Yantra:   " << (yantra ? "ready" : "not attached") << "\n";
    return 0;
}

int cmd_metrics(FieldStore& field_store, [[maybe_unused]] int days = 7) {
    int64_t n_memories = field_store.memory_count();
    int64_t n_symbols = field_store.symbol_count();

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Soul Metrics (" << days << "d)\n";
    std::cout << "═══════════════════════════════\n";
    std::cout << "  Memories: " << n_memories << "\n";
    std::cout << "  Symbols:  " << n_symbols << "\n";
    std::cout << "\n  Note: Detailed SUS metrics require SoulProjection (future work).\n";

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
              << "  --path PATH        Mind storage path (chitta-field)\n"
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
              << "  --distill-model MODEL    LLM model for distillation (default: gemma4:26b)\n"
              << "  --distill-token-trigger N  Token-triggered: chars threshold (default: 120000 ~30k tokens, 0=off)\n"
              << "  --distill-cooldown SECS  Min seconds between token-triggered distillations (default: 180)\n"
              << "  --distill-max-tokens N   LLM output token limit (default: 8192)\n"
              << "  --distill-context-chars N  Max input chars (0=unlimited, default: 0)\n"
              << "  --no-distill             Disable automatic distillation\n"
              << "\nCode Enrichment (semantic descriptions):\n"
              << "  --enrich-interval MINS   Enrichment interval (default: 2)\n"
              << "  --enrich-batch N         Symbols per batch (default: 10)\n"
              << "  --enrich-model MODEL     LLM model for enrichment (default: gemma4:26b)\n"
              << "  --no-enrich              Disable code enrichment\n"
              << "\nSubconscious (background processing):\n"
              << "  --no-hygiene             Disable hygiene (decay, pruning, consolidation)\n"
              << "  --no-autonomous          Disable autonomous agents (dream, think)\n"
              << "  --embed-interval SECS    Enable background embedding of wisdom memories (default: off)\n"
              << "\nHTTP Visualization Server:\n"
              << "  --http-port PORT         Enable HTTP viz server on PORT (0=disabled, default)\n"
              << "  --http-static-dir PATH   Static file directory (default: auto-detect docs/mind-viz/)\n"
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

    // Subconscious config
    SubconsciousConfig subconscious_config;
    bool no_autonomous = false;  // Disable dream/think callbacks

    // HTTP viz server config
    int http_port = 0;
    std::string http_static_dir;

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
        } else if (strcmp(argv[i], "--distill-max-tokens") == 0 && i + 1 < argc) {
            distill_config.max_tokens = safe_stoi(argv[++i], "--distill-max-tokens");
        } else if (strcmp(argv[i], "--distill-context-chars") == 0 && i + 1 < argc) {
            distill_config.max_context_chars = static_cast<size_t>(safe_stoi(argv[++i], "--distill-context-chars"));
        } else if (strcmp(argv[i], "--no-distill") == 0) {
            distill_config.enabled = false;
        } else if (strcmp(argv[i], "--no-enrich") == 0) {
            enrich_config.enabled = false;
        } else if (strcmp(argv[i], "--no-hygiene") == 0) {
            subconscious_config.enable_hygiene = false;
        } else if (strcmp(argv[i], "--embed-interval") == 0 && i + 1 < argc) {
            subconscious_config.enable_background_embedding = true;
            subconscious_config.embedding_interval = std::chrono::seconds(safe_stoi(argv[++i], "--embed-interval"));
        } else if (strcmp(argv[i], "--no-autonomous") == 0) {
            no_autonomous = true;
        } else if (strcmp(argv[i], "--http-port") == 0 && i + 1 < argc) {
            http_port = safe_stoi(argv[++i], "--http-port");
        } else if (strcmp(argv[i], "--http-static-dir") == 0 && i + 1 < argc) {
            http_static_dir = argv[++i];
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
    yantra_config.query_prefix = "search_query: ";
    yantra_config.doc_prefix   = "search_document: ";
    auto inner_yantra = std::make_shared<AntahkaranaYantra>(yantra_config);
    std::shared_ptr<VakYantra> yantra;
    if (inner_yantra->awaken(model_path, vocab_path)) {
        // Wrap with timeout protection (30s to accommodate batched backfill calls)
        yantra = std::make_shared<TimeoutYantra>(inner_yantra, std::chrono::milliseconds(30000));
        std::cerr << "[Yantra] Awakened (timeout-protected)\n";
    } else {
        std::cerr << "[Yantra] Failed: " << inner_yantra->error() << "\n";
        // yantra stays nullptr
    }
#endif

    // Open chitta-field store — the sole storage backend
    std::string field_path = mind_path + "/chitta-field";
    std::unique_ptr<FieldStore> field_store_ptr;

    // For daemon: clean up stale files then bind socket early so clients get
    // "warming_up" instead of "connection refused" during the 3+ minute snapshot load.
    if (command == "daemon") {
        if (!cleanup_stale_daemon(mind_path)) {
            std::cerr << "[daemon] Another daemon is running\n";
            return 1;
        }
    }
    std::unique_ptr<SocketServer> early_server;
    if (command == "daemon") {
        early_server = std::make_unique<SocketServer>(sock_path);
        if (!early_server->start()) {
            std::cerr << "[daemon] Another daemon is running (socket in use)\n";
            return 1;
        }
        std::cerr << "[socket_server] Listening (warming up) on " << sock_path << "\n";
    }

    static const std::string warming_resp =
        "{\"id\":null,\"result\":{\"text\":\"daemon loading, please retry\",\"structured\":{\"status\":\"warming_up\"}},\"error\":null}\n";
    std::atomic<bool> load_done{false};
    std::exception_ptr load_ex;
    std::thread loader([&]() {
        try {
            field_store_ptr = std::make_unique<FieldStore>(field_path, field_path);
        } catch (...) {
            load_ex = std::current_exception();
        }
        load_done.store(true, std::memory_order_release);
    });
    while (!load_done.load(std::memory_order_acquire)) {
        if (early_server) {
            auto reqs = early_server->poll(100);
            for (auto& r : reqs) early_server->respond(r.client_fd, warming_resp);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    loader.join();
    if (load_ex) {
        try { std::rethrow_exception(load_ex); }
        catch (const std::exception& e) {
            std::cerr << "[daemon] Failed to open chitta-field store at " << field_path << ": " << e.what() << "\n";
        }
        return 1;
    }

    FieldStore& field_store = *field_store_ptr;
    VakYantra* yantra_raw = nullptr;
#ifdef CHITTA_WITH_ONNX
    yantra_raw = yantra.get();
#endif
    std::cerr << "[Backend] chitta-field (" << field_store.memory_count() << " memories)\n";

    int result = 0;
    if (command == "daemon") {
        result = cmd_daemon(field_store, yantra_raw, interval, *early_server, sock_path, mind_path, pid_file, distill_config, enrich_config, subconscious_config, no_autonomous, http_port, http_static_dir);
    } else if (command == "stats") {
        result = cmd_stats(field_store, yantra_raw);
    } else if (command == "metrics") {
        result = cmd_metrics(field_store);
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

        bool success = run_distillation(field_store, yantra_raw, state, distill_config, nullptr, false);
        result = success ? 0 : 1;
    } else {
        std::cerr << "Unknown command: " << command << "\n";
        print_usage(argv[0]);
        result = 1;
    }

    return result;
}
