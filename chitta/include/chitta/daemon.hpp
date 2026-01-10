#pragma once
// Daemon: the soul's autonomous heartbeat
//
// A living system breathes without being told to.
// The daemon runs decay, coherence, pruning, and dreaming in the background.

#include "types.hpp"
#include "graph.hpp"
#include "dynamics.hpp"
#include "dream.hpp"
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>

namespace chitta {

// Daemon configuration
struct DaemonConfig {
    int64_t tick_interval_ms = 60000;       // 1 minute between ticks
    int64_t decay_interval_ms = 3600000;    // 1 hour between decay cycles
    int64_t coherence_interval_ms = 300000; // 5 minutes between coherence checks
    int64_t prune_interval_ms = 86400000;   // 1 day between prune cycles
    int64_t save_interval_ms = 300000;      // 5 minutes between saves
    int64_t dream_interval_ms = 1800000;    // 30 minutes between dream cycles
    float prune_threshold = 0.05f;          // Confidence below this = prune
    float coherence_alert_threshold = 0.3f; // Alert if coherence drops below
};

// Daemon event types
enum class DaemonEvent {
    Tick,           // Regular heartbeat
    DecayApplied,   // Decay cycle completed
    CoherenceCheck, // Coherence measured
    Pruned,         // Dead nodes removed
    Saved,          // State persisted
    Dream,          // Dream cycle completed
    Alert           // Coherence critically low
};

// Daemon callback for events
using DaemonCallback = std::function<void(DaemonEvent, const std::string&)>;

// The autonomous daemon
class Daemon {
public:
    explicit Daemon(DaemonConfig config = {})
        : config_(config)
        , running_(false)
        , last_decay_(now())
        , last_coherence_(now())
        , last_prune_(now())
        , last_save_(now())
        , last_dream_(now())
    {}

    ~Daemon() {
        stop();
    }

    // Non-copyable, movable
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    Daemon(Daemon&& other) noexcept
        : config_(other.config_)
        , running_(other.running_.load())
        , graph_(other.graph_)
        , callback_(std::move(other.callback_))
        , save_fn_(std::move(other.save_fn_))
        , last_decay_(other.last_decay_)
        , last_coherence_(other.last_coherence_)
        , last_prune_(other.last_prune_)
        , last_save_(other.last_save_)
        , last_dream_(other.last_dream_)
        , stats_(other.stats_)
    {
        other.running_ = false;
        other.graph_ = nullptr;
    }

    Daemon& operator=(Daemon&& other) noexcept {
        if (this != &other) {
            stop();
            config_ = other.config_;
            running_ = other.running_.load();
            graph_ = other.graph_;
            callback_ = std::move(other.callback_);
            save_fn_ = std::move(other.save_fn_);
            last_decay_ = other.last_decay_;
            last_coherence_ = other.last_coherence_;
            last_prune_ = other.last_prune_;
            last_save_ = other.last_save_;
            last_dream_ = other.last_dream_;
            stats_ = other.stats_;
            other.running_ = false;
            other.graph_ = nullptr;
        }
        return *this;
    }

    // Attach to a graph (required before start)
    void attach(Graph* graph) {
        graph_ = graph;
    }

    // Set callback for events
    void on_event(DaemonCallback callback) {
        callback_ = std::move(callback);
    }

    // Set save function
    void on_save(std::function<void()> save_fn) {
        save_fn_ = std::move(save_fn);
    }

    // Start the daemon
    void start() {
        if (running_.exchange(true)) return;  // Already running

        thread_ = std::thread([this]() {
            run_loop();
        });

        emit(DaemonEvent::Tick, "Daemon started");
    }

    // Stop the daemon
    void stop() {
        if (!running_.exchange(false)) return;  // Not running

        if (thread_.joinable()) {
            thread_.join();
        }

        emit(DaemonEvent::Tick, "Daemon stopped");
    }

    bool is_running() const { return running_; }

    // Get current stats
    struct Stats {
        size_t ticks = 0;
        size_t decay_cycles = 0;
        size_t coherence_checks = 0;
        size_t dream_cycles = 0;
        size_t connections_discovered = 0;
        size_t prune_cycles = 0;
        size_t saves = 0;
        size_t nodes_pruned = 0;
        float last_coherence = 1.0f;
    };

