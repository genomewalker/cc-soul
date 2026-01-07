#pragma once
// Core types: the atoms of soul
//
// Everything is a Node. Confidence is a distribution.
// Time is intrinsic. Nothing is certain.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <random>

namespace chitta {

// Embedding dimension (all-MiniLM-L6-v2 compatible)
constexpr size_t EMBED_DIM = 384;

// Timestamp as Unix millis
using Timestamp = int64_t;

// Current time as Timestamp
inline Timestamp now() {
    auto duration = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

// UUID - simple 128-bit identifier
struct NodeId {
    uint64_t high = 0;
    uint64_t low = 0;

    static NodeId generate() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;
        return {dis(gen), dis(gen)};
    }

    bool operator==(const NodeId& other) const {
        return high == other.high && low == other.low;
    }

    bool operator!=(const NodeId& other) const {
        return !(*this == other);
    }

    bool operator<(const NodeId& other) const {
        return high < other.high || (high == other.high && low < other.low);
    }

    std::string to_string() const {
        char buf[37];
        snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx",
                 (uint32_t)(high >> 32),
                 (uint16_t)(high >> 16),
                 (uint16_t)high,
                 (uint16_t)(low >> 48),
                 (unsigned long long)(low & 0xFFFFFFFFFFFFULL));
        return buf;
    }

    static NodeId from_string(const std::string& s) {
        NodeId id;
        if (s.length() < 36) return id;

        // Parse UUID: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
        uint32_t a;
        uint16_t b, c, d;
        uint64_t e;

        if (sscanf(s.c_str(), "%8x-%4hx-%4hx-%4hx-%12llx",
                   &a, &b, &c, &d, (unsigned long long*)&e) == 5) {
            id.high = ((uint64_t)a << 32) | ((uint64_t)b << 16) | c;
            id.low = ((uint64_t)d << 48) | e;
        }
        return id;
    }
};

// Semantic vector - the meaning of a node
class Vector {
public:
    std::vector<float> data;

    Vector() : data(EMBED_DIM, 0.0f) {}

    explicit Vector(const std::array<float, EMBED_DIM>& arr)
        : data(arr.begin(), arr.end()) {}

    explicit Vector(std::vector<float> v) : data(std::move(v)) {
        if (data.size() != EMBED_DIM) {
            data.resize(EMBED_DIM, 0.0f);
        }
    }

    static Vector zeros() {
        return Vector();
    }

    const float* as_ptr() const { return data.data(); }
    float* as_ptr() { return data.data(); }
    size_t size() const { return data.size(); }

    float& operator[](size_t i) { return data[i]; }
    const float& operator[](size_t i) const { return data[i]; }

    // Cosine similarity
    float cosine(const Vector& other) const {
        float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
        for (size_t i = 0; i < EMBED_DIM; ++i) {
            dot += data[i] * other.data[i];
            norm_a += data[i] * data[i];
            norm_b += other.data[i] * other.data[i];
        }
        float denom = std::sqrt(norm_a) * std::sqrt(norm_b);
        return denom > 0.0f ? dot / denom : 0.0f;
    }

    // Normalize to unit vector
    void normalize() {
        float norm = 0.0f;
        for (float x : data) norm += x * x;
        norm = std::sqrt(norm);
        if (norm > 0.0f) {
            for (float& x : data) x /= norm;
        }
    }
};

// Confidence: not a float, a distribution
// Distinguishes "90% sure, very confident" from
// "90% sure, but uncertain about that estimate"
struct Confidence {
    float mu = 0.5f;        // Mean probability estimate
    float sigma_sq = 0.1f;  // Variance (uncertainty about the estimate)
    uint32_t n = 1;         // Number of observations
    Timestamp tau = 0;      // Last updated

    Confidence() : tau(now()) {}

    explicit Confidence(float mean)
        : mu(std::clamp(mean, 0.0f, 1.0f)), sigma_sq(0.1f), n(1), tau(now()) {}

    static Confidence certain(float mean) {
        Confidence c;
        c.mu = std::clamp(mean, 0.0f, 1.0f);
        c.sigma_sq = 0.001f;
        c.n = 100;
        c.tau = now();
        return c;
    }

    // Update with new observation using Bayesian update
    void observe(float observed) {
        n += 1;
        float alpha = 1.0f / static_cast<float>(n);
        float delta = observed - mu;
        mu += alpha * delta;
        sigma_sq = (1.0f - alpha) * (sigma_sq + alpha * delta * delta);
        tau = now();
    }

