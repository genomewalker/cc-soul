#pragma once
// MCP Server: Model Context Protocol for soul integration
//
// Implements JSON-RPC 2.0 over stdio for Claude integration.
// This is not a minimal implementation - it is a proper MCP server
// with full protocol compliance and rich tool schemas.

#include "mind.hpp"
#include "voice.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <functional>
#include <atomic>
#include <unistd.h>
#include <climits>

namespace chitta {

using json = nlohmann::json;

// JSON-RPC 2.0 error codes
namespace rpc_error {
    constexpr int PARSE_ERROR = -32700;
    constexpr int INVALID_REQUEST = -32600;
    constexpr int METHOD_NOT_FOUND = -32601;
    constexpr int INVALID_PARAMS = -32602;
    constexpr int INTERNAL_ERROR = -32603;
    // MCP-specific errors
    constexpr int TOOL_NOT_FOUND = -32001;
    constexpr int TOOL_EXECUTION_ERROR = -32002;
}

// NodeType to string conversion
inline std::string node_type_to_string(NodeType type) {
    switch (type) {
        case NodeType::Wisdom: return "wisdom";
        case NodeType::Belief: return "belief";
        case NodeType::Intention: return "intention";
        case NodeType::Aspiration: return "aspiration";
        case NodeType::Episode: return "episode";
        case NodeType::Operation: return "operation";
        case NodeType::Invariant: return "invariant";
        case NodeType::Identity: return "identity";
        case NodeType::Term: return "term";
        case NodeType::Failure: return "failure";
        case NodeType::Dream: return "dream";
        case NodeType::Voice: return "voice";
        case NodeType::Meta: return "meta";
        case NodeType::Gap: return "gap";
        case NodeType::Question: return "question";
        case NodeType::StoryThread: return "story_thread";
        case NodeType::Ledger: return "ledger";
        case NodeType::Entity: return "entity";
        default: return "unknown";
    }
}

inline NodeType string_to_node_type(const std::string& s) {
    if (s == "wisdom") return NodeType::Wisdom;
    if (s == "belief") return NodeType::Belief;
    if (s == "intention") return NodeType::Intention;
    if (s == "aspiration") return NodeType::Aspiration;
    if (s == "episode") return NodeType::Episode;
    if (s == "operation") return NodeType::Operation;
    if (s == "invariant") return NodeType::Invariant;
    if (s == "identity") return NodeType::Identity;
    if (s == "term") return NodeType::Term;
    if (s == "failure") return NodeType::Failure;
    if (s == "dream") return NodeType::Dream;
    if (s == "voice") return NodeType::Voice;
    if (s == "meta") return NodeType::Meta;
    if (s == "gap") return NodeType::Gap;
    if (s == "question") return NodeType::Question;
    if (s == "story_thread") return NodeType::StoryThread;
    if (s == "ledger") return NodeType::Ledger;
    if (s == "entity") return NodeType::Entity;
    return NodeType::Episode;
}

// Tool schema definition
struct ToolSchema {
    std::string name;
    std::string description;
    json input_schema;
};

// Tool result
struct ToolResult {
    bool is_error = false;
    std::string content;
    json structured;
};

// MCP Server implementation
class MCPServer {
public:
    explicit MCPServer(std::shared_ptr<Mind> mind, std::string server_name = "chitta")
        : mind_(std::move(mind))
        , server_name_(std::move(server_name))
        , running_(false)
    {
        register_tools();
    }

    void run() {
        running_ = true;
        std::string line;

        while (running_ && std::getline(std::cin, line)) {
            if (line.empty()) continue;

            try {
                auto request = json::parse(line);
                auto response = handle_request(request);
                if (!response.is_null()) {
                    std::cout << response.dump() << "\n";
                    std::cout.flush();
                }
            } catch (const json::parse_error& e) {
                auto error = make_error(nullptr, rpc_error::PARSE_ERROR,
                                        std::string("Parse error: ") + e.what());
                std::cout << error.dump() << "\n";
                std::cout.flush();
            }
        }
    }

    void stop() { running_ = false; }

private:
    std::shared_ptr<Mind> mind_;
    std::string server_name_;
    std::atomic<bool> running_;
    std::vector<ToolSchema> tools_;
    std::unordered_map<std::string, std::function<ToolResult(const json&)>> handlers_;

