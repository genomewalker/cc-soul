// Chitta CLI - Multi-mode memory operations
// Command-line interface for soul integration
//
// Modes:
//   CLI mode:    chitta <tool> [args...]  - Direct tool invocation
//   Thin client: chitta                   - Forward JSON-RPC to daemon
//
// CLI Examples:
//   chitta recall "query"
//   chitta recall "query" --zoom sparse
//   chitta soul_context
//   chitta observe --category decision --title "..." --content "..."
//   chitta grow --type wisdom --title "..." --content "..."
//
// Options:
//   --socket-path PATH  Unix socket path
//   --json              CLI mode: output raw JSON instead of text

#include <chitta/socket_client.hpp>
#include <chitta/version.hpp>
#include <set>
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <nlohmann/json.hpp>

// Tool parameter specification
struct ToolParam {
    const char* name;
    const char* description;
    bool required;
    const char* default_val;  // nullptr if no default
};

struct ToolSpec {
    const char* name;
    const char* description;
    std::vector<ToolParam> params;
};

// Complete tool specifications with all parameters
static const std::vector<ToolSpec> TOOL_SPECS = {
    // Memory tools
    {"recall", "Semantic search with zoom levels",
     {{"query", "What to search for", true, nullptr},
      {"zoom", "Detail: sparse|normal|dense|full", false, "normal"},
      {"tag", "Filter by exact tag", false, nullptr},
      {"limit", "Max results", false, nullptr},
      {"threshold", "Min similarity (0-1)", false, "0"},
      {"learn", "Apply Hebbian learning", false, "false"},
      {"primed", "Boost by session context", false, "false"},
      {"compete", "Lateral inhibition", false, "true"}}},

    {"recall_by_tag", "Recall by exact tag match only",
     {{"tag", "Tag to filter by", true, nullptr},
      {"limit", "Max results", false, "50"}}},

    {"resonate", "Semantic search with spreading activation",
     {{"query", "What to search for", true, nullptr},
      {"k", "Max results", false, "10"},
      {"spread_strength", "Activation spread (0-1)", false, "0.5"},
      {"learn", "Apply Hebbian learning", false, "true"},
      {"hebbian_strength", "Learning rate (0-0.5)", false, "0.03"}}},

    {"full_resonate", "Full resonance with all mechanisms",
     {{"query", "What to search for", true, nullptr},
      {"k", "Max results", false, "10"},
      {"spread_strength", "Activation spread (0-1)", false, "0.5"},
      {"hebbian_strength", "Learning rate (0-0.2)", false, "0.03"},
      {"exclude_tags", "JSON array of tags to exclude", false, nullptr}}},

    {"proactive_surface", "Surface important unrequested memories",
     {{"query", "Current context", true, nullptr},
      {"exclude_ids", "JSON array of IDs to skip", false, nullptr},
      {"limit", "Max results", false, "3"},
      {"min_relevance", "Min relevance (0-1)", false, "0.25"},
      {"min_confidence", "Min confidence (0-1)", false, "0.6"},
      {"min_epsilon", "Min epsilon (0-1)", false, "0.7"}}},

    {"detect_contradictions", "Find memories conflicting with new content",
     {{"content", "New content to check", true, nullptr},
      {"similarity_threshold", "Min similarity (0-1)", false, "0.6"},
      {"limit", "Max results", false, "5"}}},

    {"multi_hop", "Multi-hop reasoning via PageRank",
     {{"query", "What to reason about", true, nullptr},
      {"k", "Max results", false, "10"},
      {"epsilon", "Approximation error (0.001-0.5)", false, "0.05"}}},

    {"timeline", "Recent activity with Hawkes weighting",
     {{"hours", "Time window (1-720)", false, "24"},
      {"limit", "Max results", false, "20"}}},

    {"causal_chain", "Find causal chains to an effect",
     {{"effect_id", "Node ID of the effect", true, nullptr},
      {"max_depth", "Max chain length (1-10)", false, "5"},
      {"min_confidence", "Min confidence (0-1)", false, "0.3"}}},

    {"consolidate", "Find/merge similar nodes via LSH",
     {{"dry_run", "Just list candidates", false, "true"},
      {"min_similarity", "Min similarity (0.8-1)", false, "0.92"},
      {"max_merges", "Max merges", false, "10"}}},

    // Learning tools
    {"grow", "Add wisdom, beliefs, failures, aspirations, dreams, terms",
     {{"type", "Type: wisdom|belief|failure|aspiration|dream|term", true, nullptr},
      {"content", "The content to add", true, nullptr},
      {"title", "Short title (required for wisdom/failure)", false, nullptr},
      {"domain", "Domain context", false, nullptr},
      {"confidence", "Initial confidence (0-1)", false, "0.8"},
      {"epsilon", "Reconstructability (0-1)", false, "0.5"}}},

    {"observe", "Record an observation/episode",
     {{"category", "Type: bugfix|decision|discovery|feature|refactor|session_ledger|signal", true, nullptr},
      {"title", "Short title (max 80 chars)", true, nullptr},
      {"content", "Full observation content", true, nullptr},
      {"project", "Project name", false, nullptr},
      {"tags", "Comma-separated tags", false, nullptr},
      {"epsilon", "Reconstructability (0-1)", false, "0.5"}}},

    {"feedback", "Mark memory as helpful or misleading",
     {{"memory_id", "UUID of the memory", true, nullptr},
      {"helpful", "true=helpful, false=misleading", true, nullptr},
      {"context", "Why this feedback", false, nullptr}}},

    {"update", "Update node content (for epsilon-yajna)",
     {{"id", "Node UUID to update", true, nullptr},
      {"content", "New content", true, nullptr}}},

    {"remove", "Remove a node from memory",
     {{"id", "Node UUID to remove", true, nullptr}}},

    {"connect", "Create triplet: subject --[predicate]--> object",
     {{"subject", "Subject entity", true, nullptr},
      {"predicate", "Relationship type", true, nullptr},
      {"object", "Object entity", true, nullptr},
      {"weight", "Edge weight (0-1)", false, "1.0"}}},

    {"query", "Query triplet relationships",
     {{"subject", "Subject (empty = any)", false, nullptr},
      {"predicate", "Predicate (empty = any)", false, nullptr},
      {"object", "Object (empty = any)", false, nullptr}}},

    {"import_soul", "Import .soul file (SSL format) into mind",
     {{"file", "Path to .soul file", true, nullptr},
      {"replace", "Full rewire: remove existing codebase nodes first", false, "false"}}},

    {"export_soul", "Export knowledge to .soul file (SSL format)",
     {{"file", "Output path for .soul file", true, nullptr},
      {"tag", "Tag to filter nodes (e.g., vessel, codebase, symbol)", true, nullptr},
      {"include_triplets", "Include triplets in export", false, "true"}}},

    {"resolve_entity", "Resolve entity name to NodeId (O(1) via EntityIndex)",
     {{"entity", "Entity name to resolve", true, nullptr}}},

    {"link_entity", "Link entity name to an existing node",
     {{"entity", "Entity name", true, nullptr},
      {"node_id", "NodeId to link to", true, nullptr}}},

    {"bootstrap_entity_index", "Auto-link triplet entities to existing nodes by title match",
     {}},

    {"list_entities", "List all linked entities in EntityIndex",
     {}},

    // Context tools
    {"soul_context", "Get soul state (tau, psi, stats)",
     {{"query", "Optional context for relevant wisdom", false, nullptr},
      {"format", "Output: text|json", false, "text"}}},

    {"attractors", "Find conceptual clusters in memory",
     {{"k", "Number of attractors", false, "5"},
      {"min_size", "Min cluster size", false, "3"}}},

    {"lens", "Search through cognitive perspective",
     {{"query", "What to search for", true, nullptr},
      {"lens", "Perspective: manas|buddhi|ahamkara|chitta|vikalpa|sakshi|all", false, "all"},
      {"limit", "Max results per lens", false, "5"}}},

    {"lens_harmony", "Check harmony across all lenses",
     {{"query", "What to check", true, nullptr}}},

    // Intention tools
    {"intend", "Set an active intention",
     {{"want", "What you want to achieve", true, nullptr},
      {"because", "Why this matters", false, nullptr}}},

    {"wonder", "Register a question/knowledge gap",
     {{"question", "The question", true, nullptr},
      {"context", "Why this matters", false, nullptr}}},

    {"answer", "Resolve a knowledge gap",
     {{"question_id", "ID of the gap node", true, nullptr},
      {"resolution", "The answer", true, nullptr}}},

    // Narrative tools
    {"narrate", "Start or end a narrative thread",
     {{"action", "Action: start|end", true, nullptr},
      {"title", "Thread title (for start)", false, nullptr},
      {"episode_id", "Thread ID (for end)", false, nullptr},
      {"content", "Summary (for end)", false, nullptr},
      {"emotion", "Emotional tone (for end)", false, nullptr}}},

    {"ledger", "Save/load/list session state",
     {{"action", "Action: save|load|list", true, nullptr},
      {"content", "Session summary (for save)", false, nullptr},
      {"project", "Project name", false, nullptr},
      {"id", "Ledger ID (for load)", false, nullptr},
      {"limit", "Max ledgers to list", false, "10"}}},

    // Maintenance tools
    {"cycle", "Run maintenance (decay, synthesis)",
     {{"force", "Force full cycle", false, "false"},
      {"regenerate_embeddings", "Regenerate zero-vector embeddings", false, "false"},
      {"batch_size", "Batch size for regeneration", false, "100"}}},

    {"version_check", "Check version compatibility",
     {}},

    // Analysis tools
    {"epistemic_state", "What I know vs uncertain about",
     {{"domain", "Filter by domain", false, nullptr}}},

    {"bias_scan", "Detect belief patterns and skews",
     {{"limit", "Max nodes to scan", false, "100"}}},

    {"propagate", "Propagate confidence change through graph",
     {{"id", "Node ID to propagate from", true, nullptr},
      {"delta", "Confidence change (-0.5 to 0.5)", true, nullptr},
      {"decay_factor", "Decay per hop (0.1-0.9)", false, "0.5"},
      {"max_depth", "Max propagation depth (1-5)", false, "3"}}},

    {"forget", "Deliberately forget a node",
     {{"id", "Node ID to forget", true, nullptr},
      {"cascade", "Weaken connected nodes", false, "true"},
      {"rewire", "Reconnect edges around", false, "true"},
      {"cascade_strength", "Cascade decay (0.05-0.3)", false, "0.1"}}},

    {"competence", "Track strengths/weaknesses by domain",
     {{"domain", "Specific domain to query", false, nullptr}}},

    {"cross_project", "Query knowledge across projects",
     {{"query", "What to search for", true, nullptr},
      {"source_project", "Project to transfer FROM", false, nullptr},
      {"target_project", "Project to transfer TO", false, nullptr},
      {"limit", "Max results", false, "10"}}},

    // Yajna tools
    {"yajna_list", "List verbose nodes for epsilon-yajna",
     {{"query", "Domain filter", false, "architecture system pattern decision"},
      {"limit", "Max results", false, "10"},
      {"min_length", "Min content length", false, "200"}}},

    {"yajna_inspect", "Get complete node content by ID",
     {{"id", "Node UUID to inspect", true, nullptr}}},

    {"tag", "Add or remove tags from a node",
     {{"id", "Node UUID", true, nullptr},
      {"add", "Tag to add", false, nullptr},
      {"remove", "Tag to remove", false, nullptr}}},

    // Phase 7: Realm tools
    {"realm_get", "Get current realm context",
     {}},

    {"realm_set", "Set current realm (persists across sessions)",
     {{"realm", "Realm name (e.g., 'project:cc-soul')", true, nullptr}}},

    {"realm_create", "Create a new realm with optional parent",
     {{"name", "Realm name (e.g., 'project:my-project')", true, nullptr},
      {"parent", "Parent realm (default: brahman)", false, "brahman"}}},

    // Phase 7: Review tools
    {"review_list", "List items in review queue",
     {{"status", "Filter: pending|approved|rejected|deferred|all", false, "pending"},
      {"limit", "Max items to return", false, "10"}}},

    {"review_decide", "Make a review decision on an item",
     {{"id", "Node ID to review", true, nullptr},
      {"decision", "Decision: approve|reject|edit|defer", true, nullptr},
      {"comment", "Optional comment", false, nullptr},
      {"edited_content", "New content (for edit)", false, nullptr},
      {"quality_rating", "Quality rating 0-5 (for approve)", false, "3"}}},

    {"review_batch", "Batch review: apply same decision to multiple items",
     {{"decision", "Decision: approve|reject|defer", true, nullptr},
      {"ids", "Comma-separated node IDs (empty = pending items)", false, nullptr},
      {"limit", "Max items if ids empty", false, "10"},
      {"comment", "Comment for all decisions", false, nullptr},
      {"quality_rating", "Quality rating 0-5", false, "3"}}},

    {"review_stats", "Get review queue statistics",
     {}},

    // Phase 7: Eval tools
    {"eval_run", "Run golden recall test suite",
     {{"test_name", "Specific test to run (empty = all)", false, nullptr}}},

    {"eval_add_test", "Add a test case to eval harness",
     {{"name", "Test name", true, nullptr},
      {"query", "Query to test", true, nullptr},
      {"expected", "Comma-separated expected node IDs", true, nullptr}}},

    // Phase 7: Epiplexity tools
    {"epiplexity_check", "Check compression quality of nodes",
     {{"node_ids", "Comma-separated IDs (empty = sample)", false, nullptr},
      {"sample_size", "Nodes to sample if no IDs", false, "10"}}},

    {"epiplexity_drift", "Analyze epsilon drift over time",
     {{"lookback_days", "Days to analyze", false, "7"}}},
};

