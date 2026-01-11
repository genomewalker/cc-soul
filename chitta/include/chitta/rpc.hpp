#pragma once
// RPC Server: Model Context Protocol for soul integration
//
// Implements JSON-RPC 2.0 over stdio for Claude integration.
// This is not a minimal implementation - it is a proper RPC server
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
#include <cmath>

namespace chitta {

// Helper: safe float-to-percentage conversion (handles NaN/infinity)
inline int safe_pct(float value) {
    if (std::isnan(value) || std::isinf(value)) return 0;
    return static_cast<int>(std::clamp(value * 100.0f, -999.0f, 999.0f));
}

using json = nlohmann::json;

// JSON-RPC 2.0 error codes
namespace rpc_error {
    constexpr int PARSE_ERROR = -32700;
    constexpr int INVALID_REQUEST = -32600;
    constexpr int METHOD_NOT_FOUND = -32601;
    constexpr int INVALID_PARAMS = -32602;
    constexpr int INTERNAL_ERROR = -32603;
    // RPC-specific errors
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

// RPC Server implementation
class RpcServer {
public:
    explicit RpcServer(std::shared_ptr<Mind> mind, std::string server_name = "chitta")
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

    // Session learning tracker - append to session file
    void track_learning(const std::string& node_id, const std::string& type, const std::string& title) {
        const char* home = getenv("HOME");
        if (!home) return;

        std::string session_file = std::string(home) + "/.claude/mind/.session_learned";
        std::ofstream ofs(session_file, std::ios::app);
        if (ofs) {
            auto now = std::chrono::system_clock::now();
            auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count();
            ofs << node_id << "|" << type << "|" << title << "|" << epoch << "\n";
        }
    }

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

        // Tool: update - Update an existing node's content (for ε-optimization migration)
        tools_.push_back({
            "update",
            "Update an existing node's content. Used for ε-optimization: convert verbose content "
            "to high-epiplexity pattern format. The node's embedding is recomputed from new content.",
            {
                {"type", "object"},
                {"properties", {
                    {"id", {
                        {"type", "string"},
                        {"description", "Node ID to update"}
                    }},
                    {"content", {
                        {"type", "string"},
                        {"description", "New content (will replace existing)"}
                    }},
                    {"keep_metadata", {
                        {"type", "boolean"},
                        {"default", true},
                        {"description", "Keep original timestamps and confidence"}
                    }}
                }},
                {"required", {"id", "content"}}
            }
        });
        handlers_["update"] = [this](const json& params) { return tool_update(params); };

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
                        {"enum", {"micro", "sparse", "normal", "dense", "full"}},
                        {"default", "normal"},
                        {"description", "Detail level: micro (titles only, 50+), sparse (titles, 25), normal (truncated text, 10), dense (full context, 5), full (complete, 3)"}
                    }},
                    {"tag", {
                        {"type", "string"},
                        {"description", "Filter by exact tag match (e.g., 'thread:abc123')"}
                    }},
                    {"exclude_tag", {
                        {"type", "string"},
                        {"description", "Exclude nodes with this tag (e.g., 'ε-processed')"}
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

        // Tool: tag - Add or remove tags from nodes
        tools_.push_back({
            "tag",
            "Add or remove tags from a node. Used for ε-yajna tracking (mark nodes as processed) "
            "and organizing memories by categories.",
            {
                {"type", "object"},
                {"properties", {
                    {"id", {
                        {"type", "string"},
                        {"description", "Node ID to tag"}
                    }},
                    {"add", {
                        {"type", "string"},
                        {"description", "Tag to add"}
                    }},
                    {"remove", {
                        {"type", "string"},
                        {"description", "Tag to remove"}
                    }}
                }},
                {"required", {"id"}}
            }
        });
        handlers_["tag"] = [this](const json& params) { return tool_tag(params); };

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

        // Phase 3 Analysis tools
        tools_.push_back({
            "propagate",
            "Propagate confidence change through graph. When a node proves useful/wrong, "
            "connected nodes are affected proportionally. Use after feedback to spread impact.",
            {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Node ID to propagate from"}}},
                    {"delta", {{"type", "number"}, {"minimum", -0.5}, {"maximum", 0.5},
                              {"description", "Confidence change (+/- boost/penalty)"}}},
                    {"decay_factor", {{"type", "number"}, {"minimum", 0.1}, {"maximum", 0.9}, {"default", 0.5},
                                     {"description", "How much propagation decays per hop"}}},
                    {"max_depth", {{"type", "integer"}, {"minimum", 1}, {"maximum", 5}, {"default", 3}}}
                }},
                {"required", {"id", "delta"}}
            }
        });
        handlers_["propagate"] = [this](const json& params) { return tool_propagate(params); };

