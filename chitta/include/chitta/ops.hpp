#pragma once
// Operations: graph transformations
//
// Not instructions executed sequentially.
// Transformations that the graph undergoes.

#include "types.hpp"
#include "graph.hpp"
#include <variant>
#include <memory>

namespace chitta {

// Forward declarations
class Graph;
struct Condition;

// Result of executing an operation
struct OpResult {
    enum class Type { Nodes, NodeId, SnapshotId, Coherence, Count, Ok, Seq, Skipped };
    Type type = Type::Ok;

    std::vector<std::pair<NodeId, float>> nodes;
    NodeId node_id;
    uint64_t snapshot_id = 0;
    chitta::Coherence coherence;
    size_t count = 0;
    std::vector<OpResult> seq;

    static OpResult ok() { return {Type::Ok}; }
    static OpResult skipped() { return {Type::Skipped}; }
    static OpResult with_nodes(std::vector<std::pair<NodeId, float>> n) {
        OpResult r{Type::Nodes};
        r.nodes = std::move(n);
        return r;
    }
    static OpResult with_node_id(NodeId id) {
        OpResult r{Type::NodeId};
        r.node_id = id;
        return r;
    }
    static OpResult with_snapshot_id(uint64_t id) {
        OpResult r{Type::SnapshotId};
        r.snapshot_id = id;
        return r;
    }
    static OpResult with_coherence(chitta::Coherence c) {
        OpResult r{Type::Coherence};
        r.coherence = c;
        return r;
    }
    static OpResult with_count(size_t c) {
        OpResult r{Type::Count};
        r.count = c;
        return r;
    }
    static OpResult with_seq(std::vector<OpResult> s) {
        OpResult r{Type::Seq};
        r.seq = std::move(s);
        return r;
    }
};

// Conditions for conditional execution
struct Condition {
    enum class Type {
        CoherenceBelow, CoherenceAbove, ConfidenceBelow,
        Exists, Always, Never, And, Or, Not
    };

    Type type = Type::Always;
    float threshold = 0.0f;
    NodeId node_id;
    std::shared_ptr<Condition> left;
    std::shared_ptr<Condition> right;

    static Condition always() { return {Type::Always}; }
    static Condition never() { return {Type::Never}; }

    static Condition coherence_below(float t) {
        Condition c{Type::CoherenceBelow};
        c.threshold = t;
        return c;
    }

    static Condition coherence_above(float t) {
        Condition c{Type::CoherenceAbove};
        c.threshold = t;
        return c;
    }

    static Condition confidence_below(NodeId id, float t) {
        Condition c{Type::ConfidenceBelow};
        c.node_id = id;
        c.threshold = t;
        return c;
    }

    static Condition exists(NodeId id) {
        Condition c{Type::Exists};
        c.node_id = id;
        return c;
    }

    static Condition and_(Condition a, Condition b) {
        Condition c{Type::And};
        c.left = std::make_shared<Condition>(std::move(a));
        c.right = std::make_shared<Condition>(std::move(b));
        return c;
    }

    static Condition or_(Condition a, Condition b) {
        Condition c{Type::Or};
        c.left = std::make_shared<Condition>(std::move(a));
        c.right = std::make_shared<Condition>(std::move(b));
        return c;
    }

    static Condition not_(Condition a) {
        Condition c{Type::Not};
        c.left = std::make_shared<Condition>(std::move(a));
        return c;
    }

    bool evaluate(const Graph& graph) const;
};

// Graph operations
struct Op {
    enum class Type {
        Query, Insert, Connect, Strengthen, Weaken,
        Decay, Prune, Snapshot, Rollback, ComputeCoherence,
        When, Seq, Touch
    };

    Type type;

    // Query params
    Vector vector;
    float threshold = 0.0f;
    size_t limit = 10;

    // Insert params
    std::shared_ptr<Node> node;

    // Connect params
    NodeId from_id;
    NodeId to_id;
    EdgeType edge_type = EdgeType::Similar;
    float weight = 1.0f;

    // Strengthen/Weaken/Touch params
    NodeId target_id;
    float delta = 0.0f;

    // Rollback params
    uint64_t snapshot_id = 0;

    // When params
    Condition condition;
    std::vector<Op> then_ops;
    std::vector<Op> else_ops;

    // Seq params
    std::vector<Op> ops;

    // Factory methods
    static Op query(Vector v, float thresh, size_t lim) {
        Op op{Type::Query};
        op.vector = std::move(v);
        op.threshold = thresh;
        op.limit = lim;
        return op;
    }

    static Op insert(Node n) {
        Op op{Type::Insert};
        op.node = std::make_shared<Node>(std::move(n));
        return op;
    }

    static Op connect(NodeId from, NodeId to, EdgeType et, float w) {
        Op op{Type::Connect};
        op.from_id = from;
        op.to_id = to;
        op.edge_type = et;
        op.weight = w;
        return op;
    }

