#pragma once
// Chitta: The soul graph engine
//
// A semantic memory system with:
// - Types: Nodes, Vectors, Confidence, Coherence
// - Graph: Semantic search, snapshots, decay
// - Ops: Graph transformations
// - Voices: Multi-perspective reasoning (Antahkarana)
// - Dynamics: Autonomous behavior
// - Storage: Tiered persistence (hot/warm/cold)
// - Mind: Unified API for soul storage

#include "types.hpp"
#include "quantized.hpp"
#include "graph.hpp"  // includes hnsw.hpp
#include "ops.hpp"
#include "voice.hpp"
#include "dynamics.hpp"
#include "storage.hpp"
#include "vak.hpp"
#ifdef CHITTA_WITH_ONNX
#include "vak_onnx.hpp"
#endif
#include "mind.hpp"