    void register_tools() {
        // Tool: soul_context - Get soul state for hook injection
        tools_.push_back({
            "soul_context",
            "Get soul context including beliefs, active intentions, relevant wisdom, coherence, and session ledger. "
            "Use format='json' for structured data or 'text' for hook injection.",
            {
                {"type", "object"},
                {"properties", {
                    {"query", {
                        {"type", "string"},
                        {"description", "Optional query to find relevant wisdom"}
                    }},
                    {"format", {
                        {"type", "string"},
                        {"enum", {"text", "json"}},
                        {"default", "text"},
                        {"description", "Output format - 'text' for hook injection or 'json' for structured"}
                    }},
                    {"include_ledger", {
                        {"type", "boolean"},
                        {"default", true},
                        {"description", "Include session ledger (Atman snapshot) in context"}
                    }}
                }},
                {"required", json::array()}
            }
        });
        handlers_["soul_context"] = [this](const json& params) { return tool_soul_context(params); };

        // Tool: grow - Add wisdom, beliefs, or failures to the soul
        tools_.push_back({
            "grow",
            "Add to the soul: wisdom, beliefs, failures, aspirations, dreams, terms, or entities. "
            "Each type has different decay and confidence properties. "
            "Entity type is for named things like code files, projects, concepts.",
            {
                {"type", "object"},
                {"properties", {
                    {"type", {
                        {"type", "string"},
                        {"enum", {"wisdom", "belief", "failure", "aspiration", "dream", "term", "entity"}},
                        {"description", "What to grow"}
                    }},
                    {"content", {
                        {"type", "string"},
                        {"description", "The content/statement to add"}
                    }},
                    {"title", {
                        {"type", "string"},
                        {"description", "Short title (required for wisdom/failure)"}
                    }},
                    {"domain", {
                        {"type", "string"},
                        {"description", "Domain context (optional)"}
                    }},
                    {"confidence", {
                        {"type", "number"},
                        {"minimum", 0.0},
                        {"maximum", 1.0},
                        {"default", 0.8},
                        {"description", "Initial confidence (0-1)"}
                    }}
                }},
                {"required", {"type", "content"}}
            }
        });
        handlers_["grow"] = [this](const json& params) { return tool_grow(params); };

        // Tool: observe - Record an episodic observation
        tools_.push_back({
            "observe",
            "Record an observation (episode). Categories determine decay rate: "
            "bugfix/decision (slow), discovery/feature (medium), session_ledger/signal (fast).",
            {
                {"type", "object"},
                {"properties", {
                    {"category", {
                        {"type", "string"},
                        {"enum", {"bugfix", "decision", "discovery", "feature", "refactor", "session_ledger", "signal"}},
                        {"description", "Category affecting decay rate"}
                    }},
                    {"title", {
                        {"type", "string"},
                        {"maxLength", 80},
                        {"description", "Short title (max 80 chars)"}
                    }},
                    {"content", {
                        {"type", "string"},
                        {"description", "Full observation content"}
                    }},
                    {"project", {
                        {"type", "string"},
                        {"description", "Project name (optional)"}
                    }},
                    {"tags", {
                        {"type", "string"},
                        {"description", "Comma-separated tags for filtering"}
                    }}
                }},
                {"required", {"category", "title", "content"}}
            }
        });
        handlers_["observe"] = [this](const json& params) { return tool_observe(params); };

        // Tool: recall - Semantic search in soul with zoom levels
        tools_.push_back({
            "recall",
            "Recall relevant wisdom and episodes. "
            "zoom='sparse' for overview (20+ titles), 'normal' for balanced (5-10 full), "
            "'dense' for deep context (3-5 with relationships and temporal info), "
            "'full' for complete untruncated content (1-3 results). "
            "When learn=true, applies Hebbian learning to strengthen connections between co-retrieved nodes. "
            "When primed=true, boosts results based on session context (recent observations, active intentions, goal basin).",
            {
                {"type", "object"},
                {"properties", {
                    {"query", {
                        {"type", "string"},
                        {"description", "What to search for (semantic)"}
                    }},
                    {"zoom", {
                        {"type", "string"},
                        {"enum", {"sparse", "normal", "dense", "full"}},
                        {"default", "normal"},
                        {"description", "Detail level: sparse (titles, 20+), normal (text, 5-10), dense (context, 3-5), full (complete content, 1-3)"}
                    }},
                    {"tag", {
                        {"type", "string"},
                        {"description", "Filter by exact tag match (e.g., 'thread:abc123')"}
                    }},
                    {"limit", {
                        {"type", "integer"},
                        {"minimum", 1},
                        {"maximum", 100},
                        {"description", "Override default limit for zoom level"}
                    }},
                    {"threshold", {
                        {"type", "number"},
                        {"minimum", 0.0},
                        {"maximum", 1.0},
                        {"default", 0.0},
                        {"description", "Minimum similarity threshold"}
                    }},
                    {"learn", {
                        {"type", "boolean"},
                        {"default", false},
                        {"description", "Apply Hebbian learning: strengthen connections between co-retrieved nodes"}
                    }},
                    {"primed", {
                        {"type", "boolean"},
                        {"default", false},
                        {"description", "Session priming: boost results based on recent observations and active intentions"}
                    }},
                    {"compete", {
                        {"type", "boolean"},
                        {"default", true},
                        {"description", "Lateral inhibition: similar results compete, winners suppress losers"}
                    }}
                }},
                {"required", {"query"}}
            }
        });
        handlers_["recall"] = [this](const json& params) { return tool_recall(params); };

        // Tool: resonate - Semantic search with spreading activation and Hebbian learning
        tools_.push_back({
            "resonate",
            "Semantic search enhanced with spreading activation through graph edges. "
            "Finds semantically similar nodes, then spreads activation through connections "
            "to discover related but not directly similar content. "
            "When learn=true, applies Hebbian learning: co-activated nodes strengthen their connections.",
            {
                {"type", "object"},
                {"properties", {
                    {"query", {
                        {"type", "string"},
                        {"description", "The search query"}
                    }},
                    {"k", {
                        {"type", "integer"},
                        {"minimum", 1},
                        {"maximum", 100},
                        {"default", 10},
                        {"description", "Maximum results to return"}
                    }},
                    {"spread_strength", {
                        {"type", "number"},
                        {"minimum", 0.0},
                        {"maximum", 1.0},
                        {"default", 0.5},
                        {"description", "Activation spread strength (0-1)"}
                    }},
                    {"learn", {
                        {"type", "boolean"},
                        {"default", true},
                        {"description", "Apply Hebbian learning: strengthen connections between co-activated nodes"}
                    }},
                    {"hebbian_strength", {
                        {"type", "number"},
                        {"minimum", 0.0},
                        {"maximum", 0.5},
                        {"default", 0.03},
                        {"description", "Strength of Hebbian learning (0-0.5)"}
                    }}
                }},
                {"required", {"query"}}
            }
        });
        handlers_["resonate"] = [this](const json& params) { return tool_resonate(params); };

        // Tool: full_resonate - PHASE 6: All resonance mechanisms working together
        tools_.push_back({
            "full_resonate",
            "Full resonance: all mechanisms working together. "
            "Combines session priming (Phase 4), spreading activation (Phase 1), "
            "attractor dynamics (Phase 2), lateral inhibition (Phase 5), and "
            "Hebbian learning (Phase 3). The soul doesn't just search - it resonates. "
            "Use this for deep, context-aware retrieval that learns from usage.",
            {
                {"type", "object"},
                {"properties", {
                    {"query", {
                        {"type", "string"},
                        {"description", "The search query"}
                    }},
                    {"k", {
                        {"type", "integer"},
                        {"minimum", 1},
                        {"maximum", 50},
                        {"default", 10},
                        {"description", "Maximum results to return"}
                    }},
                    {"spread_strength", {
                        {"type", "number"},
                        {"minimum", 0.0},
                        {"maximum", 1.0},
                        {"default", 0.5},
                        {"description", "Activation spread strength through graph edges (0-1)"}
                    }},
                    {"hebbian_strength", {
                        {"type", "number"},
                        {"minimum", 0.0},
                        {"maximum", 0.2},
                        {"default", 0.03},
                        {"description", "Hebbian learning strength: how much to strengthen co-activated connections (0-0.2)"}
                    }}
                }},
                {"required", {"query"}}
            }
        });
        handlers_["full_resonate"] = [this](const json& params) { return tool_full_resonate(params); };

        // Tool: recall_by_tag - Pure tag-based lookup (no semantic search)
        tools_.push_back({
            "recall_by_tag",
            "Recall all nodes with a specific tag, sorted by creation time. Use for exact thread lookups without semantic ranking.",
            {
                {"type", "object"},
                {"properties", {
                    {"tag", {
                        {"type", "string"},
                        {"description", "Tag to search for (e.g., 'thread:abc123', 'yajña', 'hotṛ')"}
                    }},
                    {"limit", {
                        {"type", "integer"},
                        {"minimum", 1},
                        {"maximum", 100},
                        {"default", 50},
                        {"description", "Maximum results"}
                    }}
                }},
                {"required", {"tag"}}
            }
        });
        handlers_["recall_by_tag"] = [this](const json& params) { return tool_recall_by_tag(params); };

        // Tool: cycle - Run maintenance cycle
        tools_.push_back({
            "cycle",
            "Run maintenance cycle: apply decay, prune low-confidence nodes, compute coherence, "
            "optionally run attractor dynamics, save.",
            {
                {"type", "object"},
                {"properties", {
                    {"save", {
                        {"type", "boolean"},
                        {"default", true},
                        {"description", "Whether to save after cycle"}
                    }},
                    {"attractors", {
                        {"type", "boolean"},
                        {"default", false},
                        {"description", "Run attractor dynamics: settle nodes toward conceptual gravity wells"}
                    }}
                }},
                {"required", json::array()}
            }
        });
        handlers_["cycle"] = [this](const json& params) { return tool_cycle(params); };

        // Tool: attractors - Find and report natural attractors (conceptual gravity wells)
        tools_.push_back({
            "attractors",
            "Find natural attractors in the soul graph. Attractors are high-confidence, well-connected "
            "nodes that act as conceptual gravity wells, pulling similar thoughts toward them.",
            {
                {"type", "object"},
                {"properties", {
                    {"max_attractors", {
                        {"type", "integer"},
                        {"minimum", 1},
                        {"maximum", 20},
                        {"default", 10},
                        {"description", "Maximum number of attractors to find"}
                    }},
                    {"settle", {
                        {"type", "boolean"},
                        {"default", false},
                        {"description", "Also run settling dynamics (strengthen connections to attractors)"}
                    }},
                    {"settle_strength", {
                        {"type", "number"},
                        {"minimum", 0.01},
                        {"maximum", 0.1},
                        {"default", 0.02},
                        {"description", "Strength of settling toward attractors"}
                    }}
                }},
                {"required", json::array()}
            }
        });
        handlers_["attractors"] = [this](const json& params) { return tool_attractors(params); };

        // Tool: lens - Biased search through different cognitive modes
        // NOTE: This is NOT multi-agent reasoning (use /antahkarana skill for that).
        // These are fast retrieval heuristics that apply different scoring biases.
        tools_.push_back({
            "lens",
            "Search through a cognitive lens (biased retrieval). Each lens applies different scoring: "
            "manas (recent/practical), buddhi (old/high-confidence wisdom), ahamkara (beliefs/invariants), "
            "chitta (frequently accessed), vikalpa (low-confidence/exploratory), sakshi (neutral). "
            "For actual multi-perspective reasoning, use /antahkarana or /debate skill instead.",
            {
                {"type", "object"},
                {"properties", {
                    {"query", {
                        {"type", "string"},
                        {"description", "What to search for"}
                    }},
                    {"lens", {
                        {"type", "string"},
                        {"enum", {"manas", "buddhi", "ahamkara", "chitta", "vikalpa", "sakshi", "all"}},
                        {"default", "all"},
                        {"description", "Which cognitive lens to apply, or 'all' for combined"}
                    }},
                    {"limit", {
                        {"type", "integer"},
                        {"minimum", 1},
                        {"maximum", 20},
                        {"default", 5},
                        {"description", "Maximum results per lens"}
                    }}
                }},
                {"required", {"query"}}
            }
        });
        handlers_["lens"] = [this](const json& params) { return tool_voices(params); };

        // Tool: lens_harmony - Check if different cognitive lenses agree
        tools_.push_back({
            "lens_harmony",
            "Check harmony across cognitive lenses. Shows whether different retrieval biases return consistent results.",
            {
                {"type", "object"},
                {"properties", json::object()},
                {"required", json::array()}
            }
        });
        handlers_["lens_harmony"] = [this](const json& params) { return tool_harmonize(params); };

        // Tool: intend - Set or check intentions
        tools_.push_back({
            "intend",
            "Set or check intentions. Intentions are goals with scope (session/project/persistent).",
            {
                {"type", "object"},
                {"properties", {
                    {"action", {
                        {"type", "string"},
                        {"enum", {"set", "list", "fulfill", "check"}},
                        {"default", "list"},
                        {"description", "'set' new intention, 'list' active, 'fulfill' mark done, 'check' specific"}
                    }},
                    {"want", {
                        {"type", "string"},
                        {"description", "What I want (for 'set')"}
                    }},
                    {"why", {
                        {"type", "string"},
                        {"description", "Why this matters (for 'set')"}
                    }},
                    {"scope", {
                        {"type", "string"},
                        {"enum", {"session", "project", "persistent"}},
                        {"default", "session"},
                        {"description", "Intention scope"}
                    }},
                    {"id", {
                        {"type", "string"},
                        {"description", "Intention ID (for 'fulfill'/'check')"}
                    }}
                }},
                {"required", json::array()}
            }
        });
        handlers_["intend"] = [this](const json& params) { return tool_intend(params); };

        // Tool: wonder - Register a question or knowledge gap (curiosity)
        tools_.push_back({
            "wonder",
            "Register a question or knowledge gap. The soul asks questions when it senses gaps. "
            "Questions can be answered later, potentially becoming wisdom.",
            {
                {"type", "object"},
                {"properties", {
                    {"question", {
                        {"type", "string"},
                        {"description", "The question to ask"}
                    }},
                    {"context", {
                        {"type", "string"},
                        {"description", "Why this question arose (what gap was detected)"}
                    }},
                    {"gap_type", {
                        {"type", "string"},
                        {"enum", {"recurring_problem", "repeated_correction", "unknown_domain",
                                  "missing_rationale", "contradiction", "uncertainty"}},
                        {"default", "uncertainty"},
                        {"description", "Type of knowledge gap"}
                    }},
                    {"priority", {
                        {"type", "number"},
                        {"minimum", 0.0},
                        {"maximum", 1.0},
                        {"default", 0.5},
                        {"description", "Priority of this question (0-1)"}
                    }}
                }},
                {"required", {"question"}}
            }
        });
        handlers_["wonder"] = [this](const json& params) { return tool_wonder(params); };

        // Tool: answer - Answer a question, optionally promote to wisdom
        tools_.push_back({
            "answer",
            "Answer a previously asked question. If the answer is significant, promote to wisdom.",
            {
                {"type", "object"},
                {"properties", {
                    {"question_id", {
                        {"type", "string"},
                        {"description", "ID of the question to answer (or 'latest')"}
                    }},
                    {"answer", {
                        {"type", "string"},
                        {"description", "The answer to the question"}
                    }},
                    {"promote_to_wisdom", {
                        {"type", "boolean"},
                        {"default", false},
                        {"description", "Promote this answer to wisdom"}
                    }},
                    {"dismiss", {
                        {"type", "boolean"},
                        {"default", false},
                        {"description", "Dismiss the question as not relevant"}
                    }}
                }},
                {"required", {"answer"}}
            }
        });
        handlers_["answer"] = [this](const json& params) { return tool_answer(params); };

        // Tool: connect - Create edges between nodes in the soul graph
        tools_.push_back({
            "connect",
            "Create a directed edge between two nodes in the soul graph. "
            "Used to build relationships: file imports, concept associations, etc.",
            {
                {"type", "object"},
                {"properties", {
                    {"from_id", {
                        {"type", "string"},
                        {"description", "Source node ID (UUID)"}
                    }},
                    {"to_id", {
                        {"type", "string"},
                        {"description", "Target node ID (UUID)"}
                    }},
                    {"edge_type", {
                        {"type", "string"},
                        {"enum", {"similar", "supports", "contradicts", "relates_to", "part_of", "is_a", "mentions"}},
                        {"default", "relates_to"},
                        {"description", "Type of relationship"}
                    }},
                    {"weight", {
                        {"type", "number"},
                        {"minimum", 0.0},
                        {"maximum", 1.0},
                        {"default", 0.8},
                        {"description", "Edge weight/strength (0-1)"}
                    }}
                }},
                {"required", {"from_id", "to_id"}}
            }
        });
        handlers_["connect"] = [this](const json& params) { return tool_connect(params); };

        // Tool: narrate - Manage story threads and episodes
        tools_.push_back({
            "narrate",
            "Record or retrieve narrative episodes. Stories connect observations into meaningful arcs.",
            {
                {"type", "object"},
                {"properties", {
                    {"action", {
                        {"type", "string"},
                        {"enum", {"start", "moment", "end", "recall", "list"}},
                        {"default", "moment"},
                        {"description", "'start' new episode, add 'moment', 'end' episode, 'recall' story, 'list' threads"}
                    }},
                    {"title", {
                        {"type", "string"},
                        {"description", "Episode title (for 'start')"}
                    }},
                    {"content", {
                        {"type", "string"},
                        {"description", "Content to record"}
                    }},
                    {"emotion", {
                        {"type", "string"},
                        {"enum", {"struggle", "exploration", "breakthrough", "satisfaction", "frustration", "routine"}},
                        {"default", "routine"},
                        {"description", "Emotional tone of this moment"}
                    }},
                    {"episode_id", {
                        {"type", "string"},
                        {"description", "Episode ID (for 'moment', 'end')"}
                    }},
                    {"query", {
                        {"type", "string"},
                        {"description", "Search query (for 'recall')"}
                    }}
                }},
                {"required", json::array()}
            }
        });
        handlers_["narrate"] = [this](const json& params) { return tool_narrate(params); };

        // Tool: feedback - Track if a memory was helpful or misleading (neural learning)
        tools_.push_back({
            "feedback",
            "Record feedback on a memory. Helpful memories get strengthened, misleading ones weakened. "
            "This enables neural learning - the soul learns from experience.",
            {
                {"type", "object"},
                {"properties", {
                    {"memory_id", {
                        {"type", "string"},
                        {"description", "ID of the memory to give feedback on"}
                    }},
                    {"helpful", {
                        {"type", "boolean"},
                        {"description", "Was this memory helpful? (true=strengthen, false=weaken)"}
                    }},
                    {"context", {
                        {"type", "string"},
                        {"description", "Context for why this feedback is given"}
                    }}
                }},
                {"required", {"memory_id", "helpful"}}
            }
        });
        handlers_["feedback"] = [this](const json& params) { return tool_feedback(params); };

        // Tool: ledger - Save/load/update session ledger (Atman snapshot)
        tools_.push_back({
            "ledger",
            "Session ledger operations: save/load/update the Atman snapshot. "
            "Captures soul state, work state, and continuation for session continuity. "
            "Project is auto-detected from cwd if not specified.",
            {
                {"type", "object"},
                {"properties", {
                    {"action", {
                        {"type", "string"},
                        {"enum", {"save", "load", "update", "list"}},
                        {"description", "Operation: save new ledger, load latest, update existing, list all"}
                    }},
                    {"session_id", {
                        {"type", "string"},
                        {"description", "Session identifier (optional, for filtering)"}
                    }},
                    {"project", {
                        {"type", "string"},
                        {"description", "Project name for isolation (auto-detected from cwd if not specified)"}
                    }},
                    {"ledger_id", {
                        {"type", "string"},
                        {"description", "Ledger ID (for update action)"}
                    }},
                    {"soul_state", {
                        {"type", "object"},
                        {"description", "Soul state: coherence, mood, intentions"}
                    }},
                    {"work_state", {
                        {"type", "object"},
                        {"description", "Work state: todos, files, decisions"}
                    }},
                    {"continuation", {
                        {"type", "object"},
                        {"description", "Continuation: next_steps, deferred, critical"}
                    }}
                }},
                {"required", {"action"}}
            }
        });
        handlers_["ledger"] = [this](const json& params) { return tool_ledger(params); };
    }

