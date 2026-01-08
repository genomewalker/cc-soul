#pragma once
// Hilbert curve mapping for cache-optimal disk layout
//
// Maps high-dimensional quantized vectors to 1D Hilbert keys.
// Points close in embedding space → close Hilbert keys → close on disk.
// This gives cache-friendly mmap page access during HNSW traversal.

#include "quantized.hpp"
#include <cstdint>
#include <array>
#include <algorithm>

namespace chitta {

// Hilbert curve configuration
// We use 8 dimensions × 8 bits = 64-bit key
// First 8 dimensions of quantized vector capture most variance
constexpr size_t HILBERT_DIMS = 8;
constexpr size_t HILBERT_BITS = 8;  // Bits per dimension
constexpr size_t HILBERT_ORDER = HILBERT_BITS;  // 2^8 = 256 cells per dimension

// ═══════════════════════════════════════════════════════════════════════════
// Hilbert curve utilities
// Based on: https://en.wikipedia.org/wiki/Hilbert_curve#Applications_and_mapping_algorithms
// ═══════════════════════════════════════════════════════════════════════════

// Rotate/flip a quadrant appropriately
inline void hilbert_rot(uint32_t n, uint32_t* x, uint32_t* y, uint32_t rx, uint32_t ry) {
    if (ry == 0) {
        if (rx == 1) {
            *x = n - 1 - *x;
            *y = n - 1 - *y;
        }
        std::swap(*x, *y);
    }
}

// Convert (x,y) to Hilbert distance d (2D case)
inline uint64_t xy_to_hilbert_2d(uint32_t n, uint32_t x, uint32_t y) {
    uint64_t d = 0;
    for (uint32_t s = n / 2; s > 0; s /= 2) {
        uint32_t rx = (x & s) > 0 ? 1 : 0;
        uint32_t ry = (y & s) > 0 ? 1 : 0;
        d += s * s * ((3 * rx) ^ ry);
        hilbert_rot(n, &x, &y, rx, ry);
    }
    return d;
}

// ═══════════════════════════════════════════════════════════════════════════
// N-dimensional Hilbert curve via dimension interleaving
// For N dimensions, we interleave bits from each dimension
// This gives good locality preservation while being fast to compute
// ═══════════════════════════════════════════════════════════════════════════

// Interleave bits from multiple dimensions into a single key
// coords[i] is the coordinate in dimension i (0-255 for 8 bits)
inline uint64_t interleave_bits(const std::array<uint8_t, HILBERT_DIMS>& coords) {
    uint64_t result = 0;

    // For each bit position (MSB to LSB)
    for (size_t bit = 0; bit < HILBERT_BITS; ++bit) {
        // For each dimension
        for (size_t dim = 0; dim < HILBERT_DIMS; ++dim) {
            // Extract bit from coordinate
            uint64_t b = (coords[dim] >> (HILBERT_BITS - 1 - bit)) & 1;
            // Place in interleaved position
            result |= b << (HILBERT_DIMS * (HILBERT_BITS - 1 - bit) + (HILBERT_DIMS - 1 - dim));
        }
    }

    return result;
}

// Gray code transformation for better locality
inline uint64_t to_gray_code(uint64_t n) {
    return n ^ (n >> 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// Main API: Compute Hilbert key from quantized vector
// ═══════════════════════════════════════════════════════════════════════════

// Extract first N dimensions as 8-bit coordinates
// The quantized vector is already int8 (-128 to 127), we shift to 0-255
inline std::array<uint8_t, HILBERT_DIMS> extract_coords(const QuantizedVector& vec) {
    std::array<uint8_t, HILBERT_DIMS> coords;

    for (size_t i = 0; i < HILBERT_DIMS; ++i) {
        // Shift from [-128, 127] to [0, 255]
        coords[i] = static_cast<uint8_t>(static_cast<int>(vec.data[i]) + 128);
    }

    return coords;
}

// Compute Hilbert key from quantized vector
// Returns 64-bit key where nearby vectors have nearby keys
inline uint64_t hilbert_key(const QuantizedVector& vec) {
    auto coords = extract_coords(vec);
    uint64_t interleaved = interleave_bits(coords);
    // Apply Gray code for better locality at boundaries
    return to_gray_code(interleaved);
}

// Compute Hilbert key from raw int8 data (for cases where we don't have full QuantizedVector)
inline uint64_t hilbert_key_raw(const int8_t* data, size_t len) {
    std::array<uint8_t, HILBERT_DIMS> coords;

    for (size_t i = 0; i < HILBERT_DIMS && i < len; ++i) {
        coords[i] = static_cast<uint8_t>(static_cast<int>(data[i]) + 128);
    }

    // Zero-pad if vector is shorter than HILBERT_DIMS
    for (size_t i = len; i < HILBERT_DIMS; ++i) {
        coords[i] = 128;  // Midpoint
    }

    return to_gray_code(interleave_bits(coords));
}

// ═══════════════════════════════════════════════════════════════════════════
// Hilbert key comparison for sorting
// ═══════════════════════════════════════════════════════════════════════════

struct HilbertComparator {
    bool operator()(uint64_t a, uint64_t b) const {
        return a < b;
    }
};

// Sort indices by Hilbert key
template<typename T>
void sort_by_hilbert(std::vector<T>& items,
                     std::function<uint64_t(const T&)> key_func) {
    std::sort(items.begin(), items.end(),
        [&key_func](const T& a, const T& b) {
            return key_func(a) < key_func(b);
        });
}

// ═══════════════════════════════════════════════════════════════════════════
// Distance estimation from Hilbert keys
// Nodes with similar Hilbert keys are likely to be close in embedding space
// ═══════════════════════════════════════════════════════════════════════════

// Estimate if two nodes are "Hilbert-close"
// Useful for prefetching nearby pages
inline bool hilbert_close(uint64_t key1, uint64_t key2, uint64_t threshold = 1024) {
    uint64_t diff = (key1 > key2) ? (key1 - key2) : (key2 - key1);
    return diff < threshold;
}

// Count leading zeros in XOR to estimate how far apart two keys are
// More leading zeros = closer in Hilbert space
inline uint32_t hilbert_distance_bits(uint64_t key1, uint64_t key2) {
    uint64_t xored = key1 ^ key2;
    if (xored == 0) return 64;  // Identical keys
    return __builtin_clzll(xored);
}

} // namespace chitta
