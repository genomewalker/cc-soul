#!/usr/bin/env python3
"""Generate SSL distillation DPO training corpus via Anthropic API."""

import json
import random
import re
import sys
import time
from pathlib import Path

import anthropic

OUTPUT = Path("/maps/projects/fernandezguerra/apps/repos/cc-soul/scripts/ssl_model/training_data_web.jsonl")

REJECTION_POOL = [
    "method resolve @truth_maintenance.hpp:86 — Marks the stored contradiction between two beliefs",
    "class TokenizedOutput @vak_onnx.hpp:152 — Represents a model-ready tokenization result",
    "habit: bash:build.sh → bash:ls — user tends to list after building",
    "function count_bases_avx512 @simd_utils.hpp:116 — Counts A/C/G/T using AVX-512 intrinsics",
    "class Condition @ops.hpp:65 — A composable boolean predicate over a Graph",
    "method ok @types.hpp:30 — Returns a ToolResult representing a successful outcome",
    "method update @duckdb_mind.hpp:101 — Incorporates a reward in [-1,1] into the Beta prior",
    "method sample @duckdb_mind.hpp:86 — Returns a random draw from the Beta(alpha, beta) distribution",
    "method run_attractor_dynamics @mind.hpp:3208 — Identifies up to max_attractors stable fixed points",
    "class AttractorDampener @attractor_dampener.hpp:62 — Tracks per-node retrieval frequency to damp repetition",
    "class MmapGraphStore @mmap_graph_store.hpp:99 — A persistent memory-mapped graph store",
    "method query_subject @duckdb_mind.hpp:660 — Returns a vector of (predicate, object) pairs",
    "function rrf_fusion @scoring.hpp:798 — Merges two ranked result lists (dense and sparse) via RRF",
    "class TripletDeltaResult @wal.hpp:468 — Represents a single triple change in the WAL",
    "method build_goal_basin_unlocked @mind.hpp:1840 — Clears the current goal basin and rebuilds it",
    "method lsh_find_similar @mind.hpp:4247 — Returns up to max_candidates nodes by LSH bucket match",
    "class SimpleMindConfig @simple_mind.hpp:36 — Holds tunable settings for the SimpleMind engine",
    "function predicate_to_edge_type @types.hpp:282 — Converts a predicate string to an EdgeType enum",
    "class QueryPatterns @query_router.hpp:48 — Provides regular expressions for query classification",
    "class StalenessStats @mind.hpp:4440 — Holds counters for items grouped by freshness tier",
    "class PileupData @polish.cpp:50 — Represents the pileup at a genomic position by strand",
    "method tool_transcript_get @duckdb_handler.hpp:2227 — Retrieves triplets from the conversation transcript",
    "class PendingResonanceOutcome @duckdb_mind.hpp:180 — Represents a pending resonance learning event",
    "class Suggestion @duckdb_store.hpp:231 — Represents a recorded suggestion or proposal",
    "function compute_gtdb_damage_score @gtdb_damage_likelihood.hpp:534 — Computes aDNA damage score",
    "method clear @scoring.hpp:450 — Resets the object scoring state by clearing all posting lists",
    "function normalize_answer @evaluate.py:70 — Prepares a string for comparison by lowercasing and stripping",
    "method query_predicate @graph_store.hpp:464 — Returns a vector of (subject, object) pairs for a predicate",
    "class MergeSig @binner.cpp:5161 — A compact summary of a genomic bin for merge decisions",
    "method or_ @ops.hpp:112 — Constructs a Condition representing the logical OR of two predicates",
]

TOPICS = [
    "compiler optimization techniques like loop unrolling and inlining",
    "memory management and garbage collection algorithms",
    "biological evolution and natural selection mechanisms",
    "machine learning gradient descent and backpropagation",
    "database B-tree indexing and query planning",
    "TCP/IP network protocol design and flow control",
    "Bayesian statistical inference and posterior updates",
    "software testing strategies: unit, integration, property-based",
    "REST API design principles and versioning",
    "cache eviction policies: LRU, LFU, ARC",
    "error handling patterns: exceptions vs result types",
    "concurrent programming with locks and lock-free structures",
    "genomics sequence alignment algorithms (Smith-Waterman, BLAST)",
    "protein folding energy minimization and thermodynamics",
    "climate modeling and general circulation models",
    "CRISPR gene editing mechanisms and off-target effects",
    "distributed consensus algorithms (Raft, Paxos)",
    "neural network architecture design (attention, convolution)",
    "compression algorithms (LZ77, Huffman, Zstandard)",
    "cryptographic hash functions and collision resistance",
    "file system design: journaling, copy-on-write",
    "operating system scheduling: CFS, real-time preemption",
    "graph algorithms: Dijkstra, A*, Bellman-Ford",
    "linear algebra optimizations: BLAS, cache blocking",
    "SQL query optimization: join ordering, predicate pushdown",
    "Rust ownership model and borrow checker semantics",
    "Docker container isolation using cgroups and namespaces",
    "HTTP/2 and HTTP/3 multiplexing and header compression",
    "reactive programming and backpressure handling",
    "language server protocol and incremental parsing",
    "vector databases and approximate nearest neighbor search",
    "transformer attention complexity and sparse attention",
    "LoRA fine-tuning and parameter-efficient adaptation",
    "quantization: INT8, GPTQ, AWQ trade-offs",
    "RLHF reward modeling and preference learning",
    "memory-mapped I/O and zero-copy networking",
    "SIMD intrinsics for bioinformatics (AVX-512)",
    "metagenomics binning by tetra-nucleotide frequency",
    "ancient DNA damage patterns (cytosine deamination)",
    "HMM-based gene prediction and Viterbi decoding",
    "WebAssembly sandbox and linear memory model",
    "JIT compilation and trace-based optimization",
    "NUMA-aware memory allocation strategies",
    "B+ tree vs LSM tree trade-offs for write-heavy workloads",
    "consistent hashing and virtual nodes in distributed systems",
    "TLS handshake and certificate chain validation",
    "OAuth 2.0 token flows and PKCE",
    "event sourcing and CQRS patterns",
    "observability: traces, metrics, logs correlation",
    "chaos engineering and fault injection testing",
]