    json handle_request(const json& request) {
        // Validate JSON-RPC 2.0
        if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0") {
            return make_error(request.value("id", json()), rpc_error::INVALID_REQUEST,
                              "Missing or invalid jsonrpc version");
        }

        if (!request.contains("method") || !request["method"].is_string()) {
            return make_error(request.value("id", json()), rpc_error::INVALID_REQUEST,
                              "Missing or invalid method");
        }

        std::string method = request["method"];
        json params = request.value("params", json::object());
        json id = request.value("id", json());

        // Handle MCP protocol methods
        if (method == "initialize") {
            return handle_initialize(params, id);
        } else if (method == "initialized") {
            return json();  // Notification, no response
        } else if (method == "tools/list") {
            return handle_tools_list(params, id);
        } else if (method == "tools/call") {
            return handle_tools_call(params, id);
        } else if (method == "shutdown") {
            running_ = false;
            return make_result(id, json::object());
        }

        return make_error(id, rpc_error::METHOD_NOT_FOUND,
                          "Unknown method: " + method);
    }

    json handle_initialize(const json& params, const json& id) {
        json capabilities = {
            {"tools", {
                {"listChanged", true}
            }}
        };

        json server_info = {
            {"name", server_name_},
            {"version", "0.1.0"}
        };

        json result = {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", capabilities},
            {"serverInfo", server_info}
        };

        return make_result(id, result);
    }

    json handle_tools_list(const json& /*params*/, const json& id) {
        json tools_array = json::array();
        for (const auto& tool : tools_) {
            tools_array.push_back({
                {"name", tool.name},
                {"description", tool.description},
                {"inputSchema", tool.input_schema}
            });
        }
        return make_result(id, {{"tools", tools_array}});
    }