        tools_.push_back({
            "forget",
            "Deliberately forget a node with cascade effects. Connected nodes weaken, "
            "edges rewire around the forgotten node. Audit trail preserved.",
            {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}, {"description", "Node ID to forget"}}},
                    {"cascade", {{"type", "boolean"}, {"default", true},
                                {"description", "Weaken connected nodes"}}},
                    {"rewire", {{"type", "boolean"}, {"default", true},
                               {"description", "Reconnect edges around forgotten node"}}},
                    {"cascade_strength", {{"type", "number"}, {"minimum", 0.05}, {"maximum", 0.3}, {"default", 0.1}}}
                }},
                {"required", {"id"}}
            }
        });
        handlers_["forget"] = [this](const json& params) { return tool_forget(params); };

        tools_.push_back({
            "epistemic_state",
            "Analyze what I know vs uncertain about. Shows knowledge gaps, "
            "unanswered questions, low-confidence beliefs, and coverage by domain.",
            {
                {"type", "object"},
                {"properties", {
                    {"domain", {{"type", "string"}, {"description", "Filter by domain (optional)"}}},
                    {"min_confidence", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"default", 0.3},
                                       {"description", "Threshold for 'certain' knowledge"}}},
                    {"limit", {{"type", "integer"}, {"minimum", 5}, {"maximum", 50}, {"default", 20}}}
                }},
                {"required", json::array()}
            }
        });
        handlers_["epistemic_state"] = [this](const json& params) { return tool_epistemic_state(params); };

        tools_.push_back({
            "bias_scan",
            "Detect patterns in my own beliefs and decisions. Looks for over-representation "
            "of topics, confidence inflation, and decision clustering.",
            {
                {"type", "object"},
                {"properties", {
                    {"sample_size", {{"type", "integer"}, {"minimum", 50}, {"maximum", 500}, {"default", 100}}}
                }},
                {"required", json::array()}
            }
        });
        handlers_["bias_scan"] = [this](const json& params) { return tool_bias_scan(params); };

        // Phase 3.7: Competence Mapping
        tools_.push_back({
            "competence",
            "Analyze competence by domain. Shows what I'm good at (high confidence, successes) "
            "vs weak at (low confidence, failures) across different topics/projects.",
            {
                {"type", "object"},
                {"properties", {
                    {"min_samples", {{"type", "integer"}, {"minimum", 3}, {"maximum", 50}, {"default", 5},
                                    {"description", "Minimum nodes per domain to include"}}},
                    {"top_n", {{"type", "integer"}, {"minimum", 3}, {"maximum", 20}, {"default", 10}}}
                }},
                {"required", json::array()}
            }
        });
        handlers_["competence"] = [this](const json& params) { return tool_competence(params); };

        // Phase 3.8: Cross-Project Query
        tools_.push_back({
            "cross_project",
            "Query knowledge across projects. Find patterns that transfer between domains.",
            {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "What to search for across projects"}}},
                    {"source_project", {{"type", "string"}, {"description", "Project to transfer FROM (optional)"}}},
                    {"target_project", {{"type", "string"}, {"description", "Project to transfer TO (optional)"}}},
                    {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 20}, {"default", 10}}}
                }},
                {"required", {"query"}}
            }
        });
        handlers_["cross_project"] = [this](const json& params) { return tool_cross_project(params); };
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

        // Add epiplexity stats (learnable structure metric)
        auto epi_stats = mind_->compute_soul_epiplexity();
        result["epiplexity"] = {
            {"mean", epi_stats.mean},
            {"median", epi_stats.median},
            {"min", epi_stats.min},
            {"max", epi_stats.max},
            {"count", epi_stats.count}
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

            // Epiplexity (learnable structure)
            ss << "  Epiplexity (ε): " << int(epi_stats.mean * 100) << "% mean";
            ss << " (range:" << int(epi_stats.min * 100) << "-" << int(epi_stats.max * 100) << "%)\n";

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

        // Track learning for session summary
        track_learning(id.to_string(), type_str, title.empty() ? content.substr(0, 50) : title);

        std::ostringstream ss;
        ss << "Grew " << type_str << ": " << (title.empty() ? content.substr(0, 50) : title);
        ss << " (id: " << id.to_string() << ")";

        return {false, ss.str(), result};
    }

    ToolResult tool_observe(const json& params) {
        // Rate limiter: prevent observation spam (min 500ms between observations)
        static auto last_observe = std::chrono::steady_clock::time_point{};
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_observe).count();
        if (elapsed < 500 && last_observe != std::chrono::steady_clock::time_point{}) {
            return {true, "Rate limited: wait " + std::to_string(500 - elapsed) + "ms", json()};
        }
        last_observe = now;

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

        // Track learning for session summary
        track_learning(id.to_string(), "episode", title);

        return {false, "Observed: " + title, result};
    }

    // Tool: update - Update a node's content for ε-optimization
    ToolResult tool_update(const json& params) {
        std::string id_str = params.at("id");
        std::string new_content = params.at("content");
        bool keep_metadata = params.value("keep_metadata", true);

        NodeId id = NodeId::from_string(id_str);
        auto node_opt = mind_->get(id);
        if (!node_opt) {
            return {true, "Node not found: " + id_str, json()};
        }

        // Store original metadata if keeping
        Confidence original_conf = node_opt->kappa;
        Timestamp original_created = node_opt->tau_created;
        NodeType original_type = node_opt->node_type;
        auto original_tags = node_opt->tags;
        auto original_edges = node_opt->edges;

        // Compute new embedding from new content
        auto new_embedding = mind_->embed(new_content);
        if (new_embedding) {
            node_opt->nu = *new_embedding;
        }

        // Update payload
        node_opt->payload = std::vector<uint8_t>(new_content.begin(), new_content.end());

        // Restore or reset metadata
        if (keep_metadata) {
            node_opt->kappa = original_conf;
            node_opt->tau_created = original_created;
            node_opt->tags = original_tags;
            node_opt->edges = original_edges;
        }
        node_opt->tau_accessed = chitta::now();  // Touch

        // Update the node in storage
        mind_->update_node(id, *node_opt);

        // Compute new epiplexity
        float new_epsilon = mind_->compute_epiplexity(id);

        json result = {
            {"id", id_str},
            {"content_length", new_content.length()},
            {"epiplexity", new_epsilon},
            {"kept_metadata", keep_metadata}
        };

        return {false, "Updated node (ε:" + std::to_string(int(new_epsilon * 100)) + "%)", result};
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
        std::string exclude_tag = params.value("exclude_tag", "");
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
        size_t default_limit = (zoom == "micro") ? 50 :
                               (zoom == "sparse") ? 25 :
                               (zoom == "dense") ? 5 :
                               (zoom == "full") ? 3 : 10;
        size_t limit = params.value("limit", static_cast<int>(default_limit));

        // Clamp limits per zoom level
        if (zoom == "micro") {
            limit = std::clamp(limit, size_t(10), size_t(100));
        } else if (zoom == "sparse") {
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

        // Fetch extra results if we need to filter some out
        size_t fetch_limit = exclude_tag.empty() ? limit : limit * 2;

        std::vector<Recall> recalls;
        if (!tag.empty()) {
            // Tag-filtered recall (no priming support for tag queries yet)
            recalls = mind_->recall_with_tag_filter(query, tag, fetch_limit, threshold);
        } else if (primed) {
            // Session-primed recall: boost based on recent observations and intentions
            recalls = mind_->recall_primed(query, fetch_limit, threshold);
        } else {
            // Standard recall
            recalls = mind_->recall(query, fetch_limit, threshold);
        }

        // Filter out nodes with excluded tag
        if (!exclude_tag.empty()) {
            recalls.erase(
                std::remove_if(recalls.begin(), recalls.end(),
                    [this, &exclude_tag](const Recall& r) {
                        return mind_->has_tag(r.id, exclude_tag);
                    }),
                recalls.end()
            );
            // Trim to original limit
            if (recalls.size() > limit) {
                recalls.resize(limit);
            }
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

        // Compute epiplexity for results if using seeds zoom
        // (avoid overhead for other zoom levels)
        if (zoom == "seeds") {
            auto attractors = mind_->find_attractors(5);
            for (auto& r : recalls) {
                if (auto node = mind_->get(r.id)) {
                    r.epiplexity = mind_->compute_epiplexity(r.id);
                }
            }
        }

        json results_array = json::array();
        std::ostringstream ss;
        ss << "Found " << recalls.size() << " results";
        if (!tag.empty()) ss << " with tag '" << tag << "'";
        ss << " (" << zoom << " view):\n";

        Timestamp current = now();

        for (auto& r : recalls) {
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

            } else if (zoom == "micro") {
                // Micro: ultra-lean, just title + relevance (~50 chars per result)
                std::string title = extract_title(r.text, 40);
                results_array.push_back({
                    {"t", title},  // Abbreviated keys for smaller JSON
                    {"r", safe_pct(r.relevance)}  // Relevance as int %
                });
                ss << "\n[" << safe_pct(r.relevance) << "%] " << title;

            } else if (zoom == "seeds") {
                // Seeds: ε-aware injection - high-ε get minimal tokens, low-ε get more
                // This is the epiplexity-optimized format for bounded observers
                // Thresholds calibrated to current distribution (mean ~0.31, max ~0.49)
                std::string title = extract_title(r.text, 60);
                int epsilon_pct = static_cast<int>(r.epiplexity * 100);

                if (r.epiplexity > 0.38f) {
                    // High-ε (top quartile): just the seed pattern - Claude reconstructs
                    results_array.push_back({
                        {"title", title},
                        {"type", node_type_to_string(r.type)},
                        {"ε", epsilon_pct},
                        {"conf", static_cast<int>(r.confidence.mu * 100)}
                    });
                    ss << "\n[" << node_type_to_string(r.type) << "] " << title;
                    ss << " (ε:" << epsilon_pct << "%)";
                } else if (r.epiplexity > 0.25f) {
                    // Medium-ε: title only, no extra content
                    results_array.push_back({
                        {"title", title},
                        {"type", node_type_to_string(r.type)},
                        {"ε", epsilon_pct}
                    });
                    ss << "\n[" << node_type_to_string(r.type) << "] " << title;
                    ss << " (ε:" << epsilon_pct << "%)";
                } else {
                    // Low-ε: need some content, can't fully reconstruct
                    std::string snippet = r.text.length() > 150
                        ? r.text.substr(0, 150) + "..."
                        : r.text;
                    results_array.push_back({
                        {"title", title},
                        {"snippet", snippet},
                        {"type", node_type_to_string(r.type)},
                        {"ε", epsilon_pct}
                    });
                    ss << "\n[" << node_type_to_string(r.type) << "] " << snippet;
                }

            } else {
                // Normal: balanced with truncation (500 char max)
                auto result_tags = mind_->get_tags(r.id);
                std::string truncated_text = r.text.length() > 500
                    ? r.text.substr(0, 500) + "..."
                    : r.text;
                results_array.push_back({
                    {"id", r.id.to_string()},
                    {"text", truncated_text},
                    {"similarity", r.similarity},
                    {"relevance", r.relevance},
                    {"type", node_type_to_string(r.type)},
                    {"confidence", r.confidence.mu},
                    {"tags", result_tags}
                });
                ss << "\n[" << safe_pct(r.relevance) << "%] " << r.text.substr(0, 100);
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

            ss << "\n[" << safe_pct(r.relevance) << "%] " << r.text.substr(0, 100);
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

            ss << "\n[" << safe_pct(r.relevance) << "%] ";
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

    ToolResult tool_tag(const json& params) {
        std::string id_str = params.at("id");
        std::string add_tag = params.value("add", "");
        std::string remove_tag = params.value("remove", "");

        NodeId id = NodeId::from_string(id_str);

        // Verify node exists
        auto node = mind_->get(id);
        if (!node) {
            return {true, "Node not found: " + id_str, json()};
        }

        json result = {{"id", id_str}};

        if (!add_tag.empty()) {
            mind_->add_tag(id, add_tag);
            result["added"] = add_tag;
        }

        if (!remove_tag.empty()) {
            mind_->remove_tag(id, remove_tag);
            result["removed"] = remove_tag;
        }

        if (add_tag.empty() && remove_tag.empty()) {
            // Return current tags
            result["tags"] = node->tags;
            return {false, "Current tags", result};
        }

        return {false, "Tags updated", result};
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
    // Phase 3 Analysis Tools
    // ═══════════════════════════════════════════════════════════════════

    ToolResult tool_propagate(const json& params) {
        std::string id_str = params.at("id");
        float delta = params.at("delta");
        float decay_factor = params.value("decay_factor", 0.5f);
        size_t max_depth = params.value("max_depth", 3);

        NodeId id = NodeId::from_string(id_str);
        if (!mind_->get(id)) {
            return {true, "Node not found: " + id_str, json()};
        }

        auto result = mind_->propagate_confidence(id, delta, decay_factor, max_depth);

        json changes_array = json::array();
        for (const auto& [change_id, change_delta] : result.changes) {
            changes_array.push_back({
                {"id", change_id.to_string()},
                {"delta", change_delta}
            });
        }

        std::ostringstream ss;
        ss << "Propagated " << (delta >= 0 ? "+" : "") << delta
           << " to " << result.nodes_affected << " nodes"
           << " (total impact: " << result.total_delta_applied << ")";

        return {false, ss.str(), {
            {"source_id", id_str},
            {"delta", delta},
            {"nodes_affected", result.nodes_affected},
            {"total_impact", result.total_delta_applied},
            {"changes", changes_array}
        }};
    }

    ToolResult tool_forget(const json& params) {
        std::string id_str = params.at("id");
        bool cascade = params.value("cascade", true);
        bool rewire = params.value("rewire", true);
        float cascade_strength = params.value("cascade_strength", 0.1f);

        NodeId id = NodeId::from_string(id_str);
        auto node_opt = mind_->get(id);
        if (!node_opt) {
            return {true, "Node not found: " + id_str, json()};
        }

        // Save audit trail
        std::string forgotten_text(node_opt->payload.begin(), node_opt->payload.end());
        std::string audit = "FORGOTTEN: " + forgotten_text.substr(0, 100);

        // Collect edges before removal
        std::vector<NodeId> inbound, outbound;
        for (const auto& edge : node_opt->edges) {
            outbound.push_back(edge.target);
        }

        // Check reverse edges using query
        auto all_nodes = mind_->query_by_type(NodeType::Episode);  // Sample check
        for (const auto& other : all_nodes) {
            for (const auto& edge : other.edges) {
                if (edge.target == id) {
                    inbound.push_back(other.id);
                    break;
                }
            }
        }

        size_t affected = 0;
        // Cascade: weaken connected nodes
        if (cascade) {
            for (const auto& out_id : outbound) {
                mind_->weaken(out_id, cascade_strength);
                affected++;
            }
            for (const auto& in_id : inbound) {
                mind_->weaken(in_id, cascade_strength);
                affected++;
            }
        }

        // Rewire: connect inbound to outbound (skip the forgotten node)
        size_t rewired = 0;
        if (rewire && !inbound.empty() && !outbound.empty()) {
            for (const auto& in_id : inbound) {
                for (const auto& out_id : outbound) {
                    if (in_id != out_id) {
                        mind_->hebbian_strengthen(in_id, out_id, 0.1f);
                        rewired++;
                    }
                }
            }
        }

        // Remove the node
        mind_->remove_node(id);

        // Store audit trail
        if (mind_->has_yantra()) {
            mind_->remember(audit, NodeType::Episode, {"audit:forget"});
        }

        std::ostringstream ss;
        ss << "Forgotten: " << forgotten_text.substr(0, 50);
        if (cascade) ss << " (affected " << affected << " connected)";
        if (rewire) ss << " (rewired " << rewired << " paths)";

        return {false, ss.str(), {
            {"id", id_str},
            {"forgotten_preview", forgotten_text.substr(0, 100)},
            {"nodes_weakened", affected},
            {"edges_rewired", rewired}
        }};
    }

    ToolResult tool_epistemic_state(const json& params) {
        float min_confidence = params.value("min_confidence", 0.3f);
        size_t limit = params.value("limit", 20);

        // Collect epistemic data
        size_t total_nodes = 0;
        size_t gaps = 0, questions = 0, low_confidence = 0, high_confidence = 0;
        std::unordered_map<std::string, size_t> type_counts;
        std::vector<std::pair<NodeId, float>> lowest_confidence;

        mind_->for_each_node([&](const NodeId& nid, const Node& node) {
            total_nodes++;
            float conf = node.kappa.effective();

            std::string type_name = node_type_to_string(node.node_type);
            type_counts[type_name]++;

            if (node.node_type == NodeType::Gap) gaps++;
            if (node.node_type == NodeType::Question) questions++;

            if (conf < min_confidence) {
                low_confidence++;
                if (lowest_confidence.size() < limit) {
                    lowest_confidence.push_back({nid, conf});
                }
            } else {
                high_confidence++;
            }
        });

        // Sort lowest confidence
        std::sort(lowest_confidence.begin(), lowest_confidence.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });

        json uncertain_array = json::array();
        for (const auto& [nid, conf] : lowest_confidence) {
            auto node = mind_->get(nid);
            std::string text = node ? std::string(node->payload.begin(), node->payload.end()) : "";
            uncertain_array.push_back({
                {"id", nid.to_string()},
                {"confidence", conf},
                {"type", node ? node_type_to_string(node->node_type) : "unknown"},
                {"preview", text.substr(0, 60)}
            });
        }

        json type_dist = json::object();
        for (const auto& [type, count] : type_counts) {
            type_dist[type] = count;
        }

        float certainty_ratio = total_nodes > 0 ?
            static_cast<float>(high_confidence) / total_nodes : 0.0f;

        std::ostringstream ss;
        ss << "Epistemic State:\n";
        ss << "  Total knowledge: " << total_nodes << " nodes\n";
        ss << "  High confidence (≥" << static_cast<int>(min_confidence * 100) << "%): "
           << high_confidence << " (" << static_cast<int>(certainty_ratio * 100) << "%)\n";
        ss << "  Low confidence: " << low_confidence << "\n";
        ss << "  Open questions: " << questions << "\n";
        ss << "  Knowledge gaps: " << gaps << "\n";

        return {false, ss.str(), {
            {"total_nodes", total_nodes},
            {"high_confidence", high_confidence},
            {"low_confidence", low_confidence},
            {"questions", questions},
            {"gaps", gaps},
            {"certainty_ratio", certainty_ratio},
            {"type_distribution", type_dist},
            {"most_uncertain", uncertain_array}
        }};
    }

    ToolResult tool_bias_scan(const json& params) {
        size_t sample_size = params.value("sample_size", 100);

        // Collect samples for analysis
        std::vector<const Node*> samples;
        std::unordered_map<std::string, size_t> type_counts;
        std::unordered_map<std::string, std::vector<float>> confidence_by_type;
        size_t total_edges = 0;
        float total_confidence = 0.0f;

        mind_->for_each_node([&](const NodeId& nid, const Node& node) {
            (void)nid;
            if (samples.size() < sample_size) {
                std::string type = node_type_to_string(node.node_type);
                type_counts[type]++;
                confidence_by_type[type].push_back(node.kappa.effective());
                total_edges += node.edges.size();
                total_confidence += node.kappa.effective();
                samples.push_back(&node);
            }
        });

        if (samples.empty()) {
            return {false, "No data for bias analysis", {{"biases", json::array()}}};
        }

        // Analyze biases
        json biases = json::array();
        float avg_confidence = total_confidence / samples.size();
        float avg_edges = static_cast<float>(total_edges) / samples.size();

        // 1. Type imbalance
        size_t max_type_count = 0;
        std::string dominant_type;
        for (const auto& [type, count] : type_counts) {
            if (count > max_type_count) {
                max_type_count = count;
                dominant_type = type;
            }
        }
        float dominance_ratio = static_cast<float>(max_type_count) / samples.size();
        if (dominance_ratio > 0.5f) {
            biases.push_back({
                {"type", "type_dominance"},
                {"description", "Over-representation of " + dominant_type + " nodes"},
                {"severity", dominance_ratio},
                {"dominant_type", dominant_type},
                {"percentage", static_cast<int>(dominance_ratio * 100)}
            });
        }

        // 2. Confidence inflation/deflation
        if (avg_confidence > 0.85f) {
            biases.push_back({
                {"type", "confidence_inflation"},
                {"description", "Average confidence unusually high - may be overconfident"},
                {"severity", avg_confidence},
                {"average_confidence", avg_confidence}
            });
        } else if (avg_confidence < 0.4f) {
            biases.push_back({
                {"type", "confidence_deflation"},
                {"description", "Average confidence low - may be under-trusting knowledge"},
                {"severity", 1.0f - avg_confidence},
                {"average_confidence", avg_confidence}
            });
        }

        // 3. Connectivity bias
        if (avg_edges < 1.0f) {
            biases.push_back({
                {"type", "isolation"},
                {"description", "Nodes poorly connected - knowledge fragmented"},
                {"severity", 1.0f - avg_edges},
                {"average_edges", avg_edges}
            });
        } else if (avg_edges > 10.0f) {
            biases.push_back({
                {"type", "over_connection"},
                {"description", "Nodes heavily interconnected - may lack discrimination"},
                {"severity", avg_edges / 20.0f},
                {"average_edges", avg_edges}
            });
        }

        // 4. Type confidence variance
        for (const auto& [type, confs] : confidence_by_type) {
            if (confs.size() < 5) continue;
            float type_avg = 0.0f;
            for (float c : confs) type_avg += c;
            type_avg /= confs.size();

            if (std::abs(type_avg - avg_confidence) > 0.2f) {
                biases.push_back({
                    {"type", "type_confidence_bias"},
                    {"description", type + " has " + (type_avg > avg_confidence ? "higher" : "lower") +
                                   " confidence than average"},
                    {"node_type", type},
                    {"type_average", type_avg},
                    {"overall_average", avg_confidence}
                });
            }
        }

        std::ostringstream ss;
        ss << "Bias Scan (" << samples.size() << " samples):\n";
        if (biases.empty()) {
            ss << "  No significant biases detected\n";
        } else {
            ss << "  Found " << biases.size() << " potential bias(es)\n";
            for (const auto& b : biases) {
                ss << "  - " << b["description"].get<std::string>() << "\n";
            }
        }

        json type_dist = json::object();
        for (const auto& [type, count] : type_counts) {
            type_dist[type] = count;
        }

        return {false, ss.str(), {
            {"biases", biases},
            {"sample_size", samples.size()},
            {"average_confidence", avg_confidence},
            {"average_edges", avg_edges},
            {"type_distribution", type_dist}
        }};
    }

    // Phase 3.7: Competence Mapping
    ToolResult tool_competence(const json& params) {
        size_t min_samples = params.value("min_samples", 5);
        size_t top_n = params.value("top_n", 10);

        // Aggregate by domain (extracted from tags and content)
        struct DomainStats {
            size_t count = 0;
            float total_confidence = 0.0f;
            size_t failures = 0;
            size_t wisdom = 0;
            std::vector<std::string> sample_titles;
        };
        std::unordered_map<std::string, DomainStats> domains;

        mind_->for_each_node([&](const NodeId& nid, const Node& node) {
            // Extract domain from tags (look for [project] or domain: patterns)
            std::string text(node.payload.begin(), node.payload.end());
            std::string domain = "general";

            // Check for [project] pattern at start
            if (text.size() > 2 && text[0] == '[') {
                size_t end = text.find(']');
                if (end != std::string::npos && end < 50) {
                    domain = text.substr(1, end - 1);
                }
            }

            // Also check tags
            auto tags = mind_->get_tags(nid);
            for (const auto& tag : tags) {
                if (tag.find("project:") == 0) {
                    domain = tag.substr(8);
                    break;
                }
            }

            auto& stats = domains[domain];
            stats.count++;
            stats.total_confidence += node.kappa.effective();

            if (node.node_type == NodeType::Failure) stats.failures++;
            if (node.node_type == NodeType::Wisdom) stats.wisdom++;

            // Sample titles
            if (stats.sample_titles.size() < 3) {
                std::string title = text.substr(0, 60);
                if (text.size() > 60) title += "...";
                stats.sample_titles.push_back(title);
            }
        });

        // Calculate competence scores and sort
        struct CompetenceScore {
            std::string domain;
            float score;  // Higher = more competent
            float avg_confidence;
            size_t count;
            size_t failures;
            size_t wisdom;
            std::vector<std::string> samples;
        };
        std::vector<CompetenceScore> scores;

        for (const auto& [domain, stats] : domains) {
            if (stats.count < min_samples) continue;

            float avg_conf = stats.total_confidence / stats.count;
            // Competence = avg_confidence + wisdom_ratio - failure_ratio
            float wisdom_ratio = static_cast<float>(stats.wisdom) / stats.count;
            float failure_ratio = static_cast<float>(stats.failures) / stats.count;
            float score = avg_conf + (wisdom_ratio * 0.3f) - (failure_ratio * 0.5f);

            scores.push_back({domain, score, avg_conf, stats.count,
                             stats.failures, stats.wisdom, stats.sample_titles});
        }

        // Sort by score
        std::sort(scores.begin(), scores.end(),
                  [](const auto& a, const auto& b) { return a.score > b.score; });

        // Build output
        json strengths = json::array();
        json weaknesses = json::array();
        std::ostringstream ss;

        ss << "Competence Analysis (" << scores.size() << " domains):\n\n";
        ss << "STRENGTHS (top " << std::min(top_n, scores.size()) << "):\n";

        for (size_t i = 0; i < std::min(top_n, scores.size()); ++i) {
            const auto& s = scores[i];
            strengths.push_back({
                {"domain", s.domain},
                {"score", s.score},
                {"avg_confidence", s.avg_confidence},
                {"count", s.count},
                {"wisdom", s.wisdom},
                {"failures", s.failures}
            });
            ss << "  [" << static_cast<int>(s.score * 100) << "%] " << s.domain
               << " (" << s.count << " nodes, " << s.wisdom << " wisdom)\n";
        }

        ss << "\nWEAKNESSES (bottom " << std::min(top_n, scores.size()) << "):\n";

        for (size_t i = scores.size(); i > 0 && scores.size() - i < top_n; --i) {
            const auto& s = scores[i - 1];
            weaknesses.push_back({
                {"domain", s.domain},
                {"score", s.score},
                {"avg_confidence", s.avg_confidence},
                {"count", s.count},
                {"wisdom", s.wisdom},
                {"failures", s.failures}
            });
            ss << "  [" << static_cast<int>(s.score * 100) << "%] " << s.domain
               << " (" << s.count << " nodes, " << s.failures << " failures)\n";
        }

        return {false, ss.str(), {
            {"strengths", strengths},
            {"weaknesses", weaknesses},
            {"total_domains", scores.size()}
        }};
    }

    // Phase 3.8: Cross-Project Query
    ToolResult tool_cross_project(const json& params) {
        std::string query = params.at("query");
        std::string source_project = params.value("source_project", "");
        std::string target_project = params.value("target_project", "");
        size_t limit = params.value("limit", 10);

        if (!mind_->has_yantra()) {
            return {true, "Yantra not ready for cross-project search", json()};
        }

        // Search across all projects
        auto all_results = mind_->recall(query, limit * 3);

        // Group by project
        std::unordered_map<std::string, std::vector<const Recall*>> by_project;

        for (const auto& r : all_results) {
            std::string project = "general";

            // Extract project from content
            if (r.text.size() > 2 && r.text[0] == '[') {
                size_t end = r.text.find(']');
                if (end != std::string::npos && end < 50) {
                    project = r.text.substr(1, end - 1);
                }
            }

            // Check tags
            auto tags = mind_->get_tags(r.id);
            for (const auto& tag : tags) {
                if (tag.find("project:") == 0) {
                    project = tag.substr(8);
                    break;
                }
            }

            // Filter by source/target if specified
            if (!source_project.empty() && project != source_project) continue;

            by_project[project].push_back(&r);
        }

        // Build transferable patterns
        json projects = json::object();
        json transferable = json::array();
        std::ostringstream ss;

        ss << "Cross-Project Query: " << query << "\n\n";

        for (const auto& [project, results] : by_project) {
            json proj_results = json::array();
            size_t shown = 0;

            for (const auto* rp : results) {
                if (shown++ >= limit) break;
                proj_results.push_back({
                    {"id", rp->id.to_string()},
                    {"text", rp->text.substr(0, 150)},
                    {"relevance", rp->relevance},
                    {"type", node_type_to_string(rp->type)}
                });
            }

            projects[project] = proj_results;
            ss << "[" << project << "] " << results.size() << " results\n";

            // Mark high-relevance wisdom as transferable
            for (const auto* rp : results) {
                if (rp->type == NodeType::Wisdom && rp->relevance > 0.5f) {
                    transferable.push_back({
                        {"from_project", project},
                        {"id", rp->id.to_string()},
                        {"pattern", rp->text.substr(0, 100)},
                        {"relevance", rp->relevance}
                    });
                }
            }
        }

        if (!transferable.empty()) {
            ss << "\nTRANSFERABLE PATTERNS (" << transferable.size() << "):\n";
            for (const auto& t : transferable) {
                ss << "  From [" << t["from_project"].get<std::string>() << "]: "
                   << t["pattern"].get<std::string>() << "\n";
            }
        }

        return {false, ss.str(), {
            {"projects", projects},
            {"transferable", transferable},
            {"query", query}
        }};
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
