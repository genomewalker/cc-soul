#pragma once
// Dynamics: the physics of soul
//
// Not background threads. Intrinsic behavior.
// The graph doesn't sit idle - it lives.

#include "types.hpp"
#include "graph.hpp"
#include "ops.hpp"
#include <optional>

namespace synapse {

// Report from a dynamics tick
struct DynamicsReport {
    bool decay_applied = false;
    std::optional<Coherence> coherence;
    std::vector<std::string> triggers_fired;
};

// Autonomous dynamics engine
class Dynamics {
public:
    Dynamics()
        : decay_interval_ms_(3600000)  // 1 hour
        , last_decay_(now())
        , prune_threshold_(0.1f)
        , coherence_interval_ms_(300000)  // 5 minutes
        , last_coherence_(now()) {}

    // Add a trigger to the dynamics engine
    void add_trigger(Trigger trigger) {
        triggers_.push_back(std::move(trigger));
    }

    // Remove a trigger by name
    bool remove_trigger(const std::string& name) {
        size_t before = triggers_.size();
        triggers_.erase(
            std::remove_if(triggers_.begin(), triggers_.end(),
                [&name](const Trigger& t) { return t.name == name; }),
            triggers_.end());
        return triggers_.size() < before;
    }

    // Run one tick of dynamics
    DynamicsReport tick(Graph& graph) {
        DynamicsReport report;
        Timestamp current = now();

        // Apply decay if interval elapsed
        if (current - last_decay_ >= decay_interval_ms_) {
            graph.apply_decay();
            last_decay_ = current;
            report.decay_applied = true;
        }

        // Check coherence if interval elapsed
        if (current - last_coherence_ >= coherence_interval_ms_) {
            report.coherence = graph.compute_coherence();
            last_coherence_ = current;
        }

        // Check all triggers
        for (auto& trigger : triggers_) {
            if (trigger.check(graph)) {
                report.triggers_fired.push_back(trigger.name);
            }
        }

        return report;
    }

    // Initialize with default triggers for soul health
    Dynamics& with_defaults() {
        // Emergency coherence trigger
        add_trigger(Trigger(
            "emergency_coherence",
            Condition::coherence_below(0.3f),
            {Op::snapshot(), Op::prune(0.2f), Op::compute_coherence()}
        ));

        // Periodic pruning trigger
        add_trigger(Trigger(
            "prune_dead",
            Condition::always(),
            {Op::prune(0.05f)}
        ));

        return *this;
    }

private:
    std::vector<Trigger> triggers_;
    int64_t decay_interval_ms_;
    Timestamp last_decay_;
    float prune_threshold_;
    int64_t coherence_interval_ms_;
    Timestamp last_coherence_;
};

// The three cycles from Spanda
namespace cycles {

// Learning cycle: observe → learn → apply → confirm → strengthen
class LearningCycle {
public:
    std::optional<std::string> observation;
    std::optional<std::string> learning;
    std::optional<NodeId> applied;
    std::optional<bool> outcome;

    void observe(std::string what) {
        observation = std::move(what);
    }

    void learn(std::string what) {
        learning = std::move(what);
    }

    void apply(NodeId node_id) {
        applied = node_id;
    }

    void confirm(bool success, Graph& graph) {
        outcome = success;
        if (applied) {
            float delta = success ? 0.1f : -0.1f;
            Op op = success
                ? Op::strengthen(*applied, delta)
                : Op::weaken(*applied, -delta);
            op.execute(graph);
        }
    }

    bool complete() const {
        return observation.has_value() && outcome.has_value();
    }
};

// Agency phases
enum class AgencyPhase {
    Dreaming, Aspiring, Intending, Deciding, Acting, Observing
};

// Agency cycle: dream → aspire → intend → decide → act → observe
class AgencyCycle {
public:
    AgencyPhase current_phase = AgencyPhase::Dreaming;
    std::optional<NodeId> dream;
    std::optional<NodeId> aspiration;
    std::optional<NodeId> intention;
    std::optional<std::string> decision;
    std::optional<std::string> action;

    void advance() {
        switch (current_phase) {
            case AgencyPhase::Dreaming: current_phase = AgencyPhase::Aspiring; break;
            case AgencyPhase::Aspiring: current_phase = AgencyPhase::Intending; break;
            case AgencyPhase::Intending: current_phase = AgencyPhase::Deciding; break;
            case AgencyPhase::Deciding: current_phase = AgencyPhase::Acting; break;
            case AgencyPhase::Acting: current_phase = AgencyPhase::Observing; break;
            case AgencyPhase::Observing: current_phase = AgencyPhase::Dreaming; break;
        }
    }
};

// Evolution phases
enum class EvolutionPhase {
    Introspecting, Diagnosing, Proposing, Validating, Applying
};

// Evolution cycle: introspect → diagnose → propose → validate → apply
class EvolutionCycle {
public:
    EvolutionPhase current_phase = EvolutionPhase::Introspecting;
    std::vector<std::string> insights;
    std::optional<std::string> diagnosis;
    std::optional<Op> proposal;
    bool validated = false;

    void introspect(Graph& graph) {
        auto coherence = graph.coherence();
        if (coherence.tau_k() < 0.5f) {
            insights.push_back("Low coherence detected");
        }

        auto nodes = graph.query_by_type(NodeType::Wisdom);
        size_t low_count = 0;
        for (const auto& n : nodes) {
            if (n.kappa.effective() < 0.3f) low_count++;
        }

        if (low_count > 0) {
            insights.push_back(std::to_string(low_count) + " wisdom nodes with low confidence");
        }

        current_phase = EvolutionPhase::Diagnosing;
    }

    void diagnose() {
        if (!insights.empty()) {
            std::string diag = "Found " + std::to_string(insights.size()) + " issues: ";
            for (size_t i = 0; i < insights.size(); ++i) {
                if (i > 0) diag += ", ";
                diag += insights[i];
            }
            diagnosis = diag;
        }
        current_phase = EvolutionPhase::Proposing;
    }

    void propose(Op op) {
        proposal = std::move(op);
        current_phase = EvolutionPhase::Validating;
    }

    bool validate(Graph& graph) {
        graph.snapshot();
        validated = true;
        current_phase = EvolutionPhase::Applying;
        return true;
    }

    bool apply(Graph& graph) {
        if (!validated || !proposal) return false;
        proposal->execute(graph);
        return true;
    }
};

} // namespace cycles

} // namespace synapse