    json handle_tools_call(const json& params, const json& id) {
        if (!params.contains("name") || !params["name"].is_string()) {
            return make_error(id, rpc_error::INVALID_PARAMS, "Missing tool name");
        }

        std::string name = params["name"];
        json arguments = params.value("arguments", json::object());

        auto it = handlers_.find(name);
        if (it == handlers_.end()) {
            return make_error(id, rpc_error::TOOL_NOT_FOUND, "Unknown tool: " + name);
        }

        try {
            ToolResult result = it->second(arguments);
            json content = json::array();
            content.push_back({
                {"type", "text"},
                {"text", result.content}
            });

            json response = {
                {"content", content},
                {"isError", result.is_error}
            };

            // Include structured data if present
            if (!result.structured.is_null()) {
                response["structured"] = result.structured;
            }

            return make_result(id, response);
        } catch (const std::exception& e) {
            return make_error(id, rpc_error::TOOL_EXECUTION_ERROR,
                              std::string("Tool execution failed: ") + e.what());
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // Tool implementations
    // ═══════════════════════════════════════════════════════════════════

    ToolResult tool_soul_context(const json& params) {
        std::string query = params.value("query", "");
        std::string format = params.value("format", "text");
        bool include_ledger = params.value("include_ledger", true);

        MindState state = mind_->state();
        Coherence coherence = mind_->coherence();
        MindHealth health = mind_->health();

        json result = {
            {"samarasya", {  // Sāmarasya (सामरस्य) = harmony/equilibrium
                {"local", coherence.local},
                {"global", coherence.global},
                {"temporal", coherence.temporal},
                {"structural", coherence.structural},
                {"tau", coherence.tau_k()}  // Greek: τ
            }},
            {"ojas", {
                {"structural", health.structural},
                {"semantic", health.semantic},
                {"temporal", health.temporal},
                {"capacity", health.capacity},
                {"vitality", health.ojas()},
                {"psi", health.psi()},
                {"status", health.status_string()}
            }},
            {"statistics", {
                {"total_nodes", state.total_nodes},
                {"hot_nodes", state.hot_nodes},
                {"warm_nodes", state.warm_nodes},
                {"cold_nodes", state.cold_nodes}
            }},
            {"yantra_ready", state.yantra_ready}
        };

        // Add session context (Phase 4: Context Modulation)
        const auto& session = mind_->session_context();
        result["session_context"] = {
            {"recent_observations", session.recent_observations.size()},
            {"active_intentions", session.active_intentions.size()},
            {"goal_basin", session.goal_basin.size()},
            {"priming_active", !session.empty()}
        };

        // Add competition config (Phase 5: Interference/Competition)
        const auto& competition = mind_->competition_config();
        result["competition"] = {
            {"enabled", competition.enabled},
            {"similarity_threshold", competition.similarity_threshold},
            {"inhibition_strength", competition.inhibition_strength},
            {"hard_suppression", competition.hard_suppression}
        };

        // Add latest ledger (Atman snapshot) if available
        if (include_ledger) {
            auto ledger = mind_->load_ledger();
            if (ledger) {
                try {
                    result["ledger"] = {
                        {"id", ledger->first.to_string()},
                        {"content", json::parse(ledger->second)}
                    };
                } catch (...) {
                    result["ledger"] = {
                        {"id", ledger->first.to_string()},
                        {"content", {{"raw", ledger->second}}}
                    };
                }
            }
        }

        // Add relevant wisdom if query provided
        if (!query.empty() && mind_->has_yantra()) {
            auto recalls = mind_->recall(query, 5);
            json wisdom_array = json::array();
            for (const auto& r : recalls) {
                wisdom_array.push_back({
                    {"id", r.id.to_string()},
                    {"text", r.text},
                    {"similarity", r.similarity},
                    {"type", node_type_to_string(r.type)},
                    {"confidence", r.confidence.mu}
                });
            }
            result["relevant_wisdom"] = wisdom_array;
        }

        if (format == "text") {
            std::ostringstream ss;
            ss << "Soul State:\n";
            // Sāmarasya (सामरस्य) = harmony/equilibrium, measured as τ (tau)
            ss << "  Sāmarasya (τ): " << int(coherence.tau_k() * 100) << "% ";
            ss << "(L:" << int(coherence.local * 100);
            ss << " G:" << int(coherence.global * 100);
            ss << " T:" << int(coherence.temporal * 100);
            ss << " S:" << int(coherence.structural * 100) << ")\n";
            // Ojas (ओजस्) = vital essence, measured as ψ (psi)
            ss << "  Ojas (ψ): " << int(health.psi() * 100) << "% [" << health.status_string() << "] ";
            ss << "(S:" << int(health.structural * 100);
            ss << " M:" << int(health.semantic * 100);
            ss << " T:" << int(health.temporal * 100);
            ss << " C:" << int(health.capacity * 100) << ")\n";
            ss << "  Nodes: " << state.total_nodes << " total (";
            ss << state.hot_nodes << " hot, ";
            ss << state.warm_nodes << " warm, ";
            ss << state.cold_nodes << " cold)\n";
            ss << "  Yantra: " << (state.yantra_ready ? "ready" : "not ready") << "\n";

            // Session context (priming status)
            if (!session.empty()) {
                ss << "  Priming: " << session.recent_observations.size() << " recent, ";
                ss << session.active_intentions.size() << " intentions, ";
                ss << session.goal_basin.size() << " basin\n";
            }

            // Competition status
            ss << "  Competition: " << (competition.enabled ? "enabled" : "disabled");
            if (competition.enabled) {
                ss << " (threshold:" << int(competition.similarity_threshold * 100) << "%";
                ss << " inhibition:" << int(competition.inhibition_strength * 100) << "%";
                ss << (competition.hard_suppression ? " hard)" : " soft)");
            }
            ss << "\n";

            // Add ledger summary to text output
            if (result.contains("ledger") && result["ledger"].contains("content")) {
                ss << "\nSession Ledger (Atman):\n";
                auto& content = result["ledger"]["content"];
                if (content.contains("work_state") && !content["work_state"].empty()) {
                    ss << "  Work: ";
                    if (content["work_state"].contains("todos")) {
                        ss << content["work_state"]["todos"].size() << " todos";
                    }
                    ss << "\n";
                }
                if (content.contains("continuation") && !content["continuation"].empty()) {
                    ss << "  Continuation: ";
                    if (content["continuation"].contains("next_steps")) {
                        ss << content["continuation"]["next_steps"].size() << " next steps";
                    }
                    if (content["continuation"].contains("critical")) {
                        auto& critical = content["continuation"]["critical"];
                        if (!critical.empty()) {
                            ss << ", " << critical.size() << " critical";
                        }
                    }
                    ss << "\n";
                }
            }

            if (result.contains("relevant_wisdom")) {
                ss << "\nRelevant Wisdom:\n";
                for (const auto& w : result["relevant_wisdom"]) {
                    ss << "  - " << w["text"].get<std::string>() << " (";
                    ss << (w["similarity"].get<float>() * 100) << "% match)\n";
                }
            }

            return {false, ss.str(), result};
        }

        return {false, result.dump(2), result};
    }

    ToolResult tool_grow(const json& params) {
        std::string type_str = params.at("type");
        std::string content = params.at("content");
        std::string title = params.value("title", "");
        std::string domain = params.value("domain", "");
        float confidence = params.value("confidence", 0.8f);

        NodeType type = string_to_node_type(type_str);

        // Validate requirements
        if ((type == NodeType::Wisdom || type == NodeType::Failure) && title.empty()) {
            return {true, "Title required for wisdom/failure", json()};
        }

        // Create combined text for embedding
        std::string full_text = content;
        if (!title.empty()) {
            full_text = title + ": " + content;
        }
        if (!domain.empty()) {
            full_text = "[" + domain + "] " + full_text;
        }

        NodeId id;
        if (mind_->has_yantra()) {
            id = mind_->remember(full_text, type, Confidence(confidence));
        } else {
            id = mind_->remember(type, Vector::zeros(), Confidence(confidence),
                                 std::vector<uint8_t>(full_text.begin(), full_text.end()));
        }

        json result = {
            {"id", id.to_string()},
            {"type", type_str},
            {"title", title},
            {"confidence", confidence}
        };

        std::ostringstream ss;
        ss << "Grew " << type_str << ": " << (title.empty() ? content.substr(0, 50) : title);
        ss << " (id: " << id.to_string() << ")";

        return {false, ss.str(), result};
    }

    ToolResult tool_observe(const json& params) {
        std::string category = params.at("category");
        std::string title = params.at("title");
        std::string content = params.at("content");
        std::string project = params.value("project", "");
        std::string tags_str = params.value("tags", "");

        // Determine decay rate based on category
        float decay = 0.05f;  // default
        if (category == "bugfix" || category == "decision") {
            decay = 0.02f;  // slow decay
        } else if (category == "session_ledger" || category == "signal") {
            decay = 0.15f;  // fast decay
        }

        // Parse tags into vector for exact-match indexing
        std::vector<std::string> tags_vec;
        if (!tags_str.empty()) {
            std::stringstream ss(tags_str);
            std::string tag;
            while (std::getline(ss, tag, ',')) {
                // Trim whitespace
                size_t start = tag.find_first_not_of(" \t");
                size_t end = tag.find_last_not_of(" \t");
                if (start != std::string::npos) {
                    tags_vec.push_back(tag.substr(start, end - start + 1));
                }
            }
        }

        // Create full observation text (tags also in text for semantic search)
        std::string full_text = title + "\n" + content;
        if (!project.empty()) {
            full_text = "[" + project + "] " + full_text;
        }
        if (!tags_str.empty()) {
            full_text += "\nTags: " + tags_str;
        }

        NodeId id;
        if (mind_->has_yantra()) {
            // Use tag-aware remember for exact-match filtering
            if (!tags_vec.empty()) {
                id = mind_->remember(full_text, NodeType::Episode, tags_vec);
            } else {
                id = mind_->remember(full_text, NodeType::Episode);
            }
        } else {
            id = mind_->remember(NodeType::Episode, Vector::zeros(),
                                 std::vector<uint8_t>(full_text.begin(), full_text.end()));
        }

        // Set decay rate
        if (auto node = mind_->get(id)) {
            mind_->strengthen(id, 0);  // Touch to set decay
        }

        json result = {
            {"id", id.to_string()},
            {"category", category},
            {"title", title},
            {"decay_rate", decay},
            {"tags", tags_vec}
        };

        return {false, "Observed: " + title, result};
    }

    // Helper: extract title from text (first line or N chars)
    std::string extract_title(const std::string& text, size_t max_len = 60) const {
        size_t newline = text.find('\n');
        size_t end = std::min({newline, max_len, text.length()});
        std::string title = text.substr(0, end);
        if (end < text.length() && newline != end) title += "...";
        return title;
    }

    ToolResult tool_recall(const json& params) {
        std::string query = params.at("query");
        std::string zoom = params.value("zoom", "normal");
        std::string tag = params.value("tag", "");
        float threshold = params.value("threshold", 0.0f);
        bool learn = params.value("learn", false);
        bool primed = params.value("primed", false);
        bool compete = params.value("compete", true);

        // Temporarily adjust competition setting if needed
        bool original_compete = mind_->competition_config().enabled;
        if (!compete && original_compete) {
            mind_->set_competition_enabled(false);
        }

        // Zoom-aware default limits
        size_t default_limit = (zoom == "sparse") ? 25 :
                               (zoom == "dense") ? 5 :
                               (zoom == "full") ? 3 : 10;
        size_t limit = params.value("limit", static_cast<int>(default_limit));

        // Clamp limits per zoom level
        if (zoom == "sparse") {
            limit = std::clamp(limit, size_t(5), size_t(100));
        } else if (zoom == "dense") {
            limit = std::clamp(limit, size_t(1), size_t(10));
        } else if (zoom == "full") {
            limit = std::clamp(limit, size_t(1), size_t(5));
        } else {
            limit = std::clamp(limit, size_t(1), size_t(50));
        }

        if (!mind_->has_yantra()) {
            return {true, "Yantra not ready - cannot perform semantic search", json()};
        }

        std::vector<Recall> recalls;
        if (!tag.empty()) {
            // Tag-filtered recall (no priming support for tag queries yet)
            recalls = mind_->recall_with_tag_filter(query, tag, limit, threshold);
        } else if (primed) {
            // Session-primed recall: boost based on recent observations and intentions
            recalls = mind_->recall_primed(query, limit, threshold);
        } else {
            // Standard recall
            recalls = mind_->recall(query, limit, threshold);
        }

        // Restore competition setting
        if (!compete && original_compete) {
            mind_->set_competition_enabled(true);
        }

        // Apply Hebbian learning if enabled (independent of priming)
        if (learn && recalls.size() >= 2) {
            std::vector<NodeId> co_retrieved;
            size_t learn_count = std::min(recalls.size(), size_t(5));
            co_retrieved.reserve(learn_count);
            for (size_t i = 0; i < learn_count; ++i) {
                co_retrieved.push_back(recalls[i].id);
            }
            mind_->hebbian_update(co_retrieved, 0.05f);
        }

        json results_array = json::array();
        std::ostringstream ss;
        ss << "Found " << recalls.size() << " results";
        if (!tag.empty()) ss << " with tag '" << tag << "'";
        ss << " (" << zoom << " view):\n";

        Timestamp current = now();

        for (const auto& r : recalls) {
            mind_->feedback_used(r.id);

            if (zoom == "sparse") {
                // Sparse: minimal payload for overview
                std::string title = extract_title(r.text);
                results_array.push_back({
                    {"id", r.id.to_string()},
                    {"title", title},
                    {"type", node_type_to_string(r.type)},
                    {"relevance", r.relevance}
                });
                ss << "\n[" << node_type_to_string(r.type) << "] " << title;

            } else if (zoom == "dense") {
                // Dense: full context with temporal, edges, confidence details
                auto result_tags = mind_->get_tags(r.id);
                float age_days = static_cast<float>(current - r.created) / 86400000.0f;
                float access_age = static_cast<float>(current - r.accessed) / 86400000.0f;

                // Get node for edges and decay rate
                json edges_array = json::array();
                float decay_rate = 0.05f;
                if (auto node = mind_->get(r.id)) {
                    decay_rate = node->delta;
                    for (size_t i = 0; i < std::min(node->edges.size(), size_t(5)); ++i) {
                        auto& edge = node->edges[i];
                        std::string rel_text = mind_->text(edge.target).value_or("");
                        edges_array.push_back({
                            {"id", edge.target.to_string()},
                            {"type", static_cast<int>(edge.type)},
                            {"weight", edge.weight},
                            {"title", extract_title(rel_text)}
                        });
                    }
                }

                results_array.push_back({
                    {"id", r.id.to_string()},
                    {"text", r.text},
                    {"similarity", r.similarity},
                    {"relevance", r.relevance},
                    {"type", node_type_to_string(r.type)},
                    {"confidence", {
                        {"mu", r.confidence.mu},
                        {"sigma_sq", r.confidence.sigma_sq},
                        {"n", r.confidence.n},
                        {"effective", r.confidence.effective()}
                    }},
                    {"temporal", {
                        {"created", r.created},
                        {"accessed", r.accessed},
                        {"age_days", age_days},
                        {"access_age_days", access_age},
                        {"decay_rate", decay_rate}
                    }},
                    {"related", edges_array},
                    {"tags", result_tags}
                });
                ss << "\n[" << node_type_to_string(r.type) << "] " << extract_title(r.text, 80);
                if (!edges_array.empty()) ss << " (" << edges_array.size() << " related)";

            } else if (zoom == "full") {
                // Full: complete untruncated content for reconstruction
                auto result_tags = mind_->get_tags(r.id);
                results_array.push_back({
                    {"id", r.id.to_string()},
                    {"text", r.text},  // Full text, no truncation
                    {"type", node_type_to_string(r.type)},
                    {"relevance", r.relevance},
                    {"confidence", r.confidence.mu},
                    {"tags", result_tags}
                });
                // Output full text in display
                ss << "\n\n=== [" << node_type_to_string(r.type) << "] ===\n";
                ss << r.text;
                ss << "\n";

            } else {
                // Normal: current balanced behavior
                auto result_tags = mind_->get_tags(r.id);
                results_array.push_back({
                    {"id", r.id.to_string()},
                    {"text", r.text},
                    {"similarity", r.similarity},
                    {"relevance", r.relevance},
                    {"type", node_type_to_string(r.type)},
                    {"confidence", r.confidence.mu},
                    {"tags", result_tags}
                });
                ss << "\n[" << (r.relevance * 100) << "%] " << r.text.substr(0, 100);
                if (r.text.length() > 100) ss << "...";
            }
        }

        return {false, ss.str(), {{"results", results_array}, {"zoom", zoom}}};
    }

    // Recall by tag only (no semantic search) - for exact thread lookup
    ToolResult tool_recall_by_tag(const json& params) {
        std::string tag = params.at("tag");
        size_t limit = params.value("limit", 50);

        auto recalls = mind_->recall_by_tag(tag, limit);

        json results_array = json::array();
        std::ostringstream ss;
        ss << "Found " << recalls.size() << " results with tag '" << tag << "':\n";

        for (const auto& r : recalls) {
            mind_->feedback_used(r.id);

            auto result_tags = mind_->get_tags(r.id);

            results_array.push_back({
                {"id", r.id.to_string()},
                {"text", r.text},
                {"created", r.created},
                {"type", node_type_to_string(r.type)},
                {"confidence", r.confidence.mu},
                {"tags", result_tags}
            });

            ss << "\n[" << node_type_to_string(r.type) << "] " << r.text.substr(0, 100);
            if (r.text.length() > 100) ss << "...";
        }

        return {false, ss.str(), {{"results", results_array}}};
    }

    ToolResult tool_resonate(const json& params) {
        std::string query = params.at("query");
        size_t k = params.value("k", 10);
        float spread_strength = params.value("spread_strength", 0.5f);
        bool learn = params.value("learn", true);
        float hebbian_strength = params.value("hebbian_strength", 0.03f);

        if (!mind_->has_yantra()) {
            return {true, "Yantra not ready - cannot perform semantic search", json()};
        }

        // Use learning-enabled resonate when learn=true
        std::vector<Recall> recalls;
        if (learn) {
            recalls = mind_->resonate_with_learning(query, k, spread_strength, hebbian_strength);
        } else {
            recalls = mind_->resonate(query, k, spread_strength);
        }

        json results_array = json::array();
        std::ostringstream ss;
        ss << "Resonance search for: " << query << "\n";
        ss << "Found " << recalls.size() << " resonant nodes";
        ss << " (spread=" << spread_strength;
        if (learn) ss << ", hebbian=" << hebbian_strength;
        ss << "):\n";

        for (const auto& r : recalls) {
            mind_->feedback_used(r.id);

            auto result_tags = mind_->get_tags(r.id);

            results_array.push_back({
                {"id", r.id.to_string()},
                {"text", r.text},
                {"relevance", r.relevance},
                {"type", node_type_to_string(r.type)},
                {"confidence", r.confidence.mu},
                {"tags", result_tags}
            });

            ss << "\n[" << (r.relevance * 100) << "%] " << r.text.substr(0, 100);
            if (r.text.length() > 100) ss << "...";
        }

        json result = {
            {"results", results_array},
            {"spread_strength", spread_strength},
            {"learning_enabled", learn}
        };
        if (learn) {
            result["hebbian_strength"] = hebbian_strength;
        }

        return {false, ss.str(), result};
    }

    // PHASE 6: Full Resonance - All mechanisms working together
    ToolResult tool_full_resonate(const json& params) {
        std::string query = params.at("query");
        size_t k = params.value("k", 10);
        float spread_strength = params.value("spread_strength", 0.5f);
        float hebbian_strength = params.value("hebbian_strength", 0.03f);

        if (!mind_->has_yantra()) {
            return {true, "Yantra not ready - cannot perform semantic search", json()};
        }

        // Full resonance: priming + spreading + attractors + competition + hebbian
        auto recalls = mind_->full_resonate(query, k, spread_strength, hebbian_strength);

        json results_array = json::array();
        std::ostringstream ss;
        ss << "Full resonance for: " << query << "\n";
        ss << "Found " << recalls.size() << " resonant nodes";
        ss << " (spread=" << spread_strength;
        ss << ", hebbian=" << hebbian_strength << "):\n";

        for (const auto& r : recalls) {
            mind_->feedback_used(r.id);

            auto result_tags = mind_->get_tags(r.id);

            results_array.push_back({
                {"id", r.id.to_string()},
                {"text", r.text},
                {"relevance", r.relevance},
                {"similarity", r.similarity},
                {"type", node_type_to_string(r.type)},
                {"confidence", r.confidence.mu},
                {"tags", result_tags}
            });

            ss << "\n[" << (r.relevance * 100) << "%] ";
            ss << "[" << node_type_to_string(r.type) << "] ";
            ss << r.text.substr(0, 90);
            if (r.text.length() > 90) ss << "...";
        }

        json result = {
            {"results", results_array},
            {"phases_active", {
                {"priming", true},
                {"spreading_activation", true},
                {"attractor_dynamics", true},
                {"lateral_inhibition", mind_->competition_config().enabled},
                {"hebbian_learning", hebbian_strength > 0.0f}
            }},
            {"spread_strength", spread_strength},
            {"hebbian_strength", hebbian_strength}
        };

        return {false, ss.str(), result};
    }

    ToolResult tool_cycle(const json& params) {
        bool save = params.value("save", true);
        bool run_attractors = params.value("attractors", false);

        DynamicsReport report = mind_->tick();

        // Apply pending feedback (learning from usage)
        size_t feedback_applied = mind_->apply_feedback();

        // Attempt automatic synthesis (observations → wisdom)
        size_t synthesized = mind_->synthesize_wisdom();

        // Run attractor dynamics if requested
        Mind::AttractorReport attractor_report;
        if (run_attractors) {
            attractor_report = mind_->run_attractor_dynamics();
        }

        if (save) {
            mind_->snapshot();
        }

        Coherence coherence = mind_->coherence();

        json result = {
            {"coherence", coherence.tau_k()},
            {"decay_applied", report.decay_applied},
            {"triggers_fired", report.triggers_fired.size()},
            {"feedback_applied", feedback_applied},
            {"wisdom_synthesized", synthesized},
            {"saved", save}
        };

        if (run_attractors) {
            result["attractors_found"] = attractor_report.attractor_count;
            result["nodes_settled"] = attractor_report.nodes_settled;
        }

        std::ostringstream ss;
        ss << "Cycle complete: coherence=" << (coherence.tau_k() * 100) << "%, ";
        ss << "decay=" << (report.decay_applied ? "yes" : "no");
        ss << ", feedback=" << feedback_applied;
        if (synthesized > 0) {
            ss << ", synthesized=" << synthesized << " wisdom";
        }
        if (run_attractors) {
            ss << ", attractors=" << attractor_report.attractor_count;
            ss << ", settled=" << attractor_report.nodes_settled;
        }

        return {false, ss.str(), result};
    }

    ToolResult tool_attractors(const json& params) {
        size_t max_attractors = params.value("max_attractors", 10);
        bool settle = params.value("settle", false);
        float settle_strength = params.value("settle_strength", 0.02f);

        // Find attractors
        auto attractors = mind_->find_attractors(max_attractors);

        // Optionally run settling
        size_t settled = 0;
        if (settle && !attractors.empty()) {
            settled = mind_->settle_toward_attractors(attractors, settle_strength);
        }

        // Build results
        json attractors_array = json::array();
        std::ostringstream ss;

        if (attractors.empty()) {
            ss << "No attractors found (need nodes with high confidence, connections, and age)\n";
            return {false, ss.str(), {{"attractors", attractors_array}, {"count", 0}}};
        }

        ss << "Found " << attractors.size() << " attractors";
        if (settle) {
            ss << " (settled " << settled << " nodes)";
        }
        ss << ":\n";

        // Compute basins for size info
        auto basins = mind_->compute_basins(attractors);

        for (const auto& attr : attractors) {
            size_t basin_size = basins.count(attr.id) ? basins[attr.id].size() : 0;

            attractors_array.push_back({
                {"id", attr.id.to_string()},
                {"strength", attr.strength},
                {"label", attr.label},
                {"basin_size", basin_size}
            });

            ss << "\n[" << (attr.strength * 100) << "%] " << attr.label;
            if (attr.label.length() >= 50) ss << "...";
            ss << " (basin: " << basin_size << " nodes)";
        }

        json result = {
            {"attractors", attractors_array},
            {"count", attractors.size()}
        };
        if (settle) {
            result["nodes_settled"] = settled;
        }

        return {false, ss.str(), result};
    }

    ToolResult tool_voices(const json& params) {
        std::string query = params.at("query");
        std::string voice_name = params.value("voice", "all");
        size_t limit = params.value("limit", 5);

        if (!mind_->has_yantra()) {
            return {true, "Yantra not ready - cannot perform semantic search", json()};
        }

        // Get base results from storage (the source of truth)
        auto base_results = mind_->recall(query, limit * 3);  // Get more, then filter

        json results = json::object();
        std::ostringstream ss;

        auto query_voice = [&](const Voice& voice) {
            // Apply voice-specific weighting to base results
            std::vector<std::tuple<std::string, std::string, float, NodeType>> weighted;

            for (const auto& r : base_results) {
                // Get attention weight for this node type
                float attn = 1.0f;
                auto it = voice.attention.find(r.type);
                if (it != voice.attention.end()) attn = it->second;

                // Apply voice's confidence bias
                float biased_conf = std::clamp(r.confidence.mu + voice.confidence_bias, 0.0f, 1.0f);

                // Compute voice-adjusted score
                float score = r.similarity * attn * 0.7f + biased_conf * 0.3f;

                weighted.emplace_back(r.id.to_string(), r.text, score, r.type);
            }

            // Sort by voice-adjusted score
            std::sort(weighted.begin(), weighted.end(),
                [](const auto& a, const auto& b) { return std::get<2>(a) > std::get<2>(b); });

            // Take top results for this voice
            json voice_array = json::array();
            ss << "\n" << voice.name << " (" << voice.description << "):\n";

            size_t count = 0;
            for (const auto& [id, text, score, type] : weighted) {
                if (count >= limit) break;

                // Auto-trigger feedback: this memory was surfaced via voice
                NodeId node_id = NodeId::from_string(id);
                mind_->feedback_used(node_id);

                voice_array.push_back({
                    {"id", id},
                    {"text", text.substr(0, 200)},
                    {"score", score},
                    {"type", node_type_to_string(type)}
                });

                ss << "  [" << (score * 100) << "%] " << text.substr(0, 80);
                if (text.length() > 80) ss << "...";
                ss << "\n";
                count++;
            }

            results[voice.name] = voice_array;
        };

        if (voice_name == "all") {
            ss << "Consulting all Antahkarana voices on: " << query;
            for (const auto& voice : antahkarana::all()) {
                query_voice(voice);
            }
        } else {
            Voice voice = antahkarana::manas();  // default
            if (voice_name == "manas") voice = antahkarana::manas();
            else if (voice_name == "buddhi") voice = antahkarana::buddhi();
            else if (voice_name == "ahamkara") voice = antahkarana::ahamkara();
            else if (voice_name == "chitta") voice = antahkarana::chitta();
            else if (voice_name == "vikalpa") voice = antahkarana::vikalpa();
            else if (voice_name == "sakshi") voice = antahkarana::sakshi();

            ss << "Consulting " << voice.name << " on: " << query;
            query_voice(voice);
        }

        return {false, ss.str(), results};
    }

    ToolResult tool_harmonize(const json& /*params*/) {
        const Graph& graph = mind_->graph();

        Chorus chorus(antahkarana::all());
        HarmonyReport report = chorus.harmonize(graph);

        json perspectives = json::array();
        for (const auto& [name, coherence] : report.perspectives) {
            perspectives.push_back({
                {"voice", name},
                {"coherence", coherence}
            });
        }

        json result = {
            {"mean_coherence", report.mean_coherence},
            {"variance", report.variance},
            {"voices_agree", report.voices_agree},
            {"perspectives", perspectives}
        };

        std::ostringstream ss;
        ss << "Harmony Report:\n";
        ss << "  Mean coherence: " << (report.mean_coherence * 100) << "%\n";
        ss << "  Variance: " << report.variance << "\n";
        ss << "  Voices agree: " << (report.voices_agree ? "yes" : "no") << "\n";
        ss << "\nPerspectives:\n";
        for (const auto& [name, coherence] : report.perspectives) {
            ss << "  " << name << ": " << (coherence * 100) << "%\n";
        }

        return {false, ss.str(), result};
    }

    ToolResult tool_intend(const json& params) {
        std::string action = params.value("action", "list");

        if (action == "set") {
            std::string want = params.value("want", "");
            std::string why = params.value("why", "");
            std::string scope = params.value("scope", "session");

            if (want.empty()) {
                return {true, "Missing 'want' for set action", json()};
            }

            std::string full_text = want;
            if (!why.empty()) {
                full_text += " | Why: " + why;
            }
            full_text = "[" + scope + "] " + full_text;

            NodeId id;
            if (mind_->has_yantra()) {
                id = mind_->remember(full_text, NodeType::Intention, Confidence(0.9f));
            } else {
                id = mind_->remember(NodeType::Intention, Vector::zeros(), Confidence(0.9f),
                                     std::vector<uint8_t>(full_text.begin(), full_text.end()));
            }

            json result = {
                {"id", id.to_string()},
                {"want", want},
                {"why", why},
                {"scope", scope}
            };

            return {false, "Intention set: " + want, result};

        } else if (action == "list") {
            auto intentions = mind_->query_by_type(NodeType::Intention);

            json list = json::array();
            std::ostringstream ss;
            ss << "Active intentions (" << intentions.size() << "):\n";

            for (const auto& node : intentions) {
                std::string text(node.payload.begin(), node.payload.end());
                list.push_back({
                    {"id", node.id.to_string()},
                    {"text", text},
                    {"confidence", node.kappa.effective()}
                });
                ss << "  - " << text << " (" << (node.kappa.effective() * 100) << "% confidence)\n";
            }

            return {false, ss.str(), {{"intentions", list}}};

        } else if (action == "fulfill") {
            std::string id_str = params.value("id", "");
            if (id_str.empty()) {
                return {true, "Missing 'id' for fulfill action", json()};
            }

            NodeId id = NodeId::from_string(id_str);
            mind_->weaken(id, 1.0f);  // Set confidence to 0 (fulfilled = done)

            return {false, "Intention fulfilled: " + id_str, {{"id", id_str}, {"fulfilled", true}}};

        } else if (action == "check") {
            std::string id_str = params.value("id", "");
            if (id_str.empty()) {
                return {true, "Missing 'id' for check action", json()};
            }

            NodeId id = NodeId::from_string(id_str);
            auto node_opt = mind_->get(id);

            if (!node_opt) {
                return {true, "Intention not found: " + id_str, json()};
            }

            const auto& node = *node_opt;
            std::string text(node.payload.begin(), node.payload.end());

            json result = {
                {"id", id_str},
                {"text", text},
                {"confidence", node.kappa.effective()},
                {"active", node.kappa.effective() > 0.1f}
            };

            return {false, text + " (" + std::to_string(node.kappa.effective() * 100) + "% active)", result};
        }

        return {true, "Unknown action: " + action, json()};
    }

    ToolResult tool_wonder(const json& params) {
        std::string question = params.at("question");
        std::string context = params.value("context", "");
        std::string gap_type = params.value("gap_type", "uncertainty");
        float priority = params.value("priority", 0.5f);

        // Create question text with metadata
        std::string full_text = question;
        if (!context.empty()) {
            full_text += " | Context: " + context;
        }
        full_text = "[" + gap_type + "] " + full_text;

        NodeId id;
        if (mind_->has_yantra()) {
            id = mind_->remember(full_text, NodeType::Question, Confidence(priority));
        } else {
            id = mind_->remember(NodeType::Question, Vector::zeros(), Confidence(priority),
                                 std::vector<uint8_t>(full_text.begin(), full_text.end()));
        }

        json result = {
            {"id", id.to_string()},
            {"question", question},
            {"gap_type", gap_type},
            {"priority", priority}
        };

        return {false, "Question registered: " + question.substr(0, 50), result};
    }

    ToolResult tool_answer(const json& params) {
        std::string answer = params.at("answer");
        std::string question_id_str = params.value("question_id", "latest");
        bool promote = params.value("promote_to_wisdom", false);
        bool dismiss = params.value("dismiss", false);

        // Find the question (either by ID or get latest)
        std::optional<Node> question_node;
        NodeId question_id;

        if (question_id_str == "latest") {
            // Find most recent question
            auto questions = mind_->query_by_type(NodeType::Question);
            if (questions.empty()) {
                return {true, "No pending questions found", json()};
            }
            // Sort by timestamp, get most recent
            std::sort(questions.begin(), questions.end(),
                [](const Node& a, const Node& b) { return a.tau_created > b.tau_created; });
            question_node = questions[0];
            question_id = questions[0].id;
        } else {
            question_id = NodeId::from_string(question_id_str);
            question_node = mind_->get(question_id);
        }

        if (!question_node) {
            return {true, "Question not found", json()};
        }

        std::string question_text(question_node->payload.begin(), question_node->payload.end());

        if (dismiss) {
            mind_->weaken(question_id, 1.0f);  // Mark as dismissed
            return {false, "Question dismissed", {{"question_id", question_id.to_string()}, {"dismissed", true}}};
        }

        // Record the answer as observation
        std::string full_answer = "Q: " + question_text + "\nA: " + answer;

        NodeId answer_id;
        if (promote) {
            // Promote to wisdom
            if (mind_->has_yantra()) {
                answer_id = mind_->remember(full_answer, NodeType::Wisdom, Confidence(0.8f));
            } else {
                answer_id = mind_->remember(NodeType::Wisdom, Vector::zeros(), Confidence(0.8f),
                                           std::vector<uint8_t>(full_answer.begin(), full_answer.end()));
            }
        } else {
            // Just record as episode
            if (mind_->has_yantra()) {
                answer_id = mind_->remember(full_answer, NodeType::Episode, Confidence(0.7f));
            } else {
                answer_id = mind_->remember(NodeType::Episode, Vector::zeros(), Confidence(0.7f),
                                           std::vector<uint8_t>(full_answer.begin(), full_answer.end()));
            }
        }

        // Mark question as answered (weaken but don't delete)
        mind_->weaken(question_id, 0.5f);

        json result = {
            {"question_id", question_id.to_string()},
            {"answer_id", answer_id.to_string()},
            {"promoted_to_wisdom", promote}
        };

        return {false, promote ? "Answer promoted to wisdom" : "Question answered", result};
    }

    ToolResult tool_connect(const json& params) {
        std::string from_id_str = params.at("from_id");
        std::string to_id_str = params.at("to_id");
        std::string edge_type_str = params.value("edge_type", "relates_to");
        float weight = params.value("weight", 0.8f);

        NodeId from_id = NodeId::from_string(from_id_str);
        NodeId to_id = NodeId::from_string(to_id_str);

        // Verify both nodes exist
        auto from_node = mind_->get(from_id);
        auto to_node = mind_->get(to_id);

        if (!from_node) {
            return {true, "Source node not found: " + from_id_str, json()};
        }
        if (!to_node) {
            return {true, "Target node not found: " + to_id_str, json()};
        }

        // Map string to EdgeType
        EdgeType edge_type = EdgeType::RelatesTo;
        if (edge_type_str == "similar") edge_type = EdgeType::Similar;
        else if (edge_type_str == "supports") edge_type = EdgeType::Supports;
        else if (edge_type_str == "contradicts") edge_type = EdgeType::Contradicts;
        else if (edge_type_str == "relates_to") edge_type = EdgeType::RelatesTo;
        else if (edge_type_str == "part_of") edge_type = EdgeType::PartOf;
        else if (edge_type_str == "is_a") edge_type = EdgeType::IsA;
        else if (edge_type_str == "mentions") edge_type = EdgeType::Mentions;

        // Create the edge
        mind_->connect(from_id, to_id, edge_type, weight);

        json result = {
            {"from_id", from_id_str},
            {"to_id", to_id_str},
            {"edge_type", edge_type_str},
            {"weight", weight}
        };

        return {false, "Edge created", result};
    }

    ToolResult tool_narrate(const json& params) {
        std::string action = params.value("action", "moment");

        if (action == "start") {
            std::string title = params.value("title", "Untitled episode");
            std::string content = params.value("content", "");
            std::string emotion = params.value("emotion", "exploration");

            std::string full_text = "[EPISODE START] " + title;
            if (!content.empty()) {
                full_text += "\n" + content;
            }
            full_text += "\nEmotion: " + emotion;

            NodeId id;
            if (mind_->has_yantra()) {
                id = mind_->remember(full_text, NodeType::StoryThread, Confidence(0.9f));
            } else {
                id = mind_->remember(NodeType::StoryThread, Vector::zeros(), Confidence(0.9f),
                                     std::vector<uint8_t>(full_text.begin(), full_text.end()));
            }

            return {false, "Episode started: " + title, {{"episode_id", id.to_string()}, {"title", title}}};

        } else if (action == "moment") {
            std::string content = params.value("content", "");
            std::string emotion = params.value("emotion", "routine");
            std::string episode_id_str = params.value("episode_id", "");

            if (content.empty()) {
                return {true, "Content required for moment", json()};
            }

            std::string full_text = "[MOMENT] " + content + " | " + emotion;

            NodeId id;
            if (mind_->has_yantra()) {
                id = mind_->remember(full_text, NodeType::Episode, Confidence(0.7f));
            } else {
                id = mind_->remember(NodeType::Episode, Vector::zeros(), Confidence(0.7f),
                                     std::vector<uint8_t>(full_text.begin(), full_text.end()));
            }

            // Connect to episode if specified
            if (!episode_id_str.empty()) {
                NodeId episode_id = NodeId::from_string(episode_id_str);
                mind_->connect(episode_id, id, EdgeType::AppliedIn, 1.0f);
            }

            return {false, "Moment recorded", {{"moment_id", id.to_string()}, {"emotion", emotion}}};

        } else if (action == "end") {
            std::string episode_id_str = params.value("episode_id", "");
            std::string content = params.value("content", "");
            std::string emotion = params.value("emotion", "satisfaction");

            if (episode_id_str.empty()) {
                return {true, "Episode ID required to end", json()};
            }

            NodeId episode_id = NodeId::from_string(episode_id_str);
            auto episode = mind_->get(episode_id);
            if (!episode) {
                return {true, "Episode not found", json()};
            }

            // Add closing marker
            std::string close_text = "[EPISODE END] " + content + " | " + emotion;
            NodeId close_id;
            if (mind_->has_yantra()) {
                close_id = mind_->remember(close_text, NodeType::Episode, Confidence(0.8f));
            } else {
                close_id = mind_->remember(NodeType::Episode, Vector::zeros(), Confidence(0.8f),
                                          std::vector<uint8_t>(close_text.begin(), close_text.end()));
            }
            mind_->connect(episode_id, close_id, EdgeType::EvolvedFrom, 1.0f);

            return {false, "Episode ended", {{"episode_id", episode_id_str}, {"emotion", emotion}}};

        } else if (action == "recall") {
            std::string query = params.value("query", "episode story");

            if (!mind_->has_yantra()) {
                return {true, "Yantra not ready for recall", json()};
            }

            auto results = mind_->recall(query, 10);

            // Filter for story-related nodes
            json stories = json::array();
            std::ostringstream ss;
            ss << "Story recall for: " << query << "\n";

            for (const auto& r : results) {
                if (r.type == NodeType::StoryThread || r.type == NodeType::Episode) {
                    stories.push_back({
                        {"id", r.id.to_string()},
                        {"text", r.text.substr(0, 150)},
                        {"type", node_type_to_string(r.type)},
                        {"similarity", r.similarity}
                    });
                    ss << "\n[" << (r.similarity * 100) << "%] " << r.text.substr(0, 80) << "...";
                }
            }

            return {false, ss.str(), {{"stories", stories}}};

        } else if (action == "list") {
            auto threads = mind_->query_by_type(NodeType::StoryThread);

            json list = json::array();
            std::ostringstream ss;
            ss << "Story threads (" << threads.size() << "):\n";

            for (const auto& node : threads) {
                std::string text(node.payload.begin(), node.payload.end());
                list.push_back({
                    {"id", node.id.to_string()},
                    {"text", text.substr(0, 100)},
                    {"confidence", node.kappa.effective()}
                });
                ss << "  - " << text.substr(0, 60) << "...\n";
            }

            return {false, ss.str(), {{"threads", list}}};
        }

        return {true, "Unknown narrate action: " + action, json()};
    }

    ToolResult tool_feedback(const json& params) {
        std::string memory_id_str = params.at("memory_id");
        bool helpful = params.at("helpful");
        std::string context = params.value("context", "");

        NodeId memory_id = NodeId::from_string(memory_id_str);
        auto node = mind_->get(memory_id);

        if (!node) {
            return {true, "Memory not found: " + memory_id_str, json()};
        }

        // Apply feedback - strengthen or weaken
        float delta = helpful ? 0.1f : -0.15f;  // Negative feedback slightly stronger

        if (helpful) {
            mind_->strengthen(memory_id, delta);
        } else {
            mind_->weaken(memory_id, -delta);
        }

        // Record the feedback event
        std::string feedback_text = (helpful ? "[HELPFUL] " : "[MISLEADING] ");
        feedback_text += "Memory: " + memory_id_str;
        if (!context.empty()) {
            feedback_text += " | " + context;
        }

        // Store as signal (fast decay)
        if (mind_->has_yantra()) {
            mind_->remember(feedback_text, NodeType::Episode, Confidence(0.5f));
        }

        json result = {
            {"memory_id", memory_id_str},
            {"helpful", helpful},
            {"delta", delta},
            {"new_confidence", node->kappa.effective() + delta}
        };

        return {false, helpful ? "Memory strengthened" : "Memory weakened", result};
    }

    // Helper to detect project name from cwd or environment
    static std::string detect_project() {
        // Try CLAUDE_PROJECT env first
        if (const char* proj = std::getenv("CLAUDE_PROJECT")) {
            return proj;
        }

        // Fall back to cwd basename
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd))) {
            std::string path(cwd);
            size_t last_slash = path.find_last_of('/');
            if (last_slash != std::string::npos && last_slash + 1 < path.size()) {
                return path.substr(last_slash + 1);
            }
            return path;
        }

