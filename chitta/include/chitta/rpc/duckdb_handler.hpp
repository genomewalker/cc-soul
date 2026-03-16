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

#ifdef CHITTA_FIELD_AVAILABLE
// Forward declaration — full definition is included at the bottom of this file.
class ChittaFieldHandler;

// Free function for routing check — usable before ChittaFieldHandler is complete.
// Must stay in sync with ChittaFieldHandler::is_field_routable().
inline bool cf_is_field_routable(const std::string& method) {
    static const std::unordered_set<std::string> routable = {
        "remember", "recall", "strengthen", "weaken", "forget", "touch",
        "connect", "hybrid_recall", "smart_recall", "recall_temporal", "recall_keyword",
        "theme_list", "theme_get", "theme_stats", "theme_recall",
        "theme_maintain", "theme_assign_orphans",
    };
    return routable.count(method) > 0;
}
#endif

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

#ifdef CHITTA_FIELD_AVAILABLE
    // Connect chitta-field handler.
    void set_field_handler(ChittaFieldHandler* h);  // defined after ChittaFieldHandler is complete
    void set_field_store(FieldStore* fs) { field_store_ = fs; }
    void set_field_initializing(bool v) { field_initializing_.store(v, std::memory_order_release); }
#endif

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

#ifdef CHITTA_FIELD_AVAILABLE
            if (field_handler_ && cf_is_field_routable(name)) {
                return dispatch_to_field_handler(id, name, args);
            }
#endif

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
#ifdef CHITTA_FIELD_AVAILABLE
    ChittaFieldHandler* field_handler_ = nullptr;
    FieldStore* field_store_ = nullptr;  // Direct pointer for use before ChittaFieldHandler is complete
    std::atomic<bool> field_initializing_{false};  // True during async init window

    // Defined after chitta_field_handler.hpp is included (bottom of file).
    json dispatch_to_field_handler(const json& id, const std::string& name, const json& args);
#endif
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
                    {"include_global", {{"type", "boolean"}, {"description", "Include global memories (default: true)"}}},
                    {"separation_mode", {{"type", "boolean"}, {"description", "Return maximally diverse results via MMR reranking (pattern separation, default: false)"}}},
                    {"gwt_mode", {{"type", "boolean"}, {"description", "Global Workspace Theory mode: broad search → salience competition → focused expansion from winner (default: false)"}}}
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
                    {"partnership_only", {{"type", "boolean"}, {"description", "Exclude code intel (symbol, projectessence, modulestate, patternstate)"}}},
                    {"separation_mode", {{"type", "boolean"}, {"description", "Return maximally diverse results via MMR reranking (pattern separation, default: false)"}}}
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

        // Ledger + long-task tools require chitta-field
