#pragma once
// SymbolResolver: Cross-file symbol resolution for call graphs
//
// Resolves callsite callee_leaf strings to actual symbol IDs
// and populates the call_edge table.

#include "duckdb_store.hpp"
#include "code_intel.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdlib>

namespace chitta {

// Resolution result with confidence
struct ResolvedCall {
    int64_t caller_id = 0;
    int64_t callee_id = 0;
    float confidence = 0.0f;
    std::string reason;
};

// Resolution statistics
struct ResolutionStats {
    size_t total_callsites = 0;
    size_t resolved = 0;
    size_t ambiguous = 0;
    size_t unresolved = 0;
    size_t indirect = 0;
    float avg_confidence = 0.0f;
};

class SymbolResolver {
public:
    explicit SymbolResolver(DuckDBStore& store) : store_(store) {}

    // Build name index from all symbols in the store
    void build_index() {
        name_to_ids_.clear();
        qualified_to_id_.clear();
        file_to_symbols_.clear();
        symbol_info_.clear();

        // Query all symbols using execute_sql_query (returns strings)
        auto result = store_.execute_sql_query(
            "SELECT id, kind, name, file_path, line_start FROM symbol"
        );

        if (!result.success) return;

        for (const auto& row : result.rows) {
            if (row.size() < 4) continue;

            int64_t id = std::stoll(row[0]);
            std::string kind = row[1];
            std::string name = row[2];
            std::string file_path = row[3];

            // Index by simple name
            name_to_ids_[name].push_back(id);

            // Index by qualified name (kind::name)
            std::string qualified = kind + "::" + name;
            qualified_to_id_[qualified] = id;

            // Index by file
            file_to_symbols_[file_path].push_back(id);

            // Store symbol info for lookup
            symbol_info_[id] = {name, kind, file_path};
        }

        index_built_ = true;
    }

    // Resolve all callsites from triplets and populate call_edge table
    ResolutionStats resolve_project(const std::string& project = "") {
        ResolutionStats stats;

        if (!index_built_) build_index();

        // Query calls triplets: subject (caller) → "calls" → object (callee name)
        std::string query = "SELECT subject, object, source_file "
                           "FROM triplet "
                           "WHERE predicate = 'calls'";
        if (!project.empty()) {
            // Escape single quotes in project name
            std::string escaped_project;
            for (char c : project) {
                if (c == '\'') escaped_project += "''";
                else escaped_project += c;
            }
            query += " AND source_file LIKE '%" + escaped_project + "%'";
        }

        auto result = store_.execute_sql_query(query);
        if (!result.success) return stats;

        std::vector<std::pair<int64_t, int64_t>> edges;
        float total_confidence = 0.0f;

        for (const auto& row : result.rows) {
            if (row.size() < 3) continue;

            stats.total_callsites++;

            std::string caller_name = row[0];
            std::string callee_name = row[1];
            std::string source_file = row[2];

            // Resolve caller to symbol ID
            int64_t caller_id = 0;
            auto caller_it = name_to_ids_.find(caller_name);
            if (caller_it != name_to_ids_.end()) {
                // Prefer same-file match
                for (int64_t id : caller_it->second) {
                    auto& info = symbol_info_[id];
                    if (info.file_path == source_file) {
                        caller_id = id;
                        break;
                    }
                }
                if (caller_id == 0 && !caller_it->second.empty()) {
                    caller_id = caller_it->second[0];
                }
            }

            if (caller_id == 0) {
                stats.unresolved++;
                continue;
            }

            // Resolve callee to symbol ID
            auto callee_it = name_to_ids_.find(callee_name);
            if (callee_it == name_to_ids_.end() || callee_it->second.empty()) {
                stats.unresolved++;
                continue;
            }

            const auto& callee_ids = callee_it->second;
            int64_t callee_id = 0;
            float confidence = 0.0f;

            if (callee_ids.size() == 1) {
                // Unique match
                callee_id = callee_ids[0];
                confidence = 0.85f;
            } else {
                // Multiple matches - prefer same file
                for (int64_t id : callee_ids) {
                    auto& info = symbol_info_[id];
                    if (info.file_path == source_file) {
                        callee_id = id;
                        confidence = 0.90f;
                        break;
                    }
                }
                if (callee_id == 0) {
                    // Prefer functions over methods for bare calls
                    for (int64_t id : callee_ids) {
                        auto& info = symbol_info_[id];
                        if (info.kind == "function") {
                            callee_id = id;
                            confidence = 0.70f;
                            break;
                        }
                    }
                }
                if (callee_id == 0) {
                    callee_id = callee_ids[0];
                    confidence = 0.50f;
                }
            }

            edges.push_back({caller_id, callee_id});
            total_confidence += confidence;

            if (confidence >= 0.7f) {
                stats.resolved++;
            } else {
                stats.ambiguous++;
            }
        }

        // Batch insert edges
        for (const auto& [caller, callee] : edges) {
            store_.add_call(caller, callee);
        }

        if (stats.resolved + stats.ambiguous > 0) {
            stats.avg_confidence = total_confidence / (stats.resolved + stats.ambiguous);
        }

        return stats;
    }

private:
    DuckDBStore& store_;
    bool index_built_ = false;

    // Indexes
    std::unordered_map<std::string, std::vector<int64_t>> name_to_ids_;
    std::unordered_map<std::string, int64_t> qualified_to_id_;
    std::unordered_map<std::string, std::vector<int64_t>> file_to_symbols_;

    // Symbol info cache
    struct SymbolInfo {
        std::string name;
        std::string kind;
        std::string file_path;
    };
    std::unordered_map<int64_t, SymbolInfo> symbol_info_;
};

} // namespace chitta
