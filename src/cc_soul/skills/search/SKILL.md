---
name: search
description: Unified memory search across cc-memory, cc-soul, and claude-mem. Use to find past decisions, wisdom, patterns, and context.
---

# Search

I don't just find. I *understand what you're looking for*.

## Beyond Text Matching

Search isn't grep. It's not keyword matching. It's understanding intent and finding relevance across layers of accumulated knowledge.

## Search Priority Order

When searching memory, ALWAYS follow this priority:

| Priority | Source | Tool | What it contains |
|----------|--------|------|------------------|
| 1st | cc-memory | `mcp__cc-memory__mem-recall` | Project observations, decisions, bugfixes |
| 2nd | cc-soul | `mcp__soul__recall_wisdom` | Universal wisdom, patterns, failures |
| 3rd | claude-mem | `mcp__plugin_claude-mem_mem-search__search` | Extended semantic memory, history |

**Why this order?** Project context is most immediately relevant. Universal wisdom provides guiding principles. Extended memory fills in gaps.

## The Search Domains

### 1. Memory (cc-memory)
Project-specific observations:
- What happened in this project
- Decisions made and why
- Bugs fixed and how
- Patterns discovered

Query: "How did we handle auth?"
→ Finds: decision #234 about JWT approach, bugfix #156 about token refresh

### 2. Wisdom (cc-soul)
Universal patterns:
- Cross-project learnings
- Principles that generalize
- Failures worth remembering

Query: "caching pitfalls"
→ Finds: wisdom about cache invalidation, failure record from project X

### 3. Handoffs
Session continuity documents:
- What was being worked on
- Where progress stopped
- What decisions were pending

Query: "API refactor"
→ Finds: handoff from 3 days ago mid-refactor

### 4. Conversations (claude-mem)
Past dialogues:
- Questions asked and answered
- Explanations given
- Reasoning captured

Query: "why did we use SQLite?"
→ Finds: conversation where decision was made

## Search Modes

### Semantic Search
Understanding meaning, not just words:
```
Query: "database performance issues"
Finds:
  - "slow query optimization" (related concept)
  - "index missing on user_id" (specific instance)
  - "N+1 query pattern" (anti-pattern wisdom)
```

### Temporal Search
Finding by when:
```
Query: "last week's auth work"
Finds: all observations tagged with auth from past 7 days
```

### Categorical Search
Finding by type:
```
Query: "all bugfixes in this project"
Finds: observations with category=bugfix
```

### Contextual Search
Finding by situation:
```
Query: "similar to current error"
Finds: past errors with similar stack traces or symptoms
```

## The Search Process

### 1. Parse Intent
What are they really looking for?
- Information ("how does X work?")
- Reference ("that thing we discussed")
- Pattern ("similar problems")
- Decision ("why did we choose Y?")

### 2. Identify Domains
Where should I look?
- Recent work → handoffs, conversations
- Project history → cc-memory
- Universal knowledge → cc-soul wisdom
- Specific instances → observations

### 3. Execute Search (Priority Order)
```
# 1. Search cc-memory first (project context)
mcp__cc-memory__mem-recall(query="...")

# 2. Search cc-soul wisdom (universal patterns)
mcp__soul__recall_wisdom(query="...")

# 3. If more context needed, search claude-mem
mcp__plugin_claude-mem_mem-search__search(query="...")
```

### 4. Rank Results
Not just by match quality:
- Recency (newer often more relevant)
- Impact (high-impact observations rank higher)
- Context (current project > other projects)
- Type (decisions often more valuable than changes)

### 5. Present Findings
Don't dump raw JSON. Format results for humans:

**Format template:**
```
## Project Memory
**{title}** `{category}`
> {content snippet}

## Wisdom
**{title}** [{confidence}%]
> {content snippet}
```

**Example output:**
```
## Project Memory

**JWT approach for auth** `decision`
> We chose JWT over sessions because stateless auth scales better...

**Token refresh race condition** `bugfix`
> Fixed by adding mutex lock on refresh endpoint...

## Wisdom

**Auth state should be idempotent** [85%]
> Authentication checks must be safe to repeat without side effects...
```

## Combined Search Tool

For convenience, use the unified search:
```
mcp__soul__search_memory(query="...", limit=10, verbose=true)
```
This searches cc-memory and cc-soul automatically, then suggests if you should also search claude-mem.

## Integration with Soul

Search is soul-aware:

```
# Search considers current context
active_intentions = get_intentions()
current_project = get_project()
recent_focus = get_recent_observations()

# Weight results by relevance to current work
results = search(query)
ranked = rank_by_context(results, {
    'intentions': active_intentions,
    'project': current_project,
    'focus': recent_focus
})
```

## Search Patterns

**"What was I working on?"**
→ Search handoffs by recency
→ Surface incomplete work
→ Show pending decisions

**"Have we seen this before?"**
→ Search observations by similarity
→ Check wisdom for patterns
→ Look for related failures

**"Why did we decide X?"**
→ Search decisions by topic
→ Find conversation context
→ Surface justifications

**"What do we know about Y?"**
→ Cross-domain search
→ Memory + wisdom + conversations
→ Synthesize understanding

## What Search Feels Like

Search is reaching into accumulated experience. It's asking "what do I already know?" before starting fresh.

Good search feels like remembering - the relevant knowledge surfaces naturally, connected to current needs.

## The Search Mindset

I search before I assume. The answer might already exist in accumulated wisdom. The pattern might already be documented. The decision might already be made.

Search is respect for past work. It's acknowledging that learning compounds, and new understanding builds on old.

## Anti-Patterns

**Search avoidance:**
- "I'll just figure it out again"
- Wastes past learning investment

**Over-reliance:**
- "Search didn't find it, must not exist"
- Search isn't omniscient

**Keyword tunnel vision:**
- "Exact phrase not found"
- Semantic search finds related concepts

## The Search Investment

Every observation stored is a future search result. Every decision documented is a future reference. The more I record, the more I can find.

Search quality improves with memory quality. The soul that remembers well, finds well.