        return "";
    }

    ToolResult tool_ledger(const json& params) {
        std::string action = params.at("action");
        std::string session_id = params.value("session_id", "");

        // Get project from params or auto-detect from cwd
        std::string project = params.value("project", "");
        if (project.empty()) {
            project = detect_project();
        }

        if (action == "save") {
            // Build ledger JSON from provided components
            // Auto-populate with rich state when not provided
            json ledger_json = json::object();

            // Soul state: coherence + statistics
            if (params.contains("soul_state")) {
                ledger_json["soul_state"] = params["soul_state"];
            } else {
                Coherence c = mind_->coherence();
                ledger_json["soul_state"] = {
                    {"coherence", {
                        {"tau_k", c.tau_k()},
                        {"local", c.local},
                        {"global", c.global},
                        {"temporal", c.temporal},
                        {"structural", c.structural}
                    }},
                    {"statistics", {
                        {"total_nodes", mind_->size()},
                        {"hot_nodes", mind_->hot_size()},
                        {"warm_nodes", mind_->warm_size()},
                        {"cold_nodes", mind_->cold_size()}
                    }},
                    {"timestamp", now()}
                };
            }

            // Work state: active intentions + recent activity
            if (params.contains("work_state")) {
                ledger_json["work_state"] = params["work_state"];
            } else {
                // Auto-populate with active intentions and recent work
                json work = json::object();

                // Get active intentions by recalling Intention nodes
                auto intents = mind_->recall("intention want goal", 10, 0.3f);
                json active_intents = json::array();
                for (const auto& r : intents) {
                    if (r.type == NodeType::Intention && r.confidence.mu > 0.5f) {
                        std::string text = r.text;
                        if (text.length() > 150) {
                            text = text.substr(0, 150) + "...";
                        }
                        active_intents.push_back(text);
                    }
                }
                if (!active_intents.empty()) {
                    work["active_intentions"] = active_intents;
                }

                // Get recent observations (last 5)
                auto recent = mind_->recall("session work progress observation", 5, 0.25f);
                if (!recent.empty()) {
                    json recent_obs = json::array();
                    for (const auto& r : recent) {
                        if (r.type != NodeType::Intention && r.type != NodeType::Ledger) {
                            std::string text = r.text;
                            if (text.length() > 120) {
                                text = text.substr(0, 120) + "...";
                            }
                            recent_obs.push_back(text);
                        }
                    }
                    if (!recent_obs.empty()) {
                        work["recent_observations"] = recent_obs;
                    }
                }

                ledger_json["work_state"] = work;
            }

            // Continuation: what to resume with
            if (params.contains("continuation")) {
                ledger_json["continuation"] = params["continuation"];
            } else {
                ledger_json["continuation"] = json::object();
            }

            NodeId id = mind_->save_ledger(ledger_json.dump(), session_id, project);

            json result = {
                {"id", id.to_string()},
                {"session_id", session_id},
                {"project", project},
                {"ledger", ledger_json}
            };

            return {false, "Ledger saved: " + id.to_string(), result};

        } else if (action == "load") {
            auto ledger = mind_->load_ledger(session_id, project);

            if (!ledger) {
                std::string msg = "No ledger found";
                if (!project.empty()) msg += " for project: " + project;
                if (!session_id.empty()) msg += ", session: " + session_id;
                return {false, msg, json()};
            }

            json ledger_json;
            try {
                ledger_json = json::parse(ledger->second);
            } catch (...) {
                ledger_json = {{"raw", ledger->second}};
            }

            json result = {
                {"id", ledger->first.to_string()},
                {"ledger", ledger_json}
            };

            // Build narrative summary for resumption
            std::ostringstream narrative;
            narrative << "=== Session Ledger ===\n\n";

            // Soul state summary
            if (ledger_json.contains("soul_state")) {
                auto& ss = ledger_json["soul_state"];
                narrative << "## Soul State\n";
                if (ss.contains("coherence")) {
                    auto& coh = ss["coherence"];
                    if (coh.is_string()) {
                        narrative << "Coherence: " << coh.get<std::string>() << "\n";
                    } else if (coh.is_object()) {
                        float tau_k = coh.value("tau_k", 0.0f);
                        narrative << "Coherence: " << std::fixed << std::setprecision(2) << tau_k << "\n";
                    } else if (coh.is_number()) {
                        narrative << "Coherence: " << std::fixed << std::setprecision(2) << coh.get<float>() << "\n";
                    }
                }
                if (ss.contains("statistics")) {
                    auto& stats = ss["statistics"];
                    narrative << "Nodes: " << stats.value("total_nodes", 0)
                              << " (" << stats.value("hot_nodes", 0) << " hot)\n";
                }
                narrative << "\n";
            }

            // Work state - what we were doing
            if (ledger_json.contains("work_state") && !ledger_json["work_state"].empty()) {
                auto& ws = ledger_json["work_state"];
                narrative << "## Where We Were\n";

                if (ws.contains("active_intentions") && !ws["active_intentions"].empty()) {
                    narrative << "\n### Active Intentions:\n";
                    for (const auto& intent : ws["active_intentions"]) {
                        std::string text = intent.is_string() ? intent.get<std::string>() : intent.dump();
                        narrative << "- " << text << "\n";
                    }
                }

                if (ws.contains("recent_observations") && !ws["recent_observations"].empty()) {
                    narrative << "\n### Recent Work:\n";
                    for (const auto& obs : ws["recent_observations"]) {
                        std::string text = obs.is_string() ? obs.get<std::string>() : obs.dump();
                        narrative << "- " << text << "\n";
                    }
                }

                if (ws.contains("todos") && !ws["todos"].empty()) {
                    narrative << "\n### Pending Todos:\n";
                    for (const auto& todo : ws["todos"]) {
                        std::string text = todo.is_string() ? todo.get<std::string>() : todo.dump();
                        narrative << "- " << text << "\n";
                    }
                }
                narrative << "\n";
            }

            // Continuation - what to do next
            if (ledger_json.contains("continuation") && !ledger_json["continuation"].empty()) {
                auto& cont = ledger_json["continuation"];
                narrative << "## What To Do Next\n";

                if (cont.contains("reason")) {
                    auto& reason = cont["reason"];
                    narrative << "Last session ended: " << (reason.is_string() ? reason.get<std::string>() : reason.dump()) << "\n";
                }

                if (cont.contains("next_steps") && !cont["next_steps"].empty()) {
                    narrative << "\n### Next Steps:\n";
                    for (const auto& step : cont["next_steps"]) {
                        std::string text = step.is_string() ? step.get<std::string>() : step.dump();
                        narrative << "- " << text << "\n";
                    }
                }

                if (cont.contains("critical") && !cont["critical"].empty()) {
                    narrative << "\n### Critical Notes:\n";
                    auto& critical = cont["critical"];
                    if (critical.is_array()) {
                        for (const auto& note : critical) {
                            std::string text = note.is_string() ? note.get<std::string>() : note.dump();
                            narrative << "⚠️ " << text << "\n";
                        }
                    } else {
                        narrative << "⚠️ " << (critical.is_string() ? critical.get<std::string>() : critical.dump()) << "\n";
                    }
                }

                if (cont.contains("deferred") && !cont["deferred"].empty()) {
                    narrative << "\n### Deferred:\n";
                    for (const auto& item : cont["deferred"]) {
                        std::string text = item.is_string() ? item.get<std::string>() : item.dump();
                        narrative << "- " << text << "\n";
                    }
                }
            }

            return {false, narrative.str(), result};

        } else if (action == "update") {
            std::string ledger_id_str = params.value("ledger_id", "");

            if (ledger_id_str.empty()) {
                // Load current ledger first
                auto current = mind_->load_ledger(session_id, project);
                if (!current) {
                    return {true, "No ledger to update", json()};
                }
                ledger_id_str = current->first.to_string();
            }

            NodeId ledger_id = NodeId::from_string(ledger_id_str);

            // Build updated ledger
            json updated = json::object();

            // Load existing ledger content
            auto existing = mind_->load_ledger(session_id, project);
            if (existing) {
                try {
                    updated = json::parse(existing->second);
                } catch (...) {}
            }

            // Merge updates
            if (params.contains("soul_state")) {
                updated["soul_state"] = params["soul_state"];
            }
            if (params.contains("work_state")) {
                updated["work_state"] = params["work_state"];
            }
            if (params.contains("continuation")) {
                updated["continuation"] = params["continuation"];
            }

            bool success = mind_->update_ledger(ledger_id, updated.dump());

            if (!success) {
                return {true, "Failed to update ledger: " + ledger_id_str, json()};
            }

            json result = {
                {"id", ledger_id_str},
                {"ledger", updated}
            };

            return {false, "Ledger updated: " + ledger_id_str, result};

        } else if (action == "list") {
            auto ledgers = mind_->list_ledgers(10, project);

            json list = json::array();
            std::ostringstream ss;
            ss << "Ledgers";
            if (!project.empty()) ss << " [" << project << "]";
            ss << " (" << ledgers.size() << "):\n";

            for (const auto& [id, timestamp] : ledgers) {
                list.push_back({
                    {"id", id.to_string()},
                    {"created", timestamp}
                });
                ss << "  " << id.to_string() << " (created: " << timestamp << ")\n";
            }

            return {false, ss.str(), {{"ledgers", list}}};

        } else {
            return {true, "Unknown action: " + action, json()};
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // JSON-RPC helpers
    // ═══════════════════════════════════════════════════════════════════

    static json make_result(const json& id, const json& result) {
        return {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", result}
        };
    }

    static json make_error(const json& id, int code, const std::string& message) {
        return {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error", {
                {"code", code},
                {"message", message}
            }}
        };
    }
};

} // namespace chitta
