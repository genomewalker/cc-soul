// chitta-cli: Simplified daemon for SimpleMind
//
// Usage: chittad <command> [options]
//
// Commands:
//   daemon     Run background daemon
//   shutdown   Stop running daemon
//   status     Check daemon status
//   stats      Show soul statistics

#include <sys/inotify.h>
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
               const std::string& socket_path, const std::string& mind_path,
               const std::string& pid_file,
               const DistillConfig& distill_config, EnrichConfig& enrich_config,
               const SubconsciousConfig& subconscious_config, bool no_autonomous,
               int http_port, const std::string& http_static_dir) {
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
            std::cerr << "[daemon] Code enrichment enabled (interval=" << enrich_config.interval_minutes
                      << "m, batch=" << enrich_config.batch_size
                      << ", idle=" << enrich_config.idle_seconds << "s)\n";
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

    FieldRpcHandler handler(&field_store, yantra);
    handler.set_distill_model(distill_config.model);
    handler.set_distill_enabled(distill_config.enabled);

    // Start subconscious background processor
    Subconscious subconscious(&field_store, yantra, subconscious_config);

    subconscious.start();
    handler.set_subconscious(&subconscious);

    // HTTP visualization server (optional)
    std::unique_ptr<VizServer> viz_server;
    if (http_port > 0) {
        std::string viz_dir = http_static_dir;
        if (viz_dir.empty()) {
            // Derive from executable path: go up to cc-soul/, then docs/mind-viz/
            auto exe_path = std::filesystem::read_symlink("/proc/self/exe");
            viz_dir = exe_path.parent_path().parent_path().string() + "/docs/mind-viz";
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
    }

    std::signal(SIGTERM, daemon_signal_handler);
    std::signal(SIGINT, daemon_signal_handler);
    std::signal(SIGPIPE, SIG_IGN);

    std::cerr << "[daemon] Started (socket=" << socket_path
              << ", interval=" << interval << "s, pid=" << getpid() << ")\n";

    // Record binary mtime at startup for self-update detection.
    // /proc/self/exe resolves to the actual binary path even through symlinks.
    auto startup_binary_mtime = std::filesystem::last_write_time("/proc/self/exe");

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
        auto pending_embed_interval  = std::chrono::seconds(30);  // Backfill embed_pending memories
        auto last_pending_embed = std::chrono::steady_clock::now();

        // inotify watcher on segments/ dir for same-host peer writes
        int inotify_fd = inotify_init1(IN_NONBLOCK);
        std::string seg_dir = mind_path + "/chitta-field/segments";
        if (inotify_fd >= 0) {
            inotify_add_watch(inotify_fd, seg_dir.c_str(),
                              IN_CREATE | IN_MODIFY | IN_CLOSE_WRITE);
        }

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

            // Backfill embeddings for memories stored while yantra was unavailable
            if (yantra && (now_time - last_pending_embed >= pending_embed_interval)) {
                last_pending_embed = now_time;
                try {
                    auto pending = field_store.pending_embeddings(50);
                    if (!pending.empty()) {
                        std::cerr << "[maint] Backfilling " << pending.size() << " pending embeddings\n";
                        for (uint64_t id : pending) {
                            auto mem = field_store.get_content(id);
                            if (!mem.empty()) {
                                auto emb = yantra->transform(mem).nu.data;
                                if (emb.size() == 768) {
                                    field_store.backfill_embedding(id, emb);
                                }
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    if (verbose_mode)
                        std::cerr << "[maint] Backfill error: " << e.what() << "\n";
                }
            }

            // Tick sadhana manager for autonomous agents (runs every 100ms loop)
            try {
                if (sadhana_manager) sadhana_manager->tick();
            } catch (const std::exception& e) {
                std::cerr << "[maint] Sadhana tick failed: " << e.what() << "\n";
            }

            // Ingest new ops from peer instances.
            // Triggered immediately by inotify (same-host) or every 5s (NFS fallback).
            bool seg_event = false;
            if (inotify_fd >= 0) {
                alignas(struct inotify_event) char ibuf[4096];
                ssize_t len = ::read(inotify_fd, ibuf, sizeof(ibuf));
                if (len > 0) seg_event = true;
            }
            if (seg_event || (now_time - last_foreign_sync >= foreign_sync_interval)) {
                last_foreign_sync = now_time;
                try {
                    int applied = field_store.sync_foreign();
                    if (verbose_mode && applied > 0) {
                        std::cerr << "[maint] sync_foreign: applied " << applied << " peer ops\n";
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[maint] sync_foreign failed: " << e.what() << "\n";
                }
            }

            // Self-update detection: restart if binary has been replaced on disk.
            if (now_time - last_binary_check >= binary_check_interval) {
                last_binary_check = now_time;
                try {
                    auto current_mtime = std::filesystem::last_write_time("/proc/self/exe");
                    if (current_mtime != startup_binary_mtime) {
                        std::cerr << "[maint] Binary updated, restarting daemon...\n";
                        field_store.flush();
                        daemon_running = false;
                        // execv replaces process image — argv[0] resolves via PATH or symlink
                        // systemctl restart is cleaner: it waits for shutdown then relaunches
                        ::execlp("systemctl", "systemctl", "--user", "restart", "chittad", nullptr);
                        // If systemctl not found, fall back to direct exec
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

            if (now_time - last_sync >= interval_secs) {
                last_sync = now_time;
                cycle_count++;

                try {
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
        if (inotify_fd >= 0) close(inotify_fd);
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
                            auto reg = field_store.get_latest_event("transcript", "register", ts.session_id);
                            if (reg) {
                                try {
                                    auto r = json::parse(*reg);
                                    ts.realm = r.value("realm", "brahman");
                                } catch (...) {}
                            }
                            ts.last_processed_line = 0;
                            // Check FieldStore for last progress event
                            auto progress = field_store.get_latest_event("transcript", "progress", ts.session_id);
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
    std::atomic<size_t> queue_fail_count{0};
    std::string queue_path = "/tmp/chitta-queue.jsonl";
    std::string failed_queue_path = mind_path + "/.failed_queue.jsonl";

    // Token-triggered distillation: per-session content accumulators
    std::unordered_map<std::string, int64_t> session_content_accum;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> session_last_distill;
    std::mutex session_accum_mutex;  // Protect the maps

    // Helper: embed text via yantra if available, return empty vector otherwise
    auto embed_text = [yantra](const std::string& text) -> std::vector<float> {
        if (!yantra) return {};
        try {
            return yantra->transform(text).nu.data;
        } catch (...) {}
        return {};
    };

    // Helper: map category name to chitta-field memory kind
    auto category_to_kind = [](const std::string& cat) -> std::string {
        if (cat == "episode") return "episode";
        if (cat == "belief")  return "belief";
        return "wisdom";
    };

    std::thread queue_processor([&]() {
        // In-process transcript registry: session_id -> {transcript_path, realm, last_line}
        // Updated by transcript_register/transcript_progress ops; queried by distill_trigger.
        std::unordered_map<std::string, json> transcript_reg;

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

            // Process each queued request — all writes go to chitta-field
            for (const auto& line : lines) {
                if (!daemon_running) break;

                try {
                    auto j = json::parse(line);
                    std::string tool = j.value("tool", "");
                    auto args = j.value("args", json::object());

                    // FieldStore is always ready (synchronous init)

                    if (tool == "observe") {
                        std::string category = args.value("category", "wisdom");
                        std::string title = args.value("title", "");
                        std::string content = args.value("content", "");
                        if (!content.empty()) {
                            std::string source   = args.value("source", "hook_regex");
                            std::string evidence = args.value("evidence", "");

                            // ── Source trust policy (contract enforcement) ─────
                            auto policy = source_policy(source);
                            float confidence = args.contains("confidence")
                                ? args.value("confidence", 0.50f)
                                : category_to_confidence(category);
                            float decay = policy.allow_durable ? 0.005f : 0.02f;

                            // Clamp confidence to policy bounds
                            float orig_confidence = confidence;
                            confidence = std::min(confidence, policy.max_confidence);
                            confidence = std::max(confidence, policy.min_confidence);
                            if (orig_confidence != confidence) {
                                std::cerr << "[contract] confidence clamped for source=" << source
                                          << ": " << orig_confidence << "→" << confidence << "\n";
                            }
                            // Reject: distillation below floor is a bug, not a policy violation
                            if (source == "distillation" && confidence < policy.min_confidence) {
                                std::cerr << "[contract] REJECT: distillation event confidence="
                                          << confidence << " below floor=" << policy.min_confidence
                                          << " — check distillation pipeline\n";
                                queue_fail_count++;
                                continue;  // skip this queue item
                            }
                            std::string full_text = title.empty() ? content : title + "\n" + content;
                            std::string realm = args.value("realm", "brahman");
                            auto new_id = field_store.remember(category_to_kind(category), realm,
                                                  full_text, embed_text(full_text),
                                                  confidence, decay);
                            // Set initial epistemic status and memory status based on source
                            if (new_id > 0) {
                                uint8_t es = epistemic_status_for_source(source);
                                uint8_t ms = initial_status_for_source(source);
                                if (es != 1) field_store.set_epistemic_status(new_id, es);
                                if (ms != 0) field_store.set_memory_status(new_id, ms);
                            }
                            // Store provenance as triplets
                            if (new_id > 0 && !source.empty()) {
                                field_store.add_triplet(std::to_string(new_id), "source", source);
                                if (!evidence.empty())
                                    field_store.add_triplet(std::to_string(new_id), "evidence", evidence);
                            }
                            queue_count++;
                            // Correction supersession: find semantically similar memories
                            // and mark them as superseded via a triplet relation
                            if (category == "correction" && new_id > 0) {
                                // Hard contradiction policy (CONTRACTS.md §3):
                                // A correction supersedes ONLY:
                                // 1. If target_id is explicit → supersede that specific memory
                                // 2. Otherwise → same realm + cosine > 0.92 (tight threshold)
                                //    AND same kind (corrections don't supersede different memory types)
                                // force_supersede_ids: explicit list bypasses cosine threshold
                                if (args.contains("force_supersede_ids")) {
                                    auto fsi = args["force_supersede_ids"];
                                    std::vector<std::string> ids;
                                    if (fsi.is_array()) {
                                        for (auto& v : fsi) ids.push_back(v.get<std::string>());
                                    } else if (fsi.is_string()) {
                                        std::istringstream ss(fsi.get<std::string>());
                                        std::string tok;
                                        while (std::getline(ss, tok, ',')) {
                                            tok.erase(0, tok.find_first_not_of(' '));
                                            tok.erase(tok.find_last_not_of(' ') + 1);
                                            if (!tok.empty()) ids.push_back(tok);
                                        }
                                    }
                                    for (const auto& sid : ids) {
                                        try {
                                            uint64_t tid = std::stoull(sid);
                                            field_store.add_triplet(std::to_string(new_id), "supersedes", sid, 1.0f, new_id);
                                            field_store.weaken(tid, 0.15f);
                                            field_store.set_memory_status(tid, 1);
                                        } catch (...) {}
                                    }
                                }
                                auto emb = embed_text(full_text);
                                std::string target_id_str = args.value("target_id", "");
                                if (!target_id_str.empty()) {
                                    // Explicit target: targeted supersession
                                    try {
                                        uint64_t tid = std::stoull(target_id_str);
                                        field_store.add_triplet(std::to_string(new_id), "supersedes", target_id_str, 1.0f, new_id);
                                        field_store.weaken(tid, 0.15f);
                                        field_store.set_memory_status(tid, 1);
                                        std::cerr << "[contract] explicit supersession: " << new_id << "→" << target_id_str << "\n";
                                    } catch (...) {}
                                } else if (!emb.empty()) {
                                    // Semantic supersession: strict — same realm, same kind, very high threshold
                                    auto hits = field_store.recall(emb, 5, realm);
                                    for (const auto& h : hits) {
                                        if (h.memory_id == new_id) continue;
                                        if (h.realm != realm) continue;           // same realm only
                                        if (h.kind == "correction") continue;     // don't supersede other corrections
                                        if (h.score < 0.92f) continue;            // tight: 0.92 not 0.85
                                        field_store.add_triplet(std::to_string(new_id), "supersedes", std::to_string(h.memory_id), 1.0f, new_id);
                                        field_store.weaken(h.memory_id, 0.15f);
                                        field_store.set_memory_status(h.memory_id, 1);
                                        std::cerr << "[contract] semantic supersession: " << new_id << "→" << h.memory_id << " (score=" << h.score << ")\n";
                                    }
                                }
                            }
                        }
                    } else if (tool == "strengthen") {
                        std::string id_str = args.value("id", "");
                        float amount = static_cast<float>(args.value("amount", 0.1));
                        if (!id_str.empty()) {
                            try {
                                uint64_t cf_id = std::stoull(id_str);
                                field_store.strengthen(cf_id, amount);
                                queue_count++;
                            } catch (...) {}
                        }
                    } else if (tool == "connect") {
                        std::string subj = args.value("subject", "");
                        std::string pred = args.value("predicate", "");
                        std::string obj = args.value("object", "");
                        if (!subj.empty() && !pred.empty() && !obj.empty()) {
                            field_store.add_triplet(subj, pred, obj);
                            queue_count++;
                        }
                    } else if (tool == "curiosity_note_gap") {
                        std::string gap = args.value("gap", "");
                        if (!gap.empty()) {
                            std::string content = "[curiosity] " + gap;
                            field_store.remember("episode", "brahman", content,
                                                  embed_text(content), 0.7f, 0.0f);
                            queue_count++;
                        }
                    } else if (tool == "store_policy") {
                        std::string policy_type = args.value("type", "");
                        std::string content = args.value("content", "");
                        float confidence = args.value("confidence", 0.5f);
                        if (!policy_type.empty() && !content.empty()) {
                            std::string full = "[policy:" + policy_type + "] " + content;
                            field_store.remember("wisdom", "brahman", full,
                                                  embed_text(full), confidence, 0.0f);
                            queue_count++;
                        }
                    } else if (tool == "store_claim") {
                        std::string subject = args.value("subject", "");
                        std::string predicate = args.value("predicate", "");
                        std::string object_norm = args.value("object", "");
                        if (!subject.empty() && !predicate.empty() && !object_norm.empty()) {
                            field_store.add_triplet(subject, predicate, object_norm);
                            queue_count++;
                        }
                    } else if (tool == "learn_outcome") {
                        std::string id_str = args.contains("memory-id")
                            ? args.value("memory-id", "")
                            : args.value("memory_id", "");
                        std::string outcome = args.value("outcome", "");
                        std::string context = args.value("context", "");
                        if (!id_str.empty() && !outcome.empty()) {
                            try {
                                uint64_t cf_id = std::stoull(id_str);
                                json payload = {{"outcome", outcome}, {"context", context}};
                                field_store.emit_event("analytics", "outcome",
                                                        id_str, payload.dump());
                                if (outcome == "positive") field_store.strengthen(cf_id, 0.1f);
                                else if (outcome == "negative") field_store.weaken(cf_id, 0.15f);
                                queue_count++;
                            } catch (...) {}
                        }
                    } else if (tool == "session_register") {
                        std::string sid = args.value("session_id", "");
                        if (!sid.empty()) {
                            field_store.emit_event("session", "register", sid, args.dump());
                            queue_count++;
                        }
                    } else if (tool == "session_heartbeat") {
                        std::string sid = args.value("session_id", "");
                        if (!sid.empty()) {
                            field_store.emit_event("session", "heartbeat", sid,
                                                    args.value("metadata", "{}"));
                            queue_count++;
                        }
                    } else if (tool == "session_deregister") {
                        std::string sid = args.value("session_id", "");
                        if (!sid.empty()) {
                            field_store.emit_event("session", "deregister", sid, "{}");
                            queue_count++;
                        }
                    } else if (tool == "transcript_register") {
                        std::string session_id = args.value("session_id", "");
                        std::string path = args.value("transcript_path", "");
                        if (!path.empty()) {
                            std::cerr << "[queue] transcript_register: session=" << session_id << " path=" << path << "\n";
                            transcript_reg[session_id] = args;
                            field_store.emit_event("transcript", "register",
                                                    session_id, args.dump());
                            queue_count++;
                        }
                    } else if (tool == "ledger_save") {
                        std::string session_id = args.value("session_id", "");
                        if (!session_id.empty()) {
                            field_store.emit_event("admin", "ledger_save",
                                                    session_id, args.dump());
                            queue_count++;
                        }
                    } else if (tool == "narrative_log") {
                        std::string session_id = args.value("session_id", "");
                        std::string summary = args.value("summary", "");
                        if (!session_id.empty() && !summary.empty()) {
                            field_store.emit_event("analytics", "narrative_log",
                                                    session_id, args.dump());
                            queue_count++;
                        }
                    } else if (tool == "calibration_record") {
                        std::string domain = args.value("domain", "");
                        if (!domain.empty()) {
                            field_store.emit_event("analytics", "calibration",
                                                    domain, args.dump());
                            queue_count++;
                        }
                    } else if (tool == "habit_observe") {
                        std::string trigger = args.value("trigger", "");
                        std::string response = args.value("response", "");
                        if (!trigger.empty() && !response.empty()) {
                            field_store.emit_event("narrative", "habit",
                                                    trigger, args.dump());
                            queue_count++;
                        }
                    } else if (tool == "anticipation_success") {
                        int64_t id = args.value("id", (int64_t)0);
                        if (id > 0) {
                            field_store.emit_event("narrative", "anticipation_success",
                                                    std::to_string(id), "{}");
                            queue_count++;
                        }
                    } else if (tool == "store_turn") {
                        std::string session_id = args.value("session_id", "");
                        std::string content = args.value("content", "");
                        if (!session_id.empty() && !content.empty()) {
                            field_store.emit_event("transcript", "turn",
                                                    session_id, args.dump());
                            queue_count++;
                        }
                    } else if (tool == "store_relationship_event") {
                        std::string event_type = args.value("event_type", "");
                        std::string session_id = args.value("session_id", "");
                        if (!event_type.empty()) {
                            field_store.emit_event("relationship", event_type,
                                                    session_id, args.dump());
                            queue_count++;
                        }
                    } else if (tool == "log_session_tokens") {
                        std::string sid = args.value("session_id", "");
                        if (!sid.empty()) {
                            field_store.emit_event("analytics", "session_tokens",
                                                    sid, args.dump());
                            queue_count++;
                        }
                    } else if (tool == "log_correction_outcome") {
                        std::string sid = args.value("session_id", "");
                        if (!sid.empty()) {
                            field_store.emit_event("analytics", "correction_outcome",
                                                    sid, args.dump());
                            queue_count++;
                        }
                    } else if (tool == "log_exposure") {
                        std::string session_id = args.value("session_id", "");
                        if (!session_id.empty()) {
                            field_store.emit_event("analytics", "exposure",
                                                    session_id, args.dump());
                            queue_count++;
                        }
                    } else if (tool == "distill_trigger") {
                        if (!distill_config.enabled) {
                            if (verbose_mode)
                                std::cerr << "[queue] distill_trigger ignored (distillation disabled)\n";
                        } else {
                            std::string session_id = args.value("session_id", "");
                            if (!session_id.empty()) {
                                // Look up transcript path from in-process registry first,
                                // then fall back to FieldStore event log.
                                std::string transcript_path;
                                std::string realm = "brahman";
                                auto it = transcript_reg.find(session_id);
                                if (it != transcript_reg.end()) {
                                    transcript_path = it->second.value("transcript_path", "");
                                    realm = it->second.value("realm", "brahman");
                                } else {
                                    auto reg = field_store.get_latest_event("transcript", "register", session_id);
                                    if (reg) {
                                        try {
                                            auto r = json::parse(*reg);
                                            transcript_path = r.value("transcript_path", "");
                                            realm = r.value("realm", "brahman");
                                        } catch (...) {}
                                    }
                                }
                                if (!transcript_path.empty()) {
                                    std::cerr << "[queue] distill_trigger: path=" << transcript_path << " realm=" << realm << "\n";
                                    TranscriptState ts;
                                    ts.session_id = session_id;
                                    ts.transcript_path = transcript_path;
                                    ts.realm = realm;
                                    ts.last_processed_line = 0;
                                    // Check progress from in-process registry
                                    if (it != transcript_reg.end()) {
                                        ts.last_processed_line = it->second.value("last_line", (int64_t)0);
                                    } else {
                                        auto progress = field_store.get_latest_event("transcript", "progress", session_id);
                                        if (progress) {
                                            try {
                                                auto p = json::parse(*progress);
                                                ts.last_processed_line = p.value("last_line", (int64_t)0);
                                            } catch (...) {}
                                        }
                                    }
                                    if (run_distillation(field_store, yantra, ts, distill_config, &handler, true)) {
                                        queue_distill_count++;
                                        std::cerr << "[queue] distill_trigger: success (total=" << queue_distill_count.load() << ")\n";
                                    } else {
                                        std::cerr << "[queue] distill_trigger: run_distillation returned false\n";
                                    }
                                } else {
                                    std::cerr << "[queue] distill_trigger: no transcript registered for " << session_id << "\n";
                                }
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    // Always log — never silently drop a learning event
                    std::cerr << "[queue] FAILED: " << e.what() << "\n";
                    // Write to dead-letter file for inspection/retry
                    try {
                        json entry;
                        entry["error"] = e.what();
                        entry["retry_count"] = 0;
                        entry["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                        auto parsed = json::parse(line, nullptr, false);
                        if (!parsed.is_discarded()) {
                            entry["tool"] = parsed.value("tool", "unknown");
                            entry["args"] = parsed.value("args", json::object());
                        } else {
                            entry["raw"] = line.substr(0, 300);
                        }
                        std::ofstream dlf(failed_queue_path, std::ios::app);
                        dlf << entry.dump(-1, ' ', false, json::error_handler_t::replace) << "\n";
                        queue_fail_count++;
                    } catch (...) {}
                }
            }

            if (verbose_mode && !lines.empty()) {
                std::cerr << "[queue] Processed " << lines.size() << " items, total=" << queue_count << "\n";
            }
        }
    });

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
                field_store.flush();
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

            // Fast-path: health_check stays on main thread (always responsive)
            if (tool_name == "health_check") {
                auto response = handler.handle(parsed);
                // Add pool stats to health_check
                if (response.contains("result") && response["result"].contains("structured")) {
                    response["result"]["structured"]["pool_workers"] = pool.worker_count();
                    response["result"]["structured"]["pool_active"] = pool.active_count();
                    response["result"]["structured"]["pool_pending"] = pool.pending();
                }
                server.respond(req.client_fd, response.dump(-1, ' ', false, json::error_handler_t::replace));
                continue;
            }

            // All other requests go to thread pool
            pool.submit(req.client_fd, tool_name,
                [&handler, data = req.data]() {
                    auto request = json::parse(data);
                    auto response = handler.handle(request);
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
    // while threads finish their current work. Lock is held until after joins
    // to prevent conflicts with a new daemon starting too early.
    server.stop();
    if (viz_server) viz_server->stop();

    // Watchdog: force-exit if background threads don't finish within 15s.
    // Socket is already closed so no clients will be left hanging.
    // cleanup_stale_daemon() handles any leftover pid/lock files on next start.
    std::signal(SIGALRM, [](int) { std::_Exit(0); });
    alarm(15);

    maintenance.join();
    if (distillation.joinable()) distillation.join();
    if (enrichment.joinable()) enrichment.join();
    if (queue_processor.joinable()) queue_processor.join();
    subconscious.stop();

    alarm(0);  // Cancel watchdog — all threads finished normally

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
              << "  --distill-model MODEL    OpenCode model (default: github-copilot/gpt-5-mini)\n"
              << "  --distill-local-model PATH  Local GGUF model via llama-cli (overrides opencode)\n"
              << "  --distill-token-trigger N  Token-triggered: chars threshold (default: 120000 ~30k tokens, 0=off)\n"
              << "  --distill-cooldown SECS  Min seconds between token-triggered distillations (default: 180)\n"
              << "  --no-distill             Disable automatic distillation\n"
              << "\nCode Enrichment (semantic descriptions):\n"
              << "  --enrich-interval MINS   Enrichment interval (default: 2)\n"
              << "  --enrich-batch N         Symbols per batch (default: 10)\n"
              << "  --enrich-model MODEL     OpenCode model (default: github-copilot/gpt-5-mini)\n"
              << "  --no-enrich              Disable code enrichment\n"
              << "\nSubconscious (background processing):\n"
              << "  --no-hygiene             Disable hygiene (decay, pruning, consolidation)\n"
              << "  --no-autonomous          Disable autonomous agents (dream, think)\n"
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
        } else if (strcmp(argv[i], "--distill-local-model") == 0 && i + 1 < argc) {
            distill_config.local_model_path = argv[++i];
        } else if (strcmp(argv[i], "--distill-token-trigger") == 0 && i + 1 < argc) {
            distill_config.token_trigger_chars = safe_stoi(argv[++i], "--distill-token-trigger");
        } else if (strcmp(argv[i], "--distill-cooldown") == 0 && i + 1 < argc) {
            distill_config.cooldown_seconds = safe_stoi(argv[++i], "--distill-cooldown");
        } else if (strcmp(argv[i], "--no-distill") == 0) {
            distill_config.enabled = false;
        } else if (strcmp(argv[i], "--no-enrich") == 0) {
            enrich_config.enabled = false;
        } else if (strcmp(argv[i], "--no-hygiene") == 0) {
            subconscious_config.enable_hygiene = false;
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

    // Open chitta-field store — the sole storage backend
    std::string field_path = mind_path + "/chitta-field";
    std::unique_ptr<FieldStore> field_store_ptr;
    try {
        field_store_ptr = std::make_unique<FieldStore>(field_path, field_path);
    } catch (const std::exception& e) {
        std::cerr << "[daemon] Failed to open chitta-field store at " << field_path << ": " << e.what() << "\n";
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
        result = cmd_daemon(field_store, yantra_raw, interval, sock_path, mind_path, pid_file, distill_config, enrich_config, subconscious_config, no_autonomous, http_port, http_static_dir);
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
