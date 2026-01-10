#pragma once
// Dream operations: pure embedding space computation
//
// No text. No tokens. Just vectors.
// The soul processes while Claude sleeps.

#include "types.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace chitta {
namespace dream {

// ═══════════════════════════════════════════════════════════════════════════
// Embedding Arithmetic
// ═══════════════════════════════════════════════════════════════════════════

// Analogy: a is to b as c is to ?
// Example: socket_failure - nc + python = python_socket_error
inline Vector analogy(const Vector& a, const Vector& b, const Vector& c) {
    Vector result;
    result.data.resize(EMBED_DIM);
    for (size_t i = 0; i < EMBED_DIM; i++) {
        result.data[i] = b.data[i] - a.data[i] + c.data[i];
    }
    result.normalize();
    return result;
}

// Interpolate: what lies between two concepts?
// t=0.0: pure a, t=1.0: pure b, t=0.5: midpoint
inline Vector interpolate(const Vector& a, const Vector& b, float t) {
    Vector result;
    result.data.resize(EMBED_DIM);
    for (size_t i = 0; i < EMBED_DIM; i++) {
        result.data[i] = a.data[i] * (1.0f - t) + b.data[i] * t;
    }
    result.normalize();
    return result;
}

// Centroid: the conceptual "center" of multiple embeddings
inline Vector centroid(const std::vector<Vector>& vectors) {
    if (vectors.empty()) return Vector::zeros();

    Vector result;
    result.data.resize(EMBED_DIM, 0.0f);

    for (const auto& v : vectors) {
        for (size_t i = 0; i < EMBED_DIM; i++) {
            result.data[i] += v.data[i];
        }
    }

    result.normalize();
    return result;
}

// Combine with weights: weighted average of concepts
inline Vector combine(const std::vector<Vector>& vectors,
                      const std::vector<float>& weights) {
    if (vectors.empty()) return Vector::zeros();

    Vector result;
    result.data.resize(EMBED_DIM, 0.0f);

    float total_weight = 0.0f;
    for (size_t j = 0; j < vectors.size(); j++) {
        float w = (j < weights.size()) ? weights[j] : 1.0f;
        total_weight += w;
        for (size_t i = 0; i < EMBED_DIM; i++) {
            result.data[i] += vectors[j].data[i] * w;
        }
    }

    if (total_weight > 0.0f) {
        for (size_t i = 0; i < EMBED_DIM; i++) {
            result.data[i] /= total_weight;
        }
    }

    result.normalize();
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Similarity Operations
// ═══════════════════════════════════════════════════════════════════════════

// Batch cosine similarities
inline std::vector<float> similarities(const Vector& query,
                                        const std::vector<Vector>& targets) {
    std::vector<float> sims;
    sims.reserve(targets.size());
    for (const auto& t : targets) {
        sims.push_back(query.cosine(t));
    }
    return sims;
}

// Find k most similar
inline std::vector<size_t> top_k(const std::vector<float>& similarities, size_t k) {
    std::vector<size_t> indices(similarities.size());
    for (size_t i = 0; i < indices.size(); i++) indices[i] = i;

    std::partial_sort(indices.begin(),
                      indices.begin() + std::min(k, indices.size()),
                      indices.end(),
                      [&](size_t a, size_t b) {
                          return similarities[a] > similarities[b];
                      });

    indices.resize(std::min(k, indices.size()));
    return indices;
}

// ═══════════════════════════════════════════════════════════════════════════
// Clustering (simple k-means in embedding space)
// ═══════════════════════════════════════════════════════════════════════════

struct Cluster {
    Vector centroid;
    std::vector<size_t> members;  // indices into original vector set
};

inline std::vector<Cluster> cluster_kmeans(const std::vector<Vector>& vectors,
                                            size_t k, size_t max_iter = 20) {
    if (vectors.empty() || k == 0) return {};
    k = std::min(k, vectors.size());

    std::vector<Cluster> clusters(k);

    // Initialize centroids with random vectors
    std::random_device rd;
    std::mt19937 gen(rd());
    std::vector<size_t> indices(vectors.size());
    for (size_t i = 0; i < indices.size(); i++) indices[i] = i;
    std::shuffle(indices.begin(), indices.end(), gen);

    for (size_t i = 0; i < k; i++) {
        clusters[i].centroid = vectors[indices[i]];
    }

    // Iterate
    for (size_t iter = 0; iter < max_iter; iter++) {
        // Clear assignments
        for (auto& c : clusters) c.members.clear();

        // Assign each vector to nearest centroid
        for (size_t i = 0; i < vectors.size(); i++) {
            float best_sim = -1.0f;
            size_t best_cluster = 0;
            for (size_t j = 0; j < k; j++) {
                float sim = vectors[i].cosine(clusters[j].centroid);
                if (sim > best_sim) {
                    best_sim = sim;
                    best_cluster = j;
                }
            }
            clusters[best_cluster].members.push_back(i);
        }

        // Update centroids
        for (auto& c : clusters) {
            if (c.members.empty()) continue;
            std::vector<Vector> member_vecs;
            for (size_t idx : c.members) {
                member_vecs.push_back(vectors[idx]);
            }
            c.centroid = centroid(member_vecs);
        }
    }

    return clusters;
}

// ═══════════════════════════════════════════════════════════════════════════
// Spreading Activation
// ═══════════════════════════════════════════════════════════════════════════

struct ActivationResult {
    size_t index;
    float activation;
};

// Spread activation from seed through similarity network
inline std::vector<ActivationResult> spread_activation(
    size_t seed_index,
    const std::vector<Vector>& vectors,
    float initial_activation = 1.0f,
    float decay = 0.5f,
    float threshold = 0.1f,
    size_t max_spread = 3
) {
    std::vector<float> activations(vectors.size(), 0.0f);
    activations[seed_index] = initial_activation;

    std::vector<size_t> frontier = {seed_index};

    for (size_t depth = 0; depth < max_spread && !frontier.empty(); depth++) {
        std::vector<size_t> next_frontier;

        for (size_t idx : frontier) {
            float current = activations[idx];
            if (current < threshold) continue;

            // Spread to similar vectors
            for (size_t i = 0; i < vectors.size(); i++) {
                if (i == idx) continue;
                float sim = vectors[idx].cosine(vectors[i]);
                if (sim > 0.5f) {  // Only spread to similar concepts
                    float spread = current * sim * decay;
                    if (spread > activations[i]) {
                        activations[i] = spread;
                        next_frontier.push_back(i);
                    }
                }
            }
        }

        frontier = std::move(next_frontier);
    }

    // Collect results above threshold
    std::vector<ActivationResult> results;
    for (size_t i = 0; i < activations.size(); i++) {
        if (activations[i] > threshold) {
            results.push_back({i, activations[i]});
        }
    }

    // Sort by activation
    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) {
                  return a.activation > b.activation;
              });