    static Op strengthen(NodeId id, float d) {
        Op op{Type::Strengthen};
        op.target_id = id;
        op.delta = d;
        return op;
    }

    static Op weaken(NodeId id, float d) {
        Op op{Type::Weaken};
        op.target_id = id;
        op.delta = d;
        return op;
    }

    static Op decay() { return {Type::Decay}; }

    static Op prune(float thresh) {
        Op op{Type::Prune};
        op.threshold = thresh;
        return op;
    }

    static Op snapshot() { return {Type::Snapshot}; }

    static Op rollback(uint64_t id) {
        Op op{Type::Rollback};
        op.snapshot_id = id;
        return op;
    }

    static Op compute_coherence() { return {Type::ComputeCoherence}; }

    static Op when(Condition cond, std::vector<Op> then_ops, std::vector<Op> else_ops = {}) {
        Op op{Type::When};
        op.condition = std::move(cond);
        op.then_ops = std::move(then_ops);
        op.else_ops = std::move(else_ops);
        return op;
    }

    static Op seq(std::vector<Op> ops) {
        Op op{Type::Seq};
        op.ops = std::move(ops);
        return op;
    }

    static Op touch(NodeId id) {
        Op op{Type::Touch};
        op.target_id = id;
        return op;
    }

    OpResult execute(Graph& graph) const;
};

// Trigger: condition + ops
struct Trigger {
    std::string name;
    Condition condition;
    std::vector<Op> ops;
    bool enabled = true;

    Trigger(std::string n, Condition c, std::vector<Op> o)
        : name(std::move(n)), condition(std::move(c)), ops(std::move(o)) {}

    std::optional<std::vector<OpResult>> check(Graph& graph) const {
        if (enabled && condition.evaluate(graph)) {
            std::vector<OpResult> results;
            for (const auto& op : ops) {
                results.push_back(op.execute(graph));
            }
            return results;
        }
        return std::nullopt;
    }
};

// Implementation of Condition::evaluate
inline bool Condition::evaluate(const Graph& graph) const {
    switch (type) {
        case Type::CoherenceBelow:
            return graph.coherence().tau_k() < threshold;
        case Type::CoherenceAbove:
            return graph.coherence().tau_k() > threshold;
        case Type::ConfidenceBelow:
            if (auto n = graph.get(node_id)) {
                return n->kappa.effective() < threshold;
            }
            return false;
        case Type::Exists:
            return graph.get(node_id).has_value();
        case Type::Always:
            return true;
        case Type::Never:
            return false;
        case Type::And:
            return left->evaluate(graph) && right->evaluate(graph);
        case Type::Or:
            return left->evaluate(graph) || right->evaluate(graph);
        case Type::Not:
            return !left->evaluate(graph);
    }
    return false;
}

// Implementation of Op::execute
inline OpResult Op::execute(Graph& graph) const {
    switch (type) {
        case Type::Query:
            return OpResult::with_nodes(graph.query(vector, threshold, limit));

        case Type::Insert:
            return OpResult::with_node_id(graph.insert(*node));

        case Type::Connect:
            graph.connect(from_id, to_id, edge_type, weight);
            return OpResult::ok();

        case Type::Strengthen:
            graph.with_node(target_id, [this](Node& n) {
                n.kappa.observe(n.kappa.mu + delta);
                n.touch();
            });
            return OpResult::ok();

        case Type::Weaken:
            graph.with_node(target_id, [this](Node& n) {
                n.kappa.observe(n.kappa.mu - delta);
            });
            return OpResult::ok();

        case Type::Decay:
            graph.apply_decay();
            return OpResult::ok();

        case Type::Prune:
            return OpResult::with_count(graph.prune(threshold));

        case Type::Snapshot:
            return OpResult::with_snapshot_id(graph.snapshot());

        case Type::Rollback:
            graph.rollback(snapshot_id);
            return OpResult::ok();

        case Type::ComputeCoherence:
            return OpResult::with_coherence(graph.compute_coherence());

        case Type::When:
            if (condition.evaluate(graph)) {
                std::vector<OpResult> results;
                for (const auto& op : then_ops) {
                    results.push_back(op.execute(graph));
                }
                return OpResult::with_seq(std::move(results));
            } else if (!else_ops.empty()) {
                std::vector<OpResult> results;
                for (const auto& op : else_ops) {
                    results.push_back(op.execute(graph));
                }
                return OpResult::with_seq(std::move(results));
            }
            return OpResult::skipped();

        case Type::Seq: {
            std::vector<OpResult> results;
            for (const auto& op : ops) {
                results.push_back(op.execute(graph));
            }
            return OpResult::with_seq(std::move(results));
        }

        case Type::Touch:
            graph.with_node(target_id, [](Node& n) { n.touch(); });
            return OpResult::ok();
    }
    return OpResult::ok();
}

} // namespace chitta
