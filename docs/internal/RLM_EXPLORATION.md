# RLM-Style Memory Exploration

## Problem

Current `full_resonate` injects top-k memories into context. This:
- Scales poorly with large memory stores (context explosion)
- May miss relevant memories outside top-k
- Doesn't follow associative chains
- Can't adapt exploration based on partial findings

## Solution: Recursive Exploration

Instead of dumping memories, let the LLM explore the memory graph dynamically.

```
Current:  query → top-k memories → inject all → answer
RLM:      query → explore iteratively → accumulate relevant → answer
```

## Architecture

### Exploration Primitives

| Tool | Purpose | Returns |
|------|---------|---------|
| `explore_start` | Begin exploration session | session_id, initial_hints |
| `explore_recall` | Semantic search (lightweight) | List of (id, title, score) - no full content |
| `explore_peek` | Get summary of a memory | 1-2 sentence summary |
| `explore_expand` | Get full content | Full memory content |
| `explore_neighbors` | Get connected nodes via triplets | List of (node, predicate, direction) |
| `explore_path` | Find path between two nodes | List of triplet hops |
| `explore_answer` | Signal exploration complete | Ends session, returns trace |

### Agent Loop

```python
class ExplorationAgent:
    def __init__(self, query: str, max_iterations: int = 10):
        self.query = query
        self.max_iterations = max_iterations
        self.context = []  # Accumulated findings
        self.trace = []    # Exploration history

    def run(self) -> ExplorationResult:
        # Initial recall for entry points
        hints = explore_recall(self.query, limit=5)
        self.trace.append(("recall", self.query, hints))

        for i in range(self.max_iterations):
            # LLM decides next action based on query + current context
            action = self.decide_action()

            if action.type == "answer":
                return ExplorationResult(
                    answer=action.content,
                    context=self.context,
                    trace=self.trace,
                    iterations=i
                )

            # Execute exploration action
            result = self.execute(action)
            self.trace.append((action.type, action.args, result))

            # Accumulate relevant findings
            if self.is_relevant(result):
                self.context.append(result)

        # Max iterations reached
        return self.force_answer()

    def decide_action(self) -> Action:
        """LLM decides: peek, expand, neighbors, recall_more, or answer"""
        prompt = f"""
Query: {self.query}

Current context ({len(self.context)} items):
{self.format_context()}

Exploration trace:
{self.format_trace()}

What should I explore next? Choose one:
- RECALL("new query") - search for more memories
- PEEK(memory_id) - get summary of a memory
- EXPAND(memory_id) - get full content (use sparingly)
- NEIGHBORS(node) - find connected concepts
- ANSWER - I have enough context to answer

Response format: ACTION(args)
"""
        return parse_action(llm.complete(prompt))
```

### Token Budget

Each action has a token cost:

| Action | Approx Tokens |
|--------|---------------|
| recall (5 results) | ~100 |
| peek | ~50 |
| expand | ~200-500 |
| neighbors | ~100 |
| decide prompt | ~300 |

Budget: 10 iterations × ~400 tokens = ~4000 tokens exploration
vs. full_resonate: 10 memories × ~300 tokens = ~3000 tokens

**Trade-off**: Similar tokens but smarter selection. RLM wins when:
- Memory store is large (top-k misses relevant items)
- Query needs associative exploration (follow chains)
- Some memories are much more relevant than others

### Implementation Options

**Option A: Server-side (RPC tool)**
```cpp
// New RPC tool: explore
DuckDBToolResult tool_explore(const json& params) {
    std::string query = params["query"];
    int max_iterations = params.value("max_iterations", 10);

    // Run exploration loop server-side
    // Uses internal LLM calls for decisions
    return run_exploration(query, max_iterations);
}
```
- Pro: Single RPC call, encapsulated
- Con: Needs LLM access in daemon, complex

**Option B: Client-side (Skill)**
```markdown
# /explore skill
Uses exploration primitives iteratively:
1. Call explore_recall for initial hints
2. Loop: decide action → call primitive → accumulate
3. Return answer with trace
```
- Pro: Uses existing Claude context, simpler
- Con: Multiple RPC round-trips, latency

**Option C: Hybrid (Recommended)**
- Primitives in daemon (fast, lightweight)
- Agent loop in skill (uses Claude for decisions)
- Best of both: fast primitives, smart decisions

## Phase 1: Primitives (Task 2)

Add to `duckdb_handler.hpp`:

```cpp
// Lightweight recall - returns summaries only
DuckDBToolResult tool_explore_recall(const json& params);

// Get first N chars of memory content
DuckDBToolResult tool_explore_peek(const json& params);

// Get triplet neighbors
DuckDBToolResult tool_explore_neighbors(const json& params);
```

## Phase 2: Skill (Task 3-4)

Create `skills/explore/SKILL.md`:
- Implements agent loop
- Uses primitives
- Shows exploration trace
- Compares to full_resonate

## Success Metrics

1. **Token efficiency**: Same or fewer tokens than full_resonate
2. **Accuracy**: Finds relevant memories that top-k misses
3. **Latency**: Acceptable (<5s for typical query)
4. **Scalability**: Works with 10k+ memories
