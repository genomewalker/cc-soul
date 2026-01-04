#pragma once
// Voices: projections of the same truth
//
// The Antahkarana - inner instruments.
// Each voice sees the same graph differently.

#include "types.hpp"
#include "graph.hpp"
#include <unordered_map>

namespace chitta {

// Hash for NodeType and EdgeType
struct NodeTypeHash {
    size_t operator()(NodeType t) const { return static_cast<size_t>(t); }
};
struct EdgeTypeHash {
    size_t operator()(EdgeType t) const { return static_cast<size_t>(t); }
};

// Voice identifier (same structure as NodeId)
using VoiceId = NodeId;

// A voice (projection) of the soul graph
struct Voice {
    VoiceId id;
    std::string name;
    std::string description;
    std::unordered_map<NodeType, float, NodeTypeHash> attention;
    float confidence_bias = 0.0f;
    std::unordered_map<EdgeType, float, EdgeTypeHash> edge_preferences;
    bool active = true;

    Voice(std::string n, std::string desc)
        : id(NodeId::generate())
        , name(std::move(n))
        , description(std::move(desc)) {}

    Voice& attend(NodeType type, float weight) {
        attention[type] = weight;
        return *this;
    }

    Voice& with_bias(float bias) {
        confidence_bias = bias;
        return *this;
    }

    Voice& prefer_edge(EdgeType type, float weight) {
        edge_preferences[type] = weight;
        return *this;
    }

    // Query graph through this voice's lens
    std::vector<std::pair<NodeId, float>> query(
        const Graph& graph, const Vector& vector,
        float threshold, size_t limit) const
    {
        auto raw_results = graph.query(vector, threshold, limit * 2);
        std::vector<std::pair<NodeId, float>> weighted;

        for (const auto& [id, sim] : raw_results) {
            auto node_opt = graph.get(id);
            if (!node_opt) continue;
            const auto& node = *node_opt;

            float attn = 1.0f;
            auto it = attention.find(node.node_type);
            if (it != attention.end()) attn = it->second;

            float adjusted_sim = sim * attn;
            float confidence = node.kappa.effective();
            float biased_conf = std::clamp(confidence + confidence_bias, 0.0f, 1.0f);
            float score = adjusted_sim * 0.7f + biased_conf * 0.3f;

            weighted.emplace_back(id, score);
        }

        std::sort(weighted.begin(), weighted.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        if (weighted.size() > limit) weighted.resize(limit);
        return weighted;
    }

    // Evaluate coherence through this voice's perspective
    float evaluate_coherence(const Graph& graph) const {
        float base = graph.coherence().tau_k();
        return std::clamp(base + confidence_bias * 0.2f, 0.0f, 1.0f);
    }
};

// The six classical Antahkarana voices
namespace antahkarana {

// Manas: Quick intuition, first impressions
inline Voice manas() {
    return Voice("Manas", "Sensory mind - quick intuitive responses")
        .attend(NodeType::Wisdom, 0.8f)
        .attend(NodeType::Episode, 1.2f)
        .attend(NodeType::Intention, 1.0f)
        .with_bias(0.1f)
        .prefer_edge(EdgeType::Similar, 1.5f);
}

// Buddhi: Deep analysis, thorough reasoning
inline Voice buddhi() {
    return Voice("Buddhi", "Intellect - deep analytical reasoning")
        .attend(NodeType::Wisdom, 1.5f)
        .attend(NodeType::Belief, 1.3f)
        .attend(NodeType::Episode, 0.7f)
        .with_bias(0.0f)
        .prefer_edge(EdgeType::Supports, 1.3f)
        .prefer_edge(EdgeType::Contradicts, 1.2f);
}

// Ahamkara: Self-protective criticism, finding flaws
inline Voice ahamkara() {
    return Voice("Ahamkara", "Self-protective critic - finding flaws")
        .attend(NodeType::Failure, 1.5f)
        .attend(NodeType::Invariant, 1.3f)
        .attend(NodeType::Dream, 0.5f)
        .with_bias(-0.2f)
        .prefer_edge(EdgeType::Contradicts, 1.5f);
}

// Chitta: Memory patterns, practical wisdom
inline Voice chitta() {
    return Voice("Chitta", "Memory - practical wisdom from experience")
        .attend(NodeType::Episode, 1.5f)
        .attend(NodeType::Wisdom, 1.2f)
        .attend(NodeType::Term, 1.3f)
        .with_bias(0.0f)
        .prefer_edge(EdgeType::AppliedIn, 1.5f)
        .prefer_edge(EdgeType::EvolvedFrom, 1.3f);
}

// Vikalpa: Imagination, unconventional approaches
inline Voice vikalpa() {
    return Voice("Vikalpa", "Imagination - creative unconventional thinking")
        .attend(NodeType::Dream, 1.5f)
        .attend(NodeType::Aspiration, 1.3f)
        .attend(NodeType::Belief, 0.7f)
        .with_bias(0.15f)
        .prefer_edge(EdgeType::Similar, 0.7f);
}

// Sakshi: Witness, essential truth
inline Voice sakshi() {
    return Voice("Sakshi", "Witness - detached observation of essential truth")
        .attend(NodeType::Invariant, 1.5f)
        .attend(NodeType::Belief, 1.2f)
        .attend(NodeType::Wisdom, 1.0f)
        .attend(NodeType::Episode, 0.5f)
        .with_bias(0.0f)
        .prefer_edge(EdgeType::Supports, 1.0f);
}

// All six voices
inline std::vector<Voice> all() {
    return {manas(), buddhi(), ahamkara(), chitta(), vikalpa(), sakshi()};
}

} // namespace antahkarana

// Report from voice harmonization
struct HarmonyReport {
    float mean_coherence = 0.0f;
    float variance = 0.0f;
    bool voices_agree = false;
    std::vector<std::pair<std::string, float>> perspectives;
};

// Orchestrator for multi-voice reasoning
class Chorus {
public:
    Chorus() = default;

