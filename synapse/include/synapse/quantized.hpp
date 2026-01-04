#pragma once
// Quantized vectors for space-efficient storage
// int8 quantization: 74% space savings, ~1% accuracy loss

#include "types.hpp"
#include <cstring>
#include <limits>

namespace synapse {

// Quantized 384-dim vector: 392 bytes vs 1536 bytes (74% savings)
struct QuantizedVector {
    int8_t data[EMBED_DIM];  // 384 bytes
    float scale;              // 4 bytes
    float offset;             // 4 bytes

    QuantizedVector() : scale(1.0f), offset(0.0f) {
        std::memset(data, 0, EMBED_DIM);
    }

    // Quantize from float32
    static QuantizedVector from_float(const Vector& v) {
        QuantizedVector q;

        // Find min/max for scaling
        float min_val = std::numeric_limits<float>::max();
        float max_val = std::numeric_limits<float>::lowest();
        for (size_t i = 0; i < EMBED_DIM; ++i) {
            min_val = std::min(min_val, v.data[i]);
            max_val = std::max(max_val, v.data[i]);
        }

        // Compute scale and offset
        float range = max_val - min_val;
        if (range < 1e-8f) range = 1.0f;

        q.scale = range / 254.0f;  // Map to [-127, 127]
        q.offset = min_val + range / 2.0f;

        // Quantize
        for (size_t i = 0; i < EMBED_DIM; ++i) {
            float normalized = (v.data[i] - q.offset) / q.scale;
            int val = static_cast<int>(std::round(normalized));
            q.data[i] = static_cast<int8_t>(std::clamp(val, -127, 127));
        }

        return q;
    }

    // Dequantize to float32
    Vector to_float() const {
        Vector v;
        for (size_t i = 0; i < EMBED_DIM; ++i) {
            v.data[i] = static_cast<float>(data[i]) * scale + offset;
        }
        return v;
    }

    // Fast approximate cosine similarity (without full dequantization)
    float cosine_approx(const QuantizedVector& other) const {
        int32_t dot = 0;
        int32_t norm_a = 0;
        int32_t norm_b = 0;

        for (size_t i = 0; i < EMBED_DIM; ++i) {
            dot += static_cast<int32_t>(data[i]) * static_cast<int32_t>(other.data[i]);
            norm_a += static_cast<int32_t>(data[i]) * static_cast<int32_t>(data[i]);
            norm_b += static_cast<int32_t>(other.data[i]) * static_cast<int32_t>(other.data[i]);
        }

        float denom = std::sqrt(static_cast<float>(norm_a)) *
                      std::sqrt(static_cast<float>(norm_b));
        return denom > 0.0f ? static_cast<float>(dot) / denom : 0.0f;
    }

    // Exact cosine (dequantize first)
    float cosine_exact(const QuantizedVector& other) const {
        return to_float().cosine(other.to_float());
    }
};

static_assert(sizeof(QuantizedVector) == EMBED_DIM + 8, "QuantizedVector should be 392 bytes");

// Storage tier for nodes
enum class StorageTier : uint8_t {
    Hot = 0,   // RAM, float32, full index
    Warm = 1,  // mmap, int8, sparse index
    Cold = 2   // disk, no vectors, re-embed on access
};

// Node metadata for storage
struct NodeMeta {
    NodeId id;                    // 16 bytes
    Timestamp tau_created;        // 8 bytes
    Timestamp tau_accessed;       // 8 bytes
    float confidence_mu;          // 4 bytes
    float confidence_sigma;       // 4 bytes
    float decay_rate;             // 4 bytes
    uint32_t vector_offset;       // 4 bytes (offset in vector store)
    uint32_t payload_offset;      // 4 bytes (offset in payload store)
    uint32_t payload_size;        // 4 bytes
    uint32_t edge_offset;         // 4 bytes
    NodeType node_type;           // 1 byte
    StorageTier tier;             // 1 byte
    uint16_t flags;               // 2 bytes
};

} // namespace synapse
