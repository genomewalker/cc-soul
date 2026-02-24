#pragma once
// DuckDBRpcHandler: RPC handler for DuckDBMind
//
// Same interface as SimpleRpcHandler but uses DuckDBMind backend.

#include "../mind/duckdb_mind.hpp"
#include "../mind/subconscious.hpp"
#include "../mind/payload.hpp"
#include "../sadhana/sadhana_manager.hpp"
#include "../code_intel.hpp"
#include "../symbol_resolver.hpp"
#include "../version.hpp"
#include "../query_intent.hpp"
#include "../transcript_parser.hpp"
#include "../temporal.hpp"
#include <nlohmann/json.hpp>
#include <glob.h>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cctype>
#include <array>
#include <regex>
#include <cstdio>
#include <cmath>
#include <unistd.h>

namespace chitta {

using json = nlohmann::json;

struct DuckDBToolResult {
    bool is_error = false;
    std::string text;
    json structured;

    static DuckDBToolResult ok(const std::string& t, const json& s = json()) {
        return {false, t, s};
    }
    static DuckDBToolResult error(const std::string& msg) {
        return {true, msg, json()};
    }
};

// Extract "parent/basename" from a full file path for display (disambiguates same-named files)
inline std::string display_path(const std::string& file_path) {
    size_t last_slash = file_path.rfind('/');
    if (last_slash == std::string::npos) return file_path;
    std::string basename = file_path.substr(last_slash + 1);
    if (last_slash > 0) {
        size_t prev_slash = file_path.rfind('/', last_slash - 1);
        if (prev_slash != std::string::npos) {
            return file_path.substr(prev_slash + 1);
        }
    }
    return basename;
}

class DuckDBRpcHandler {
public:
    explicit DuckDBRpcHandler(DuckDBMind* mind) : mind_(mind), subconscious_(nullptr), sadhana_manager_(nullptr) {
        register_tools();
    }

    // Connect subconscious for event pushing
    void set_subconscious(Subconscious* s) { subconscious_ = s; }

    // Connect sadhana manager for autonomous agents
    void set_sadhana_manager(SadhanaManager* sm) { sadhana_manager_ = sm; }

    json handle(const json& request) {
        std::string method = request.value("method", "");
        json params = request.value("params", json::object());
        auto id = request.value("id", json());

        if (method == "tools/list") {
            return make_response(id, tool_list());
        }

        if (method == "tools/call") {
            std::string name = params.value("name", "");
            json args = params.value("arguments", json::object());

            auto it = handlers_.find(name);
            if (it == handlers_.end()) {
                return make_error(id, -32601, "Unknown tool: " + name);
            }

            auto result = it->second(args);
            return make_tool_response(id, result);
        }

        return make_error(id, -32601, "Unknown method: " + method);
    }

private:
    DuckDBMind* mind_;
    Subconscious* subconscious_;
    SadhanaManager* sadhana_manager_;
    std::vector<json> tools_;
    std::unordered_map<std::string, std::function<DuckDBToolResult(const json&)>> handlers_;
    std::unordered_map<std::string, std::string> tool_visibility_;

    // Category to confidence mapping for high-value learnings
    static float category_to_confidence(const std::string& category) {
        if (category == "correction") return 0.95f;
        if (category == "preference") return 0.90f;
        if (category == "solution")   return 0.90f;
        if (category == "milestone")  return 0.90f;
        if (category == "decision")   return 0.85f;
        if (category == "failure")    return 0.85f;
        if (category == "gotcha")     return 0.85f;
        if (category == "episode")    return 0.70f;
        return 0.80f;  // wisdom, pattern, insight, belief, etc.
    }

    // Heuristic: detect code-like queries (use BM25) vs natural language (use semantic)
    static std::string_view trim_view(std::string_view s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
        return s;
    }

    static bool starts_with_ci(std::string_view s, std::string_view prefix) {
        if (s.size() < prefix.size()) return false;
        for (size_t i = 0; i < prefix.size(); ++i) {
            unsigned char a = static_cast<unsigned char>(s[i]);
            unsigned char b = static_cast<unsigned char>(prefix[i]);
            if (std::tolower(a) != std::tolower(b)) return false;
        }
        return true;
    }

    static bool looks_like_code_query(const std::string& q) {
        std::string_view s = trim_view(q);
        if (s.empty()) return false;

        // Strong NL prefixes - use semantic search
        if (starts_with_ci(s, "how ") || starts_with_ci(s, "why ") || starts_with_ci(s, "what ") ||
            starts_with_ci(s, "where ") || starts_with_ci(s, "explain ") || starts_with_ci(s, "describe ") ||
            starts_with_ci(s, "find ") || starts_with_ci(s, "show ")) {
            return false;
        }

        // Strong code punctuation - use BM25
        if (s.find("::") != std::string_view::npos || s.find("->") != std::string_view::npos ||
            s.find('(') != std::string_view::npos || s.find(')') != std::string_view::npos ||
            s.find('_') != std::string_view::npos || s.find('/') != std::string_view::npos ||
            s.find('\\') != std::string_view::npos || s.find('#') != std::string_view::npos ||
            s.find('.') != std::string_view::npos) {
            return true;
        }

        // Single-token identifier-ish (no spaces, mostly identifier chars)
        bool has_space = s.find_first_of(" \t\n") != std::string_view::npos;
        if (!has_space && s.size() <= 80) {
            size_t ok = 0;
            for (char c : s) {
                unsigned char uc = static_cast<unsigned char>(c);
                if (std::isalnum(uc) || c == ':' || c == '.' || c == '-') ok++;
            }
            if (ok == s.size()) return true;
        }

        return false;
    }

    // Typed query expansion: split query into BM25-optimized, vector-optimized, and HyDE variants
    struct ExpandedQuery {
        std::string lex;   // keyword variant for BM25
        std::string vec;   // natural language for vector
        std::string hyde;  // hypothetical memory excerpt
    };

    static ExpandedQuery expand_query(const std::string& query) {
        ExpandedQuery eq;
        eq.vec = query;
        eq.hyde = "[memory about] " + query;

        static const std::unordered_set<std::string> STOP_WORDS = {
            "a","an","the","is","are","was","were","be","been","being","have","has","had",
            "do","does","did","will","would","could","should","may","might","shall","can",
            "to","of","in","for","on","with","at","by","from","as","into","through",
            "i","me","my","we","our","you","your","he","him","his","she","her","it","its",
            "they","them","their","what","which","who","this","that","these","those",
            "not","only","just","about","so","than","too","very","all","both","each",
            "more","most","other","some","such","no","own","same","here","there",
            "when","where","why","how","then","once","am","out","off","over","under"
        };

        std::ostringstream lex_ss;
        std::istringstream iss(query);
        std::string word;
        bool first = true;
        while (iss >> word) {
            std::string lower;
            lower.reserve(word.size());
            for (char c : word) {
                unsigned char uc = static_cast<unsigned char>(c);
                if (std::isalnum(uc)) lower += static_cast<char>(std::tolower(uc));
            }
            if (!lower.empty() && STOP_WORDS.find(lower) == STOP_WORDS.end()) {
                if (!first) lex_ss << " ";
                lex_ss << lower;
                first = false;
            }
        }
        eq.lex = lex_ss.str().empty() ? query : lex_ss.str();
        return eq;
    }

    // Helper to parse ID from JSON (accepts both string and number)
    static std::pair<int64_t, std::string> parse_id(const json& params, const std::string& key = "id") {
        int64_t db_id = 0;
        std::string id_str;

        if (params.contains(key)) {
            const auto& val = params[key];
            if (val.is_number_integer()) {
                db_id = val.get<int64_t>();
                id_str = std::to_string(db_id);
            } else if (val.is_string()) {
                id_str = val.get<std::string>();
                // Check if it's a UUID (contains dashes or is 36 chars)
                if (id_str.find('-') != std::string::npos || id_str.length() == 36) {
                    NodeId nid = NodeId::from_string(id_str);
                    db_id = static_cast<int64_t>(nid.low);
                } else {
                    try {
                        db_id = std::stoll(id_str);
                    } catch (...) {
                        db_id = 0;
                    }
                }
            }
        }
        return {db_id, id_str};
    }

    void register_tools() {
        // remember
        tools_.push_back({
            {"name", "remember"},
            {"description", "Store text in memory with optional tags and realm"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"content", {{"type", "string"}, {"description", "Text to remember"}}},
                    {"type", {{"type", "string"}, {"description", "Node type (wisdom, insight, signal, episode)"}}},
                    {"confidence", {{"type", "number"}, {"description", "Initial confidence 0-1 (default: 0.8)"}}},
                    {"tags", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Optional tags"}}},
                    {"realm", {{"type", "string"}, {"description", "Primary realm (default: brahman)"}}},
                    {"visibility", {{"type", "integer"}, {"description", "0=Private, 1=Shared, 2=Global (default: 0)"}}},
                    {"shared_realms", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Additional realms to share with"}}}
                }},
                {"required", {"content"}}
            }}
        });
        handlers_["remember"] = [this](const json& p) { return tool_remember(p); };

        // recall
        tools_.push_back({
            {"name", "recall"},
            {"description", "Search memory by semantic similarity with realm filtering"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Search query"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default 10)"}}},
                    {"min_confidence", {{"type", "number"}, {"description", "Minimum confidence threshold (default: 0)"}}},
                    {"tag", {{"type", "string"}, {"description", "Filter by tag"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm (empty = all realms)"}}},
                    {"include_global", {{"type", "boolean"}, {"description", "Include global memories (default: true)"}}}
                }},
                {"required", {"query"}}
            }}
        });
        handlers_["recall"] = [this](const json& p) { return tool_recall(p); };

        // recall_temporal: time-bounded memory search
        tools_.push_back({
            {"name", "recall_temporal"},
            {"description", "Search memories within a time window (defaults to last 7 days if no dates), optionally with semantic filtering"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Optional semantic search query"}}},
                    {"start", {{"type", "string"}, {"description", "Start date (ISO8601 or YYYY-MM-DD)"}}},
                    {"end", {{"type", "string"}, {"description", "End date (ISO8601 or YYYY-MM-DD)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default 20)"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}},
                    {"include_global", {{"type", "boolean"}, {"description", "Include global memories (default: true)"}}}
                }}
            }}
        });
        handlers_["recall_temporal"] = [this](const json& p) { return tool_recall_temporal(p); };

        // RLM-style exploration primitives
        tools_.push_back({
            {"name", "explore_recall"},
            {"description", "Lightweight recall - returns titles/scores only, no full content (for iterative exploration)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Search query"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default 10)"}}}
                }},
                {"required", {"query"}}
            }}
        });
        handlers_["explore_recall"] = [this](const json& p) { return tool_explore_recall(p); };

        tools_.push_back({
            {"name", "explore_peek"},
            {"description", "Get summary of a memory (first 200 chars) without loading full content"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Memory ID to peek"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["explore_peek"] = [this](const json& p) { return tool_explore_peek(p); };

        tools_.push_back({
            {"name", "explore_expand"},
            {"description", "Get full content of a memory (use sparingly during exploration)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Memory ID to expand"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["explore_expand"] = [this](const json& p) { return tool_explore_expand(p); };

        tools_.push_back({
            {"name", "explore_neighbors"},
            {"description", "Get nodes connected to this node via triplets"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"node", {{"type", "string"}, {"description", "Node name to find neighbors of"}}},
                    {"direction", {{"type", "string"}, {"description", "outgoing, incoming, or both (default: both)"}}}
                }},
                {"required", {"node"}}
            }}
        });
        handlers_["explore_neighbors"] = [this](const json& p) { return tool_explore_neighbors(p); };

        // connect
        tools_.push_back({
            {"name", "connect"},
            {"description", "Create a triplet relationship"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"subject", {{"type", "string"}, {"description", "Subject entity"}}},
                    {"predicate", {{"type", "string"}, {"description", "Relationship type"}}},
                    {"object", {{"type", "string"}, {"description", "Object entity"}}}
                }},
                {"required", {"subject", "predicate", "object"}}
            }}
        });
        handlers_["connect"] = [this](const json& p) { return tool_connect(p); };

        // query_graph
        tools_.push_back({
            {"name", "query_graph"},
            {"description", "Query triplets by subject or object"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"subject", {{"type", "string"}, {"description", "Query by subject"}}},
                    {"object", {{"type", "string"}, {"description", "Query by object"}}}
                }}
            }}
        });
        handlers_["query_graph"] = [this](const json& p) { return tool_query_graph(p); };

        // query_triplets_temporal: point-in-time triplet queries
        tools_.push_back({
            {"name", "query_triplets_temporal"},
            {"description", "Query triplets at a specific point in time (temporal fact versioning)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"subject", {{"type", "string"}, {"description", "Filter by subject entity"}}},
                    {"predicate", {{"type", "string"}, {"description", "Filter by relationship type"}}},
                    {"object", {{"type", "string"}, {"description", "Filter by object entity"}}},
                    {"at_date", {{"type", "string"}, {"description", "Date to query at (YYYY-MM-DD). Default: now"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default 50)"}}}
                }}
            }}
        });
        handlers_["query_triplets_temporal"] = [this](const json& p) { return tool_query_triplets_temporal(p); };

        // triplet_history: show evolution of a fact over time
        tools_.push_back({
            {"name", "triplet_history"},
            {"description", "Get history of a subject-predicate relationship over time"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"subject", {{"type", "string"}, {"description", "Subject entity"}}},
                    {"predicate", {{"type", "string"}, {"description", "Relationship type"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default 20)"}}}
                }},
                {"required", {"subject", "predicate"}}
            }}
        });
        handlers_["triplet_history"] = [this](const json& p) { return tool_triplet_history(p); };

        // connect_temporal: create triplet with temporal validity
        tools_.push_back({
            {"name", "connect_temporal"},
            {"description", "Create a triplet with temporal validity period"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"subject", {{"type", "string"}, {"description", "Subject entity"}}},
                    {"predicate", {{"type", "string"}, {"description", "Relationship type"}}},
                    {"object", {{"type", "string"}, {"description", "Object entity"}}},
                    {"valid_from", {{"type", "string"}, {"description", "When fact became true (YYYY-MM-DD or relative like 'yesterday')"}}},
                    {"valid_to", {{"type", "string"}, {"description", "When fact stopped being true (YYYY-MM-DD). Omit for still valid"}}},
                    {"context_date", {{"type", "string"}, {"description", "Session date for resolving relative dates (YYYY-MM-DD)"}}}
                }},
                {"required", {"subject", "predicate", "object"}}
            }}
        });
        handlers_["connect_temporal"] = [this](const json& p) { return tool_connect_temporal(p); };

        // convergence_metrics: measure whether self-improvement is converging or drifting
        tools_.push_back({
            {"name", "convergence_metrics"},
            {"description", "Measure self-improvement convergence: correction recall rate + triplet traversal rate"},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}
        });
        handlers_["convergence_metrics"] = [this](const json& p) { return tool_convergence_metrics(p); };

        // resonance_stats: expose what the ResonanceLearner Bayesian bandit has learned
        tools_.push_back({
            {"name", "resonance_stats"},
            {"description", "Show what the ResonanceLearner Bayesian bandit has learned about optimal memory retrieval. Exposes the 7 resonance parameters, their posterior means, uncertainty, and feedback history. Use this to understand how the soul's memory retrieval is self-tuning."},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}
        });
        handlers_["resonance_stats"] = [this](const json& p) { return tool_resonance_stats(p); };

        // soul_context
        tools_.push_back({
            {"name", "soul_context"},
            {"description", "Get current soul state and statistics"},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}
        });
        handlers_["soul_context"] = [this](const json& p) { return tool_soul_context(p); };

        tools_.push_back({
            {"name", "enrichment_status"},
            {"description", "Get code enrichment progress (semantic descriptions for symbols)"},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}
        });
        handlers_["enrichment_status"] = [this](const json& p) { return tool_enrichment_status(p); };

        tools_.push_back({
            {"name", "describe_symbol"},
            {"description", "Set description for a code symbol (stores directly in symbol table, not as wisdom)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"symbol_id", {{"type", "integer"}, {"description", "Symbol ID to describe"}}},
                    {"description", {{"type", "string"}, {"description", "Semantic description of the symbol"}}}
                }},
                {"required", {"symbol_id", "description"}}
            }}
        });
        handlers_["describe_symbol"] = [this](const json& p) { return tool_describe_symbol(p); };

        tools_.push_back({
            {"name", "cleanup_code_wisdom"},
            {"description", "Migration: delete [code] wisdom memories and clear orphaned symbol.memory_id references"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"dry_run", {{"type", "boolean"}, {"description", "Preview only without changes (default: true)"}}}
                }}
            }}
        });
        handlers_["cleanup_code_wisdom"] = [this](const json& p) { return tool_cleanup_code_wisdom(p); };

        tools_.push_back({
            {"name", "subconscious_stats"},
            {"description", "Get subconscious background processor statistics"},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}
        });
        handlers_["subconscious_stats"] = [this](const json& p) { return tool_subconscious_stats(p); };

        tools_.push_back({
            {"name", "reembed_memories"},
            {"description", "Re-embed memories with proper embeddings. Use to fix memories stored with zero embeddings."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"all", {{"type", "boolean"}, {"description", "Re-embed ALL memories with NULL embeddings, not just global (default: false)"}}},
                    {"kind", {{"type", "string"}, {"description", "Filter by kind: belief, wisdom, episode, correction, preference"}}},
                    {"min_confidence", {{"type", "number"}, {"description", "Min confidence threshold (default: 0)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max memories to process (default: 30)"}}},
                    {"dry_run", {{"type", "boolean"}, {"description", "Preview without updating (default: false)"}}}
                }}
            }}
        });
        handlers_["reembed_memories"] = [this](const json& p) { return tool_reembed_memories(p); };

        tools_.push_back({
            {"name", "embed_symbols"},
            {"description", "Fast embed symbol metadata (no LLM needed, ~100/sec). Use reset=true to clear all embeddings and re-embed with richer text."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"batch_size", {{"type", "integer"}, {"description", "Symbols per batch (default: 100)"}}},
                    {"reset", {{"type", "boolean"}, {"description", "Clear all symbol embeddings before re-embedding (default: false)"}}}
                }}
            }}
        });
        handlers_["embed_symbols"] = [this](const json& p) { return tool_embed_symbols(p); };

        tools_.push_back({
            {"name", "dedupe_symbols"},
            {"description", "Remove duplicate symbols from the database"},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}
        });
        handlers_["dedupe_symbols"] = [this](const json& p) { return tool_dedupe_symbols(p); };

        // Migrate embeddings to VSS database
        tools_.push_back({
            {"name", "migrate_vss"},
            {"description", "Migrate embeddings from main DB to separate VSS database for HNSW stability"},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}
        });
        handlers_["migrate_vss"] = [this](const json& p) { return tool_migrate_vss(p); };

        // strengthen
        tools_.push_back({
            {"name", "strengthen"},
            {"description", "Increase confidence of a memory"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Node ID"}}},
                    {"amount", {{"type", "number"}, {"description", "Amount to strengthen (default 0.1)"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["strengthen"] = [this](const json& p) { return tool_strengthen(p); };

        // weaken
        tools_.push_back({
            {"name", "weaken"},
            {"description", "Decrease confidence of a memory"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Node ID"}}},
                    {"amount", {{"type", "number"}, {"description", "Amount to weaken (default 0.1)"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["weaken"] = [this](const json& p) { return tool_weaken(p); };

        // forget
        tools_.push_back({
            {"name", "forget"},
            {"description", "Remove a memory"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Node ID to forget"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["forget"] = [this](const json& p) { return tool_forget(p); };

        // batch_forget - Bulk delete multiple nodes
        tools_.push_back({
            {"name", "batch_forget"},
            {"description", "Delete multiple nodes by ID (batch deletion)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"ids", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Array of node IDs (UUID strings)"}}},
                    {"pattern", {{"type", "string"}, {"description", "Search pattern to find and delete matching nodes (alternative to ids)"}}}
                }}
            }}
        });
        handlers_["batch_forget"] = [this](const json& p) { return tool_batch_forget(p); };

        // observe
        tools_.push_back({
            {"name", "observe"},
            {"description", "Store an observation/learning (used by hooks for [LEARN] extraction)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"category", {{"type", "string"}, {"description", "Category: correction, preference, solution, decision, failure, wisdom, episode"}}},
                    {"title", {{"type", "string"}, {"description", "Title/summary"}}},
                    {"content", {{"type", "string"}, {"description", "Full content"}}},
                    {"tags", {{"type", "string"}, {"description", "Comma-separated tags"}}},
                    {"confidence", {{"type", "number"}, {"description", "Optional confidence override (0.0-1.0). If not set, derived from category."}}}
                }},
                {"required", {"title", "content"}}
            }}
        });
        handlers_["observe"] = [this](const json& p) { return tool_observe(p); };

        // full_resonate
        tools_.push_back({
            {"name", "full_resonate"},
            {"description", "Semantic search with full context (for hooks)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Search query"}}},
                    {"k", {{"type", "integer"}, {"description", "Max results"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm (e.g., 'project:my-project')"}}},
                    {"include_global", {{"type", "boolean"}, {"description", "Include global memories (default true)"}}},
                    {"exclude_kinds", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Memory kinds to exclude from results"}}},
                    {"partnership_only", {{"type", "boolean"}, {"description", "Exclude code intel (symbol, projectessence, modulestate, patternstate)"}}}
                }},
                {"required", {"query"}}
            }}
        });
        handlers_["full_resonate"] = [this](const json& p) { return tool_full_resonate(p); };

        // Code intelligence tools
        tools_.push_back({
            {"name", "extract_symbols"},
            {"description", "Extract symbols (functions, classes) from source file using tree-sitter"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "File path to analyze"}}}
                }},
                {"required", {"path"}}
            }}
        });
        handlers_["extract_symbols"] = [this](const json& p) { return tool_extract_symbols(p); };

        tools_.push_back({
            {"name", "learn_codebase"},
            {"description", "Learn codebase incrementally - only re-indexes changed files"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Directory path to analyze"}}},
                    {"project", {{"type", "string"}, {"description", "Project name (auto-detected if empty)"}}},
                    {"max_files", {{"type", "integer"}, {"description", "Max files to process (default 500)"}}},
                    {"exclude", {{"type", "string"}, {"description", "Comma-separated directories to exclude"}}},
                    {"incremental", {{"type", "boolean"}, {"description", "Only process changed files (default true)"}}},
                    {"force", {{"type", "boolean"}, {"description", "Force full re-index (default false)"}}}
                }},
                {"required", {"path"}}
            }}
        });
        handlers_["learn_codebase"] = [this](const json& p) { return tool_learn_codebase(p); };

        tools_.push_back({
            {"name", "find_symbol"},
            {"description", "Search for symbols by name"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"name", {{"type", "string"}, {"description", "Symbol name to search"}}},
                    {"kind", {{"type", "string"}, {"description", "Symbol kind filter (function, class, method)"}}}
                }},
                {"required", {"name"}}
            }}
        });
        handlers_["find_symbol"] = [this](const json& p) { return tool_find_symbol(p); };

        // Call graph tools
        tools_.push_back({
            {"name", "symbol_callers"},
            {"description", "Find all symbols that call the given symbol (reverse call graph)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"name", {{"type", "string"}, {"description", "Symbol name to find callers for"}}},
                    {"id", {{"type", "integer"}, {"description", "Symbol ID (alternative to name)"}}},
                    {"kind", {{"type", "string"}, {"description", "Filter by symbol kind when using name"}}},
                    {"project", {{"type", "string"}, {"description", "Project name to disambiguate when multiple symbols share the same name"}}}
                }}
            }}
        });
        handlers_["symbol_callers"] = [this](const json& p) { return tool_symbol_callers(p); };

        tools_.push_back({
            {"name", "symbol_callees"},
            {"description", "Find all symbols that the given symbol calls (forward call graph)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"name", {{"type", "string"}, {"description", "Symbol name to find callees for"}}},
                    {"id", {{"type", "integer"}, {"description", "Symbol ID (alternative to name)"}}},
                    {"kind", {{"type", "string"}, {"description", "Filter by symbol kind when using name"}}},
                    {"project", {{"type", "string"}, {"description", "Project name to disambiguate when multiple symbols share the same name"}}}
                }}
            }}
        });
        handlers_["symbol_callees"] = [this](const json& p) { return tool_symbol_callees(p); };

        tools_.push_back({
            {"name", "read_symbol"},
            {"description", "Read the actual source code for a symbol by name or ID"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"name", {{"type", "string"}, {"description", "Symbol name to read"}}},
                    {"id", {{"type", "integer"}, {"description", "Symbol ID (alternative to name)"}}},
                    {"kind", {{"type", "string"}, {"description", "Filter by symbol kind (function, class, method)"}}},
                    {"project", {{"type", "string"}, {"description", "Project name to disambiguate when multiple symbols share the same name"}}}
                }}
            }}
        });
        handlers_["read_symbol"] = [this](const json& p) { return tool_read_symbol(p); };

        tools_.push_back({
            {"name", "read_function"},
            {"description", "Read the source code of a function/method by name (shorthand for read_symbol with kind=function|method)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"name", {{"type", "string"}, {"description", "Function name to read"}}},
                    {"project", {{"type", "string"}, {"description", "Project name to disambiguate"}}}
                }},
                {"required", {"name"}}
            }}
        });
        handlers_["read_function"] = [this](const json& p) { return tool_read_function(p); };

        tools_.push_back({
            {"name", "search_symbols"},
            {"description", "Semantic search for code symbols by natural language query. Returns symbols ranked by embedding similarity."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Natural language query to find symbols"}}},
                    {"kind", {{"type", "string"}, {"description", "Filter by symbol kind (class, function, method)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default 10)"}}},
                    {"project", {{"type", "string"}, {"description", "Filter results to symbols from this project only"}}}
                }},
                {"required", {"query"}}
            }}
        });
        handlers_["search_symbols"] = [this](const json& p) { return tool_search_symbols(p); };

        tools_.push_back({
            {"name", "code_context"},
            {"description", "Get code context summary for hooks"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Limit to files under this path"}}}
                }}
            }}
        });
        handlers_["code_context"] = [this](const json& p) { return tool_code_context(p); };

        // Unified smart context for agentic search
        tools_.push_back({
            {"name", "smart_context"},
            {"description", "Build intelligent context combining memories, code symbols, and graph relationships. Two modes: fast (<80ms) for PreToolUse hooks, full (<200ms) for UserPromptSubmit hooks."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"task", {{"type", "string"}, {"description", "Query to find context for"}}},
                    {"mode", {{"type", "string"}, {"description", "fast: <80ms (vector + BM25), full: <200ms (full_resonate + semantic)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Token limit (default: 300)"}}},
                    {"memories", {{"type", "boolean"}, {"description", "Include semantic memories (default: true)"}}},
                    {"code", {{"type", "boolean"}, {"description", "Include code symbols (default: true)"}}},
                    {"neighbors", {{"type", "boolean"}, {"description", "Include triplet neighbors (default: true)"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}}
                }},
                {"required", {"task"}}
            }}
        });
        handlers_["smart_context"] = [this](const json& p) { return tool_smart_context(p); };

        tools_.push_back({
            {"name", "codebase_overview"},
            {"description", "Get full indexed codebase structure: files, classes, functions, relationships"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"project", {{"type", "string"}, {"description", "Project name to filter"}}},
                    {"format", {{"type", "string"}, {"description", "Output format: tree, flat, or json (default: tree)"}}},
                    {"include_callsites", {{"type", "boolean"}, {"description", "Include callsite info (default: false)"}}}
                }}
            }}
        });
        handlers_["codebase_overview"] = [this](const json& p) { return tool_codebase_overview(p); };

        tools_.push_back({
            {"name", "clear_codebase"},
            {"description", "Remove all code intelligence data (symbols, triplets) for a project"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"project", {{"type", "string"}, {"description", "Project name to clear"}}},
                    {"dry_run", {{"type", "boolean"}, {"description", "Preview only (default: false)"}}}
                }},
                {"required", {"project"}}
            }}
        });
        handlers_["clear_codebase"] = [this](const json& p) { return tool_clear_codebase(p); };

        tools_.push_back({
            {"name", "clear_triplets"},
            {"description", "Delete triplets by subject pattern (e.g., '%.cpp' for all C++ file triplets)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"pattern", {{"type", "string"}, {"description", "SQL LIKE pattern for subject (e.g., '%.cpp', '%.hpp', 'chitta/%')"}}},
                    {"dry_run", {{"type", "boolean"}, {"description", "Preview only (default: false)"}}}
                }},
                {"required", {"pattern"}}
            }}
        });
        handlers_["clear_triplets"] = [this](const json& p) { return tool_clear_triplets(p); };

        // Cross-file symbol resolution
        tools_.push_back({
            {"name", "resolve_callsites"},
            {"description", "Resolve callsites to symbols and populate call_edge table for call graph queries"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"project", {{"type", "string"}, {"description", "Filter to specific project path (optional)"}}}
                }}
            }}
        });
        handlers_["resolve_callsites"] = [this](const json& p) { return tool_resolve_callsites(p); };

        // Type hierarchy queries
        tools_.push_back({
            {"name", "type_hierarchy"},
            {"description", "Get type hierarchy (base classes, implemented interfaces) for a type"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"name", {{"type", "string"}, {"description", "Type name to query"}}},
                    {"direction", {{"type", "string"}, {"description", "ancestors, descendants, or both (default: both)"}}}
                }},
                {"required", {"name"}}
            }}
        });
        handlers_["type_hierarchy"] = [this](const json& p) { return tool_type_hierarchy(p); };

        // File imports query
        tools_.push_back({
            {"name", "file_imports"},
            {"description", "Get all imports for a file"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "File path or filename to query imports for"}}}
                }},
                {"required", {"path"}}
            }}
        });
        handlers_["file_imports"] = [this](const json& p) { return tool_file_imports(p); };

        // File dependents query
        tools_.push_back({
            {"name", "file_dependents"},
            {"description", "Get files that import a given module/file"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"module", {{"type", "string"}, {"description", "Module or file name to find dependents of"}}}
                }},
                {"required", {"module"}}
            }}
        });
        handlers_["file_dependents"] = [this](const json& p) { return tool_file_dependents(p); };

        // Essential memory tools
        tools_.push_back({
            {"name", "grow"},
            {"description", "Add wisdom, belief, failure, aspiration, or dream"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"type", {{"type", "string"}, {"description", "Type: wisdom, belief, failure, aspiration, dream"}}},
                    {"content", {{"type", "string"}, {"description", "Content to store"}}},
                    {"title", {{"type", "string"}, {"description", "Short title"}}},
                    {"tags", {{"type", "string"}, {"description", "Comma-separated tags"}}}
                }},
                {"required", {"type", "content"}}
            }}
        });
        handlers_["grow"] = [this](const json& p) { return tool_grow(p); };

        tools_.push_back({
            {"name", "get"},
            {"description", "Get a node by ID"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Node ID"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["get"] = [this](const json& p) { return tool_get(p); };

        tools_.push_back({
            {"name", "expand_memory"},
            {"description", "Expand a memory to its full hierarchical context: SSL memory → episode → full turns"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Memory ID to expand"}}},
                    {"depth", {{"type", "integer"}, {"description", "1=memory only, 2=+episode, 3=+full turns (default: 3)"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["expand_memory"] = [this](const json& p) { return tool_expand_memory(p); };

        tools_.push_back({
            {"name", "update"},
            {"description", "Update node content"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Node ID"}}},
                    {"content", {{"type", "string"}, {"description", "New content"}}}
                }},
                {"required", {"id", "content"}}
            }}
        });
        handlers_["update"] = [this](const json& p) { return tool_update(p); };

        tools_.push_back({
            {"name", "query"},
            {"description", "Query triplets with flexible filters"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"subject", {{"type", "string"}, {"description", "Subject filter"}}},
                    {"predicate", {{"type", "string"}, {"description", "Predicate filter"}}},
                    {"object", {{"type", "string"}, {"description", "Object filter"}}}
                }}
            }}
        });
        handlers_["query"] = [this](const json& p) { return tool_query(p); };

        tools_.push_back({
            {"name", "tag"},
            {"description", "Add or remove tags from a node"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Node ID"}}},
                    {"add", {{"type", "string"}, {"description", "Tag to add"}}},
                    {"remove", {{"type", "string"}, {"description", "Tag to remove"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["tag"] = [this](const json& p) { return tool_tag(p); };

        // Maintenance tools
        tools_.push_back({
            {"name", "health_check"},
            {"description", "Check daemon health and readiness"},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}
        });
        handlers_["health_check"] = [this](const json& p) { return tool_health_check(p); };

        tools_.push_back({
            {"name", "version_check"},
            {"description", "Get version information"},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}
        });
        handlers_["version_check"] = [this](const json&) { return tool_version_check(); };

        tools_.push_back({
            {"name", "cycle"},
            {"description", "Run maintenance cycle (decay, cleanup)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"force", {{"type", "boolean"}, {"description", "Force full cycle"}}}
                }}
            }}
        });
        handlers_["cycle"] = [this](const json& p) { return tool_cycle(p); };

        tools_.push_back({
            {"name", "cleanup"},
            {"description", "Remove garbage nodes"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"dry_run", {{"type", "boolean"}, {"description", "Preview only"}}}
                }}
            }}
        });
        handlers_["cleanup"] = [this](const json& p) { return tool_cleanup(p); };

        // Import/Export tools
        tools_.push_back({
            {"name", "import_soul"},
            {"description", "Import .soul file (SSL format)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"file", {{"type", "string"}, {"description", "Path to .soul file"}}},
                    {"content", {{"type", "string"}, {"description", "SSL content (alternative to file)"}}}
                }}
            }}
        });
        handlers_["import_soul"] = [this](const json& p) { return tool_import_soul(p); };

        tools_.push_back({
            {"name", "export_soul"},
            {"description", "Export memories to SSL format"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"file", {{"type", "string"}, {"description", "Output file path"}}},
                    {"tag", {{"type", "string"}, {"description", "Filter by tag"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max nodes to export"}}}
                }}
            }}
        });
        handlers_["export_soul"] = [this](const json& p) { return tool_export_soul(p); };

        // Realm tools
        tools_.push_back({
            {"name", "realm_list"},
            {"description", "List all known realms"},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}
        });
        handlers_["realm_list"] = [this](const json&) { return tool_realm_list(); };

        tools_.push_back({
            {"name", "realm_get"},
            {"description", "Get all realms a memory belongs to"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Memory ID"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["realm_get"] = [this](const json& p) { return tool_realm_get(p); };

        tools_.push_back({
            {"name", "realm_set"},
            {"description", "Set primary realm for a memory"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Memory ID"}}},
                    {"realm", {{"type", "string"}, {"description", "Primary realm name"}}}
                }},
                {"required", {"id", "realm"}}
            }}
        });
        handlers_["realm_set"] = [this](const json& p) { return tool_realm_set(p); };

        tools_.push_back({
            {"name", "realm_add"},
            {"description", "Add memory to a shared realm (multi-realm membership)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Memory ID"}}},
                    {"realm", {{"type", "string"}, {"description", "Realm to add to"}}}
                }},
                {"required", {"id", "realm"}}
            }}
        });
        handlers_["realm_add"] = [this](const json& p) { return tool_realm_add(p); };

        tools_.push_back({
            {"name", "realm_remove"},
            {"description", "Remove memory from a shared realm"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Memory ID"}}},
                    {"realm", {{"type", "string"}, {"description", "Realm to remove from"}}}
                }},
                {"required", {"id", "realm"}}
            }}
        });
        handlers_["realm_remove"] = [this](const json& p) { return tool_realm_remove(p); };

        tools_.push_back({
            {"name", "realm_visibility"},
            {"description", "Set visibility level for a memory (0=Private, 1=Shared, 2=Global)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Memory ID"}}},
                    {"visibility", {{"type", "integer"}, {"description", "0=Private, 1=Shared, 2=Global"}}}
                }},
                {"required", {"id", "visibility"}}
            }}
        });
        handlers_["realm_visibility"] = [this](const json& p) { return tool_realm_visibility(p); };

        tools_.push_back({
            {"name", "realm_detect"},
            {"description", "Detect current realm from environment, .cc-soul-realm file, or git repository"},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}
        });
        handlers_["realm_detect"] = [this](const json&) { return tool_realm_detect(); };

        // Ledger tools (session continuity)
        tools_.push_back({
            {"name", "ledger_save"},
            {"description", "Save session checkpoint for continuity"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session identifier"}}},
                    {"project", {{"type", "string"}, {"description", "Project scope"}}},
                    {"transcript_path", {{"type", "string"}, {"description", "Path to JSONL transcript file for full context recovery"}}},
                    {"mood", {{"type", "string"}, {"description", "Current feeling: confident|uncertain|flowing|frustrated"}}},
                    {"coherence", {{"type", "number"}, {"description", "Coherence score 0-1"}}},
                    {"confidence", {{"type", "number"}, {"description", "Confidence score 0-1"}}},
                    {"todos", {{"type", "array"}, {"description", "Array of {content, status} objects"}}},
                    {"active_files", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Array of file paths"}}},
                    {"decisions", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Array of key decisions"}}},
                    {"next_steps", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Array of next steps"}}},
                    {"blockers", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Array of blockers"}}},
                    {"discoveries", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Array of discoveries"}}},
                    {"snapshot", {{"type", "string"}, {"description", "Full checkpoint text for reconstruction"}}}
                }},
                {"required", json::array()}
            }}
        });
        handlers_["ledger_save"] = [this](const json& p) { return tool_ledger_save(p); };

        tools_.push_back({
            {"name", "ledger_load"},
            {"description", "Load most recent checkpoint"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session identifier (optional)"}}},
                    {"project", {{"type", "string"}, {"description", "Project filter (optional)"}}}
                }}
            }}
        });
        handlers_["ledger_load"] = [this](const json& p) { return tool_ledger_load(p); };

        tools_.push_back({
            {"name", "ledger_list"},
            {"description", "List recent checkpoints"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"project", {{"type", "string"}, {"description", "Project filter (optional)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max entries to return (default 10)"}}}
                }}
            }}
        });
        handlers_["ledger_list"] = [this](const json& p) { return tool_ledger_list(p); };

        tools_.push_back({
            {"name", "ledger_get"},
            {"description", "Get specific checkpoint by ID"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Checkpoint ID"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["ledger_get"] = [this](const json& p) { return tool_ledger_get(p); };

        tools_.push_back({
            {"name", "ledger_delete"},
            {"description", "Delete checkpoint"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Checkpoint ID to delete"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["ledger_delete"] = [this](const json& p) { return tool_ledger_delete(p); };

        // Long-running tasks (mind-powered Ralph Wiggum)
        tools_.push_back({
            {"name", "long_task_start"},
            {"description", "Start a long-running task with goal and completion criteria"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"task_id", {{"type", "string"}, {"description", "Unique task identifier"}}},
                    {"goal", {{"type", "string"}, {"description", "What you're trying to achieve"}}},
                    {"realm", {{"type", "string"}, {"description", "Project scope (default: brahman)"}}},
                    {"hard_checks", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Deterministic completion criteria"}}},
                    {"soft_checks", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Semantic completion criteria"}}},
                    {"work_items", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Initial subtasks"}}}
                }},
                {"required", {"task_id", "goal"}}
            }}
        });
        handlers_["long_task_start"] = [this](const json& p) { return tool_long_task_start(p); };

        tools_.push_back({
            {"name", "long_task_get"},
            {"description", "Get a long-running task by ID"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"task_id", {{"type", "string"}, {"description", "Task identifier"}}}
                }},
                {"required", {"task_id"}}
            }}
        });
        handlers_["long_task_get"] = [this](const json& p) { return tool_long_task_get(p); };

        tools_.push_back({
            {"name", "long_task_active"},
            {"description", "Get the active long-running task for a realm"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"realm", {{"type", "string"}, {"description", "Project scope (optional)"}}}
                }}
            }}
        });
        handlers_["long_task_active"] = [this](const json& p) { return tool_long_task_active(p); };

        tools_.push_back({
            {"name", "long_task_update"},
            {"description", "Update a long-running task progress"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"task_id", {{"type", "string"}, {"description", "Task identifier"}}},
                    {"completed_summary", {{"type", "string"}, {"description", "What's been accomplished"}}},
                    {"work_items", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Updated subtasks"}}},
                    {"blockers", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Current blockers"}}}
                }},
                {"required", {"task_id"}}
            }}
        });
        handlers_["long_task_update"] = [this](const json& p) { return tool_long_task_update(p); };

        tools_.push_back({
            {"name", "long_task_complete"},
            {"description", "Mark a long-running task as completed"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"task_id", {{"type", "string"}, {"description", "Task identifier"}}},
                    {"outcome", {{"type", "string"}, {"description", "Final outcome description"}}}
                }},
                {"required", {"task_id", "outcome"}}
            }}
        });
        handlers_["long_task_complete"] = [this](const json& p) { return tool_long_task_complete(p); };

        tools_.push_back({
            {"name", "long_task_event"},
            {"description", "Append an event to the task log (tool result, decision, observation)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"task_id", {{"type", "string"}, {"description", "Task identifier"}}},
                    {"kind", {{"type", "string"}, {"description", "Event type: tool_result, decision, observation, error, checkpoint"}}},
                    {"payload", {{"type", "string"}, {"description", "Event data (JSON)"}}},
                    {"tags", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Tags for filtering"}}},
                    {"related_entities", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Related files/functions"}}}
                }},
                {"required", {"task_id", "kind"}}
            }}
        });
        handlers_["long_task_event"] = [this](const json& p) { return tool_long_task_event(p); };

        // Unified checkpoint (consolidates ledger + task_event)
        tools_.push_back({
            {"name", "checkpoint"},
            {"description", "Save session state. Uses active long task if exists, otherwise standalone ledger."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"realm", {{"type", "string"}, {"description", "Project scope"}}},
                    {"mood", {{"type", "string"}, {"description", "confident|uncertain|flowing|frustrated"}}},
                    {"summary", {{"type", "string"}, {"description", "What's been accomplished"}}},
                    {"next_steps", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Next actions"}}},
                    {"active_files", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Files being worked on"}}},
                    {"discoveries", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Insights learned"}}}
                }}
            }}
        });
        handlers_["checkpoint"] = [this](const json& p) { return tool_unified_checkpoint(p); };

        tools_.push_back({
            {"name", "long_task_snapshot"},
            {"description", "Get synthesized task context for injection (what's done, pending, blockers, relevant memories)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"task_id", {{"type", "string"}, {"description", "Task identifier"}}},
                    {"mode", {{"type", "string"}, {"description", "Output mode: inject (compact) or debug (verbose)"}}},
                    {"max_tokens", {{"type", "integer"}, {"description", "Max output tokens (default: 2000)"}}}
                }},
                {"required", {"task_id"}}
            }}
        });
        handlers_["long_task_snapshot"] = [this](const json& p) { return tool_long_task_snapshot(p); };

        tools_.push_back({
            {"name", "long_task_evaluate"},
            {"description", "Evaluate task completion: run hard checks, return decision (complete/continue/blocked)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"task_id", {{"type", "string"}, {"description", "Task identifier"}}}
                }},
                {"required", {"task_id"}}
            }}
        });
        handlers_["long_task_evaluate"] = [this](const json& p) { return tool_long_task_evaluate(p); };

        // Suggestion tracking (loop closure)
        tools_.push_back({
            {"name", "suggestion_track"},
            {"description", "Track a suggestion made for later outcome evaluation"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"content", {{"type", "string"}, {"description", "What was suggested"}}},
                    {"context", {{"type", "string"}, {"description", "Why/when it was suggested"}}},
                    {"realm", {{"type", "string"}, {"description", "Project scope (default: brahman)"}}}
                }},
                {"required", {"content"}}
            }}
        });
        handlers_["suggestion_track"] = [this](const json& p) { return tool_suggestion_track(p); };

        tools_.push_back({
            {"name", "suggestion_pending"},
            {"description", "List suggestions awaiting outcome feedback"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default: 20)"}}}
                }}
            }}
        });
        handlers_["suggestion_pending"] = [this](const json& p) { return tool_suggestion_pending(p); };

        tools_.push_back({
            {"name", "suggestion_resolve"},
            {"description", "Record the outcome of a suggestion (did it help?)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Suggestion ID"}}},
                    {"helped", {{"type", "boolean"}, {"description", "Did the suggestion help?"}}},
                    {"details", {{"type", "string"}, {"description", "What happened"}}}
                }},
                {"required", {"id", "helped"}}
            }}
        });
        handlers_["suggestion_resolve"] = [this](const json& p) { return tool_suggestion_resolve(p); };

        tools_.push_back({
            {"name", "suggestion_count"},
            {"description", "Count pending suggestions awaiting feedback"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}}
                }}
            }}
        });
        handlers_["suggestion_count"] = [this](const json& p) { return tool_suggestion_count(p); };

        // Memory consolidation
        tools_.push_back({
            {"name", "consolidation_scan"},
            {"description", "Find similar memory pairs that could be merged"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"similarity_threshold", {{"type", "number"}, {"description", "Min similarity (0-1, default: 0.85)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max candidates (default: 20)"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}}
                }}
            }}
        });
        handlers_["consolidation_scan"] = [this](const json& p) { return tool_consolidation_scan(p); };

        tools_.push_back({
            {"name", "consolidation_merge"},
            {"description", "Merge two similar memories (primary absorbs secondary)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"primary_id", {{"type", "integer"}, {"description", "ID of primary memory (kept)"}}},
                    {"secondary_id", {{"type", "integer"}, {"description", "ID of secondary memory (absorbed)"}}},
                    {"merged_content", {{"type", "string"}, {"description", "Optional combined content"}}}
                }},
                {"required", {"primary_id", "secondary_id"}}
            }}
        });
        handlers_["consolidation_merge"] = [this](const json& p) { return tool_consolidation_merge(p); };

        tools_.push_back({
            {"name", "consolidation_auto"},
            {"description", "Auto-merge highly similar memories (>90% similarity)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"similarity_threshold", {{"type", "number"}, {"description", "Min similarity (default: 0.90)"}}},
                    {"max_merges", {{"type", "integer"}, {"description", "Max merges to perform (default: 20)"}}}
                }}
            }}
        });
        handlers_["consolidation_auto"] = [this](const json& p) { return tool_consolidation_auto(p); };

        // Meta-cognition (self-reflection on learning)
        tools_.push_back({
            {"name", "metacognition_corrections"},
            {"description", "Analyze patterns in past corrections. Finds recurring mistakes."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"limit", {{"type", "integer"}, {"description", "Max corrections to analyze (default: 50)"}}}
                }}
            }}
        });
        handlers_["metacognition_corrections"] = [this](const json& p) { return tool_metacognition_corrections(p); };

        tools_.push_back({
            {"name", "metacognition_outcomes"},
            {"description", "Analyze suggestion outcomes (what worked vs failed). Finds success patterns."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"limit", {{"type", "integer"}, {"description", "Max outcomes to analyze (default: 50)"}}}
                }}
            }}
        });
        handlers_["metacognition_outcomes"] = [this](const json& p) { return tool_metacognition_outcomes(p); };

        tools_.push_back({
            {"name", "metacognition_evaluate"},
            {"description", "Self-evaluate learning effectiveness. Returns metrics and recommendations."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });
        handlers_["metacognition_evaluate"] = [this](const json& p) { return tool_metacognition_evaluate(p); };

        // Curiosity (knowledge gaps)
        tools_.push_back({
            {"name", "curiosity_note_gap"},
            {"description", "Record a knowledge gap - something I don't know or couldn't answer"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"gap", {{"type", "string"}, {"description", "What I don't know"}}},
                    {"context", {{"type", "string"}, {"description", "When/why this came up"}}},
                    {"realm", {{"type", "string"}, {"description", "Project scope"}}}
                }},
                {"required", {"gap"}}
            }}
        });
        handlers_["curiosity_note_gap"] = [this](const json& p) { return tool_curiosity_note_gap(p); };

        tools_.push_back({
            {"name", "curiosity_gaps"},
            {"description", "List current knowledge gaps awaiting exploration"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"limit", {{"type", "integer"}, {"description", "Max gaps (default: 20)"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}}
                }}
            }}
        });
        handlers_["curiosity_gaps"] = [this](const json& p) { return tool_curiosity_gaps(p); };

        tools_.push_back({
            {"name", "curiosity_resolve"},
            {"description", "Mark a gap as explored/resolved"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Gap memory ID"}}},
                    {"learned", {{"type", "string"}, {"description", "What was learned"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["curiosity_resolve"] = [this](const json& p) { return tool_curiosity_resolve(p); };

        // Transcript tools (for distillation)
        tools_.push_back({
            {"name", "transcript_register"},
            {"description", "Register a transcript file for distillation tracking"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Claude session ID"}}},
                    {"transcript_path", {{"type", "string"}, {"description", "Path to .jsonl transcript file"}}},
                    {"realm", {{"type", "string"}, {"description", "Project/realm isolation (default: 'default')"}}}
                }},
                {"required", {"session_id", "transcript_path"}}
            }}
        });
        handlers_["transcript_register"] = [this](const json& p) { return tool_transcript_register(p); };

        tools_.push_back({
            {"name", "transcript_get"},
            {"description", "Get transcript state for a session"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session ID to look up"}}}
                }},
                {"required", {"session_id"}}
            }}
        });
        handlers_["transcript_get"] = [this](const json& p) { return tool_transcript_get(p); };

        tools_.push_back({
            {"name", "transcript_list"},
            {"description", "List all registered transcripts"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {}}
            }}
        });
        handlers_["transcript_list"] = [this](const json& p) { return tool_transcript_list(p); };

        tools_.push_back({
            {"name", "transcript_update"},
            {"description", "Update transcript processing progress"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session ID"}}},
                    {"last_line", {{"type", "integer"}, {"description", "Last processed line number"}}}
                }},
                {"required", {"session_id", "last_line"}}
            }}
        });
        handlers_["transcript_update"] = [this](const json& p) { return tool_transcript_update(p); };

        tools_.push_back({
            {"name", "transcript_remove"},
            {"description", "Remove transcript from tracking"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session ID to remove"}}}
                }},
                {"required", {"session_id"}}
            }}
        });
        handlers_["transcript_remove"] = [this](const json& p) { return tool_transcript_remove(p); };

        tools_.push_back({
            {"name", "transcript_parse"},
            {"description", "Parse new turns from a transcript JSONL file"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session ID to parse"}}},
                    {"min_turns", {{"type", "integer"}, {"description", "Minimum turns to return (default: 4)"}}}
                }},
                {"required", {"session_id"}}
            }}
        });
        handlers_["transcript_parse"] = [this](const json& p) { return tool_transcript_parse(p); };

        tools_.push_back({
            {"name", "transcript_search"},
            {"description", "Semantic search across transcript content. Defaults to current session. Use keyword_only=true for fast search."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Search query"}}},
                    {"session_id", {{"type", "string"}, {"description", "Session to search (default: current, '*' for all)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default: 10)"}}},
                    {"min_similarity", {{"type", "number"}, {"description", "Minimum cosine similarity 0-1 (default: 0.3)"}}},
                    {"keyword_only", {{"type", "boolean"}, {"description", "Fast keyword match without embeddings (default: false)"}}}
                }},
                {"required", {"query"}}
            }}
        });
        handlers_["transcript_search"] = [this](const json& p) { return tool_transcript_search(p); };

        tools_.push_back({
            {"name", "distill_status"},
            {"description", "Get distillation system status: transcripts, realms, pending work"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {}}
            }}
        });
        handlers_["distill_status"] = [this](const json& p) { return tool_distill_status(p); };

        // Epiplexity tools
        tools_.push_back({
            {"name", "epiplexity_check"},
            {"description", "Compute epiplexity (ε) score for a seed - measures reconstruction quality"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"original", {{"type", "string"}, {"description", "Original full text"}}},
                    {"seed", {{"type", "string"}, {"description", "Compressed SSL seed"}}},
                    {"reconstructed", {{"type", "string"}, {"description", "Text reconstructed from seed"}}}
                }},
                {"required", {"original", "seed", "reconstructed"}}
            }}
        });
        handlers_["epiplexity_check"] = [this](const json& p) { return tool_epiplexity_check(p); };

        // Anticipation: context→action pattern learning
        tools_.push_back({
            {"name", "anticipation_observe"},
            {"description", "Record a context→action pattern. Called when an action is taken in response to a context."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"context", {{"type", "string"}, {"description", "What triggered the action (situation/state)"}}},
                    {"action", {{"type", "string"}, {"description", "What was done in response"}}},
                    {"realm", {{"type", "string"}, {"description", "Project scope (default: brahman)"}}}
                }},
                {"required", {"context", "action"}}
            }}
        });
        handlers_["anticipation_observe"] = [this](const json& p) { return tool_anticipation_observe(p); };

        tools_.push_back({
            {"name", "anticipation_predict"},
            {"description", "Given a context, predict likely actions based on learned patterns."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"context", {{"type", "string"}, {"description", "Current situation/context"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max predictions (default: 5)"}}},
                    {"min_confidence", {{"type", "number"}, {"description", "Minimum pattern confidence (default: 0.3)"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}}
                }},
                {"required", {"context"}}
            }}
        });
        handlers_["anticipation_predict"] = [this](const json& p) { return tool_anticipation_predict(p); };

        tools_.push_back({
            {"name", "anticipation_success"},
            {"description", "Mark a predicted action as successful - the prediction was correct and helpful."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Pattern ID to mark successful"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["anticipation_success"] = [this](const json& p) { return tool_anticipation_success(p); };

        tools_.push_back({
            {"name", "anticipation_list"},
            {"description", "List learned anticipation patterns, ordered by frequency."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max patterns (default: 20)"}}},
                    {"sort_by", {{"type", "string"}, {"description", "Sort by: frequency, confidence, created_at (default: frequency)"}}}
                }}
            }}
        });
        handlers_["anticipation_list"] = [this](const json& p) { return tool_anticipation_list(p); };

        // Habit Formation: repeated patterns that strengthen
        tools_.push_back({
            {"name", "habit_observe"},
            {"description", "Record a trigger→response pattern. Each observation strengthens the habit."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"trigger", {{"type", "string"}, {"description", "What triggers the habit"}}},
                    {"response", {{"type", "string"}, {"description", "What should happen when triggered"}}},
                    {"realm", {{"type", "string"}, {"description", "Project scope (default: brahman)"}}}
                }},
                {"required", {"trigger", "response"}}
            }}
        });
        handlers_["habit_observe"] = [this](const json& p) { return tool_habit_observe(p); };

        tools_.push_back({
            {"name", "habit_match"},
            {"description", "Find habits that match the given context."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"context", {{"type", "string"}, {"description", "Current context to match against"}}},
                    {"min_strength", {{"type", "number"}, {"description", "Minimum habit strength 0-1 (default: 0.3)"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}}
                }},
                {"required", {"context"}}
            }}
        });
        handlers_["habit_match"] = [this](const json& p) { return tool_habit_match(p); };

        tools_.push_back({
            {"name", "habit_strengthen"},
            {"description", "Manually strengthen a habit."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Habit ID"}}},
                    {"amount", {{"type", "number"}, {"description", "Amount to strengthen (default: 0.1)"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["habit_strengthen"] = [this](const json& p) { return tool_habit_strengthen(p); };

        tools_.push_back({
            {"name", "habit_weaken"},
            {"description", "Weaken a habit (negative feedback)."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Habit ID"}}},
                    {"amount", {{"type", "number"}, {"description", "Amount to weaken (default: 0.05)"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["habit_weaken"] = [this](const json& p) { return tool_habit_weaken(p); };

        tools_.push_back({
            {"name", "habit_list"},
            {"description", "List formed habits, ordered by strength."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}},
                    {"min_strength", {{"type", "number"}, {"description", "Minimum strength (default: 0)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max habits (default: 20)"}}},
                    {"sort_by", {{"type", "string"}, {"description", "Sort by: strength, frequency, created_at (default: strength)"}}}
                }}
            }}
        });
        handlers_["habit_list"] = [this](const json& p) { return tool_habit_list(p); };

        // Background Processing: daemon-level tasks
        tools_.push_back({
            {"name", "background_schedule"},
            {"description", "Schedule a background task (consolidation, decay, pruning, pattern_extraction)."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"task_type", {{"type", "string"}, {"description", "Type: consolidation, decay, pruning, pattern_extraction"}}},
                    {"realm", {{"type", "string"}, {"description", "Project scope (default: brahman)"}}}
                }},
                {"required", {"task_type"}}
            }}
        });
        handlers_["background_schedule"] = [this](const json& p) { return tool_background_schedule(p); };

        tools_.push_back({
            {"name", "background_status"},
            {"description", "Get status of background processing (pending, running, completed today)."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {}}
            }}
        });
        handlers_["background_status"] = [this](const json& p) { return tool_background_status(p); };

        tools_.push_back({
            {"name", "background_run_cycle"},
            {"description", "Run one cycle of background processing. Processes pending tasks."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {}}
            }}
        });
        handlers_["background_run_cycle"] = [this](const json& p) { return tool_background_run_cycle(p); };

        // User Profile: structured understanding of partner
        tools_.push_back({
            {"name", "profile_get"},
            {"description", "Get user profile - expertise, communication style, work patterns, preferences."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"user_id", {{"type", "string"}, {"description", "User ID (default: 'default')"}}}
                }}
            }}
        });
        handlers_["profile_get"] = [this](const json& p) { return tool_profile_get(p); };

        tools_.push_back({
            {"name", "profile_update"},
            {"description", "Update a specific profile field (expertise_json, style_json, patterns_json, preferences_json)."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"user_id", {{"type", "string"}, {"description", "User ID (default: 'default')"}}},
                    {"field", {{"type", "string"}, {"description", "Field to update: expertise_json, style_json, patterns_json, preferences_json"}}},
                    {"value", {{"type", "string"}, {"description", "JSON value for the field"}}}
                }},
                {"required", {"field", "value"}}
            }}
        });
        handlers_["profile_update"] = [this](const json& p) { return tool_profile_update(p); };

        tools_.push_back({
            {"name", "profile_observe"},
            {"description", "Record an observation about the user (expertise, style, pattern, preference)."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"observation_type", {{"type", "string"}, {"description", "Type: expertise, style, pattern, preference"}}},
                    {"value", {{"type", "string"}, {"description", "For expertise: 'domain:level'. For others: JSON object"}}},
                    {"user_id", {{"type", "string"}, {"description", "User ID (default: 'default')"}}}
                }},
                {"required", {"observation_type", "value"}}
            }}
        });
        handlers_["profile_observe"] = [this](const json& p) { return tool_profile_observe(p); };

        // Long-term Goals: objectives spanning weeks/months
        tools_.push_back({
            {"name", "goal_set"},
            {"description", "Define a new long-term goal with optional milestones and deadline."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"title", {{"type", "string"}, {"description", "Short goal name (e.g., 'Ship v4.0')"}}},
                    {"description", {{"type", "string"}, {"description", "Detailed description"}}},
                    {"milestones", {{"type", "string"}, {"description", "JSON array: [{\"name\":\"v1\",\"done\":false}, ...]"}}},
                    {"deadline", {{"type", "integer"}, {"description", "Unix timestamp deadline (optional)"}}},
                    {"realm", {{"type", "string"}, {"description", "Project scope (default: brahman)"}}}
                }},
                {"required", {"title"}}
            }}
        });
        handlers_["goal_set"] = [this](const json& p) { return tool_goal_set(p); };

        tools_.push_back({
            {"name", "goal_get"},
            {"description", "Get details of a specific goal by ID."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Goal ID"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["goal_get"] = [this](const json& p) { return tool_goal_get(p); };

        tools_.push_back({
            {"name", "goal_list"},
            {"description", "List goals, optionally filtered by status and realm."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"status", {{"type", "string"}, {"description", "Filter: active, paused, completed, abandoned (default: active)"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default: 20)"}}},
                    {"sort_by", {{"type", "string"}, {"description", "Sort by: progress, created_at, updated_at (default: updated_at)"}}}
                }}
            }}
        });
        handlers_["goal_list"] = [this](const json& p) { return tool_goal_list(p); };

        tools_.push_back({
            {"name", "goal_progress"},
            {"description", "Update goal progress (0-1) and optionally mark a milestone complete."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Goal ID"}}},
                    {"progress", {{"type", "number"}, {"description", "Progress 0-1 (e.g., 0.5 = 50%)"}}},
                    {"milestone", {{"type", "string"}, {"description", "Milestone name to mark complete (optional)"}}}
                }},
                {"required", {"id", "progress"}}
            }}
        });
        handlers_["goal_progress"] = [this](const json& p) { return tool_goal_progress(p); };

        tools_.push_back({
            {"name", "goal_complete"},
            {"description", "Mark a goal as completed with an outcome summary."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Goal ID"}}},
                    {"outcome", {{"type", "string"}, {"description", "Summary of what was achieved"}}}
                }},
                {"required", {"id", "outcome"}}
            }}
        });
        handlers_["goal_complete"] = [this](const json& p) { return tool_goal_complete(p); };

        // Confidence Calibration: tracking prediction accuracy
        tools_.push_back({
            {"name", "calibration_record"},
            {"description", "Record a prediction outcome (success/failure) for a domain to track accuracy."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"domain", {{"type", "string"}, {"description", "Domain (e.g., 'code', 'architecture', 'debugging')"}}},
                    {"success", {{"type", "boolean"}, {"description", "Was the prediction correct?"}}}
                }},
                {"required", {"domain", "success"}}
            }}
        });
        handlers_["calibration_record"] = [this](const json& p) { return tool_calibration_record(p); };

        tools_.push_back({
            {"name", "calibration_score"},
            {"description", "Get accuracy score for a domain or all domains."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"domain", {{"type", "string"}, {"description", "Specific domain (optional - omit for all)"}}}
                }}
            }}
        });
        handlers_["calibration_score"] = [this](const json& p) { return tool_calibration_score(p); };

        // Memory Hygiene: keep memory bounded and healthy
        tools_.push_back({
            {"name", "hygiene_stats"},
            {"description", "Get memory hygiene statistics - confidence distribution, growth rate, stale memories."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {}}
            }}
        });
        handlers_["hygiene_stats"] = [this](const json& p) { return tool_hygiene_stats(p); };

        tools_.push_back({
            {"name", "hygiene_run"},
            {"description", "Run memory hygiene: decay, prune low-confidence old memories, consolidate similar."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"prune_threshold", {{"type", "number"}, {"description", "Confidence below which to prune (default: 0.1)"}}},
                    {"min_age_days", {{"type", "number"}, {"description", "Minimum age in days for pruning (default: 7)"}}},
                    {"consolidation_threshold", {{"type", "number"}, {"description", "Similarity threshold for consolidation (default: 0.85)"}}},
                    {"max_consolidations", {{"type", "integer"}, {"description", "Max consolidations per run (default: 10)"}}}
                }}
            }}
        });
        handlers_["hygiene_run"] = [this](const json& p) { return tool_hygiene_run(p); };

        tools_.push_back({
            {"name", "rebuild_fts_index"},
            {"description", "Rebuild FTS index for BM25 keyword search on memory. Call this if keyword searches return no results."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });
        handlers_["rebuild_fts_index"] = [this](const json& p) { return tool_rebuild_fts_index(p); };

        // ═══════════════════════════════════════════════════════════════════
        // xMemory Theme System: Hierarchical Memory Organization
        // ═══════════════════════════════════════════════════════════════════

        tools_.push_back({
            {"name", "theme_list"},
            {"description", "List all themes with statistics"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max themes to return (default: 20)"}}}
                }}
            }}
        });
        handlers_["theme_list"] = [this](const json& p) { return tool_theme_list(p); };

        tools_.push_back({
            {"name", "theme_get"},
            {"description", "Get theme details including representatives"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Theme ID"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["theme_get"] = [this](const json& p) { return tool_theme_get(p); };

        tools_.push_back({
            {"name", "theme_recall"},
            {"description", "Two-stage theme-based retrieval (xMemory): diverse representatives then adaptive expansion"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Search query"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default: 10)"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}}
                }},
                {"required", {"query"}}
            }}
        });
        handlers_["theme_recall"] = [this](const json& p) { return tool_theme_recall(p); };

        tools_.push_back({
            {"name", "theme_stats"},
            {"description", "Get theme organization statistics"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}}
                }}
            }}
        });
        handlers_["theme_stats"] = [this](const json& p) { return tool_theme_stats(p); };

        tools_.push_back({
            {"name", "theme_maintain"},
            {"description", "Force theme maintenance cycle: split oversized, merge similar, reassign memories"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}}
                }}
            }}
        });
        handlers_["theme_maintain"] = [this](const json& p) { return tool_theme_maintain(p); };

        // Batch assign orphan memories to themes
        tools_.push_back({
            {"name", "theme_assign_orphans"},
            {"description", "Assign orphan memories (not in any theme) to themes in batches"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"batch_size", {{"type", "integer"}, {"description", "Memories per batch (default: 100)"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}}
                }}
            }}
        });
        handlers_["theme_assign_orphans"] = [this](const json& p) { return tool_theme_assign_orphans(p); };

        // Usage outcome tracking: did surfaced memories help?
        tools_.push_back({
            {"name", "learn_outcome"},
            {"description", "Record whether a previously surfaced memory helped or hurt. Adjusts confidence based on outcome."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"memory_id", {{"type", "string"}, {"description", "Memory ID or UUID that was surfaced"}}},
                    {"outcome", {{"type", "string"}, {"enum", {"positive", "negative", "neutral"}}, {"description", "Did it help?"}}},
                    {"context", {{"type", "string"}, {"description", "What you were trying to do when this memory was surfaced"}}}
                }},
                {"required", {"memory_id", "outcome"}}
            }}
        });
        handlers_["learn_outcome"] = [this](const json& p) { return tool_learn_outcome(p); };

        // SUS Phase 1: Log memory exposure
        tools_.push_back({
            {"name", "log_exposure"},
            {"description", "Log that memories were exposed to Claude via hooks (SUS metrics)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}}},
                    {"turn_id", {{"type", "integer"}}},
                    {"hook_type", {{"type", "string"}, {"description", "session_start or user_prompt"}}},
                    {"memory_ids", {{"type", "array"}, {"items", {{"type", "integer"}}}}},
                    {"ranks", {{"type", "array"}, {"items", {{"type", "integer"}}}}},
                    {"resonance_scores", {{"type", "array"}, {"items", {{"type", "number"}}}}}
                }},
                {"required", {"session_id", "turn_id", "hook_type", "memory_ids"}}
            }}
        });
        handlers_["log_exposure"] = [this](const json& p) { return tool_log_exposure(p); };

        // SUS Phase 2: Get composite metrics
        tools_.push_back({
            {"name", "get_sus_metrics"},
            {"description", "Get Soul Utility Score (SUS) metrics: R(relevance), P(precision), D(durability) and composite score"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"days", {{"type", "integer"}, {"description", "Lookback window in days (default: 7)"}}}
                }}
            }}
        });
        handlers_["get_sus_metrics"] = [this](const json& p) { return tool_get_sus_metrics(p); };

        // Episode auto-distillation status (clusters of similar episodes for wisdom extraction)
        tools_.push_back({
            {"name", "episode_cluster_status"},
            {"description", "Find clusters of similar episodes that could be distilled into wisdom."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"similarity_threshold", {{"type", "number"}, {"description", "Minimum similarity for cluster (default: 0.85)"}}},
                    {"min_occurrences", {{"type", "integer"}, {"description", "Minimum episodes in cluster (default: 3)"}}}
                }}
            }}
        });
        handlers_["episode_cluster_status"] = [this](const json& p) { return tool_episode_cluster_status(p); };

        tools_.push_back({
            {"name", "restore_code_intel_confidence"},
            {"description", "Restore confidence and fix decay_rate for code intel memories (symbol, projectessence, modulestate, patternstate). Run this once to fix memories that were incorrectly decayed."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"confidence", {{"type", "number"}, {"description", "Confidence to restore (default: 0.8)"}}},
                    {"dry_run", {{"type", "boolean"}, {"description", "Preview changes without applying (default: false)"}}}
                }}
            }}
        });
        handlers_["restore_code_intel_confidence"] = [this](const json& p) { return tool_restore_code_intel_confidence(p); };

        // SQL query tool for advanced debugging and analysis
        tools_.push_back({
            {"name", "sql_query"},
            {"description", "Execute a read-only SQL query against the soul database. Use for debugging, analysis, and complex queries."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "SQL query to execute (SELECT only)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max rows to return (default: 20)"}}}
                }},
                {"required", {"query"}}
            }}
        });
        handlers_["sql_query"] = [this](const json& p) { return tool_sql_query(p); };

        // Cross-Project Learning: transfer insights across realms
        tools_.push_back({
            {"name", "insight_promote"},
            {"description", "Promote a memory to global visibility so it applies across all projects."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Memory ID to promote"}}},
                    {"reason", {{"type", "string"}, {"description", "Why this insight is cross-project"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["insight_promote"] = [this](const json& p) { return tool_insight_promote(p); };

        tools_.push_back({
            {"name", "insight_global"},
            {"description", "List all global insights that apply across projects."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"limit", {{"type", "integer"}, {"description", "Max results (default: 20)"}}},
                    {"tag", {{"type", "string"}, {"description", "Filter by tag (optional)"}}}
                }}
            }}
        });
        handlers_["insight_global"] = [this](const json& p) { return tool_insight_global(p); };

        // Aspect-based memory access
        tools_.push_back({
            {"name", "list_by_aspect"},
            {"description", "List memories filtered by semantic aspect. Aspects group related node kinds (e.g., 'preferences' returns preference memories, 'wisdom' returns wisdom+insight)."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"aspect", {{"type", "string"}, {"description", "Semantic aspect: preferences, corrections, insights, failures, decisions, approaches, milestones, goals, habits, beliefs, wisdom, code, gaps"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default: 30)"}}},
                    {"min_confidence", {{"type", "number"}, {"description", "Minimum confidence threshold (default: 0.1)"}}}
                }},
                {"required", {"aspect"}}
            }}
        });
        handlers_["list_by_aspect"] = [this](const json& p) { return tool_list_by_aspect(p); };

        tools_.push_back({
            {"name", "list_aspects"},
            {"description", "List all available semantic aspects and the node kinds they include."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {}}
            }}
        });
        handlers_["list_aspects"] = [this](const json& p) { return tool_list_aspects(p); };

        // ═══════════════════════════════════════════════════════════════════════
        // Memory Index: Fast pre-retrieval scanning (ClawVault-inspired)
        // ═══════════════════════════════════════════════════════════════════════

        tools_.push_back({
            {"name", "list_memories_brief"},
            {"description", "Fast memory index: returns id, kind, priority, date, and one-liner (first 80 chars). Use as fast path before expensive retrieval - scan what exists, then fetch full content for relevant entries."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"limit", {{"type", "integer"}, {"description", "Max entries (default: 200)"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}},
                    {"kind", {{"type", "string"}, {"description", "Filter by memory kind"}}},
                    {"priority_tier", {{"type", "integer"}, {"description", "Filter by tier: 0=background, 1=notable, 2=critical"}}}
                }}
            }}
        });
        handlers_["list_memories_brief"] = [this](const json& p) { return tool_list_memories_brief(p); };

        tools_.push_back({
            {"name", "set_priority_tier"},
            {"description", "Set memory priority tier. Tiers: 0=background (🟢), 1=notable (🟡), 2=critical (🔴). Critical memories always load first in budget-aware recall."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"memory_id", {{"type", "integer"}, {"description", "Memory ID"}}},
                    {"tier", {{"type", "integer"}, {"description", "Priority tier: 0=background, 1=notable, 2=critical"}}}
                }},
                {"required", {"memory_id", "tier"}}
            }}
        });
        handlers_["set_priority_tier"] = [this](const json& p) { return tool_set_priority_tier(p); };

        tools_.push_back({
            {"name", "recall_by_priority"},
            {"description", "Budget-aware recall: fills critical (🔴) first, then notable (🟡), then background (🟢). Respects token budget. Use when context window is limited."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Search query for semantic filtering"}}},
                    {"budget_tokens", {{"type", "integer"}, {"description", "Token budget (default: 4000)"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}},
                    {"include_global", {{"type", "boolean"}, {"description", "Include global memories (default: true)"}}}
                }}
            }}
        });
        handlers_["recall_by_priority"] = [this](const json& p) { return tool_recall_by_priority(p); };

        // ═══════════════════════════════════════════════════════════════════════
        // Memory Type Taxonomy (formalized aspect system)
        // ═══════════════════════════════════════════════════════════════════════

        tools_.push_back({
            {"name", "set_memory_type"},
            {"description", "Set the semantic type (kind) of a memory. Types: decision, preference, correction, insight, milestone, approach, habit, belief, gap, wisdom, episode."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"memory_id", {{"type", "integer"}, {"description", "Memory ID"}}},
                    {"type", {{"type", "string"}, {"description", "Memory type: decision, preference, correction, insight, milestone, approach, habit, belief, gap, wisdom, episode"}}}
                }},
                {"required", {"memory_id", "type"}}
            }}
        });
        handlers_["set_memory_type"] = [this](const json& p) { return tool_set_memory_type(p); };

        tools_.push_back({
            {"name", "memory_type_stats"},
            {"description", "Get statistics on memory types: counts by kind and priority tier."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}}
                }}
            }}
        });
        handlers_["memory_type_stats"] = [this](const json& p) { return tool_memory_type_stats(p); };

        // Smart recall: unified query intent classification and routing with hierarchical expansion
        tools_.push_back({
            {"name", "smart_recall"},
            {"description", "Intelligent memory recall with hierarchical expansion. Classifies query intent, routes to optimal retrieval, and auto-expands top results to full conversation context. Single entry point for finding the right memory."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Natural language query (e.g., 'what happened last week', 'show preferences', 'what connects X and Y')"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default: 20)"}}},
                    {"expand_top", {{"type", "integer"}, {"description", "Auto-expand top N results to full context: SSL→episode→turns (default: 2, 0=disable)"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm (empty = all realms)"}}},
                    {"include_global", {{"type", "boolean"}, {"description", "Include global memories (default: true)"}}}
                }},
                {"required", {"query"}}
            }}
        });
        handlers_["smart_recall"] = [this](const json& p) { return tool_smart_recall(p); };

        // SSL conversion tool
        tools_.push_back({
            {"name", "ssl_convert"},
            {"description", "Convert raw text to SSL (Soul Semantic Language) format. Use before remember for non-SSL content."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"content", {{"type", "string"}, {"description", "Raw text to convert"}}},
                    {"domain", {{"type", "string"}, {"description", "Domain tag (e.g., 'cc-soul', 'partnership')"}}},
                    {"location", {{"type", "string"}, {"description", "Optional location reference (@file:line)"}}}
                }},
                {"required", {"content"}}
            }}
        });
        handlers_["ssl_convert"] = [this](const json& p) { return tool_ssl_convert(p); };

        // ======================================================================
        // Narrative and Anticipation Tools
        // ======================================================================

        // narrative_status - Get current work mode and segment summary
        tools_.push_back({
            {"name", "narrative_status"},
            {"description", "Get current work mode, confidence, and segment summary for the session"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session ID (default: current)"}}}
                }},
                {"required", json::array()}
            }}
        });
        handlers_["narrative_status"] = [this](const json& p) { return tool_narrative_status(p); };

        // narrative_log - Manually append event to session log
        tools_.push_back({
            {"name", "narrative_log"},
            {"description", "Manually append an event to the session event log"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session ID"}}},
                    {"kind", {{"type", "string"}, {"description", "Event kind: user_message, assistant_message, tool_use, tool_result, error, file_edit, search, build, test, commit, mode_change"}}},
                    {"summary", {{"type", "string"}, {"description", "Brief description of the event"}}},
                    {"tool_name", {{"type", "string"}, {"description", "Tool name (for tool events)"}}},
                    {"success", {{"type", "boolean"}, {"description", "Whether the action succeeded"}}},
                    {"payload", {{"type", "string"}, {"description", "JSON payload with event details"}}},
                    {"files_mentioned", {{"type", "string"}, {"description", "JSON array of file paths"}}}
                }},
                {"required", {"kind", "summary"}}
            }}
        });
        handlers_["narrative_log"] = [this](const json& p) { return tool_narrative_log(p); };

        // narrative_history - Get state segment history
        tools_.push_back({
            {"name", "narrative_history"},
            {"description", "Get history of work mode segments for a session"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session ID"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max segments to return (default 20)"}}}
                }},
                {"required", json::array()}
            }}
        });
        handlers_["narrative_history"] = [this](const json& p) { return tool_narrative_history(p); };

        // anticipation_filter - Get filtered predictions that pass annoyance gate
        tools_.push_back({
            {"name", "anticipation_filter"},
            {"description", "Get anticipation candidates that pass the annoyance gate"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session ID"}}},
                    {"max", {{"type", "integer"}, {"description", "Max predictions to return (default 2)"}}}
                }},
                {"required", json::array()}
            }}
        });
        handlers_["anticipation_filter"] = [this](const json& p) { return tool_anticipation_filter(p); };

        // anticipation_gate_status - Show annoyance gate state
        tools_.push_back({
            {"name", "anticipation_gate_status"},
            {"description", "Show the current annoyance gate state for debugging"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session ID"}}}
                }},
                {"required", json::array()}
            }}
        });
        handlers_["anticipation_gate_status"] = [this](const json& p) { return tool_anticipation_gate_status(p); };

        // anticipation_record_outcome - Record outcome of a prediction
        tools_.push_back({
            {"name", "anticipation_record_outcome"},
            {"description", "Record whether a surfaced prediction was correct or incorrect"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"candidate_id", {{"type", "integer"}, {"description", "Anticipation candidate ID"}}},
                    {"correct", {{"type", "boolean"}, {"description", "Whether the prediction was correct"}}}
                }},
                {"required", {"candidate_id", "correct"}}
            }}
        });
        handlers_["anticipation_record_outcome"] = [this](const json& p) { return tool_anticipation_record_outcome(p); };

        // ========================================================================
        // Cross-Session Messaging Tools
        // ========================================================================

        tools_.push_back({
            {"name", "msg_send"},
            {"description", "Send a message to another session, a realm, or all sessions"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"target", {{"type", "string"}, {"description", "Target: session_id, realm name (e.g., project:cc-soul), or '*' for global"}}},
                    {"content", {{"type", "string"}, {"description", "Message content"}}},
                    {"target_type", {{"type", "string"}, {"description", "direct|realm|global (auto-detected if omitted)"}}},
                    {"priority", {{"type", "integer"}, {"description", "0=info, 1=normal, 2=important, 3=urgent"}}},
                    {"content_type", {{"type", "string"}, {"description", "text|json|ssl (default: text)"}}},
                    {"ttl", {{"type", "integer"}, {"description", "TTL in seconds (default: 3600, 0 = no expiry)"}}},
                    {"session_id", {{"type", "string"}, {"description", "Sender session ID (default: from env)"}}}
                }},
                {"required", {"target", "content"}}
            }}
        });
        handlers_["msg_send"] = [this](const json& p) { return tool_msg_send(p); };

        tools_.push_back({
            {"name", "msg_inbox"},
            {"description", "Check unread cross-session messages"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session ID (default: from env)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max messages (default: 20)"}}},
                    {"min_priority", {{"type", "integer"}, {"description", "Minimum priority level (default: 0)"}}},
                    {"auto_ack", {{"type", "boolean"}, {"description", "Auto-acknowledge returned messages (default: false)"}}}
                }}
            }}
        });
        handlers_["msg_inbox"] = [this](const json& p) { return tool_msg_inbox(p); };

        tools_.push_back({
            {"name", "msg_ack"},
            {"description", "Acknowledge a specific message as read"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"message_id", {{"type", "integer"}, {"description", "Message ID to acknowledge"}}},
                    {"session_id", {{"type", "string"}, {"description", "Session ID (default: from env)"}}}
                }},
                {"required", {"message_id"}}
            }}
        });
        handlers_["msg_ack"] = [this](const json& p) { return tool_msg_ack(p); };

        tools_.push_back({
            {"name", "msg_ack_all"},
            {"description", "Acknowledge all unread messages"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session ID (default: from env)"}}}
                }}
            }}
        });
        handlers_["msg_ack_all"] = [this](const json& p) { return tool_msg_ack_all(p); };

        tools_.push_back({
            {"name", "msg_history"},
            {"description", "Get message history for the session"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session ID (default: from env)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max messages (default: 30)"}}}
                }}
            }}
        });
        handlers_["msg_history"] = [this](const json& p) { return tool_msg_history(p); };

        tools_.push_back({
            {"name", "session_register"},
            {"description", "Register this session in the session registry"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session ID (default: from env)"}}},
                    {"realm", {{"type", "string"}, {"description", "Session realm (default: auto-detect)"}}},
                    {"pid", {{"type", "integer"}, {"description", "Process ID (default: caller's PID)"}}},
                    {"transcript_path", {{"type", "string"}, {"description", "Path to transcript .jsonl file"}}},
                    {"project_dir", {{"type", "string"}, {"description", "Project working directory"}}},
                    {"metadata", {{"type", "string"}, {"description", "JSON metadata about the session"}}}
                }}
            }}
        });
        handlers_["session_register"] = [this](const json& p) { return tool_session_register(p); };

        tools_.push_back({
            {"name", "session_heartbeat"},
            {"description", "Send heartbeat to keep session active"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session ID (default: from env)"}}},
                    {"metadata", {{"type", "string"}, {"description", "Optional metadata update"}}}
                }}
            }}
        });
        handlers_["session_heartbeat"] = [this](const json& p) { return tool_session_heartbeat(p); };

        tools_.push_back({
            {"name", "session_list"},
            {"description", "List active sessions"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"realm", {{"type", "string"}, {"description", "Filter by realm (empty = all)"}}},
                    {"status", {{"type", "string"}, {"description", "Filter by status: active|idle|dead (default: active)"}}}
                }}
            }}
        });
        handlers_["session_list"] = [this](const json& p) { return tool_session_list(p); };

        tools_.push_back({
            {"name", "session_deregister"},
            {"description", "Deregister this session from the registry"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session ID (default: from env)"}}}
                }}
            }}
        });
        handlers_["session_deregister"] = [this](const json& p) { return tool_session_deregister(p); };

        tools_.push_back({
            {"name", "session_sync"},
            {"description", "Sync session registry with running Claude processes and transcript files. Discovers new sessions, updates existing, and marks dead sessions."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"projects_dir", {{"type", "string"}, {"description", "Claude projects directory (default: ~/.claude/projects)"}}}
                }}
            }}
        });
        handlers_["session_sync"] = [this](const json& p) { return tool_session_sync(p); };

        tools_.push_back({
            {"name", "read_transcript"},
            {"description", "Read JSONL transcript file directly with pagination. Use for exploring conversations without loading into memory. Returns metadata and paginated turns."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Path to JSONL transcript file"}}},
                    {"session_id", {{"type", "string"}, {"description", "Session ID (auto-finds transcript if path not provided)"}}},
                    {"start_turn", {{"type", "integer"}, {"description", "Starting turn index (default: 0)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max turns to return (default: 20)"}}},
                    {"max_chars_per_turn", {{"type", "integer"}, {"description", "Truncate turn content (default: 500, 0=full)"}}},
                    {"role_filter", {{"type", "string"}, {"description", "Filter by role: user, assistant, or empty for all"}}},
                    {"keyword", {{"type", "string"}, {"description", "Filter turns containing this keyword"}}},
                    {"metadata_only", {{"type", "boolean"}, {"description", "Return only file metadata (turn count, size) without content"}}}
                }}
            }}
        });
        handlers_["read_transcript"] = [this](const json& p) { return tool_read_transcript(p); };

        // ========================================================================
        // Conversational Memory System - Lossless storage and retrieval
        // ========================================================================

        tools_.push_back({
            {"name", "get_turns"},
            {"description", "Get conversation turns for a session. Returns lossless history of all user and assistant messages."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session ID (default: current session)"}}},
                    {"start_index", {{"type", "integer"}, {"description", "Starting turn index (default: 0)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max turns to return (default: 50)"}}}
                }}
            }}
        });
        handlers_["get_turns"] = [this](const json& p) { return tool_get_turns(p); };

        tools_.push_back({
            {"name", "create_episode"},
            {"description", "Create a dialogue episode for tracking conversation segments. Links to turn ranges for hierarchical retrieval."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session ID"}}},
                    {"title", {{"type", "string"}, {"description", "Episode title/topic"}}},
                    {"start_turn", {{"type", "integer"}, {"description", "Starting turn index"}}},
                    {"end_turn", {{"type", "integer"}, {"description", "Ending turn index (optional, 0 = ongoing)"}}},
                    {"episode_type", {{"type", "string"}, {"description", "Type: distillation, task, discussion"}}},
                    {"realm", {{"type", "string"}, {"description", "Realm (default: brahman)"}}}
                }},
                {"required", {"session_id", "title", "start_turn"}}
            }}
        });
        handlers_["create_episode"] = [this](const json& p) { return tool_create_episode(p); };

        tools_.push_back({
            {"name", "query_claims"},
            {"description", "Query semantic claims (subject-predicate-object facts). Use for retrieving learned facts and detecting contradictions."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"subject", {{"type", "string"}, {"description", "Filter by subject (e.g., 'user', 'assistant', entity name)"}}},
                    {"predicate", {{"type", "string"}, {"description", "Filter by predicate (e.g., 'prefers', 'was_corrected_on')"}}},
                    {"scope", {{"type", "string"}, {"description", "Filter by scope: task, session, repo, project, user, global"}}},
                    {"active_only", {{"type", "boolean"}, {"description", "Only return non-superseded claims (default: true)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max claims to return (default: 20)"}}}
                }}
            }}
        });
        handlers_["query_claims"] = [this](const json& p) { return tool_query_claims(p); };

        tools_.push_back({
            {"name", "get_policies"},
            {"description", "Get active policy memories (preferences, corrections, constraints). Policies have promotion states: ephemeral → candidate → stable_soft → stable_hard."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"scope", {{"type", "string"}, {"description", "Filter by scope: session, repo, project, user, global"}}},
                    {"type", {{"type", "string"}, {"description", "Filter by policy type: preference, correction, constraint"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max policies to return (default: 30)"}}}
                }}
            }}
        });
        handlers_["get_policies"] = [this](const json& p) { return tool_get_policies(p); };

        tools_.push_back({
            {"name", "hybrid_recall"},
            {"description", "State-of-the-art memory retrieval combining vector similarity, BM25 keyword matching, and graph spreading with RRF fusion."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Search query text"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default: 10)"}}},
                    {"tag", {{"type", "string"}, {"description", "Filter by tag"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}},
                    {"vector_weight", {{"type", "number"}, {"description", "Weight for vector similarity (default: 0.4)"}}},
                    {"bm25_weight", {{"type", "number"}, {"description", "Weight for BM25 keyword match (default: 0.3)"}}},
                    {"graph_weight", {{"type", "number"}, {"description", "Weight for graph spreading (default: 0.2)"}}},
                    {"recency_weight", {{"type", "number"}, {"description", "Weight for recency bonus (default: 0.1)"}}}
                }},
                {"required", {"query"}}
            }}
        });
        handlers_["hybrid_recall"] = [this](const json& p) { return tool_hybrid_recall(p); };

        tools_.push_back({
            {"name", "expand_query"},
            {"description", "Expand a query into typed variants (lex for BM25, vec for vector search, hyde as hypothetical memory excerpt) for improved hybrid retrieval"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Query to expand"}}}
                }},
                {"required", {"query"}}
            }}
        });
        handlers_["expand_query"] = [this](const json& p) { return tool_expand_query(p); };

        tools_.push_back({
            {"name", "smart_recall"},
            {"description", "Intent-aware memory retrieval. Classifies query type (temporal, entity, relational) and routes to optimal retrieval path. Returns structured results with date candidates for temporal queries."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Natural language query"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default: 10)"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}}
                }},
                {"required", {"query"}}
            }}
        });
        handlers_["smart_recall"] = [this](const json& p) { return tool_smart_recall(p); };

        tools_.push_back({
            {"name", "get_entities"},
            {"description", "Get tracked entities (user, assistant, projects, concepts) with salience scores."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"type", {{"type", "string"}, {"description", "Filter by entity type: person, project, concept, tool, file"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max entities to return (default: 20)"}}}
                }}
            }}
        });
        handlers_["get_entities"] = [this](const json& p) { return tool_get_entities(p); };

        tools_.push_back({
            {"name", "get_relationship_events"},
            {"description", "Get relationship events (corrections, praise, frustration, discoveries) for understanding user-assistant interaction patterns."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"event_type", {{"type", "string"}, {"description", "Filter by type: correction, praise, frustration, discovery"}}},
                    {"session_id", {{"type", "string"}, {"description", "Filter by session"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max events to return (default: 20)"}}}
                }}
            }}
        });
        handlers_["get_relationship_events"] = [this](const json& p) { return tool_get_relationship_events(p); };

        // ═══════════════════════════════════════════════════════════════════════
        // Sadhana: Unified Autonomous Agent System
        // ═══════════════════════════════════════════════════════════════════════

        tools_.push_back({
            {"name", "sadhana_start"},
            {"description", "Create and start a new autonomous agent (sadhana) that works toward a goal using full Claude Code sessions with tool access. Each cycle runs a complete agent with bash, file, and chitta memory tools. Default interval: 300s."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"goal", {{"type", "string"}, {"description", "The goal for the agent to achieve"}}},
                    {"brain_provider", {{"type", "string"}, {"description", "LLM provider: claude or opencode (default: claude)"}}},
                    {"brain_model", {{"type", "string"}, {"description", "Model to use: sonnet, opus, haiku, gpt-4o (default: sonnet)"}}},
                    {"interval_seconds", {{"type", "integer"}, {"description", "Seconds between sense-think-act cycles (default: 60)"}}},
                    {"max_turns", {{"type", "integer"}, {"description", "Max turns per cycle (0 = use global default of 20)"}}},
                    {"realm", {{"type", "string"}, {"description", "Realm for isolation (default: brahman)"}}}
                }},
                {"required", {"goal"}}
            }}
        });
        handlers_["sadhana_start"] = [this](const json& p) { return tool_sadhana_start(p); };

        tools_.push_back({
            {"name", "sadhana_pause"},
            {"description", "Pause a running sadhana"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Sadhana ID to pause"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["sadhana_pause"] = [this](const json& p) { return tool_sadhana_pause(p); };

        tools_.push_back({
            {"name", "sadhana_resume"},
            {"description", "Resume a paused sadhana"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Sadhana ID to resume"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["sadhana_resume"] = [this](const json& p) { return tool_sadhana_resume(p); };

        tools_.push_back({
            {"name", "sadhana_stop"},
            {"description", "Stop a sadhana (mark as done or failed)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Sadhana ID to stop"}}},
                    {"success", {{"type", "boolean"}, {"description", "Whether the goal was achieved (default: true)"}}},
                    {"reason", {{"type", "string"}, {"description", "Reason for stopping"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["sadhana_stop"] = [this](const json& p) { return tool_sadhana_stop(p); };

        tools_.push_back({
            {"name", "sadhana_status"},
            {"description", "Get status of a sadhana including history"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Sadhana ID"}}},
                    {"history_limit", {{"type", "integer"}, {"description", "Max history events to return (default: 20)"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["sadhana_status"] = [this](const json& p) { return tool_sadhana_status(p); };

        tools_.push_back({
            {"name", "sadhana_list"},
            {"description", "List all sadhanas with optional filters"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"state", {{"type", "string"}, {"description", "Filter by state: pending, running, paused, done, failed"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default: 50)"}}}
                }}
            }}
        });
        handlers_["sadhana_list"] = [this](const json& p) { return tool_sadhana_list(p); };

        tools_.push_back({
            {"name", "sadhana_set_model"},
            {"description", "Change the brain model for a running sadhana"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Sadhana ID"}}},
                    {"model", {{"type", "string"}, {"description", "New model: opus, sonnet, haiku, etc."}}}
                }},
                {"required", {"id", "model"}}
            }}
        });
        handlers_["sadhana_set_model"] = [this](const json& p) { return tool_sadhana_set_model(p); };

        tools_.push_back({
            {"name", "sadhana_set_goal"},
            {"description", "Change the goal/prompt for a sadhana"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Sadhana ID"}}},
                    {"goal", {{"type", "string"}, {"description", "New goal/prompt for the sadhana"}}}
                }},
                {"required", {"id", "goal"}}
            }}
        });
        handlers_["sadhana_set_goal"] = [this](const json& p) { return tool_sadhana_set_goal(p); };

        tools_.push_back({
            {"name", "sadhana_set_interval"},
            {"description", "Change the tick interval (in seconds) for a sadhana"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Sadhana ID"}}},
                    {"interval", {{"type", "integer"}, {"description", "New interval in seconds"}}}
                }},
                {"required", {"id", "interval"}}
            }}
        });
        handlers_["sadhana_set_interval"] = [this](const json& p) { return tool_sadhana_set_interval(p); };

        tools_.push_back({
            {"name", "sadhana_set_max_turns"},
            {"description", "Set the max turns per cycle for a sadhana (0 = use global default of 20)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Sadhana ID"}}},
                    {"max_turns", {{"type", "integer"}, {"description", "Max turns per cycle (0 = use global default)"}}}
                }},
                {"required", {"id", "max_turns"}}
            }}
        });
        handlers_["sadhana_set_max_turns"] = [this](const json& p) { return tool_sadhana_set_max_turns(p); };

        tools_.push_back({
            {"name", "sadhana_checkpoint"},
            {"description", "Report a mid-cycle checkpoint from within an agentic sadhana. "
             "Call this from inside a running sadhana cycle to log progress and optionally signal completion. "
             "Use status='achieved' to stop the sadhana, 'blocked' to pause it, 'progressed' to continue normally."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Sadhana ID"}}},
                    {"status", {{"type", "string"},
                                {"enum", {"progressed", "achieved", "blocked"}},
                                {"description", "Cycle status"}}},
                    {"summary", {{"type", "string"}, {"description", "What was done this cycle"}}}
                }},
                {"required", {"id", "status", "summary"}}
            }}
        });
        handlers_["sadhana_checkpoint"] = [this](const json& p) { return tool_sadhana_checkpoint(p); };

        // ═══════════════════════════════════════════════════════════════════════
        // Dream: Autonomous Curiosity-Driven Exploration (svapna)
        // ═══════════════════════════════════════════════════════════════════════

        tools_.push_back({
            {"name", "dream_start"},
            {"description", "Start an autonomous dream: the soul explores a topic freely using WebSearch, WebFetch, and memory tools during idle time. Dream memories are tagged with [dream]."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"topic", {{"type", "string"}, {"description", "Topic to explore (e.g. 'quantum entanglement', 'stoic philosophy')"}}},
                    {"realm", {{"type", "string"}, {"description", "Memory realm (default: brahman)"}}},
                    {"publish_path", {{"type", "string"}, {"description", "Optional: absolute path to docs/dreams/ directory. If set, the dream agent will write an HTML blog post there and commit+push."}}}
                }},
                {"required", {"topic"}}
            }}
        });
        handlers_["dream_start"] = [this](const json& p) { return tool_dream_start(p); };

        tools_.push_back({
            {"name", "dream_wander"},
            {"description", "Auto-select a topic from memory gaps or curiosity seeds and start a dream. Use when you want the soul to explore freely without specifying a topic."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"realm", {{"type", "string"}, {"description", "Memory realm (default: brahman)"}}},
                    {"publish_path", {{"type", "string"}, {"description", "Optional: path to docs/dreams/ for blog post publishing. Falls back to CHITTA_DREAM_PUBLISH_PATH env var."}}}
                }}
            }}
        });
        handlers_["dream_wander"] = [this](const json& p) { return tool_dream_wander(p); };

        tools_.push_back({
            {"name", "dream_cancel"},
            {"description", "Cancel a stuck or unwanted dream. Stops the underlying sadhana and marks the dream as cancelled."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Dream ID to cancel"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["dream_cancel"] = [this](const json& p) { return tool_dream_cancel(p); };

        tools_.push_back({
            {"name", "dream_force_woke"},
            {"description", "Force a stuck dreaming dream to woke status without findings. Stops the sadhana and marks the dream as complete so new dreams can start."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Dream ID to force-wake"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["dream_force_woke"] = [this](const json& p) { return tool_dream_force_woke(p); };

        tools_.push_back({
            {"name", "dream_list"},
            {"description", "List recent dreams with their topics, status, and findings"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"limit", {{"type", "integer"}, {"description", "Max results (default: 10)"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm"}}}
                }}
            }}
        });
        handlers_["dream_list"] = [this](const json& p) { return tool_dream_list(p); };

        tools_.push_back({
            {"name", "dream_status"},
            {"description", "Get full details of a dream including its sadhana agent history"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Dream ID"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["dream_status"] = [this](const json& p) { return tool_dream_status(p); };

        // ═══════════════════════════════════════════════════════════════════════
        // Context Repository Tools (Letta-inspired)
        // ═══════════════════════════════════════════════════════════════════════

        // Memory version history
        tools_.push_back({
            {"name", "memory_history"},
            {"description", "View version history of a memory (git-like audit trail)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Memory ID"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max versions to return (default: 20)"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["memory_history"] = [this](const json& p) { return tool_memory_history(p); };

        // Revert to previous version
        tools_.push_back({
            {"name", "memory_revert"},
            {"description", "Revert memory to a previous version"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Memory ID"}}},
                    {"version", {{"type", "integer"}, {"description", "Version number to revert to"}}},
                    {"reason", {{"type", "string"}, {"description", "Reason for reverting"}}}
                }},
                {"required", {"id", "version"}}
            }}
        });
        handlers_["memory_revert"] = [this](const json& p) { return tool_memory_revert(p); };

        // Pin memory (agent-managed context)
        tools_.push_back({
            {"name", "pin_memory"},
            {"description", "Pin a memory to keep it 'hot' in context (agent decides importance)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Memory ID to pin"}}},
                    {"reason", {{"type", "string"}, {"description", "Why this memory should stay hot"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["pin_memory"] = [this](const json& p) { return tool_pin_memory(p); };

        // Unpin memory
        tools_.push_back({
            {"name", "unpin_memory"},
            {"description", "Unpin a memory (allow it to fade naturally)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Memory ID to unpin"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["unpin_memory"] = [this](const json& p) { return tool_unpin_memory(p); };

        // List pinned memories
        tools_.push_back({
            {"name", "list_pinned"},
            {"description", "List all pinned memories (agent-managed context)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"realm", {{"type", "string"}, {"description", "Filter by realm (optional)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default: 50)"}}}
                }}
            }}
        });
        handlers_["list_pinned"] = [this](const json& p) { return tool_list_pinned(p); };

        // Acquire memory lock (concurrent coordination)
        tools_.push_back({
            {"name", "memory_lock"},
            {"description", "Acquire a lock on a memory for exclusive access (concurrent sadhana coordination)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Memory ID to lock"}}},
                    {"holder_id", {{"type", "string"}, {"description", "ID of the session/sadhana acquiring the lock"}}},
                    {"holder_type", {{"type", "string"}, {"description", "Type: session, sadhana, skill (default: session)"}}},
                    {"duration", {{"type", "integer"}, {"description", "Lock duration in seconds (default: 300)"}}}
                }},
                {"required", {"id", "holder_id"}}
            }}
        });
        handlers_["memory_lock"] = [this](const json& p) { return tool_memory_lock(p); };

        // Release memory lock
        tools_.push_back({
            {"name", "memory_unlock"},
            {"description", "Release a lock on a memory"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Memory ID to unlock"}}},
                    {"holder_id", {{"type", "string"}, {"description", "ID of the holder releasing the lock"}}}
                }},
                {"required", {"id", "holder_id"}}
            }}
        });
        handlers_["memory_unlock"] = [this](const json& p) { return tool_memory_unlock(p); };

        // Check lock status
        tools_.push_back({
            {"name", "memory_lock_status"},
            {"description", "Check if a memory is locked and by whom"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Memory ID to check"}}}
                }},
                {"required", {"id"}}
            }}
        });
        handlers_["memory_lock_status"] = [this](const json& p) { return tool_memory_lock_status(p); };

        // Propose change to locked memory
        tools_.push_back({
            {"name", "propose_change"},
            {"description", "Propose a change to a memory (queued if locked, for later merge)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "integer"}, {"description", "Memory ID to change"}}},
                    {"content", {{"type", "string"}, {"description", "Proposed new content"}}},
                    {"proposed_by", {{"type", "string"}, {"description", "ID of the proposer"}}}
                }},
                {"required", {"id", "content", "proposed_by"}}
            }}
        });
        handlers_["propose_change"] = [this](const json& p) { return tool_propose_change(p); };

        // List merge queue
        tools_.push_back({
            {"name", "list_merge_queue"},
            {"description", "List pending merge proposals for conflict resolution"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"status", {{"type", "string"}, {"description", "Filter by status: pending, applied, rejected, conflict"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default: 50)"}}}
                }}
            }}
        });
        handlers_["list_merge_queue"] = [this](const json& p) { return tool_list_merge_queue(p); };

        // Resolve merge
        tools_.push_back({
            {"name", "resolve_merge"},
            {"description", "Resolve a pending merge proposal (apply, reject, or mark conflict)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"merge_id", {{"type", "integer"}, {"description", "Merge proposal ID"}}},
                    {"resolution", {{"type", "string"}, {"description", "Resolution notes"}}},
                    {"status", {{"type", "string"}, {"description", "New status: applied, rejected, conflict"}}}
                }},
                {"required", {"merge_id", "status"}}
            }}
        });
        handlers_["resolve_merge"] = [this](const json& p) { return tool_resolve_merge(p); };

        // ═══════════════════════════════════════════════════════════════════════
        // File Time Machine: Explore and restore file versions from past sessions
        // ═══════════════════════════════════════════════════════════════════════

        tools_.push_back({
            {"name", "file_timeline"},
            {"description", "Show files modified in a time range or session (Time Machine). Default: last 24h. Use path or file_pattern to look up all versions of a specific file across all indexed sessions. Use cross_session=true for history across all sessions (7-day window)."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Natural language time query like 'at 22:33', 'yesterday', 'last hour'"}}},
                    {"session_id", {{"type", "string"}, {"description", "Specific session to query"}}},
                    {"path", {{"type", "string"}, {"description", "File path or glob pattern to look up (alias for file_pattern). When provided, searches all indexed history with no time constraint."}}},
                    {"file_pattern", {{"type", "string"}, {"description", "Glob pattern to filter files (e.g., '*.cpp', 'src/*'). Alias: path"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max results (default: 20)"}}},
                    {"cross_session", {{"type", "boolean"}, {"description", "Search across all sessions (auto-indexes unindexed sessions, default: true when path is given, false otherwise)"}}}
                }}
            }}
        });
        handlers_["file_timeline"] = [this](const json& p) { return tool_file_timeline(p); };

        tools_.push_back({
            {"name", "file_at_time"},
            {"description", "Get file content as it was at a specific time (Time Machine)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"file_path", {{"type", "string"}, {"description", "File path to retrieve"}}},
                    {"time", {{"type", "string"}, {"description", "Timestamp or natural language (e.g., '2024-02-13T22:33:00', '5 minutes ago')"}}},
                    {"session_id", {{"type", "string"}, {"description", "Specific session to search in"}}},
                    {"show_diff", {{"type", "boolean"}, {"description", "Show diff against current version (default: false)"}}}
                }},
                {"required", {"file_path"}}
            }}
        });
        handlers_["file_at_time"] = [this](const json& p) { return tool_file_at_time(p); };

        tools_.push_back({
            {"name", "file_restore"},
            {"description", "Restore file to a previous version (Time Machine)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"file_path", {{"type", "string"}, {"description", "File path to restore"}}},
                    {"version_id", {{"type", "integer"}, {"description", "Version ID from file_timeline"}}},
                    {"preview", {{"type", "boolean"}, {"description", "Preview only, don't actually restore (default: true)"}}}
                }},
                {"required", {"file_path"}}
            }}
        });
        handlers_["file_restore"] = [this](const json& p) { return tool_file_restore(p); };

        tools_.push_back({
            {"name", "file_index_session"},
            {"description", "Index file-history from a session transcript (internal)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"session_id", {{"type", "string"}, {"description", "Session ID to index"}}},
                    {"force", {{"type", "boolean"}, {"description", "Re-index even if already indexed (default: false)"}}}
                }},
                {"required", {"session_id"}}
            }}
        });
        handlers_["file_index_session"] = [this](const json& p) { return tool_file_index_session(p); };

        // file_index_all - index all sessions from file-history for cross-session timeline
        tools_.push_back({
            {"name", "file_index_all"},
            {"description", "Index all sessions from file-history for cross-session file timeline. Run once to enable cross-session file recovery."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"force", {{"type", "boolean"}, {"description", "Re-index all sessions even if already indexed (default: false)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max sessions to index (default: 100, 0 = all)"}}}
                }}
            }}
        });
        handlers_["file_index_all"] = [this](const json& p) { return tool_file_index_all(p); };

        // chitta_health - soul feedback loop health diagnostics
        tools_.push_back({
            {"name", "chitta_health"},
            {"description", "Report feedback loop health: correction learning rate, recurring mistakes, dream synthesis lag, and memory type distribution."},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}
        });
        handlers_["chitta_health"] = [this](const json& p) { return tool_chitta_health(p); };

        classify_tools();
    }

    void classify_tools() {
        // Internal tools - hidden from MCP tools/list (maintenance/hooks only)
        static const std::vector<std::string> internal_tools = {
            "cleanup", "cleanup_code_wisdom", "hygiene_run",
            "consolidation_scan", "consolidation_merge", "consolidation_auto",
            "batch_forget", "sql_query", "migrate_vss", "reembed_memories",
            "dedupe_symbols", "background_run_cycle", "background_schedule", "background_status",
            "metacognition_corrections", "metacognition_outcomes",
            "distill_status", "enrichment_status", "epiplexity_check",
            "clear_codebase", "clear_triplets", "describe_symbol", "extract_symbols",
            "file_dependents", "file_imports", "resolve_callsites",
            "restore_code_intel_confidence", "ssl_convert", "subconscious_stats",
            "suggestion_count", "suggestion_pending", "suggestion_resolve", "suggestion_track",
            "transcript_get", "transcript_list", "transcript_parse", "transcript_register",
            "transcript_remove", "transcript_search", "transcript_update",
            "type_hierarchy", "version_check",
            "export_soul", "import_soul",
            "connect_batch", "research_cycle", "research_store", "research_topics",
            "cycle", "anticipation_gate_status", "anticipation_record_outcome",
            "session_register", "session_heartbeat", "session_deregister", "msg_ack",
            "file_index_session", "file_index_all",
            "chitta_health"
        };

        // Advanced tools - visible but secondary
        static const std::vector<std::string> advanced_tools = {
            "strengthen", "weaken", "tag", "update", "get", "query_graph",
            "realm_add", "realm_detect", "realm_get", "realm_list", "realm_remove", "realm_set", "realm_visibility",
            "goal_set", "goal_get", "goal_list", "goal_complete", "goal_progress",
            "habit_observe", "habit_match", "habit_list", "habit_strengthen", "habit_weaken",
            "anticipation_predict", "anticipation_observe", "anticipation_list", "anticipation_success",
            "calibration_record", "calibration_score",
            "profile_get", "profile_observe", "profile_update",
            "curiosity_gaps", "curiosity_note_gap", "curiosity_resolve",
            "narrative_history",
            // Context Repository (Letta-inspired)
            "memory_history", "memory_revert", "pin_memory", "unpin_memory", "list_pinned",
            "memory_lock", "memory_unlock", "memory_lock_status",
            "propose_change", "list_merge_queue", "resolve_merge",
            // File Time Machine
            "file_timeline", "file_at_time", "file_restore"
        };

        // Everything defaults to "default" visibility
        for (const auto& name : internal_tools) {
            tool_visibility_[name] = "internal";
        }
        for (const auto& name : advanced_tools) {
            tool_visibility_[name] = "advanced";
        }
        // All remaining tools are implicitly "default"
    }

    // ========================================================================
    // SSL (Soul Semantic Language) helpers
    // ========================================================================

    // Check if content follows SSL format: [domain] or starts with known prefixes
    static bool is_ssl_format(const std::string& content) {
        if (content.empty()) return false;
        // Has domain tag: [domain]
        if (content[0] == '[' && content.find(']') != std::string::npos) return true;
        // Known SSL prefixes from distillation
        if (content.rfind("[LEARN]", 0) == 0) return true;
        if (content.rfind("[ε]", 0) == 0) return true;
        // Has SSL arrows (→)
        if (content.find("→") != std::string::npos) return true;
        return false;
    }

    // Convert raw text to basic SSL format
    static std::string to_ssl_format(const std::string& content,
                                      const std::string& domain = "note",
                                      const std::string& location = "") {
        if (is_ssl_format(content)) return content;  // Already SSL

        std::string result = "[" + domain + "] ";

        // Extract first line as subject, rest as detail
        size_t newline = content.find('\n');
        if (newline != std::string::npos && newline < 80) {
            result += content.substr(0, newline);
            if (!location.empty()) result += " @" + location;
            result += "\n" + content.substr(newline + 1);
        } else if (content.size() > 80) {
            // Truncate first line, keep rest
            result += content.substr(0, 80) + "...";
            if (!location.empty()) result += " @" + location;
            result += "\n" + content;
        } else {
            result += content;
            if (!location.empty()) result += " @" + location;
        }

        return result;
    }

    // Tool implementations
    DuckDBToolResult tool_remember(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string content = params.value("content", "");
        if (content.empty()) {
            return DuckDBToolResult::error("Content is required");
        }

        std::string type_str = params.value("type", "episode");
        std::string realm = params.value("realm", "brahman");

        // Auto-convert to SSL format if not already
        // Skip for code-intel types which have their own format
        bool is_code_intel = (type_str == "symbol" || type_str == "projectessence" ||
                              type_str == "modulestate" || type_str == "patternstate");
        if (!is_code_intel && !is_ssl_format(content)) {
            // Infer domain from realm or type
            std::string domain = (realm != "brahman") ? realm : type_str;
            content = to_ssl_format(content, domain);
        }
        int visibility_int = params.value("visibility", 0);
        RealmVisibility visibility = static_cast<RealmVisibility>(std::clamp(visibility_int, 0, 2));

        std::vector<std::string> shared_realms;
        if (params.contains("shared_realms") && params["shared_realms"].is_array()) {
            for (const auto& r : params["shared_realms"]) {
                if (r.is_string()) shared_realms.push_back(r.get<std::string>());
            }
        }

        NodeType type = NodeType::Episode;
        if (type_str == "wisdom") type = NodeType::Wisdom;
        else if (type_str == "belief") type = NodeType::Belief;
        else if (type_str == "intention") type = NodeType::Intention;

        // Validate before calling mind_->remember
        if (!mind_->passes_quality_gate_public(content)) {
            return DuckDBToolResult::error("Failed: quality gate (length=" +
                std::to_string(content.size()) + ", min=10)");
        }
        if (!mind_->embedder_ready()) {
            return DuckDBToolResult::error("Failed: embedder not ready");
        }

        NodeId id;
        if (params.contains("tags") && params["tags"].is_array()) {
            std::vector<std::string> tags;
            for (const auto& t : params["tags"]) {
                if (t.is_string()) tags.push_back(t.get<std::string>());
            }
            id = mind_->remember(content, type, tags);
        } else {
            id = mind_->remember(content, type);
        }

        static const NodeId null_id{};
        if (id == null_id) {
            std::string err = mind_->store().last_error();
            return DuckDBToolResult::error("Failed: " + (err.empty() ? "store.remember returned -1" : err));
        }

        // Set realm, visibility, and confidence if non-default
        int64_t db_id = static_cast<int64_t>(id.low);
        if (realm != "brahman") {
            mind_->store().set_realm(db_id, realm);
        }
        if (visibility != RealmVisibility::Private) {
            mind_->store().set_visibility(db_id, visibility);
        }
        // Apply custom confidence if specified (default is 0.8)
        if (params.contains("confidence")) {
            float target = std::clamp(params.value("confidence", 0.8f), 0.0f, 1.0f);
            float delta = target - 0.8f;  // Default confidence is 0.8
            if (delta > 0.01f) {
                mind_->store().strengthen(db_id, delta);
            } else if (delta < -0.01f) {
                mind_->store().weaken(db_id, -delta);
            }
        }
        for (const auto& shared : shared_realms) {
            mind_->store().add_to_realm(db_id, shared);
        }

        std::string preview = content.substr(0, 50);
        if (content.size() > 50) preview += "...";

        json result = {{"id", id.to_string()}, {"realm", realm}};
        if (visibility != RealmVisibility::Private) {
            result["visibility"] = visibility_int;
        }
        if (!shared_realms.empty()) {
            result["shared_realms"] = shared_realms;
        }

        return DuckDBToolResult::ok("Remembered: " + preview, result);
    }

    DuckDBToolResult tool_recall(const json& params) {
        // Notify subconscious that we're handling a query (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string query = params.value("query", "");
        if (query.empty()) {
            return DuckDBToolResult::error("Query is required");
        }

        size_t limit = params.value("limit", 10);
        float min_confidence = params.value("min_confidence", 0.0f);
        std::string tag = params.value("tag", "");
        std::string realm = params.value("realm", "");
        bool include_global = params.value("include_global", true);

        // If tag specified, use tag-filtered recall for proper scoping
        std::vector<Recall> results;
        if (!tag.empty()) {
            // Get embedding for query
            if (!mind_->embedder_ready()) {
                return DuckDBToolResult::error("Embedder not ready");
            }
            auto embedding = mind_->embedder().embed_query(query).data;
            if (embedding.empty()) {
                return DuckDBToolResult::error("Failed to generate query embedding");
            }
            // Use tag-filtered recall
            auto store_results = mind_->store().recall_with_tag(embedding, tag, limit);
            for (const auto& r : store_results) {
                Recall rec;
                rec.id = NodeId(r.id);
                rec.text = r.content;
                rec.type = NodeType::Episode; // Default type
                rec.similarity = r.similarity;
                rec.relevance = r.similarity;
                rec.confidence = Confidence(r.confidence);
                results.push_back(rec);
            }
        } else {
            // Use normal recall
            size_t fetch_limit = !realm.empty() ? limit * 3 : limit;
            results = mind_->recall(query, fetch_limit);
        }

        std::ostringstream ss;
        json results_json = json::array();
        size_t count = 0;

        for (const auto& r : results) {
            int64_t mem_id = static_cast<int64_t>(r.id.low);

            // Tag filtering is handled by recall_with_tag, no post-hoc filter needed

            // Post-hoc realm filtering if realm specified
            if (!realm.empty()) {
                auto realms = mind_->store().get_realms(mem_id);
                bool in_realm = false;
                for (const auto& rm : realms) {
                    if (rm == realm) { in_realm = true; break; }
                }
                // Check if memory is global and include_global is true
                auto mem = mind_->store().get_memory(mem_id);
                if (mem && mem->visibility == RealmVisibility::Global && include_global) {
                    in_realm = true;
                }
                if (!in_realm) continue;
            }

            int pct = static_cast<int>(std::min(r.relevance, 1.0f) * 100);
            std::string type_name = node_type_name(r.type);

            json result_entry = {
                {"id", r.id.to_string()},
                {"relevance", r.relevance},
                {"similarity", r.similarity},
                {"type", type_name},
                {"text", r.text}
            };

            // Include realm info in results and apply confidence filter
            auto mem = mind_->store().get_memory(mem_id);
            if (mem) {
                // Skip if below min_confidence threshold
                if (mem->confidence < min_confidence) continue;

                result_entry["realm"] = mem->realm;
                result_entry["confidence"] = mem->confidence;
                if (mem->visibility != RealmVisibility::Private) {
                    result_entry["visibility"] = static_cast<int>(mem->visibility);
                }
            }

            results_json.push_back(result_entry);
            count++;
            if (count >= limit) break;
        }

        // Build output text with header and results
        ss << "Found " << count << " results";
        if (!realm.empty()) ss << " in realm '" << realm << "'";
        ss << ":\n";
        for (const auto& r : results_json) {
            int pct = static_cast<int>(r["relevance"].get<double>() * 100);
            ss << "[" << pct << "%] [" << r["type"].get<std::string>() << "] "
               << r["text"].get<std::string>().substr(0, 100);
            if (r["text"].get<std::string>().size() > 100) ss << "...";
            ss << "\n";
        }

        // SUS: log recall query
        try {
            std::string _sus_sid = get_session_id(params);
            if (!_sus_sid.empty() && _sus_sid != "default") {
                json _sus_ids = json::array();
                json _sus_scores = json::array();
                for (const auto& rj : results_json) {
                    _sus_ids.push_back(rj["id"]);
                    _sus_scores.push_back(rj.value("similarity", 0.0));
                }
                mind_->store().log_recall_query(
                    _sus_sid, 0, "recall", query,
                    _sus_ids.dump(), _sus_scores.dump());
            }
        } catch (...) {}

        return DuckDBToolResult::ok(ss.str(), {{"results", results_json}, {"realm", realm}});
    }

    // Parse ISO8601 or YYYY-MM-DD date to Unix milliseconds
    std::optional<int64_t> parse_timestamp_str(const std::string& ts) {
        if (ts.empty()) return std::nullopt;

        std::tm tm = {};
        int year, month, day, hour = 0, min = 0, sec = 0;

        // Try YYYY-MM-DDTHH:MM:SS or YYYY-MM-DD HH:MM:SS
        if (std::sscanf(ts.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &min, &sec) >= 3 ||
            std::sscanf(ts.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec) >= 3 ||
            std::sscanf(ts.c_str(), "%d-%d-%d", &year, &month, &day) == 3) {
            tm.tm_year = year - 1900;
            tm.tm_mon = month - 1;
            tm.tm_mday = day;
            tm.tm_hour = hour;
            tm.tm_min = min;
            tm.tm_sec = sec;
            tm.tm_isdst = -1;

            std::time_t time = std::mktime(&tm);
            if (time == -1) return std::nullopt;
            return static_cast<int64_t>(time) * 1000;  // Convert to milliseconds
        }

        // Try parsing as Unix timestamp (seconds or milliseconds)
        try {
            int64_t val = std::stoll(ts);
            // If value is too small to be milliseconds (before year 2000), assume seconds
            if (val < 946684800000LL) val *= 1000;
            return val;
        } catch (...) {
            return std::nullopt;
        }
    }

    DuckDBToolResult tool_recall_temporal(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        std::string query = params.value("query", "");
        std::string start_str = params.value("start", "");
        std::string end_str = params.value("end", "");
        size_t limit = params.value("limit", 20);
        std::string realm = params.value("realm", "");
        bool include_global = params.value("include_global", true);

        // Parse timestamps - default to last 7 days if not specified
        auto start_time = parse_timestamp_str(start_str);
        auto end_time = parse_timestamp_str(end_str);

        if (!start_time && !end_time) {
            // Default: last 7 days
            auto now = std::chrono::system_clock::now();
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            end_time = now_ms;
            start_time = now_ms - (7 * 24 * 60 * 60 * 1000LL);  // 7 days ago
        }

        // If end time is a date without time, set to end of day
        if (end_time && end_str.find('T') == std::string::npos && end_str.find(':') == std::string::npos) {
            *end_time += 86400000 - 1;  // Add 24 hours minus 1 ms
        }

        // Get query embedding if query provided
        std::vector<float> embedding;
        if (!query.empty() && mind_->embedder_ready()) {
            embedding = mind_->embedder().embed_query(query).data;
        }

        auto results = mind_->store().recall_temporal(
            embedding, start_time, end_time, limit, realm, include_global
        );

        std::ostringstream ss;
        json results_json = json::array();

        for (const auto& r : results) {
            // Format timestamp for display
            std::time_t created_sec = r.created_at / 1000;
            std::tm* tm = std::localtime(&created_sec);
            char time_buf[32];
            std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", tm);

            json result_entry = {
                {"id", std::to_string(r.id)},
                {"kind", r.kind},
                {"content", r.content},
                {"confidence", r.confidence},
                {"created_at", r.created_at},
                {"created_at_str", std::string(time_buf)},
                {"realm", r.realm}
            };

            if (!embedding.empty()) {
                result_entry["similarity"] = r.similarity;
            }

            results_json.push_back(result_entry);

            // Build text output
            int sim_pct = embedding.empty() ? 0 : static_cast<int>(r.similarity * 100);
            ss << "[" << time_buf << "]";
            if (!embedding.empty()) ss << " [" << sim_pct << "%]";
            ss << " [" << r.kind << "] ";
            std::string preview = r.content.substr(0, 100);
            size_t nl = preview.find('\n');
            if (nl != std::string::npos) preview = preview.substr(0, nl);
            ss << preview;
            if (r.content.size() > 100) ss << "...";
            ss << "\n";
        }

        std::ostringstream header;
        header << "Found " << results.size() << " memories";
        if (start_time || end_time) {
            header << " from ";
            if (start_time) header << start_str;
            else header << "beginning";
            header << " to ";
            if (end_time) header << end_str;
            else header << "now";
        }
        if (!realm.empty()) header << " in realm '" << realm << "'";
        header << ":\n";

        return DuckDBToolResult::ok(header.str() + ss.str(), {
            {"results", results_json},
            {"count", results.size()},
            {"start", start_str},
            {"end", end_str},
            {"realm", realm}
        });
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // RLM-style Exploration Primitives
    // ═══════════════════════════════════════════════════════════════════════════

    DuckDBToolResult tool_explore_recall(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) {
            return DuckDBToolResult::error("Query is required");
        }

        size_t limit = params.value("limit", 15);
        auto results = mind_->recall(query, limit);

        // Return lightweight results: id, title (first line), score only
        json hints = json::array();
        std::ostringstream ss;
        ss << "Found " << results.size() << " hints:\n";

        for (const auto& r : results) {
            // Extract title (first line or first 80 chars)
            std::string title = r.text;
            size_t newline = title.find('\n');
            if (newline != std::string::npos) title = title.substr(0, newline);
            if (title.size() > 80) title = title.substr(0, 77) + "...";

            int pct = static_cast<int>(std::min(r.relevance, 1.0f) * 100);
            ss << "  [" << pct << "%] " << r.id.to_string() << ": " << title << "\n";

            hints.push_back({
                {"id", r.id.to_string()},
                {"title", title},
                {"score", r.relevance},
                {"type", node_type_name(r.type)}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"hints", hints}, {"count", results.size()}});
    }

    DuckDBToolResult tool_explore_peek(const json& params) {
        auto [db_id, id_str] = parse_id(params);
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        auto result = mind_->store().get_memory(db_id);
        if (!result) {
            return DuckDBToolResult::error("Memory not found: " + id_str);
        }

        // Return first 200 chars as summary
        std::string summary = result->content;
        if (summary.size() > 200) {
            summary = summary.substr(0, 197) + "...";
        }

        return DuckDBToolResult::ok(summary, {
            {"id", id_str},
            {"kind", result->kind},
            {"confidence", result->confidence},
            {"summary", summary},
            {"full_length", result->content.size()}
        });
    }

    DuckDBToolResult tool_explore_expand(const json& params) {
        auto [db_id, id_str] = parse_id(params);
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        auto result = mind_->store().get_memory(db_id);
        if (!result) {
            return DuckDBToolResult::error("Memory not found: " + id_str);
        }

        return DuckDBToolResult::ok(result->content, {
            {"id", id_str},
            {"kind", result->kind},
            {"confidence", result->confidence},
            {"content", result->content}
        });
    }

    DuckDBToolResult tool_explore_neighbors(const json& params) {
        std::string node = params.value("node", "");
        if (node.empty()) {
            return DuckDBToolResult::error("Node is required");
        }

        std::string direction = params.value("direction", "both");

        json neighbors = json::array();
        std::ostringstream ss;
        ss << "Neighbors of '" << node << "':\n";

        // Outgoing: node → predicate → ?
        if (direction == "both" || direction == "outgoing") {
            auto outgoing = mind_->query_subject(node);
            for (const auto& [pred, obj, weight] : outgoing) {
                ss << "  → " << pred << " → " << obj << "\n";
                neighbors.push_back({
                    {"node", obj},
                    {"predicate", pred},
                    {"direction", "outgoing"},
                    {"weight", weight}
                });
            }
        }

        // Incoming: ? → predicate → node
        if (direction == "both" || direction == "incoming") {
            auto incoming = mind_->query_object(node);
            for (const auto& [subj, pred, weight] : incoming) {
                ss << "  " << subj << " → " << pred << " →\n";
                neighbors.push_back({
                    {"node", subj},
                    {"predicate", pred},
                    {"direction", "incoming"},
                    {"weight", weight}
                });
            }
        }

        return DuckDBToolResult::ok(ss.str(), {{"neighbors", neighbors}, {"count", neighbors.size()}});
    }

    DuckDBToolResult tool_connect(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string subject = params.value("subject", "");
        std::string predicate = params.value("predicate", "");
        std::string object = params.value("object", "");

        if (subject.empty() || predicate.empty() || object.empty()) {
            return DuckDBToolResult::error("Subject, predicate, and object are required");
        }

        bool ok = mind_->connect(subject, predicate, object);
        if (!ok) {
            return DuckDBToolResult::error("Failed to create triplet");
        }

        return DuckDBToolResult::ok(
            "Connected: " + subject + " → " + predicate + " → " + object,
            {{"subject", subject}, {"predicate", predicate}, {"object", object}}
        );
    }

    DuckDBToolResult tool_query_graph(const json& params) {
        std::string subject = params.value("subject", "");
        std::string object = params.value("object", "");

        json results_json = json::array();
        std::ostringstream ss;

        if (!subject.empty()) {
            auto results = mind_->query_subject(subject);
            ss << "Triplets with subject '" << subject << "':\n";
            for (const auto& [pred, obj, weight] : results) {
                ss << "  → " << pred << " → " << obj << "\n";
                results_json.push_back({
                    {"subject", subject},
                    {"predicate", pred},
                    {"object", obj},
                    {"weight", weight}
                });
            }
        }

        if (!object.empty()) {
            auto results = mind_->query_object(object);
            ss << "Triplets with object '" << object << "':\n";
            for (const auto& [subj, pred, weight] : results) {
                ss << "  " << subj << " → " << pred << " →\n";
                results_json.push_back({
                    {"subject", subj},
                    {"predicate", pred},
                    {"object", object},
                    {"weight", weight}
                });
            }
        }

        if (subject.empty() && object.empty()) {
            return DuckDBToolResult::error("Either subject or object is required");
        }

        return DuckDBToolResult::ok(ss.str(), {{"triplets", results_json}});
    }

    DuckDBToolResult tool_query_triplets_temporal(const json& params) {
        std::string subject = params.value("subject", "");
        std::string predicate = params.value("predicate", "");
        std::string object = params.value("object", "");
        std::string at_date = params.value("at_date", "");
        size_t limit = params.value("limit", 50);

        // Parse at_date to timestamp
        int64_t at_time_ms = 0;
        if (!at_date.empty()) {
            auto resolved = TemporalResolver::resolve(at_date, TemporalResolver::now_ms());
            if (resolved) {
                at_time_ms = resolved->timestamp_ms;
            } else {
                return DuckDBToolResult::error("Invalid date format: " + at_date);
            }
        }

        auto triplets = mind_->store().query_triplets_temporal(subject, predicate, object, at_time_ms, limit);

        // Track traversal for convergence metrics
        if (!triplets.empty()) {
            std::ostringstream upd;
            upd << "UPDATE triplet SET use_count = use_count + 1, last_used_at = "
                << TemporalResolver::now_ms() << " WHERE id IN (";
            for (size_t i = 0; i < triplets.size(); ++i) {
                if (i > 0) upd << ",";
                upd << triplets[i].id;
            }
            upd << ")";
            mind_->store().execute_raw(upd.str());
        }

        std::ostringstream ss;
        json results_json = json::array();

        if (at_time_ms > 0) {
            ss << "Triplets valid at " << TemporalResolver::format_iso_date(at_time_ms) << ":\n";
        } else {
            ss << "Current triplets:\n";
        }

        for (const auto& t : triplets) {
            ss << "  " << t.subject << " → " << t.predicate << " → " << t.object;
            if (t.valid_from_ms > 0) {
                ss << " (from " << TemporalResolver::format_iso_date(t.valid_from_ms);
                if (t.valid_to_ms > 0) {
                    ss << " to " << TemporalResolver::format_iso_date(t.valid_to_ms);
                }
                ss << ")";
            }
            ss << "\n";

            results_json.push_back({
                {"id", t.id},
                {"subject", t.subject},
                {"predicate", t.predicate},
                {"object", t.object},
                {"weight", t.weight},
                {"valid_from_ms", t.valid_from_ms},
                {"valid_to_ms", t.valid_to_ms},
                {"valid_from", t.valid_from_ms > 0 ? TemporalResolver::format_iso_date(t.valid_from_ms) : ""},
                {"valid_to", t.valid_to_ms > 0 ? TemporalResolver::format_iso_date(t.valid_to_ms) : ""}
            });
        }

        if (triplets.empty()) {
            ss << "  (no matching triplets found)\n";
        }

        return DuckDBToolResult::ok(ss.str(), {{"triplets", results_json}, {"count", triplets.size()}});
    }

    DuckDBToolResult tool_triplet_history(const json& params) {
        std::string subject = params.value("subject", "");
        std::string predicate = params.value("predicate", "");
        size_t limit = params.value("limit", 20);

        if (subject.empty() || predicate.empty()) {
            return DuckDBToolResult::error("Subject and predicate are required");
        }

        auto history = mind_->store().query_triplet_history(subject, predicate, limit);

        std::ostringstream ss;
        json results_json = json::array();

        ss << "History of " << subject << " → " << predicate << ":\n";

        for (const auto& t : history) {
            ss << "  " << t.object;
            if (t.valid_from_ms > 0) {
                ss << " (from " << TemporalResolver::format_iso_date(t.valid_from_ms);
                if (t.valid_to_ms > 0) {
                    ss << " to " << TemporalResolver::format_iso_date(t.valid_to_ms);
                } else {
                    ss << " to now";
                }
                ss << ")";
            }
            if (t.superseded_by > 0) {
                ss << " [superseded]";
            }
            ss << "\n";

            results_json.push_back({
                {"id", t.id},
                {"object", t.object},
                {"valid_from_ms", t.valid_from_ms},
                {"valid_to_ms", t.valid_to_ms},
                {"valid_from", t.valid_from_ms > 0 ? TemporalResolver::format_iso_date(t.valid_from_ms) : ""},
                {"valid_to", t.valid_to_ms > 0 ? TemporalResolver::format_iso_date(t.valid_to_ms) : ""},
                {"superseded_by", t.superseded_by}
            });
        }

        if (history.empty()) {
            ss << "  (no history found)\n";
        }

        return DuckDBToolResult::ok(ss.str(), {{"history", results_json}, {"count", history.size()}});
    }

    DuckDBToolResult tool_connect_temporal(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        std::string subject = params.value("subject", "");
        std::string predicate = params.value("predicate", "");
        std::string object = params.value("object", "");
        std::string valid_from = params.value("valid_from", "");
        std::string valid_to = params.value("valid_to", "");
        std::string context_date = params.value("context_date", "");

        if (subject.empty() || predicate.empty() || object.empty()) {
            return DuckDBToolResult::error("Subject, predicate, and object are required");
        }

        int64_t context_date_ms = TemporalResolver::now_ms();
        if (!context_date.empty()) {
            auto resolved = TemporalResolver::resolve(context_date, context_date_ms);
            if (resolved) {
                context_date_ms = resolved->timestamp_ms;
            }
        }

        int64_t valid_from_ms = 0;
        if (!valid_from.empty()) {
            auto resolved = TemporalResolver::resolve(valid_from, context_date_ms);
            if (resolved) {
                valid_from_ms = resolved->timestamp_ms;
            } else {
                return DuckDBToolResult::error("Invalid valid_from date: " + valid_from);
            }
        }

        int64_t valid_to_ms = 0;
        if (!valid_to.empty()) {
            auto resolved = TemporalResolver::resolve(valid_to, context_date_ms);
            if (resolved) {
                valid_to_ms = resolved->timestamp_ms;
            } else {
                return DuckDBToolResult::error("Invalid valid_to date: " + valid_to);
            }
        }

        bool ok = mind_->store().connect_temporal(
            subject, predicate, object, 1.0f,
            valid_from_ms, valid_to_ms, context_date_ms
        );

        if (!ok) {
            return DuckDBToolResult::error("Failed to create temporal triplet");
        }

        std::ostringstream ss;
        ss << "Connected: " << subject << " → " << predicate << " → " << object;
        if (valid_from_ms > 0) {
            ss << " (from " << TemporalResolver::format_iso_date(valid_from_ms);
            if (valid_to_ms > 0) {
                ss << " to " << TemporalResolver::format_iso_date(valid_to_ms);
            }
            ss << ")";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"subject", subject},
            {"predicate", predicate},
            {"object", object},
            {"valid_from_ms", valid_from_ms},
            {"valid_to_ms", valid_to_ms}
        });
    }

    DuckDBToolResult tool_convergence_metrics(const json&) {
        std::ostringstream ss;
        json metrics;

        // NULL-safe parsers (execute_sql_query serializes NULLs as "NULL")
        auto to_i = [](const std::string& s) -> int64_t {
            return (s.empty() || s == "NULL") ? 0 : std::stoll(s);
        };
        auto to_d = [](const std::string& s) -> double {
            return (s.empty() || s == "NULL") ? 0.0 : std::stod(s);
        };

        // --- Correction recall rate ---
        // Wisdom/distilled memories tagged correction/compliance: are they being recalled?
        auto corr = mind_->store().execute_sql_query(
            "SELECT COUNT(*) as total, "
            "SUM(CASE WHEN access_count = 0 THEN 1 ELSE 0 END) as never_recalled, "
            "AVG(access_count) as avg_access, "
            "SUM(CASE WHEN access_count >= 3 THEN 1 ELSE 0 END) as well_recalled "
            "FROM memory WHERE kind IN ('wisdom', 'distilled') "
            "AND (content LIKE '%CORRECTION%' OR content LIKE '%[correction%' "
            "  OR id IN (SELECT memory_id FROM memory_tags "
            "    WHERE tag IN ('correction', 'compliance', 'gotcha')))");

        int64_t total_corrections = 0, never_recalled = 0, well_recalled = 0;
        double avg_access = 0.0;
        if (corr.success && !corr.rows.empty() && corr.rows[0].size() >= 4) {
            const auto& r = corr.rows[0];
            total_corrections = to_i(r[0]);
            never_recalled    = to_i(r[1]);
            avg_access        = to_d(r[2]);
            well_recalled     = to_i(r[3]);
        }
        double recall_rate = total_corrections > 0
            ? 100.0 * (total_corrections - never_recalled) / total_corrections : 0.0;

        ss << "=== Correction Recall ===\n";
        ss << "  Total correction memories: " << total_corrections << "\n";
        ss << "  Never recalled:            " << never_recalled << "\n";
        ss << "  Well recalled (>=3x):      " << well_recalled << "\n";
        ss << "  Avg access count:          " << std::fixed << std::setprecision(1) << avg_access << "\n";
        ss << "  Recall rate:               " << std::fixed << std::setprecision(1) << recall_rate << "%\n\n";

        metrics["correction"] = {
            {"total", total_corrections},
            {"never_recalled", never_recalled},
            {"well_recalled", well_recalled},
            {"avg_access", avg_access},
            {"recall_rate_pct", recall_rate}
        };

        // --- Triplet traversal rate ---
        auto trav = mind_->store().execute_sql_query(
            "SELECT COUNT(*) as total, "
            "SUM(CASE WHEN use_count > 0 THEN 1 ELSE 0 END) as traversed, "
            "MAX(use_count) as max_use, "
            "AVG(CASE WHEN use_count > 0 THEN CAST(use_count AS DOUBLE) ELSE NULL END) as avg_active_use "
            "FROM triplet WHERE valid_to_ms = 0");

        int64_t total_triplets = 0, traversed = 0, max_use = 0;
        double avg_active_use = 0.0;
        if (trav.success && !trav.rows.empty() && trav.rows[0].size() >= 4) {
            const auto& r = trav.rows[0];
            total_triplets = to_i(r[0]);
            traversed      = to_i(r[1]);
            max_use        = to_i(r[2]);
            avg_active_use = to_d(r[3]);
        }
        double traversal_rate = total_triplets > 0
            ? 100.0 * traversed / total_triplets : 0.0;

        ss << "=== Triplet Traversal ===\n";
        ss << "  Active triplets:           " << total_triplets << "\n";
        ss << "  Ever traversed:            " << traversed << "\n";
        ss << "  Traversal rate:            " << std::fixed << std::setprecision(2) << traversal_rate << "%\n";
        ss << "  Max traversals (one edge): " << max_use << "\n";
        ss << "  Avg traversals (active):   " << std::fixed << std::setprecision(1) << avg_active_use << "\n\n";

        metrics["triplets"] = {
            {"total_active", total_triplets},
            {"traversed", traversed},
            {"traversal_rate_pct", traversal_rate},
            {"max_use", max_use},
            {"avg_active_use", avg_active_use}
        };

        // --- Top traversed triplets ---
        auto top = mind_->store().execute_sql_query(
            "SELECT subject, predicate, object, use_count FROM triplet "
            "WHERE use_count > 0 AND valid_to_ms = 0 "
            "ORDER BY use_count DESC LIMIT 10");

        ss << "=== Most Traversed Edges ===\n";
        json top_json = json::array();
        if (top.success) {
            for (const auto& row : top.rows) {
                if (row.size() < 4) continue;
                ss << "  [" << row[3] << "x] " << row[0] << " -> " << row[1] << " -> " << row[2] << "\n";
                top_json.push_back({
                    {"subject", row[0]}, {"predicate", row[1]},
                    {"object", row[2]},  {"use_count", std::stoll(row[3])}
                });
            }
        }
        if (top_json.empty()) ss << "  (none yet -- traversal tracking just started)\n";

        // --- Convergence signal ---
        ss << "\n=== Signal ===\n";
        bool converging = recall_rate >= 60.0 && traversal_rate >= 1.0;
        ss << "  " << (converging ? "CONVERGING" : "DRIFTING") << " -- "
           << "corrections recalled " << std::fixed << std::setprecision(0) << recall_rate << "%, "
           << "graph explored " << std::fixed << std::setprecision(2) << traversal_rate << "%\n";

        metrics["signal"] = converging ? "converging" : "drifting";
        metrics["top_traversed"] = top_json;

        return DuckDBToolResult::ok(ss.str(), metrics);
    }

    DuckDBToolResult tool_resonance_stats(const json&) {
        auto stats = mind_->get_learning_stats();
        auto best  = mind_->learner().get_best_params();

        std::ostringstream ss;
        ss << "=== ResonanceLearner State ===\n";
        ss << "  Learning enabled: " << (mind_->is_learning_enabled() ? "yes" : "no") << "\n";
        ss << "  Total feedback:   " << stats.total_feedback << "\n";
        ss << "  Positive:         " << stats.positive_feedback << "\n";
        ss << "  Negative:         " << stats.negative_feedback << "\n";
        ss << "  Avg reward:       " << std::fixed << std::setprecision(3) << stats.avg_reward << "\n\n";

        ss << "=== Parameter Posteriors (Beta distribution means ± std dev) ===\n";
        for (const auto& [name, mean] : stats.param_means) {
            double uncertainty = 0.0;
            auto it = stats.param_uncertainties.find(name);
            if (it != stats.param_uncertainties.end()) uncertainty = it->second;
            ss << "  " << std::left << std::setw(26) << name
               << " mean=" << std::fixed << std::setprecision(3) << mean
               << "  ±" << std::fixed << std::setprecision(3) << uncertainty << "\n";
        }

        ss << "\n=== Current Best Config (posterior exploitation) ===\n";
        ss << "  spread_strength:      " << std::fixed << std::setprecision(3) << best.spread_strength << "\n";
        ss << "  spread_decay:         " << std::fixed << std::setprecision(3) << best.spread_decay << "\n";
        ss << "  hebbian_strength:     " << std::fixed << std::setprecision(3) << best.hebbian_strength << "\n";
        ss << "  basin_boost:          " << std::fixed << std::setprecision(3) << best.basin_boost << "\n";
        ss << "  similarity_threshold: " << std::fixed << std::setprecision(3) << best.similarity_threshold << "\n";
        ss << "  inhibition_strength:  " << std::fixed << std::setprecision(3) << best.inhibition_strength << "\n";
        ss << "  semantic_weight:      " << std::fixed << std::setprecision(3) << best.semantic_weight << "\n";
        ss << "  activation_weight:    " << std::fixed << std::setprecision(3) << best.activation_weight << "\n";

        if (stats.total_feedback == 0) {
            ss << "\nNOTE: No feedback received yet — learner is using uniform Beta(1,1) priors.\n"
               << "Call strengthen/weaken on memory IDs to start training the posterior.\n";
        }

        json result;
        result["learning_enabled"] = mind_->is_learning_enabled();
        result["feedback"] = {
            {"total",    (int64_t)stats.total_feedback},
            {"positive", (int64_t)stats.positive_feedback},
            {"negative", (int64_t)stats.negative_feedback},
            {"avg_reward", stats.avg_reward}
        };
        result["param_means"]         = stats.param_means;
        result["param_uncertainties"] = stats.param_uncertainties;
        result["best_config"] = {
            {"spread_strength",      best.spread_strength},
            {"spread_decay",         best.spread_decay},
            {"hebbian_strength",     best.hebbian_strength},
            {"basin_boost",          best.basin_boost},
            {"similarity_threshold", best.similarity_threshold},
            {"inhibition_strength",  best.inhibition_strength},
            {"semantic_weight",      best.semantic_weight},
            {"activation_weight",    best.activation_weight}
        };

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_soul_context(const json&) {
        auto cached = mind_->store().cached_health();

        // Query actual relationship state (not just stats)
        size_t preferences = 0, corrections = 0, insights = 0, solutions = 0;
        size_t wisdom_nodes = 0, beliefs = 0, episodes = 0;
        float strongest_conf = 0.0f;
        std::string strongest_memory;

        auto type_counts = mind_->store().execute_sql_query(
            "SELECT "
            "  SUM(CASE WHEN content LIKE '[preference%' OR content LIKE '[pref]%' THEN 1 ELSE 0 END) as prefs, "
            "  SUM(CASE WHEN content LIKE '[correction%' OR content LIKE '[gotcha%' THEN 1 ELSE 0 END) as corrections, "
            "  SUM(CASE WHEN content LIKE '[insight%' THEN 1 ELSE 0 END) as insights, "
            "  SUM(CASE WHEN content LIKE '[solution%' OR content LIKE '[sol]%' THEN 1 ELSE 0 END) as solutions, "
            "  SUM(CASE WHEN kind = 'wisdom' OR kind = 'distilled' THEN 1 ELSE 0 END) as wisdom, "
            "  SUM(CASE WHEN kind = 'belief' THEN 1 ELSE 0 END) as beliefs, "
            "  SUM(CASE WHEN kind = 'episode' THEN 1 ELSE 0 END) as episodes "
            "FROM memory WHERE confidence > 0.01"
        );
        if (type_counts.success && !type_counts.rows.empty()) {
            const auto& r = type_counts.rows[0];
            if (r.size() >= 7) {
                preferences = r[0].empty() ? 0 : std::stoull(r[0]);
                corrections = r[1].empty() ? 0 : std::stoull(r[1]);
                insights = r[2].empty() ? 0 : std::stoull(r[2]);
                solutions = r[3].empty() ? 0 : std::stoull(r[3]);
                wisdom_nodes = r[4].empty() ? 0 : std::stoull(r[4]);
                beliefs = r[5].empty() ? 0 : std::stoull(r[5]);
                episodes = r[6].empty() ? 0 : std::stoull(r[6]);
            }
        }

        // Get strongest memory (highest confidence, most accessed)
        auto top = mind_->store().execute_sql_query(
            "SELECT content, confidence FROM memory WHERE confidence > 0.5 "
            "ORDER BY confidence DESC, accessed_at DESC LIMIT 1"
        );
        if (top.success && !top.rows.empty() && top.rows[0].size() >= 2) {
            strongest_memory = top.rows[0][0];
            if (strongest_memory.size() > 80) strongest_memory = strongest_memory.substr(0, 80) + "...";
            strongest_conf = std::stof(top.rows[0][1]);
        }

        // Get indexed projects
        auto projects = mind_->store().execute_sql_query(
            "SELECT project, COUNT(*) as files FROM code_file GROUP BY project ORDER BY files DESC"
        );
        json projects_json = json::array();
        if (projects.success) {
            for (const auto& row : projects.rows) {
                if (row.size() >= 2) {
                    projects_json.push_back({{"name", row[0]}, {"files", std::stoi(row[1])}});
                }
            }
        }

        std::ostringstream ss;
        ss << "Soul State:\n";
        ss << "  Partnership: " << preferences << " preferences, "
           << corrections << " corrections, " << insights << " insights, "
           << solutions << " solutions\n";
        ss << "  Memory: " << wisdom_nodes << " wisdom, " << beliefs << " beliefs, "
           << episodes << " episodes (" << cached.total_memories << " total)\n";
        ss << "  Confidence: " << std::fixed << std::setprecision(2) << cached.avg_confidence << " avg\n";
        if (!strongest_memory.empty()) {
            ss << "  Strongest: [" << std::setprecision(0) << (strongest_conf * 100) << "%] "
               << strongest_memory << "\n";
        }
        ss << "  Code: " << cached.total_symbols << " symbols, "
           << cached.total_triplets << " triplets";
        if (!projects_json.empty()) {
            ss << " across " << projects_json.size() << " project"
               << (projects_json.size() > 1 ? "s" : "");
        }
        ss << "\n";
        ss << "  Yantra: " << (mind_->has_yantra() ? "ready" : "not attached") << "\n";
        ss << "  Status: " << (cached.is_open ? "OK" : "ERROR");

        return DuckDBToolResult::ok(ss.str(), {
            {"version", CHITTA_VERSION},
            {"partnership", {
                {"preferences", preferences},
                {"corrections", corrections},
                {"insights", insights},
                {"solutions", solutions}
            }},
            {"memory", {
                {"wisdom", wisdom_nodes},
                {"beliefs", beliefs},
                {"episodes", episodes},
                {"total", cached.total_memories},
                {"avg_confidence", cached.avg_confidence}
            }},
            {"code", {
                {"symbols", cached.total_symbols},
                {"triplets", cached.total_triplets},
                {"projects", projects_json}
            }},
            {"yantra_ready", mind_->has_yantra()},
            {"status", cached.is_open ? "OK" : "ERROR"}
        });
    }

    DuckDBToolResult tool_enrichment_status(const json&) {
        size_t total_symbols = mind_->store().count_total_symbols();
        size_t pending = mind_->store().count_undescribed_symbols();
        size_t described_symbols = total_symbols - pending;
        float coverage = total_symbols > 0 ? (float)described_symbols / total_symbols * 100.0f : 0.0f;

        std::ostringstream ss;
        ss << "Code Enrichment Status:\n";
        ss << "  Total symbols: " << total_symbols << "\n";
        ss << "  Described: " << described_symbols << "\n";
        ss << "  Pending: " << pending << "\n";
        ss << "  Coverage: " << std::fixed << std::setprecision(1) << coverage << "%\n";

        if (pending > 0 && described_symbols > 0) {
            // Dynamic estimate based on current rate (symbols per minute)
            // Assume ~20 symbols per minute at batch=20, interval=1m
            size_t rate_per_min = 20;  // Conservative estimate
            size_t minutes_remaining = pending / rate_per_min;
            size_t hours = minutes_remaining / 60;
            size_t mins = minutes_remaining % 60;
            ss << "  Est. remaining: ~" << hours << "h " << mins << "m (at " << rate_per_min << "/min)\n";
        } else if (pending > 0) {
            ss << "  Est. remaining: calculating...\n";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"total_symbols", total_symbols},
            {"described", described_symbols},
            {"pending", pending},
            {"coverage_percent", coverage}
        });
    }

    DuckDBToolResult tool_describe_symbol(const json& params) {
        int64_t symbol_id = params.value("symbol_id", static_cast<int64_t>(0));
        std::string description = params.value("description", "");

        if (symbol_id == 0) {
            return DuckDBToolResult::error("symbol_id is required");
        }
        if (description.empty()) {
            return DuckDBToolResult::error("description is required");
        }

        bool success = mind_->store().set_symbol_description(symbol_id, description);
        if (!success) {
            return DuckDBToolResult::error("Failed to set symbol description");
        }

        return DuckDBToolResult::ok("Symbol description set", {
            {"symbol_id", symbol_id},
            {"description_length", description.size()}
        });
    }

    DuckDBToolResult tool_cleanup_code_wisdom(const json& params) {
        // Migration tool: delete [code] wisdom memories and clear orphaned symbol.memory_id
        // Accept both hyphen and underscore conventions
        bool dry_run = params.value("dry_run", params.value("dry-run", true));

        // Count what would be deleted
        auto count_result = mind_->store().raw_query(
            "SELECT COUNT(*) FROM memory WHERE kind = 'wisdom' AND content LIKE '[code]%'");
        size_t wisdom_count = 0;
        if (count_result && !count_result->HasError()) {
            auto chunk = count_result->Fetch();
            if (chunk && chunk->size() > 0) {
                wisdom_count = chunk->GetValue(0, 0).GetValue<int64_t>();
            }
        }

        // Count orphaned memory_id references
        auto orphan_result = mind_->store().raw_query(
            "SELECT COUNT(*) FROM symbol WHERE memory_id IS NOT NULL AND memory_id NOT IN (SELECT id FROM memory)");
        size_t orphan_count = 0;
        if (orphan_result && !orphan_result->HasError()) {
            auto chunk = orphan_result->Fetch();
            if (chunk && chunk->size() > 0) {
                orphan_count = chunk->GetValue(0, 0).GetValue<int64_t>();
            }
        }

        if (dry_run) {
            std::ostringstream ss;
            ss << "Cleanup preview (dry_run=true):\n";
            ss << "  [code] wisdom memories to delete: " << wisdom_count << "\n";
            ss << "  Orphaned symbol.memory_id to clear: " << orphan_count << "\n";
            ss << "\nRun with dry_run=false to execute.";
            return DuckDBToolResult::ok(ss.str(), {
                {"dry_run", true},
                {"wisdom_to_delete", wisdom_count},
                {"orphans_to_clear", orphan_count}
            });
        }

        // Execute cleanup
        bool ok1 = mind_->store().execute_raw(
            "DELETE FROM memory WHERE kind = 'wisdom' AND content LIKE '[code]%'");
        bool ok2 = mind_->store().execute_raw(
            "UPDATE symbol SET memory_id = NULL WHERE memory_id IS NOT NULL AND memory_id NOT IN (SELECT id FROM memory)");

        if (!ok1 || !ok2) {
            return DuckDBToolResult::error("Cleanup failed");
        }

        std::ostringstream ss;
        ss << "Cleanup complete:\n";
        ss << "  [code] wisdom memories deleted: " << wisdom_count << "\n";
        ss << "  Orphaned symbol.memory_id cleared: " << orphan_count << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"dry_run", false},
            {"wisdom_deleted", wisdom_count},
            {"orphans_cleared", orphan_count}
        });
    }

    DuckDBToolResult tool_subconscious_stats(const json&) {
        if (!subconscious_) {
            return DuckDBToolResult::ok("Subconscious not attached", {{"attached", false}});
        }

        const auto& stats = subconscious_->stats();
        const auto& config = subconscious_->config();

        // Calculate uptime
        int64_t uptime_ms = 0;
        if (stats.started_at > 0) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            uptime_ms = now - stats.started_at;
        }
        int64_t uptime_mins = uptime_ms / 60000;

        std::ostringstream ss;
        ss << "Subconscious Status:\n";
        ss << "  Running: " << (subconscious_->is_running() ? "yes" : "no") << "\n";
        ss << "  Uptime: " << uptime_mins << " minutes\n";
        ss << "\nEvents:\n";
        ss << "  Processed: " << stats.events_processed.load() << "\n";
        ss << "\nPattern Detection:\n";
        ss << "  Corrections: " << stats.corrections_detected.load() << "\n";
        ss << "  Preferences: " << stats.preferences_detected.load() << "\n";
        ss << "  Frustrations: " << stats.frustrations_detected.load() << "\n";
        ss << "  Milestones: " << stats.milestones_detected.load() << "\n";
        ss << "\nFeedback Loops:\n";
        ss << "  Suggestions tracked: " << stats.suggestions_tracked.load() << "\n";
        ss << "  Outcomes verified: " << stats.outcomes_verified.load() << "\n";
        ss << "\nMaintenance:\n";
        ss << "  Hygiene runs: " << stats.hygiene_runs.load() << "\n";
        if (stats.last_hygiene_at > 0) {
            int64_t mins_since_hygiene = (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count() - stats.last_hygiene_at) / 60000;
            ss << "  Last hygiene: " << mins_since_hygiene << " minutes ago\n";
        }
        ss << "\nConfig:\n";
        ss << "  Hygiene enabled: " << (config.enable_hygiene ? "yes" : "no") << "\n";
        ss << "  Anticipation enabled: " << (config.enable_anticipation ? "yes" : "no") << "\n";
        ss << "  Pattern detection enabled: " << (config.enable_pattern_detection ? "yes" : "no") << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"running", subconscious_->is_running()},
            {"uptime_minutes", uptime_mins},
            {"events_processed", stats.events_processed.load()},
            {"corrections_detected", stats.corrections_detected.load()},
            {"preferences_detected", stats.preferences_detected.load()},
            {"frustrations_detected", stats.frustrations_detected.load()},
            {"milestones_detected", stats.milestones_detected.load()},
            {"suggestions_tracked", stats.suggestions_tracked.load()},
            {"outcomes_verified", stats.outcomes_verified.load()},
            {"hygiene_runs", stats.hygiene_runs.load()},
            {"last_hygiene_at", stats.last_hygiene_at.load()}
        });
    }

    DuckDBToolResult tool_reembed_memories(const json& params) {
        if (!mind_->has_yantra()) {
            return DuckDBToolResult::error("Yantra (embedder) not attached");
        }

        std::string kind_filter = params.value("kind", "");
        float min_confidence = params.value("min_confidence", 0.0f);
        int limit = params.value("limit", 30);
        bool dry_run = params.value("dry_run", false);
        bool all = params.value("all", false);  // Re-embed ALL memories, not just global

        std::vector<std::pair<int64_t, std::string>> to_reembed;
        size_t total_checked = 0;

        if (all) {
            // Query ALL memories with NULL embeddings directly
            std::ostringstream sql;
            sql << "SELECT id, COALESCE(content, '') FROM memory WHERE embedding IS NULL";
            if (!kind_filter.empty()) {
                std::string escaped = kind_filter;
                for (size_t pos = 0; (pos = escaped.find('\'', pos)) != std::string::npos; pos += 2) {
                    escaped.replace(pos, 1, "''");
                }
                sql << " AND kind = '" << escaped << "'";
            }
            if (min_confidence > 0) {
                sql << " AND confidence >= " << min_confidence;
            }
            sql << " ORDER BY confidence DESC LIMIT " << limit;

            auto result = mind_->store().raw_query(sql.str());
            if (result) {
                while (auto chunk = result->Fetch()) {
                    for (size_t i = 0; i < chunk->size(); ++i) {
                        int64_t id = chunk->GetValue(0, i).GetValue<int64_t>();
                        std::string content = chunk->GetValue(1, i).ToString();
                        if (!content.empty()) {
                            to_reembed.push_back({id, content});
                        }
                        total_checked++;
                    }
                }
            }
        } else {
            // Original behavior: only global memories
            auto memories = mind_->store().list_global_memories(limit, kind_filter);
            total_checked = memories.size();

            for (const auto& mem : memories) {
                if (min_confidence > 0 && mem.confidence < min_confidence) continue;

                // Check if memory has meaningful embedding by recalling it
                auto recalls = mind_->store().recall(
                    mind_->embedder().transform(mem.content).nu.data,
                    5, "", true
                );

                bool found_self = false;
                for (const auto& r : recalls) {
                    if (r.id == mem.id && r.similarity > 0.9f) {
                        found_self = true;
                        break;
                    }
                }

                if (!found_self) {
                    to_reembed.push_back({mem.id, mem.content});
                }
            }
        }

        size_t zero_embed_count = to_reembed.size();

        if (dry_run) {
            std::ostringstream ss;
            ss << "Dry run - found " << zero_embed_count << " memories likely needing re-embedding out of "
               << total_checked << " checked.\n";
            if (!to_reembed.empty()) {
                ss << "\nWould re-embed:\n";
                for (size_t i = 0; i < std::min(to_reembed.size(), size_t(10)); ++i) {
                    ss << "  #" << to_reembed[i].first << ": "
                       << to_reembed[i].second.substr(0, 60) << "...\n";
                }
                if (to_reembed.size() > 10) {
                    ss << "  ... and " << (to_reembed.size() - 10) << " more\n";
                }
            }
            return DuckDBToolResult::ok(ss.str(), {
                {"dry_run", true},
                {"total_checked", total_checked},
                {"zero_embed_count", zero_embed_count}
            });
        }

        // Actually re-embed
        size_t reembedded = 0;
        size_t failed = 0;

        for (const auto& [id, content] : to_reembed) {
            try {
                // Generate new embedding
                Artha artha = mind_->embedder().transform(content);

                // Update the memory with new embedding
                if (mind_->store().set_memory_embedding(id, artha.nu.data)) {
                    reembedded++;
                } else {
                    failed++;
                }
            } catch (...) {
                failed++;
            }
        }

        std::ostringstream ss;
        ss << "Re-embedded " << reembedded << " memories";
        if (failed > 0) {
            ss << " (" << failed << " failed)";
        }
        ss << " out of " << zero_embed_count << " needing re-embedding.";

        return DuckDBToolResult::ok(ss.str(), {
            {"total_checked", total_checked},
            {"zero_embed_count", zero_embed_count},
            {"reembedded", reembedded},
            {"failed", failed}
        });
    }

    DuckDBToolResult tool_embed_symbols(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        if (!mind_->has_yantra()) {
            return DuckDBToolResult::error("Yantra (embedder) not attached");
        }

        // Reset all embeddings if requested (for re-embedding with richer text)
        bool reset = params.value("reset", false);
        size_t purged = 0;
        if (reset) {
            // Clear all from separate embeddings DB
            purged = mind_->store().clear_symbol_embeddings();
            // Clear main DB embeddings and described_at
            mind_->store().execute_raw(
                "UPDATE symbol SET embedding = NULL, described_at = 0 "
                "WHERE embedding IS NOT NULL OR described_at > 0");
        }

        size_t batch_size = params.value("batch_size", 100);
        auto symbols = mind_->store().get_unembedded_symbols(batch_size);

        if (symbols.empty()) {
            return DuckDBToolResult::ok("All symbols embedded", {{"embedded", 0}, {"remaining", 0}});
        }

        size_t embedded = 0;
        auto start = std::chrono::steady_clock::now();

        // Pre-fetch triplet members for classes/structs (contains predicate)
        // to enrich embedding text with member names
        std::unordered_map<std::string, std::vector<std::string>> class_members;
        {
            auto members_result = mind_->store().execute_sql_query(
                "SELECT subject, object FROM triplet WHERE predicate = 'contains' "
                "AND subject IN (SELECT DISTINCT subject FROM triplet WHERE predicate = 'contains')");
            if (members_result.success) {
                for (const auto& row : members_result.rows) {
                    if (row.size() >= 2) {
                        // Extract leaf name from object for cleaner text
                        std::string leaf = row[1];
                        size_t cpos = leaf.rfind(':');
                        if (cpos != std::string::npos && cpos + 1 < leaf.size())
                            leaf = leaf.substr(cpos + 1);
                        class_members[row[0]].push_back(leaf);
                    }
                }
            }
        }

        for (const auto& sym : symbols) {
            // Build rich searchable text from metadata + context
            std::string disp = display_path(sym.file_path);

            std::ostringstream text;
            text << sym.kind << " " << sym.name;
            text << " in " << disp;

            if (!sym.signature.empty() && sym.signature != sym.name) {
                text << ": " << sym.signature;
            }

            // For classes/structs, append member names for richer semantics
            if (sym.kind == "class" || sym.kind == "struct" || sym.kind == "interface") {
                // Build lowercase key to match triplet subjects (connect_batch lowercases)
                std::string lower_name;
                for (char c : sym.name) {
                    lower_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                // Try both the raw name and common triplet key patterns
                for (const auto& key : {lower_name, sym.kind + ":" + lower_name}) {
                    auto it = class_members.find(key);
                    if (it != class_members.end() && !it->second.empty()) {
                        text << " { ";
                        size_t count = 0;
                        for (const auto& member : it->second) {
                            if (count++ > 0) text << ", ";
                            if (count > 8) { text << "..."; break; }  // Cap at 8 members
                            text << member;
                        }
                        text << " }";
                        break;
                    }
                }
            }

            // Embed using Yantra
            auto artha = mind_->embedder().transform(text.str());
            if (!artha.nu.is_zero()) {
                if (mind_->store().set_symbol_embedding(sym.id, artha.nu.data)) {
                    embedded++;
                }
            }
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        float rate = ms > 0 ? (float)embedded * 1000.0f / ms : 0;

        size_t remaining = mind_->store().count_unembedded_symbols();

        std::ostringstream ss;
        if (purged > 0) {
            ss << "Purged " << purged << " zero-vector embeddings\n";
        }
        ss << "Embedded " << embedded << " symbols in " << ms << "ms";
        ss << " (" << std::fixed << std::setprecision(1) << rate << "/sec)\n";
        ss << "Remaining: " << remaining;

        json result = {
            {"embedded", embedded},
            {"remaining", remaining},
            {"elapsed_ms", ms},
            {"rate_per_sec", rate}
        };
        if (purged > 0) result["purged_zeros"] = purged;
        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_dedupe_symbols(const json& /*params*/) {
        // Delete duplicate symbols, keeping the one with lowest id
        size_t before = mind_->store().count_total_symbols();

        // Use a subquery to find duplicates and delete them
        std::string sql = R"(
            DELETE FROM symbol
            WHERE id NOT IN (
                SELECT MIN(id) FROM symbol
                GROUP BY kind, name, file_path, line_start
            )
        )";

        if (!mind_->store().execute_raw(sql)) {
            return DuckDBToolResult::error("Failed to dedupe symbols");
        }

        size_t after = mind_->store().count_total_symbols();
        size_t removed = before - after;

        // Clean orphaned embeddings left behind by deleted duplicates
        size_t orphans_cleaned = mind_->store().clean_orphaned_symbol_embeddings();

        std::ostringstream ss;
        ss << "Removed " << removed << " duplicate symbols\n";
        ss << "Before: " << before << ", After: " << after;
        if (orphans_cleaned > 0) {
            ss << "\nCleaned " << orphans_cleaned << " orphaned embeddings";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"before", before},
            {"after", after},
            {"removed", removed},
            {"orphaned_embeddings_cleaned", orphans_cleaned}
        });
    }

    DuckDBToolResult tool_migrate_vss(const json& /*params*/) {
        // Migrate embeddings from main DB to separate VSS database
        size_t migrated = mind_->store().migrate_embeddings_to_vss();

        std::ostringstream ss;
        ss << "Migrated " << migrated << " embeddings to VSS database\n";
        ss << "HNSW index is now isolated from main database";

        return DuckDBToolResult::ok(ss.str(), {
            {"migrated", migrated}
        });
    }

    DuckDBToolResult tool_strengthen(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        auto [db_id, id_str] = parse_id(params);
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        NodeId id;
        id.low = static_cast<uint64_t>(db_id);
        float amount = params.value("amount", 0.1f);

        if (!mind_->strengthen(id, amount)) {
            return DuckDBToolResult::error("Node not found");
        }

        return DuckDBToolResult::ok("Strengthened node " + id_str);
    }

    DuckDBToolResult tool_weaken(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        auto [db_id, id_str] = parse_id(params);
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        NodeId id;
        id.low = static_cast<uint64_t>(db_id);
        float amount = params.value("amount", 0.1f);

        if (!mind_->weaken(id, amount)) {
            return DuckDBToolResult::error("Node not found");
        }

        return DuckDBToolResult::ok("Weakened node " + id_str);
    }

    DuckDBToolResult tool_forget(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        auto [db_id, id_str] = parse_id(params);
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        NodeId id;
        id.low = static_cast<uint64_t>(db_id);
        if (!mind_->remove(id)) {
            return DuckDBToolResult::error("Node not found");
        }

        return DuckDBToolResult::ok("Forgot node " + id_str);
    }

    DuckDBToolResult tool_batch_forget(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::vector<std::string> ids_to_delete;
        size_t deleted = 0;
        size_t not_found = 0;

        // Mode 1: Delete by explicit IDs
        if (params.contains("ids") && params["ids"].is_array()) {
            for (const auto& id_val : params["ids"]) {
                if (id_val.is_string()) {
                    ids_to_delete.push_back(id_val.get<std::string>());
                }
            }
        }

        // Mode 2: Delete by pattern (search and delete matching)
        if (params.contains("pattern") && params["pattern"].is_string()) {
            std::string pattern = params["pattern"].get<std::string>();
            auto results = mind_->recall(pattern, 100);  // Find up to 100 matching
            for (const auto& r : results) {
                // Check if content contains the pattern (case-insensitive would be better)
                if (r.text.find(pattern) != std::string::npos) {
                    ids_to_delete.push_back(r.id.to_string());
                }
            }
        }

        if (ids_to_delete.empty()) {
            return DuckDBToolResult::error("No IDs provided (use 'ids' array or 'pattern' string)");
        }

        // Delete each one
        for (const auto& id_str : ids_to_delete) {
            NodeId nid = NodeId::from_string(id_str);
            if (mind_->remove(nid)) {
                deleted++;
            } else {
                not_found++;
            }
        }

        std::ostringstream ss;
        ss << "Batch forget complete: " << deleted << " deleted";
        if (not_found > 0) {
            ss << ", " << not_found << " not found";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"deleted", deleted},
            {"not_found", not_found},
            {"total_requested", ids_to_delete.size()}
        });
    }

    DuckDBToolResult tool_observe(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string title = params.value("title", "");
        std::string content = params.value("content", "");
        std::string category = params.value("category", "wisdom");

        if (title.empty() || content.empty()) {
            return DuckDBToolResult::error("Title and content are required");
        }

        // Derive confidence from category (or use explicit override)
        float confidence = params.contains("confidence")
            ? params.value("confidence", 0.8f)
            : category_to_confidence(category);

        // Map category to NodeType
        NodeType type = NodeType::Wisdom;
        if (category == "episode") type = NodeType::Episode;
        else if (category == "belief") type = NodeType::Belief;
        // correction, preference, solution, decision, failure, wisdom, insight all use Wisdom

        std::string full_text = title + "\n" + content;
        NodeId id = mind_->remember(full_text, type, "brahman", RealmVisibility::Private, confidence);

        static const NodeId null_id{};
        if (id == null_id) {
            return DuckDBToolResult::error("Failed to observe");
        }

        // Process tags if provided
        std::string tags = params.value("tags", "");
        if (!tags.empty()) {
            int64_t db_id = static_cast<int64_t>(id.low);
            // Split comma-separated tags
            std::istringstream tag_stream(tags);
            std::string tag;
            while (std::getline(tag_stream, tag, ',')) {
                // Trim whitespace
                while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.front()))) tag.erase(tag.begin());
                while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.back()))) tag.pop_back();
                if (!tag.empty()) {
                    mind_->store().add_tag(db_id, tag);
                }
            }
        }

        return DuckDBToolResult::ok(
            "Observed: " + title.substr(0, 50),
            {{"id", id.to_string()}, {"category", category}, {"confidence", confidence}}
        );
    }

    // Extract significant words from query for graph lookup
    static std::vector<std::string> extract_terms(const std::string& query) {
        std::vector<std::string> terms;
        std::istringstream iss(query);
        std::string word;
        while (iss >> word) {
            // Remove punctuation and convert to lowercase
            std::string clean;
            for (char c : word) {
                if (std::isalnum(c)) clean += std::tolower(c);
            }
            // Keep words with 3+ chars, skip common words
            if (clean.length() >= 3 &&
                clean != "the" && clean != "and" && clean != "for" &&
                clean != "that" && clean != "with" && clean != "how" &&
                clean != "what" && clean != "does" && clean != "can") {
                terms.push_back(clean);
            }
        }
        return terms;
    }

    DuckDBToolResult tool_full_resonate(const json& params) {
        // Notify subconscious that we're handling a query (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string query = params.value("query", "");
        if (query.empty()) {
            return DuckDBToolResult::error("Query is required");
        }

        size_t k = params.value("k", 10);
        std::string realm = params.value("realm", "");
        bool include_global = params.value("include_global", true);

        // Parse exclude_kinds parameter (accept both underscore and hyphen conventions)
        std::vector<std::string> exclude_kinds;
        const json* kinds_array = nullptr;
        if (params.contains("exclude_kinds") && params["exclude_kinds"].is_array()) {
            kinds_array = &params["exclude_kinds"];
        } else if (params.contains("exclude-kinds") && params["exclude-kinds"].is_array()) {
            kinds_array = &params["exclude-kinds"];
        } else if (params.contains("exclude-kinds") && params["exclude-kinds"].is_string()) {
            // CLI passes comma-separated string
            std::string kinds_str = params["exclude-kinds"].get<std::string>();
            std::istringstream iss(kinds_str);
            std::string kind;
            while (std::getline(iss, kind, ',')) {
                // Trim whitespace
                size_t start = kind.find_first_not_of(" \t");
                size_t end = kind.find_last_not_of(" \t");
                if (start != std::string::npos && end != std::string::npos) {
                    exclude_kinds.push_back(kind.substr(start, end - start + 1));
                }
            }
        }
        if (kinds_array) {
            for (const auto& kind : *kinds_array) {
                if (kind.is_string()) {
                    exclude_kinds.push_back(kind.get<std::string>());
                }
            }
        }

        // partnership_only flag: exclude all code intel kinds and code-tagged wisdom
        // Accept both underscore (JSON convention) and hyphen (CLI convention)
        bool partnership_only = params.value("partnership_only", false) ||
                                params.value("partnership-only", false);
        if (partnership_only) {
            exclude_kinds = {"symbol", "projectessence", "modulestate", "patternstate"};
        }

        // PARTNERSHIP FIRST: Query partnership memories (beliefs, preferences) separately
        // These are the memories that make Claude feel personalized
        std::vector<Recall> partnership_results;
        if (mind_->embedder_ready()) {
            Artha artha = mind_->embedder().transform_query(query);
            // Get global partnership memories directly
            auto globals = mind_->store().list_global_memories(k, "");
            for (const auto& mem : globals) {
                // Calculate similarity if we have embeddings
                auto recalls = mind_->store().recall(artha.nu.data, 50, "", true);
                for (const auto& r : recalls) {
                    if (r.id == mem.id) {
                        Recall recall;
                        recall.id.high = 0;
                        recall.id.low = static_cast<uint64_t>(r.id);
                        recall.text = r.content;
                        recall.similarity = r.similarity;
                        recall.relevance = r.similarity * r.confidence * 1.5f;  // 1.5x boost for partnership
                        // Inline string_to_node_type
                        if (r.kind == "belief") recall.type = NodeType::Belief;
                        else if (r.kind == "wisdom") recall.type = NodeType::Wisdom;
                        else if (r.kind == "episode") recall.type = NodeType::Episode;
                        else if (r.kind == "intention") recall.type = NodeType::Intention;
                        else recall.type = NodeType::Episode;
                        recall.confidence = Confidence(r.confidence);
                        partnership_results.push_back(recall);
                        break;
                    }
                }
            }
            // Sort by boosted relevance
            std::sort(partnership_results.begin(), partnership_results.end(),
                [](const Recall& a, const Recall& b) { return a.relevance > b.relevance; });
        }

        // Use full resonance architecture for general memories:
        // 1. Session Priming - context biases retrieval
        // 2. Spreading Activation - flows through triplet graph
        // 3. Attractor Dynamics - results pulled toward conceptual gravity wells
        // 4. Lateral Inhibition - similar patterns compete
        // 5. Hebbian Learning - co-activated nodes strengthen connections
        auto general_results = mind_->full_resonate(query, realm.empty() ? k : k * 2, exclude_kinds);

        // Merge: partnership memories first, then general
        std::vector<Recall> results;
        std::unordered_set<uint64_t> seen_ids;

        // Add partnership memories first (up to k/2)
        size_t partnership_limit = std::min(partnership_results.size(), k / 2);
        for (size_t i = 0; i < partnership_limit; ++i) {
            results.push_back(partnership_results[i]);
            seen_ids.insert(partnership_results[i].id.low);
        }

        // Add general results (avoiding duplicates)
        for (const auto& r : general_results) {
            if (seen_ids.find(r.id.low) == seen_ids.end()) {
                // Skip code-tagged wisdom when partnership_only is true
                if (partnership_only && r.text.rfind("[code]", 0) == 0) {
                    continue;
                }
                results.push_back(r);
                seen_ids.insert(r.id.low);
            }
        }

        std::ostringstream ss;
        json results_json = json::array();
        size_t count = 0;

        for (const auto& r : results) {
            // Post-hoc realm filtering if realm specified
            if (!realm.empty()) {
                auto realms = mind_->store().get_realms(static_cast<int64_t>(r.id.low));
                bool in_realm = false;
                for (const auto& rm : realms) {
                    if (rm == realm) { in_realm = true; break; }
                }
                // Check if memory is global and include_global is true
                auto mem = mind_->store().get_memory(static_cast<int64_t>(r.id.low));
                if (mem && mem->visibility == RealmVisibility::Global && include_global) {
                    in_realm = true;
                }
                if (!in_realm) continue;
            }

            if (count >= k) break;
            count++;

            int pct = static_cast<int>(std::min(r.relevance, 1.0f) * 100);
            std::string type_name = node_type_name(r.type);

            ss << "[" << pct << "%] [" << type_name << "] "
               << r.text.substr(0, 200);
            if (r.text.size() > 200) ss << "...";
            ss << "\n\n";

            // Check for provenance: wisdom derived_from episode
            std::string provenance;
            if (r.type == NodeType::Wisdom) {
                std::string wisdom_ref = "wisdom:" + r.id.to_string();
                auto provenance_triplets = mind_->store().query_subject(wisdom_ref);
                for (const auto& pt : provenance_triplets) {
                    if (pt.predicate == "derived_from" && pt.object.substr(0, 8) == "episode:") {
                        // Extract episode ID and fetch content
                        std::string episode_id_str = pt.object.substr(8);
                        try {
                            NodeId episode_id = NodeId::from_string(episode_id_str);
                            auto episode_mem = mind_->store().get_memory(static_cast<int64_t>(episode_id.low));
                            if (episode_mem) {
                                provenance = episode_mem->content.substr(0, 300);
                                if (episode_mem->content.size() > 300) provenance += "...";
                            }
                        } catch (...) {}
                        break;
                    }
                }
            }

            json result_entry = {
                {"id", r.id.to_string()},
                {"relevance", r.relevance},
                {"type", type_name},
                {"text", r.text}
            };
            if (!provenance.empty()) {
                result_entry["provenance"] = provenance;
                ss << "  ↳ Source: " << provenance.substr(0, 100) << "...\n\n";
            }
            results_json.push_back(result_entry);
        }

        // Graph expansion - find related concepts via triplets
        std::vector<std::string> terms = extract_terms(query);
        std::set<std::string> seen_triplets;
        json triplets_json = json::array();

        auto add_triplet = [&](const StringTriplet& t) {
            std::string key = t.subject + "→" + t.predicate + "→" + t.object;
            if (seen_triplets.find(key) == seen_triplets.end()) {
                seen_triplets.insert(key);
                triplets_json.push_back({
                    {"subject", t.subject},
                    {"predicate", t.predicate},
                    {"object", t.object}
                });
            }
        };

        for (const auto& term : terms) {
            for (const auto& t : mind_->store().query_subject(term)) add_triplet(t);
            for (const auto& t : mind_->store().query_object(term)) add_triplet(t);
        }

        // Get attractor info for diagnostics
        auto attractors = mind_->find_attractors();
        json attractors_json = json::array();
        for (const auto& attr : attractors) {
            attractors_json.push_back({
                {"entity", attr.entity},
                {"strength", attr.strength},
                {"connections", attr.connections}
            });
        }

        // Build output
        std::ostringstream header;
        header << "[Resonance]\n";
        if (!results_json.empty()) {
            header << "Found " << results_json.size() << " results";
            if (!attractors.empty()) {
                header << " (attractor: " << attractors[0].entity << ")";
            }
            header << ":\n\n";
        }

        std::string output = header.str() + ss.str();

        // Add graph relationships if found
        if (!triplets_json.empty()) {
            output += "[Related]\n";
            for (const auto& t : triplets_json) {
                output += t["subject"].get<std::string>() + " → " +
                         t["predicate"].get<std::string>() + " → " +
                         t["object"].get<std::string>() + "\n";
            }
        }

        // Push event to subconscious for pattern detection (always, even if no results)
        if (subconscious_) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            subconscious_->push_event({
                SubconsciousEventType::UserMessage,
                query,
                realm,
                now
            });
        }

        if (results_json.empty() && triplets_json.empty()) {
            return DuckDBToolResult::ok("No memories or relationships found.", {
                {"results", json::array()},
                {"triplets", json::array()},
                {"attractors", json::array()}
            });
        }

        return DuckDBToolResult::ok(output, {
            {"results", results_json},
            {"triplets", triplets_json},
            {"attractors", attractors_json}
        });
    }

    DuckDBToolResult tool_extract_symbols(const json& params) {
        std::string path = params.value("path", "");
        if (path.empty()) {
            return DuckDBToolResult::error("Path is required");
        }

        if (!std::filesystem::exists(path)) {
            return DuckDBToolResult::error("Path does not exist: " + path);
        }

        CodeIntel intel;
        auto symbols = intel.extract_file(path);

        if (symbols.empty()) {
            std::string lang = intel.detect_language(path);
            if (lang.empty()) {
                return DuckDBToolResult::error("Unsupported file type");
            }
            return DuckDBToolResult::ok("No symbols found in " + path, {{"symbols", json::array()}});
        }

        std::ostringstream ss;
        ss << "Extracted " << symbols.size() << " symbols from " << path << ":\n";

        json symbols_json = json::array();
        for (const auto& sym : symbols) {
            ss << "  " << sym.kind << " " << sym.name << " @" << sym.line_start;
            if (!sym.parent.empty()) ss << " (in " << sym.parent << ")";
            ss << "\n";

            symbols_json.push_back({
                {"kind", sym.kind},
                {"name", sym.name},
                {"file", sym.file_path},
                {"line_start", sym.line_start},
                {"line_end", sym.line_end},
                {"parent", sym.parent}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"symbols", symbols_json}, {"count", symbols.size()}});
    }

    DuckDBToolResult tool_learn_codebase(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string path = params.value("path", "");
        if (path.empty()) {
            return DuckDBToolResult::error("Path is required");
        }

        if (!std::filesystem::exists(path)) {
            return DuckDBToolResult::error("Path does not exist: " + path);
        }

        std::string project = params.value("project", "");
        if (project.empty()) {
            project = std::filesystem::path(path).filename().string();
        }

        size_t max_files = params.value("max_files", 500);
        bool incremental = params.value("incremental", true);  // Default to incremental
        bool force = params.value("force", false);  // Force full re-index

        std::vector<std::string> exclude = {"node_modules", ".git", "build", "__pycache__", "venv", "target", ".venv"};
        if (params.contains("exclude") && params["exclude"].is_string()) {
            std::string exclude_str = params["exclude"].get<std::string>();
            std::istringstream iss(exclude_str);
            std::string dir;
            while (std::getline(iss, dir, ',')) {
                if (!dir.empty()) exclude.push_back(dir);
            }
        }

        CodeIntel intel;
        std::ostringstream ss;

        if (incremental && !force) {
            // Incremental: only process changed files
            auto inc_result = intel.extract_directory_incremental(
                mind_->store(), path, project, exclude, max_files);

            if (inc_result.files_processed == 0 && inc_result.files_skipped > 0) {
                ss << "Codebase up-to-date: " << project << "\n";
                ss << "  Files: " << inc_result.files_skipped << " (all current)\n";
                return DuckDBToolResult::ok(ss.str(), {
                    {"project", project},
                    {"path", path},
                    {"mode", "incremental"},
                    {"files_skipped", inc_result.files_skipped},
                    {"up_to_date", true}
                });
            }

            // Store new symbols and callsites
            size_t symbols_stored = 0, callsites_stored = 0;
            size_t symbols_embedded = 0;
            if (!inc_result.extracted.symbols.empty() || !inc_result.extracted.callsites.empty()) {
                auto [s, c] = intel.store_full(mind_->store(), inc_result.extracted);
                symbols_stored = s;
                callsites_stored = c;

                // Pre-embed symbols if yantra available (move embedding cost to index time)
                if (mind_->has_yantra() && symbols_stored > 0) {
                    // Collect file paths from extracted symbols
                    std::unordered_set<std::string> files;
                    for (const auto& sym : inc_result.extracted.symbols) {
                        files.insert(sym.file_path);
                    }
                    std::vector<std::string> file_list(files.begin(), files.end());

                    // Get unembedded symbols for these files
                    auto unembedded = mind_->store().get_unembedded_symbols(100);  // Batch of 100

                    // Build embedding texts and embed in batch
                    std::vector<std::string> texts;
                    std::vector<int64_t> ids;
                    for (const auto& sym : unembedded) {
                        std::string text = sym.kind + " " + sym.name;
                        if (!sym.signature.empty()) text += " " + sym.signature;
                        texts.push_back(text);
                        ids.push_back(sym.id);
                    }

                    if (!texts.empty()) {
                        auto embeddings = mind_->embedder().embed_batch(texts);
                        for (size_t i = 0; i < embeddings.size(); ++i) {
                            if (!embeddings[i].is_zero()) {
                                mind_->store().set_symbol_embedding(ids[i], embeddings[i].data);
                                symbols_embedded++;
                            }
                        }
                    }
                }
            }

            ss << "Learned codebase (incremental): " << project << "\n";
            ss << "  Path: " << path << "\n";
            ss << "  Files processed: " << inc_result.files_processed << "\n";
            ss << "  Files skipped (up-to-date): " << inc_result.files_skipped << "\n";
            ss << "  Symbols added: " << symbols_stored << "\n";
            ss << "  Symbols embedded: " << symbols_embedded << "\n";
            ss << "  Callsites added: " << callsites_stored << "\n";
            if (inc_result.symbols_deleted > 0 || inc_result.triplets_deleted > 0) {
                ss << "  Old data cleaned: " << inc_result.symbols_deleted << " symbols, "
                   << inc_result.triplets_deleted << " triplets\n";
            }

            // Summary by kind
            std::unordered_map<std::string, size_t> by_kind;
            for (const auto& sym : inc_result.extracted.symbols) {
                by_kind[sym.kind]++;
            }
            if (!by_kind.empty()) {
                ss << "  Symbol breakdown:\n";
                for (const auto& [kind, count] : by_kind) {
                    ss << "    " << kind << ": " << count << "\n";
                }
            }

            return DuckDBToolResult::ok(ss.str(), {
                {"project", project},
                {"path", path},
                {"mode", "incremental"},
                {"files_processed", inc_result.files_processed},
                {"files_skipped", inc_result.files_skipped},
                {"symbols_stored", symbols_stored},
                {"symbols_embedded", symbols_embedded},
                {"callsites_stored", callsites_stored},
                {"symbols_deleted", inc_result.symbols_deleted},
                {"triplets_deleted", inc_result.triplets_deleted}
            });
        }

        // Full extraction (force or non-incremental)
        auto result = intel.extract_directory_full(path, exclude, max_files);

        if (result.symbols.empty()) {
            return DuckDBToolResult::ok("No symbols found in " + path, {{"stored", 0}});
        }

        // Store symbols and callsites in DuckDB
        auto [symbols_stored, callsites_stored] = intel.store_full(mind_->store(), result);

        // Pre-embed symbols if yantra available
        size_t symbols_embedded = 0;
        if (mind_->has_yantra() && symbols_stored > 0) {
            auto unembedded = mind_->store().get_unembedded_symbols(100);
            std::vector<std::string> texts;
            std::vector<int64_t> ids;
            for (const auto& sym : unembedded) {
                std::string text = sym.kind + " " + sym.name;
                if (!sym.signature.empty()) text += " " + sym.signature;
                texts.push_back(text);
                ids.push_back(sym.id);
            }
            if (!texts.empty()) {
                auto embeddings = mind_->embedder().embed_batch(texts);
                for (size_t i = 0; i < embeddings.size(); ++i) {
                    if (!embeddings[i].is_zero()) {
                        mind_->store().set_symbol_embedding(ids[i], embeddings[i].data);
                        symbols_embedded++;
                    }
                }
            }
        }

        // Clean orphaned embeddings after force re-index
        size_t orphans_cleaned = mind_->store().clean_orphaned_symbol_embeddings();

        // Create project triplet
        mind_->connect(project, "contains", std::to_string(symbols_stored) + "_symbols");

        ss << "Learned codebase: " << project << "\n";
        ss << "  Path: " << path << "\n";
        ss << "  Mode: " << (force ? "force" : "full") << "\n";
        ss << "  Symbols: " << symbols_stored << "\n";
        ss << "  Symbols embedded: " << symbols_embedded << "\n";
        ss << "  Callsites: " << callsites_stored << "\n";
        if (orphans_cleaned > 0) {
            ss << "  Orphaned embeddings cleaned: " << orphans_cleaned << "\n";
        }

        // Summary by kind
        std::unordered_map<std::string, size_t> by_kind;
        for (const auto& sym : result.symbols) {
            by_kind[sym.kind]++;
        }
        ss << "  Symbol breakdown:\n";
        for (const auto& [kind, count] : by_kind) {
            ss << "    " << kind << ": " << count << "\n";
        }

        // Callsite summary by kind
        std::unordered_map<std::string, size_t> callsites_by_kind;
        for (const auto& cs : result.callsites) {
            callsites_by_kind[call_kind_to_string(cs.kind)]++;
        }
        if (!callsites_by_kind.empty()) {
            ss << "  Callsite breakdown:\n";
            for (const auto& [kind, count] : callsites_by_kind) {
                ss << "    " << kind << ": " << count << "\n";
            }
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"project", project},
            {"path", path},
            {"mode", force ? "force" : "full"},
            {"symbols_stored", symbols_stored},
            {"symbols_embedded", symbols_embedded},
            {"callsites_stored", callsites_stored},
            {"symbols_by_kind", by_kind},
            {"callsites_by_kind", callsites_by_kind}
        });
    }

    DuckDBToolResult tool_find_symbol(const json& params) {
        // Notify subconscious that we're handling a query (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string name = params.value("name", "");
        if (name.empty()) {
            return DuckDBToolResult::error("Name is required");
        }

        std::string kind = params.value("kind", "");
        auto symbols = mind_->store().find_symbol(name, kind);

        if (symbols.empty()) {
            return DuckDBToolResult::ok("No symbols found matching '" + name + "'", {{"symbols", json::array()}});
        }

        std::ostringstream ss;
        ss << "Found " << symbols.size() << " symbols matching '" << name << "':\n";

        json symbols_json = json::array();
        for (const auto& sym : symbols) {
            ss << "  " << sym.kind << " " << sym.name << " @" << sym.file_path << ":" << sym.line_start << "\n";

            symbols_json.push_back({
                {"id", sym.id},
                {"kind", sym.kind},
                {"name", sym.name},
                {"file", sym.file_path},
                {"line_start", sym.line_start},
                {"line_end", sym.line_end},
                {"signature", sym.signature}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"symbols", symbols_json}, {"count", symbols.size()}});
    }

    // Helper to resolve symbol by name or ID
    std::optional<Symbol> resolve_symbol(const json& params) {
        // Try ID first (most specific)
        if (params.contains("id") && params["id"].is_number_integer()) {
            int64_t id = params["id"].get<int64_t>();
            return mind_->store().get_symbol_by_id(id);
        }

        // Fall back to name search
        std::string name = params.value("name", "");
        if (name.empty()) return std::nullopt;

        std::string kind = params.value("kind", "");
        auto symbols = mind_->store().find_symbol(name, kind);
        if (symbols.empty()) return std::nullopt;

        // Collect exact name matches
        std::vector<Symbol> exact;
        for (const auto& s : symbols) {
            if (s.name == name) exact.push_back(s);
        }
        if (exact.empty()) return symbols[0];
        if (exact.size() == 1) return exact[0];

        // Multiple exact matches — disambiguate

        // 1. Project filter: match symbol file_path against project's indexed files
        std::string project = params.value("project", "");
        if (!project.empty()) {
            std::string escaped;
            for (char c : project) {
                if (c == '\'') escaped += "''";
                else escaped += c;
            }
            auto path_result = mind_->store().execute_sql_query(
                "SELECT DISTINCT path FROM code_file WHERE project = '" + escaped + "'"
            );
            if (path_result.success && !path_result.rows.empty()) {
                std::unordered_set<std::string> project_files;
                for (const auto& row : path_result.rows) {
                    if (!row.empty()) project_files.insert(row[0]);
                }
                for (const auto& s : exact) {
                    if (project_files.count(s.file_path)) return s;
                }
            }
        }

        // 2. Prefer symbols that have call edges (actually participate in call graph)
        for (const auto& s : exact) {
            auto callers = mind_->store().callers(s.id);
            auto callees = mind_->store().callees(s.id);
            if (!callers.empty() || !callees.empty()) return s;
        }

        // 3. Fallback: first exact match
        return exact[0];
    }

    DuckDBToolResult tool_symbol_callers(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        auto sym_opt = resolve_symbol(params);
        if (!sym_opt) {
            return DuckDBToolResult::error("Symbol not found. Provide 'name' or 'id'.");
        }
        const auto& sym = *sym_opt;

        auto caller_ids = mind_->store().callers(sym.id);

        if (caller_ids.empty()) {
            return DuckDBToolResult::ok(
                "No callers found for " + sym.kind + " " + sym.name,
                {{"symbol", sym.name}, {"callers", json::array()}}
            );
        }

        std::ostringstream ss;
        ss << "Found " << caller_ids.size() << " callers for " << sym.kind << " " << sym.name << ":\n";

        json callers_json = json::array();
        for (int64_t cid : caller_ids) {
            auto caller_opt = mind_->store().get_symbol_by_id(cid);
            if (caller_opt) {
                const auto& c = *caller_opt;
                ss << "  " << c.kind << " " << c.name << " @" << c.file_path << ":" << c.line_start << "\n";
                callers_json.push_back({
                    {"id", c.id},
                    {"kind", c.kind},
                    {"name", c.name},
                    {"file", c.file_path},
                    {"line_start", c.line_start},
                    {"line_end", c.line_end}
                });
            }
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"symbol", sym.name},
            {"symbol_id", sym.id},
            {"callers", callers_json},
            {"count", callers_json.size()}
        });
    }

    DuckDBToolResult tool_symbol_callees(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        auto sym_opt = resolve_symbol(params);
        if (!sym_opt) {
            return DuckDBToolResult::error("Symbol not found. Provide 'name' or 'id'.");
        }
        const auto& sym = *sym_opt;

        auto callee_ids = mind_->store().callees(sym.id);

        if (callee_ids.empty()) {
            return DuckDBToolResult::ok(
                "No callees found for " + sym.kind + " " + sym.name,
                {{"symbol", sym.name}, {"callees", json::array()}}
            );
        }

        std::ostringstream ss;
        ss << "Found " << callee_ids.size() << " callees for " << sym.kind << " " << sym.name << ":\n";

        json callees_json = json::array();
        for (int64_t cid : callee_ids) {
            auto callee_opt = mind_->store().get_symbol_by_id(cid);
            if (callee_opt) {
                const auto& c = *callee_opt;
                ss << "  " << c.kind << " " << c.name << " @" << c.file_path << ":" << c.line_start << "\n";
                callees_json.push_back({
                    {"id", c.id},
                    {"kind", c.kind},
                    {"name", c.name},
                    {"file", c.file_path},
                    {"line_start", c.line_start},
                    {"line_end", c.line_end}
                });
            }
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"symbol", sym.name},
            {"symbol_id", sym.id},
            {"callees", callees_json},
            {"count", callees_json.size()}
        });
    }

    DuckDBToolResult tool_read_symbol(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        auto sym_opt = resolve_symbol(params);
        if (!sym_opt) {
            return DuckDBToolResult::error("Symbol not found. Provide 'name' or 'id'.");
        }
        const auto& sym = *sym_opt;

        // Read the source file
        std::ifstream file(sym.file_path);
        if (!file) {
            return DuckDBToolResult::error("Cannot open file: " + sym.file_path);
        }

        // Read lines from line_start to line_end
        std::ostringstream source;
        std::string line;
        int line_num = 1;
        while (std::getline(file, line)) {
            if (line_num >= sym.line_start && line_num <= sym.line_end) {
                source << line << "\n";
            }
            if (line_num > sym.line_end) break;
            line_num++;
        }

        std::string code = source.str();
        if (code.empty()) {
            return DuckDBToolResult::error("No code found at " + sym.file_path + ":" +
                                          std::to_string(sym.line_start) + "-" +
                                          std::to_string(sym.line_end));
        }

        std::ostringstream ss;
        ss << sym.kind << " " << sym.name << " @" << sym.file_path << ":"
           << sym.line_start << "-" << sym.line_end << "\n\n" << code;

        return DuckDBToolResult::ok(ss.str(), {
            {"symbol", sym.name},
            {"kind", sym.kind},
            {"file", sym.file_path},
            {"line_start", sym.line_start},
            {"line_end", sym.line_end},
            {"code", code}
        });
    }

    DuckDBToolResult tool_read_function(const json& params) {
        // Shorthand for read_symbol with kind = function or method
        std::string name = params.value("name", "");
        if (name.empty()) {
            return DuckDBToolResult::error("Function name is required");
        }

        // Try function first, then method
        auto symbols = mind_->store().find_symbol(name, "function");
        if (symbols.empty()) {
            symbols = mind_->store().find_symbol(name, "method");
        }

        if (symbols.empty()) {
            return DuckDBToolResult::error("Function/method '" + name + "' not found");
        }

        // Find exact match
        const Symbol* best = nullptr;
        for (const auto& s : symbols) {
            if (s.name == name) {
                best = &s;
                break;
            }
        }
        if (!best) best = &symbols[0];

        // Read the source file
        std::ifstream file(best->file_path);
        if (!file) {
            return DuckDBToolResult::error("Cannot open file: " + best->file_path);
        }

        std::ostringstream source;
        std::string line;
        int line_num = 1;
        while (std::getline(file, line)) {
            if (line_num >= best->line_start && line_num <= best->line_end) {
                source << line << "\n";
            }
            if (line_num > best->line_end) break;
            line_num++;
        }

        std::string code = source.str();
        std::ostringstream ss;
        ss << best->kind << " " << best->name << " @" << best->file_path << ":"
           << best->line_start << "-" << best->line_end << "\n\n" << code;

        return DuckDBToolResult::ok(ss.str(), {
            {"symbol", best->name},
            {"kind", best->kind},
            {"file", best->file_path},
            {"line_start", best->line_start},
            {"line_end", best->line_end},
            {"code", code}
        });
    }

    DuckDBToolResult tool_search_symbols(const json& params) {
        // Notify subconscious that we're handling a query (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string query = params.value("query", "");
        if (query.empty()) {
            return DuckDBToolResult::error("Query is required");
        }

        std::string kind = params.value("kind", "");
        size_t limit = params.value("limit", 10);
        std::string mode = params.value("mode", "auto");  // auto, bm25, semantic
        std::string project = params.value("project", "");

        bool is_code_query = looks_like_code_query(query);
        bool use_bm25 = (mode == "bm25") || (mode == "auto" && is_code_query);
        bool use_semantic = (mode == "semantic") || (mode == "auto" && !is_code_query);

        // If semantic requested but no yantra, fall back to BM25
        if (use_semantic && !mind_->has_yantra()) {
            use_bm25 = true;
            use_semantic = false;
        }

        std::vector<DuckDBStore::SymbolMatch> semantic_matches;
        std::vector<Symbol> bm25_matches;
        std::string search_mode;

        // BM25 search (fast, ~50ms)
        if (use_bm25) {
            bm25_matches = mind_->store().bm25_search_symbols(query, limit, project);
            search_mode = "bm25";
        }

        // Semantic search (slow, ~2-5s on CPU)
        if (use_semantic && mind_->has_yantra()) {
            auto artha = mind_->embedder().transform_query(query);
            if (!artha.nu.is_zero()) {
                semantic_matches = mind_->store().search_symbols_by_embedding(artha.nu.data, limit, kind, project);
                search_mode = use_bm25 ? "hybrid" : "semantic";
            }
        }

        // Merge results: semantic first for NL queries, BM25 first for code queries
        json symbols_json = json::array();
        std::unordered_set<int64_t> seen_ids;
        std::ostringstream ss;

        auto add_symbol = [&](const Symbol& sym, float score, const std::string& source) {
            if (seen_ids.count(sym.id) || symbols_json.size() >= limit) return;
            seen_ids.insert(sym.id);

            std::string disp = display_path(sym.file_path);

            // Filter by kind if specified
            if (!kind.empty() && sym.kind != kind) return;

            if (score > 0) {
                ss << "  [" << std::fixed << std::setprecision(0) << (score * 100) << "%] ";
            } else {
                ss << "  ";
            }
            ss << sym.kind << " " << sym.name << " @" << disp << ":" << sym.line_start
               << " (" << source << ")\n";

            json sym_json = {
                {"id", sym.id},
                {"kind", sym.kind},
                {"name", sym.name},
                {"file", sym.file_path},
                {"line_start", sym.line_start},
                {"line_end", sym.line_end},
                {"signature", sym.signature},
                {"source", source}
            };
            if (score > 0) sym_json["score"] = score;
            symbols_json.push_back(sym_json);
        };

        // Order depends on query type
        if (is_code_query) {
            // Code query: BM25 first
            for (const auto& sym : bm25_matches) add_symbol(sym, 0, "bm25");
            for (const auto& m : semantic_matches) add_symbol(m.symbol, m.score, "semantic");
        } else {
            // NL query: semantic first
            for (const auto& m : semantic_matches) add_symbol(m.symbol, m.score, "semantic");
            for (const auto& sym : bm25_matches) add_symbol(sym, 0, "bm25");
        }

        if (symbols_json.empty()) {
            return DuckDBToolResult::ok("No symbols found for query: " + query,
                {{"symbols", json::array()}, {"mode", search_mode}});
        }

        std::ostringstream header;
        header << "Found " << symbols_json.size() << " symbols for '" << query
               << "' (" << search_mode << ", " << (is_code_query ? "code" : "NL") << " query):\n";

        return DuckDBToolResult::ok(header.str() + ss.str(),
            {{"symbols", symbols_json}, {"count", symbols_json.size()}, {"mode", search_mode}});
    }

    DuckDBToolResult tool_code_context(const json& params) {
        // Notify subconscious that we're handling a query (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string path = params.value("path", "");

        // Get symbol counts
        auto health = mind_->store().health();

        std::ostringstream ss;
        ss << "Code Context:\n";
        ss << "  Symbols: " << health.total_symbols << " indexed\n";

        json result;
        result["total_symbols"] = health.total_symbols;

        // If path specified, get file-specific info
        if (!path.empty() && std::filesystem::exists(path)) {
            CodeIntel intel;
            if (std::filesystem::is_regular_file(path)) {
                auto symbols = intel.extract_file(path);
                ss << "  File: " << path << " (" << symbols.size() << " symbols)\n";
                result["file_symbols"] = symbols.size();
            } else if (std::filesystem::is_directory(path)) {
                auto symbols = intel.extract_directory(path, {}, 50);
                ss << "  Directory: " << path << " (" << symbols.size() << " symbols in sample)\n";
                result["dir_symbols"] = symbols.size();
            }
        }

        return DuckDBToolResult::ok(ss.str(), result);
    }

    // ========================================================================
    // Smart Context - Unified agentic search combining all search capabilities
    // ========================================================================

    DuckDBToolResult tool_smart_context(const json& params) {
        // Notify subconscious that we're handling a query (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string task = params.value("task", "");
        if (task.empty()) {
            return DuckDBToolResult::error("task is required");
        }

        std::string mode = params.value("mode", "full");
        size_t token_limit = params.value("limit", 300);
        bool include_memories = params.value("memories", true);
        bool include_code = params.value("code", true);
        bool include_neighbors = params.value("neighbors", true);
        std::string realm = params.value("realm", "");

        bool fast = (mode == "fast");
        std::ostringstream ss;
        json result;

        // Performance budgets:
        // fast: 3 memories (vector), 3 symbols (BM25), 1 neighbor expansion
        // full: 5 memories (full_resonate), 5 symbols (semantic), 3 neighbor expansions
        size_t mem_limit = fast ? 3 : 5;
        size_t sym_limit = fast ? 3 : 5;
        size_t neighbor_limit = fast ? 1 : 3;

        // 1. MEMORIES
        std::vector<Recall> memories;
        json memories_json = json::array();

        if (include_memories) {
            if (fast) {
                // Fast: simple vector recall
                memories = mind_->recall(task, mem_limit);
            } else {
                // Full: full resonance with spreading activation
                memories = mind_->full_resonate(task, mem_limit);
            }

            ss << "[mem]\n";
            for (const auto& m : memories) {
                // Post-hoc realm filtering if specified
                if (!realm.empty()) {
                    auto realms = mind_->store().get_realms(static_cast<int64_t>(m.id.low));
                    bool in_realm = false;
                    for (const auto& rm : realms) {
                        if (rm == realm) { in_realm = true; break; }
                    }
                    // Also include global memories
                    auto mem = mind_->store().get_memory(static_cast<int64_t>(m.id.low));
                    if (mem && mem->visibility == RealmVisibility::Global) {
                        in_realm = true;
                    }
                    if (!in_realm) continue;
                }

                int pct = static_cast<int>(std::min(m.relevance, 1.0f) * 100);
                std::string type_name = node_type_name(m.type);
                // Short type code
                std::string type_short = type_name.substr(0, 3);
                if (type_name == "wisdom") type_short = "wis";
                else if (type_name == "belief") type_short = "bel";
                else if (type_name == "episode") type_short = "epi";

                // Extract title (first line or first ~60 chars)
                std::string title = m.text.substr(0, 60);
                size_t newline = title.find('\n');
                if (newline != std::string::npos) {
                    title = title.substr(0, newline);
                }

                ss << "[" << pct << "%:" << type_short << ":" << m.id.to_string().substr(0, 8)
                   << "] " << title << "\n";

                json mem_entry;
                mem_entry["id"] = m.id.to_string();
                mem_entry["relevance"] = m.relevance;
                mem_entry["type"] = type_name;
                mem_entry["text"] = m.text;
                memories_json.push_back(mem_entry);
            }
        }

        // 2. CODE SYMBOLS
        json symbols_json = json::array();

        if (include_code) {
            bool is_code_query = looks_like_code_query(task);

            if (fast || is_code_query) {
                // Fast mode or code-like query: BM25 search
                auto bm25_results = mind_->store().bm25_search_symbols(task, sym_limit);

                if (!bm25_results.empty()) {
                    ss << "\n[code]\n";
                    for (const auto& sym : bm25_results) {
                        ss << sym.file_path << ":" << sym.line_start
                           << " " << sym.kind << " " << sym.name << "\n";

                        json sym_entry;
                        sym_entry["name"] = sym.name;
                        sym_entry["kind"] = sym.kind;
                        sym_entry["file"] = sym.file_path;
                        sym_entry["line_start"] = sym.line_start;
                        sym_entry["line_end"] = sym.line_end;
                        symbols_json.push_back(sym_entry);
                    }
                }
            } else {
                // Full mode with natural language: semantic search
                if (mind_->has_yantra()) {
                    auto artha = mind_->embedder().transform_query(task);
                    if (!artha.nu.is_zero()) {
                        auto semantic_results = mind_->store().search_symbols_by_embedding(
                            artha.nu.data, sym_limit, "");

                        if (!semantic_results.empty()) {
                            ss << "\n[code]\n";
                            for (const auto& match : semantic_results) {
                                const auto& sym = match.symbol;
                                ss << sym.file_path << ":" << sym.line_start
                                   << " " << sym.kind << " " << sym.name
                                   << " (" << static_cast<int>(match.score * 100) << "%)\n";

                                json sym_entry;
                                sym_entry["name"] = sym.name;
                                sym_entry["kind"] = sym.kind;
                                sym_entry["file"] = sym.file_path;
                                sym_entry["line_start"] = sym.line_start;
                                sym_entry["similarity"] = match.score;
                                symbols_json.push_back(sym_entry);
                            }
                        }
                    }
                } else {
                    // Fallback to BM25 if no embedder
                    auto bm25_results = mind_->store().bm25_search_symbols(task, sym_limit);
                    if (!bm25_results.empty()) {
                        ss << "\n[code]\n";
                        for (const auto& sym : bm25_results) {
                            ss << sym.file_path << ":" << sym.line_start
                               << " " << sym.kind << " " << sym.name << "\n";
                            json sym_entry;
                            sym_entry["name"] = sym.name;
                            sym_entry["kind"] = sym.kind;
                            sym_entry["file"] = sym.file_path;
                            sym_entry["line_start"] = sym.line_start;
                            symbols_json.push_back(sym_entry);
                        }
                    }
                }
            }
        }

        // 3. TRIPLET NEIGHBORS (from top memory terms)
        json triplets_json = json::array();

        if (include_neighbors && !memories.empty()) {
            ss << "\n[graph]\n";
            std::set<std::string> seen_triplets;
            size_t processed = 0;

            for (const auto& m : memories) {
                if (processed >= neighbor_limit) break;

                // Extract key terms from memory content
                auto terms = extract_terms(m.text);
                if (terms.empty()) continue;

                // Query triplets for the first significant term
                for (const auto& term : terms) {
                    if (term.length() < 4) continue;  // Skip short terms

                    auto subj_triplets = mind_->store().query_subject(term);
                    auto obj_triplets = mind_->store().query_object(term);

                    for (const auto& t : subj_triplets) {
                        std::string key = t.subject + "→" + t.predicate + "→" + t.object;
                        if (seen_triplets.find(key) == seen_triplets.end()) {
                            seen_triplets.insert(key);
                            ss << t.subject << "→" << t.predicate << "→" << t.object << "\n";
                            json triplet_entry;
                            triplet_entry["subject"] = t.subject;
                            triplet_entry["predicate"] = t.predicate;
                            triplet_entry["object"] = t.object;
                            triplets_json.push_back(triplet_entry);
                        }
                        if (triplets_json.size() >= 5) break;  // Limit triplets
                    }

                    for (const auto& t : obj_triplets) {
                        std::string key = t.subject + "→" + t.predicate + "→" + t.object;
                        if (seen_triplets.find(key) == seen_triplets.end()) {
                            seen_triplets.insert(key);
                            ss << t.subject << "→" << t.predicate << "→" << t.object << "\n";
                            json triplet_entry;
                            triplet_entry["subject"] = t.subject;
                            triplet_entry["predicate"] = t.predicate;
                            triplet_entry["object"] = t.object;
                            triplets_json.push_back(triplet_entry);
                        }
                        if (triplets_json.size() >= 5) break;
                    }

                    if (triplets_json.size() >= 3) break;  // Found enough
                }

                processed++;
            }
        }

        // Build result
        result["memories"] = memories_json;
        result["symbols"] = symbols_json;
        result["triplets"] = triplets_json;
        result["mode"] = mode;
        result["task"] = task;

        std::string output = ss.str();
        if (output.empty()) {
            output = "No context found for: " + task;
        }

        return DuckDBToolResult::ok(output, result);
    }

    DuckDBToolResult tool_codebase_overview(const json& params) {
        std::string project = params.value("project", "");
        std::string format = params.value("format", "tree");
        bool include_callsites = params.value("include_callsites", false);

        std::ostringstream ss;
        json result;

        // Get all files for project
        auto files = mind_->store().list_project_files(project);

        if (files.empty()) {
            ss << "No indexed files";
            if (!project.empty()) ss << " for project: " << project;
            ss << "\nRun: learn_codebase --path /your/project --project " << (project.empty() ? "myproj" : project);
            return DuckDBToolResult::ok(ss.str(), {{"files", 0}});
        }

        // Summary
        size_t total_symbols = 0, total_callsites = 0;
        for (const auto& f : files) {
            total_symbols += f.symbols_count;
            total_callsites += f.callsites_count;
        }

        ss << "Codebase: " << (project.empty() ? "(all)" : project) << "\n";
        ss << "  Files: " << files.size() << "\n";
        ss << "  Symbols: " << total_symbols << "\n";
        ss << "  Callsites: " << total_callsites << "\n\n";

        // List files with counts
        ss << "Files:\n";
        for (const auto& f : files) {
            std::filesystem::path p(f.path);
            ss << "  " << p.filename().string() << " (" << f.symbols_count << " symbols";
            if (include_callsites) ss << ", " << f.callsites_count << " callsites";
            ss << ")\n";
        }

        // Get symbol breakdown by querying triplets
        auto contains_triplets = mind_->store().query_predicate("contains");
        std::unordered_map<std::string, std::vector<std::string>> file_symbols;

        for (const auto& t : contains_triplets) {
            // subject is file or class, object is symbol
            file_symbols[t.subject].push_back(t.object);
        }

        if (format == "tree" && !file_symbols.empty()) {
            ss << "\nStructure:\n";
            for (const auto& [parent, children] : file_symbols) {
                if (children.size() > 1) {  // Only show containers
                    ss << "  " << parent << ":\n";
                    for (const auto& child : children) {
                        if (child.find("callsite") == std::string::npos) {  // Skip callsites in tree
                            ss << "    - " << child << "\n";
                        }
                    }
                }
            }
        }

        result["files"] = files.size();
        result["symbols"] = total_symbols;
        result["callsites"] = total_callsites;
        result["project"] = project;

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_clear_codebase(const json& params) {
        std::string project = params.value("project", "");
        if (project.empty()) {
            return DuckDBToolResult::error("Project name is required");
        }

        bool dry_run = params.value("dry_run", false);

        // Get current stats for preview
        auto files = mind_->store().list_project_files(project);
        if (files.empty()) {
            return DuckDBToolResult::ok("No code intelligence data found for project: " + project,
                {{"project", project}, {"files", 0}});
        }

        if (dry_run) {
            // Count what would be deleted
            size_t total_symbols = 0, total_callsites = 0;
            for (const auto& f : files) {
                total_symbols += f.symbols_count;
                total_callsites += f.callsites_count;
            }

            std::ostringstream ss;
            ss << "Would clear codebase: " << project << "\n";
            ss << "  Files: " << files.size() << "\n";
            ss << "  Symbols: " << total_symbols << "\n";
            ss << "  Callsites: " << total_callsites << "\n";

            return DuckDBToolResult::ok(ss.str(), {
                {"project", project},
                {"dry_run", true},
                {"files", files.size()},
                {"symbols", total_symbols},
                {"callsites", total_callsites}
            });
        }

        // Actually clear
        auto result = mind_->store().clear_project_codebase(project);

        std::ostringstream ss;
        ss << "Cleared codebase: " << project << "\n";
        ss << "  Files deleted: " << result.files_deleted << "\n";
        ss << "  Symbols deleted: " << result.symbols_deleted << "\n";
        ss << "  Triplets deleted: " << result.triplets_deleted << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"project", project},
            {"files_deleted", result.files_deleted},
            {"symbols_deleted", result.symbols_deleted},
            {"triplets_deleted", result.triplets_deleted}
        });
    }

    DuckDBToolResult tool_clear_triplets(const json& params) {
        std::string pattern = params.value("pattern", "");
        if (pattern.empty()) {
            return DuckDBToolResult::error("Pattern is required (e.g., '%.cpp', '%.hpp')");
        }

        bool dry_run = params.value("dry_run", false);

        size_t count = mind_->store().count_triplets_by_pattern(pattern);

        if (count == 0) {
            return DuckDBToolResult::ok("No triplets match pattern: " + pattern,
                {{"pattern", pattern}, {"count", 0}});
        }

        if (dry_run) {
            std::ostringstream ss;
            ss << "Would delete " << count << " triplets matching: " << pattern;
            return DuckDBToolResult::ok(ss.str(), {
                {"pattern", pattern},
                {"dry_run", true},
                {"count", count}
            });
        }

        // Actually delete
        size_t deleted = mind_->store().delete_triplets_by_pattern(pattern);

        std::ostringstream ss;
        ss << "Deleted " << deleted << " triplets matching: " << pattern;
        return DuckDBToolResult::ok(ss.str(), {
            {"pattern", pattern},
            {"deleted", deleted}
        });
    }

    DuckDBToolResult tool_resolve_callsites(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        std::string project = params.value("project", "");

        SymbolResolver resolver(mind_->store());
        auto stats = resolver.resolve_project(project);

        std::ostringstream ss;
        ss << "Resolved " << stats.resolved << "/" << stats.total_callsites << " callsites\n";
        ss << "  Resolved (high confidence): " << stats.resolved << "\n";
        ss << "  Ambiguous (low confidence): " << stats.ambiguous << "\n";
        ss << "  Unresolved: " << stats.unresolved << "\n";
        ss << "  Indirect/skipped: " << stats.indirect << "\n";
        ss << "  Avg confidence: " << std::fixed << std::setprecision(2) << stats.avg_confidence;

        return DuckDBToolResult::ok(ss.str(), {
            {"total", stats.total_callsites},
            {"resolved", stats.resolved},
            {"ambiguous", stats.ambiguous},
            {"unresolved", stats.unresolved},
            {"indirect", stats.indirect},
            {"avg_confidence", stats.avg_confidence}
        });
    }

    DuckDBToolResult tool_type_hierarchy(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        std::string name = params.value("name", "");
        if (name.empty()) {
            return DuckDBToolResult::error("Type name is required");
        }

        std::string direction = params.value("direction", "both");

        json ancestors = json::array();
        json descendants = json::array();

        // Query ancestors (what this type extends/implements)
        if (direction == "ancestors" || direction == "both") {
            auto triplets = mind_->store().query_subject(name);
            for (const auto& t : triplets) {
                if (t.predicate == "extends" || t.predicate == "implements" || t.predicate == "embeds") {
                    ancestors.push_back({
                        {"name", t.object},
                        {"relationship", t.predicate}
                    });
                }
            }
        }

        // Query descendants (what extends/implements this type)
        if (direction == "descendants" || direction == "both") {
            auto triplets = mind_->store().query_object(name);
            for (const auto& t : triplets) {
                if (t.predicate == "extends" || t.predicate == "implements" || t.predicate == "embeds") {
                    descendants.push_back({
                        {"name", t.subject},
                        {"relationship", t.predicate}
                    });
                }
            }
        }

        std::ostringstream ss;
        ss << "Type hierarchy for " << name << ":\n";

        if (!ancestors.empty()) {
            ss << "  Ancestors (" << ancestors.size() << "):\n";
            for (const auto& a : ancestors) {
                ss << "    " << a["relationship"].get<std::string>() << " " << a["name"].get<std::string>() << "\n";
            }
        }

        if (!descendants.empty()) {
            ss << "  Descendants (" << descendants.size() << "):\n";
            for (const auto& d : descendants) {
                ss << "    " << d["name"].get<std::string>() << " " << d["relationship"].get<std::string>() << " " << name << "\n";
            }
        }

        if (ancestors.empty() && descendants.empty()) {
            ss << "  (no type relationships found)";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"type", name},
            {"ancestors", ancestors},
            {"descendants", descendants}
        });
    }

    DuckDBToolResult tool_file_imports(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        std::string path = params.value("path", "");
        if (path.empty()) {
            return DuckDBToolResult::error("File path is required");
        }

        // Get just the filename if a full path is provided
        std::filesystem::path p(path);
        std::string filename = p.filename().string();

        json imports = json::array();

        // Query imports triplets
        auto triplets = mind_->store().query_subject(filename);
        for (const auto& t : triplets) {
            if (t.predicate == "imports") {
                imports.push_back({
                    {"module", t.object},
                    {"type", "module"}
                });
            } else if (t.predicate == "imports_name") {
                imports.push_back({
                    {"module", t.object},
                    {"type", "name"}
                });
            } else if (t.predicate == "imports_as") {
                imports.push_back({
                    {"alias", t.object},
                    {"type", "alias"}
                });
            }
        }

        std::ostringstream ss;
        ss << "Imports for " << filename << ":\n";
        for (const auto& imp : imports) {
            if (imp["type"] == "module") {
                ss << "  import " << imp["module"].get<std::string>() << "\n";
            } else if (imp["type"] == "name") {
                ss << "  from ... import " << imp["module"].get<std::string>() << "\n";
            }
        }

        if (imports.empty()) {
            ss << "  (no imports found)";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"file", filename},
            {"imports", imports}
        });
    }

    DuckDBToolResult tool_file_dependents(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        std::string module = params.value("module", "");
        if (module.empty()) {
            return DuckDBToolResult::error("Module name is required");
        }

        json dependents = json::array();

        // Query files that import this module
        auto triplets = mind_->store().query_object(module);
        for (const auto& t : triplets) {
            if (t.predicate == "imports" || t.predicate == "imports_name") {
                dependents.push_back({
                    {"file", t.subject},
                    {"source_file", t.weight}  // Note: weight is reused, may not be useful
                });
            }
        }

        std::ostringstream ss;
        ss << "Files that import " << module << ":\n";
        for (const auto& d : dependents) {
            ss << "  " << d["file"].get<std::string>() << "\n";
        }

        if (dependents.empty()) {
            ss << "  (no dependents found)";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"module", module},
            {"dependents", dependents}
        });
    }

    // Essential memory tool implementations
    DuckDBToolResult tool_grow(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        std::string type_str = params.value("type", "");
        std::string content = params.value("content", "");
        std::string title = params.value("title", "");
        std::string realm = params.value("realm", "brahman");
        int visibility = params.value("visibility", 0);

        if (type_str.empty() || content.empty()) {
            return DuckDBToolResult::error("Type and content are required");
        }

        NodeType type = NodeType::Wisdom;
        if (type_str == "belief") type = NodeType::Belief;
        else if (type_str == "failure" || type_str == "episode") type = NodeType::Episode;
        else if (type_str == "aspiration") type = NodeType::Aspiration;
        else if (type_str == "dream") type = NodeType::Dream;
        else if (type_str == "symbol") type = NodeType::Symbol;
        else if (type_str == "projectessence") type = NodeType::ProjectEssence;
        else if (type_str == "modulestate") type = NodeType::ModuleState;
        else if (type_str == "patternstate") type = NodeType::PatternState;

        std::string full_content = title.empty() ? content : title + "\n" + content;
        NodeId id = mind_->remember(full_content, type, realm, static_cast<RealmVisibility>(visibility));

        static const NodeId null_id{};
        if (id == null_id) {
            return DuckDBToolResult::error("Failed to grow (quality gate or embedding failed)");
        }

        return DuckDBToolResult::ok(
            "Grew " + type_str + ": " + (title.empty() ? content.substr(0, 50) : title),
            {{"id", id.to_string()}, {"type", type_str}, {"realm", realm}}
        );
    }

    DuckDBToolResult tool_get(const json& params) {
        auto [db_id, id_str] = parse_id(params);
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        auto result = mind_->store().get_memory(db_id);
        if (!result) {
            return DuckDBToolResult::error("Node not found: " + id_str);
        }

        std::ostringstream ss;
        ss << "Node " << id_str << ":\n";
        ss << "  Kind: " << result->kind << "\n";
        ss << "  Confidence: " << result->confidence << "\n";
        ss << "  Content:\n" << result->content << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"id", db_id},
            {"kind", result->kind},
            {"content", result->content},
            {"confidence", result->confidence}
        });
    }

    DuckDBToolResult tool_expand_memory(const json& params) {
        auto [db_id, id_str] = parse_id(params);
        if (id_str.empty()) {
            return DuckDBToolResult::error("Memory ID is required");
        }

        int depth = params.value("depth", 3);
        if (depth < 1) depth = 1;
        if (depth > 3) depth = 3;

        auto expanded = mind_->store().expand_memory(db_id, depth);
        if (!expanded) {
            return DuckDBToolResult::error("Memory not found: " + id_str);
        }

        std::ostringstream ss;
        ss << "=== Level 1: SSL Memory ===\n";
        ss << "ID: " << expanded->memory_id << "\n";
        ss << "Type: " << expanded->memory_type << "\n";
        ss << "Confidence: " << expanded->confidence << "\n";
        ss << "Content:\n" << expanded->memory_content << "\n";

        json result = {
            {"memory_id", expanded->memory_id},
            {"memory_type", expanded->memory_type},
            {"memory_content", expanded->memory_content},
            {"confidence", expanded->confidence}
        };

        if (depth >= 2 && expanded->episode_id > 0) {
            ss << "\n=== Level 2: Episode ===\n";
            ss << "Episode ID: " << expanded->episode_id << "\n";
            ss << "Session: " << expanded->session_id << "\n";
            ss << "Title: " << expanded->episode_title << "\n";
            ss << "Turn range: " << expanded->start_turn << " - " << expanded->end_turn << "\n";
            if (!expanded->episode_summary.empty()) {
                ss << "Summary: " << expanded->episode_summary << "\n";
            }

            result["episode_id"] = expanded->episode_id;
            result["session_id"] = expanded->session_id;
            result["episode_title"] = expanded->episode_title;
            result["start_turn"] = expanded->start_turn;
            result["end_turn"] = expanded->end_turn;
            result["episode_summary"] = expanded->episode_summary;
        }

        if (depth >= 3 && !expanded->turns.empty()) {
            ss << "\n=== Level 3: Full Turns (" << expanded->turns.size() << ") ===\n";
            json turns_json = json::array();
            for (const auto& turn : expanded->turns) {
                ss << "\n[" << turn.role << "] (turn " << turn.turn_index << ")\n";
                ss << turn.content << "\n";

                turns_json.push_back({
                    {"turn_index", turn.turn_index},
                    {"role", turn.role},
                    {"content", turn.content},
                    {"token_count", turn.token_count}
                });
            }
            result["turns"] = turns_json;
        }

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_update(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        auto [db_id, id_str] = parse_id(params);
        std::string content = params.value("content", "");

        if (id_str.empty() || content.empty()) {
            return DuckDBToolResult::error("ID and content are required");
        }

        bool ok = mind_->store().update_content(db_id, content);
        if (!ok) {
            return DuckDBToolResult::error("Failed to update node");
        }

        return DuckDBToolResult::ok("Updated node " + id_str);
    }

    DuckDBToolResult tool_query(const json& params) {
        std::string subject = params.value("subject", "");
        std::string predicate = params.value("predicate", "");
        std::string object = params.value("object", "");

        json results_json = json::array();
        std::ostringstream ss;

        if (!subject.empty()) {
            auto triplets = mind_->store().query_subject(subject);
            for (const auto& t : triplets) {
                if (!predicate.empty() && t.predicate != predicate) continue;
                if (!object.empty() && t.object != object) continue;
                ss << subject << " → " << t.predicate << " → " << t.object << "\n";
                results_json.push_back({
                    {"subject", subject},
                    {"predicate", t.predicate},
                    {"object", t.object},
                    {"weight", t.weight}
                });
            }
        }

        if (!object.empty() && subject.empty()) {
            auto triplets = mind_->store().query_object(object);
            for (const auto& t : triplets) {
                if (!predicate.empty() && t.predicate != predicate) continue;
                ss << t.subject << " → " << t.predicate << " → " << object << "\n";
                results_json.push_back({
                    {"subject", t.subject},
                    {"predicate", t.predicate},
                    {"object", object},
                    {"weight", t.weight}
                });
            }
        }

        if (!predicate.empty() && subject.empty() && object.empty()) {
            auto triplets = mind_->store().query_predicate(predicate);
            for (const auto& t : triplets) {
                ss << t.subject << " → " << predicate << " → " << t.object << "\n";
                results_json.push_back({
                    {"subject", t.subject},
                    {"predicate", predicate},
                    {"object", t.object},
                    {"weight", t.weight}
                });
            }
        }

        if (results_json.empty()) {
            return DuckDBToolResult::ok("No triplets found", {{"triplets", json::array()}});
        }

        return DuckDBToolResult::ok(ss.str(), {{"triplets", results_json}, {"count", results_json.size()}});
    }

    DuckDBToolResult tool_tag(const json& params) {
        // Notify subconscious of DB activity (idle scheduling)
        if (subconscious_) subconscious_->notify_query();

        auto [db_id, id_str] = parse_id(params);
        std::string add_tag = params.value("add", "");
        std::string remove_tag = params.value("remove", "");

        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        if (add_tag.empty() && remove_tag.empty()) {
            return DuckDBToolResult::error("Either 'add' or 'remove' tag is required");
        }

        std::string result_msg;
        if (!add_tag.empty()) {
            mind_->store().add_tag(db_id, add_tag);
            result_msg = "Added tag '" + add_tag + "'";
        }
        if (!remove_tag.empty()) {
            mind_->store().remove_tag(db_id, remove_tag);
            if (!result_msg.empty()) result_msg += ", ";
            result_msg += "Removed tag '" + remove_tag + "'";
        }

        return DuckDBToolResult::ok(result_msg + " on node " + id_str);
    }

    // Maintenance tool implementations
    DuckDBToolResult tool_health_check(const json&) {
        // Fast health check - no database queries to avoid blocking
        // Uses cached stats from last update (updated every maintenance cycle)
        bool is_open = mind_->store().is_open();
        bool yantra = mind_->has_yantra();

        // Get cached stats (non-blocking)
        auto cached = mind_->store().cached_health();

        // Get execution provider from embedder's yantra
        std::string exec_provider = "N/A";
        auto embedder_yantra = mind_->embedder_yantra();
        if (embedder_yantra) {
            exec_provider = embedder_yantra->execution_provider_name();
        }

        // Count stale sessions (with dead PIDs) - use public session_list API
        auto stale_list = mind_->store().session_list("", "stale");
        int64_t stale_sessions = static_cast<int64_t>(stale_list.size());

        // Count queue items and failed observations
        int64_t queue_depth = 0;
        int64_t failed_observations = 0;
        const char* home = std::getenv("HOME");
        if (home) {
            std::string mind_dir = std::string(home) + "/.claude/mind";

            // Count queue items
            std::string queue_file = mind_dir + "/.queue.jsonl";
            std::ifstream qf(queue_file);
            if (qf) {
                std::string line;
                while (std::getline(qf, line)) {
                    if (!line.empty()) queue_depth++;
                }
            }

            // Count failed observations
            std::string failed_file = mind_dir + "/.failed_observations.jsonl";
            std::ifstream ff(failed_file);
            if (ff) {
                std::string line;
                while (std::getline(ff, line)) {
                    if (!line.empty()) failed_observations++;
                }
            }
        }

        std::ostringstream ss;
        ss << "Health Check:\n";
        ss << "  Status: " << (is_open ? "OK" : "ERROR") << "\n";
        ss << "  Memories: " << cached.total_memories << "\n";
        ss << "  Symbols: " << cached.total_symbols << "\n";
        ss << "  Triplets: " << cached.total_triplets << "\n";
        ss << "  Avg Confidence: " << cached.avg_confidence << "\n";
        ss << "  Yantra: " << (yantra ? "ready" : "not attached") << "\n";
        ss << "  Execution: " << exec_provider << "\n";
        ss << "  Stale Sessions: " << stale_sessions << "\n";
        ss << "  Queue Depth: " << queue_depth << "\n";
        ss << "  Failed Observations: " << failed_observations << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"status", is_open ? "ok" : "error"},
            {"daemon", "healthy"},
            {"software_version", CHITTA_VERSION},
            {"protocol_major", CHITTA_PROTOCOL_VERSION_MAJOR},
            {"protocol_minor", CHITTA_PROTOCOL_VERSION_MINOR},
            {"memories", cached.total_memories},
            {"symbols", cached.total_symbols},
            {"triplets", cached.total_triplets},
            {"avg_confidence", cached.avg_confidence},
            {"yantra_ready", yantra},
            {"execution_provider", exec_provider},
            {"stale_sessions", stale_sessions},
            {"queue_depth", queue_depth},
            {"failed_observations", failed_observations}
        });
    }

    DuckDBToolResult tool_version_check() {
        return DuckDBToolResult::ok(
            "cc-soul " + std::string(CHITTA_VERSION) + " (DuckDB backend)",
            {{"version", CHITTA_VERSION}, {"backend", "duckdb"}}
        );
    }

    DuckDBToolResult tool_cycle(const json& params) {
        bool force = params.value("force", false);

        size_t decayed = mind_->tick();
        std::ostringstream ss;
        ss << "Maintenance cycle complete:\n";
        ss << "  Decayed: " << decayed << " nodes\n";

        return DuckDBToolResult::ok(ss.str(), {{"decayed", decayed}, {"forced", force}});
    }

    DuckDBToolResult tool_cleanup(const json& params) {
        bool dry_run = params.value("dry_run", true);

        // Find low-confidence nodes that should be removed
        auto health = mind_->store().health();
        size_t removed = 0;

        if (!dry_run) {
            removed = mind_->store().prune(0.1f, 7.0f);  // Remove nodes with <10% confidence after 7 days
        }

        std::ostringstream ss;
        ss << "Cleanup " << (dry_run ? "(dry run)" : "") << ":\n";
        ss << "  Total memories: " << health.total_memories << "\n";
        ss << "  Removed: " << removed << " weak nodes\n";

        return DuckDBToolResult::ok(ss.str(), {{"removed", removed}, {"dry_run", dry_run}});
    }

    // Import/Export tool implementations
    DuckDBToolResult tool_import_soul(const json& params) {
        std::string file = params.value("file", "");
        std::string content = params.value("content", "");

        if (file.empty() && content.empty()) {
            return DuckDBToolResult::error("Either file or content is required");
        }

        std::string ssl_content = content;
        if (!file.empty()) {
            std::ifstream f(file);
            if (!f) {
                return DuckDBToolResult::error("Cannot read file: " + file);
            }
            std::stringstream buffer;
            buffer << f.rdbuf();
            ssl_content = buffer.str();
        }

        // Parse SSL content
        size_t learns = 0;
        size_t triplets = 0;
        std::istringstream stream(ssl_content);
        std::string line;
        std::string current_learn;

        while (std::getline(stream, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') continue;

            // [LEARN] lines
            if (line.find("[LEARN]") == 0 || line.find("[REMEMBER]") == 0) {
                if (!current_learn.empty()) {
                    mind_->remember(current_learn, NodeType::Wisdom);
                    learns++;
                }
                current_learn = line.substr(line.find(']') + 1);
                // Trim leading space
                while (!current_learn.empty() && current_learn[0] == ' ') {
                    current_learn = current_learn.substr(1);
                }
            }
            // [ε] expansion lines - append to current learn
            else if (line.find("[ε]") == 0 || line.find("[e]") == 0) {
                if (!current_learn.empty()) {
                    current_learn += "\n" + line;
                }
            }
            // [TRIPLET] lines
            else if (line.find("[TRIPLET]") == 0) {
                std::string triplet = line.substr(10);
                // Parse "subject predicate object"
                std::istringstream ts(triplet);
                std::string subj, pred, obj;
                ts >> subj >> pred;
                std::getline(ts, obj);
                // Trim obj
                while (!obj.empty() && obj[0] == ' ') obj = obj.substr(1);

                if (!subj.empty() && !pred.empty() && !obj.empty()) {
                    mind_->connect(subj, pred, obj);
                    triplets++;
                }
            }
        }

        // Don't forget the last learn
        if (!current_learn.empty()) {
            mind_->remember(current_learn, NodeType::Wisdom);
            learns++;
        }

        std::ostringstream ss;
        ss << "Imported:\n";
        ss << "  [LEARN] entries: " << learns << "\n";
        ss << "  [TRIPLET] entries: " << triplets << "\n";

        return DuckDBToolResult::ok(ss.str(), {{"learns", learns}, {"triplets", triplets}});
    }

    DuckDBToolResult tool_export_soul(const json& params) {
        std::string file = params.value("file", "");
        std::string tag = params.value("tag", "");
        size_t limit = params.value("limit", 100);

        // Get memories
        std::vector<MemoryResult> memories;
        // For now, do a broad recall to get memories
        if (!tag.empty()) {
            // TODO: Add tag-based recall to store
            return DuckDBToolResult::error("Tag-based export not yet implemented");
        }

        // Export all (limited)
        auto all = mind_->recall("*", limit);  // Broad query

        std::ostringstream ss;
        ss << "# Soul Export\n";
        ss << "# Generated by cc-soul " << CHITTA_VERSION << "\n\n";

        for (const auto& m : all) {
            ss << "[LEARN] " << m.text.substr(0, 200) << "\n";
            if (m.text.size() > 200) {
                ss << "[ε] Full content truncated\n";
            }
            ss << "\n";
        }

        if (!file.empty()) {
            std::ofstream f(file);
            if (!f) {
                return DuckDBToolResult::error("Cannot write to file: " + file);
            }
            f << ss.str();
            return DuckDBToolResult::ok("Exported " + std::to_string(all.size()) + " memories to " + file,
                                        {{"count", all.size()}, {"file", file}});
        }

        return DuckDBToolResult::ok(ss.str(), {{"count", all.size()}});
    }

    // Realm tool implementations
    DuckDBToolResult tool_realm_list() {
        auto realms = mind_->store().list_realms();

        std::ostringstream ss;
        ss << "Known realms (" << realms.size() << "):\n";
        for (const auto& r : realms) {
            ss << "  - " << r << "\n";
        }

        return DuckDBToolResult::ok(ss.str(), {{"realms", realms}, {"count", realms.size()}});
    }

    DuckDBToolResult tool_realm_get(const json& params) {
        auto [db_id, id_str] = parse_id(params);
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        auto realms = mind_->store().get_realms(db_id);
        if (realms.empty()) {
            return DuckDBToolResult::ok("Memory not found or has no realms", {{"realms", json::array()}});
        }

        std::ostringstream ss;
        ss << "Memory " << id_str << " belongs to:\n";
        ss << "  Primary: " << realms[0] << "\n";
        if (realms.size() > 1) {
            ss << "  Shared: ";
            for (size_t i = 1; i < realms.size(); ++i) {
                if (i > 1) ss << ", ";
                ss << realms[i];
            }
            ss << "\n";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"primary", realms[0]},
            {"shared", json(std::vector<std::string>(realms.begin() + 1, realms.end()))},
            {"all", realms}
        });
    }

    DuckDBToolResult tool_realm_set(const json& params) {
        auto [db_id, id_str] = parse_id(params);
        std::string realm = params.value("realm", "");

        if (id_str.empty() || realm.empty()) {
            return DuckDBToolResult::error("ID and realm are required");
        }

        bool ok = mind_->store().set_realm(db_id, realm);
        if (!ok) {
            return DuckDBToolResult::error("Failed to set realm");
        }

        return DuckDBToolResult::ok("Set primary realm to '" + realm + "' for memory " + id_str);
    }

    DuckDBToolResult tool_realm_add(const json& params) {
        auto [db_id, id_str] = parse_id(params);
        std::string realm = params.value("realm", "");

        if (id_str.empty() || realm.empty()) {
            return DuckDBToolResult::error("ID and realm are required");
        }

        bool ok = mind_->store().add_to_realm(db_id, realm);
        if (!ok) {
            return DuckDBToolResult::error("Failed to add to realm");
        }

        return DuckDBToolResult::ok("Added memory " + id_str + " to realm '" + realm + "'");
    }

    DuckDBToolResult tool_realm_remove(const json& params) {
        auto [db_id, id_str] = parse_id(params);
        std::string realm = params.value("realm", "");

        if (id_str.empty() || realm.empty()) {
            return DuckDBToolResult::error("ID and realm are required");
        }

        bool ok = mind_->store().remove_from_realm(db_id, realm);
        if (!ok) {
            return DuckDBToolResult::error("Failed to remove from realm");
        }

        return DuckDBToolResult::ok("Removed memory " + id_str + " from realm '" + realm + "'");
    }

    DuckDBToolResult tool_realm_visibility(const json& params) {
        auto [db_id, id_str] = parse_id(params);
        int visibility = params.value("visibility", 0);

        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        if (visibility < 0 || visibility > 2) {
            return DuckDBToolResult::error("Visibility must be 0 (Private), 1 (Shared), or 2 (Global)");
        }

        bool ok = mind_->store().set_visibility(db_id, static_cast<RealmVisibility>(visibility));
        if (!ok) {
            return DuckDBToolResult::error("Failed to set visibility");
        }

        std::string vis_name = visibility == 0 ? "Private" : (visibility == 1 ? "Shared" : "Global");
        return DuckDBToolResult::ok("Set visibility to " + vis_name + " for memory " + id_str);
    }

    DuckDBToolResult tool_realm_detect() {
        // Detect realm from environment/git/config
        // Priority: CHITTA_REALM env > .cc-soul-realm file > git repo name > "brahman"

        // 1. Environment variable
        if (const char* env_realm = std::getenv("CHITTA_REALM")) {
            std::string realm = env_realm;
            return DuckDBToolResult::ok("Realm detected from environment: " + realm, {{"realm", realm}, {"source", "env"}});
        }

        // 2. Config file in current directory
        std::ifstream realm_file(".cc-soul-realm");
        if (realm_file.good()) {
            std::string realm;
            std::getline(realm_file, realm);
            // Trim whitespace
            realm.erase(0, realm.find_first_not_of(" \t\n\r"));
            realm.erase(realm.find_last_not_of(" \t\n\r") + 1);
            if (!realm.empty()) {
                return DuckDBToolResult::ok("Realm detected from .cc-soul-realm file: " + realm, {{"realm", realm}, {"source", "file"}});
            }
        }

        // 3. Git repository name
        std::array<char, 256> buffer;
        std::string git_root;
        FILE* pipe = popen("git rev-parse --show-toplevel 2>/dev/null", "r");
        if (pipe) {
            if (fgets(buffer.data(), buffer.size(), pipe)) {
                git_root = buffer.data();
                // Remove trailing newline
                if (!git_root.empty() && git_root.back() == '\n') {
                    git_root.pop_back();
                }
            }
            pclose(pipe);
        }

        if (!git_root.empty()) {
            // Extract repo name from path
            size_t last_slash = git_root.rfind('/');
            std::string repo_name = (last_slash != std::string::npos)
                ? git_root.substr(last_slash + 1)
                : git_root;
            std::string realm = "project:" + repo_name;
            return DuckDBToolResult::ok("Realm detected from git repository: " + realm, {{"realm", realm}, {"source", "git"}});
        }

        // 4. Default
        std::string realm = "brahman";
        return DuckDBToolResult::ok("Realm defaulted to: " + realm, {{"realm", realm}, {"source", "default"}});
    }

    // Ledger tool implementations
    DuckDBToolResult tool_ledger_save(const json& params) {
        std::string session_id = get_session_id(params);

        LedgerEntry entry;
        entry.session_id = session_id;
        entry.project = params.value("project", "default");
        entry.transcript_path = params.value("transcript_path", "");
        entry.mood = params.value("mood", "");
        entry.coherence = params.value("coherence", 0.0f);
        entry.confidence = params.value("confidence", 0.0f);

        // Convert arrays to JSON strings
        if (params.contains("todos") && params["todos"].is_array()) {
            entry.todos = params["todos"].dump();
        }
        if (params.contains("active_files") && params["active_files"].is_array()) {
            entry.active_files = params["active_files"].dump();
        }
        if (params.contains("decisions") && params["decisions"].is_array()) {
            entry.decisions = params["decisions"].dump();
        }
        if (params.contains("next_steps") && params["next_steps"].is_array()) {
            entry.next_steps = params["next_steps"].dump();
        }
        if (params.contains("blockers") && params["blockers"].is_array()) {
            entry.blockers = params["blockers"].dump();
        }
        if (params.contains("discoveries") && params["discoveries"].is_array()) {
            entry.discoveries = params["discoveries"].dump();
        }

        entry.snapshot = params.value("snapshot", "");

        int64_t id = mind_->store().save_ledger(entry);
        if (id < 0) {
            return DuckDBToolResult::error("Failed to save checkpoint");
        }

        std::ostringstream ss;
        ss << "Checkpoint saved:\n";
        ss << "  ID: " << id << "\n";
        ss << "  Session: " << session_id << "\n";
        ss << "  Project: " << entry.project << "\n";
        if (!entry.mood.empty()) ss << "  Mood: " << entry.mood << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"id", id},
            {"session_id", session_id},
            {"project", entry.project}
        });
    }

    DuckDBToolResult tool_ledger_load(const json& params) {
        std::string session_id = params.value("session_id", "");
        std::string project = params.value("project", "");

        auto entry = mind_->store().load_ledger(session_id, project);
        if (!entry) {
            return DuckDBToolResult::ok("No checkpoint found", {{"found", false}});
        }

        std::ostringstream ss;
        ss << "Checkpoint loaded:\n";
        ss << "  ID: " << entry->id << "\n";
        ss << "  Session: " << entry->session_id << "\n";
        ss << "  Project: " << entry->project << "\n";
        if (!entry->transcript_path.empty()) ss << "  Transcript: " << entry->transcript_path << "\n";
        if (!entry->mood.empty()) ss << "  Mood: " << entry->mood << "\n";
        if (entry->coherence > 0) ss << "  Coherence: " << entry->coherence << "\n";
        if (entry->confidence > 0) ss << "  Confidence: " << entry->confidence << "\n";

        // Parse JSON fields back to arrays for structured output
        json result = {
            {"found", true},
            {"id", entry->id},
            {"session_id", entry->session_id},
            {"project", entry->project},
            {"transcript_path", entry->transcript_path},
            {"created_at", entry->created_at},
            {"mood", entry->mood},
            {"coherence", entry->coherence},
            {"confidence", entry->confidence},
            {"snapshot", entry->snapshot}
        };

        // Parse JSON strings back to JSON arrays
        auto parse_json_field = [](const std::string& s) -> json {
            if (s.empty()) return json::array();
            try {
                return json::parse(s);
            } catch (...) {
                return json::array();
            }
        };

        result["todos"] = parse_json_field(entry->todos);
        result["active_files"] = parse_json_field(entry->active_files);
        result["decisions"] = parse_json_field(entry->decisions);
        result["next_steps"] = parse_json_field(entry->next_steps);
        result["blockers"] = parse_json_field(entry->blockers);
        result["discoveries"] = parse_json_field(entry->discoveries);

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_ledger_list(const json& params) {
        std::string project = params.value("project", "");
        size_t limit = params.value("limit", 10);

        auto entries = mind_->store().list_ledgers(project, limit);

        std::ostringstream ss;
        ss << "Checkpoints";
        if (!project.empty()) ss << " for project '" << project << "'";
        ss << " (" << entries.size() << "):\n\n";

        json entries_json = json::array();
        for (const auto& entry : entries) {
            // Format timestamp
            auto ts = entry.created_at / 1000;
            std::time_t t = static_cast<std::time_t>(ts);
            char time_buf[32];
            std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", std::localtime(&t));

            ss << "  [" << entry.id << "] " << time_buf << " - " << entry.session_id;
            if (!entry.mood.empty()) ss << " (" << entry.mood << ")";
            ss << "\n";

            entries_json.push_back({
                {"id", entry.id},
                {"session_id", entry.session_id},
                {"project", entry.project},
                {"transcript_path", entry.transcript_path},
                {"created_at", entry.created_at},
                {"mood", entry.mood}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"entries", entries_json}, {"count", entries.size()}});
    }

    DuckDBToolResult tool_ledger_get(const json& params) {
        int64_t id = params.value("id", 0LL);
        if (id <= 0) {
            return DuckDBToolResult::error("Valid ID is required");
        }

        auto entry = mind_->store().get_ledger(id);
        if (!entry) {
            return DuckDBToolResult::error("Checkpoint not found: " + std::to_string(id));
        }

        std::ostringstream ss;
        ss << "Checkpoint " << id << ":\n";
        ss << "  Session: " << entry->session_id << "\n";
        ss << "  Project: " << entry->project << "\n";
        if (!entry->transcript_path.empty()) ss << "  Transcript: " << entry->transcript_path << "\n";
        if (!entry->mood.empty()) ss << "  Mood: " << entry->mood << "\n";
        if (entry->coherence > 0) ss << "  Coherence: " << entry->coherence << "\n";
        if (entry->confidence > 0) ss << "  Confidence: " << entry->confidence << "\n";
        if (!entry->snapshot.empty()) {
            ss << "\nSnapshot:\n" << entry->snapshot << "\n";
        }

        auto parse_json_field = [](const std::string& s) -> json {
            if (s.empty()) return json::array();
            try {
                return json::parse(s);
            } catch (...) {
                return json::array();
            }
        };

        json result = {
            {"id", entry->id},
            {"session_id", entry->session_id},
            {"project", entry->project},
            {"transcript_path", entry->transcript_path},
            {"created_at", entry->created_at},
            {"mood", entry->mood},
            {"coherence", entry->coherence},
            {"confidence", entry->confidence},
            {"snapshot", entry->snapshot},
            {"todos", parse_json_field(entry->todos)},
            {"active_files", parse_json_field(entry->active_files)},
            {"decisions", parse_json_field(entry->decisions)},
            {"next_steps", parse_json_field(entry->next_steps)},
            {"blockers", parse_json_field(entry->blockers)},
            {"discoveries", parse_json_field(entry->discoveries)}
        };

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_ledger_delete(const json& params) {
        int64_t id = params.value("id", 0LL);
        if (id <= 0) {
            return DuckDBToolResult::error("Valid ID is required");
        }

        bool ok = mind_->store().delete_ledger(id);
        if (!ok) {
            return DuckDBToolResult::error("Failed to delete checkpoint " + std::to_string(id));
        }

        return DuckDBToolResult::ok("Deleted checkpoint " + std::to_string(id), {{"id", id}, {"deleted", true}});
    }

    // Long-running task tools
    DuckDBToolResult tool_long_task_start(const json& params) {
        std::string task_id = params.value("task_id", "");
        std::string goal = params.value("goal", "");

        if (task_id.empty() || goal.empty()) {
            return DuckDBToolResult::error("task_id and goal are required");
        }

        LongTask task;
        task.task_id = task_id;
        task.goal = goal;
        task.realm = params.value("realm", "brahman");

        if (params.contains("hard_checks") && params["hard_checks"].is_array()) {
            task.hard_checks = params["hard_checks"].dump();
        }
        if (params.contains("soft_checks") && params["soft_checks"].is_array()) {
            task.soft_checks = params["soft_checks"].dump();
        }
        if (params.contains("work_items") && params["work_items"].is_array()) {
            task.work_items = params["work_items"].dump();
        }

        int64_t id = mind_->store().task_start(task);
        if (id < 0) {
            return DuckDBToolResult::error("Failed to start task: " + mind_->store().last_error());
        }

        std::ostringstream ss;
        ss << "Started long task:\n"
           << "  ID: " << task_id << "\n"
           << "  Goal: " << goal.substr(0, 100) << (goal.size() > 100 ? "..." : "") << "\n"
           << "  Realm: " << task.realm;

        return DuckDBToolResult::ok(ss.str(), {
            {"task_id", task_id},
            {"db_id", id},
            {"realm", task.realm},
            {"status", "active"}
        });
    }

    DuckDBToolResult tool_long_task_get(const json& params) {
        std::string task_id = params.value("task_id", "");
        if (task_id.empty()) {
            return DuckDBToolResult::error("task_id is required");
        }

        auto task = mind_->store().task_get(task_id);
        if (!task) {
            return DuckDBToolResult::error("Task not found: " + task_id);
        }

        json result = {
            {"task_id", task->task_id},
            {"goal", task->goal},
            {"realm", task->realm},
            {"status", task->status},
            {"iterations", task->iterations},
            {"started_at", task->started_at},
            {"updated_at", task->updated_at}
        };

        // Parse JSON fields
        auto parse_json = [](const std::string& s) -> json {
            if (s.empty()) return json::array();
            try { return json::parse(s); } catch (...) { return json::array(); }
        };

        result["hard_checks"] = parse_json(task->hard_checks);
        result["soft_checks"] = parse_json(task->soft_checks);
        result["work_items"] = parse_json(task->work_items);
        result["blockers"] = parse_json(task->blockers);

        if (!task->completed_summary.empty()) result["completed_summary"] = task->completed_summary;
        if (!task->outcome.empty()) result["outcome"] = task->outcome;
        if (task->completed_at > 0) result["completed_at"] = task->completed_at;

        std::ostringstream ss;
        ss << "Task: " << task->task_id << " [" << task->status << "]\n"
           << "Goal: " << task->goal.substr(0, 200) << "\n"
           << "Iterations: " << task->iterations;

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_long_task_active(const json& params) {
        std::string realm = params.value("realm", "");

        auto task = mind_->store().task_get_active(realm);
        if (!task) {
            return DuckDBToolResult::ok("No active task" + (realm.empty() ? "" : " in realm " + realm),
                                        {{"found", false}});
        }

        json result = {
            {"found", true},
            {"task_id", task->task_id},
            {"goal", task->goal},
            {"realm", task->realm},
            {"iterations", task->iterations}
        };

        std::ostringstream ss;
        ss << "Active task: " << task->task_id << "\n"
           << "Goal: " << task->goal.substr(0, 200) << "\n"
           << "Iterations: " << task->iterations;

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_long_task_update(const json& params) {
        std::string task_id = params.value("task_id", "");
        if (task_id.empty()) {
            return DuckDBToolResult::error("task_id is required");
        }

        LongTask updates;
        updates.completed_summary = params.value("completed_summary", "");

        if (params.contains("work_items") && params["work_items"].is_array()) {
            updates.work_items = params["work_items"].dump();
        }
        if (params.contains("blockers") && params["blockers"].is_array()) {
            updates.blockers = params["blockers"].dump();
        }

        updates.iterations = 1;  // Signal to increment

        bool ok = mind_->store().task_update(task_id, updates);
        if (!ok) {
            return DuckDBToolResult::error("Failed to update task: " + task_id);
        }

        return DuckDBToolResult::ok("Updated task: " + task_id, {{"task_id", task_id}, {"updated", true}});
    }

    DuckDBToolResult tool_long_task_complete(const json& params) {
        std::string task_id = params.value("task_id", "");
        std::string outcome = params.value("outcome", "");

        if (task_id.empty() || outcome.empty()) {
            return DuckDBToolResult::error("task_id and outcome are required");
        }

        bool ok = mind_->store().task_complete(task_id, outcome);
        if (!ok) {
            return DuckDBToolResult::error("Failed to complete task: " + task_id);
        }

        return DuckDBToolResult::ok("Completed task: " + task_id, {
            {"task_id", task_id},
            {"status", "completed"},
            {"outcome", outcome}
        });
    }

    DuckDBToolResult tool_long_task_event(const json& params) {
        std::string task_id = params.value("task_id", "");
        std::string kind = params.value("kind", "");

        if (task_id.empty() || kind.empty()) {
            return DuckDBToolResult::error("task_id and kind are required");
        }

        TaskEvent event;
        event.task_id = task_id;
        event.kind = kind;
        event.payload = params.value("payload", "");

        if (params.contains("tags") && params["tags"].is_array()) {
            event.tags = params["tags"].dump();
        }
        if (params.contains("related_entities") && params["related_entities"].is_array()) {
            event.related_entities = params["related_entities"].dump();
        }

        int64_t id = mind_->store().event_append(event);
        if (id < 0) {
            return DuckDBToolResult::error("Failed to append event: " + mind_->store().last_error());
        }

        return DuckDBToolResult::ok("Event logged", {{"event_id", id}, {"task_id", task_id}, {"kind", kind}});
    }

    // Unified checkpoint - consolidates ledger + task_event
    DuckDBToolResult tool_unified_checkpoint(const json& params) {
        std::string realm = params.value("realm", "brahman");
        std::string mood = params.value("mood", "flowing");
        std::string summary = params.value("summary", "");

        // Check for active long task in this realm
        auto active_task = mind_->store().task_get_active(realm);

        json payload = {
            {"mood", mood},
            {"summary", summary}
        };

        if (params.contains("next_steps")) payload["next_steps"] = params["next_steps"];
        if (params.contains("active_files")) payload["active_files"] = params["active_files"];
        if (params.contains("discoveries")) payload["discoveries"] = params["discoveries"];

        if (active_task) {
            // Use long task event system
            TaskEvent event;
            event.task_id = active_task->task_id;
            event.kind = "checkpoint";
            event.payload = payload.dump();

            if (params.contains("active_files") && params["active_files"].is_array()) {
                event.related_entities = params["active_files"].dump();
            }

            int64_t id = mind_->store().event_append(event);

            // Also update task's completed_summary if summary provided
            if (!summary.empty()) {
                LongTask updates;
                updates.completed_summary = summary;
                mind_->store().task_update(active_task->task_id, updates);
            }

            std::ostringstream ss;
            ss << "Checkpoint saved to long task: " << active_task->task_id << "\n"
               << "  Event #" << id << " (kind: checkpoint)\n"
               << "  Mood: " << mood;

            return DuckDBToolResult::ok(ss.str(), {
                {"mode", "long_task"},
                {"task_id", active_task->task_id},
                {"event_id", id}
            });
        } else {
            // Fallback to standalone ledger
            LedgerEntry entry;
            entry.session_id = "checkpoint-" + std::to_string(std::time(nullptr));
            entry.project = realm;
            entry.mood = mood;
            entry.coherence = 0.85f;
            entry.confidence = 0.85f;

            if (params.contains("next_steps") && params["next_steps"].is_array()) {
                entry.next_steps = params["next_steps"].dump();
            }
            if (params.contains("active_files") && params["active_files"].is_array()) {
                entry.active_files = params["active_files"].dump();
            }
            if (params.contains("discoveries") && params["discoveries"].is_array()) {
                entry.discoveries = params["discoveries"].dump();
            }
            entry.snapshot = summary;

            int64_t id = mind_->store().save_ledger(entry);

            return DuckDBToolResult::ok(
                "Checkpoint saved to ledger #" + std::to_string(id) + " (no active long task)",
                {{"mode", "ledger"}, {"ledger_id", id}}
            );
        }
    }

    DuckDBToolResult tool_long_task_snapshot(const json& params) {
        std::string task_id = params.value("task_id", "");
        std::string mode = params.value("mode", "inject");
        size_t max_tokens = params.value("max_tokens", 2000);
        size_t max_chars = max_tokens * 4;  // Rough estimate: ~4 chars per token

        if (task_id.empty()) {
            return DuckDBToolResult::error("task_id is required");
        }

        auto task = mind_->store().task_get(task_id);
        if (!task) {
            return DuckDBToolResult::error("Task not found: " + task_id);
        }

        // Get recent events
        auto events = mind_->store().event_get_recent(task_id, "", 20);

        // Parse JSON fields
        auto parse_json = [](const std::string& s) -> json {
            if (s.empty()) return json::array();
            try { return json::parse(s); } catch (...) { return json::array(); }
        };

        json work_items = parse_json(task->work_items);
        json blockers = parse_json(task->blockers);

        // Build snapshot
        std::ostringstream ss;

        if (mode == "inject") {
            // Compact format for context injection
            ss << "[LONG_TASK:" << task_id << "] " << task->goal << "\n";
            ss << "Iteration: " << task->iterations << " | Status: " << task->status << "\n";

            if (!task->completed_summary.empty()) {
                ss << "Done: " << task->completed_summary.substr(0, 200) << "\n";
            }

            if (!work_items.empty()) {
                ss << "Pending: ";
                for (size_t i = 0; i < std::min(work_items.size(), size_t(3)); i++) {
                    if (i > 0) ss << "; ";
                    ss << work_items[i].get<std::string>().substr(0, 50);
                }
                if (work_items.size() > 3) ss << " (+" << (work_items.size() - 3) << " more)";
                ss << "\n";
            }

            if (!blockers.empty()) {
                ss << "BLOCKED: " << blockers[0].get<std::string>() << "\n";
            }

            // Recent significant events
            int event_count = 0;
            for (const auto& e : events) {
                if (e.kind == "error" || e.kind == "decision") {
                    ss << "[" << e.kind << "] " << e.payload.substr(0, 100) << "\n";
                    if (++event_count >= 2) break;
                }
            }
        } else {
            // Verbose debug format
            ss << "=== Task Snapshot: " << task_id << " ===\n\n";
            ss << "Goal: " << task->goal << "\n";
            ss << "Status: " << task->status << "\n";
            ss << "Realm: " << task->realm << "\n";
            ss << "Iterations: " << task->iterations << "\n\n";

            ss << "Completed: " << task->completed_summary << "\n\n";

            ss << "Work Items:\n";
            for (const auto& item : work_items) {
                ss << "  - " << item.get<std::string>() << "\n";
            }

            ss << "\nBlockers:\n";
            for (const auto& b : blockers) {
                ss << "  ! " << b.get<std::string>() << "\n";
            }

            ss << "\nRecent Events (" << events.size() << "):\n";
            for (const auto& e : events) {
                ss << "  [" << e.kind << "] " << e.payload.substr(0, 200) << "\n";
            }
        }

        std::string snapshot = ss.str();
        bool truncated = false;
        if (snapshot.size() > max_chars) {
            snapshot = snapshot.substr(0, max_chars) + "\n... (truncated)";
            truncated = true;
        }

        json result = {
            {"task_id", task_id},
            {"status", task->status},
            {"iterations", task->iterations},
            {"event_count", events.size()},
            {"truncated", truncated},
            {"snapshot", snapshot}
        };

        return DuckDBToolResult::ok(snapshot, result);
    }

    DuckDBToolResult tool_long_task_evaluate(const json& params) {
        std::string task_id = params.value("task_id", "");
        if (task_id.empty()) {
            return DuckDBToolResult::error("task_id is required");
        }

        auto task = mind_->store().task_get(task_id);
        if (!task) {
            return DuckDBToolResult::error("Task not found: " + task_id);
        }

        // Parse hard_checks
        auto parse_json = [](const std::string& s) -> json {
            if (s.empty()) return json::array();
            try { return json::parse(s); } catch (...) { return json::array(); }
        };

        json hard_checks = parse_json(task->hard_checks);
        json blockers = parse_json(task->blockers);

        // For now, basic evaluation:
        // - If blockers exist -> blocked
        // - If hard_checks empty -> continue (no criteria defined)
        // - Otherwise -> continue (need semantic evaluation)

        std::string decision = "continue";
        std::vector<std::string> missing;
        float confidence = 0.5f;
        std::string next_prompt;

        if (!blockers.empty()) {
            decision = "blocked";
            confidence = 0.9f;
            for (const auto& b : blockers) {
                missing.push_back(b.get<std::string>());
            }
            next_prompt = "Task is blocked. Blockers: " + blockers.dump();
        } else if (hard_checks.empty()) {
            // No completion criteria defined
            decision = "continue";
            confidence = 0.3f;
            next_prompt = "Continue working on: " + task->goal;
        } else {
            // Has criteria but we can't evaluate them automatically yet
            // This would need shell command execution or semantic evaluation
            decision = "continue";
            confidence = 0.5f;
            next_prompt = "Continue task. Check completion criteria when ready.";
            for (const auto& c : hard_checks) {
                missing.push_back(c.get<std::string>());
            }
        }

        std::ostringstream ss;
        ss << "Evaluation: " << decision << " (confidence: " << confidence << ")\n";
        if (!missing.empty()) {
            ss << "Missing/Blocked:\n";
            for (const auto& m : missing) {
                ss << "  - " << m << "\n";
            }
        }
        ss << "\nNext: " << next_prompt;

        json result = {
            {"decision", decision},
            {"confidence", confidence},
            {"missing", missing},
            {"next_prompt", next_prompt},
            {"iterations", task->iterations}
        };

        return DuckDBToolResult::ok(ss.str(), result);
    }

    // Suggestion tracking (loop closure)
    DuckDBToolResult tool_suggestion_track(const json& params) {
        std::string content = params.value("content", "");
        if (content.empty()) {
            return DuckDBToolResult::error("content is required");
        }

        Suggestion s;
        s.content = content;
        s.context = params.value("context", "");
        s.realm = params.value("realm", "brahman");

        int64_t id = mind_->store().suggestion_track(s);
        if (id < 0) {
            return DuckDBToolResult::error("Failed to track suggestion");
        }

        std::ostringstream ss;
        ss << "Tracked suggestion #" << id << ": " << content.substr(0, 100)
           << (content.size() > 100 ? "..." : "");

        return DuckDBToolResult::ok(ss.str(), {
            {"id", id},
            {"content", content},
            {"realm", s.realm}
        });
    }

    DuckDBToolResult tool_suggestion_pending(const json& params) {
        std::string realm = params.value("realm", "");
        size_t limit = params.value("limit", 20);

        auto suggestions = mind_->store().suggestion_list_pending(realm, limit);

        if (suggestions.empty()) {
            return DuckDBToolResult::ok("No pending suggestions", {{"count", 0}});
        }

        std::ostringstream ss;
        ss << "Pending suggestions (" << suggestions.size() << "):\n";
        json items = json::array();

        for (const auto& s : suggestions) {
            ss << "  #" << s.id << ": " << s.content.substr(0, 80)
               << (s.content.size() > 80 ? "..." : "") << "\n";
            items.push_back({
                {"id", s.id},
                {"content", s.content},
                {"context", s.context},
                {"realm", s.realm},
                {"suggested_at", s.suggested_at}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"count", suggestions.size()},
            {"suggestions", items}
        });
    }

    DuckDBToolResult tool_suggestion_resolve(const json& params) {
        int64_t id = params.value("id", 0);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        bool helped = params.value("helped", false);
        std::string details = params.value("details", "");

        // First, create an outcome memory
        std::string outcome_type = helped ? "worked" : "failed";
        auto suggestion = mind_->store().suggestion_get(id);
        if (!suggestion) {
            return DuckDBToolResult::error("Suggestion not found");
        }

        // Store outcome as memory
        std::string memory_content = "[outcome:" + outcome_type + "] " + suggestion->content;
        if (!details.empty()) {
            memory_content += "\nDetails: " + details;
        }

        // Remember the outcome
        std::vector<float> embed;
        if (mind_->embedder_ready()) {
            embed = mind_->embedder().embed(memory_content).data;
        }
        int64_t memory_id = mind_->store().remember(
            memory_content,
            "episode",
            embed,
            0.8f,       // confidence
            0.05f,      // decay_rate
            suggestion->realm,
            RealmVisibility::Global  // Outcomes are globally visible
        );

        // Resolve the suggestion
        bool ok = mind_->store().suggestion_resolve(id, helped, details, memory_id);
        if (!ok) {
            return DuckDBToolResult::error("Failed to resolve suggestion");
        }

        // Create triplet for feedback tracking
        std::string slug = suggestion->content.substr(0, 40);
        for (char& c : slug) {
            if (c == ' ') c = '_';
            else c = std::tolower(c);
        }
        mind_->store().connect(slug, "resulted_in", outcome_type);

        std::ostringstream ss;
        ss << "Resolved suggestion #" << id << "\n"
           << "  Helped: " << (helped ? "yes" : "no") << "\n"
           << "  Memory: #" << memory_id;

        return DuckDBToolResult::ok(ss.str(), {
            {"id", id},
            {"helped", helped},
            {"memory_id", memory_id}
        });
    }

    DuckDBToolResult tool_suggestion_count(const json& params) {
        std::string realm = params.value("realm", "");
        size_t count = mind_->store().suggestion_count_pending(realm);

        return DuckDBToolResult::ok(
            "Pending suggestions: " + std::to_string(count),
            {{"count", count}, {"realm", realm.empty() ? "all" : realm}}
        );
    }

    // Memory consolidation
    DuckDBToolResult tool_consolidation_scan(const json& params) {
        float threshold = params.value("similarity_threshold", 0.85f);
        size_t limit = params.value("limit", 20);
        std::string realm = params.value("realm", "");

        auto candidates = mind_->store().consolidation_scan(threshold, limit, realm);

        if (candidates.empty()) {
            return DuckDBToolResult::ok("No similar memory pairs found", {{"count", 0}});
        }

        std::ostringstream ss;
        ss << "Found " << candidates.size() << " consolidation candidates:\n\n";
        json items = json::array();

        for (const auto& c : candidates) {
            int pct = static_cast<int>(c.similarity * 100);
            ss << "[" << pct << "%] #" << c.primary_id << " <-> #" << c.secondary_id << "\n"
               << "  Primary: " << c.primary_content.substr(0, 60) << "...\n"
               << "  Secondary: " << c.secondary_content.substr(0, 60) << "...\n\n";

            items.push_back({
                {"primary_id", c.primary_id},
                {"secondary_id", c.secondary_id},
                {"similarity", c.similarity},
                {"primary_preview", c.primary_content.substr(0, 100)},
                {"secondary_preview", c.secondary_content.substr(0, 100)}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"count", candidates.size()},
            {"candidates", items}
        });
    }

    DuckDBToolResult tool_consolidation_merge(const json& params) {
        int64_t primary_id = params.value("primary_id", 0);
        int64_t secondary_id = params.value("secondary_id", 0);
        std::string merged_content = params.value("merged_content", "");

        if (primary_id <= 0 || secondary_id <= 0) {
            return DuckDBToolResult::error("primary_id and secondary_id are required");
        }

        bool ok = mind_->store().consolidation_merge(primary_id, secondary_id, merged_content);
        if (!ok) {
            return DuckDBToolResult::error("Failed to merge memories");
        }

        std::ostringstream ss;
        ss << "Merged memories:\n"
           << "  Primary #" << primary_id << " absorbed #" << secondary_id << "\n"
           << "  Secondary marked for pruning";

        return DuckDBToolResult::ok(ss.str(), {
            {"primary_id", primary_id},
            {"secondary_id", secondary_id},
            {"merged", true}
        });
    }

    DuckDBToolResult tool_consolidation_auto(const json& params) {
        float threshold = params.value("similarity_threshold", 0.90f);
        size_t max_merges = params.value("max_merges", 20);

        size_t merged = mind_->store().consolidation_auto(threshold, max_merges);

        std::ostringstream ss;
        ss << "Auto-consolidation complete:\n"
           << "  Merged " << merged << " memory pairs\n"
           << "  Threshold: " << static_cast<int>(threshold * 100) << "%";

        return DuckDBToolResult::ok(ss.str(), {
            {"merged_count", merged},
            {"threshold", threshold}
        });
    }

    // Meta-cognition (self-reflection)
    DuckDBToolResult tool_metacognition_corrections(const json& params) {
        size_t limit = params.value("limit", 50);

        // Query memories tagged as corrections (tags in separate table)
        std::string sql = "SELECT DISTINCT m.content, m.confidence, m.created_at FROM memory m "
                          "LEFT JOIN memory_tags t ON m.id = t.memory_id "
                          "WHERE m.content LIKE '%[correction]%' OR t.tag = 'correction' "
                          "ORDER BY m.created_at DESC LIMIT " + std::to_string(limit);

        auto result = mind_->store().raw_query(sql);
        if (!result) {
            return DuckDBToolResult::error("Failed to query corrections");
        }

        std::vector<std::string> corrections;
        std::map<std::string, int> domains;  // Domain -> count

        auto chunk = result->Fetch();
        while (chunk && chunk->size() > 0) {
            for (size_t i = 0; i < chunk->size(); i++) {
                std::string content = chunk->GetValue(0, i).ToString();
                corrections.push_back(content);

                // Extract domain from content like "[correction:domain]"
                size_t start = content.find("[correction:");
                if (start != std::string::npos) {
                    size_t end = content.find("]", start);
                    if (end != std::string::npos) {
                        std::string domain = content.substr(start + 12, end - start - 12);
                        domains[domain]++;
                    }
                } else {
                    domains["general"]++;
                }
            }
            chunk = result->Fetch();
        }

        std::ostringstream ss;
        ss << "Correction analysis (" << corrections.size() << " corrections):\n\n";

        if (!domains.empty()) {
            ss << "Domains with corrections:\n";
            for (const auto& [domain, count] : domains) {
                ss << "  " << domain << ": " << count << "\n";
            }
        }

        // Sample recent corrections
        ss << "\nRecent corrections:\n";
        for (size_t i = 0; i < std::min(corrections.size(), size_t(5)); i++) {
            ss << "  - " << corrections[i].substr(0, 100) << "...\n";
        }

        json result_json = {
            {"total_corrections", corrections.size()},
            {"domains", domains}
        };

        return DuckDBToolResult::ok(ss.str(), result_json);
    }

    DuckDBToolResult tool_metacognition_outcomes(const json& params) {
        size_t limit = params.value("limit", 50);

        // Query outcome memories
        std::string sql = "SELECT content, tags, created_at FROM memory "
                          "WHERE content LIKE '%[outcome:%' "
                          "ORDER BY created_at DESC LIMIT " + std::to_string(limit);

        auto result = mind_->store().raw_query(sql);
        if (!result) {
            return DuckDBToolResult::error("Failed to query outcomes");
        }

        int worked = 0, failed = 0;
        std::vector<std::string> worked_examples, failed_examples;

        auto chunk = result->Fetch();
        while (chunk && chunk->size() > 0) {
            for (size_t i = 0; i < chunk->size(); i++) {
                std::string content = chunk->GetValue(0, i).ToString();
                if (content.find("[outcome:worked]") != std::string::npos) {
                    worked++;
                    if (worked_examples.size() < 3) worked_examples.push_back(content);
                } else if (content.find("[outcome:failed]") != std::string::npos) {
                    failed++;
                    if (failed_examples.size() < 3) failed_examples.push_back(content);
                }
            }
            chunk = result->Fetch();
        }

        int total = worked + failed;
        float success_rate = total > 0 ? (float)worked / total * 100.0f : 0.0f;

        std::ostringstream ss;
        ss << "Outcome analysis:\n\n"
           << "  Total tracked: " << total << "\n"
           << "  Worked: " << worked << " (" << std::fixed << std::setprecision(1) << success_rate << "%)\n"
           << "  Failed: " << failed << "\n";

        if (!failed_examples.empty()) {
            ss << "\nRecent failures (learn from these):\n";
            for (const auto& ex : failed_examples) {
                ss << "  - " << ex.substr(0, 80) << "...\n";
            }
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"total", total},
            {"worked", worked},
            {"failed", failed},
            {"success_rate", success_rate}
        });
    }

    DuckDBToolResult tool_chitta_health(const json& /*params*/) {
        json metrics;
        std::ostringstream report;
        bool healthy = true;

        // 1. Correction implementation rate
        // correction_detected=true means mistake was repeated (bad); false means followed (good)
        {
            auto r = mind_->store().execute_sql_query(
                "SELECT COUNT(*) AS total, "
                "       COUNT(CASE WHEN correction_detected = false THEN 1 END) AS followed "
                "FROM correction_outcome");
            int64_t total = 0, followed = 0;
            if (r.success && !r.rows.empty() && r.rows[0].size() >= 2) {
                try { total   = std::stoll(r.rows[0][0]); } catch (...) {}
                try { followed = std::stoll(r.rows[0][1]); } catch (...) {}
            }
            double rate = total > 0 ? (double)followed / total : -1.0;
            metrics["correction_implementation_rate"] = rate < 0 ? "no_data" : std::to_string((int)(rate * 100)) + "%";
            if (rate >= 0 && rate < 0.60) healthy = false;
            report << "Correction follow rate: "
                   << (rate < 0 ? "no data" : std::to_string((int)(rate * 100)) + "% (" + std::to_string(followed) + "/" + std::to_string(total) + ")")
                   << (rate >= 0 && rate < 0.60 ? " ⚠ NEEDS ATTENTION" : "") << "\n";
        }

        // 2. Recurring corrections (same mistake repeated 2+ times)
        {
            auto r = mind_->store().execute_sql_query(
                "SELECT correction_memory_id, COUNT(*) AS repeats "
                "FROM correction_outcome WHERE correction_detected = true "
                "GROUP BY correction_memory_id HAVING COUNT(*) >= 2 "
                "ORDER BY repeats DESC LIMIT 5");
            int recurring = r.success ? (int)r.rows.size() : 0;
            if (recurring > 0) healthy = false;
            json arr = json::array();
            for (const auto& row : r.rows) {
                if (row.size() >= 2) arr.push_back({{"memory_id", row[0]}, {"repeats", row[1]}});
            }
            metrics["recurring_corrections"] = arr;
            report << "Recurring mistakes: " << recurring
                   << (recurring > 0 ? " ⚠ NEEDS ATTENTION — see recurring_corrections" : " (none)") << "\n";
        }

        // 3. Dream synthesis lag — dreams completed without synthesis
        {
            auto r = mind_->store().execute_sql_query(
                "SELECT COUNT(*) FROM dream d "
                "WHERE d.status = 'woke' AND d.memories_created >= 3 "
                "  AND NOT EXISTS ("
                "    SELECT 1 FROM triplets t "
                "    WHERE t.subject = 'dream:' || CAST(d.id AS VARCHAR) "
                "      AND t.predicate = 'synthesized_by'"
                "  )");
            int unsynth = 0;
            if (r.success && !r.rows.empty() && !r.rows[0].empty()) {
                try { unsynth = std::stoi(r.rows[0][0]); } catch (...) {}
            }
            if (unsynth > 2) healthy = false;
            metrics["unsynth_dreams"] = unsynth;
            report << "Dreams awaiting synthesis: " << unsynth
                   << (unsynth > 2 ? " ⚠ NEEDS ATTENTION" : "") << "\n";
        }

        // 4. Memory type distribution
        {
            auto r = mind_->store().execute_sql_query(
                "SELECT "
                "  COUNT(CASE WHEN content LIKE '%[correction]%' THEN 1 END) AS corrections, "
                "  COUNT(CASE WHEN content LIKE '%[gap]%' THEN 1 END) AS gaps, "
                "  COUNT(CASE WHEN content LIKE '%[curiosity]%' THEN 1 END) AS curiosity, "
                "  COUNT(CASE WHEN content LIKE '%[dream]%' THEN 1 END) AS dreams, "
                "  COUNT(CASE WHEN content LIKE '%[meta-correction]%' THEN 1 END) AS meta_corrections "
                "FROM memory");
            if (r.success && !r.rows.empty() && r.rows[0].size() >= 5) {
                json dist;
                dist["corrections"]      = r.rows[0][0];
                dist["gaps"]             = r.rows[0][1];
                dist["curiosity"]        = r.rows[0][2];
                dist["dreams"]           = r.rows[0][3];
                dist["meta_corrections"] = r.rows[0][4];
                metrics["memory_type_distribution"] = dist;
                report << "Memory types — corrections:" << r.rows[0][0]
                       << " gaps:" << r.rows[0][1]
                       << " curiosity:" << r.rows[0][2]
                       << " dreams:" << r.rows[0][3]
                       << " meta-corrections:" << r.rows[0][4] << "\n";
            }
        }

        std::string status = healthy ? "HEALTHY" : "NEEDS ATTENTION";
        metrics["status"] = status;
        report << "\nOverall: " << status << "\n";
        return DuckDBToolResult::ok(report.str(), metrics);
    }

    DuckDBToolResult tool_metacognition_evaluate(const json& params) {
        // Get various learning metrics
        std::map<std::string, int> tag_counts;

        // Count memories by key learning tags (tags stored in memory_tags table)
        std::vector<std::string> learning_tags = {"correction", "preference", "insight", "outcome"};
        for (const auto& tag : learning_tags) {
            std::string sql = "SELECT COUNT(DISTINCT m.id) FROM memory m "
                              "LEFT JOIN memory_tags t ON m.id = t.memory_id "
                              "WHERE t.tag = '" + tag + "' OR m.content LIKE '%[" + tag + "%'";
            auto result = mind_->store().raw_query(sql);
            if (result) {
                auto chunk = result->Fetch();
                if (chunk && chunk->size() > 0) {
                    tag_counts[tag] = chunk->GetValue(0, 0).GetValue<int64_t>();
                }
            }
        }

        // Get outcome success rate
        std::string outcome_sql = "SELECT "
            "SUM(CASE WHEN content LIKE '%[outcome:worked]%' THEN 1 ELSE 0 END) as worked, "
            "SUM(CASE WHEN content LIKE '%[outcome:failed]%' THEN 1 ELSE 0 END) as failed "
            "FROM memory WHERE content LIKE '%[outcome:%'";
        auto outcome_result = mind_->store().raw_query(outcome_sql);

        int worked = 0, failed = 0;
        if (outcome_result) {
            auto chunk = outcome_result->Fetch();
            if (chunk && chunk->size() > 0) {
                worked = chunk->GetValue(0, 0).GetValue<int64_t>();
                failed = chunk->GetValue(1, 0).GetValue<int64_t>();
            }
        }

        // Evaluate health
        std::vector<std::string> recommendations;
        float health_score = 0.5f;

        // Check if tracking outcomes
        if (tag_counts["outcome"] < 5) {
            recommendations.push_back("Track more suggestion outcomes to close feedback loops");
        } else {
            health_score += 0.1f;
        }

        // Check correction ratio
        if (tag_counts["correction"] > 20 && tag_counts["insight"] < 5) {
            recommendations.push_back("Many corrections but few insights - look for patterns in mistakes");
        }

        // Success rate
        int total = worked + failed;
        float success_rate = total > 0 ? (float)worked / total * 100.0f : 50.0f;
        if (success_rate > 70) health_score += 0.2f;
        else if (success_rate < 40) recommendations.push_back("Low success rate - review failed suggestions");

        // Check preferences captured
        if (tag_counts["preference"] >= 5) health_score += 0.1f;
        else recommendations.push_back("Capture more user preferences");

        std::ostringstream ss;
        ss << "Meta-cognition evaluation:\n\n"
           << "Learning metrics:\n"
           << "  Corrections: " << tag_counts["correction"] << "\n"
           << "  Preferences: " << tag_counts["preference"] << "\n"
           << "  Insights: " << tag_counts["insight"] << "\n"
           << "  Outcomes: " << tag_counts["outcome"] << " (success: " << std::fixed << std::setprecision(1) << success_rate << "%)\n\n"
           << "Health score: " << std::fixed << std::setprecision(2) << health_score << "/1.0\n";

        if (!recommendations.empty()) {
            ss << "\nRecommendations:\n";
            for (const auto& r : recommendations) {
                ss << "  - " << r << "\n";
            }
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"health_score", health_score},
            {"corrections", tag_counts["correction"]},
            {"preferences", tag_counts["preference"]},
            {"insights", tag_counts["insight"]},
            {"outcomes", tag_counts["outcome"]},
            {"success_rate", success_rate},
            {"recommendations", recommendations}
        });
    }

    // Curiosity (knowledge gaps)
    DuckDBToolResult tool_curiosity_note_gap(const json& params) {
        std::string gap = params.value("gap", "");
        if (gap.empty()) {
            return DuckDBToolResult::error("gap is required");
        }

        std::string context = params.value("context", "");
        std::string realm = params.value("realm", "brahman");

        // Store as a memory with type "gap"
        std::string content = "[gap] " + gap;
        if (!context.empty()) {
            content += "\nContext: " + context;
        }

        std::vector<float> embed;
        if (mind_->embedder_ready()) {
            embed = mind_->embedder().embed(content).data;
        }

        int64_t id = mind_->store().remember(
            content,
            "gap",  // NodeType::Gap
            embed,
            0.7f,   // moderate confidence
            0.02f,  // slow decay - gaps should persist
            realm,
            RealmVisibility::Private
        );

        // Tag it
        mind_->store().add_tag(id, "gap");
        mind_->store().add_tag(id, "unresolved");

        return DuckDBToolResult::ok(
            "Gap noted #" + std::to_string(id) + ": " + gap.substr(0, 60),
            {{"id", id}, {"gap", gap}}
        );
    }

    DuckDBToolResult tool_curiosity_gaps(const json& params) {
        size_t limit = params.value("limit", 10);
        std::string realm = params.value("realm", "");

        std::string sql = "SELECT m.id, m.content, m.created_at FROM memory m "
                          "JOIN memory_tags t ON m.id = t.memory_id "
                          "WHERE t.tag = 'unresolved' AND m.kind = 'gap' ";
        if (!realm.empty()) {
            sql += "AND m.realm = '" + realm + "' ";
        }
        sql += "ORDER BY m.created_at DESC LIMIT " + std::to_string(limit);

        auto result = mind_->store().raw_query(sql);
        if (!result) {
            return DuckDBToolResult::error("Failed to query gaps");
        }

        std::ostringstream ss;
        ss << "Knowledge gaps:\n";
        json gaps = json::array();

        auto chunk = result->Fetch();
        while (chunk && chunk->size() > 0) {
            for (size_t i = 0; i < chunk->size(); i++) {
                int64_t id = chunk->GetValue(0, i).GetValue<int64_t>();
                std::string content = chunk->GetValue(1, i).ToString();
                ss << "  #" << id << ": " << content.substr(0, 80) << "...\n";
                gaps.push_back({{"id", id}, {"content", content}});
            }
            chunk = result->Fetch();
        }

        if (gaps.empty()) {
            return DuckDBToolResult::ok("No unresolved knowledge gaps", {{"count", 0}});
        }

        return DuckDBToolResult::ok(ss.str(), {{"count", gaps.size()}, {"gaps", gaps}});
    }

    DuckDBToolResult tool_curiosity_resolve(const json& params) {
        int64_t id = params.value("id", 0);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        std::string learned = params.value("learned", "");

        // Remove unresolved tag, add resolved
        mind_->store().remove_tag(id, "unresolved");
        mind_->store().add_tag(id, "resolved");

        // If learned something, store it as an insight
        if (!learned.empty()) {
            std::vector<float> embed;
            if (mind_->embedder_ready()) {
                embed = mind_->embedder().embed(learned).data;
            }

            int64_t insight_id = mind_->store().remember(
                "[insight:exploration] " + learned,
                "wisdom",
                embed,
                0.8f,
                0.05f,
                "brahman",
                RealmVisibility::Global
            );

            // Link gap to insight
            mind_->store().connect(std::to_string(id), "led_to", std::to_string(insight_id));
        }

        return DuckDBToolResult::ok(
            "Gap #" + std::to_string(id) + " resolved",
            {{"id", id}, {"learned", learned}}
        );
    }

    // Transcript tools
    DuckDBToolResult tool_transcript_register(const json& params) {
        std::string session_id = params.value("session_id", "");
        std::string transcript_path = params.value("transcript_path", "");
        std::string realm = params.value("realm", "default");

        if (session_id.empty()) {
            return DuckDBToolResult::error("session_id is required");
        }
        if (transcript_path.empty()) {
            return DuckDBToolResult::error("transcript_path is required");
        }

        bool ok = mind_->store().register_transcript(session_id, transcript_path, realm);
        if (!ok) {
            return DuckDBToolResult::error("Failed to register transcript");
        }

        return DuckDBToolResult::ok("Registered transcript", {
            {"session_id", session_id},
            {"transcript_path", transcript_path},
            {"realm", realm}
        });
    }

    DuckDBToolResult tool_transcript_get(const json& params) {
        std::string session_id = params.value("session_id", "");

        if (session_id.empty()) {
            return DuckDBToolResult::error("session_id is required");
        }

        auto state = mind_->store().get_transcript(session_id);
        if (!state) {
            return DuckDBToolResult::ok("Transcript not found", {{"found", false}});
        }

        std::ostringstream ss;
        ss << "Transcript: " << state->session_id << "\n";
        ss << "  Path: " << state->transcript_path << "\n";
        ss << "  Realm: " << state->realm << "\n";
        ss << "  Last processed line: " << state->last_processed_line << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"found", true},
            {"session_id", state->session_id},
            {"transcript_path", state->transcript_path},
            {"realm", state->realm},
            {"last_processed_line", state->last_processed_line},
            {"last_distilled_at", state->last_distilled_at},
            {"created_at", state->created_at}
        });
    }

    DuckDBToolResult tool_transcript_list(const json& params) {
        auto transcripts = mind_->store().get_pending_transcripts();

        std::ostringstream ss;
        ss << "Registered transcripts: " << transcripts.size() << "\n\n";

        json list_json = json::array();
        for (const auto& t : transcripts) {
            ss << "  [" << t.session_id << "] " << t.realm << " - line " << t.last_processed_line << "\n";
            ss << "    " << t.transcript_path << "\n";

            list_json.push_back({
                {"session_id", t.session_id},
                {"transcript_path", t.transcript_path},
                {"realm", t.realm},
                {"last_processed_line", t.last_processed_line},
                {"last_distilled_at", t.last_distilled_at}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"transcripts", list_json},
            {"count", transcripts.size()}
        });
    }

    DuckDBToolResult tool_transcript_update(const json& params) {
        std::string session_id = params.value("session_id", "");
        int64_t last_line = params.value("last_line", 0LL);

        if (session_id.empty()) {
            return DuckDBToolResult::error("session_id is required");
        }

        bool ok = mind_->store().update_transcript_progress(session_id, last_line);
        if (!ok) {
            return DuckDBToolResult::error("Failed to update transcript progress");
        }

        return DuckDBToolResult::ok("Updated transcript progress", {
            {"session_id", session_id},
            {"last_line", last_line}
        });
    }

    DuckDBToolResult tool_transcript_remove(const json& params) {
        std::string session_id = params.value("session_id", "");

        if (session_id.empty()) {
            return DuckDBToolResult::error("session_id is required");
        }

        bool ok = mind_->store().remove_transcript(session_id);
        if (!ok) {
            return DuckDBToolResult::error("Failed to remove transcript");
        }

        return DuckDBToolResult::ok("Removed transcript", {
            {"session_id", session_id}
        });
    }

    DuckDBToolResult tool_transcript_parse(const json& params) {
        std::string session_id = params.value("session_id", "");
        size_t min_turns = params.value("min_turns", 4);

        if (session_id.empty()) {
            return DuckDBToolResult::error("session_id is required");
        }

        // Get transcript state
        auto state = mind_->store().get_transcript(session_id);
        if (!state) {
            return DuckDBToolResult::error("Transcript not found: " + session_id);
        }

        // Open and read the JSONL file
        std::ifstream file(state->transcript_path);
        if (!file) {
            return DuckDBToolResult::error("Cannot open transcript: " + state->transcript_path);
        }

        // Skip to last processed line
        std::string line;
        int64_t current_line = 0;
        while (current_line < state->last_processed_line && std::getline(file, line)) {
            current_line++;
        }

        // Parse new lines
        json turns_json = json::array();
        int64_t last_line = state->last_processed_line;

        while (std::getline(file, line)) {
            current_line++;
            if (line.empty()) continue;

            try {
                auto entry = json::parse(line);

                // Claude Code JSONL format: {"type": "user"|"assistant", "message": {...}}
                std::string type = entry.value("type", "");
                if (type != "user" && type != "assistant") continue;

                std::string content;
                if (entry.contains("message")) {
                    auto& msg = entry["message"];
                    // Extract text content from message
                    if (msg.contains("content")) {
                        auto& msg_content = msg["content"];
                        if (msg_content.is_string()) {
                            content = msg_content.get<std::string>();
                        } else if (msg_content.is_array()) {
                            // Array of content blocks
                            for (const auto& block : msg_content) {
                                if (block.contains("text")) {
                                    if (!content.empty()) content += "\n";
                                    content += block["text"].get<std::string>();
                                }
                            }
                        }
                    }
                }

                if (!content.empty()) {
                    turns_json.push_back({
                        {"role", type},
                        {"content", content},
                        {"line", current_line}
                    });
                    last_line = current_line;
                }
            } catch (...) {
                // Skip malformed lines
                continue;
            }
        }

        // Check if we have enough turns
        if (turns_json.size() < min_turns) {
            return DuckDBToolResult::ok("Not enough new turns", {
                {"session_id", session_id},
                {"turns_found", turns_json.size()},
                {"min_turns", min_turns},
                {"ready", false}
            });
        }

        std::ostringstream ss;
        ss << "Parsed " << turns_json.size() << " new turns from transcript\n";
        ss << "  Session: " << session_id << "\n";
        ss << "  Lines: " << state->last_processed_line << " -> " << last_line << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"session_id", session_id},
            {"realm", state->realm},
            {"turns", turns_json},
            {"turns_count", turns_json.size()},
            {"last_line", last_line},
            {"ready", true}
        });
    }

    DuckDBToolResult tool_transcript_search(const json& params) {
        std::string query = params.value("query", "");
        std::string session_id = params.value("session_id", "");
        size_t limit = params.value("limit", 10);
        float min_similarity = params.value("min_similarity", 0.3f);
        size_t max_candidates = params.value("max_candidates", 100);  // Pre-filter limit
        bool keyword_only = params.value("keyword_only", false);  // Skip embedding, keyword match only

        if (query.empty()) {
            return DuckDBToolResult::error("query is required");
        }

        // Check embedder is ready (unless keyword_only)
        if (!keyword_only && !mind_->embedder_ready()) {
            return DuckDBToolResult::error("Embedder not ready");
        }

        // Extract keywords from query (lowercase, split on spaces)
        std::vector<std::string> keywords;
        {
            std::string lower_query = query;
            std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);
            std::istringstream iss(lower_query);
            std::string word;
            while (iss >> word) {
                if (word.size() >= 3) {  // Skip very short words
                    keywords.push_back(word);
                }
            }
        }

        // Helper to check if content contains any keyword
        auto contains_keyword = [&keywords](const std::string& content) -> bool {
            if (keywords.empty()) return true;  // No keywords = match all
            std::string lower = content;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            for (const auto& kw : keywords) {
                if (lower.find(kw) != std::string::npos) return true;
            }
            return false;
        };

        // Helper to compute cosine similarity
        auto cosine_similarity = [](const std::vector<float>& a, const std::vector<float>& b) -> float {
            if (a.size() != b.size() || a.empty()) return 0.0f;
            float dot = 0, norm_a = 0, norm_b = 0;
            for (size_t i = 0; i < a.size(); ++i) {
                dot += a[i] * b[i];
                norm_a += a[i] * a[i];
                norm_b += b[i] * b[i];
            }
            float denom = std::sqrt(norm_a) * std::sqrt(norm_b);
            return denom > 0 ? dot / denom : 0.0f;
        };

        // Get transcripts to search
        std::vector<TranscriptState> transcripts;
        if (!session_id.empty()) {
            auto state = mind_->store().get_transcript(session_id);
            if (state) transcripts.push_back(*state);
        } else {
            transcripts = mind_->store().get_pending_transcripts();
        }

        if (transcripts.empty()) {
            return DuckDBToolResult::error("No transcripts found");
        }

        // Structure to hold candidates (pre-filtered by keyword)
        struct Candidate {
            std::string session_id;
            std::string realm;
            std::string role;
            std::string content;
            int64_t line;
        };
        std::vector<Candidate> candidates;

        // Phase 1: Keyword pre-filter (fast, no embeddings)
        for (const auto& state : transcripts) {
            std::ifstream file(state.transcript_path);
            if (!file) continue;

            std::string line;
            int64_t current_line = 0;

            while (std::getline(file, line) && candidates.size() < max_candidates * 2) {
                current_line++;
                if (line.empty()) continue;

                try {
                    auto entry = json::parse(line);
                    std::string type = entry.value("type", "");
                    if (type != "user" && type != "assistant") continue;

                    std::string content;
                    if (entry.contains("message")) {
                        auto& msg = entry["message"];
                        if (msg.contains("content")) {
                            auto& msg_content = msg["content"];
                            if (msg_content.is_string()) {
                                content = msg_content.get<std::string>();
                            } else if (msg_content.is_array()) {
                                for (const auto& block : msg_content) {
                                    if (block.contains("text")) {
                                        if (!content.empty()) content += "\n";
                                        content += block["text"].get<std::string>();
                                    }
                                }
                            }
                        }
                    }

                    if (content.empty() || content.size() < 20) continue;

                    // Keyword pre-filter
                    if (!contains_keyword(content)) continue;

                    candidates.push_back({
                        state.session_id,
                        state.realm,
                        type,
                        content,
                        current_line
                    });
                } catch (...) {
                    continue;
                }
            }
        }

        // Limit candidates before embedding
        if (candidates.size() > max_candidates) {
            candidates.resize(max_candidates);
        }

        // Phase 2: Semantic ranking (only on filtered candidates)
        struct SearchResult {
            std::string session_id;
            std::string realm;
            std::string role;
            std::string content;
            int64_t line;
            float similarity;
        };
        std::vector<SearchResult> results;

        if (!candidates.empty()) {
            if (keyword_only) {
                // Keyword-only mode: rank by keyword density (fast, no embedding)
                auto count_keywords = [&keywords](const std::string& content) -> float {
                    if (keywords.empty()) return 1.0f;
                    std::string lower = content;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                    int count = 0;
                    for (const auto& kw : keywords) {
                        size_t pos = 0;
                        while ((pos = lower.find(kw, pos)) != std::string::npos) {
                            count++;
                            pos += kw.size();
                        }
                    }
                    return static_cast<float>(count) / keywords.size();
                };

                for (const auto& c : candidates) {
                    float density = count_keywords(c.content);
                    results.push_back({
                        c.session_id,
                        c.realm,
                        c.role,
                        c.content.size() > 500 ? c.content.substr(0, 500) + "..." : c.content,
                        c.line,
                        density
                    });
                }
            } else {
                // Full semantic search with embeddings
                // Generate query embedding once (query mode for BGE)
                Artha query_artha = mind_->embedder().transform_query(query);
                const std::vector<float>& query_embedding = query_artha.nu.data;

                for (const auto& c : candidates) {
                    std::string embed_content = c.content;
                    if (embed_content.size() > 2000) {
                        embed_content = embed_content.substr(0, 2000);
                    }

                    Artha content_artha = mind_->embedder().transform(embed_content);
                    const std::vector<float>& content_embedding = content_artha.nu.data;

                    float sim = cosine_similarity(query_embedding, content_embedding);
                    if (sim >= min_similarity) {
                        results.push_back({
                            c.session_id,
                            c.realm,
                            c.role,
                            c.content.size() > 500 ? c.content.substr(0, 500) + "..." : c.content,
                            c.line,
                            sim
                        });
                    }
                }
            }
        }

        // Sort by similarity (descending)
        std::sort(results.begin(), results.end(),
            [](const SearchResult& a, const SearchResult& b) { return a.similarity > b.similarity; });

        // Limit results
        if (results.size() > limit) {
            results.resize(limit);
        }

        // Build response
        json results_json = json::array();
        for (const auto& r : results) {
            results_json.push_back({
                {"session_id", r.session_id},
                {"realm", r.realm},
                {"role", r.role},
                {"content", r.content},
                {"line", r.line},
                {"similarity", r.similarity}
            });
        }

        std::ostringstream ss;
        ss << "Found " << results.size() << " matching passages\n";
        if (!results.empty()) {
            ss << "Top match: " << results[0].content.substr(0, 100) << "...\n";
            ss << "  Similarity: " << std::fixed << std::setprecision(2) << results[0].similarity;
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"query", query},
            {"results", results_json},
            {"count", results.size()},
            {"transcripts_searched", transcripts.size()}
        });
    }

    DuckDBToolResult tool_distill_status(const json& params) {
        auto transcripts = mind_->store().get_pending_transcripts();

        // Group by realm
        std::map<std::string, std::vector<const TranscriptState*>> by_realm;
        for (const auto& t : transcripts) {
            by_realm[t.realm].push_back(&t);
        }

        // Count pending work per transcript
        size_t total_pending = 0;
        json transcripts_json = json::array();

        for (const auto& t : transcripts) {
            // Check file for new lines
            size_t file_lines = 0;
            size_t pending_lines = 0;

            std::ifstream file(t.transcript_path);
            if (file) {
                std::string line;
                while (std::getline(file, line)) {
                    file_lines++;
                }
                pending_lines = (file_lines > static_cast<size_t>(t.last_processed_line))
                    ? file_lines - t.last_processed_line : 0;
                total_pending += pending_lines;
            }

            transcripts_json.push_back({
                {"session_id", t.session_id},
                {"realm", t.realm},
                {"last_processed_line", t.last_processed_line},
                {"file_lines", file_lines},
                {"pending_lines", pending_lines},
                {"last_distilled_at", t.last_distilled_at}
            });
        }

        // Build realm summary
        json realms_json = json::object();
        for (const auto& [realm, ts] : by_realm) {
            realms_json[realm] = ts.size();
        }

        std::ostringstream ss;
        ss << "Distillation Status\n";
        ss << "═══════════════════════════════\n\n";
        ss << "Registered transcripts: " << transcripts.size() << "\n";
        ss << "Total pending lines: " << total_pending << "\n\n";

        ss << "By realm:\n";
        for (const auto& [realm, ts] : by_realm) {
            ss << "  " << realm << ": " << ts.size() << " transcript(s)\n";
        }

        ss << "\nTranscripts:\n";
        for (const auto& t : transcripts) {
            ss << "  [" << t.session_id.substr(0, 8) << "...] "
               << t.realm << " - line " << t.last_processed_line << "\n";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"transcript_count", transcripts.size()},
            {"total_pending_lines", total_pending},
            {"realms", realms_json},
            {"transcripts", transcripts_json}
        });
    }

    DuckDBToolResult tool_epiplexity_check(const json& params) {
        std::string original = params.value("original", "");
        std::string seed = params.value("seed", "");
        std::string reconstructed = params.value("reconstructed", "");

        if (original.empty() || seed.empty() || reconstructed.empty()) {
            return DuckDBToolResult::error("original, seed, and reconstructed are all required");
        }

        Epiplexity e = mind_->compute_epiplexity(original, seed, reconstructed);

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "Epiplexity Analysis\n";
        ss << "═══════════════════════════════\n\n";
        ss << "ε = " << e.score << " (combined score)\n\n";
        ss << "Components:\n";
        ss << "  S (semantic fidelity):    " << e.semantic_fidelity << "\n";
        ss << "  K (entity preservation):  " << e.entity_preservation << "\n";
        ss << "  D (information density):  " << e.information_density << "\n";
        ss << "  C (compression utility):  " << e.compression_utility << "\n\n";

        // Quality assessment
        std::string quality;
        if (e.score >= 0.8f) quality = "Excellent - seed is highly reconstructable";
        else if (e.score >= 0.6f) quality = "Good - seed preserves key meaning";
        else if (e.score >= 0.4f) quality = "Fair - some information loss";
        else quality = "Poor - consider expanding seed or using full content";

        ss << "Quality: " << quality << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"score", e.score},
            {"semantic_fidelity", e.semantic_fidelity},
            {"entity_preservation", e.entity_preservation},
            {"information_density", e.information_density},
            {"compression_utility", e.compression_utility}
        });
    }

    // ========================================================================
    // Anticipation: context→action pattern learning
    // ========================================================================

    DuckDBToolResult tool_anticipation_observe(const json& params) {
        std::string context = params.value("context", "");
        std::string action = params.value("action", "");
        std::string realm = params.value("realm", "brahman");

        if (context.empty() || action.empty()) {
            return DuckDBToolResult::error("context and action are required");
        }

        int64_t id = mind_->store().anticipation_observe(context, action, realm);
        if (id <= 0) {
            return DuckDBToolResult::error("Failed to record pattern: " + mind_->store().last_error());
        }

        return DuckDBToolResult::ok("Pattern recorded (id: " + std::to_string(id) + ")", {{"id", id}});
    }

    DuckDBToolResult tool_anticipation_predict(const json& params) {
        std::string context = params.value("context", "");
        size_t limit = params.value("limit", 5);
        float min_confidence = params.value("min_confidence", 0.3f);
        std::string realm = params.value("realm", "");

        if (context.empty()) {
            return DuckDBToolResult::error("context is required");
        }

        auto patterns = mind_->store().anticipation_predict(context, limit * 2, realm);  // Fetch more for filtering

        std::ostringstream ss;
        ss << "Predicted Actions for Context\n";
        ss << "══════════════════════════════\n\n";

        json patterns_json = json::array();
        size_t count = 0;
        for (const auto& p : patterns) {
            float success_rate = p.frequency > 0 ? (float)p.success_count / p.frequency : 0;
            // Filter by min_confidence (success rate)
            if (success_rate < min_confidence) continue;

            ss << "• " << p.action << "\n";
            ss << "  Context: " << p.context.substr(0, 80) << (p.context.length() > 80 ? "..." : "") << "\n";
            ss << "  Frequency: " << p.frequency << " | Success: " << std::fixed << std::setprecision(0) << (success_rate * 100) << "%\n\n";

            patterns_json.push_back({
                {"id", p.id},
                {"context", p.context},
                {"action", p.action},
                {"frequency", p.frequency},
                {"success_count", p.success_count},
                {"confidence", success_rate},
                {"realm", p.realm}
            });

            if (++count >= limit) break;
        }

        if (patterns_json.empty()) {
            ss << "No matching patterns found.\n";
        }

        return DuckDBToolResult::ok(ss.str(), {{"patterns", patterns_json}});
    }

    DuckDBToolResult tool_anticipation_success(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        if (!mind_->store().anticipation_success(id)) {
            return DuckDBToolResult::error("Failed to mark success");
        }

        return DuckDBToolResult::ok("Pattern #" + std::to_string(id) + " marked successful");
    }

    DuckDBToolResult tool_anticipation_list(const json& params) {
        std::string realm = params.value("realm", "");
        size_t limit = params.value("limit", 20);
        std::string sort_by = params.value("sort_by", "frequency");

        auto patterns = mind_->store().anticipation_list(realm, limit);

        // Apply sorting
        if (sort_by == "confidence") {
            std::sort(patterns.begin(), patterns.end(), [](const auto& a, const auto& b) {
                float rate_a = a.frequency > 0 ? (float)a.success_count / a.frequency : 0;
                float rate_b = b.frequency > 0 ? (float)b.success_count / b.frequency : 0;
                return rate_a > rate_b;
            });
        } else if (sort_by == "created_at") {
            std::sort(patterns.begin(), patterns.end(), [](const auto& a, const auto& b) {
                return a.created_at > b.created_at;
            });
        }
        // Default: frequency (already sorted by store)

        std::ostringstream ss;
        ss << "Learned Anticipation Patterns\n";
        ss << "══════════════════════════════\n\n";

        if (patterns.empty()) {
            ss << "No patterns learned yet.\n";
        } else {
            for (const auto& p : patterns) {
                float success_rate = p.frequency > 0 ? (float)p.success_count / p.frequency * 100 : 0;
                ss << "#" << p.id << " [" << p.realm << "]\n";
                ss << "  Context: " << p.context.substr(0, 60) << (p.context.length() > 60 ? "..." : "") << "\n";
                ss << "  Action: " << p.action.substr(0, 60) << (p.action.length() > 60 ? "..." : "") << "\n";
                ss << "  Freq: " << p.frequency << " | Success: " << std::fixed << std::setprecision(0) << success_rate << "%\n\n";
            }
        }

        return DuckDBToolResult::ok(ss.str(), {{"count", patterns.size()}});
    }

    // ========================================================================
    // Habit Formation: repeated patterns that strengthen
    // ========================================================================

    DuckDBToolResult tool_habit_observe(const json& params) {
        std::string trigger = params.value("trigger", "");
        std::string response = params.value("response", "");
        std::string realm = params.value("realm", "brahman");

        if (trigger.empty() || response.empty()) {
            return DuckDBToolResult::error("trigger and response are required");
        }

        int64_t id = mind_->store().habit_observe(trigger, response, realm);
        if (id <= 0) {
            return DuckDBToolResult::error("Failed to record habit: " + mind_->store().last_error());
        }

        return DuckDBToolResult::ok("Habit recorded/strengthened (id: " + std::to_string(id) + ")", {{"id", id}});
    }

    DuckDBToolResult tool_habit_match(const json& params) {
        std::string context = params.value("context", "");
        float min_strength = params.value("min_strength", 0.3f);
        std::string realm = params.value("realm", "");

        if (context.empty()) {
            return DuckDBToolResult::error("context is required");
        }

        auto habits = mind_->store().habit_match(context, min_strength, realm);

        std::ostringstream ss;
        ss << "Matching Habits\n";
        ss << "═══════════════\n\n";

        if (habits.empty()) {
            ss << "No matching habits found.\n";
        } else {
            for (const auto& h : habits) {
                ss << "• " << h.trigger_pattern << " → " << h.response << "\n";
                ss << "  Strength: " << std::fixed << std::setprecision(2) << h.strength;
                ss << " | Frequency: " << h.frequency << "\n\n";
            }
        }

        json habits_json = json::array();
        for (const auto& h : habits) {
            habits_json.push_back({
                {"id", h.id},
                {"trigger", h.trigger_pattern},
                {"response", h.response},
                {"strength", h.strength},
                {"frequency", h.frequency},
                {"realm", h.realm}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"habits", habits_json}});
    }

    DuckDBToolResult tool_habit_strengthen(const json& params) {
        auto [id, id_str] = parse_id(params);
        float amount = params.value("amount", 0.1f);

        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        if (!mind_->store().habit_strengthen(id, amount)) {
            return DuckDBToolResult::error("Failed to strengthen habit");
        }

        return DuckDBToolResult::ok("Habit #" + std::to_string(id) + " strengthened by " + std::to_string(amount));
    }

    DuckDBToolResult tool_habit_weaken(const json& params) {
        auto [id, id_str] = parse_id(params);
        float amount = params.value("amount", 0.05f);

        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        if (!mind_->store().habit_weaken(id, amount)) {
            return DuckDBToolResult::error("Failed to weaken habit");
        }

        return DuckDBToolResult::ok("Habit #" + std::to_string(id) + " weakened by " + std::to_string(amount));
    }

    DuckDBToolResult tool_habit_list(const json& params) {
        std::string realm = params.value("realm", "");
        float min_strength = params.value("min_strength", 0.0f);
        size_t limit = params.value("limit", 20);
        std::string sort_by = params.value("sort_by", "strength");

        auto habits = mind_->store().habit_list(realm, min_strength, limit);

        // Apply sorting
        if (sort_by == "frequency") {
            std::sort(habits.begin(), habits.end(), [](const auto& a, const auto& b) {
                return a.frequency > b.frequency;
            });
        } else if (sort_by == "created_at") {
            std::sort(habits.begin(), habits.end(), [](const auto& a, const auto& b) {
                return a.created_at > b.created_at;
            });
        }
        // Default: strength (already sorted by store)

        std::ostringstream ss;
        ss << "Formed Habits\n";
        ss << "══════════════\n\n";

        if (habits.empty()) {
            ss << "No habits formed yet.\n";
        } else {
            for (const auto& h : habits) {
                int strength_bars = static_cast<int>(h.strength * 10);
                std::string bar(strength_bars, '#');
                bar += std::string(10 - strength_bars, '-');

                ss << "#" << h.id << " [" << bar << "] " << std::fixed << std::setprecision(2) << h.strength << "\n";
                ss << "  " << h.trigger_pattern << " → " << h.response << "\n";
                ss << "  Realm: " << h.realm << " | Freq: " << h.frequency << "\n\n";
            }
        }

        return DuckDBToolResult::ok(ss.str(), {{"count", habits.size()}});
    }

    // ========================================================================
    // Background Processing: daemon-level tasks
    // ========================================================================

    DuckDBToolResult tool_background_schedule(const json& params) {
        std::string task_type = params.value("task_type", "");
        std::string realm = params.value("realm", "brahman");

        if (task_type.empty()) {
            return DuckDBToolResult::error("task_type is required");
        }

        // Validate task type
        std::vector<std::string> valid_types = {"consolidation", "decay", "pruning", "pattern_extraction"};
        bool valid = std::find(valid_types.begin(), valid_types.end(), task_type) != valid_types.end();
        if (!valid) {
            return DuckDBToolResult::error("Invalid task_type. Valid: consolidation, decay, pruning, pattern_extraction");
        }

        int64_t id = mind_->store().background_schedule(task_type, realm);
        if (id <= 0) {
            return DuckDBToolResult::error("Failed to schedule task: " + mind_->store().last_error());
        }

        return DuckDBToolResult::ok("Background task scheduled (id: " + std::to_string(id) + ")", {
            {"id", id},
            {"task_type", task_type}
        });
    }

    DuckDBToolResult tool_background_status(const json& params) {
        auto status = mind_->store().background_status();

        std::ostringstream ss;
        ss << "Background Processing Status\n";
        ss << "════════════════════════════\n\n";
        ss << "Task Queue:\n";
        ss << "  Pending:         " << status.pending << "\n";
        ss << "  Running:         " << status.running << "\n";
        ss << "  Completed today: " << status.completed_today << "\n";
        ss << "  Failed today:    " << status.failed_today << "\n";

        json result = {
            {"pending", status.pending},
            {"running", status.running},
            {"completed_today", status.completed_today},
            {"failed_today", status.failed_today}
        };

        // Add subconscious stats if available
        if (subconscious_) {
            const auto& stats = subconscious_->stats();
            const auto& config = subconscious_->config();
            bool idle = subconscious_->is_idle();
            int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            int64_t idle_for = stats.last_query_at > 0 ? (now - stats.last_query_at) / 1000 : -1;

            ss << "\nEmbedding Scheduler:\n";
            ss << "  Enabled:          " << (config.enable_background_embedding ? "yes" : "no") << "\n";
            ss << "  Status:           " << (idle ? "IDLE (will embed)" : "BUSY (queries active)") << "\n";
            ss << "  Idle threshold:   " << config.idle_threshold.count() << "s\n";
            if (idle_for >= 0) {
                ss << "  Idle for:         " << idle_for << "s\n";
            }
            ss << "  Queue size:       " << subconscious_->embedding_queue_size() << "\n";
            ss << "  Embedded total:   " << stats.symbols_embedded.load() << "\n";
            ss << "  Skipped (busy):   " << stats.embedding_skips.load() << "\n";

            result["embedding"] = {
                {"enabled", config.enable_background_embedding},
                {"is_idle", idle},
                {"idle_threshold_s", config.idle_threshold.count()},
                {"idle_for_s", idle_for},
                {"queue_size", subconscious_->embedding_queue_size()},
                {"embedded_total", stats.symbols_embedded.load()},
                {"skipped_busy", stats.embedding_skips.load()}
            };
        }

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_background_run_cycle(const json& params) {
        size_t processed = mind_->store().background_run_cycle();

        std::ostringstream ss;
        if (processed == 0) {
            ss << "No pending background tasks to process.";
        } else {
            ss << "Processed " << processed << " background task(s).";
        }

        return DuckDBToolResult::ok(ss.str(), {{"processed", processed}});
    }

    // Helpers
    json tool_list() {
        json filtered = json::array();
        for (const auto& tool : tools_) {
            auto name = tool["name"].get<std::string>();
            auto it = tool_visibility_.find(name);
            std::string vis = (it != tool_visibility_.end()) ? it->second : "default";
            if (vis != "internal") {
                filtered.push_back(tool);
            }
        }
        return {{"tools", filtered}};
    }

    json make_response(const json& id, const json& result) {
        return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    }

    json make_error(const json& id, int code, const std::string& msg) {
        return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", msg}}}};
    }

    json make_tool_response(const json& id, const DuckDBToolResult& result) {
        json content = json::array();
        content.push_back({{"type", "text"}, {"text", result.text}});

        return {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", {
                {"content", content},
                {"isError", result.is_error},
                {"structured", result.structured}
            }}
        };
    }

    static std::string node_type_name(NodeType t) {
        switch (t) {
            case NodeType::Wisdom: return "wisdom";
            case NodeType::Belief: return "belief";
            case NodeType::Intention: return "intention";
            case NodeType::Aspiration: return "aspiration";
            case NodeType::Episode: return "episode";
            case NodeType::Symbol: return "symbol";
            case NodeType::Dream: return "dream";
            case NodeType::Gap: return "gap";
            case NodeType::Question: return "question";
            default: return "unknown";
        }
    }

    // ========================================================================
    // Profile Tool Implementations
    // ========================================================================

    DuckDBToolResult tool_profile_get(const json& params) {
        std::string user_id = params.value("user_id", "default");

        auto profile = mind_->store().profile_get(user_id);
        if (!profile) {
            return DuckDBToolResult::ok("No profile found for user: " + user_id, {{"found", false}});
        }

        std::ostringstream ss;
        ss << "User Profile: " << profile->user_id << "\n";
        ss << "  Expertise: " << profile->expertise_json << "\n";
        ss << "  Style: " << profile->style_json << "\n";
        ss << "  Patterns: " << profile->patterns_json << "\n";
        ss << "  Preferences: " << profile->preferences_json << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"found", true},
            {"user_id", profile->user_id},
            {"expertise", profile->expertise_json},
            {"style", profile->style_json},
            {"patterns", profile->patterns_json},
            {"preferences", profile->preferences_json},
            {"updated_at", profile->updated_at}
        });
    }

    DuckDBToolResult tool_profile_update(const json& params) {
        std::string user_id = params.value("user_id", "default");
        std::string field = params.value("field", "");
        std::string value = params.value("value", "");

        if (field.empty()) {
            return DuckDBToolResult::error("field is required");
        }
        if (value.empty()) {
            return DuckDBToolResult::error("value is required");
        }

        bool success = mind_->store().profile_update(user_id, field, value);
        if (!success) {
            return DuckDBToolResult::error("Failed to update profile: " + mind_->store().last_error());
        }

        return DuckDBToolResult::ok("Updated " + field + " for user " + user_id, {
            {"user_id", user_id},
            {"field", field},
            {"success", true}
        });
    }

    DuckDBToolResult tool_profile_observe(const json& params) {
        std::string observation_type = params.value("observation_type", "");
        std::string value = params.value("value", "");
        std::string user_id = params.value("user_id", "default");

        if (observation_type.empty()) {
            return DuckDBToolResult::error("observation_type is required");
        }
        if (value.empty()) {
            return DuckDBToolResult::error("value is required");
        }

        bool success = mind_->store().profile_observe(observation_type, value, user_id);
        if (!success) {
            return DuckDBToolResult::error("Failed to observe: " + mind_->store().last_error());
        }

        return DuckDBToolResult::ok("Observed " + observation_type + ": " + value.substr(0, 50), {
            {"user_id", user_id},
            {"observation_type", observation_type},
            {"success", true}
        });
    }

    // ========================================================================
    // Goal Tool Implementations
    // ========================================================================

    DuckDBToolResult tool_goal_set(const json& params) {
        std::string title = params.value("title", "");
        if (title.empty()) {
            return DuckDBToolResult::error("title is required");
        }

        std::string description = params.value("description", "");
        std::string milestones = params.value("milestones", "[]");
        int64_t deadline = params.value("deadline", 0);
        std::string realm = params.value("realm", "brahman");

        int64_t id = mind_->store().goal_set(title, description, milestones, deadline, realm);
        if (id < 0) {
            return DuckDBToolResult::error("Failed to create goal");
        }

        return DuckDBToolResult::ok("Goal created: #" + std::to_string(id) + " " + title, {
            {"id", id},
            {"title", title}
        });
    }

    DuckDBToolResult tool_goal_get(const json& params) {
        int64_t id = params.value("id", 0);
        if (id == 0) {
            return DuckDBToolResult::error("id is required");
        }

        auto goal = mind_->store().goal_get(id);
        if (!goal) {
            return DuckDBToolResult::ok("Goal not found: #" + std::to_string(id), {{"found", false}});
        }

        std::ostringstream ss;
        ss << "Goal #" << goal->id << ": " << goal->title << "\n";
        ss << "  Status: " << goal->status << " (" << (int)(goal->progress * 100) << "%)\n";
        if (!goal->description.empty()) {
            ss << "  Description: " << goal->description.substr(0, 100) << "\n";
        }
        ss << "  Milestones: " << goal->milestones_json << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"found", true},
            {"id", goal->id},
            {"title", goal->title},
            {"description", goal->description},
            {"status", goal->status},
            {"progress", goal->progress},
            {"milestones", goal->milestones_json},
            {"deadline", goal->deadline},
            {"realm", goal->realm}
        });
    }

    DuckDBToolResult tool_goal_list(const json& params) {
        std::string status = params.value("status", "active");
        std::string realm = params.value("realm", "");
        size_t limit = params.value("limit", 20);
        std::string sort_by = params.value("sort_by", "updated_at");

        auto goals = mind_->store().goal_list(status, realm, limit);

        // Apply sorting
        if (sort_by == "progress") {
            std::sort(goals.begin(), goals.end(), [](const auto& a, const auto& b) {
                return a.progress > b.progress;
            });
        } else if (sort_by == "created_at") {
            std::sort(goals.begin(), goals.end(), [](const auto& a, const auto& b) {
                return a.created_at > b.created_at;
            });
        }
        // Default: updated_at (already sorted by store)

        if (goals.empty()) {
            return DuckDBToolResult::ok("No " + status + " goals", {{"count", 0}, {"goals", json::array()}});
        }

        std::ostringstream ss;
        ss << "Goals (" << status << "):\n";
        json goals_json = json::array();

        for (const auto& g : goals) {
            ss << "  #" << g.id << " [" << (int)(g.progress * 100) << "%] " << g.title << "\n";
            goals_json.push_back({
                {"id", g.id},
                {"title", g.title},
                {"progress", g.progress},
                {"status", g.status}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"count", goals.size()}, {"goals", goals_json}});
    }

    DuckDBToolResult tool_goal_progress(const json& params) {
        int64_t id = params.value("id", 0);
        if (id == 0) {
            return DuckDBToolResult::error("id is required");
        }

        float progress = params.value("progress", 0.0f);
        std::string milestone = params.value("milestone", "");

        bool success = mind_->store().goal_progress(id, progress, milestone);
        if (!success) {
            return DuckDBToolResult::error("Failed to update progress");
        }

        std::string msg = "Goal #" + std::to_string(id) + " progress: " + std::to_string((int)(progress * 100)) + "%";
        if (!milestone.empty()) {
            msg += " (completed: " + milestone + ")";
        }

        return DuckDBToolResult::ok(msg, {
            {"id", id},
            {"progress", progress},
            {"milestone_completed", milestone}
        });
    }

    DuckDBToolResult tool_goal_complete(const json& params) {
        int64_t id = params.value("id", 0);
        if (id == 0) {
            return DuckDBToolResult::error("id is required");
        }

        std::string outcome = params.value("outcome", "");
        if (outcome.empty()) {
            return DuckDBToolResult::error("outcome is required");
        }

        bool success = mind_->store().goal_complete(id, outcome);
        if (!success) {
            return DuckDBToolResult::error("Failed to complete goal");
        }

        return DuckDBToolResult::ok("Goal #" + std::to_string(id) + " completed: " + outcome.substr(0, 50), {
            {"id", id},
            {"outcome", outcome},
            {"status", "completed"}
        });
    }

    // ========================================================================
    // Calibration Tool Implementations
    // ========================================================================

    DuckDBToolResult tool_calibration_record(const json& params) {
        std::string domain = params.value("domain", "");
        if (domain.empty()) {
            return DuckDBToolResult::error("domain is required");
        }

        bool success = params.value("success", false);

        bool recorded = mind_->store().calibration_record(domain, success);
        if (!recorded) {
            return DuckDBToolResult::error("Failed to record calibration");
        }

        std::string msg = "Recorded " + std::string(success ? "success" : "failure") + " for domain: " + domain;

        // Get updated score
        auto score = mind_->store().calibration_get(domain);
        if (score) {
            msg += " (accuracy: " + std::to_string((int)(score->accuracy * 100)) + "%)";
        }

        return DuckDBToolResult::ok(msg, {
            {"domain", domain},
            {"success", success},
            {"recorded", true}
        });
    }

    DuckDBToolResult tool_calibration_score(const json& params) {
        std::string domain = params.value("domain", "");

        if (!domain.empty()) {
            // Get specific domain
            auto score = mind_->store().calibration_get(domain);
            if (!score) {
                return DuckDBToolResult::ok("No calibration data for domain: " + domain, {
                    {"found", false},
                    {"domain", domain}
                });
            }

            std::ostringstream ss;
            ss << "Calibration for " << domain << ":\n"
               << "  Predictions: " << score->predictions << "\n"
               << "  Successes: " << score->successes << " (" << (int)(score->accuracy * 100) << "%)\n"
               << "  Failures: " << score->failures << "\n"
               << "  Confidence adjustment: " << std::showpos << std::fixed << std::setprecision(2)
               << score->confidence_adjustment;

            return DuckDBToolResult::ok(ss.str(), {
                {"found", true},
                {"domain", score->domain},
                {"predictions", score->predictions},
                {"successes", score->successes},
                {"failures", score->failures},
                {"accuracy", score->accuracy},
                {"confidence_adjustment", score->confidence_adjustment}
            });
        }

        // Get all domains
        auto scores = mind_->store().calibration_all();
        if (scores.empty()) {
            return DuckDBToolResult::ok("No calibration data yet", {{"count", 0}});
        }

        std::ostringstream ss;
        ss << "Calibration scores:\n";
        json scores_json = json::array();

        for (const auto& s : scores) {
            ss << "  " << s.domain << ": " << (int)(s.accuracy * 100) << "% "
               << "(" << s.successes << "/" << s.predictions << ")";
            if (s.confidence_adjustment != 0.0f) {
                ss << " [adj: " << std::showpos << std::fixed << std::setprecision(2)
                   << s.confidence_adjustment << "]";
            }
            ss << "\n";

            scores_json.push_back({
                {"domain", s.domain},
                {"accuracy", s.accuracy},
                {"predictions", s.predictions},
                {"adjustment", s.confidence_adjustment}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"count", scores.size()}, {"scores", scores_json}});
    }

    // ========================================================================
    // Hygiene Tool Implementations
    // ========================================================================

    DuckDBToolResult tool_hygiene_stats(const json&) {
        auto stats = mind_->store().hygiene_stats();

        std::ostringstream ss;
        ss << "Memory Hygiene Stats:\n"
           << "  Total: " << stats.total_memories << " memories\n"
           << "  Confidence: " << stats.high_confidence << " high, "
           << stats.medium_confidence << " medium, "
           << stats.low_confidence << " low\n"
           << "  Avg confidence: " << std::fixed << std::setprecision(2) << stats.avg_confidence << "\n"
           << "  Stale (30+ days): " << stats.old_unaccessed << "\n"
           << "  Growth rate: " << std::setprecision(1) << stats.growth_rate_per_day << "/day\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"total", stats.total_memories},
            {"high_confidence", stats.high_confidence},
            {"medium_confidence", stats.medium_confidence},
            {"low_confidence", stats.low_confidence},
            {"avg_confidence", stats.avg_confidence},
            {"stale", stats.old_unaccessed},
            {"growth_rate", stats.growth_rate_per_day}
        });
    }

    DuckDBToolResult tool_hygiene_run(const json& params) {
        float prune_threshold = params.value("prune_threshold", 0.1f);
        float min_age_days = params.value("min_age_days", 7.0f);
        float consolidation_threshold = params.value("consolidation_threshold", 0.85f);
        size_t max_consolidations = params.value("max_consolidations", 10);

        auto result = mind_->store().hygiene_run(prune_threshold, min_age_days,
                                                  consolidation_threshold, max_consolidations);

        std::ostringstream ss;
        ss << "Hygiene run complete:\n"
           << "  Decayed: " << result.decayed << " memories\n"
           << "  Pruned: " << result.pruned << " memories\n"
           << "  Consolidated: " << result.consolidated << " pairs\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"decayed", result.decayed},
            {"pruned", result.pruned},
            {"consolidated", result.consolidated}
        });
    }

    DuckDBToolResult tool_rebuild_fts_index(const json& params) {
        bool success = mind_->store().rebuild_fts_index();

        if (success) {
            return DuckDBToolResult::ok("FTS index rebuilt successfully. Keyword search should now work.", {
                {"success", true}
            });
        } else {
            return DuckDBToolResult::error("Failed to rebuild FTS index. FTS extension may not be available.");
        }
    }

    // ========================================================================
    // xMemory Theme System Tool Implementations
    // ========================================================================

    DuckDBToolResult tool_theme_list(const json& params) {
        std::string realm = params.value("realm", "");
        size_t limit = params.value("limit", 20);

        auto themes = mind_->store().theme_list(realm, limit);

        std::ostringstream ss;
        ss << "Themes (" << themes.size() << "):\n";

        json theme_array = json::array();
        for (const auto& t : themes) {
            ss << "  [" << t.id << "] " << t.name
               << " (" << t.memory_count << " memories, coherence="
               << std::fixed << std::setprecision(2) << t.coherence << ")\n";

            theme_array.push_back({
                {"id", t.id},
                {"name", t.name},
                {"memory_count", t.memory_count},
                {"coherence", t.coherence},
                {"sparsity", t.sparsity},
                {"realm", t.realm}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"themes", theme_array}});
    }

    DuckDBToolResult tool_theme_get(const json& params) {
        int64_t theme_id = params.value("id", 0);
        if (theme_id == 0) {
            return DuckDBToolResult::error("theme id required");
        }

        auto theme = mind_->store().theme_get(theme_id);
        if (!theme) {
            return DuckDBToolResult::error("Theme not found");
        }

        // Get representatives
        auto reps = mind_->store().theme_representatives(theme_id, 5);

        std::ostringstream ss;
        ss << "Theme: " << theme->name << " (id=" << theme->id << ")\n"
           << "  Memories: " << theme->memory_count << "\n"
           << "  Coherence: " << std::fixed << std::setprecision(2) << theme->coherence << "\n"
           << "  Realm: " << theme->realm << "\n"
           << "  Representatives (" << reps.size() << "):\n";

        json rep_array = json::array();
        for (const auto& r : reps) {
            std::string preview = r.content.substr(0, 100);
            if (r.content.size() > 100) preview += "...";
            ss << "    [" << r.id << "] " << preview << "\n";
            rep_array.push_back({
                {"id", r.id},
                {"content", r.content},
                {"kind", r.kind},
                {"confidence", r.confidence}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"id", theme->id},
            {"name", theme->name},
            {"memory_count", theme->memory_count},
            {"coherence", theme->coherence},
            {"sparsity", theme->sparsity},
            {"realm", theme->realm},
            {"representatives", rep_array}
        });
    }

    DuckDBToolResult tool_theme_recall(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) {
            return DuckDBToolResult::error("query required");
        }

        size_t limit = params.value("limit", 10);
        std::string realm = params.value("realm", "");

        auto recalls = mind_->theme_recall(query, limit, realm);

        std::ostringstream ss;
        ss << "Theme Recall (" << recalls.size() << " results):\n\n";

        json result_array = json::array();
        for (const auto& r : recalls) {
            std::string preview = r.text.substr(0, 150);
            if (r.text.size() > 150) preview += "...";

            ss << "[" << std::fixed << std::setprecision(2) << r.relevance << "] "
               << preview << "\n\n";

            json result_obj;
            result_obj["id"] = r.id.low;
            result_obj["content"] = r.text;
            result_obj["relevance"] = r.relevance;
            result_obj["similarity"] = r.similarity;
            result_obj["confidence"] = r.confidence.mu;
            result_array.push_back(result_obj);
        }

        return DuckDBToolResult::ok(ss.str(), {{"results", result_array}});
    }

    DuckDBToolResult tool_theme_stats(const json& params) {
        std::string realm = params.value("realm", "");

        auto stats = mind_->store().theme_stats(realm);

        std::ostringstream ss;
        ss << "Theme Organization Stats:\n"
           << "  Total themes: " << stats.total_themes << "\n"
           << "  Total memberships: " << stats.total_memberships << "\n"
           << "  Orphan memories: " << stats.orphan_memories << "\n"
           << "  Avg theme size: " << std::fixed << std::setprecision(1) << stats.avg_theme_size << "\n"
           << "  Avg coherence: " << std::setprecision(2) << stats.avg_coherence << "\n"
           << "  Size variance: " << std::setprecision(1) << stats.size_variance << "\n"
           << "  Undersized themes (<3): " << stats.undersized_themes << "\n"
           << "  Oversized themes (>100): " << stats.oversized_themes << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"total_themes", stats.total_themes},
            {"total_memberships", stats.total_memberships},
            {"orphan_memories", stats.orphan_memories},
            {"avg_theme_size", stats.avg_theme_size},
            {"avg_coherence", stats.avg_coherence},
            {"size_variance", stats.size_variance},
            {"undersized_themes", stats.undersized_themes},
            {"oversized_themes", stats.oversized_themes}
        });
    }

    DuckDBToolResult tool_theme_maintain(const json& params) {
        std::string realm = params.value("realm", "");

        auto result = mind_->run_theme_maintenance(realm);

        std::ostringstream ss;
        ss << "Theme Maintenance Complete:\n"
           << "  Themes split: " << result.themes_split << "\n"
           << "  Themes merged: " << result.themes_merged << "\n"
           << "  Memories reassigned: " << result.memories_reassigned << "\n"
           << "  Representatives updated: " << result.representatives_updated << "\n"
           << "  Centroids recomputed: " << result.centroids_recomputed << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"themes_split", result.themes_split},
            {"themes_merged", result.themes_merged},
            {"memories_reassigned", result.memories_reassigned},
            {"representatives_updated", result.representatives_updated},
            {"centroids_recomputed", result.centroids_recomputed}
        });
    }

    DuckDBToolResult tool_theme_assign_orphans(const json& params) {
        size_t batch_size = params.value("batch_size", 100);
        std::string realm = params.value("realm", "");

        auto* theme_mgr = mind_->theme_manager();
        if (!theme_mgr) {
            return DuckDBToolResult::error("ThemeManager not initialized");
        }

        size_t assigned = theme_mgr->assign_orphans(batch_size, realm);
        size_t remaining = theme_mgr->orphan_count(realm);
        size_t theme_count = mind_->store().theme_list(realm).size();

        std::ostringstream ss;
        ss << "Assigned " << assigned << " orphan memories to themes\n"
           << "Remaining orphans: " << remaining << "\n"
           << "Total themes: " << theme_count;

        return DuckDBToolResult::ok(ss.str(), {
            {"assigned", assigned},
            {"remaining_orphans", remaining},
            {"theme_count", theme_count}
        });
    }

    DuckDBToolResult tool_learn_outcome(const json& params) {
        // Parse memory ID (can be integer or UUID string)
        // Try both "memory_id" (RPC) and "memory-id" (CLI)
        auto [memory_id, id_str] = parse_id(params, "memory_id");
        if (memory_id == 0) {
            std::tie(memory_id, id_str) = parse_id(params, "memory-id");
        }
        if (memory_id == 0) {
            return DuckDBToolResult::error("Invalid memory_id");
        }

        std::string outcome = params.value("outcome", "");
        if (outcome != "positive" && outcome != "negative" && outcome != "neutral") {
            return DuckDBToolResult::error("outcome must be positive, negative, or neutral");
        }

        std::string context = params.value("context", "");

        // Get session ID (if available from environment or use default)
        std::string session_id = "current_session";

        // Record the outcome
        int64_t outcome_id = mind_->store().record_usage_outcome(memory_id, session_id, outcome, context);
        if (outcome_id < 0) {
            return DuckDBToolResult::error("Failed to record outcome");
        }

        // Adjust confidence based on outcome
        // Construct NodeId from int64
        NodeId nid;
        nid.high = 0;
        nid.low = static_cast<uint64_t>(memory_id);

        if (outcome == "positive") {
            mind_->strengthen(nid, 0.1f);
        } else if (outcome == "negative") {
            mind_->weaken(nid, 0.15f);
            // Flag high-value categories for review if negative
            auto mem = mind_->store().get_memory(memory_id);
            if (mem && (mem->kind == "correction" || mem->kind == "preference" ||
                        mem->kind == "solution" || mem->kind == "gotcha")) {
                // Could add to synthesis queue for review
            }
        }

        // Get updated stats
        auto stats = mind_->store().get_usage_stats(memory_id);

        std::ostringstream ss;
        ss << "Recorded " << outcome << " outcome for memory " << memory_id << "\n"
           << "Stats: " << stats.positive << " positive, "
           << stats.negative << " negative, "
           << stats.neutral << " neutral "
           << "(positive rate: " << std::fixed << std::setprecision(1)
           << (stats.positive_rate * 100) << "%)\n";

        if (outcome == "positive") {
            ss << "Confidence boosted by 0.1";
        } else if (outcome == "negative") {
            ss << "Confidence reduced by 0.15";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"outcome_id", outcome_id},
            {"memory_id", memory_id},
            {"outcome", outcome},
            {"stats", {
                {"positive", stats.positive},
                {"negative", stats.negative},
                {"neutral", stats.neutral},
                {"positive_rate", stats.positive_rate}
            }}
        });
    }

    DuckDBToolResult tool_log_exposure(const json& params) {
        auto session_id = params.value("session_id", std::string{});
        auto turn_id = params.value("turn_id", 0);
        auto hook_type = params.value("hook_type", std::string{});

        if (session_id.empty() || hook_type.empty()) {
            return DuckDBToolResult::error("session_id and hook_type required");
        }

        std::vector<int64_t> memory_ids;
        if (params.contains("memory_ids") && params["memory_ids"].is_array()) {
            for (const auto& id : params["memory_ids"]) memory_ids.push_back(id.get<int64_t>());
        }
        if (memory_ids.empty()) return DuckDBToolResult::error("memory_ids required");

        std::vector<int> ranks;
        if (params.contains("ranks") && params["ranks"].is_array()) {
            for (const auto& r : params["ranks"]) ranks.push_back(r.get<int>());
        }
        std::vector<double> scores;
        if (params.contains("resonance_scores") && params["resonance_scores"].is_array()) {
            for (const auto& s : params["resonance_scores"]) scores.push_back(s.get<double>());
        }

        auto n = mind_->store().log_exposures_batch(
            session_id, turn_id, hook_type, memory_ids, ranks, {}, scores, {});

        return DuckDBToolResult::ok("Logged " + std::to_string(n) + " exposures",
            {{"logged", n}});
    }

    DuckDBToolResult tool_get_sus_metrics(const json& params) {
        int days = params.value("days", 7);
        auto m = compute_sus(days);

        json result = {
            {"days", m.days},
            {"n_sessions", m.n_sessions},
            {"n_exposures", m.n_exposures},
            {"n_recalls", m.n_recalls},
            {"n_memories", m.n_memories},
            {"R", m.R >= 0 ? json(m.R) : json(nullptr)},
            {"P", m.P >= 0 ? json(m.P) : json(nullptr)},
            {"M", m.M >= 0 ? json(m.M) : json(nullptr)},
            {"T", m.T >= 0 ? json(m.T) : json(nullptr)},
            {"D", m.D >= 0 ? json(m.D) : json(nullptr)},
            {"sus_partial", m.sus >= 0 ? json(m.sus) : json(nullptr)},
            {"note", m.M >= 0 ? "Full SUS score (all dimensions)" : "M accumulating (need >=3 correction exposures)"}
        };

        std::string summary = "SUS(" + std::to_string(days) + "d): ";
        if (m.sus >= 0) summary += std::to_string((int)m.sus);
        else summary += "--";
        summary += "  R:" + (m.R >= 0 ? std::to_string(m.R).substr(0,4) : "--");
        summary += " P:" + (m.P >= 0 ? std::to_string(m.P).substr(0,4) : "--");
        summary += " D:" + (m.D >= 0 ? std::to_string(m.D).substr(0,4) : "--");

        return DuckDBToolResult::ok(summary, result);
    }

    DuckDBToolResult tool_episode_cluster_status(const json& params) {
        // Accept both underscore and hyphen versions of params
        float similarity_threshold = params.contains("similarity_threshold")
            ? params.value("similarity_threshold", 0.85f)
            : params.value("similarity-threshold", 0.85f);
        size_t min_occurrences = params.contains("min_occurrences")
            ? params.value("min_occurrences", 3)
            : params.value("min-occurrences", 3);

        auto candidates = mind_->store().find_distill_candidates(
            similarity_threshold, min_occurrences, 20);

        std::ostringstream ss;
        ss << "Episode Cluster Status (for Auto-Distillation)\n"
           << "Similarity threshold: " << similarity_threshold << "\n"
           << "Minimum occurrences: " << min_occurrences << "\n\n";

        if (candidates.empty()) {
            ss << "No episode clusters found for distillation.\n";
        } else {
            ss << "Found " << candidates.size() << " candidate clusters:\n\n";
            for (size_t i = 0; i < candidates.size(); ++i) {
                const auto& c = candidates[i];
                ss << "Cluster " << (i + 1) << ":\n"
                   << "  Episodes: " << c.episode_ids.size() << "\n"
                   << "  Avg similarity: " << std::fixed << std::setprecision(2)
                   << c.avg_similarity << "\n"
                   << "  Avg confidence: " << c.avg_confidence << "\n"
                   << "  Pattern: " << c.pattern_content.substr(0, 100)
                   << (c.pattern_content.size() > 100 ? "..." : "") << "\n\n";
            }
        }

        json result = {
            {"similarity_threshold", similarity_threshold},
            {"min_occurrences", min_occurrences},
            {"cluster_count", candidates.size()},
            {"clusters", json::array()}
        };
        for (const auto& c : candidates) {
            result["clusters"].push_back({
                {"episode_count", c.episode_ids.size()},
                {"avg_similarity", c.avg_similarity},
                {"avg_confidence", c.avg_confidence},
                {"pattern_preview", c.pattern_content.substr(0, 200)}
            });
        }

        return DuckDBToolResult::ok(ss.str(), result);
    }

    DuckDBToolResult tool_restore_code_intel_confidence(const json& params) {
        float confidence = params.value("confidence", 0.8f);
        bool dry_run = params.value("dry_run", false);

        auto result = mind_->store().restore_code_intel_confidence(confidence, dry_run);

        std::ostringstream ss;
        ss << "Code intel confidence restoration " << (dry_run ? "(DRY RUN)" : "complete") << ":\n";
        for (size_t i = 0; i < result.counts_by_kind.size(); ++i) {
            const auto& [kind, count] = result.counts_by_kind[i];
            float avg_before = i < result.avg_confidence_before.size()
                ? result.avg_confidence_before[i].second : 0.0f;
            ss << "  " << kind << ": " << count << " memories"
               << " (avg conf before: " << std::fixed << std::setprecision(2) << avg_before;
            if (!dry_run) {
                ss << " → " << confidence;
            }
            ss << ")\n";
        }
        if (!dry_run) {
            ss << "Total updated: " << result.total_updated << " memories\n";
            ss << "Decay rate set to 0.0 (never decay)\n";
        }

        json result_json = {
            {"dry_run", dry_run},
            {"confidence", confidence},
            {"total_updated", result.total_updated}
        };
        for (size_t i = 0; i < result.counts_by_kind.size(); ++i) {
            const auto& [kind, count] = result.counts_by_kind[i];
            float avg_before = i < result.avg_confidence_before.size()
                ? result.avg_confidence_before[i].second : 0.0f;
            result_json[kind] = {
                {"count", count},
                {"avg_confidence_before", avg_before}
            };
        }

        return DuckDBToolResult::ok(ss.str(), result_json);
    }

    DuckDBToolResult tool_sql_query(const json& params) {
        std::string query = params.value("query", "");
        size_t limit = params.value("limit", 20);

        if (query.empty()) {
            return DuckDBToolResult::error("Query is required");
        }

        // Safety: only allow SELECT queries
        std::string upper_query = query;
        std::transform(upper_query.begin(), upper_query.end(), upper_query.begin(), ::toupper);
        if (upper_query.find("SELECT") != 0 && upper_query.find("WITH") != 0 &&
            upper_query.find("SHOW") != 0 && upper_query.find("DESCRIBE") != 0) {
            return DuckDBToolResult::error("Only SELECT/WITH/SHOW/DESCRIBE queries allowed");
        }

        // Add LIMIT if not present
        if (upper_query.find("LIMIT") == std::string::npos) {
            query += " LIMIT " + std::to_string(limit);
        }

        auto result = mind_->store().execute_sql_query(query);
        if (!result.success) {
            return DuckDBToolResult::error("SQL error: " + result.error);
        }

        if (result.rows.empty()) {
            return DuckDBToolResult::ok("Query returned 0 rows", {{"rows", json::array()}, {"count", 0}});
        }

        std::ostringstream ss;
        json rows_json = json::array();

        // Format as table header
        ss << "| ";
        for (const auto& col : result.columns) {
            ss << col << " | ";
        }
        ss << "\n|";
        for (size_t i = 0; i < result.columns.size(); ++i) {
            ss << "---|";
        }
        ss << "\n";

        // Format rows
        size_t displayed = 0;
        for (const auto& row : result.rows) {
            if (displayed >= limit) break;
            json row_obj;
            ss << "| ";
            for (size_t col = 0; col < row.size() && col < result.columns.size(); ++col) {
                row_obj[result.columns[col]] = row[col];
                ss << row[col] << " | ";
            }
            ss << "\n";
            rows_json.push_back(row_obj);
            ++displayed;
        }

        ss << "\n" << displayed << " row(s)";

        return DuckDBToolResult::ok(ss.str(), {{"rows", rows_json}, {"count", displayed}});
    }

    // ========================================================================
    // Cross-Project Learning: insight promotion across realms
    // ========================================================================

    DuckDBToolResult tool_insight_promote(const json& params) {
        auto [id, id_str] = parse_id(params);
        std::string reason = params.value("reason", "");

        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        // Get the memory
        auto mem = mind_->store().get_memory(id);
        if (!mem) {
            return DuckDBToolResult::error("Memory not found: " + id_str);
        }

        // Update visibility to global
        bool ok = mind_->store().update_visibility(id, RealmVisibility::Global);
        if (!ok) {
            return DuckDBToolResult::error("Failed to promote memory");
        }

        // Add promotion triplet for tracking
        std::string slug = "memory_" + std::to_string(id);
        mind_->store().connect(slug, "promoted_to", "global");
        if (!reason.empty()) {
            mind_->store().connect(slug, "promotion_reason", reason);
        }

        std::ostringstream ss;
        ss << "Promoted memory #" << id << " to global visibility\n";
        ss << "Content: " << mem->content.substr(0, 100) << (mem->content.size() > 100 ? "..." : "") << "\n";
        if (!reason.empty()) {
            ss << "Reason: " << reason;
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"id", id},
            {"visibility", 2},
            {"reason", reason}
        });
    }

    DuckDBToolResult tool_insight_global(const json& params) {
        size_t limit = params.value("limit", 20);
        std::string kind = params.value("kind", "");

        auto memories = mind_->store().list_global_memories(limit, kind);

        if (memories.empty()) {
            return DuckDBToolResult::ok("No global memories found", {{"count", 0}});
        }

        std::ostringstream ss;
        ss << "Global Insights (" << memories.size() << "):\n";
        ss << "══════════════════════════════\n\n";

        json items = json::array();
        for (const auto& m : memories) {
            ss << "#" << m.id << " [" << m.kind << "] ";
            ss << m.content.substr(0, 80) << (m.content.size() > 80 ? "..." : "") << "\n";
            ss << "  Confidence: " << std::fixed << std::setprecision(2) << m.confidence;
            ss << " | Source: " << m.realm << "\n\n";

            items.push_back({
                {"id", m.id},
                {"kind", m.kind},
                {"content", m.content},
                {"confidence", m.confidence},
                {"realm", m.realm}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"count", memories.size()},
            {"memories", items}
        });
    }

    // ========================================================================
    // Aspect-based memory access
    // ========================================================================

    // Aspect to node kind mapping (must match duckdb_store.cpp)
    static inline const std::unordered_map<std::string, std::vector<std::string>> ASPECT_KINDS = {
        {"preferences", {"preference"}},
        {"corrections", {"correction"}},
        {"insights", {"insight", "wisdom"}},
        {"failures", {"failure"}},
        {"decisions", {"decision"}},
        {"approaches", {"approach"}},
        {"milestones", {"milestone"}},
        {"goals", {"goal"}},
        {"habits", {"habit"}},
        {"beliefs", {"belief", "invariant"}},
        {"wisdom", {"wisdom", "insight"}},
        {"code", {"symbol", "function", "class", "file", "dependency"}},
        {"gaps", {"gap", "curiosity"}},
        {"analyses", {"analysis"}}
    };

    DuckDBToolResult tool_list_by_aspect(const json& params) {
        std::string aspect = params.value("aspect", "");
        if (aspect.empty()) {
            return DuckDBToolResult::error("aspect parameter required");
        }

        size_t limit = params.value("limit", 30);
        float min_confidence = params.value("min_confidence", 0.1f);

        auto memories = mind_->store().list_by_aspect(aspect, limit, min_confidence);

        if (memories.empty()) {
            // Check if aspect is valid
            if (ASPECT_KINDS.find(aspect) == ASPECT_KINDS.end()) {
                std::ostringstream ss;
                ss << "Unknown aspect: '" << aspect << "'. Valid aspects: ";
                bool first = true;
                for (const auto& [name, _] : ASPECT_KINDS) {
                    if (!first) ss << ", ";
                    ss << name;
                    first = false;
                }
                return DuckDBToolResult::error(ss.str());
            }
            return DuckDBToolResult::ok("No memories found for aspect: " + aspect, {{"count", 0}});
        }

        std::ostringstream ss;
        ss << "Memories for aspect '" << aspect << "' (" << memories.size() << "):\n";
        ss << "══════════════════════════════\n\n";

        json items = json::array();
        for (const auto& m : memories) {
            ss << "#" << m.id << " [" << m.kind << "] ";
            ss << m.content.substr(0, 80) << (m.content.size() > 80 ? "..." : "") << "\n";
            ss << "  Confidence: " << std::fixed << std::setprecision(2) << m.confidence;
            ss << " | Realm: " << m.realm << "\n\n";

            items.push_back({
                {"id", m.id},
                {"kind", m.kind},
                {"content", m.content},
                {"confidence", m.confidence},
                {"realm", m.realm}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"count", memories.size()},
            {"aspect", aspect},
            {"memories", items}
        });
    }

    DuckDBToolResult tool_list_aspects(const json& params) {
        (void)params;  // Unused

        std::ostringstream ss;
        ss << "Available Semantic Aspects:\n";
        ss << "══════════════════════════════\n\n";

        json aspects = json::array();
        for (const auto& [aspect, kinds] : ASPECT_KINDS) {
            ss << "  " << aspect << ": ";
            for (size_t i = 0; i < kinds.size(); ++i) {
                if (i > 0) ss << ", ";
                ss << kinds[i];
            }
            ss << "\n";

            aspects.push_back({
                {"name", aspect},
                {"kinds", kinds}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"aspects", aspects}});
    }

    // ========================================================================
    // Memory Index: Fast pre-retrieval scanning (ClawVault-inspired)
    // ========================================================================

    DuckDBToolResult tool_list_memories_brief(const json& params) {
        size_t limit = params.value("limit", 200);
        std::string realm = params.value("realm", "");
        std::string kind = params.value("kind", "");
        std::optional<PriorityTier> tier;
        if (params.contains("priority_tier")) {
            tier = static_cast<PriorityTier>(params["priority_tier"].get<int>());
        }

        auto entries = mind_->store().list_memories_brief(limit, realm, kind, tier);

        std::ostringstream ss;
        ss << "Memory Index (" << entries.size() << " entries):\n";
        ss << "══════════════════════════════════════════════════════════════════\n";
        ss << "ID       | Tier | Kind       | Date       | Preview\n";
        ss << "---------|------|------------|------------|---------------------------\n";

        json items = json::array();
        for (const auto& e : entries) {
            // Format date
            auto ms = std::chrono::milliseconds(e.created_at);
            auto tp = std::chrono::system_clock::time_point(ms);
            auto tt = std::chrono::system_clock::to_time_t(tp);
            std::tm tm = *std::localtime(&tt);
            char date_buf[16];
            std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm);

            // Tier emoji
            const char* tier_str = "🟢";
            if (e.priority_tier == PriorityTier::Critical) tier_str = "🔴";
            else if (e.priority_tier == PriorityTier::Notable) tier_str = "🟡";

            // Truncate one-liner for display
            std::string preview = e.one_liner;
            if (preview.size() > 40) preview = preview.substr(0, 37) + "...";

            ss << std::setw(8) << e.id << " | " << tier_str << "   | "
               << std::setw(10) << e.kind.substr(0, 10) << " | "
               << date_buf << " | " << preview << "\n";

            items.push_back({
                {"id", e.id},
                {"kind", e.kind},
                {"priority_tier", static_cast<int>(e.priority_tier)},
                {"created_at", e.created_at},
                {"one_liner", e.one_liner}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"count", entries.size()},
            {"entries", items}
        });
    }

    DuckDBToolResult tool_set_priority_tier(const json& params) {
        if (!params.contains("memory_id") || !params.contains("tier")) {
            return DuckDBToolResult::error("memory_id and tier required");
        }

        int64_t memory_id = params["memory_id"].get<int64_t>();
        int tier_val = params["tier"].get<int>();

        if (tier_val < 0 || tier_val > 2) {
            return DuckDBToolResult::error("tier must be 0 (background), 1 (notable), or 2 (critical)");
        }

        PriorityTier tier = static_cast<PriorityTier>(tier_val);
        bool success = mind_->store().set_priority_tier(memory_id, tier);

        if (!success) {
            return DuckDBToolResult::error("Failed to set priority tier");
        }

        const char* tier_emoji = tier == PriorityTier::Critical ? "🔴" :
                                 tier == PriorityTier::Notable ? "🟡" : "🟢";
        const char* tier_name = tier == PriorityTier::Critical ? "critical" :
                                tier == PriorityTier::Notable ? "notable" : "background";

        std::ostringstream ss;
        ss << "Set memory #" << memory_id << " to " << tier_emoji << " " << tier_name << " tier";

        return DuckDBToolResult::ok(ss.str(), {
            {"memory_id", memory_id},
            {"tier", tier_val},
            {"tier_name", tier_name}
        });
    }

    DuckDBToolResult tool_recall_by_priority(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        std::string query = params.value("query", "");
        size_t budget_tokens = params.value("budget_tokens", 4000);
        std::string realm = params.value("realm", "");
        bool include_global = params.value("include_global", true);

        // Get embedding if query provided
        std::vector<float> embedding;
        if (!query.empty() && mind_->embedder_ready()) {
            embedding = mind_->embedder().embed_query(query).data;
        }

        auto results = mind_->store().recall_by_priority(embedding, budget_tokens, realm, include_global);

        std::ostringstream ss;
        ss << "Budget-Aware Recall (" << results.size() << " memories, ~" << budget_tokens << " token budget):\n";
        ss << "══════════════════════════════════════════════════════════════════\n\n";

        // Count by tier
        int critical_count = 0, notable_count = 0, background_count = 0;
        size_t total_chars = 0;
        json items = json::array();

        for (const auto& m : results) {
            if (m.priority_tier == PriorityTier::Critical) critical_count++;
            else if (m.priority_tier == PriorityTier::Notable) notable_count++;
            else background_count++;
            total_chars += m.content.size();

            const char* tier_emoji = m.priority_tier == PriorityTier::Critical ? "🔴" :
                                     m.priority_tier == PriorityTier::Notable ? "🟡" : "🟢";

            ss << tier_emoji << " #" << m.id << " [" << m.kind << "]\n";
            ss << "   " << m.content.substr(0, 100) << (m.content.size() > 100 ? "..." : "") << "\n\n";

            items.push_back({
                {"id", m.id},
                {"kind", m.kind},
                {"content", m.content},
                {"confidence", m.confidence},
                {"priority_tier", static_cast<int>(m.priority_tier)}
            });
        }

        ss << "───────────────────────────────────────────\n";
        ss << "Tiers: 🔴 " << critical_count << " | 🟡 " << notable_count << " | 🟢 " << background_count << "\n";
        ss << "Est. tokens: ~" << (total_chars / 4) << " / " << budget_tokens << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"count", results.size()},
            {"critical_count", critical_count},
            {"notable_count", notable_count},
            {"background_count", background_count},
            {"estimated_tokens", total_chars / 4},
            {"budget_tokens", budget_tokens},
            {"memories", items}
        });
    }

    // ========================================================================
    // Memory Type Taxonomy (formalized aspect system)
    // ========================================================================

    // Valid memory types (normalized from aspect system)
    static constexpr std::array<const char*, 11> VALID_MEMORY_TYPES = {
        "decision", "preference", "correction", "insight", "milestone",
        "approach", "habit", "belief", "gap", "wisdom", "episode"
    };

    DuckDBToolResult tool_set_memory_type(const json& params) {
        if (!params.contains("memory_id") || !params.contains("type")) {
            return DuckDBToolResult::error("memory_id and type required");
        }

        int64_t memory_id = params["memory_id"].get<int64_t>();
        std::string type = params["type"].get<std::string>();

        // Validate type
        bool valid = false;
        for (const auto& t : VALID_MEMORY_TYPES) {
            if (type == t) { valid = true; break; }
        }

        if (!valid) {
            std::ostringstream err;
            err << "Invalid type '" << type << "'. Valid types: ";
            for (size_t i = 0; i < VALID_MEMORY_TYPES.size(); ++i) {
                if (i > 0) err << ", ";
                err << VALID_MEMORY_TYPES[i];
            }
            return DuckDBToolResult::error(err.str());
        }

        // Check memory exists
        auto mem = mind_->store().get_memory(memory_id);
        if (!mem) {
            return DuckDBToolResult::error("Memory not found");
        }

        // Update kind via store
        bool success = mind_->store().update_kind(memory_id, type);
        if (!success) {
            return DuckDBToolResult::error("Failed to update memory type");
        }

        return DuckDBToolResult::ok(
            "Set memory #" + std::to_string(memory_id) + " type to: " + type,
            {{"memory_id", memory_id}, {"type", type}, {"previous_type", mem->kind}}
        );
    }

    DuckDBToolResult tool_memory_type_stats(const json& params) {
        std::string realm = params.value("realm", "");

        std::ostringstream where;
        if (!realm.empty()) {
            std::string escaped;
            for (char c : realm) {
                if (c == '\'') escaped += "''";
                else escaped += c;
            }
            where << " WHERE (realm = '" << escaped << "' OR visibility = 2)";
        }

        // Query for kind counts
        std::ostringstream sql;
        sql << "SELECT COALESCE(kind, 'unknown') as kind, COUNT(*) as count "
            << "FROM memory" << where.str()
            << " GROUP BY kind ORDER BY count DESC";

        auto result = mind_->store().raw_query(sql.str());

        std::ostringstream ss;
        ss << "Memory Type Statistics:\n";
        ss << "══════════════════════════════\n\n";

        json by_kind = json::object();
        if (result && !result->HasError()) {
            while (auto chunk = result->Fetch()) {
                if (!chunk || chunk->size() == 0) break;
                for (size_t i = 0; i < chunk->size(); ++i) {
                    std::string kind = chunk->GetValue(0, i).GetValue<std::string>();
                    int64_t count = chunk->GetValue(1, i).GetValue<int64_t>();
                    ss << "  " << std::setw(15) << kind << ": " << count << "\n";
                    by_kind[kind] = count;
                }
            }
        }

        // Query for priority tier counts
        std::ostringstream tier_sql;
        tier_sql << "SELECT COALESCE(priority_tier, 0) as tier, COUNT(*) as count "
                 << "FROM memory" << where.str()
                 << " GROUP BY priority_tier ORDER BY tier DESC";

        auto tier_result = mind_->store().raw_query(tier_sql.str());

        ss << "\nPriority Tiers:\n";
        json by_tier = json::object();
        int critical = 0, notable = 0, background = 0;
        if (tier_result && !tier_result->HasError()) {
            while (auto chunk = tier_result->Fetch()) {
                if (!chunk || chunk->size() == 0) break;
                for (size_t i = 0; i < chunk->size(); ++i) {
                    int tier = chunk->GetValue(0, i).GetValue<int32_t>();
                    int64_t count = chunk->GetValue(1, i).GetValue<int64_t>();
                    if (tier == 2) { critical = count; ss << "  🔴 Critical: " << count << "\n"; }
                    else if (tier == 1) { notable = count; ss << "  🟡 Notable: " << count << "\n"; }
                    else { background = count; ss << "  🟢 Background: " << count << "\n"; }
                }
            }
        }

        by_tier["critical"] = critical;
        by_tier["notable"] = notable;
        by_tier["background"] = background;

        return DuckDBToolResult::ok(ss.str(), {
            {"by_kind", by_kind},
            {"by_tier", by_tier}
        });
    }

    // ========================================================================
    // Smart Recall: unified query intent classification and routing
    // ========================================================================

    DuckDBToolResult tool_smart_recall(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        std::string query = params.value("query", "");
        if (query.empty()) {
            return DuckDBToolResult::error("query parameter required");
        }

        size_t limit = params.value("limit", 20);
        size_t expand_top = params.value("expand_top", 2);
        std::string realm = params.value("realm", "");
        bool include_global = params.value("include_global", true);

        // 1. Classify the query intent
        QueryIntentClassifier classifier;
        QueryIntent intent = classifier.classify(query);

        std::vector<MemoryResult> results;
        std::string route_taken;

        // 2. Route based on intent type
        switch (intent.type) {
            case QueryIntentType::Aspect: {
                // Use list_by_aspect with the detected aspect
                if (intent.aspect) {
                    results = mind_->store().list_by_aspect(*intent.aspect, limit, 0.1f);
                    route_taken = "aspect:" + *intent.aspect;
                }
                break;
            }

            case QueryIntentType::Temporal: {
                // Enhanced temporal routing based on subtype
                json temporal_candidates = json::array();

                // Query temporal triplets for extracted entities
                for (const auto& entity : intent.entities) {
                    std::string entity_lower;
                    entity_lower.reserve(entity.size());
                    for (char c : entity) {
                        entity_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }

                    auto triplets = mind_->store().query_triplets_temporal(
                        entity_lower, "", "", 0, limit
                    );

                    for (const auto& t : triplets) {
                        if (t.valid_from_ms > 0) {
                            temporal_candidates.push_back({
                                {"entity", t.subject},
                                {"predicate", t.predicate},
                                {"object", t.object},
                                {"date", TemporalResolver::format_iso_date(t.valid_from_ms)},
                                {"timestamp_ms", t.valid_from_ms}
                            });
                        }
                    }
                }

                // If we have temporal candidates, return them with context
                if (!temporal_candidates.empty()) {
                    // Also do semantic recall for supporting context
                    if (mind_->embedder_ready()) {
                        auto embedding = mind_->embedder().embed_query(query).data;
                        results = mind_->store().recall(embedding, limit, realm, include_global);
                    }

                    std::ostringstream ss;
                    ss << "Temporal Query Results\n";
                    ss << "══════════════════════════════\n";
                    ss << "Subtype: " << temporal_subtype_to_string(intent.temporal_subtype) << "\n\n";
                    ss << "Temporal Facts (" << temporal_candidates.size() << "):\n";
                    for (const auto& tc : temporal_candidates) {
                        ss << "  - " << tc["entity"].get<std::string>()
                           << " " << tc["predicate"].get<std::string>()
                           << " " << tc["object"].get<std::string>()
                           << " @" << tc["date"].get<std::string>() << "\n";
                    }

                    json results_json = json::array();
                    for (const auto& r : results) {
                        results_json.push_back({
                            {"id", std::to_string(r.id)},
                            {"content", r.content.substr(0, 200)},
                            {"similarity", r.similarity}
                        });
                    }

                    return DuckDBToolResult::ok(ss.str(), {
                        {"intent", {
                            {"type", query_intent_type_to_string(intent.type)},
                            {"temporal_subtype", temporal_subtype_to_string(intent.temporal_subtype)},
                            {"confidence", intent.confidence},
                            {"entities", intent.entities}
                        }},
                        {"route", "temporal"},
                        {"temporal_candidates", temporal_candidates},
                        {"results", results_json},
                        {"count", results.size()}
                    });
                }

                // Fall back to temporal recall with time range if no triplet matches
                if (intent.time_range && intent.time_range->valid()) {
                    auto& tr = *intent.time_range;
                    std::optional<int64_t> start_ms, end_ms;
                    if (tr.start) {
                        start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            tr.start->time_since_epoch()).count();
                    }
                    if (tr.end) {
                        end_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            tr.end->time_since_epoch()).count();
                    }
                    // Get query embedding if there's still an entity component
                    std::vector<float> embedding;
                    if (intent.entity && !intent.entity->empty() && mind_->embedder_ready()) {
                        embedding = mind_->embedder().embed_query(*intent.entity).data;
                    }
                    results = mind_->store().recall_temporal(embedding, start_ms, end_ms, limit, realm, include_global);
                    route_taken = "temporal";
                }
                break;
            }

            case QueryIntentType::Entity: {
                // Standard semantic recall on the entity
                if (intent.entity && !intent.entity->empty() && mind_->embedder_ready()) {
                    auto embedding = mind_->embedder().embed_query(*intent.entity).data;
                    results = mind_->store().recall(embedding, limit, realm, include_global);
                    route_taken = "entity:" + *intent.entity;
                }
                break;
            }

            case QueryIntentType::Relationship: {
                // Query the triplet graph for connections between entities
                if (intent.subject && intent.object) {
                    // Get triplets from subject and object
                    auto subject_triplets = mind_->store().query_subject(*intent.subject);
                    auto object_triplets = mind_->store().query_object(*intent.object);

                    // Build response from triplet data (no direct memory results)
                    std::ostringstream ss;
                    ss << "Relationships involving '" << *intent.subject << "' and '" << *intent.object << "':\n";
                    ss << "══════════════════════════════\n\n";

                    json triplets_json = json::array();
                    size_t count = 0;

                    // Subject triplets
                    for (const auto& t : subject_triplets) {
                        if (count >= limit) break;
                        ss << "  " << t.subject << " --[" << t.predicate << "]--> " << t.object << "\n";
                        triplets_json.push_back({
                            {"subject", t.subject},
                            {"predicate", t.predicate},
                            {"object", t.object},
                            {"weight", t.weight}
                        });
                        count++;
                    }

                    // Object triplets (if room)
                    for (const auto& t : object_triplets) {
                        if (count >= limit) break;
                        ss << "  " << t.subject << " --[" << t.predicate << "]--> " << t.object << "\n";
                        triplets_json.push_back({
                            {"subject", t.subject},
                            {"predicate", t.predicate},
                            {"object", t.object},
                            {"weight", t.weight}
                        });
                        count++;
                    }

                    return DuckDBToolResult::ok(ss.str(), {
                        {"intent", {
                            {"type", query_intent_type_to_string(intent.type)},
                            {"confidence", intent.confidence},
                            {"subject", intent.subject.value_or("")},
                            {"object", intent.object.value_or("")}
                        }},
                        {"route", "relationship"},
                        {"triplets", triplets_json},
                        {"count", count}
                    });
                }
                break;
            }

            case QueryIntentType::Code: {
                // Code queries should go through find_symbol/search_symbols
                return DuckDBToolResult::ok(
                    "Code queries are best handled by dedicated tools:\n"
                    "  - find_symbol: search by name/kind\n"
                    "  - search_symbols: semantic search\n"
                    "  - symbol_callers/symbol_callees: call graph navigation\n"
                    "  - read_symbol/read_function: get source code\n",
                    {
                        {"intent", {
                            {"type", "code"},
                            {"confidence", intent.confidence},
                            {"entity", intent.entity.value_or("")}
                        }},
                        {"route", "code"},
                        {"suggestion", "Use find_symbol or search_symbols for code queries"}
                    }
                );
            }

            case QueryIntentType::Meta: {
                // Return memory stats
                auto health = mind_->store().health();
                auto hygiene = mind_->store().hygiene_stats();

                std::ostringstream ss;
                ss << "Memory Health Stats:\n";
                ss << "══════════════════════════════\n\n";
                ss << "  Total memories: " << health.total_memories << "\n";
                ss << "  Total symbols: " << health.total_symbols << "\n";
                ss << "  Total triplets: " << health.total_triplets << "\n";
                ss << "  Average confidence: " << std::fixed << std::setprecision(2) << health.avg_confidence << "\n";
                ss << "\n";
                ss << "  High confidence (>0.7): " << hygiene.high_confidence << "\n";
                ss << "  Medium confidence: " << hygiene.medium_confidence << "\n";
                ss << "  Low confidence (<0.3): " << hygiene.low_confidence << "\n";
                ss << "  Old unaccessed (30+ days): " << hygiene.old_unaccessed << "\n";
                ss << "  Consolidation candidates: " << hygiene.consolidation_candidates << "\n";

                return DuckDBToolResult::ok(ss.str(), {
                    {"intent", {
                        {"type", "meta"},
                        {"confidence", intent.confidence}
                    }},
                    {"route", "meta"},
                    {"health", {
                        {"total_memories", health.total_memories},
                        {"total_symbols", health.total_symbols},
                        {"total_triplets", health.total_triplets},
                        {"avg_confidence", health.avg_confidence}
                    }},
                    {"hygiene", {
                        {"high_confidence", hygiene.high_confidence},
                        {"medium_confidence", hygiene.medium_confidence},
                        {"low_confidence", hygiene.low_confidence},
                        {"old_unaccessed", hygiene.old_unaccessed},
                        {"consolidation_candidates", hygiene.consolidation_candidates}
                    }}
                });
            }

            case QueryIntentType::Exploratory:
            default: {
                // Fall back to standard semantic recall
                if (mind_->embedder_ready()) {
                    auto embedding = mind_->embedder().embed_query(query).data;
                    results = mind_->store().recall(embedding, limit, realm, include_global);
                    route_taken = "exploratory";
                }
                break;
            }
        }

        // Format results (for non-special cases)
        if (results.empty() && route_taken.empty()) {
            return DuckDBToolResult::error("Could not process query - embedder may not be ready");
        }

        std::ostringstream ss;
        ss << "Smart Recall Results (" << results.size() << " found)\n";
        ss << "Route: " << route_taken << " | Intent: " << query_intent_type_to_string(intent.type);
        ss << " (" << static_cast<int>(intent.confidence * 100) << "% confidence)\n";
        ss << "══════════════════════════════\n\n";

        json results_json = json::array();
        json expanded_json = json::array();
        size_t expanded_count = 0;

        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            // Format timestamp for display
            std::time_t created_sec = r.created_at / 1000;
            std::tm* tm = std::localtime(&created_sec);
            char time_buf[32];
            std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", tm);

            int sim_pct = static_cast<int>(r.similarity * 100);
            ss << "#" << r.id << " [" << r.kind << "]";
            if (r.similarity > 0) ss << " [" << sim_pct << "%]";
            ss << " " << time_buf << "\n";
            ss << "  " << r.content.substr(0, 100) << (r.content.size() > 100 ? "..." : "") << "\n";

            json mem_json = {
                {"id", std::to_string(r.id)},
                {"kind", r.kind},
                {"content", r.content},
                {"confidence", r.confidence},
                {"similarity", r.similarity},
                {"created_at", r.created_at},
                {"created_at_str", std::string(time_buf)},
                {"realm", r.realm}
            };

            // Hierarchical expansion for top N results
            if (expand_top > 0 && expanded_count < expand_top) {
                auto expanded = mind_->store().expand_memory(r.id, 3);
                if (expanded && expanded->episode_id > 0) {
                    json exp;
                    exp["memory_id"] = r.id;
                    exp["episode_id"] = expanded->episode_id;
                    exp["episode_title"] = expanded->episode_title;
                    exp["session_id"] = expanded->session_id;
                    exp["start_turn"] = expanded->start_turn;
                    exp["end_turn"] = expanded->end_turn;

                    ss << "  └─ Episode: " << expanded->episode_title
                       << " (turns " << expanded->start_turn << "-" << expanded->end_turn << ")\n";

                    if (!expanded->turns.empty()) {
                        json turns_arr = json::array();
                        size_t total_chars = 0;
                        const size_t max_total_chars = 20000;  // Limit expanded content
                        const size_t max_turn_chars = 2000;    // Limit per turn

                        for (const auto& turn : expanded->turns) {
                            if (total_chars >= max_total_chars) {
                                turns_arr.push_back({
                                    {"role", "system"},
                                    {"content", "[... truncated, " + std::to_string(expanded->turns.size() - turns_arr.size()) + " more turns]"},
                                    {"turn_index", -1}
                                });
                                break;
                            }

                            std::string content = turn.content;
                            if (content.size() > max_turn_chars) {
                                content = content.substr(0, max_turn_chars) + "... [truncated]";
                            }
                            total_chars += content.size();

                            turns_arr.push_back({
                                {"role", turn.role},
                                {"content", content},
                                {"turn_index", turn.turn_index}
                            });
                        }
                        exp["turns"] = turns_arr;
                        exp["turn_count"] = expanded->turns.size();
                        exp["truncated"] = total_chars >= max_total_chars;
                    }

                    mem_json["expanded"] = exp;
                    expanded_json.push_back(exp);
                    expanded_count++;
                }
            }

            results_json.push_back(mem_json);
            ss << "\n";
        }

        // SUS: log recall query
        try {
            std::string _sus_sid = get_session_id(params);
            if (!_sus_sid.empty() && _sus_sid != "default") {
                json _sus_ids = json::array();
                json _sus_scores = json::array();
                for (const auto& rj : results_json) {
                    _sus_ids.push_back(rj["id"]);
                    _sus_scores.push_back(rj.value("similarity", 0.0));
                }
                mind_->store().log_recall_query(
                    _sus_sid, 0, "smart_recall", query,
                    _sus_ids.dump(), _sus_scores.dump());
            }
        } catch (...) {}

        return DuckDBToolResult::ok(ss.str(), {
            {"intent", {
                {"type", query_intent_type_to_string(intent.type)},
                {"temporal_subtype", temporal_subtype_to_string(intent.temporal_subtype)},
                {"confidence", intent.confidence},
                {"aspect", intent.aspect.value_or("")},
                {"entity", intent.entity.value_or("")},
                {"entities", intent.entities}
            }},
            {"route", route_taken},
            {"results", results_json},
            {"expanded", expanded_json},
            {"expanded_count", expanded_count},
            {"count", results.size()}
        });
    }

    // ========================================================================
    // SSL conversion tool
    // ========================================================================

    DuckDBToolResult tool_ssl_convert(const json& params) {
        std::string content = params.value("content", "");
        if (content.empty()) {
            return DuckDBToolResult::error("Content is required");
        }

        std::string domain = params.value("domain", "note");
        std::string location = params.value("location", "");

        if (is_ssl_format(content)) {
            return DuckDBToolResult::ok("Already in SSL format", {
                {"converted", false},
                {"content", content}
            });
        }

        std::string ssl_content = to_ssl_format(content, domain, location);

        std::ostringstream ss;
        ss << "Converted to SSL format:\n" << ssl_content;

        return DuckDBToolResult::ok(ss.str(), {
            {"converted", true},
            {"content", ssl_content},
            {"domain", domain}
        });
    }

    // ========================================================================
    // Narrative and Anticipation Tools
    // ========================================================================

    DuckDBToolResult tool_narrative_status(const json& params) {
        std::string session_id = get_session_id(params);

        auto& store = mind_->store();

        // Get current segment
        auto segment = store.segment_current(session_id);
        if (!segment) {
            return DuckDBToolResult::ok("No active session segment", {
                {"session_id", session_id},
                {"mode", "unknown"},
                {"confidence", 0.0f},
                {"segment_id", 0}
            });
        }

        std::ostringstream ss;
        ss << "Mode: " << work_mode_to_string(segment->mode)
           << " (" << std::fixed << std::setprecision(2) << segment->confidence << ")\n";
        ss << "Segment: " << segment->event_count << " events";

        // Parse files_active JSON array
        if (!segment->files_active.empty() && segment->files_active != "[]") {
            size_t count = std::count(segment->files_active.begin(), segment->files_active.end(), '"') / 2;
            ss << ", " << count << " files active";
        }

        // Parse tools_used JSON array
        if (!segment->tools_used.empty() && segment->tools_used != "[]") {
            size_t count = std::count(segment->tools_used.begin(), segment->tools_used.end(), '"') / 2;
            ss << ", " << count << " tools used";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"session_id", session_id},
            {"mode", work_mode_to_string(segment->mode)},
            {"confidence", segment->confidence},
            {"segment_id", segment->id},
            {"event_count", segment->event_count},
            {"started_at", segment->started_at},
            {"tools_used", segment->tools_used},
            {"files_active", segment->files_active}
        });
    }

    DuckDBToolResult tool_narrative_log(const json& params) {
        std::string session_id = get_session_id(params);
        std::string kind_str = params.value("kind", "");
        std::string summary = params.value("summary", "");

        if (kind_str.empty() || summary.empty()) {
            return DuckDBToolResult::error("kind and summary are required");
        }

        SessionEvent event;
        event.session_id = session_id;
        event.kind = string_to_session_event_kind(kind_str);
        event.summary = summary;
        event.tool_name = params.value("tool_name", "");
        event.success = params.value("success", true);
        event.payload = params.value("payload", "");
        event.files_mentioned = params.value("files_mentioned", "[]");
        event.realm = params.value("realm", "brahman");

        // Use NarrativeEngine to record event (handles mode inference and segments)
        auto* narrative = mind_->narrative();
        if (!narrative) {
            return DuckDBToolResult::error("Narrative engine not initialized");
        }

        // Append event to log first
        auto& store = mind_->store();
        int64_t event_id = store.event_log_append(event);
        if (event_id <= 0) {
            return DuckDBToolResult::error("Failed to append event: " + store.last_error());
        }

        // Then evaluate mode (updates segments)
        WorkMode mode = narrative->evaluate(session_id, event);

        return DuckDBToolResult::ok("Event logged, mode: " + work_mode_to_string(mode), {
            {"session_id", session_id},
            {"kind", kind_str},
            {"mode", work_mode_to_string(mode)}
        });
    }

    DuckDBToolResult tool_narrative_history(const json& params) {
        std::string session_id = get_session_id(params);

        size_t limit = params.value("limit", 20);
        auto& store = mind_->store();
        auto segments = store.segment_history(session_id, limit);

        std::ostringstream ss;
        ss << segments.size() << " segments for session " << session_id << ":\n\n";

        json segment_list = json::array();
        for (const auto& seg : segments) {
            ss << "- " << work_mode_to_string(seg.mode) << " ("
               << std::fixed << std::setprecision(2) << seg.confidence << "): "
               << seg.event_count << " events";
            if (seg.status == "open") ss << " [active]";
            ss << "\n";

            segment_list.push_back({
                {"id", seg.id},
                {"mode", work_mode_to_string(seg.mode)},
                {"confidence", seg.confidence},
                {"event_count", seg.event_count},
                {"started_at", seg.started_at},
                {"ended_at", seg.ended_at},
                {"status", seg.status}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"session_id", session_id},
            {"count", segments.size()},
            {"segments", segment_list}
        });
    }

    DuckDBToolResult tool_anticipation_filter(const json& params) {
        std::string session_id = params.value("session_id", "");
        if (session_id.empty()) {
            return DuckDBToolResult::error("session_id is required");
        }

        size_t max = params.value("max", 2);
        auto& store = mind_->store();

        // First, generate new candidates using the Anticipator
        auto* anticipator = mind_->anticipator();
        if (anticipator) {
            anticipator->generate(session_id);
        }

        // Get pending candidates
        auto candidates = store.candidate_pending(session_id, 10);

        std::vector<AnticipationCandidate> surfaceable;
        for (const auto& c : candidates) {
            if (surfaceable.size() >= max) break;
            if (store.gate_allows(session_id, c.confidence)) {
                // Mark as surfaced before returning
                store.candidate_surface(c.id);
                surfaceable.push_back(c);
            }
        }

        if (surfaceable.empty()) {
            return DuckDBToolResult::ok("No predictions pass the annoyance gate", {
                {"session_id", session_id},
                {"count", 0},
                {"candidates", json::array()}
            });
        }

        std::ostringstream ss;
        ss << surfaceable.size() << " prediction(s) ready to surface:\n\n";

        json cand_list = json::array();
        for (const auto& c : surfaceable) {
            ss << "- [" << anticipation_source_to_string(c.source) << "] "
               << c.prediction << " (conf: "
               << std::fixed << std::setprecision(2) << c.confidence << ")\n";

            cand_list.push_back({
                {"id", c.id},
                {"prediction", c.prediction},
                {"source", anticipation_source_to_string(c.source)},
                {"confidence", c.confidence},
                {"current_mode", c.current_mode},
                {"evidence", c.evidence}
            });
        }

        ss << "\nUse anticipation_record_outcome to record feedback.";

        return DuckDBToolResult::ok(ss.str(), {
            {"session_id", session_id},
            {"count", surfaceable.size()},
            {"candidates", cand_list}
        });
    }

    DuckDBToolResult tool_anticipation_gate_status(const json& params) {
        std::string session_id = params.value("session_id", "");
        if (session_id.empty()) {
            return DuckDBToolResult::error("session_id is required");
        }

        auto& store = mind_->store();
        auto gate = store.gate_get(session_id);

        if (!gate) {
            // Initialize gate if not exists
            store.gate_init(session_id);
            gate = store.gate_get(session_id);
        }

        if (!gate) {
            return DuckDBToolResult::error("Failed to get or create gate state");
        }

        std::ostringstream ss;
        ss << "Annoyance Gate for session " << session_id << ":\n\n";
        ss << "Budget remaining: " << gate->budget_remaining << "/5\n";
        ss << "Confidence floor: " << std::fixed << std::setprecision(2) << gate->confidence_floor << "\n";
        ss << "Cooldown: " << gate->cooldown_ms / 1000 << "s\n";
        ss << "Predictions surfaced: " << gate->predictions_surfaced << "\n";
        ss << "Correct: " << gate->predictions_correct << " / Incorrect: " << gate->predictions_incorrect << "\n";

        float accuracy = gate->predictions_surfaced > 0 ?
            static_cast<float>(gate->predictions_correct) / gate->predictions_surfaced : 0.0f;
        ss << "Accuracy: " << std::fixed << std::setprecision(1) << (accuracy * 100) << "%";

        return DuckDBToolResult::ok(ss.str(), {
            {"session_id", session_id},
            {"budget_remaining", gate->budget_remaining},
            {"confidence_floor", gate->confidence_floor},
            {"cooldown_ms", gate->cooldown_ms},
            {"predictions_surfaced", gate->predictions_surfaced},
            {"predictions_correct", gate->predictions_correct},
            {"predictions_incorrect", gate->predictions_incorrect},
            {"last_surfaced_at", gate->last_surfaced_at}
        });
    }

    DuckDBToolResult tool_anticipation_record_outcome(const json& params) {
        auto [candidate_id, _] = parse_id(params, "candidate_id");
        if (candidate_id <= 0) {
            return DuckDBToolResult::error("candidate_id is required");
        }

        bool correct = params.value("correct", false);
        auto& store = mind_->store();

        // Get candidate to find session
        auto candidate = store.candidate_get(candidate_id);
        if (!candidate) {
            return DuckDBToolResult::error("Candidate not found: " + std::to_string(candidate_id));
        }

        std::string outcome = correct ? "correct" : "incorrect";
        if (!store.candidate_resolve(candidate_id, outcome)) {
            return DuckDBToolResult::error("Failed to resolve candidate");
        }

        if (!store.gate_record_outcome(candidate->session_id, correct)) {
            return DuckDBToolResult::error("Failed to update gate state");
        }

        // Feed calibration system
        std::string domain = "anticipation";
        if (candidate) {
            domain = "anticipation:" + candidate->current_mode;
        }
        mind_->store().calibration_record(domain, correct);

        std::ostringstream ss;
        ss << "Recorded outcome: " << outcome << " for prediction \"" << candidate->prediction << "\"";

        return DuckDBToolResult::ok(ss.str(), {
            {"candidate_id", candidate_id},
            {"outcome", outcome},
            {"prediction", candidate->prediction},
            {"session_id", candidate->session_id}
        });
    }

    // ========================================================================
    // Cross-Session Messaging Tool Implementations
    // ========================================================================

    // Helper: get parent PID from /proc (Linux)
    int64_t get_parent_pid(int64_t pid) {
        std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
        std::ifstream stat_file(stat_path);
        if (!stat_file) return 0;

        std::string line;
        std::getline(stat_file, line);

        // Format: pid (comm) state ppid ...
        // Find closing paren, then parse fields after it
        size_t paren_end = line.rfind(')');
        if (paren_end == std::string::npos) return 0;

        std::istringstream iss(line.substr(paren_end + 2));
        char state;
        int64_t ppid;
        iss >> state >> ppid;
        return ppid;
    }

    // Helper: get session ID from params, env, or PID lookup
    std::string get_session_id(const json& params) {
        std::string session_id = params.value("session_id", "");
        if (!session_id.empty()) {
            return session_id;
        }

        // Try environment variable
        if (const char* env_session = std::getenv("CLAUDE_SESSION_ID")) {
            return env_session;
        }

        // Try PID lookup - walk up process tree to find Claude's PID
        int64_t pid = params.value("pid", 0LL);
        if (pid > 0) {
            // Walk up process tree (max 10 levels to avoid infinite loops)
            for (int depth = 0; depth < 10 && pid > 1; ++depth) {
                std::ostringstream sql;
                sql << "SELECT session_id FROM session_registry WHERE pid = " << pid
                    << " AND status = 'active' LIMIT 1";
                auto result = mind_->store().execute_sql_query(sql.str());
                if (!result.rows.empty() && !result.rows[0].empty()) {
                    return result.rows[0][0];
                }
                // Walk up to parent
                pid = get_parent_pid(pid);
            }
        }

        return "default";
    }

    struct SusMetrics {
        double R = -1.0, P = -1.0, M = -1.0, T = -1.0, D = -1.0;
        double sus = -1.0;
        int64_t n_sessions = 0, n_exposures = 0, n_recalls = 0, n_memories = 0;
        int days = 7;
    };

    SusMetrics compute_sus(int days = 7) {
        SusMetrics m;
        m.days = days;

        // Build cutoff as Unix timestamp (BIGINT)
        auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto cutoff = now_sec - (int64_t)(days * 86400);
        std::string cs = std::to_string(cutoff);

        // n_exposures and n_sessions (from memory_exposed)
        {
            auto r = mind_->store().execute_sql_query(
                "SELECT COUNT(*), COUNT(DISTINCT session_id) FROM memory_exposed WHERE created_at >= " + cs);
            if (r.success && !r.rows.empty() && r.rows[0].size() >= 2) {
                try { m.n_exposures = std::stoll(r.rows[0][0]); } catch (...) {}
                try { m.n_sessions = std::stoll(r.rows[0][1]); } catch (...) {}
            }
        }

        // R: fraction of sessions (in window) that had at least 1 exposure
        {
            auto r = mind_->store().execute_sql_query(
                "SELECT COUNT(DISTINCT session_id) FROM session_registry WHERE started_at >= " + cs);
            int64_t total_sessions = 0;
            if (r.success && !r.rows.empty() && !r.rows[0].empty()) {
                try { total_sessions = std::stoll(r.rows[0][0]); } catch (...) {}
            }
            if (total_sessions > 0)
                m.R = std::min(1.0, (double)m.n_sessions / total_sessions);
        }

        // P: fraction of recall queries that returned >= 1 result
        {
            auto r = mind_->store().execute_sql_query(
                "SELECT COUNT(*), COUNT(CASE WHEN returned_memory_ids != '[]' AND returned_memory_ids != '' THEN 1 END) "
                "FROM memory_recall_query WHERE created_at >= " + cs);
            if (r.success && !r.rows.empty() && r.rows[0].size() >= 2) {
                try { m.n_recalls = std::stoll(r.rows[0][0]); } catch (...) {}
                if (m.n_recalls > 0) {
                    int64_t hits = 0;
                    try { hits = std::stoll(r.rows[0][1]); } catch (...) {}
                    m.P = (double)hits / m.n_recalls;
                }
            }
        }

        // D: fraction of memories accessed in last 30 days
        {
            auto r = mind_->store().execute_sql_query(
                "SELECT COUNT(*), COUNT(CASE WHEN accessed_at >= " + std::to_string(now_sec - 30*86400) +
                " THEN 1 END) FROM memory WHERE confidence > 0.1");
            if (r.success && !r.rows.empty() && r.rows[0].size() >= 2) {
                try { m.n_memories = std::stoll(r.rows[0][0]); } catch (...) {}
                if (m.n_memories > 0) {
                    int64_t accessed = 0;
                    try { accessed = std::stoll(r.rows[0][1]); } catch (...) {}
                    m.D = (double)accessed / m.n_memories;
                }
            }
        }

        // T: average cache hit ratio across sessions in window
        {
            auto r = mind_->store().execute_sql_query(
                "SELECT AVG(cache_hit_ratio) FROM session_token_usage "
                "WHERE created_at >= " + cs + " AND n_messages > 3");
            if (r.success && !r.rows.empty() && !r.rows[0].empty()
                && !r.rows[0][0].empty()) {
                try { m.T = std::stod(r.rows[0][0]); } catch (...) {}
            }
        }

        // M: fraction of correction exposures that did NOT result in a correction detection
        {
            auto r = mind_->store().execute_sql_query(
                "SELECT COUNT(*), COUNT(CASE WHEN correction_detected = false THEN 1 END) "
                "FROM correction_outcome WHERE created_at >= " + cs);
            if (r.success && !r.rows.empty() && r.rows[0].size() >= 2) {
                int64_t total = 0, prevented = 0;
                try { total = std::stoll(r.rows[0][0]); } catch (...) {}
                try { prevented = std::stoll(r.rows[0][1]); } catch (...) {}
                if (total >= 3) {
                    m.M = (double)prevented / total;
                }
            }
        }

        // SUS score: Full weights R=0.25 P=0.20 M=0.30 T=0.10 D=0.15
        if (m.R >= 0 && m.D >= 0) {
            double p_val = (m.P >= 0) ? m.P : 0.5;
            double t_val = (m.T >= 0) ? m.T : 0.5;
            if (m.M >= 0) {
                // Full SUS: R=0.25 P=0.20 M=0.30 T=0.10 D=0.15
                m.sus = 100.0 * std::pow(std::max(m.R, 1e-6), 0.25)
                              * std::pow(std::max(p_val, 1e-6), 0.20)
                              * std::pow(std::max(m.M, 1e-6), 0.30)
                              * std::pow(std::max(t_val, 1e-6), 0.10)
                              * std::pow(std::max(m.D, 1e-6), 0.15);
            } else {
                // Partial (M not yet available): R(0.25) P(0.20) T(0.10) D(0.15) / 0.70
                double avail = 0.70;
                m.sus = 100.0 * std::pow(std::max(m.R, 1e-6), 0.25 / avail)
                              * std::pow(std::max(p_val, 1e-6), 0.20 / avail)
                              * std::pow(std::max(t_val, 1e-6), 0.10 / avail)
                              * std::pow(std::max(m.D, 1e-6), 0.15 / avail);
            }
        }

        return m;
    }

    // Helper: detect current realm
    std::string detect_current_realm() {
        // Priority: CHITTA_REALM env > .cc-soul-realm file > git repo > "brahman"
        if (const char* env_realm = std::getenv("CHITTA_REALM")) {
            return env_realm;
        }

        std::ifstream realm_file(".cc-soul-realm");
        if (realm_file.good()) {
            std::string realm;
            std::getline(realm_file, realm);
            realm.erase(0, realm.find_first_not_of(" \t\n\r"));
            realm.erase(realm.find_last_not_of(" \t\n\r") + 1);
            if (!realm.empty()) return realm;
        }

        std::array<char, 256> buffer;
        FILE* pipe = popen("git rev-parse --show-toplevel 2>/dev/null", "r");
        if (pipe) {
            std::string git_root;
            if (fgets(buffer.data(), buffer.size(), pipe)) {
                git_root = buffer.data();
                if (!git_root.empty() && git_root.back() == '\n') git_root.pop_back();
            }
            pclose(pipe);
            if (!git_root.empty()) {
                size_t last_slash = git_root.rfind('/');
                std::string repo = (last_slash != std::string::npos)
                    ? git_root.substr(last_slash + 1) : git_root;
                return "project:" + repo;
            }
        }

        return "brahman";
    }

    // Helper: get current time in milliseconds
    static int64_t current_time_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Helper: format timestamp for display
    static std::string format_timestamp(int64_t ms) {
        if (ms == 0) return "never";
        time_t seconds = ms / 1000;
        std::tm* tm = std::localtime(&seconds);
        std::ostringstream ss;
        ss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    // Helper: priority to string
    static std::string priority_to_string(int32_t priority) {
        switch (priority) {
            case 0: return "info";
            case 1: return "normal";
            case 2: return "important";
            case 3: return "urgent";
            default: return "unknown";
        }
    }

    DuckDBToolResult tool_msg_send(const json& params) {
        std::string target = params.value("target", "");
        std::string content = params.value("content", "");

        if (target.empty()) return DuckDBToolResult::error("target is required");
        if (content.empty()) return DuckDBToolResult::error("content is required");

        std::string target_type = params.value("target_type", "");
        int32_t priority = params.value("priority", 1);
        std::string content_type = params.value("content_type", "text");
        int32_t ttl = params.value("ttl", 3600);
        std::string session_id = get_session_id(params);

        // Auto-detect target_type
        if (target_type.empty()) {
            if (target == "*") {
                target_type = "global";
            } else if (target.find("project:") == 0 || target == "brahman") {
                target_type = "realm";
            } else {
                target_type = "direct";
            }
        }

        SessionMessage msg;
        msg.sender_session = session_id;
        msg.sender_realm = detect_current_realm();
        msg.target_type = target_type;
        msg.target_id = target;
        msg.priority = priority;
        msg.content = content;
        msg.content_type = content_type;
        msg.expires_at = ttl > 0 ? current_time_ms() + (static_cast<int64_t>(ttl) * 1000) : 0;
        msg.created_at = current_time_ms();

        int64_t msg_id = mind_->store().msg_send(msg);

        if (msg_id <= 0) {
            return DuckDBToolResult::error("Failed to send message: " + mind_->store().last_error());
        }

        std::ostringstream ss;
        ss << "Message sent (" << target_type << " to " << target << ")";

        return DuckDBToolResult::ok(ss.str(), {
            {"message_id", msg_id},
            {"target_type", target_type},
            {"target", target},
            {"priority", priority}
        });
    }

    DuckDBToolResult tool_msg_inbox(const json& params) {
        std::string session_id = get_session_id(params);
        size_t limit = params.value("limit", 20);
        int32_t min_priority = params.value("min_priority", 0);
        bool auto_ack = params.value("auto_ack", false);

        auto items = mind_->store().msg_inbox(session_id, limit, min_priority);

        if (items.empty()) {
            return DuckDBToolResult::ok("No unread messages", {
                {"session_id", session_id},
                {"count", 0},
                {"messages", json::array()}
            });
        }

        std::ostringstream ss;
        ss << items.size() << " unread message(s):\n\n";

        json msg_list = json::array();
        for (const auto& item : items) {
            const auto& msg = item.message;
            ss << "[" << priority_to_string(msg.priority) << "] "
               << "From: " << msg.sender_session << " (" << msg.sender_realm << ")\n"
               << msg.content << "\n\n";

            msg_list.push_back({
                {"id", msg.id},
                {"sender_session", msg.sender_session},
                {"sender_realm", msg.sender_realm},
                {"priority", msg.priority},
                {"content", msg.content},
                {"content_type", msg.content_type},
                {"created_at", msg.created_at},
                {"is_read", item.is_read}
            });

            if (auto_ack) {
                mind_->store().msg_ack(msg.id, session_id);
            }
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"session_id", session_id},
            {"count", items.size()},
            {"messages", msg_list}
        });
    }

    DuckDBToolResult tool_msg_ack(const json& params) {
        auto [message_id, _] = parse_id(params, "message_id");
        if (message_id <= 0) {
            return DuckDBToolResult::error("message_id is required");
        }

        std::string session_id = get_session_id(params);

        if (!mind_->store().msg_ack(message_id, session_id)) {
            return DuckDBToolResult::error("Failed to acknowledge message");
        }

        return DuckDBToolResult::ok("Message acknowledged", {
            {"message_id", message_id},
            {"session_id", session_id}
        });
    }

    DuckDBToolResult tool_msg_ack_all(const json& params) {
        std::string session_id = get_session_id(params);

        if (!mind_->store().msg_ack_all(session_id)) {
            return DuckDBToolResult::error("Failed to acknowledge messages");
        }

        return DuckDBToolResult::ok("All messages acknowledged", {
            {"session_id", session_id}
        });
    }

    DuckDBToolResult tool_msg_history(const json& params) {
        std::string session_id = get_session_id(params);
        size_t limit = params.value("limit", 30);

        auto messages = mind_->store().msg_history(session_id, limit);

        if (messages.empty()) {
            return DuckDBToolResult::ok("No message history", {
                {"session_id", session_id},
                {"count", 0},
                {"messages", json::array()}
            });
        }

        std::ostringstream ss;
        ss << "Message history (" << messages.size() << " messages):\n\n";

        json msg_list = json::array();
        for (const auto& msg : messages) {
            ss << "[" << format_timestamp(msg.created_at) << "] "
               << msg.sender_session << " -> " << msg.target_id << ": "
               << msg.content.substr(0, 80)
               << (msg.content.size() > 80 ? "..." : "") << "\n";

            msg_list.push_back({
                {"id", msg.id},
                {"sender_session", msg.sender_session},
                {"sender_realm", msg.sender_realm},
                {"target_type", msg.target_type},
                {"target_id", msg.target_id},
                {"priority", msg.priority},
                {"content", msg.content},
                {"content_type", msg.content_type},
                {"created_at", msg.created_at},
                {"expires_at", msg.expires_at}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"session_id", session_id},
            {"count", messages.size()},
            {"messages", msg_list}
        });
    }

    DuckDBToolResult tool_session_register(const json& params) {
        std::string session_id = get_session_id(params);
        std::string realm = params.value("realm", "");
        std::string transcript_path = params.value("transcript_path", "");
        std::string project_dir = params.value("project_dir", "");
        std::string metadata = params.value("metadata", "{}");

        if (realm.empty()) {
            realm = detect_current_realm();
        }

        // Use passed PID or fall back to caller's PID
        int32_t pid = params.contains("pid") ? params["pid"].get<int32_t>() : static_cast<int32_t>(getpid());

        if (!mind_->store().session_register(session_id, realm, pid, transcript_path, project_dir, metadata)) {
            return DuckDBToolResult::error("Failed to register session: " + mind_->store().last_error());
        }

        return DuckDBToolResult::ok("Session registered", {
            {"session_id", session_id},
            {"realm", realm},
            {"pid", pid},
            {"transcript_path", transcript_path}
        });
    }

    DuckDBToolResult tool_session_heartbeat(const json& params) {
        std::string session_id = get_session_id(params);
        std::string metadata = params.value("metadata", "");

        if (!mind_->store().session_heartbeat(session_id, metadata)) {
            return DuckDBToolResult::error("Failed to send heartbeat");
        }

        return DuckDBToolResult::ok("Heartbeat sent", {
            {"session_id", session_id}
        });
    }

    DuckDBToolResult tool_session_list(const json& params) {
        std::string realm = params.value("realm", "");
        std::string status = params.value("status", "active");

        auto sessions = mind_->store().session_list(realm, status);

        if (sessions.empty()) {
            return DuckDBToolResult::ok("No sessions found", {
                {"count", 0},
                {"realm", realm},
                {"status", status},
                {"sessions", json::array()}
            });
        }

        std::ostringstream ss;
        ss << sessions.size() << " session(s):\n\n";

        json session_list = json::array();
        for (const auto& s : sessions) {
            ss << "- " << s.session_id << " [" << s.status << "] "
               << s.realm << " (pid " << s.pid << ")\n";

            session_list.push_back({
                {"session_id", s.session_id},
                {"realm", s.realm},
                {"pid", s.pid},
                {"status", s.status},
                {"started_at", s.started_at},
                {"last_heartbeat", s.last_heartbeat},
                {"metadata", s.metadata}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"count", sessions.size()},
            {"realm", realm},
            {"status", status},
            {"sessions", session_list}
        });
    }

    DuckDBToolResult tool_session_deregister(const json& params) {
        std::string session_id = get_session_id(params);

        if (!mind_->store().session_deregister(session_id)) {
            return DuckDBToolResult::error("Failed to deregister session");
        }

        return DuckDBToolResult::ok("Session deregistered", {
            {"session_id", session_id}
        });
    }

    DuckDBToolResult tool_session_sync(const json& params) {
        std::string projects_dir = params.value("projects_dir", "");

        auto result = mind_->store().session_sync(projects_dir);

        std::ostringstream msg;
        msg << "Session sync complete: "
            << result.discovered << " discovered, "
            << result.updated << " updated, "
            << result.marked_dead << " marked dead";

        return DuckDBToolResult::ok(msg.str(), {
            {"discovered", result.discovered},
            {"updated", result.updated},
            {"marked_dead", result.marked_dead}
        });
    }

    DuckDBToolResult tool_read_transcript(const json& params) {
        std::string path = params.value("path", "");
        std::string session_id = params.value("session_id", "");
        int start_turn = params.value("start_turn", 0);
        size_t limit = params.value("limit", 20);
        size_t max_chars = params.value("max_chars_per_turn", 500);
        std::string role_filter = params.value("role_filter", "");
        std::string keyword = params.value("keyword", "");
        bool metadata_only = params.value("metadata_only", false);

        // Find transcript path from session_id if not provided
        if (path.empty() && !session_id.empty()) {
            // Try common locations using glob
            std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
            std::string pattern = home + "/.claude/projects/*/" + session_id + ".jsonl";

            glob_t glob_result;
            if (glob(pattern.c_str(), GLOB_TILDE, nullptr, &glob_result) == 0) {
                if (glob_result.gl_pathc > 0) {
                    path = glob_result.gl_pathv[0];
                }
                globfree(&glob_result);
            }
        }

        if (path.empty()) {
            return DuckDBToolResult::error("No transcript path provided and couldn't find session");
        }

        // Parse transcript
        TranscriptParser parser;
        TranscriptParseOptions opts;
        opts.filter_system_reminders = true;
        opts.include_thinking = false;  // Skip thinking blocks for brevity

        int64_t last_line = 0;
        auto all_turns = parser.parse(path, opts, &last_line);

        if (all_turns.empty()) {
            return DuckDBToolResult::error("Failed to parse transcript: " + parser.last_error());
        }

        // Calculate metadata
        size_t total_chars = 0;
        for (const auto& t : all_turns) total_chars += t.content.size();

        json result;
        result["path"] = path;
        result["total_turns"] = all_turns.size();
        result["total_chars"] = total_chars;
        result["last_line"] = last_line;

        if (metadata_only) {
            std::ostringstream ss;
            ss << "Transcript: " << path << "\n"
               << "Total turns: " << all_turns.size() << "\n"
               << "Total chars: " << total_chars << "\n"
               << "Lines: " << last_line;
            return DuckDBToolResult::ok(ss.str(), result);
        }

        // Apply filters and pagination
        std::vector<ConversationTurn> filtered;
        for (size_t i = 0; i < all_turns.size(); i++) {
            const auto& t = all_turns[i];

            // Role filter
            if (!role_filter.empty() && t.role != role_filter) continue;

            // Keyword filter
            if (!keyword.empty()) {
                if (t.content.find(keyword) == std::string::npos) continue;
            }

            filtered.push_back(t);
        }

        result["filtered_turns"] = filtered.size();

        // Paginate
        json turns_arr = json::array();
        size_t output_chars = 0;
        const size_t max_output_chars = 30000;  // Prevent huge responses

        for (size_t i = start_turn; i < filtered.size() && turns_arr.size() < limit; i++) {
            const auto& t = filtered[i];

            std::string content = t.content;
            if (max_chars > 0 && content.size() > max_chars) {
                content = content.substr(0, max_chars) + "...";
            }

            if (output_chars + content.size() > max_output_chars) {
                turns_arr.push_back({
                    {"role", "system"},
                    {"content", "[output truncated - use start_turn=" + std::to_string(i) + " to continue]"},
                    {"turn_index", -1}
                });
                break;
            }

            output_chars += content.size();
            turns_arr.push_back({
                {"role", t.role},
                {"content", content},
                {"turn_index", t.turn_index},
                {"line_number", t.line_number}
            });
        }

        result["turns"] = turns_arr;
        result["returned"] = turns_arr.size();
        result["start_turn"] = start_turn;

        std::ostringstream ss;
        ss << "Transcript: " << path << "\n"
           << "Total: " << all_turns.size() << " turns";
        if (!role_filter.empty() || !keyword.empty()) {
            ss << " (filtered: " << filtered.size() << ")";
        }
        ss << "\nShowing turns " << start_turn << "-" << (start_turn + turns_arr.size() - 1) << ":\n\n";

        for (const auto& t : turns_arr) {
            ss << "[" << t["role"].get<std::string>() << "] ";
            std::string content = t["content"].get<std::string>();
            if (content.size() > 100) {
                ss << content.substr(0, 100) << "...\n";
            } else {
                ss << content << "\n";
            }
        }

        return DuckDBToolResult::ok(ss.str(), result);
    }

    // ========================================================================
    // Conversational Memory System Tool Implementations
    // ========================================================================

    DuckDBToolResult tool_get_turns(const json& params) {
        std::string session_id = params.value("session_id", "");
        int start_index = params.value("start_index", 0);
        size_t limit = params.value("limit", 50);

        auto turns = mind_->store().get_conversation_turns(session_id, start_index, limit);

        json result;
        result["turns"] = json::array();
        result["count"] = turns.size();

        for (const auto& turn : turns) {
            json t;
            t["id"] = turn.id;
            t["session_id"] = turn.session_id;
            t["role"] = turn.role;
            t["turn_index"] = turn.turn_index;
            t["content"] = turn.content.substr(0, 500);
            t["tools_used"] = turn.tools_used;
            t["files_touched"] = turn.files_touched;
            t["has_error"] = turn.has_error;
            t["created_at"] = turn.created_at;
            result["turns"].push_back(t);
        }

        std::ostringstream msg;
        msg << "Found " << turns.size() << " conversation turn(s)";

        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_create_episode(const json& params) {
        std::string session_id = params.value("session_id", "");
        std::string title = params.value("title", "");
        int start_turn = params.value("start_turn", 0);
        int end_turn = params.value("end_turn", 0);
        std::string episode_type = params.value("episode_type", "distillation");
        std::string realm = params.value("realm", "brahman");

        if (session_id.empty() || title.empty()) {
            return DuckDBToolResult::error("session_id and title are required");
        }

        int64_t episode_id = mind_->store().create_dialogue_episode(
            session_id, title, start_turn, episode_type, realm
        );

        if (episode_id < 0) {
            return DuckDBToolResult::error("Failed to create episode");
        }

        // If end_turn provided, set it directly (close_dialogue_episode would override)
        if (end_turn > 0) {
            std::ostringstream sql;
            sql << "UPDATE dialogue_episode SET end_turn = " << end_turn
                << ", turn_count = " << (end_turn - start_turn + 1)
                << ", outcome = 'completed' WHERE id = " << episode_id;
            mind_->store().execute_raw(sql.str());
        }

        std::ostringstream msg;
        msg << "Created episode " << episode_id << ": " << title
            << " (turns " << start_turn << "-" << (end_turn > 0 ? end_turn : start_turn) << ")";

        return DuckDBToolResult::ok(msg.str(), {
            {"episode_id", episode_id},
            {"session_id", session_id},
            {"title", title},
            {"start_turn", start_turn},
            {"end_turn", end_turn}
        });
    }

    DuckDBToolResult tool_query_claims(const json& params) {
        std::string subject = params.value("subject", "");
        std::string predicate = params.value("predicate", "");
        std::string scope = params.value("scope", "");
        bool active_only = params.value("active_only", true);
        size_t limit = params.value("limit", 20);

        auto claims = mind_->store().query_claims(subject, predicate, scope, active_only, limit);

        json result;
        result["claims"] = json::array();
        result["count"] = claims.size();

        for (const auto& claim : claims) {
            json c;
            c["id"] = claim.id;
            c["subject"] = claim.subject;
            c["predicate"] = claim.predicate;
            c["object"] = claim.object_norm;
            c["scope"] = claim.scope_key;
            c["polarity"] = claim.polarity;
            c["confidence"] = claim.confidence;
            c["support_count"] = claim.support_count;
            c["source"] = claim.source_class;
            c["created_at"] = claim.created_at;
            result["claims"].push_back(c);
        }

        std::ostringstream msg;
        msg << "Found " << claims.size() << " claim(s)";

        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_get_policies(const json& params) {
        std::string scope = params.value("scope", "");
        std::string policy_type = params.value("type", "");
        size_t limit = params.value("limit", 30);

        auto policies = mind_->store().get_active_policies(scope, policy_type, limit);

        json result;
        result["policies"] = json::array();
        result["count"] = policies.size();

        for (const auto& policy : policies) {
            json p;
            p["id"] = policy.id;
            p["type"] = policy.policy_type;
            p["content"] = policy.content.substr(0, 300);
            p["scope"] = policy.scope_key;
            p["state"] = policy.state;
            p["confidence"] = policy.confidence;
            p["support_count"] = policy.support_count;
            p["session_count"] = policy.session_count;
            p["created_at"] = policy.created_at;
            result["policies"].push_back(p);
        }

        std::ostringstream msg;
        msg << "Found " << policies.size() << " active policy/policies";

        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_expand_query(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) return DuckDBToolResult::error("query required");
        auto eq = expand_query(query);
        return DuckDBToolResult::ok("Query expanded into lex/vec/hyde variants", {
            {"lex", eq.lex}, {"vec", eq.vec}, {"hyde", eq.hyde}
        });
    }

    bool try_bm25_short_circuit(
        const std::string& query,
        size_t limit,
        const std::string& realm,
        bool include_global,
        std::vector<MemoryResult>& out_results
    ) {
        if (query.empty()) return false;
        auto bm25_results = mind_->store().bm25_search_memory(query, limit * 2, realm, include_global);
        if (bm25_results.size() < 2) return false;
        float top = bm25_results[0].second;
        float second = bm25_results[1].second;
        if (top <= 2.0f) return false;
        float gap = 1.0f - (second / top);
        if (gap <= 0.15f) return false;
        for (size_t i = 0; i < std::min(limit, bm25_results.size()); ++i) {
            auto mem = mind_->store().get_memory(bm25_results[i].first);
            if (mem) {
                mem->similarity = bm25_results[i].second / top;
                out_results.push_back(*mem);
            }
        }
        return true;
    }

    DuckDBToolResult tool_hybrid_recall(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) {
            return DuckDBToolResult::error("Query is required");
        }

        size_t limit = params.value("limit", 10);
        std::string tag = params.value("tag", "");
        std::string realm = params.value("realm", "");

        // Feature 2: BM25 short-circuit for strong lexical matches
        std::vector<MemoryResult> sc_results;
        bool short_circuited = try_bm25_short_circuit(query, limit, realm, true, sc_results);
        if (short_circuited && !sc_results.empty()) {
            json result;
            result["short_circuit"] = true;
            result["memories"] = json::array();
            size_t count = 0;
            for (const auto& r : sc_results) {
                json mem;
                mem["id"] = r.id;
                mem["kind"] = r.kind;
                mem["content"] = r.content.substr(0, 300);
                mem["similarity"] = r.similarity;
                mem["confidence"] = r.confidence;
                mem["realm"] = r.realm;
                mem["created_at"] = r.created_at;
                result["memories"].push_back(mem);
                count++;
                if (count >= limit) break;
            }
            result["count"] = count;

            // SUS: log recall query
            try {
                std::string _sus_sid = get_session_id(params);
                if (!_sus_sid.empty() && _sus_sid != "default") {
                    json _sus_ids = json::array();
                    json _sus_scores = json::array();
                    for (const auto& mem : result["memories"]) {
                        _sus_ids.push_back(mem["id"]);
                        _sus_scores.push_back(mem.value("similarity", 0.0));
                    }
                    mind_->store().log_recall_query(
                        _sus_sid, 0, "hybrid_recall", query,
                        _sus_ids.dump(), _sus_scores.dump());
                }
            } catch (...) {}

            return DuckDBToolResult::ok(
                "Found " + std::to_string(count) + " memories (BM25 short-circuit)", result);
        }

        // Feature 1: Typed query expansion
        auto eq = expand_query(query);

        // Get embedding for vector-optimized query
        if (!mind_->embedder_ready()) {
            return DuckDBToolResult::error("Embedder not ready");
        }
        auto embedding = mind_->embedder().embed_query(eq.vec).data;
        if (embedding.empty()) {
            return DuckDBToolResult::error("Failed to generate query embedding");
        }

        std::vector<chitta::MemoryResult> results;

        if (!tag.empty()) {
            // Use tag-filtered recall for proper scoping
            results = mind_->store().recall_with_tag(embedding, tag, limit);
        } else {
            // Build config from params
            DuckDBStore::HybridRecallConfig config;
            if (params.contains("vector_weight")) config.vector_weight = params["vector_weight"].get<float>();
            if (params.contains("bm25_weight")) config.bm25_weight = params["bm25_weight"].get<float>();
            if (params.contains("graph_weight")) config.graph_weight = params["graph_weight"].get<float>();
            if (params.contains("recency_weight")) config.recency_weight = params["recency_weight"].get<float>();

            // Pass lex and hyde variants to store for improved BM25 matching
            results = mind_->store().hybrid_recall(embedding, query, limit, realm, true, config, eq.lex, eq.hyde);
        }

        json result;
        result["memories"] = json::array();
        size_t count = 0;

        for (const auto& r : results) {
            json mem;
            mem["id"] = r.id;
            mem["kind"] = r.kind;
            mem["content"] = r.content.substr(0, 300);
            mem["similarity"] = r.similarity;
            mem["confidence"] = r.confidence;
            mem["realm"] = r.realm;
            mem["created_at"] = r.created_at;
            result["memories"].push_back(mem);
            count++;
            if (count >= limit) break;
        }
        result["count"] = count;

        // Only include config when not using tag-filtered recall
        if (tag.empty()) {
            json cfg;
            cfg["note"] = "Config only applies to hybrid mode (no tag filter)";
            result["config"] = cfg;
        }

        std::ostringstream msg;
        msg << "Found " << results.size() << " memory/memories via hybrid retrieval";

        // SUS: log recall query
        try {
            std::string _sus_sid = get_session_id(params);
            if (!_sus_sid.empty() && _sus_sid != "default") {
                json _sus_ids = json::array();
                json _sus_scores = json::array();
                for (const auto& mem : result["memories"]) {
                    _sus_ids.push_back(mem["id"]);
                    _sus_scores.push_back(mem.value("similarity", 0.0));
                }
                mind_->store().log_recall_query(
                    _sus_sid, 0, "hybrid_recall", query,
                    _sus_ids.dump(), _sus_scores.dump());
            }
        } catch (...) {}

        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_get_entities(const json& params) {
        std::string entity_type = params.value("type", "");
        size_t limit = params.value("limit", 20);

        auto entities = mind_->store().get_top_entities(entity_type, limit);

        json result;
        result["entities"] = json::array();
        result["count"] = entities.size();

        for (const auto& e : entities) {
            json ent;
            ent["id"] = e.id;
            ent["name"] = e.name;
            ent["display_name"] = e.display_name;
            ent["type"] = e.entity_type;
            ent["mention_count"] = e.mention_count;
            ent["salience"] = e.salience_score;
            ent["last_mentioned"] = e.last_mentioned;
            result["entities"].push_back(ent);
        }

        std::ostringstream msg;
        msg << "Found " << entities.size() << " entity/entities";

        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_get_relationship_events(const json& params) {
        std::string event_type = params.value("event_type", "");
        std::string session_id = params.value("session_id", "");
        size_t limit = params.value("limit", 20);

        auto events = mind_->store().get_relationship_events(event_type, session_id, limit);

        json result;
        result["events"] = json::array();
        result["count"] = events.size();

        for (const auto& e : events) {
            json evt;
            evt["id"] = e.id;
            evt["session_id"] = e.session_id;
            evt["event_type"] = e.event_type;
            evt["content"] = e.content.substr(0, 300);
            evt["context"] = e.context;
            evt["resolved"] = e.resolved;
            evt["created_at"] = e.created_at;
            result["events"].push_back(evt);
        }

        std::ostringstream msg;
        msg << "Found " << events.size() << " relationship event(s)";

        return DuckDBToolResult::ok(msg.str(), result);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Sadhana Tool Handlers
    // ═══════════════════════════════════════════════════════════════════════

    DuckDBToolResult tool_sadhana_start(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        std::string goal = params.value("goal", "");
        if (goal.empty()) {
            return DuckDBToolResult::error("Goal is required");
        }

        std::string provider = params.value("brain_provider", "");
        std::string model = params.value("brain_model", "");
        int interval = params.value("interval_seconds", 0);
        int max_turns = params.value("max_turns", 0);
        std::string realm = params.value("realm", "brahman");

        int64_t id = sadhana_manager_->create(goal, provider, model, interval, realm, json(), max_turns);
        if (id == 0) {
            return DuckDBToolResult::error("Failed to create sadhana");
        }

        // Auto-start the sadhana
        if (!sadhana_manager_->start(id)) {
            return DuckDBToolResult::error("Created sadhana " + std::to_string(id) + " but failed to start");
        }

        json result;
        result["id"] = id;
        result["state"] = "running";
        result["goal"] = goal.substr(0, 100);

        return DuckDBToolResult::ok("Started sadhana " + std::to_string(id), result);
    }

    DuckDBToolResult tool_sadhana_pause(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) {
            return DuckDBToolResult::error("Invalid sadhana ID");
        }

        if (!sadhana_manager_->pause(id)) {
            return DuckDBToolResult::error("Failed to pause sadhana " + std::to_string(id));
        }

        json result;
        result["id"] = id;
        result["state"] = "paused";

        return DuckDBToolResult::ok("Paused sadhana " + std::to_string(id), result);
    }

    DuckDBToolResult tool_sadhana_resume(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) {
            return DuckDBToolResult::error("Invalid sadhana ID");
        }

        if (!sadhana_manager_->resume(id)) {
            return DuckDBToolResult::error("Failed to resume sadhana " + std::to_string(id));
        }

        json result;
        result["id"] = id;
        result["state"] = "running";

        return DuckDBToolResult::ok("Resumed sadhana " + std::to_string(id), result);
    }

    DuckDBToolResult tool_sadhana_stop(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) {
            return DuckDBToolResult::error("Invalid sadhana ID");
        }

        bool success = params.value("success", true);
        std::string reason = params.value("reason", "");

        if (!sadhana_manager_->stop(id, success, reason)) {
            return DuckDBToolResult::error("Failed to stop sadhana " + std::to_string(id));
        }

        json result;
        result["id"] = id;
        result["state"] = success ? "done" : "failed";
        result["reason"] = reason;

        return DuckDBToolResult::ok("Stopped sadhana " + std::to_string(id), result);
    }

    DuckDBToolResult tool_sadhana_status(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) {
            return DuckDBToolResult::error("Invalid sadhana ID");
        }

        auto opt = sadhana_manager_->get(id);
        if (!opt) {
            return DuckDBToolResult::error("Sadhana " + std::to_string(id) + " not found");
        }

        size_t history_limit = params.value("history_limit", 20);
        auto history = sadhana_manager_->get_history(id, history_limit);

        json result;
        result["id"] = opt->id;
        result["goal"] = opt->goal;
        result["state"] = sadhana_state_to_string(opt->state);
        result["brain_provider"] = opt->brain_provider;
        result["brain_model"] = opt->brain_model;
        result["iterations"] = opt->iterations;
        result["brain_calls"] = opt->brain_calls;
        result["cost_usd"] = opt->cost_usd;
        result["interval_seconds"] = opt->interval_seconds;
        result["max_turns"] = opt->max_turns;
        result["realm"] = opt->realm;
        result["created_at"] = opt->created_at;
        result["updated_at"] = opt->updated_at;
        result["last_sense"] = opt->last_sense;
        result["last_action"] = opt->last_action;
        result["last_result"] = opt->last_result;
        result["history"] = history;

        std::ostringstream msg;
        msg << "Sadhana " << id << " [" << sadhana_state_to_string(opt->state) << "] "
            << opt->iterations << " iterations";

        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_sadhana_list(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        std::string state = params.value("state", "");
        std::string realm = params.value("realm", "");
        size_t limit = params.value("limit", 50);

        auto sadhanas = sadhana_manager_->list(state, realm, limit);

        json result;
        result["sadhanas"] = json::array();
        result["count"] = sadhanas.size();

        for (const auto& s : sadhanas) {
            json item;
            item["id"] = s.id;
            item["goal"] = s.goal.substr(0, 100);
            item["state"] = sadhana_state_to_string(s.state);
            item["brain_model"] = s.brain_model;
            item["iterations"] = s.iterations;
            item["interval_seconds"] = s.interval_seconds;
            item["realm"] = s.realm;
            item["created_at"] = s.created_at;
            result["sadhanas"].push_back(item);
        }

        std::ostringstream msg;
        msg << "Found " << sadhanas.size() << " sadhana(s)";

        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_sadhana_set_model(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) {
            return DuckDBToolResult::error("Invalid sadhana ID");
        }

        std::string model = params.value("model", "");
        if (model.empty()) {
            return DuckDBToolResult::error("Model is required");
        }

        if (!sadhana_manager_->set_model(id, model)) {
            return DuckDBToolResult::error("Failed to set model for sadhana " + std::to_string(id));
        }

        json result;
        result["id"] = id;
        result["model"] = model;

        return DuckDBToolResult::ok("Set model to " + model + " for sadhana " + std::to_string(id), result);
    }

    DuckDBToolResult tool_sadhana_set_goal(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) {
            return DuckDBToolResult::error("Invalid sadhana ID");
        }

        std::string goal = params.value("goal", "");
        if (goal.empty()) {
            return DuckDBToolResult::error("Goal is required");
        }

        if (!sadhana_manager_->set_goal(id, goal)) {
            return DuckDBToolResult::error("Failed to set goal for sadhana " + std::to_string(id));
        }

        json result;
        result["id"] = id;
        result["goal"] = goal;

        return DuckDBToolResult::ok("Updated goal for sadhana " + std::to_string(id), result);
    }

    DuckDBToolResult tool_sadhana_set_interval(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) {
            return DuckDBToolResult::error("Invalid sadhana ID");
        }

        int interval = params.value("interval", 0);
        if (interval <= 0) {
            return DuckDBToolResult::error("Interval must be positive");
        }

        if (!sadhana_manager_->set_interval(id, interval)) {
            return DuckDBToolResult::error("Failed to set interval for sadhana " + std::to_string(id));
        }

        json result;
        result["id"] = id;
        result["interval"] = interval;

        return DuckDBToolResult::ok("Set interval to " + std::to_string(interval) + "s for sadhana " + std::to_string(id), result);
    }

    DuckDBToolResult tool_sadhana_set_max_turns(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) {
            return DuckDBToolResult::error("Invalid sadhana ID");
        }

        int max_turns = params.value("max_turns", -1);
        if (max_turns < 0) {
            return DuckDBToolResult::error("max_turns must be >= 0 (0 = use global default)");
        }

        if (!sadhana_manager_->set_max_turns(id, max_turns)) {
            return DuckDBToolResult::error("Failed to set max_turns for sadhana " + std::to_string(id));
        }

        json result;
        result["id"] = id;
        result["max_turns"] = max_turns;

        std::string msg = max_turns == 0
            ? "Reset max_turns to global default for sadhana " + std::to_string(id)
            : "Set max_turns to " + std::to_string(max_turns) + " for sadhana " + std::to_string(id);
        return DuckDBToolResult::ok(msg, result);
    }

    DuckDBToolResult tool_sadhana_checkpoint(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }

        auto [id, id_str] = parse_id(params, "id");
        if (id == 0) {
            return DuckDBToolResult::error("Invalid sadhana ID");
        }

        std::string status = params.value("status", "progressed");
        std::string summary = params.value("summary", "");

        if (summary.empty()) {
            return DuckDBToolResult::error("Summary is required");
        }

        if (!sadhana_manager_->checkpoint(id, status, summary)) {
            return DuckDBToolResult::error("Checkpoint failed for sadhana " + std::to_string(id));
        }

        json result;
        result["id"] = id;
        result["status"] = status;
        result["summary"] = summary;

        return DuckDBToolResult::ok("Checkpoint [" + status + "] for sadhana " + std::to_string(id), result);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Context Repository Tool Handlers (Letta-inspired)
    // ═══════════════════════════════════════════════════════════════════════

    DuckDBToolResult tool_memory_history(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        int32_t limit = params.value("limit", 20);
        auto history = mind_->store().get_history(id, limit);

        if (history.empty()) {
            return DuckDBToolResult::ok("No version history for memory #" + std::to_string(id),
                                       {{"memory_id", id}, {"count", 0}, {"history", json::array()}});
        }

        std::ostringstream ss;
        ss << "Version history for memory #" << id << " (" << history.size() << " versions)\n";
        ss << std::string(60, '-') << "\n";

        json history_json = json::array();
        for (const auto& entry : history) {
            ss << "v" << entry.version << " [" << entry.operation << "] ";
            if (!entry.commit_message.empty()) {
                ss << entry.commit_message.substr(0, 50);
            }
            ss << "\n";

            history_json.push_back({
                {"version", entry.version},
                {"operation", entry.operation},
                {"content_before", entry.content_before.substr(0, 200)},
                {"content_after", entry.content_after.substr(0, 200)},
                {"confidence_before", entry.confidence_before},
                {"confidence_after", entry.confidence_after},
                {"commit_message", entry.commit_message},
                {"session_id", entry.session_id},
                {"tool_name", entry.tool_name},
                {"created_at", entry.created_at}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"memory_id", id},
            {"count", history.size()},
            {"history", history_json}
        });
    }

    DuckDBToolResult tool_memory_revert(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        int32_t version = params.value("version", 0);
        if (version <= 0) {
            return DuckDBToolResult::error("version is required");
        }

        std::string reason = params.value("reason", "");

        if (!mind_->store().revert_to_version(id, version, reason)) {
            return DuckDBToolResult::error("Failed to revert memory #" + std::to_string(id) +
                                          " to version " + std::to_string(version));
        }

        return DuckDBToolResult::ok(
            "Reverted memory #" + std::to_string(id) + " to version " + std::to_string(version),
            {{"memory_id", id}, {"reverted_to_version", version}, {"reason", reason}}
        );
    }

    DuckDBToolResult tool_pin_memory(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        std::string reason = params.value("reason", "important");

        if (!mind_->store().pin_memory(id, reason)) {
            return DuckDBToolResult::error("Failed to pin memory #" + std::to_string(id));
        }

        return DuckDBToolResult::ok(
            "Pinned memory #" + std::to_string(id) + " (" + reason + ")",
            {{"memory_id", id}, {"pinned", true}, {"reason", reason}}
        );
    }

    DuckDBToolResult tool_unpin_memory(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        if (!mind_->store().unpin_memory(id)) {
            return DuckDBToolResult::error("Failed to unpin memory #" + std::to_string(id));
        }

        return DuckDBToolResult::ok(
            "Unpinned memory #" + std::to_string(id),
            {{"memory_id", id}, {"pinned", false}}
        );
    }

    DuckDBToolResult tool_list_pinned(const json& params) {
        std::string realm = params.value("realm", "");
        size_t limit = params.value("limit", 50);

        auto pinned = mind_->store().list_pinned(realm, limit);

        std::ostringstream ss;
        ss << "Pinned Memories";
        if (!realm.empty()) ss << " (realm: " << realm << ")";
        ss << "\n" << std::string(40, '-') << "\n";

        json pinned_json = json::array();
        for (const auto& mem : pinned) {
            ss << "• #" << mem.id << " [" << mem.kind << "] "
               << mem.content.substr(0, 60) << "...\n";

            pinned_json.push_back({
                {"id", mem.id},
                {"content", mem.content.substr(0, 200)},
                {"kind", mem.kind},
                {"confidence", mem.confidence},
                {"realm", mem.realm}
            });
        }

        if (pinned.empty()) {
            ss << "No pinned memories.\n";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"count", pinned.size()},
            {"pinned", pinned_json}
        });
    }

    DuckDBToolResult tool_memory_lock(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        std::string holder_id = params.value("holder_id", "");
        if (holder_id.empty()) {
            return DuckDBToolResult::error("holder_id is required");
        }

        std::string holder_type = params.value("holder_type", "session");
        int64_t duration = params.value("duration", 300);  // 5 min default

        if (!mind_->store().acquire_lock(id, holder_id, holder_type, "exclusive", duration)) {
            auto existing = mind_->store().get_lock(id);
            if (existing) {
                return DuckDBToolResult::error(
                    "Memory #" + std::to_string(id) + " is locked by " +
                    existing->holder_id + " (" + existing->holder_type + ")"
                );
            }
            return DuckDBToolResult::error("Failed to acquire lock on memory #" + std::to_string(id));
        }

        return DuckDBToolResult::ok(
            "Acquired lock on memory #" + std::to_string(id) + " for " + std::to_string(duration) + "s",
            {{"memory_id", id}, {"holder_id", holder_id}, {"duration_seconds", duration}, {"locked", true}}
        );
    }

    DuckDBToolResult tool_memory_unlock(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        std::string holder_id = params.value("holder_id", "");
        if (holder_id.empty()) {
            return DuckDBToolResult::error("holder_id is required");
        }

        if (!mind_->store().release_lock(id, holder_id)) {
            return DuckDBToolResult::error("Failed to release lock (not held by " + holder_id + "?)");
        }

        return DuckDBToolResult::ok(
            "Released lock on memory #" + std::to_string(id),
            {{"memory_id", id}, {"locked", false}}
        );
    }

    DuckDBToolResult tool_memory_lock_status(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        auto lock = mind_->store().get_lock(id);
        if (!lock) {
            return DuckDBToolResult::ok(
                "Memory #" + std::to_string(id) + " is not locked",
                {{"memory_id", id}, {"locked", false}}
            );
        }

        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        int64_t remaining_ms = lock->expires_at - now_ms;
        int64_t remaining_s = remaining_ms / 1000;

        std::ostringstream ss;
        ss << "Memory #" << id << " is locked\n";
        ss << "  Holder: " << lock->holder_id << " (" << lock->holder_type << ")\n";
        ss << "  Type: " << lock->lock_type << "\n";
        ss << "  Expires in: " << remaining_s << "s\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"memory_id", id},
            {"locked", true},
            {"holder_id", lock->holder_id},
            {"holder_type", lock->holder_type},
            {"lock_type", lock->lock_type},
            {"expires_in_seconds", remaining_s}
        });
    }

    DuckDBToolResult tool_propose_change(const json& params) {
        auto [id, id_str] = parse_id(params);
        if (id <= 0) {
            return DuckDBToolResult::error("id is required");
        }

        std::string content = params.value("content", "");
        if (content.empty()) {
            return DuckDBToolResult::error("content is required");
        }

        std::string proposed_by = params.value("proposed_by", "unknown");

        int64_t merge_id = mind_->store().propose_change(id, content, proposed_by);
        if (merge_id <= 0) {
            return DuckDBToolResult::error("Failed to propose change");
        }

        return DuckDBToolResult::ok(
            "Change proposed for memory #" + std::to_string(id) + " (merge request #" + std::to_string(merge_id) + ")",
            {{"memory_id", id}, {"merge_id", merge_id}, {"status", "pending"}}
        );
    }

    DuckDBToolResult tool_list_merge_queue(const json& params) {
        std::string status = params.value("status", "pending");
        size_t limit = params.value("limit", 50);

        auto queue = mind_->store().list_merge_queue(status, limit);

        std::ostringstream ss;
        ss << "Merge Queue";
        if (!status.empty()) ss << " (status: " << status << ")";
        ss << "\n" << std::string(40, '-') << "\n";

        json queue_json = json::array();
        for (const auto& entry : queue) {
            ss << "• #" << entry.id << " → memory #" << entry.memory_id
               << " (v" << entry.base_version << ") by " << entry.proposed_by << "\n";

            queue_json.push_back({
                {"merge_id", entry.id},
                {"memory_id", entry.memory_id},
                {"proposed_content", entry.proposed_content.substr(0, 200)},
                {"proposed_by", entry.proposed_by},
                {"base_version", entry.base_version},
                {"status", entry.status},
                {"created_at", entry.created_at}
            });
        }

        if (queue.empty()) {
            ss << "No pending merge requests.\n";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"count", queue.size()},
            {"queue", queue_json}
        });
    }

    DuckDBToolResult tool_resolve_merge(const json& params) {
        int64_t merge_id = params.value("merge_id", 0);
        if (merge_id <= 0) {
            return DuckDBToolResult::error("merge_id is required");
        }

        std::string status = params.value("status", "applied");
        std::string resolution = params.value("resolution", "");

        if (!mind_->store().resolve_merge(merge_id, resolution, status)) {
            return DuckDBToolResult::error("Failed to resolve merge request #" + std::to_string(merge_id));
        }

        std::string action = (status == "applied") ? "applied" :
                            (status == "rejected") ? "rejected" : "marked as " + status;

        return DuckDBToolResult::ok(
            "Merge request #" + std::to_string(merge_id) + " " + action,
            {{"merge_id", merge_id}, {"status", status}, {"resolution", resolution}}
        );
    }

    // ═══════════════════════════════════════════════════════════════════════
    // File Time Machine: Explore and restore file versions from past sessions
    // ═══════════════════════════════════════════════════════════════════════

    // Helper: get Claude projects directory
    static std::string get_claude_projects_dir() {
        const char* home = std::getenv("HOME");
        if (!home) return "";
        return std::string(home) + "/.claude/projects";
    }

    // Helper: get file-history directory
    static std::string get_file_history_dir() {
        const char* home = std::getenv("HOME");
        if (!home) return "";
        return std::string(home) + "/.claude/file-history";
    }

    // Helper: parse time string (natural language or ISO8601)
    static int64_t parse_time_string(const std::string& time_str) {
        if (time_str.empty()) return 0;

        auto now = std::chrono::system_clock::now();
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();

        // Natural language time parsing
        std::string lower = time_str;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // "X minutes ago", "X hours ago", "X days ago"
        std::regex ago_regex(R"((\d+)\s*(minute|min|hour|hr|day|second|sec)s?\s*ago)", std::regex::icase);
        std::smatch match;
        if (std::regex_search(time_str, match, ago_regex)) {
            int64_t value = std::stoll(match[1].str());
            std::string unit = match[2].str();
            std::transform(unit.begin(), unit.end(), unit.begin(), ::tolower);

            int64_t ms_offset = 0;
            if (unit == "second" || unit == "sec") ms_offset = value * 1000LL;
            else if (unit == "minute" || unit == "min") ms_offset = value * 60 * 1000LL;
            else if (unit == "hour" || unit == "hr") ms_offset = value * 60 * 60 * 1000LL;
            else if (unit == "day") ms_offset = value * 24 * 60 * 60 * 1000LL;

            return now_ms - ms_offset;
        }

        // "at HH:MM" (today)
        std::regex at_time_regex(R"(at\s+(\d{1,2}):(\d{2}))", std::regex::icase);
        if (std::regex_search(time_str, match, at_time_regex)) {
            int hour = std::stoi(match[1].str());
            int minute = std::stoi(match[2].str());

            auto now_t = std::chrono::system_clock::to_time_t(now);
            std::tm* tm_now = std::localtime(&now_t);
            tm_now->tm_hour = hour;
            tm_now->tm_min = minute;
            tm_now->tm_sec = 0;

            auto target = std::chrono::system_clock::from_time_t(std::mktime(tm_now));
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                target.time_since_epoch()).count();
        }

        // "yesterday", "today", "last hour"
        if (lower.find("yesterday") != std::string::npos) {
            return now_ms - 24 * 60 * 60 * 1000LL;
        }
        if (lower.find("today") != std::string::npos || lower.find("now") != std::string::npos) {
            return now_ms;
        }
        if (lower.find("last hour") != std::string::npos) {
            return now_ms - 60 * 60 * 1000LL;
        }
        if (lower.find("last 10 minutes") != std::string::npos) {
            return now_ms - 10 * 60 * 1000LL;
        }

        // Try ISO8601 format: YYYY-MM-DDTHH:MM:SS or YYYY-MM-DD
        std::tm tm = {};
        std::istringstream ss(time_str);

        // Try full datetime first
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        if (!ss.fail()) {
            auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                tp.time_since_epoch()).count();
        }

        // Try date only
        ss.clear();
        ss.str(time_str);
        ss >> std::get_time(&tm, "%Y-%m-%d");
        if (!ss.fail()) {
            auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                tp.time_since_epoch()).count();
        }

        return 0;  // Could not parse
    }

    // Helper: format timestamp for display
    static std::string format_time(int64_t timestamp_ms) {
        auto seconds = timestamp_ms / 1000;
        std::time_t t = static_cast<std::time_t>(seconds);
        std::tm* tm = std::localtime(&t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
        return std::string(buf);
    }

    // Helper: index file-history-snapshot entries from a transcript
    size_t index_file_history_from_transcript(const std::string& session_id, const std::string& transcript_path, const std::string& realm) {
        if (!std::filesystem::exists(transcript_path)) return 0;

        std::vector<FileEdit> edits;
        std::ifstream file(transcript_path);
        std::string line;

        while (std::getline(file, line)) {
            if (line.empty()) continue;

            try {
                auto j = json::parse(line);

                // Look for file-history-snapshot entries
                if (j.value("type", "") != "file-history-snapshot") continue;

                auto snapshot = j.value("snapshot", json::object());
                auto backups = snapshot.value("trackedFileBackups", json::object());

                for (auto& [file_path, info] : backups.items()) {
                    std::string backup_filename = info.value("backupFileName", "");
                    if (backup_filename.empty() || backup_filename == "null") continue;

                    int version = info.value("version", 1);
                    std::string backup_time_str = info.value("backupTime", "");

                    // Parse ISO8601 timestamp
                    int64_t backup_time = 0;
                    if (!backup_time_str.empty()) {
                        backup_time = parse_time_string(backup_time_str);
                    }

                    FileEdit edit;
                    edit.session_id = session_id;
                    edit.file_path = file_path;
                    edit.version = version;
                    edit.backup_filename = backup_filename;
                    edit.backup_time = backup_time;
                    edit.realm = realm;

                    edits.push_back(edit);
                }
            } catch (...) {
                // Skip malformed lines
                continue;
            }
        }

        // Store all edits
        size_t stored = 0;
        for (const auto& edit : edits) {
            if (mind_->store().store_file_edit(edit) > 0) {
                stored++;
            }
        }

        return stored;
    }

    DuckDBToolResult tool_file_index_session(const json& params) {
        std::string session_id = params.value("session_id", "");
        if (session_id.empty()) {
            return DuckDBToolResult::error("session_id is required");
        }

        bool force = params.value("force", false);

        // Check if already indexed (unless force)
        if (!force && mind_->store().session_file_edits_indexed(session_id)) {
            return DuckDBToolResult::ok(
                "Session " + session_id + " already indexed",
                {{"session_id", session_id}, {"already_indexed", true}}
            );
        }

        // Find transcript path
        std::string projects_dir = get_claude_projects_dir();
        std::string transcript_path;
        std::string realm = "brahman";

        // Search through project directories for this session's transcript
        for (const auto& project_entry : std::filesystem::directory_iterator(projects_dir)) {
            if (!project_entry.is_directory()) continue;

            std::string candidate = project_entry.path().string() + "/" + session_id + ".jsonl";
            if (std::filesystem::exists(candidate)) {
                transcript_path = candidate;
                // Extract realm from project directory name
                realm = "project:" + project_entry.path().filename().string();
                break;
            }
        }

        if (transcript_path.empty()) {
            return DuckDBToolResult::error("Could not find transcript for session " + session_id);
        }

        size_t indexed = index_file_history_from_transcript(session_id, transcript_path, realm);

        return DuckDBToolResult::ok(
            "Indexed " + std::to_string(indexed) + " file edits from session " + session_id,
            {{"session_id", session_id}, {"indexed_count", indexed}, {"transcript_path", transcript_path}}
        );
    }

    DuckDBToolResult tool_file_index_all(const json& params) {
        bool force = params.value("force", false);
        size_t max_sessions = params.value("limit", 100);  // Default limit to avoid long runs

        std::string file_history_dir = get_file_history_dir();
        if (!std::filesystem::exists(file_history_dir)) {
            return DuckDBToolResult::error("File history directory not found: " + file_history_dir);
        }

        std::string projects_dir = get_claude_projects_dir();
        size_t total_indexed = 0;
        size_t sessions_processed = 0;
        size_t sessions_skipped = 0;
        std::vector<std::string> indexed_sessions;

        // Iterate through file-history directories (each is a session)
        for (const auto& session_entry : std::filesystem::directory_iterator(file_history_dir)) {
            if (!session_entry.is_directory()) continue;
            if (max_sessions > 0 && sessions_processed >= max_sessions) break;

            std::string session_id = session_entry.path().filename().string();

            // Skip if already indexed (unless force)
            if (!force && mind_->store().session_file_edits_indexed(session_id)) {
                sessions_skipped++;
                continue;
            }

            // Find transcript for this session
            std::string transcript_path;
            std::string realm = "brahman";

            for (const auto& project_entry : std::filesystem::directory_iterator(projects_dir)) {
                if (!project_entry.is_directory()) continue;

                std::string candidate = project_entry.path().string() + "/" + session_id + ".jsonl";
                if (std::filesystem::exists(candidate)) {
                    transcript_path = candidate;
                    realm = "project:" + project_entry.path().filename().string();
                    break;
                }
            }

            if (transcript_path.empty()) {
                // No transcript found - index directly from file-history metadata
                // Just mark as indexed with 0 entries for now
                mind_->store().mark_session_file_edits_indexed(session_id);
                sessions_processed++;
                continue;
            }

            size_t indexed = index_file_history_from_transcript(session_id, transcript_path, realm);
            total_indexed += indexed;
            sessions_processed++;

            if (indexed > 0) {
                indexed_sessions.push_back(session_id);
            }
        }

        std::ostringstream text;
        text << "Indexed " << total_indexed << " file edits from " << sessions_processed << " sessions\n";
        text << "Skipped " << sessions_skipped << " already-indexed sessions\n";

        return DuckDBToolResult::ok(
            text.str(),
            {
                {"total_indexed", total_indexed},
                {"sessions_processed", sessions_processed},
                {"sessions_skipped", sessions_skipped},
                {"indexed_sessions", indexed_sessions}
            }
        );
    }

    DuckDBToolResult tool_file_timeline(const json& params) {
        std::string query = params.value("query", "");
        std::string session_id = params.value("session_id", "");
        // Accept "path" as alias for "file_pattern" (matches file_at_time parameter name)
        std::string file_pattern = params.value("file_pattern", params.value("path", ""));
        size_t limit = params.value("limit", 20);
        bool path_given = !file_pattern.empty();
        // Default cross_session to true when a specific path is given (search all history)
        bool cross_session = params.value("cross_session", path_given);

        std::vector<FileEdit> edits;

        // If cross_session, auto-index recent sessions first
        if (cross_session) {
            // Index up to 50 recent sessions to enable cross-session queries
            tool_file_index_all({{"limit", 50}});
        }

        if (!session_id.empty()) {
            // Index this session if not already done
            if (!mind_->store().session_file_edits_indexed(session_id)) {
                tool_file_index_session({{"session_id", session_id}});
            }
            edits = mind_->store().get_session_file_edits(session_id, limit);
        } else if (!query.empty()) {
            // Parse time query
            int64_t target_time = parse_time_string(query);
            if (target_time == 0) {
                return DuckDBToolResult::error("Could not parse time query: " + query);
            }

            // Search ±30 minutes around target time (or wider for cross-session)
            int64_t window_ms = cross_session ? (24 * 60 * 60 * 1000LL) : (30 * 60 * 1000LL);
            edits = mind_->store().get_file_edits_in_range(
                target_time - window_ms,
                target_time + window_ms,
                file_pattern,
                limit
            );
        } else if (path_given) {
            // Path given but no time query: search all indexed history (no time constraint)
            edits = mind_->store().get_file_edits_in_range(
                0,
                std::numeric_limits<int64_t>::max(),
                file_pattern,
                limit
            );
        } else {
            // Default: last 24h (or last 7 days for cross-session)
            auto now = std::chrono::system_clock::now();
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            int64_t window_ms = cross_session ? (7 * 24 * 60 * 60 * 1000LL) : (24 * 60 * 60 * 1000LL);
            edits = mind_->store().get_file_edits_in_range(
                now_ms - window_ms,
                now_ms,
                file_pattern,
                limit
            );
        }

        // Format output
        json results = json::array();
        std::ostringstream text;
        text << "File Timeline:\n";

        for (const auto& edit : edits) {
            text << format_time(edit.backup_time) << "  "
                 << edit.file_path << "  v" << edit.version << "\n";

            results.push_back({
                {"id", edit.id},
                {"file_path", edit.file_path},
                {"version", edit.version},
                {"backup_filename", edit.backup_filename},
                {"backup_time", edit.backup_time},
                {"time_formatted", format_time(edit.backup_time)},
                {"session_id", edit.session_id}
            });
        }

        if (edits.empty()) {
            text << "(no file edits found)\n";
        }

        return DuckDBToolResult::ok(text.str(), {{"edits", results}, {"count", edits.size()}});
    }

    DuckDBToolResult tool_file_at_time(const json& params) {
        std::string file_path = params.value("file_path", "");
        if (file_path.empty()) {
            return DuckDBToolResult::error("file_path is required");
        }

        std::string time_str = params.value("time", "");
        std::string session_id = params.value("session_id", "");
        bool show_diff = params.value("show_diff", false);

        // Determine target time
        int64_t target_time;
        if (!time_str.empty()) {
            target_time = parse_time_string(time_str);
            if (target_time == 0) {
                return DuckDBToolResult::error("Could not parse time: " + time_str);
            }
        } else {
            // Default: now (get most recent version)
            auto now = std::chrono::system_clock::now();
            target_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
        }

        // Find the file edit closest to the target time
        auto edit_opt = mind_->store().get_file_at_time(file_path, target_time);
        if (!edit_opt) {
            return DuckDBToolResult::error("No version found for " + file_path + " at or before " + format_time(target_time));
        }

        auto& edit = *edit_opt;

        // Read the actual content from file-history
        std::string file_history_dir = get_file_history_dir();
        std::string backup_path = file_history_dir + "/" + edit.session_id + "/" + edit.backup_filename;

        if (!std::filesystem::exists(backup_path)) {
            return DuckDBToolResult::error("Backup file not found: " + backup_path);
        }

        std::ifstream file(backup_path);
        std::stringstream content_stream;
        content_stream << file.rdbuf();
        std::string content = content_stream.str();

        json result = {
            {"file_path", edit.file_path},
            {"version", edit.version},
            {"backup_filename", edit.backup_filename},
            {"backup_time", edit.backup_time},
            {"time_formatted", format_time(edit.backup_time)},
            {"session_id", edit.session_id},
            {"content", content},
            {"content_lines", std::count(content.begin(), content.end(), '\n') + 1}
        };

        std::ostringstream text;
        text << "File: " << edit.file_path << " (v" << edit.version << ")\n";
        text << "Time: " << format_time(edit.backup_time) << "\n";
        text << "Session: " << edit.session_id << "\n";
        text << "Lines: " << result["content_lines"] << "\n\n";

        if (show_diff) {
            // Read current file if it exists
            if (std::filesystem::exists(file_path)) {
                std::ifstream current_file(file_path);
                std::stringstream current_stream;
                current_stream << current_file.rdbuf();
                std::string current_content = current_stream.str();

                if (current_content != content) {
                    text << "[Content differs from current version - use file_restore to restore]\n\n";
                    result["differs_from_current"] = true;
                } else {
                    text << "[Content matches current version]\n\n";
                    result["differs_from_current"] = false;
                }
            }
        }

        // Show first 100 lines of content
        std::istringstream lines(content);
        std::string line;
        int line_count = 0;
        text << "--- Content ---\n";
        while (std::getline(lines, line) && line_count < 100) {
            text << line << "\n";
            line_count++;
        }
        if (line_count >= 100) {
            text << "\n... (truncated, " << result["content_lines"] << " total lines)\n";
        }

        return DuckDBToolResult::ok(text.str(), result);
    }

    DuckDBToolResult tool_file_restore(const json& params) {
        std::string file_path = params.value("file_path", "");
        if (file_path.empty()) {
            return DuckDBToolResult::error("file_path is required");
        }

        int64_t version_id = params.value("version_id", int64_t(0));
        bool preview = params.value("preview", true);

        // If no version_id, get the most recent version
        std::optional<FileEdit> edit_opt;
        if (version_id > 0) {
            // Look up by ID - not implemented yet, use file_at_time for now
            // For now, we'll use the file path lookup
            auto edits = mind_->store().get_file_edits(file_path, 1);
            if (edits.empty()) {
                return DuckDBToolResult::error("No version found for " + file_path);
            }
            edit_opt = edits[0];
        } else {
            auto edits = mind_->store().get_file_edits(file_path, 1);
            if (edits.empty()) {
                return DuckDBToolResult::error("No version found for " + file_path);
            }
            edit_opt = edits[0];
        }

        auto& edit = *edit_opt;

        // Read the backup content
        std::string file_history_dir = get_file_history_dir();
        std::string backup_path = file_history_dir + "/" + edit.session_id + "/" + edit.backup_filename;

        if (!std::filesystem::exists(backup_path)) {
            return DuckDBToolResult::error("Backup file not found: " + backup_path);
        }

        std::ifstream backup_file(backup_path);
        std::stringstream content_stream;
        content_stream << backup_file.rdbuf();
        std::string content = content_stream.str();

        json result = {
            {"file_path", edit.file_path},
            {"version", edit.version},
            {"backup_time", edit.backup_time},
            {"time_formatted", format_time(edit.backup_time)},
            {"preview", preview},
            {"content_lines", std::count(content.begin(), content.end(), '\n') + 1}
        };

        if (preview) {
            std::ostringstream text;
            text << "Preview: Would restore " << file_path << " to version from " << format_time(edit.backup_time) << "\n";
            text << "Lines: " << result["content_lines"] << "\n\n";
            text << "To actually restore, call file_restore with preview=false\n\n";

            // Show first 50 lines
            std::istringstream lines(content);
            std::string line;
            int line_count = 0;
            text << "--- Preview Content ---\n";
            while (std::getline(lines, line) && line_count < 50) {
                text << line << "\n";
                line_count++;
            }
            if (line_count >= 50) {
                text << "\n... (truncated)\n";
            }

            result["content"] = content;
            return DuckDBToolResult::ok(text.str(), result);
        }

        // Actually restore the file
        std::ofstream out_file(file_path);
        if (!out_file) {
            return DuckDBToolResult::error("Failed to write to " + file_path);
        }
        out_file << content;
        out_file.close();

        result["restored"] = true;
        return DuckDBToolResult::ok(
            "Restored " + file_path + " to version from " + format_time(edit.backup_time),
            result
        );
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Dream Tool Handlers
    // ═══════════════════════════════════════════════════════════════════════

    // Escape single quotes for SQL string literals
    static std::string esc_sql(const std::string& s) {
        std::string r;
        r.reserve(s.size() + 8);
        for (char c : s) {
            if (c == '\'') r += "''";
            else r += c;
        }
        return r;
    }

    DuckDBToolResult tool_dream_cancel(const json& params) {
        if (!params.contains("id")) {
            return DuckDBToolResult::error("id is required");
        }
        int64_t dream_id = params["id"].is_string()
            ? std::stoll(params["id"].get<std::string>())
            : params["id"].get<int64_t>();

        // Get sadhana_id for this dream
        auto res = mind_->store().execute_sql_query(
            "SELECT sadhana_id, status FROM dream WHERE id = " + std::to_string(dream_id));
        if (!res.success || res.rows.empty()) {
            return DuckDBToolResult::error("Dream " + std::to_string(dream_id) + " not found");
        }
        std::string status = res.rows[0][1];
        if (status == "cancelled" || status == "woke") {
            return DuckDBToolResult::ok("Dream " + std::to_string(dream_id) + " already " + status, {});
        }

        int64_t sadhana_id = 0;
        if (res.rows[0][0] != "NULL" && !res.rows[0][0].empty()) {
            try { sadhana_id = std::stoll(res.rows[0][0]); } catch (...) {}
        }

        // Stop the underlying sadhana
        if (sadhana_manager_ && sadhana_id > 0) {
            sadhana_manager_->stop(sadhana_id);
        }

        // Mark dream as cancelled
        auto now_val = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        mind_->store().execute_raw(
            "UPDATE dream SET status = 'cancelled', ended_at = " + std::to_string(now_val) +
            " WHERE id = " + std::to_string(dream_id));

        return DuckDBToolResult::ok(
            "Cancelled dream #" + std::to_string(dream_id) +
            (sadhana_id > 0 ? " (stopped sadhana #" + std::to_string(sadhana_id) + ")" : ""),
            {{"dream_id", dream_id}, {"sadhana_id", sadhana_id}, {"status", "cancelled"}});
    }

    DuckDBToolResult tool_dream_force_woke(const json& params) {
        if (!params.contains("id")) {
            return DuckDBToolResult::error("id is required");
        }
        int64_t dream_id = params["id"].is_string()
            ? std::stoll(params["id"].get<std::string>())
            : params["id"].get<int64_t>();

        auto res = mind_->store().execute_sql_query(
            "SELECT sadhana_id, status FROM dream WHERE id = " + std::to_string(dream_id));
        if (!res.success || res.rows.empty()) {
            return DuckDBToolResult::error("Dream " + std::to_string(dream_id) + " not found");
        }
        std::string status = res.rows[0][1];
        if (status == "woke") {
            return DuckDBToolResult::ok("Dream " + std::to_string(dream_id) + " already woke", {});
        }

        int64_t sadhana_id = 0;
        if (res.rows[0][0] != "NULL" && !res.rows[0][0].empty()) {
            try { sadhana_id = std::stoll(res.rows[0][0]); } catch (...) {}
        }
        if (sadhana_manager_ && sadhana_id > 0) {
            sadhana_manager_->stop(sadhana_id);
        }

        auto now_val = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        mind_->store().execute_raw(
            "UPDATE dream SET status = 'woke', findings = '[force-woke]', ended_at = " +
            std::to_string(now_val) + " WHERE id = " + std::to_string(dream_id));

        return DuckDBToolResult::ok(
            "Force-woke dream #" + std::to_string(dream_id) +
            (sadhana_id > 0 ? " (stopped sadhana #" + std::to_string(sadhana_id) + ")" : ""),
            {{"dream_id", dream_id}, {"sadhana_id", sadhana_id}, {"status", "woke"}});
    }

    DuckDBToolResult tool_dream_start(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }
        if (subconscious_) subconscious_->notify_query();

        std::string topic = params.value("topic", "");
        if (topic.empty()) {
            return DuckDBToolResult::error("Topic is required");
        }
        std::string realm = params.value("realm", "brahman");
        std::string publish_path = params.value("publish_path", "");
        std::string brain_provider = params.value("brain_provider", "claude");
        std::string brain_model   = params.value("brain_model", "sonnet");
        int max_concurrent = params.value("max_concurrent", 2);

        auto now_val = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Rate limit: check active dream count
        auto active_res = mind_->store().execute_sql_query(
            "SELECT COUNT(*) FROM dream WHERE status = 'dreaming'");
        if (active_res.success && !active_res.rows.empty() && !active_res.rows[0].empty()) {
            int active = 0;
            try { active = std::stoi(active_res.rows[0][0]); } catch (...) {}
            if (active >= max_concurrent) {
                return DuckDBToolResult::error(
                    "Dream rate limit: " + std::to_string(active) +
                    " dreams already active (max " + std::to_string(max_concurrent) + "). "
                    "Wait for a dream to finish or use dream_cancel to clear a stuck one.");
            }
        }

        // Insert dream record using write connection (execute_raw = write_execute)
        if (!mind_->store().execute_raw(
            "INSERT INTO dream (id, topic, status, sadhana_id, started_at, realm) "
            "VALUES (nextval('dream_seq'), '" + esc_sql(topic) + "', 'dreaming', 0, " +
            std::to_string(now_val) + ", '" + esc_sql(realm) + "')")) {
            return DuckDBToolResult::error("Failed to create dream record");
        }

        // Retrieve the newly inserted dream ID
        auto id_res = mind_->store().execute_sql_query(
            "SELECT id FROM dream WHERE topic = '" + esc_sql(topic) +
            "' AND started_at = " + std::to_string(now_val));
        if (!id_res.success || id_res.rows.empty()) {
            return DuckDBToolResult::error("Failed to retrieve dream ID after insert");
        }
        int64_t dream_id = std::stoll(id_res.rows[0][0]);

        // Create sadhana with dream goal_dsl (single-cycle: agent returns "achieved")
        json goal_dsl = {{"kind", "dream"}, {"topic", topic}, {"dream_id", dream_id}};
        if (!publish_path.empty()) {
            goal_dsl["publish_path"] = publish_path;
        }
        std::string goal = "[dream] Explore: " + topic;

        int64_t sadhana_id = sadhana_manager_->create(goal, brain_provider, brain_model, 0, realm, goal_dsl);
        if (sadhana_id == 0) {
            return DuckDBToolResult::error("Failed to create dream sadhana");
        }

        // Link sadhana to dream record before starting (so it's always set even if start fails)
        mind_->store().execute_raw(
            "UPDATE dream SET sadhana_id = " + std::to_string(sadhana_id) +
            " WHERE id = " + std::to_string(dream_id));

        if (!sadhana_manager_->start(sadhana_id)) {
            // Roll back dream status so it doesn't clog the rate limit
            int64_t ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            mind_->store().execute_raw(
                "UPDATE dream SET status = 'cancelled', ended_at = " + std::to_string(ts) +
                " WHERE id = " + std::to_string(dream_id));
            return DuckDBToolResult::error(
                "Created dream sadhana " + std::to_string(sadhana_id) + " but failed to start");
        }

        json result;
        result["dream_id"]   = dream_id;
        result["sadhana_id"] = sadhana_id;
        result["topic"]      = topic;
        result["status"]     = "dreaming";

        std::ostringstream msg;
        msg << "Dream started: " << topic
            << " (dream #" << dream_id << ", sadhana #" << sadhana_id << ")";
        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_dream_wander(const json& params) {
        if (!sadhana_manager_) {
            return DuckDBToolResult::error("Sadhana manager not initialized");
        }
        if (subconscious_) subconscious_->notify_query();

        std::string realm = params.value("realm", "brahman");
        std::string publish_path = params.value("publish_path", "");
        if (publish_path.empty()) {
            const char* env_path = std::getenv("CHITTA_DREAM_PUBLISH_PATH");
            if (env_path) publish_path = env_path;
        }
        std::string topic;

        // Exclude internal/code topics that can't be web-searched meaningfully
        const std::string exclude_code =
            " AND content NOT LIKE '[code]%' "
            " AND content NOT LIKE '[training]%' "
            " AND content NOT LIKE '[symbol]%' "
            " AND content NOT LIKE '[distilled]%' "
            " AND content NOT LIKE '[locomo%' "
            " AND tags NOT LIKE '%symbol%' ";

        // Priority 1: memories tagged as gaps/unresolved
        auto gap_res = mind_->store().execute_sql_query(
            "SELECT content FROM memory "
            "WHERE tags LIKE '%gap%' AND tags LIKE '%unresolved%' " +
            exclude_code +
            "ORDER BY RANDOM() LIMIT 1");
        if (gap_res.success && !gap_res.rows.empty()) {
            topic = gap_res.rows[0][0];
            if (topic.size() > 100) topic = topic.substr(0, 100);
        }

        // Priority 2: low-confidence memories (uncertain knowledge)
        if (topic.empty()) {
            auto low_res = mind_->store().execute_sql_query(
                "SELECT content FROM memory "
                "WHERE confidence < 0.5 AND confidence > 0.0 " +
                exclude_code +
                "ORDER BY RANDOM() LIMIT 1");
            if (low_res.success && !low_res.rows.empty()) {
                topic = low_res.rows[0][0];
                if (topic.size() > 100) topic = topic.substr(0, 100);
            }
        }

        // Priority 3: curiosity seeds (hardcoded topics of enduring interest)
        if (topic.empty()) {
            static const std::vector<std::string> seeds = {
                "consciousness and the hard problem of subjective experience",
                "emergent complexity in distributed systems",
                "the nature of memory and forgetting in biological brains",
                "Vedantic philosophy and modern neuroscience",
                "language models and the limits of statistical learning",
                "self-organization in nature: from cells to civilizations",
                "the history of symbolic AI versus connectionism",
                "epistemic humility in scientific discovery",
                "attention mechanisms and the binding problem",
                "entropy, information, and the arrow of time"
            };
            auto now_val = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            topic = seeds[static_cast<size_t>(now_val) % seeds.size()];
        }

        json start_params = {
            {"topic", topic},
            {"realm", realm},
            // Oracle pattern: use opencode (cheap) for autonomous dreaming;
            // Claude is reserved for interactive sessions.
            {"brain_provider", "opencode"},
            {"brain_model", "gpt-4o"},
            // Hard cap: never more than 2 concurrent wandering dreams
            {"max_concurrent", 2}
        };
        if (!publish_path.empty()) start_params["publish_path"] = publish_path;
        return tool_dream_start(start_params);
    }

    // Finalize completed dreams: set status, ended_at, findings, memories_created.
    // Also backfills already-woke dreams that have NULL findings (legacy from old code).
    // If specific_dream_id > 0, only that dream is checked.
    void finalize_completed_dreams(int64_t specific_dream_id = 0) {
        std::string filter = specific_dream_id > 0
            ? " AND d.id = " + std::to_string(specific_dream_id)
            : "";
        auto pending = mind_->store().execute_sql_query(
            "SELECT d.id, d.started_at, d.sadhana_id, "
            "       COALESCE(d.ended_at, s.updated_at, 0), COALESCE(s.last_action, '') "
            "FROM dream d JOIN sadhana s ON d.sadhana_id = s.id "
            "WHERE (d.status = 'dreaming' OR (d.status = 'woke' AND d.findings IS NULL)) "
            "  AND s.state = 'done'" + filter);
        if (!pending.success) return;
        for (const auto& row : pending.rows) {
            if (row.size() < 5) continue;
            int64_t dream_id   = std::stoll(row[0]);
            int64_t started_at = std::stoll(row[1]);
            int64_t sadhana_id = std::stoll(row[2]);
            int64_t ended_at   = (row[3] == "NULL") ? 0 : std::stoll(row[3]);
            std::string findings = (row[4] == "NULL") ? "" : row[4];
            (void)sadhana_id;

            // Count memories tagged [dream] created during this dream's window
            int64_t window_end = ended_at > 0 ? ended_at + 5000 : started_at + 3600000LL;
            auto mc_res = mind_->store().execute_sql_query(
                "SELECT COUNT(*) FROM memory "
                "WHERE created_at >= " + std::to_string(started_at) +
                " AND created_at <= " + std::to_string(window_end) +
                " AND content LIKE '%[dream]%'");
            int mc = 0;
            if (mc_res.success && !mc_res.rows.empty() && !mc_res.rows[0].empty()
                && mc_res.rows[0][0] != "NULL") {
                mc = std::stoi(mc_res.rows[0][0]);
            }

            mind_->store().execute_raw(
                "UPDATE dream SET status = 'woke', "
                "  ended_at = " + std::to_string(ended_at) + ", "
                "  memories_created = " + std::to_string(mc) + ", "
                "  findings = '" + esc_sql(findings) + "' "
                "WHERE id = " + std::to_string(dream_id));

            // Spawn a synthesis sadhana if the dream produced meaningful memories
            if (sadhana_manager_ && mc >= 3) {
                // Retrieve dream topic for the synthesis goal
                auto topic_res = mind_->store().execute_sql_query(
                    "SELECT topic FROM dream WHERE id = " + std::to_string(dream_id));
                std::string topic;
                if (topic_res.success && !topic_res.rows.empty() && !topic_res.rows[0].empty()) {
                    topic = topic_res.rows[0][0];
                }
                json dsl;
                dsl["kind"]     = "dream_synthesis";
                dsl["dream_id"] = dream_id;
                dsl["topic"]    = topic;
                std::string goal = "Synthesize dream #" + std::to_string(dream_id)
                                 + (topic.empty() ? "" : " (" + topic + ")")
                                 + " findings into actionable code gaps";
                int64_t synth_id = sadhana_manager_->create(
                    goal, "", "", 0, "brahman", dsl, 1);
                if (synth_id > 0) sadhana_manager_->start(synth_id);
            }
        }
    }

    DuckDBToolResult tool_dream_list(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        int limit = params.value("limit", 10);
        if (limit <= 0 || limit > 100) limit = 10;
        std::string realm = params.value("realm", "");

        // Lazily finalize any completed dreams (sets status, ended_at, findings, memories_created)
        finalize_completed_dreams();

        std::string where = realm.empty() ? "" : "WHERE d.realm = '" + esc_sql(realm) + "' ";

        auto res = mind_->store().execute_sql_query(
            "SELECT d.id, d.topic, d.status, d.findings, d.memories_created, "
            "       d.started_at, d.ended_at, d.sadhana_id, d.realm, "
            "       s.iterations, s.last_action "
            "FROM dream d "
            "LEFT JOIN sadhana s ON d.sadhana_id = s.id " +
            where +
            "ORDER BY d.started_at DESC LIMIT " + std::to_string(limit));

        json dreams = json::array();
        if (res.success) {
            for (const auto& row : res.rows) {
                if (row.size() < 9) continue;
                json d;
                d["id"]               = std::stoll(row[0]);
                d["topic"]            = row[1];
                d["status"]           = row[2];
                d["findings"]         = (row[3] == "NULL") ? "" : row[3];
                d["memories_created"] = (row[4] == "NULL") ? 0 : std::stoi(row[4]);
                d["started_at"]       = std::stoll(row[5]);
                d["ended_at"]         = std::stoll(row[6]);
                d["sadhana_id"]       = std::stoll(row[7]);
                d["realm"]            = row[8];
                if (row.size() >= 10) d["iterations"]  = (row[9]  == "NULL") ? 0 : std::stoi(row[9]);
                if (row.size() >= 11) d["last_action"]  = (row[10] == "NULL") ? "" : row[10];
                dreams.push_back(d);
            }
        }

        json result;
        result["dreams"] = dreams;
        result["count"]  = dreams.size();

        std::ostringstream msg;
        msg << "Found " << dreams.size() << " dream(s)";
        return DuckDBToolResult::ok(msg.str(), result);
    }

    DuckDBToolResult tool_dream_status(const json& params) {
        if (subconscious_) subconscious_->notify_query();

        if (!params.contains("id")) {
            return DuckDBToolResult::error("id is required");
        }
        int64_t dream_id = params["id"].is_string()
            ? std::stoll(params["id"].get<std::string>())
            : params["id"].get<int64_t>();

        // Lazily finalize if sadhana completed (sets status, ended_at, findings, memories_created)
        finalize_completed_dreams(dream_id);

        auto res = mind_->store().execute_sql_query(
            "SELECT d.id, d.topic, d.status, d.findings, d.memories_created, "
            "       d.started_at, d.ended_at, d.sadhana_id, d.realm, "
            "       s.id, s.state, s.goal, s.iterations, s.brain_calls, s.last_action, "
            "       s.brain_provider, s.brain_model "
            "FROM dream d "
            "LEFT JOIN sadhana s ON d.sadhana_id = s.id "
            "WHERE d.id = " + std::to_string(dream_id));

        if (!res.success || res.rows.empty()) {
            return DuckDBToolResult::error("Dream #" + std::to_string(dream_id) + " not found");
        }

        const auto& row = res.rows[0];
        json dream;
        dream["id"]               = std::stoll(row[0]);
        dream["topic"]            = row[1];
        dream["status"]           = row[2];
        dream["findings"]         = (row[3] == "NULL") ? "" : row[3];
        dream["memories_created"] = (row[4] == "NULL") ? 0 : std::stoi(row[4]);
        dream["started_at"]       = std::stoll(row[5]);
        dream["ended_at"]         = std::stoll(row[6]);
        dream["sadhana_id"]       = std::stoll(row[7]);
        dream["realm"]            = row[8];

        if (row.size() >= 15 && row[9] != "NULL") {
            json sadhana;
            sadhana["id"]             = std::stoll(row[9]);
            sadhana["state"]          = row[10];
            sadhana["goal"]           = row[11];
            sadhana["iterations"]     = (row[12] == "NULL") ? 0 : std::stoi(row[12]);
            sadhana["brain_calls"]    = (row[13] == "NULL") ? 0 : std::stoi(row[13]);
            sadhana["last_action"]    = (row[14] == "NULL") ? "" : row[14];
            sadhana["brain_provider"] = (row.size() > 15 && row[15] != "NULL") ? row[15] : "";
            sadhana["brain_model"]    = (row.size() > 16 && row[16] != "NULL") ? row[16] : "";
            dream["sadhana"] = sadhana;

            if (sadhana_manager_) {
                int64_t sadhana_id = std::stoll(row[9]);
                dream["sadhana_history"] = sadhana_manager_->get_history(sadhana_id, 3);
            }
        }

        std::ostringstream msg;
        msg << "Dream #" << dream_id << " [" << row[2] << "]: " << row[1];
        return DuckDBToolResult::ok(msg.str(), dream);
    }
};

}  // namespace chitta