    // Apply decay: increase uncertainty, decrease mean toward 0.5
    void decay(float rate, float days_elapsed) {
        float decay_factor = std::exp(-rate * days_elapsed);
        mu = 0.5f + (mu - 0.5f) * decay_factor;
        sigma_sq = std::min(sigma_sq + 0.01f * (1.0f - decay_factor), 0.25f);
        tau = now();
    }

    // Effective confidence (mean adjusted by uncertainty)
    float effective() const {
        float uncertainty_penalty = std::sqrt(sigma_sq) * 2.0f;
        return mu * std::max(1.0f - uncertainty_penalty, 0.0f);
    }
};

// Node types in the soul graph
enum class NodeType : uint8_t {
    Wisdom = 0,      // Universal pattern/insight
    Belief = 1,      // Guiding principle (immutable confidence)
    Intention = 2,   // Concrete want with scope
    Aspiration = 3,  // Direction of growth
    Episode = 4,     // Episodic memory
    Operation = 5,   // Self-referential operation
    Invariant = 6,   // Protected constraint
    Identity = 7,    // Identity aspect
    Term = 8,        // Vocabulary term
    Failure = 9,     // Failure record (gold for learning)
    Dream = 10,      // Dream (wilder than aspiration)
    Voice = 11,      // Voice configuration
    Meta = 12,       // Meta-node (references graph structure)
    Gap = 13,        // Detected knowledge gap (curiosity)
    Question = 14,   // Active wondering (curiosity → question)
    StoryThread = 15, // Connected episode arc (narrative)
    Ledger = 16,     // Session ledger (Atman snapshot)
    Entity = 17,     // Named entity (person, concept, codebase, etc.)
};

// Edge types connecting nodes
enum class EdgeType : uint8_t {
    Similar = 0,     // Semantic similarity
    AppliedIn = 1,   // Applied in context
    Contradicts = 2, // Contradicts
    Supports = 3,    // Supports/confirms
    EvolvedFrom = 4, // Evolved from
    PartOf = 5,      // Part of (episode, story)
    TriggeredBy = 6, // Triggered by
    CreatedBy = 7,   // Created by (voice, op)
    ScopedTo = 8,    // Scope binding
    Answers = 9,     // Answers a question
    Addresses = 10,  // Addresses a gap
    Continues = 11,  // Continues a story thread
    Mentions = 12,   // Episode/wisdom mentions entity
    IsA = 13,        // Entity type hierarchy (pizza IsA food)
    RelatesTo = 14,  // Generic entity relationship
};

// Intention scope
enum class Scope : uint8_t {
    Session = 0,
    Project = 1,
    Persistent = 2,
};

// Entity classification for structured knowledge
enum class EntityType : uint8_t {
    Person = 0,      // Human or AI identity
    Concept = 1,     // Abstract idea (authentication, rate limiting)
    Codebase = 2,    // Project or repository
    Tool = 3,        // Function, command, or utility
    Decision = 4,    // Named decision that can be referenced
    Location = 5,    // File, path, or place
    Unknown = 255,   // Unclassified
};

// Triplet: structured fact (subject, predicate, object)
// Enables deterministic O(1) lookup instead of O(N) similarity scan
struct Triplet {
    NodeId subject;          // Entity node
    std::string predicate;   // Relationship verb (likes, uses, depends_on)
    NodeId object;           // Target entity or value node
    float weight;            // Confidence/strength of relationship
    NodeId source;           // Episode/wisdom that stated this fact
    Timestamp created;       // When this triplet was recorded

    Triplet()
        : weight(1.0f)
        , created(now()) {}

    Triplet(NodeId subj, const std::string& pred, NodeId obj, float w = 1.0f)
        : subject(subj)
        , predicate(pred)
        , object(obj)
        , weight(w)
        , created(now()) {}

    Triplet& with_source(NodeId src) {
        source = src;
        return *this;
    }
};

// Entity: named thing that can be referenced across observations
// Solves the "Antonio problem" - multiple facts anchored to one entity
struct Entity {
    NodeId id;
    std::string canonical_name;       // Normalized: lowercase, trimmed
    std::vector<std::string> aliases; // Alternative names ["Antonio", "AFG"]
    EntityType entity_type;
    Timestamp created;
    Timestamp last_mentioned;
    size_t mention_count;

    Entity()
        : entity_type(EntityType::Unknown)
        , created(now())
        , last_mentioned(now())
        , mention_count(0) {}

    Entity(const std::string& name, EntityType type = EntityType::Unknown)
        : id(NodeId::generate())
        , entity_type(type)
        , created(now())
        , last_mentioned(now())
        , mention_count(0) {
        set_canonical_name(name);
    }