    explicit Chorus(std::vector<Voice> voices) : voices_(std::move(voices)) {}

    void add(Voice voice) {
        voices_.push_back(std::move(voice));
    }

    // Query through all voices and harmonize
    std::vector<std::tuple<NodeId, float, std::vector<std::string>>> query(
        const Graph& graph, const Vector& vector,
        float threshold, size_t limit) const
    {
        std::unordered_map<NodeId, std::pair<float, std::vector<std::string>>, NodeIdHash> all_results;

        for (const auto& voice : voices_) {
            if (!voice.active) continue;
            auto results = voice.query(graph, vector, threshold, limit);
            for (const auto& [id, score] : results) {
                auto& entry = all_results[id];
                entry.first += score;
                entry.second.push_back(voice.name);
            }
        }

        size_t n_voices = std::count_if(voices_.begin(), voices_.end(),
                                         [](const Voice& v) { return v.active; });
        float n = static_cast<float>(n_voices);

        std::vector<std::tuple<NodeId, float, std::vector<std::string>>> results;
        for (auto& [id, data] : all_results) {
            results.emplace_back(id, data.first / n, std::move(data.second));
        }

        std::sort(results.begin(), results.end(),
                  [](const auto& a, const auto& b) {
                      return std::get<1>(a) > std::get<1>(b);
                  });

        if (results.size() > limit) results.resize(limit);
        return results;
    }

    // Harmonize: find where voices agree
    HarmonyReport harmonize(const Graph& graph) const {
        std::vector<const Voice*> active;
        for (const auto& v : voices_) {
            if (v.active) active.push_back(&v);
        }

        if (active.empty()) return {};

        std::vector<float> coherences;
        for (const auto* v : active) {
            coherences.push_back(v->evaluate_coherence(graph));
        }

        float sum = 0.0f;
        for (float c : coherences) sum += c;
        float mean = sum / static_cast<float>(active.size());

        float var_sum = 0.0f;
        for (float c : coherences) {
            float diff = c - mean;
            var_sum += diff * diff;
        }
        float variance = var_sum / static_cast<float>(active.size());

        HarmonyReport report;
        report.mean_coherence = mean;
        report.variance = variance;
        report.voices_agree = variance < 0.05f;

        for (size_t i = 0; i < active.size(); ++i) {
            report.perspectives.emplace_back(active[i]->name, coherences[i]);
        }

        return report;
    }

private:
    std::vector<Voice> voices_;
};

} // namespace chitta
