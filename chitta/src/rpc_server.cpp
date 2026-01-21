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
#include <chitta/mind/duckdb_mind.hpp>
#include <chitta/rpc/duckdb_handler.hpp>
#ifdef CHITTA_WITH_ONNX
#include <chitta/vak_onnx.hpp>
#endif
#include <fstream>
#include <array>
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

// Tool specifications - only includes tools implemented in daemon
static const std::vector<ToolSpec> TOOL_SPECS = {
    // Core memory tools
    {"remember", "Store text in memory with optional tags",
     {{"content", "Text to remember", true, nullptr},
      {"type", "Node type: wisdom|belief|episode", false, "episode"},
      {"tags", "Comma-separated tags", false, nullptr},
      {"realm", "Primary realm (default: brahman)", false, nullptr},
      {"visibility", "0=Private, 1=Shared, 2=Global", false, "0"}}},

    {"recall", "Search memory by semantic similarity",
     {{"query", "Search query", true, nullptr},
      {"limit", "Max results", false, "10"},
      {"tag", "Filter by tag", false, nullptr},
      {"realm", "Filter by realm (empty = all visible)", false, nullptr}}},

    {"grow", "Add wisdom, belief, failure, aspiration, or dream",
     {{"type", "Type: wisdom|belief|failure|aspiration|dream", true, nullptr},
      {"content", "Content to store", true, nullptr},
      {"title", "Short title", false, nullptr},
      {"tags", "Comma-separated tags", false, nullptr},
      {"realm", "Primary realm (default: brahman)", false, nullptr},
      {"visibility", "0=Private, 1=Shared, 2=Global", false, "0"}}},

    {"get", "Get a node by ID",
     {{"id", "Node ID", true, nullptr}}},

    {"update", "Update node content",
     {{"id", "Node ID", true, nullptr},
      {"content", "New content", true, nullptr}}},

    {"forget", "Remove a memory",
     {{"id", "Node ID to forget", true, nullptr}}},

    {"strengthen", "Increase confidence of a memory",
     {{"id", "Node ID", true, nullptr},
      {"amount", "Amount to strengthen (0-1)", false, "0.1"}}},

    {"weaken", "Decrease confidence of a memory",
     {{"id", "Node ID", true, nullptr},
      {"amount", "Amount to weaken (0-1)", false, "0.1"}}},

    {"tag", "Add or remove tags from a node",
     {{"id", "Node ID", true, nullptr},
      {"add", "Tag to add", false, nullptr},
      {"remove", "Tag to remove", false, nullptr}}},

    // Graph/Triplet tools
    {"connect", "Create triplet: subject → predicate → object",
     {{"subject", "Subject entity", true, nullptr},
      {"predicate", "Relationship type", true, nullptr},
      {"object", "Object entity", true, nullptr}}},

    {"query", "Query triplets with flexible filters",
     {{"subject", "Subject filter", false, nullptr},
      {"predicate", "Predicate filter", false, nullptr},
      {"object", "Object filter", false, nullptr}}},

    {"query_graph", "Query triplets by subject or object",
     {{"subject", "Query by subject", false, nullptr},
      {"object", "Query by object", false, nullptr}}},

    // Hook tools
    {"observe", "Store observation (used by hooks for [LEARN] extraction)",
     {{"title", "Short title", true, nullptr},
      {"content", "Full content", true, nullptr},
      {"category", "Category: wisdom|insight|signal|episode", false, "episode"},
      {"tags", "Comma-separated tags", false, nullptr},
      {"realm", "Primary realm (default: brahman)", false, nullptr}}},

    {"full_resonate", "Semantic search with full context (for hooks)",
     {{"query", "Search query", true, nullptr},
      {"k", "Max results", false, "10"},
      {"realm", "Filter by realm (empty = all visible)", false, nullptr}}},

    // Context tools
    {"soul_context", "Get current soul state and statistics",
     {}},

    {"health_check", "Check daemon health and readiness",
     {}},

    {"version_check", "Get version information",
     {}},

    // Maintenance tools
    {"cycle", "Run maintenance cycle (decay, cleanup)",
     {{"force", "Force full cycle", false, "false"}}},

    {"cleanup", "Remove weak/garbage nodes",
     {{"dry_run", "Preview only", false, "true"}}},

    // Import/Export tools
    {"import_soul", "Import .soul file (SSL format)",
     {{"file", "Path to .soul file", false, nullptr},
      {"content", "SSL content (alternative to file)", false, nullptr}}},

    {"export_soul", "Export memories to SSL format",
     {{"file", "Output file path", false, nullptr},
      {"tag", "Filter by tag", false, nullptr},
      {"limit", "Max nodes to export", false, "100"}}},

    // Code intelligence tools
    {"extract_symbols", "Extract symbols from source file using tree-sitter",
     {{"path", "File path to analyze", true, nullptr}}},

    {"learn_codebase", "Learn codebase by extracting all symbols",
     {{"path", "Directory path to analyze", true, nullptr},
      {"project", "Project name (auto-detected if empty)", false, nullptr},
      {"max_files", "Max files to process", false, "500"},
      {"exclude", "Comma-separated directories to exclude", false, nullptr}}},

    {"find_symbol", "Search for symbols by name",
     {{"name", "Symbol name to search", true, nullptr},
      {"kind", "Symbol kind filter (function, class, method)", false, nullptr}}},

    {"code_context", "Get code context summary",
     {{"path", "Limit to files under this path", false, nullptr}}},

    // Realm tools
    {"realm_list", "List all known realms",
     {}},

    {"realm_get", "Get realm for a memory",
     {{"id", "Memory ID", true, nullptr}}},

    {"realm_set", "Set primary realm for a memory",
     {{"id", "Memory ID", true, nullptr},
      {"realm", "Realm name", true, nullptr}}},

    {"realm_add", "Add memory to additional realm",
     {{"id", "Memory ID", true, nullptr},
      {"realm", "Realm name to add", true, nullptr}}},

    {"realm_remove", "Remove memory from realm",
     {{"id", "Memory ID", true, nullptr},
      {"realm", "Realm name to remove", true, nullptr}}},

    {"realm_visibility", "Set realm visibility for a memory",
     {{"id", "Memory ID", true, nullptr},
      {"visibility", "Visibility: private|shared|global", true, nullptr}}},

    // Ledger tools (session continuity)
    {"ledger_save", "Save session checkpoint for continuity",
     {{"session_id", "Session identifier", true, nullptr},
      {"project", "Project scope", false, "default"},
      {"mood", "Current feeling: confident|uncertain|flowing|frustrated", false, nullptr},
      {"coherence", "Coherence score 0-1", false, nullptr},
      {"confidence", "Confidence score 0-1", false, nullptr},
      {"todos", "JSON array of {content, status} objects", false, nullptr},
      {"active_files", "JSON array of file paths", false, nullptr},
      {"decisions", "JSON array of key decisions", false, nullptr},
      {"next_steps", "JSON array of next steps", false, nullptr},
      {"blockers", "JSON array of blockers", false, nullptr},
      {"discoveries", "JSON array of discoveries", false, nullptr},
      {"snapshot", "Full checkpoint text for reconstruction", false, nullptr}}},

    {"ledger_load", "Load most recent checkpoint",
     {{"session_id", "Session identifier (optional)", false, nullptr},
      {"project", "Project filter (optional)", false, nullptr}}},

    {"ledger_list", "List recent checkpoints",
     {{"project", "Project filter (optional)", false, nullptr},
      {"limit", "Max entries to return", false, "10"}}},

    {"ledger_get", "Get specific checkpoint by ID",
     {{"id", "Checkpoint ID", true, nullptr}}},

    {"ledger_delete", "Delete checkpoint",
     {{"id", "Checkpoint ID to delete", true, nullptr}}},
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
              << "  " << name << " recall --query \"search terms\"\n"
              << "  " << name << " soul_context\n"
              << "  " << name << " observe --title \"Decision\" --content \"Chose X over Y\"\n"
              << "  " << name << " grow --type wisdom --content \"Pattern discovered\"\n"
              << "  " << name << " learn_codebase --path /path/to/project\n"
              << "\n"
              << "Tool categories:\n"
              << "  Memory:      remember, recall, grow, get, update, forget, strengthen, weaken, tag\n"
              << "  Triplets:    connect, query, query_graph\n"
              << "  Hooks:       observe, full_resonate\n"
              << "  Context:     soul_context, health_check, version_check\n"
              << "  Maintenance: cycle, cleanup\n"
              << "  Import/Export: import_soul, export_soul\n"
              << "  Code Intel:  extract_symbols, learn_codebase, find_symbol, code_context\n"
              << "  Realm:       realm_detect, realm_list, realm_get, realm_set, realm_add, realm_remove, realm_visibility\n"
              << "  Ledger:      ledger_save, ledger_load, ledger_list, ledger_get, ledger_delete\n"
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
        std::cerr << "Hint: Start daemon with 'chittad daemon' or let hooks start it\n";
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
        std::cerr << "[chitta] Hint: Start daemon with 'chittad daemon'\n";
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

// Detect realm from environment/git/config
// Priority: CHITTA_REALM env > .cc-soul-realm file > git repo name > "brahman"
static std::string detect_realm() {
    // 1. Environment variable
    if (const char* env_realm = std::getenv("CHITTA_REALM")) {
        return env_realm;
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
            return realm;
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
        return "project:" + repo_name;
    }

    // 4. Default
    return "brahman";
}

int main(int argc, char* argv[]) {
    std::string socket_path = chitta::SocketClient::default_socket_path();
    bool json_output = false;
    std::string tool;
    int tool_arg_index = 0;

    // Pre-scan for --socket-path and --json flags, find tool name
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
            std::cout << "chitta " << CHITTA_VERSION << "\n";
            return 0;
        } else if (std::strcmp(argv[i], "--socket-path") == 0 && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (std::strcmp(argv[i], "--json") == 0) {
            json_output = true;
        } else if (tool.empty() && argv[i][0] != '-') {
            tool = argv[i];
            tool_arg_index = i;
        }
    }

    // Handle realm_detect command (client-side, no daemon needed)
    if (tool == "realm_detect") {
        std::string realm = detect_realm();
        if (json_output) {
            std::cout << "{\"realm\":\"" << realm << "\"}\n";
        } else {
            std::cout << realm << "\n";
        }
        return 0;
    }

    // Handle status command (daemon health check)
    if (tool == "status") {
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
    if (tool == "shutdown") {
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

    // Check for --help without a tool
    if (tool.empty()) {
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
                print_usage(argv[0]);
                return 0;
            }
        }
    }

    // Handle MCP mode (JSON-RPC bridge to daemon via socket)
    if (tool == "mcp") {
        // MCP mode: connect to daemon via socket, forward JSON-RPC
        chitta::SocketClient client(socket_path);

        if (!client.connect()) {
            // Daemon not running - output MCP error
            nlohmann::json error;
            error["jsonrpc"] = "2.0";
            error["error"]["code"] = -32603;
            error["error"]["message"] = "Daemon not running. Start with: chittad daemon";
            error["id"] = nullptr;
            std::cout << error.dump() << std::endl;
            return 1;
        }

        // Read JSON-RPC from stdin, forward to daemon, write response to stdout
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;

            try {
                auto mcp_request = nlohmann::json::parse(line);
                std::string method = mcp_request.value("method", "");
                auto request_id = mcp_request.value("id", nlohmann::json());

                // Handle MCP protocol methods
                if (method == "initialize") {
                    // MCP initialization response
                    nlohmann::json response;
                    response["jsonrpc"] = "2.0";
                    response["result"]["protocolVersion"] = "2024-11-05";
                    response["result"]["capabilities"]["tools"] = nlohmann::json::object();
                    response["result"]["serverInfo"]["name"] = "chitta";
                    response["result"]["serverInfo"]["version"] = "3.2.0";
                    response["id"] = request_id;
                    std::cout << response.dump() << std::endl;
                } else if (method == "notifications/initialized") {
                    // No response needed for notifications
                    continue;
                } else if (method == "tools/list") {
                    // Forward to daemon
                    nlohmann::json daemon_req;
                    daemon_req["jsonrpc"] = "2.0";
                    daemon_req["id"] = 1;
                    daemon_req["method"] = "tools/list";
                    daemon_req["params"] = nlohmann::json::object();

                    auto result_str = client.request(daemon_req.dump());
                    if (result_str) {
                        auto daemon_resp = nlohmann::json::parse(*result_str);
                        auto tools = daemon_resp.value("result", nlohmann::json::object()).value("tools", nlohmann::json::array());

                        nlohmann::json response;
                        response["jsonrpc"] = "2.0";
                        response["result"]["tools"] = tools;
                        response["id"] = request_id;
                        std::cout << response.dump() << std::endl;
                    }
                } else if (method == "tools/call") {
                    // Forward tool call to daemon
                    auto params = mcp_request.value("params", nlohmann::json::object());
                    std::string tool_name = params.value("name", "");
                    auto arguments = params.value("arguments", nlohmann::json::object());

                    nlohmann::json daemon_req;
                    daemon_req["jsonrpc"] = "2.0";
                    daemon_req["id"] = 1;
                    daemon_req["method"] = "tools/call";
                    daemon_req["params"]["name"] = tool_name;
                    daemon_req["params"]["arguments"] = arguments;

                    auto result_str = client.request(daemon_req.dump());
                    if (result_str) {
                        auto daemon_resp = nlohmann::json::parse(*result_str);
                        auto result = daemon_resp.value("result", nlohmann::json::object());

                        // Daemon already returns MCP-compatible content array
                        // Just pass it through
                        nlohmann::json response;
                        response["jsonrpc"] = "2.0";
                        response["result"]["content"] = result.value("content", nlohmann::json::array());
                        response["id"] = request_id;
                        std::cout << response.dump() << std::endl;
                    }
                } else {
                    // Unknown method
                    nlohmann::json response;
                    response["jsonrpc"] = "2.0";
                    response["error"]["code"] = -32601;
                    response["error"]["message"] = "Method not found: " + method;
                    response["id"] = request_id;
                    std::cout << response.dump() << std::endl;
                }
            } catch (const std::exception& e) {
                nlohmann::json error;
                error["jsonrpc"] = "2.0";
                error["error"]["code"] = -32700;
                error["error"]["message"] = e.what();
                error["id"] = nullptr;
                std::cout << error.dump() << std::endl;
            }
        }

        return 0;
    }

    // Check for CLI mode: tool is a known tool name
    if (!tool.empty() && KNOWN_TOOLS.count(tool)) {
        return run_cli(socket_path, tool, argc, argv, tool_arg_index + 1, json_output);
    }

    // No tool specified - run interactive mode or show usage
    if (tool.empty()) {
        return run_thin_client(socket_path);
    }

    // Unknown tool
    std::cerr << "Unknown option: " << tool << "\n";
    print_usage(argv[0]);
    return 1;
}