    // Normalize name: lowercase, trim whitespace
    void set_canonical_name(const std::string& name) {
        canonical_name.clear();
        for (char c : name) {
            if (c == ' ' && (canonical_name.empty() || canonical_name.back() == ' '))
                continue;
            canonical_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        // Trim trailing space
        while (!canonical_name.empty() && canonical_name.back() == ' ')
            canonical_name.pop_back();
        // Store original as first alias if different
        if (name != canonical_name) {
            aliases.push_back(name);
        }
    }

    void add_alias(const std::string& alias) {
        // Check if already exists
        for (const auto& a : aliases) {
            if (a == alias) return;
        }
        aliases.push_back(alias);
    }

    void touch() {
        last_mentioned = now();
        mention_count++;
    }

    // Check if name matches canonical or any alias (case-insensitive)
    bool matches(const std::string& name) const {
        std::string lower_name;
        for (char c : name) {
            lower_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (canonical_name == lower_name) return true;
        for (const auto& alias : aliases) {
            std::string lower_alias;
            for (char c : alias) {
                lower_alias += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (lower_alias == lower_name) return true;
        }
        return false;
    }
};

// Edge: connection to another node
struct Edge {
    NodeId target;
    EdgeType type;
    float weight;
};

// A node in the soul graph
struct Node {
    NodeId id;
    Vector nu;                // Semantic embedding
    Confidence kappa;         // Confidence distribution
    Timestamp tau_created;    // Creation timestamp
    Timestamp tau_accessed;   // Last access timestamp
    float delta;              // Decay rate (per day, 0 = never decays)
    NodeType node_type;
    std::vector<uint8_t> payload;
    std::vector<Edge> edges;
    std::vector<std::string> tags;  // Exact-match tags for filtering

    Node()
        : id()
        , nu()
        , kappa(0.5f)
        , tau_created(now())
        , tau_accessed(now())
        , delta(0.05f)
        , node_type(NodeType::Episode) {}

    Node(NodeType type, Vector embedding)
        : id(NodeId::generate())
        , nu(std::move(embedding))
        , kappa(0.8f)
        , tau_created(now())
        , tau_accessed(now())
        , delta(0.05f)
        , node_type(type) {}

    Node& with_confidence(Confidence c) {
        kappa = c;
        return *this;
    }

    Node& with_decay(float d) {
        delta = d;
        return *this;
    }

    Node& immutable() {
        delta = 0.0f;
        kappa = Confidence::certain(1.0f);
        return *this;
    }

    Node& with_payload(std::vector<uint8_t> p) {
        payload = std::move(p);
        return *this;
    }

    Node& with_tags(std::vector<std::string> t) {
        tags = std::move(t);
        return *this;
    }

    void connect(NodeId target, EdgeType type, float weight) {
        edges.push_back({target, type, weight});
    }

    void touch() {
        tau_accessed = now();
    }

    void apply_decay(Timestamp current_time) {
        if (delta == 0.0f) return;
        float days = static_cast<float>(current_time - tau_accessed) / 86400000.0f;
        if (days > 0.0f) {
            kappa.decay(delta, days);
        }
    }

    bool is_alive(float threshold) const {
        return kappa.effective() > threshold;
    }
};

// Coherence measurement across dimensions
struct Coherence {
    float local = 1.0f;      // Local: nodes don't contradict (explicit + semantic)
    float global = 1.0f;     // Global: weighted confidence by node importance
    float temporal = 0.5f;   // Temporal: activity + maturity balance
    float structural = 1.0f; // Structural: connectivity health
    Timestamp tau;           // Computed timestamp

    Coherence() : tau(now()) {}

    // τₖ: the coherence coefficient (geometric mean for stricter coherence)
    float tau_k() const {
        // Geometric mean: all dimensions must be healthy
        // A single failing dimension tanks the score
        float product = local * global * temporal * structural;
        return std::pow(product, 0.25f);
    }

    // Alternative: weighted average (more forgiving)
    float tau_k_weighted() const {
        return 0.30f * local + 0.30f * global + 0.20f * temporal + 0.20f * structural;
    }

    bool needs_attention() const {
        return tau_k() < 0.5f;
    }

    // Individual dimension health checks
    bool local_healthy() const { return local > 0.7f; }
    bool global_healthy() const { return global > 0.5f; }
    bool temporal_healthy() const { return temporal > 0.4f; }
    bool structural_healthy() const { return structural > 0.3f; }
};

} // namespace chitta