    Stats stats() const { return stats_; }

private:
    void run_loop() {
        while (running_) {
            Timestamp current = now();

            // Regular tick
            stats_.ticks++;

            // Decay cycle
            if (current - last_decay_ >= config_.decay_interval_ms) {
                run_decay();
                last_decay_ = current;
            }

            // Coherence check
            if (current - last_coherence_ >= config_.coherence_interval_ms) {
                run_coherence_check();
                last_coherence_ = current;
            }

            // Prune cycle
            if (current - last_prune_ >= config_.prune_interval_ms) {
                run_prune();
                last_prune_ = current;
            }

            // Save cycle
            if (current - last_save_ >= config_.save_interval_ms) {
                run_save();
                last_save_ = current;
            }

            // Dream cycle
            if (current - last_dream_ >= config_.dream_interval_ms) {
                run_dream();
                last_dream_ = current;
            }

            // Sleep until next tick
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config_.tick_interval_ms));
        }
    }

    void run_decay() {
        if (!graph_) return;

        graph_->apply_decay();
        stats_.decay_cycles++;

        emit(DaemonEvent::DecayApplied, "Decay applied to all nodes");
    }

    void run_coherence_check() {
        if (!graph_) return;

        Coherence c = graph_->compute_coherence();
        stats_.coherence_checks++;
        stats_.last_coherence = c.tau_k();

        std::string msg = "Coherence: " + std::to_string(int(c.tau_k() * 100)) + "%";
        emit(DaemonEvent::CoherenceCheck, msg);

        if (c.tau_k() < config_.coherence_alert_threshold) {
            emit(DaemonEvent::Alert, "Coherence critically low!");
        }
    }

    void run_prune() {
        if (!graph_) return;

        size_t before = graph_->size();
        graph_->prune(config_.prune_threshold);
        size_t after = graph_->size();

        size_t pruned = before - after;
        stats_.prune_cycles++;
        stats_.nodes_pruned += pruned;

        std::string msg = "Pruned " + std::to_string(pruned) + " dead nodes";
        emit(DaemonEvent::Pruned, msg);
    }

    void run_save() {
        if (!save_fn_) return;

        save_fn_();
        stats_.saves++;

        emit(DaemonEvent::Saved, "State persisted");
    }

    void run_dream() {
        if (!graph_) return;

        // Collect embeddings from graph
        std::vector<Vector> embeddings;
        std::vector<NodeId> node_ids;

        auto nodes = graph_->all_nodes();
        for (const auto& node : nodes) {
            if (!node.nu.is_zero()) {
                embeddings.push_back(node.nu);
                node_ids.push_back(node.id);
            }
        }

        if (embeddings.size() < 10) {
            emit(DaemonEvent::Dream, "Not enough nodes for dreaming");
            return;
        }

        size_t connections = 0;

        // 1. Cluster similar concepts
        auto clusters = dream::cluster_kmeans(embeddings, 5, 10);

        // 2. Find highly similar pairs within clusters
        for (const auto& cluster : clusters) {
            if (cluster.members.size() < 2) continue;

            for (size_t i = 0; i < cluster.members.size(); i++) {
                for (size_t j = i + 1; j < cluster.members.size(); j++) {
                    size_t idx_i = cluster.members[i];
                    size_t idx_j = cluster.members[j];

                    float sim = embeddings[idx_i].cosine(embeddings[idx_j]);
                    if (sim > 0.7f) {
                        // Connect via triplet (similar relationship)
                        graph_->add_triplet(node_ids[idx_i], "similar_to",
                                           node_ids[idx_j], sim);
                        connections++;
                    }
                }
            }
        }

        // 3. Find gaps (unexplored regions)
        auto gaps = dream::find_gaps(embeddings, 50, 0.4f);

        stats_.dream_cycles++;
        stats_.connections_discovered += connections;

        std::string msg = "Dream: " + std::to_string(connections) + " connections, "
                        + std::to_string(gaps.size()) + " gaps";
        emit(DaemonEvent::Dream, msg);
    }

    void emit(DaemonEvent event, const std::string& msg) {
        if (callback_) {
            callback_(event, msg);
        }
    }

    DaemonConfig config_;
    std::atomic<bool> running_;
    std::thread thread_;
    Graph* graph_ = nullptr;
    DaemonCallback callback_;
    std::function<void()> save_fn_;

    Timestamp last_decay_;
    Timestamp last_coherence_;
    Timestamp last_prune_;
    Timestamp last_save_;
    Timestamp last_dream_;

    Stats stats_;
};

} // namespace chitta
