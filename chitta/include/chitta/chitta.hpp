#pragma once
// Chitta: The soul memory engine
//
// SimpleMind: Clean interface to soul storage
// - remember(), recall(), connect() - core memory operations
// - Dense search with embeddings
// - Graph triplets for relationships

#include "types.hpp"
#include "quantized.hpp"
#include "unified_index.hpp"
#include "mmap_graph_store.hpp"
#include "vak.hpp"
#ifdef CHITTA_WITH_ONNX
#include "vak_onnx.hpp"
#endif
#include "mind/simple_mind.hpp"

// Old components - include explicitly if needed:
// #include "mind.hpp"    // Old Mind class (for migration tools)
// #include "storage.hpp" // Tiered storage
// #include "graph.hpp"   // HNSW graph