// Build set of known tools from specs
static std::set<std::string> build_known_tools() {
    std::set<std::string> tools;
    for (const auto& spec : TOOL_SPECS) {
        tools.insert(spec.name);
    }
    return tools;
}

static const std::set<std::string> KNOWN_TOOLS = build_known_tools();

// Find tool spec by name
static const ToolSpec* find_tool_spec(const std::string& name) {
    for (const auto& spec : TOOL_SPECS) {
        if (spec.name == name) return &spec;
    }
    return nullptr;
}

// Print help for a specific tool
static void print_tool_help(const std::string& tool) {
    const ToolSpec* spec = find_tool_spec(tool);
    if (!spec) {
        std::cerr << "Unknown tool: " << tool << "\n";
        return;
    }

    std::cerr << "chitta " << spec->name << " - " << spec->description << "\n\n";

    if (spec->params.empty()) {
        std::cerr << "  No parameters required.\n";
        return;
    }

    std::cerr << "Parameters:\n";
    for (const auto& p : spec->params) {
        std::cerr << "  --" << p.name;
        if (p.required) {
            std::cerr << " (required)";
        } else if (p.default_val) {
            std::cerr << " [default: " << p.default_val << "]";
        }
        std::cerr << "\n      " << p.description << "\n";
    }

    // Show example
    std::cerr << "\nExample:\n  chitta " << spec->name;
    for (const auto& p : spec->params) {
        if (p.required) {
            std::cerr << " --" << p.name << " \"...\"";
        }
    }
    std::cerr << "\n";
}

