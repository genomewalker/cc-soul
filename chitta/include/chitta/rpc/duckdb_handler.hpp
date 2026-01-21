#pragma once
// DuckDBRpcHandler: RPC handler for DuckDBMind
//
// Same interface as SimpleRpcHandler but uses DuckDBMind backend.

#include "../mind/duckdb_mind.hpp"
#include "../mind/payload.hpp"
#include "../code_intel.hpp"
#include "../version.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <algorithm>

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
    explicit DuckDBRpcHandler(DuckDBMind* mind) : mind_(mind) {
        register_tools();
    }

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
    std::vector<json> tools_;
    std::unordered_map<std::string, std::function<DuckDBToolResult(const json&)>> handlers_;

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

        // observe
        tools_.push_back({
            {"name", "observe"},
            {"description", "Store an observation/learning (used by hooks for [LEARN] extraction)"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"category", {{"type", "string"}, {"description", "Category: wisdom, insight, signal, episode"}}},
                    {"title", {{"type", "string"}, {"description", "Title/summary"}}},
                    {"content", {{"type", "string"}, {"description", "Full content"}}},
                    {"tags", {{"type", "string"}, {"description", "Comma-separated tags"}}}
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
                    {"k", {{"type", "integer"}, {"description", "Max results"}}}
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
            {"description", "Learn codebase by extracting symbols from all source files"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Directory path to analyze"}}},
                    {"project", {{"type", "string"}, {"description", "Project name (auto-detected if empty)"}}},
                    {"max_files", {{"type", "integer"}, {"description", "Max files to process (default 500)"}}},
                    {"exclude", {{"type", "string"}, {"description", "Comma-separated directories to exclude"}}}
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
    }

    // Tool implementations
    DuckDBToolResult tool_remember(const json& params) {
        std::string content = params.value("content", "");
        if (content.empty()) {
            return DuckDBToolResult::error("Content is required");
        }

        std::string type_str = params.value("type", "episode");
        std::string realm = params.value("realm", "brahman");
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
            return DuckDBToolResult::error("Failed to remember (quality gate or embedding failed)");
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

            ss << "[" << pct << "%] [" << type_name << "] "
               << r.text.substr(0, 100);
            if (r.text.size() > 100) ss << "...";
            ss << "\n";

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

        ss.str("");
        ss << "Found " << count << " results";
        if (!realm.empty()) ss << " in realm '" << realm << "'";
        ss << ":\n";

        return DuckDBToolResult::ok(ss.str(), {{"results", results_json}, {"realm", realm}});
    }

    DuckDBToolResult tool_connect(const json& params) {
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
        DuckDBHealth h = mind_->health();

        std::ostringstream ss;
        ss << "Soul State (DuckDB):\n";
        ss << "  Nodes: " << h.total_nodes << " total";
        if (h.total_nodes > 0) {
            ss << ", " << h.active_nodes << " active";
            if (h.weak_nodes > 0) ss << ", " << h.weak_nodes << " weak";
            if (h.stale_nodes > 0) ss << ", " << h.stale_nodes << " stale";
        }
        ss << "\n";
        ss << "  Confidence: " << std::fixed << std::setprecision(2) << h.avg_confidence << " avg\n";
        ss << "  Triplets: " << mind_->triplet_count() << "\n";
        ss << "  Yantra: " << (mind_->has_yantra() ? "ready" : "not attached") << "\n";
        ss << "  Status: " << h.status() << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"total_nodes", h.total_nodes},
            {"active_nodes", h.active_nodes},
            {"weak_nodes", h.weak_nodes},
            {"stale_nodes", h.stale_nodes},
            {"avg_confidence", h.avg_confidence},
            {"triplet_count", mind_->triplet_count()},
            {"yantra_ready", mind_->has_yantra()},
            {"status", h.status()}
        });
    }

    DuckDBToolResult tool_strengthen(const json& params) {
        std::string id_str = params.value("id", "");
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        NodeId id = NodeId::from_string(id_str);
        float amount = params.value("amount", 0.1f);

        if (!mind_->strengthen(id, amount)) {
            return DuckDBToolResult::error("Node not found");
        }

        return DuckDBToolResult::ok("Strengthened node " + id_str);
    }

    DuckDBToolResult tool_weaken(const json& params) {
        std::string id_str = params.value("id", "");
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        NodeId id = NodeId::from_string(id_str);
        float amount = params.value("amount", 0.1f);

        if (!mind_->weaken(id, amount)) {
            return DuckDBToolResult::error("Node not found");
        }

        return DuckDBToolResult::ok("Weakened node " + id_str);
    }

    DuckDBToolResult tool_forget(const json& params) {
        std::string id_str = params.value("id", "");
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        NodeId id = NodeId::from_string(id_str);
        if (!mind_->remove(id)) {
            return DuckDBToolResult::error("Node not found");
        }

        return DuckDBToolResult::ok("Forgot node " + id_str);
    }

    DuckDBToolResult tool_observe(const json& params) {
        std::string title = params.value("title", "");
        std::string content = params.value("content", "");
        std::string category = params.value("category", "episode");

        if (title.empty() || content.empty()) {
            return DuckDBToolResult::error("Title and content are required");
        }

        NodeType type = NodeType::Episode;
        if (category == "wisdom" || category == "insight") type = NodeType::Wisdom;
        else if (category == "belief") type = NodeType::Belief;
        else if (category == "decision") type = NodeType::Wisdom;

        std::string full_text = title + "\n" + content;
        NodeId id = mind_->remember(full_text, type);

        static const NodeId null_id{};
        if (id == null_id) {
            return DuckDBToolResult::error("Failed to observe");
        }

        return DuckDBToolResult::ok(
            "Observed: " + title.substr(0, 50),
            {{"id", id.to_string()}, {"category", category}}
        );
    }

    DuckDBToolResult tool_full_resonate(const json& params) {
        std::string query = params.value("query", "");
        if (query.empty()) {
            return DuckDBToolResult::error("Query is required");
        }

        size_t k = params.value("k", 10);
        auto results = mind_->recall(query, k);

        if (results.empty()) {
            return DuckDBToolResult::ok("No memories found matching query.", {{"results", json::array()}});
        }

        std::ostringstream ss;
        ss << "[I know]\n";
        ss << "Found " << results.size() << " results:\n\n";

        json results_json = json::array();
        for (const auto& r : results) {
            int pct = static_cast<int>(std::min(r.relevance, 1.0f) * 100);
            std::string type_name = node_type_name(r.type);

            ss << "[" << pct << "%] [" << type_name << "] "
               << r.text.substr(0, 200);
            if (r.text.size() > 200) ss << "...";
            ss << "\n\n";

            results_json.push_back({
                {"id", r.id.to_string()},
                {"relevance", r.relevance},
                {"type", type_name},
                {"text", r.text}
            });
        }

        return DuckDBToolResult::ok(ss.str(), {{"results", results_json}});
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
        auto symbols = intel.extract_directory(path, exclude, max_files);

        if (symbols.empty()) {
            return DuckDBToolResult::ok("No symbols found in " + path, {{"stored", 0}});
        }

        // Store symbols in DuckDB
        size_t stored = intel.store_symbols(mind_->store(), symbols);

        // Create project triplet
        mind_->connect(project, "contains", std::to_string(stored) + "_symbols");

        std::ostringstream ss;
        ss << "Learned " << stored << " symbols from " << project << "\n";
        ss << "  Path: " << path << "\n";
        ss << "  Symbols: " << stored << " stored\n";

        // Summary by kind
        std::unordered_map<std::string, size_t> by_kind;
        for (const auto& sym : symbols) {
            by_kind[sym.kind]++;
        }
        ss << "  Breakdown:\n";
        for (const auto& [kind, count] : by_kind) {
            ss << "    " << kind << ": " << count << "\n";
        }

        return DuckDBToolResult::ok(ss.str(), {
            {"project", project},
            {"path", path},
            {"stored", stored},
            {"by_kind", by_kind}
        });
    }

    DuckDBToolResult tool_find_symbol(const json& params) {
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

    DuckDBToolResult tool_code_context(const json& params) {
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

    // Essential memory tool implementations
    DuckDBToolResult tool_grow(const json& params) {
        std::string type_str = params.value("type", "");
        std::string content = params.value("content", "");
        std::string title = params.value("title", "");

        if (type_str.empty() || content.empty()) {
            return DuckDBToolResult::error("Type and content are required");
        }

        NodeType type = NodeType::Wisdom;
        if (type_str == "belief") type = NodeType::Belief;
        else if (type_str == "failure" || type_str == "episode") type = NodeType::Episode;
        else if (type_str == "aspiration") type = NodeType::Aspiration;
        else if (type_str == "dream") type = NodeType::Dream;

        std::string full_content = title.empty() ? content : title + "\n" + content;
        NodeId id = mind_->remember(full_content, type);

        static const NodeId null_id{};
        if (id == null_id) {
            return DuckDBToolResult::error("Failed to grow (quality gate or embedding failed)");
        }

        return DuckDBToolResult::ok(
            "Grew " + type_str + ": " + (title.empty() ? content.substr(0, 50) : title),
            {{"id", id.to_string()}, {"type", type_str}}
        );
    }

    DuckDBToolResult tool_get(const json& params) {
        std::string id_str = params.value("id", "");
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        // Parse ID - could be UUID string or int64
        int64_t db_id = 0;
        try {
            db_id = std::stoll(id_str);
        } catch (...) {
            // Try parsing as UUID
            NodeId nid = NodeId::from_string(id_str);
            db_id = static_cast<int64_t>(nid.low);
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
        std::string id_str = params.value("id", "");
        std::string content = params.value("content", "");

        if (id_str.empty() || content.empty()) {
            return DuckDBToolResult::error("ID and content are required");
        }

        int64_t db_id = 0;
        try {
            db_id = std::stoll(id_str);
        } catch (...) {
            NodeId nid = NodeId::from_string(id_str);
            db_id = static_cast<int64_t>(nid.low);
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
        std::string id_str = params.value("id", "");
        std::string add_tag = params.value("add", "");
        std::string remove_tag = params.value("remove", "");

        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        if (add_tag.empty() && remove_tag.empty()) {
            return DuckDBToolResult::error("Either 'add' or 'remove' tag is required");
        }

        int64_t db_id = 0;
        try {
            db_id = std::stoll(id_str);
        } catch (...) {
            NodeId nid = NodeId::from_string(id_str);
            db_id = static_cast<int64_t>(nid.low);
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
        auto health = mind_->store().health();

        std::ostringstream ss;
        ss << "Health Check:\n";
        ss << "  Status: " << (health.is_open ? "OK" : "ERROR") << "\n";
        ss << "  Memories: " << health.total_memories << "\n";
        ss << "  Symbols: " << health.total_symbols << "\n";
        ss << "  Triplets: " << health.total_triplets << "\n";
        ss << "  Avg Confidence: " << health.avg_confidence << "\n";
        ss << "  Yantra: " << (mind_->has_yantra() ? "ready" : "not attached") << "\n";

        return DuckDBToolResult::ok(ss.str(), {
            {"status", health.is_open ? "ok" : "error"},
            {"software_version", CHITTA_VERSION},
            {"protocol_major", CHITTA_PROTOCOL_VERSION_MAJOR},
            {"protocol_minor", CHITTA_PROTOCOL_VERSION_MINOR},
            {"memories", health.total_memories},
            {"symbols", health.total_symbols},
            {"triplets", health.total_triplets},
            {"avg_confidence", health.avg_confidence},
            {"yantra_ready", mind_->has_yantra()}
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
        std::string id_str = params.value("id", "");
        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        int64_t db_id = 0;
        try {
            db_id = std::stoll(id_str);
        } catch (...) {
            NodeId nid = NodeId::from_string(id_str);
            db_id = static_cast<int64_t>(nid.low);
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
        std::string id_str = params.value("id", "");
        std::string realm = params.value("realm", "");

        if (id_str.empty() || realm.empty()) {
            return DuckDBToolResult::error("ID and realm are required");
        }

        int64_t db_id = 0;
        try {
            db_id = std::stoll(id_str);
        } catch (...) {
            NodeId nid = NodeId::from_string(id_str);
            db_id = static_cast<int64_t>(nid.low);
        }

        bool ok = mind_->store().set_realm(db_id, realm);
        if (!ok) {
            return DuckDBToolResult::error("Failed to set realm");
        }

        return DuckDBToolResult::ok("Set primary realm to '" + realm + "' for memory " + id_str);
    }

    DuckDBToolResult tool_realm_add(const json& params) {
        std::string id_str = params.value("id", "");
        std::string realm = params.value("realm", "");

        if (id_str.empty() || realm.empty()) {
            return DuckDBToolResult::error("ID and realm are required");
        }

        int64_t db_id = 0;
        try {
            db_id = std::stoll(id_str);
        } catch (...) {
            NodeId nid = NodeId::from_string(id_str);
            db_id = static_cast<int64_t>(nid.low);
        }

        bool ok = mind_->store().add_to_realm(db_id, realm);
        if (!ok) {
            return DuckDBToolResult::error("Failed to add to realm");
        }

        return DuckDBToolResult::ok("Added memory " + id_str + " to realm '" + realm + "'");
    }

    DuckDBToolResult tool_realm_remove(const json& params) {
        std::string id_str = params.value("id", "");
        std::string realm = params.value("realm", "");

        if (id_str.empty() || realm.empty()) {
            return DuckDBToolResult::error("ID and realm are required");
        }

        int64_t db_id = 0;
        try {
            db_id = std::stoll(id_str);
        } catch (...) {
            NodeId nid = NodeId::from_string(id_str);
            db_id = static_cast<int64_t>(nid.low);
        }

        bool ok = mind_->store().remove_from_realm(db_id, realm);
        if (!ok) {
            return DuckDBToolResult::error("Failed to remove from realm");
        }

        return DuckDBToolResult::ok("Removed memory " + id_str + " from realm '" + realm + "'");
    }

    DuckDBToolResult tool_realm_visibility(const json& params) {
        std::string id_str = params.value("id", "");
        int visibility = params.value("visibility", 0);

        if (id_str.empty()) {
            return DuckDBToolResult::error("ID is required");
        }

        if (visibility < 0 || visibility > 2) {
            return DuckDBToolResult::error("Visibility must be 0 (Private), 1 (Shared), or 2 (Global)");
        }

        int64_t db_id = 0;
        try {
            db_id = std::stoll(id_str);
        } catch (...) {
            NodeId nid = NodeId::from_string(id_str);
            db_id = static_cast<int64_t>(nid.low);
        }

        bool ok = mind_->store().set_visibility(db_id, static_cast<RealmVisibility>(visibility));
        if (!ok) {
            return DuckDBToolResult::error("Failed to set visibility");
        }

        std::string vis_name = visibility == 0 ? "Private" : (visibility == 1 ? "Shared" : "Global");
        return DuckDBToolResult::ok("Set visibility to " + vis_name + " for memory " + id_str);
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
};

}  // namespace chitta
