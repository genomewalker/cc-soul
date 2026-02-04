#pragma once
// DuckDBRpcHandler: RPC handler for DuckDBMind
//
// Same interface as SimpleRpcHandler but uses DuckDBMind backend.

#include "../mind/duckdb_mind.hpp"
#include "../mind/subconscious.hpp"
#include "../mind/payload.hpp"
#include "../code_intel.hpp"
#include "../symbol_resolver.hpp"
#include "../version.hpp"
#include <nlohmann/json.hpp>
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
#include <unordered_set>
#include <cctype>
#include <array>
#include <cstdio>

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

class DuckDBRpcHandler {
public:
    explicit DuckDBRpcHandler(DuckDBMind* mind) : mind_(mind), subconscious_(nullptr) {
        register_tools();
    }

    // Connect subconscious for event pushing
    void set_subconscious(Subconscious* s) { subconscious_ = s; }

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
    std::vector<json> tools_;
    std::unordered_map<std::string, std::function<DuckDBToolResult(const json&)>> handlers_;

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
                    {"tag", {{"type", "string"}, {"description", "Filter by tag"}}},
                    {"realm", {{"type", "string"}, {"description", "Filter by realm (empty = all realms)"}}},
                    {"include_global", {{"type", "boolean"}, {"description", "Include global memories (default: true)"}}}
                }},
                {"required", {"query"}}
            }}
        });
        handlers_["recall"] = [this](const json& p) { return tool_recall(p); };

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
                    {"kind", {{"type", "string"}, {"description", "Filter by kind: belief, wisdom, episode, correction, preference"}}},
                    {"min_confidence", {{"type", "number"}, {"description", "Min confidence threshold (default: 0)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max memories to process (default: 100)"}}},
                    {"dry_run", {{"type", "boolean"}, {"description", "Preview without updating (default: false)"}}}
                }}
            }}
        });
        handlers_["reembed_memories"] = [this](const json& p) { return tool_reembed_memories(p); };

        tools_.push_back({
            {"name", "embed_symbols"},
            {"description", "Fast embed symbol metadata (no LLM needed, ~100/sec)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"batch_size", {{"type", "integer"}, {"description", "Symbols per batch (default: 100)"}}}
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
                    {"kind", {{"type", "string"}, {"description", "Filter by symbol kind when using name"}}}
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
                    {"kind", {{"type", "string"}, {"description", "Filter by symbol kind when using name"}}}
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
                    {"kind", {{"type", "string"}, {"description", "Filter by symbol kind (function, class, method)"}}}
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
                    {"name", {{"type", "string"}, {"description", "Function name to read"}}}
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
                    {"limit", {{"type", "integer"}, {"description", "Max results (default 10)"}}}
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
                {"required", {"session_id"}}
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
                    {"mode", {{"type", "string"}, {"description", "Output mode: inject (compact) or debug (verbose)"}}}
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
                    {"limit", {{"type", "integer"}, {"description", "Max candidates (default: 50)"}}},
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
                    {"limit", {{"type", "integer"}, {"description", "Max patterns (default: 50)"}}}
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
                    {"limit", {{"type", "integer"}, {"description", "Max habits (default: 50)"}}}
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
                    {"limit", {{"type", "integer"}, {"description", "Max results (default: 20)"}}}
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
                    {"limit", {{"type", "integer"}, {"description", "Max rows to return (default: 100)"}}}
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

        // Set realm and visibility if non-default
        int64_t db_id = static_cast<int64_t>(id.low);
        if (realm != "brahman") {
            mind_->store().set_realm(db_id, realm);
        }
        if (visibility != RealmVisibility::Private) {
            mind_->store().set_visibility(db_id, visibility);
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
        std::string realm = params.value("realm", "");
        bool include_global = params.value("include_global", true);

        // If realm filtering is requested, we need to use the store directly
        std::vector<MemoryResult> store_results;
        if (!realm.empty() && mind_->has_yantra()) {
            // Get embedding for query and call store directly with realm filter
            // For now, fall back to mind_->recall() and filter post-hoc
            // TODO: Expose embedder through mind for direct store queries
        }

        auto results = mind_->recall(query, realm.empty() ? limit : limit * 2);

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

            int pct = static_cast<int>(std::min(r.relevance, 1.0f) * 100);
            std::string type_name = node_type_name(r.type);

            json result_entry = {
                {"id", r.id.to_string()},
                {"relevance", r.relevance},
                {"similarity", r.similarity},
                {"type", type_name},
                {"text", r.text}
            };

            // Include realm info in results
            auto mem = mind_->store().get_memory(static_cast<int64_t>(r.id.low));
            if (mem) {
                result_entry["realm"] = mem->realm;
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

        return DuckDBToolResult::ok(ss.str(), {{"results", results_json}, {"realm", realm}});
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // RLM-style Exploration Primitives
    // ═══════════════════════════════════════════════════════════════════════════

    DuckDBToolResult tool_explore_recall(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) {
            return DuckDBToolResult::error("Query is required");
        }

        size_t limit = params.value("limit", 10);
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

    DuckDBToolResult tool_soul_context(const json&) {
        // Use cached health for fast response (updated by maintenance cycle)
        auto cached = mind_->store().cached_health();

        std::ostringstream ss;
        ss << "Soul State (DuckDB):\n";
        ss << "  Nodes: " << cached.total_memories << " total\n";
        ss << "  Confidence: " << std::fixed << std::setprecision(2) << cached.avg_confidence << " avg\n";
        ss << "  Triplets: " << cached.total_triplets << "\n";
        ss << "  Symbols: " << cached.total_symbols << "\n";
        ss << "  Yantra: " << (mind_->has_yantra() ? "ready" : "not attached") << "\n";
        ss << "  Status: " << (cached.is_open ? "OK" : "ERROR") << "\n";

        // Skip expensive queries - use cached values
        size_t transcripts = 0;  // Skip transcript_count() query
        json calibration_json = json::array();  // Skip calibration query

        return DuckDBToolResult::ok(ss.str(), {
            {"version", CHITTA_VERSION},
            {"total_nodes", cached.total_memories},
            {"total_symbols", cached.total_symbols},
            {"avg_confidence", cached.avg_confidence},
            {"triplet_count", cached.total_triplets},
            {"yantra_ready", mind_->has_yantra()},
            {"status", cached.is_open ? "OK" : "ERROR"},
            {"transcripts_tracked", transcripts},
            {"calibration", calibration_json}
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
        int limit = params.value("limit", 100);
        bool dry_run = params.value("dry_run", false);

        // Get global memories (these are the partnership memories we care most about)
        auto memories = mind_->store().list_global_memories(limit, kind_filter);

        // Filter to those that might have zero embeddings (check by recalling with their own content)
        std::vector<std::pair<int64_t, std::string>> to_reembed;

        for (const auto& mem : memories) {
            if (min_confidence > 0 && mem.confidence < min_confidence) continue;

            // Check if memory has meaningful embedding by recalling it
            // If a memory with its own content doesn't recall itself well, it likely has zero embedding
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

        size_t zero_embed_count = to_reembed.size();
        size_t total_checked = memories.size();

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

        size_t batch_size = params.value("batch_size", 100);
        auto symbols = mind_->store().get_unembedded_symbols(batch_size);

        if (symbols.empty()) {
            return DuckDBToolResult::ok("All symbols embedded", {{"embedded", 0}, {"remaining", 0}});
        }

        size_t embedded = 0;
        auto start = std::chrono::steady_clock::now();

        for (const auto& sym : symbols) {
            // Build searchable text from metadata
            std::string basename = sym.file_path;
            size_t pos = basename.rfind('/');
            if (pos != std::string::npos) basename = basename.substr(pos + 1);

            std::ostringstream text;
            text << sym.kind << " " << sym.name << " in " << basename;
            if (!sym.signature.empty() && sym.signature != sym.name) {
                text << ": " << sym.signature;
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
        ss << "Embedded " << embedded << " symbols in " << ms << "ms";
        ss << " (" << std::fixed << std::setprecision(1) << rate << "/sec)\n";
        ss << "Remaining: " << remaining;

        return DuckDBToolResult::ok(ss.str(), {
            {"embedded", embedded},
            {"remaining", remaining},
            {"elapsed_ms", ms},
            {"rate_per_sec", rate}
        });
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

        std::ostringstream ss;
        ss << "Removed " << removed << " duplicate symbols\n";
        ss << "Before: " << before << ", After: " << after;

        return DuckDBToolResult::ok(ss.str(), {
            {"before", before},
            {"after", after},
            {"removed", removed}
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
            Artha artha = mind_->embedder().transform(query);
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

        // Create project triplet
        mind_->connect(project, "contains", std::to_string(symbols_stored) + "_symbols");

        ss << "Learned codebase: " << project << "\n";
        ss << "  Path: " << path << "\n";
        ss << "  Mode: " << (force ? "force" : "full") << "\n";
        ss << "  Symbols: " << symbols_stored << "\n";
        ss << "  Symbols embedded: " << symbols_embedded << "\n";
        ss << "  Callsites: " << callsites_stored << "\n";

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
        // Try ID first (more specific)
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

        // Return first match (exact match preferred)
        for (const auto& s : symbols) {
            if (s.name == name) return s;
        }
        return symbols[0];
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
            bm25_matches = mind_->store().bm25_search_symbols(query, limit);
            search_mode = "bm25";
        }

        // Semantic search (slow, ~2-5s on CPU)
        if (use_semantic && mind_->has_yantra()) {
            auto artha = mind_->embedder().transform(query);
            if (!artha.nu.is_zero()) {
                semantic_matches = mind_->store().search_symbols_by_embedding(artha.nu.data, limit, kind);
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

            std::string basename = sym.file_path;
            size_t pos = basename.rfind('/');
            if (pos != std::string::npos) basename = basename.substr(pos + 1);

            // Filter by kind if specified
            if (!kind.empty() && sym.kind != kind) return;

            if (score > 0) {
                ss << "  [" << std::fixed << std::setprecision(0) << (score * 100) << "%] ";
            } else {
                ss << "  ";
            }
            ss << sym.kind << " " << sym.name << " @" << basename << ":" << sym.line_start
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
                    auto artha = mind_->embedder().transform(task);
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

        std::ostringstream ss;
        ss << "Health Check:\n";
        ss << "  Status: " << (is_open ? "OK" : "ERROR") << "\n";
        ss << "  Memories: " << cached.total_memories << "\n";
        ss << "  Symbols: " << cached.total_symbols << "\n";
        ss << "  Triplets: " << cached.total_triplets << "\n";
        ss << "  Avg Confidence: " << cached.avg_confidence << "\n";
        ss << "  Yantra: " << (yantra ? "ready" : "not attached") << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"status", is_open ? "ok" : "error"},
            {"software_version", CHITTA_VERSION},
            {"protocol_major", CHITTA_PROTOCOL_VERSION_MAJOR},
            {"protocol_minor", CHITTA_PROTOCOL_VERSION_MINOR},
            {"memories", cached.total_memories},
            {"symbols", cached.total_symbols},
            {"triplets", cached.total_triplets},
            {"avg_confidence", cached.avg_confidence},
            {"yantra_ready", yantra}
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
        std::string session_id = params.value("session_id", "");
        if (session_id.empty()) {
            return DuckDBToolResult::error("session_id is required");
        }

        LedgerEntry entry;
        entry.session_id = session_id;
        entry.project = params.value("project", "default");
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
        if (!entry->mood.empty()) ss << "  Mood: " << entry->mood << "\n";
        if (entry->coherence > 0) ss << "  Coherence: " << entry->coherence << "\n";
        if (entry->confidence > 0) ss << "  Confidence: " << entry->confidence << "\n";

        // Parse JSON fields back to arrays for structured output
        json result = {
            {"found", true},
            {"id", entry->id},
            {"session_id", entry->session_id},
            {"project", entry->project},
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

        json result = {
            {"task_id", task_id},
            {"status", task->status},
            {"iterations", task->iterations},
            {"event_count", events.size()},
            {"snapshot", ss.str()}
        };

        return DuckDBToolResult::ok(ss.str(), result);
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
        size_t limit = params.value("limit", 50);
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
        size_t limit = params.value("limit", 20);
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
                // Generate query embedding once
                Artha query_artha = mind_->embedder().transform(query);
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
        std::string realm = params.value("realm", "");

        if (context.empty()) {
            return DuckDBToolResult::error("context is required");
        }

        auto patterns = mind_->store().anticipation_predict(context, limit, realm);

        std::ostringstream ss;
        ss << "Predicted Actions for Context\n";
        ss << "══════════════════════════════\n\n";

        if (patterns.empty()) {
            ss << "No matching patterns found.\n";
        } else {
            for (const auto& p : patterns) {
                float success_rate = p.frequency > 0 ? (float)p.success_count / p.frequency * 100 : 0;
                ss << "• " << p.action << "\n";
                ss << "  Context: " << p.context.substr(0, 80) << (p.context.length() > 80 ? "..." : "") << "\n";
                ss << "  Frequency: " << p.frequency << " | Success: " << std::fixed << std::setprecision(0) << success_rate << "%\n\n";
            }
        }

        json patterns_json = json::array();
        for (const auto& p : patterns) {
            patterns_json.push_back({
                {"id", p.id},
                {"context", p.context},
                {"action", p.action},
                {"frequency", p.frequency},
                {"success_count", p.success_count},
                {"realm", p.realm}
            });
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
        size_t limit = params.value("limit", 50);

        auto patterns = mind_->store().anticipation_list(realm, limit);

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
        size_t limit = params.value("limit", 50);

        auto habits = mind_->store().habit_list(realm, min_strength, limit);

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
        return {{"tools", tools_}};
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

        auto goals = mind_->store().goal_list(status, realm, limit);

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
        size_t limit = params.value("limit", 100);

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
};

}  // namespace chitta