// Get program name from path (strip directory)
static const char* prog_name(const char* path) {
    const char* last = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '/') last = p + 1;
    }
    return last;
}

void print_usage(const char* prog) {
    const char* name = prog_name(prog);
    std::cerr << "Usage:\n"
              << "  " << name << " <tool> --param value ...   Invoke tool\n"
              << "  " << name << " <tool> --help              Show tool parameters\n"
              << "  " << name << " [options]                  Interactive mode (JSON-RPC)\n"
              << "\n"
              << "Examples:\n"
              << "  " << name << " recall --query \"search terms\" --zoom sparse\n"
              << "  " << name << " soul_context\n"
              << "  " << name << " observe --category decision --title \"...\" --content \"...\"\n"
              << "  " << name << " grow --type wisdom --content \"...\" --title \"...\"\n"
              << "  " << name << " yajna_inspect --id \"uuid\"\n"
              << "\n"
              << "Tool categories:\n"
              << "  Memory:    recall, resonate, full_resonate, recall_by_tag, multi_hop, timeline\n"
              << "  Learning:  grow, observe, update, feedback, connect, query, import_soul, export_soul\n"
              << "  Entity:    resolve_entity, link_entity, bootstrap_entity_index, list_entities\n"
              << "  Context:   soul_context, attractors, lens, lens_harmony\n"
              << "  Intention: intend, wonder, answer\n"
              << "  Narrative: narrate, ledger\n"
              << "  Analysis:  epistemic_state, bias_scan, propagate, forget, competence\n"
              << "  Yajna:     yajna_list, yajna_inspect, tag\n"
              << "  Realm:     realm_get, realm_set, realm_create\n"
              << "  Review:    review_list, review_decide, review_batch, review_stats\n"
              << "  Eval:      eval_run, eval_add_test, epiplexity_check, epiplexity_drift\n"
              << "\n"
              << "Global options:\n"
              << "  --socket-path PATH  Unix socket path\n"
              << "  --json              Output raw JSON instead of text\n"
              << "  --help              Show this help message\n";
}

