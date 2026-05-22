#pragma once
// Core types: the atoms of soul

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cinttypes>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>
#include <random>

// POSIX headers for atomic file persistence (must be outside namespace)
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

namespace chitta {

// Embedding dimension (nomic-embed-text v1.5 full 768-d)
constexpr size_t EMBED_DIM = 768;

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
        thread_local static std::random_device rd;
        thread_local static std::mt19937_64 gen(rd());
        thread_local static std::uniform_int_distribution<uint64_t> dis;
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
        snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012" PRIx64,
                 static_cast<uint32_t>(high >> 32),
                 static_cast<uint16_t>(high >> 16),
                 static_cast<uint16_t>(high),
                 static_cast<uint16_t>(low >> 48),
                 static_cast<uint64_t>(low & 0xFFFFFFFFFFFFULL));
        return buf;
    }

    static NodeId from_string(const std::string& s) {
        NodeId id;
        if (s.length() < 36) return id;

        // Parse UUID: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
        uint32_t a;
        uint16_t b, c, d;
        uint64_t e;

        if (sscanf(s.c_str(), "%8x-%4hx-%4hx-%4hx-%12" SCNx64,
                   &a, &b, &c, &d, &e) == 5) {
            id.high = (static_cast<uint64_t>(a) << 32) | (static_cast<uint64_t>(b) << 16) | c;
            id.low = (static_cast<uint64_t>(d) << 48) | e;
        }
        return id;
    }

    bool valid() const { return high != 0 || low != 0; }
};

// Hash function for NodeId (for use in unordered containers)
struct NodeIdHash {
    size_t operator()(const NodeId& id) const {
        return std::hash<uint64_t>{}(id.high) ^ (std::hash<uint64_t>{}(id.low) << 1);
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

    // Cosine similarity (optimized: single pass)
    float cosine(const Vector& other) const {
        float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
        for (size_t i = 0; i < EMBED_DIM; ++i) {
            float ai = data[i];
            float bi = other.data[i];
            dot += ai * bi;
            norm_a += ai * ai;
            norm_b += bi * bi;
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

    // Check if vector is effectively zero (no embedding)
    bool is_zero() const {
        float sum_sq = 0.0f;
        for (float x : data) sum_sq += x * x;
        return sum_sq < 1e-10f;
    }

    // Squared L2 norm
    float norm_sq() const {
        float sum = 0.0f;
        for (float x : data) sum += x * x;
        return sum;
    }
};

// Free function: cosine similarity for std::vector<float>
// Consolidates duplicate implementations across codebase
inline float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }
    float denom = std::sqrt(norm_a) * std::sqrt(norm_b);
    return denom > 0.0f ? dot / denom : 0.0f;
}

// Sparse Distributed Representation (SDR)
// Stores indices of the top-k active dimensions from a dense embedding.
// Similarity via intersection-over-union (IoU) is more orthogonal than cosine.
struct SparseVector {
    std::vector<uint16_t> active; // sorted active dimension indices

    static SparseVector from_dense(const std::vector<float>& dense, float k_pct = 0.05f) {
        if (dense.empty()) return SparseVector{};
        size_t k = std::clamp(size_t(dense.size() * k_pct), size_t(1), dense.size());
        std::vector<std::pair<float, uint16_t>> indexed;
        indexed.reserve(dense.size());
        for (size_t i = 0; i < dense.size(); i++)
            indexed.push_back({std::abs(dense[i]), static_cast<uint16_t>(i)});
        std::partial_sort(indexed.begin(), indexed.begin() + k, indexed.end(),
                          [](const auto& a, const auto& b) { return a.first > b.first; });
        SparseVector sv;
        sv.active.reserve(k);
        for (size_t i = 0; i < k; i++) sv.active.push_back(indexed[i].second);
        std::sort(sv.active.begin(), sv.active.end());
        return sv;
    }

    float iou(const SparseVector& other) const {
        if (active.empty() && other.active.empty()) return 1.0f;
        size_t inter = 0, i = 0, j = 0;
        while (i < active.size() && j < other.active.size()) {
            if (active[i] == other.active[j]) { inter++; i++; j++; }
            else if (active[i] < other.active[j]) i++;
            else j++;
        }
        size_t union_sz = active.size() + other.active.size() - inter;
        return union_sz ? static_cast<float>(inter) / union_sz : 0.0f;
    }

    std::string serialize() const {
        std::string s;
        for (size_t i = 0; i < active.size(); i++) {
            if (i) s += ',';
            s += std::to_string(active[i]);
        }
        return s;
    }

    static SparseVector deserialize(const std::string& s) {
        SparseVector sv;
        if (s.empty()) return sv;
        std::istringstream ss(s);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (tok.empty()) continue;
            try {
                sv.active.push_back(static_cast<uint16_t>(std::stoi(tok)));
            } catch (const std::exception&) {
                // skip malformed token
            }
        }
        std::sort(sv.active.begin(), sv.active.end());
        sv.active.erase(std::unique(sv.active.begin(), sv.active.end()), sv.active.end());
        return sv;
    }

    bool empty() const { return active.empty(); }
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
    Triplet = 18,    // Relationship: subject --[predicate]--> object
    // Hierarchical state compression (token-efficient context injection)
    ProjectEssence = 19,  // Level 0: project thesis, core modules (~50 tokens)
    ModuleState = 20,     // Level 1: module summary, entrypoints (~20 tokens each)
    PatternState = 21,    // Level 2: active patterns, seeds (~10 tokens each)
    Symbol = 22,          // Level 3: code symbol (function, class, etc.)
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

    bool local_healthy() const { return local > 0.7f; }
    bool global_healthy() const { return global > 0.5f; }
    bool temporal_healthy() const { return temporal > 0.4f; }
    bool structural_healthy() const { return structural > 0.3f; }
};

// Fsync parent directory for durability
inline bool fsync_dir(const std::string& path) {
    auto slash = path.find_last_of('/');
    std::string dir = (slash == std::string::npos) ? "." : path.substr(0, slash);
    int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (dfd < 0) return false;
    int rc = ::fsync(dfd);
    ::close(dfd);
    return rc == 0;
}

// Atomic save: write to temp file, fsync, rename to final path
// Writer function takes FILE* and returns true on success
template <typename Writer>
bool safe_save(const std::string& path, Writer&& write_fn) {
    std::string tmp = path + ".tmp." + std::to_string(::getpid());
    FILE* f = ::fopen(tmp.c_str(), "wb");
    if (!f) return false;

    bool ok = write_fn(f);
    if (ok && ::fflush(f) == 0 && ::fsync(::fileno(f)) == 0) {
        ok = true;
    } else {
        ok = false;
    }

    ::fclose(f);
    if (!ok) { ::remove(tmp.c_str()); return false; }

    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        ::remove(tmp.c_str());
        return false;
    }

    fsync_dir(path);
    return true;
}

} // namespace chitta