#ifdef CHITTA_FIELD_AVAILABLE
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
#endif  // CHITTA_FIELD_AVAILABLE

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
                    {"include_global", {{"type", "boolean"}, {"description", "Include global memories (default: true)"}}},
                    {"separation_mode", {{"type", "boolean"}, {"description", "Return maximally diverse results via MMR reranking (pattern separation, default: false)"}}},
                    {"gwt_mode", {{"type", "boolean"}, {"description", "Global Workspace Theory mode: broad search → salience competition → focused expansion from winner. Bypasses intent classification (default: false)"}}}
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
                    {"realm", {{"type", "string"}, {"description", "Realm for isolation (default: brahman)"}}},
                    {"goal_dsl", {{"type", "object"}, {"description", "Optional DSL object, e.g. {\"kind\":\"impl\",\"repo\":\"/path\"} or {\"kind\":\"think\"}"}}}
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

        // think_wander: trigger internal memory synthesis sadhana
        tools_.push_back({
            {"name", "think_wander"},
            {"description", "Trigger a think sadhana: internal synthesis of existing memories. Finds patterns, connects disparate insights, may generate [thought][impl] memories. Fires automatically every hour during idle; can also be triggered manually."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"realm", {{"type", "string"}, {"description", "Memory realm (default: current realm)"}}}
                }},
                {"required", json::array()}
            }}
        });
        handlers_["think_wander"] = [this](const json& p) { return tool_think_wander(p); };

        // impl_start: create and start an impl sadhana with the review gate
        tools_.push_back({
            {"name", "impl_start"},
            {"description", "Start the self-improvement implementation sadhana. Each cycle it picks one pending [impl]/[thought][impl]/[dream][impl] memory, implements the change, runs an opencode review gate, and commits only if approved. Runs once per day."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"repo", {{"type", "string"}, {"description", "Absolute path to the cc-soul repo (default: auto-detected from git)"}}},
                    {"interval_seconds", {{"type", "integer"}, {"description", "Seconds between cycles (default: 86400 = 1 day)"}}},
                    {"max_turns", {{"type", "integer"}, {"description", "Max turns per cycle (default: 15)"}}},
                    {"realm", {{"type", "string"}, {"description", "Memory realm (default: brahman)"}}}
                }},
                {"required", json::array()}
            }}
        });
        handlers_["impl_start"] = [this](const json& p) { return tool_impl_start(p); };

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

    // ═══════════════════════════════════════════════════════════════════════════
    // RLM-style Exploration Primitives
    // ═══════════════════════════════════════════════════════════════════════════

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

    // Valid memory types (normalized from aspect system)
    static constexpr std::array<const char*, 11> VALID_MEMORY_TYPES = {
        "decision", "preference", "correction", "insight", "milestone",
        "approach", "habit", "belief", "gap", "wisdom", "episode"
    };

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

    // ========================================================================
    // Conversational Memory System Tool Implementations
    // ========================================================================

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

    // ═══════════════════════════════════════════════════════════════════════
    // Sadhana Tool Handlers
    // ═══════════════════════════════════════════════════════════════════════

    // ═══════════════════════════════════════════════════════════════════════
    // Context Repository Tool Handlers (Letta-inspired)
    // ═══════════════════════════════════════════════════════════════════════

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

    #include "handlers/memory.hpp"
    #include "handlers/triplets.hpp"
    #include "handlers/code_intel.hpp"
    #include "handlers/sadhana.hpp"
    #include "handlers/session.hpp"
    #include "handlers/realm.hpp"
#ifdef CHITTA_FIELD_AVAILABLE
    #include "handlers/ledger.hpp"
    #include "handlers/long_task.hpp"
#endif
    #include "handlers/system.hpp"
    #include "handlers/narrative.hpp"
    #include "handlers/theme.hpp"
    #include "handlers/distill.hpp"
    #include "handlers/file_tracking.hpp"
};

}  // namespace chitta

#ifdef CHITTA_FIELD_AVAILABLE
// Include the full ChittaFieldHandler definition after DuckDBRpcHandler is complete.
// This resolves the forward-declaration: DuckDBRpcHandler forward-declares
// ChittaFieldHandler above; ChittaFieldHandler only needs DuckDBToolResult which
// is already defined — by including here, both are fully defined.
#include "chitta_field_handler.hpp"

namespace chitta {

// dispatch_to_field_handler: defined here so ChittaFieldHandler is complete.
// For semantic recall methods, embed the query text using DuckDBMind's yantra
// before forwarding to chitta-field, which expects a float embedding vector.
inline void DuckDBRpcHandler::set_field_handler(ChittaFieldHandler* h) {
    field_handler_ = h;
    field_store_ = h ? h->field_store() : nullptr;
}

inline json DuckDBRpcHandler::dispatch_to_field_handler(const json& id,
                                                         const std::string& name,
                                                         const json& args) {
    json enriched = args;
    if (mind_ && !enriched.contains("embedding")) {
        // Embed query for semantic recall methods.
        static const std::unordered_set<std::string> query_methods = {
            "recall", "hybrid_recall", "smart_recall", "theme_recall"
        };
        if (query_methods.count(name) && enriched.contains("query")) {
            Vector emb = mind_->embedder().embed_query(enriched["query"].get<std::string>());
            if (!emb.data.empty()) enriched["embedding"] = emb.data;
        }
        // Embed content for remember so stored memories are semantically searchable.
        if (name == "remember" && enriched.contains("content")) {
            Vector emb = mind_->embedder().embed(enriched["content"].get<std::string>());
            if (!emb.data.empty()) enriched["embedding"] = emb.data;
        }
    }

    auto result = field_handler_->dispatch(name, enriched);
    return make_tool_response(id, result);
}

} // namespace chitta
#endif