PROMPT_TEMPLATE = """Generate 10 diverse SSL distillation training examples about software engineering, science, and engineering topics. For each, provide:
1. A TEXT PASSAGE (2-4 sentences about a technical topic)
2. SSL LINES distilled from that passage (2-4 lines using exactly this format)

SSL format types:
[SOLUTION] [domain] subject→action→result
[GOTCHA] [domain] trap→consequence→lesson
[PATTERN] [domain] concept→mechanism→outcome
[DECISION] [domain] option_chosen→why→tradeoff
[PREFERENCE] [domain] preferred→over→reason
[FAILURE] [domain] attempt→what_broke→lesson

Format each example exactly as:
---EXAMPLE---
TEXT: <the passage>
SSL:
<ssl line 1>
<ssl line 2>
<optional ssl line 3>
---END---

Focus on these topics (mix them up): {topics}

Be specific and technical. Each passage should be distinct and informative."""

def parse_examples(response_text: str) -> list:
    examples = []
    blocks = re.split(r'---EXAMPLE---', response_text)
    for block in blocks[1:]:
        end_match = re.search(r'---END---', block)
        if end_match:
            block = block[:end_match.start()]
        text_match = re.search(r'TEXT:\s*(.+?)(?=\nSSL:)', block, re.DOTALL)
        ssl_match = re.search(r'SSL:\s*\n(.+)', block, re.DOTALL)
        if text_match and ssl_match:
            text = text_match.group(1).strip()
            ssl_raw = ssl_match.group(1).strip()
            ssl_lines = [l.strip() for l in ssl_raw.split('\n') if l.strip().startswith('[')]
            if text and ssl_lines:
                examples.append({
                    "text": text,
                    "ssl": "\n".join(ssl_lines),
                })
    return examples

def main():
    client = anthropic.Anthropic()
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)

    existing = 0
    if OUTPUT.exists():
        with open(OUTPUT) as f:
            existing = sum(1 for line in f if line.strip())
    print(f"Existing pairs: {existing}", flush=True)

    target = 300
    pairs_written = existing
    batch_num = 0
    errors = 0

    with open(OUTPUT, "a") as out_f:
        while pairs_written < target:
            batch_num += 1
            topics_str = ", ".join(random.sample(TOPICS, 5))
            prompt = PROMPT_TEMPLATE.format(topics=topics_str)

            try:
                print(f"Batch {batch_num}: requesting... (have {pairs_written}/{target})", flush=True)
                msg = client.messages.create(
                    model="claude-haiku-4-5",
                    max_tokens=4096,
                    messages=[{"role": "user", "content": prompt}],
                )
                response_text = msg.content[0].text
                examples = parse_examples(response_text)
                print(f"  Parsed {len(examples)} examples", flush=True)

                for ex in examples:
                    if pairs_written >= target:
                        break
                    rejected = random.choice(REJECTION_POOL)
                    record = {
                        "prompt": ex["text"],
                        "chosen": ex["ssl"],
                        "rejected": rejected,
                        "source": "synthetic",
                    }
                    out_f.write(json.dumps(record) + "\n")
                    out_f.flush()
                    pairs_written += 1

                print(f"  Total written: {pairs_written}", flush=True)

            except anthropic.RateLimitError:
                print("  Rate limited, waiting 30s...", flush=True)
                time.sleep(30)
                errors += 1
            except Exception as e:
                print(f"  Error in batch {batch_num}: {e}", flush=True)
                errors += 1
                if errors > 10:
                    print("Too many errors, stopping.", flush=True)
                    break
                time.sleep(5)

    print(f"\nDone. Total pairs: {pairs_written}, errors: {errors}", flush=True)
    print(f"Output: {OUTPUT}", flush=True)

if __name__ == "__main__":
    main()