    return results;
}

// ═══════════════════════════════════════════════════════════════════════════
// Dream Synthesis
// ═══════════════════════════════════════════════════════════════════════════

// Find "gaps" - regions of embedding space with no concepts
// These are places where new understanding could emerge
inline std::vector<Vector> find_gaps(const std::vector<Vector>& vectors,
                                      size_t num_samples = 100,
                                      float gap_threshold = 0.3f) {
    if (vectors.size() < 2) return {};

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, vectors.size() - 1);

    std::vector<Vector> gaps;

    for (size_t s = 0; s < num_samples; s++) {
        // Generate random interpolation between two random concepts
        size_t i = dist(gen);
        size_t j = dist(gen);
        if (i == j) continue;

        float t = 0.3f + 0.4f * (float(gen() % 100) / 100.0f);  // 0.3-0.7
        Vector candidate = interpolate(vectors[i], vectors[j], t);

        // Check if any existing concept is close
        float max_sim = 0.0f;
        for (const auto& v : vectors) {
            float sim = candidate.cosine(v);
            if (sim > max_sim) max_sim = sim;
        }

        // If nothing is close, this is a gap
        if (max_sim < gap_threshold) {
            gaps.push_back(candidate);
        }
    }

    return gaps;
}

// Synthesize new concept from related concepts
// This is "dreaming" - creating new understanding from existing
inline Vector dream_synthesis(const std::vector<Vector>& related,
                               float noise_scale = 0.1f) {
    if (related.empty()) return Vector::zeros();

    // Start with centroid
    Vector dream = centroid(related);

    // Add small noise for creativity
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<float> noise(0.0f, noise_scale);

    for (size_t i = 0; i < EMBED_DIM; i++) {
        dream.data[i] += noise(gen);
    }

    dream.normalize();
    return dream;
}

} // namespace dream
} // namespace chitta