// CLI mode: invoke tool directly
int run_cli(const std::string& socket_path, const std::string& tool,
            int argc, char* argv[], int arg_start, bool json_output) {
    using json = nlohmann::json;

    // Check for --help first
    for (int i = arg_start; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_tool_help(tool);
            return 0;
        }
    }

    // Build arguments JSON from command line (all named, no positional)
    json args = json::object();

    for (int i = arg_start; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg.rfind("--", 0) == 0) {
            // Named argument: --key value
            std::string key = arg.substr(2);
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                std::string value = argv[++i];
                // Try to parse as JSON object/array, number, or boolean
                if (value == "true") {
                    args[key] = true;
                } else if (value == "false") {
                    args[key] = false;
                } else if (!value.empty() && (value[0] == '{' || value[0] == '[')) {
                    // Try to parse as JSON object or array
                    try {
                        args[key] = json::parse(value);
                    } catch (...) {
                        args[key] = value;  // Fall back to string
                    }
                } else {
                    // Only parse as number if entire string is numeric
                    bool is_numeric = !value.empty();
                    bool has_dot = false;
                    for (size_t j = 0; j < value.size(); ++j) {
                        char c = value[j];
                        if (c == '-' && j == 0) continue;  // Leading minus OK
                        if (c == '.' && !has_dot) { has_dot = true; continue; }
                        if (!std::isdigit(c)) { is_numeric = false; break; }
                    }
                    if (is_numeric && !value.empty()) {
                        try {
                            if (has_dot) {
                                args[key] = std::stod(value);
                            } else {
                                args[key] = std::stoll(value);
                            }
                        } catch (...) {
                            args[key] = value;
                        }
                    } else {
                        args[key] = value;
                    }
                }
            } else {
                args[key] = true;  // Flag without value
            }
        } else {
            // Positional argument - show help instead of silently ignoring
            std::cerr << "Error: Unexpected positional argument: " << arg << "\n";
            std::cerr << "All arguments must be named (--param value).\n\n";
            print_tool_help(tool);
            return 1;
        }
    }

    // Validate required parameters
    const ToolSpec* spec = find_tool_spec(tool);
    if (spec) {
        std::vector<std::string> missing;
        for (const auto& p : spec->params) {
            if (p.required && !args.contains(p.name)) {
                missing.push_back(p.name);
            }
        }
        if (!missing.empty()) {
            std::cerr << "Error: Missing required parameter(s): ";
            for (size_t i = 0; i < missing.size(); ++i) {
                if (i > 0) std::cerr << ", ";
                std::cerr << "--" << missing[i];
            }
            std::cerr << "\n\n";
            print_tool_help(tool);
            return 1;
        }
    }

    // Connect to daemon (safe mode: never kill/restart)
    chitta::SocketClient client(socket_path);
    if (!client.connect_only()) {
        std::cerr << "Error: " << client.last_error() << "\n";
        std::cerr << "Hint: Start daemon with 'chittad daemon --socket' or let hooks start it\n";
        return 1;
    }

    // Send initialize
    json init_req = {
        {"jsonrpc", "2.0"},
        {"method", "initialize"},
        {"params", {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", json::object()},
            {"clientInfo", {{"name", "chittad"}, {"version", CHITTA_VERSION}}}
        }},
        {"id", 0}
    };
    auto init_resp = client.request(init_req.dump());
    if (!init_resp) {
        std::cerr << "Error: Initialize failed: " << client.last_error() << "\n";
        return 1;
    }

    // Send tool call
    json tool_req = {
        {"jsonrpc", "2.0"},
        {"method", "tools/call"},
        {"params", {
            {"name", tool},
            {"arguments", args}
        }},
        {"id", 1}
    };

    auto resp = client.request(tool_req.dump());
    if (!resp) {
        std::cerr << "Error: Tool call failed: " << client.last_error() << "\n";
        return 1;
    }

    // Parse and output result
    try {
        auto result = json::parse(*resp);

        if (result.contains("error")) {
            std::cerr << "Error: " << result["error"]["message"].get<std::string>() << "\n";
            return 1;
        }

        if (json_output) {
            // Raw JSON output
            if (result.contains("result") && result["result"].contains("structured")) {
                std::cout << result["result"]["structured"].dump(2) << "\n";
            } else {
                std::cout << result.dump(2) << "\n";
            }
        } else {
            // Text output
            if (result.contains("result") && result["result"].contains("content")) {
                auto& content = result["result"]["content"];
                if (content.is_array() && !content.empty() && content[0].contains("text")) {
                    std::cout << content[0]["text"].get<std::string>() << "\n";
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing response: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

/// Thin client mode: forward stdin → daemon → stdout
int run_thin_client(const std::string& socket_path) {
    chitta::SocketClient client(socket_path);

    // Safe connect: never kill/restart daemon
    if (!client.connect_only()) {
        std::cerr << "[chitta] " << client.last_error() << "\n";
        std::cerr << "[chitta] Hint: Start daemon with 'chittad daemon --socket'\n";
        return 1;
    }

    std::cerr << "[chitta] Connected to daemon at " << socket_path << "\n";
    std::cerr << "[chitta] Listening on stdin...\n";

    // Forward requests
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        auto response = client.request(line);
        if (response) {
            std::cout << *response << "\n";
            std::cout.flush();
        } else {
            std::cerr << "[chitta] Request failed: " << client.last_error() << "\n";

            // Try to reconnect (safe: don't restart daemon)
            client.disconnect();
            if (!client.connect_only()) {
                std::cerr << "[chitta] Reconnection failed: " << client.last_error() << "\n";
                return 1;
            }
            std::cerr << "[chitta] Reconnected to daemon\n";

            // Retry the request
            response = client.request(line);
            if (response) {
                std::cout << *response << "\n";
                std::cout.flush();
            } else {
                // Return error to client
                std::cout << R"({"jsonrpc":"2.0","error":{"code":-32603,"message":"Daemon connection lost"},"id":null})" << "\n";
                std::cout.flush();
            }
        }
    }

    std::cerr << "[chitta] Shutdown complete\n";
    return 0;
}

int main(int argc, char* argv[]) {
    std::string socket_path = chitta::SocketClient::default_socket_path();
    bool json_output = false;

    // Handle status command (daemon health check)
    if (argc > 1 && std::strcmp(argv[1], "status") == 0) {
        chitta::SocketClient client(socket_path);
        if (!client.connect()) {
            std::cout << "Daemon: not running\n";
            std::cout << "Socket: " << socket_path << " (not found)\n";
            return 1;
        }
        auto version = client.check_version();
        if (version) {
            std::cout << "Daemon: running\n";
            std::cout << "Socket: " << socket_path << "\n";
            std::cout << "Version: " << version->software << "\n";
            std::cout << "Protocol: " << version->protocol_major << "." << version->protocol_minor << "\n";
            return 0;
        }
        std::cout << "Daemon: running (version unknown)\n";
        return 0;
    }

    // Handle shutdown command specially (not a tool, direct daemon control)
    if (argc > 1 && std::strcmp(argv[1], "shutdown") == 0) {
        chitta::SocketClient client(socket_path);
        if (!client.connect()) {
            std::cerr << "No daemon running\n";
            return 1;
        }
        if (client.request_shutdown()) {
            std::cout << "Daemon shutdown requested\n";
            if (client.wait_for_socket_gone(5000)) {
                std::cout << "Daemon stopped\n";
            }
            return 0;
        }
        std::cerr << "Failed to request shutdown\n";
        return 1;
    }

    // Check for CLI mode: first arg is a known tool name
    if (argc > 1 && KNOWN_TOOLS.count(argv[1])) {
        std::string tool = argv[1];

        // Check for --json flag anywhere in args
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--json") == 0) {
                json_output = true;
            }
        }

        return run_cli(socket_path, tool, argc, argv, 2, json_output);
    }

    // Parse arguments for interactive mode
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--socket-path") == 0 && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    return run_thin_client(socket_path);
}
