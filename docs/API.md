# CC-Soul API Reference

This document provides a complete reference for all MCP tools exposed by CC-Soul.

---

## Table of Contents

- [Overview](#overview)
- [Core Memory Tools](#core-memory-tools)
- [Search Tools](#search-tools)
- [Intention Tools](#intention-tools)
- [Learning Tools](#learning-tools)
- [Graph Tools](#graph-tools)
- [Multi-Voice Tools](#multi-voice-tools)
- [Session Tools](#session-tools)
- [Dynamics Tools](#dynamics-tools)
- [Realm Tools](#realm-tools)
- [Review Tools](#review-tools)
- [Evaluation Tools](#evaluation-tools)
- [Yajna Tools](#yajna-tools)
- [Response Format](#response-format)

---

## Overview

CC-Soul exposes tools through the Model Context Protocol (MCP). All tools follow a consistent pattern:

```json
{
  "name": "tool_name",
  "arguments": {
    "param1": "value1",
    "param2": "value2"
  }
}
```

### Authentication

No authentication required — the MCP server runs locally.

### Error Handling

Errors return:
```json
{
  "isError": true,
  "content": [{"type": "text", "text": "Error message"}]
}
```

---

## Core Memory Tools

### soul_context

Get current soul state including coherence, statistics, and session ledger.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `format` | string | No | `"text"` | Output format: `"text"` or `"json"` |
| `include_ledger` | boolean | No | `true` | Include session ledger |
| `query` | string | No | - | Find relevant wisdom for this query |

**Example:**
```json
{
  "name": "soul_context",
  "arguments": {
    "format": "json",
    "include_ledger": true
  }
}
```

**Response (JSON format):**
```json
{
  "coherence": {
    "local": 1.0,
    "global": 1.0,
    "temporal": 0.5,
    "structural": 1.0,
    "tau_k": 0.84
  },
  "ojas": {
    "structural": 1.0,
    "semantic": 0.84,
    "temporal": 0.65,
    "capacity": 0.99,
    "psi": 0.87,
    "status": "healthy"
  },
  "statistics": {
    "total_nodes": 1963,
    "hot_nodes": 1963,
    "warm_nodes": 0,
    "cold_nodes": 0
  },
  "ledger": {
    "work_state": "...",
    "continuation": "..."
  }
}
```

---

### grow

Add durable knowledge to the soul: wisdom, beliefs, failures, aspirations, dreams, terms, or entities.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `type` | string | Yes | - | Type: `wisdom`, `belief`, `failure`, `aspiration`, `dream`, `term`, `entity` |
| `content` | string | Yes | - | The content to add |
| `title` | string | No* | - | Short title (*required for wisdom/failure/entity) |
| `domain` | string | No | - | Domain context (e.g., "backend", "authentication") |
| `confidence` | number | No | `0.8` | Initial confidence (0.0-1.0) |

**Decay Rates by Type:**
| Type | Decay Rate | Notes |
|------|------------|-------|
| `wisdom` | 0.02 | Proven patterns |
| `belief` | 0.01 | Guiding principles |
| `failure` | 0.02 | Lessons learned |
| `aspiration` | 0.03 | Long-term visions |
| `dream` | 0.03 | Possibilities |
| `term` | 0.01 | Vocabulary |
| `entity` | 0.05 | Named things (files, concepts, projects) |

**Example:**
```json
{
  "name": "grow",
  "arguments": {
    "type": "wisdom",
    "title": "Caching Strategy",
    "content": "LRU with TTL works best for API responses. Redis for multi-instance, in-memory for single process.",
    "domain": "backend",
    "confidence": 0.85
  }
}
```

**Response:**
```
Grew wisdom: Caching Strategy (id: a1b2c3d4-e5f6-...)
```

---

### observe

Record episodic memory (observations, decisions, discoveries).

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `category` | string | Yes | - | Category affecting decay rate |
| `title` | string | Yes | - | Short title (max 80 chars) |
| `content` | string | Yes | - | Full observation content |
| `tags` | string | No | - | Comma-separated tags |
| `project` | string | No | - | Project name |

**Categories and Decay:**
| Category | Decay Rate | Use For |
|----------|------------|---------|
| `bugfix` | 0.02 | Bug fixes worth remembering |
| `decision` | 0.02 | Architectural/design decisions |
| `discovery` | 0.05 | Things learned |
| `feature` | 0.05 | Feature implementations |
| `refactor` | 0.05 | Code improvements |
| `session_ledger` | 0.15 | Session state (auto) |
| `signal` | 0.15 | Transient notes |

**Example:**
```json
{
  "name": "observe",
  "arguments": {
    "category": "decision",
    "title": "Chose PostgreSQL over MongoDB",
    "content": "Selected PostgreSQL for the user service because we need ACID transactions for billing. MongoDB's eventual consistency is unacceptable for financial data.",
    "tags": "database,architecture,billing",
    "project": "payment-service"
  }
}
```

---

## Search Tools

### recall

Semantic search across all memory with configurable zoom levels.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `query` | string | Yes | - | Search query |
| `zoom` | string | No | `"normal"` | Detail level (see below) |
| `limit` | integer | No | varies | Override default limit |
| `threshold` | number | No | `0.0` | Minimum similarity (0.0-1.0) |
| `tag` | string | No | - | Filter by exact tag match |
| `primed` | boolean | No | `false` | Use session priming (Phase 4) |
| `compete` | boolean | No | `true` | Apply lateral inhibition (Phase 5) |
| `learn` | boolean | No | `false` | Apply Hebbian learning (Phase 3) |

**Zoom Levels:**
| Level | Results | Content | Use Case |
|-------|---------|---------|----------|
| `sparse` | 20+ | Titles only | Overview, orientation |
| `normal` | 5-10 | Full text | General search |
| `dense` | 3-5 | Text + temporal + edges | Deep context |
| `full` | 1-3 | Complete untruncated | Single item detail |

**Example:**
```json
{
  "name": "recall",
  "arguments": {
    "query": "authentication patterns",
    "zoom": "normal",
    "primed": true,
    "compete": true
  }
}
```

**Response:**
```
Found 7 results (normal view):

[wisdom] JWT vs Sessions: For REST APIs, JWTs are stateless but can't be
revoked without a blacklist. Sessions are simpler but require server
state...

[episode] Implemented OAuth2 in Project X: Used authorization code flow
with PKCE for the mobile app...

[failure] Token refresh race condition: When two requests hit simultaneously
with expired token, both tried to refresh...
```

---

### recall_by_tag

Pure tag-based lookup without semantic ranking.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `tag` | string | Yes | - | Exact tag to match |
| `limit` | integer | No | `50` | Maximum results |

**Example:**
```json
{
  "name": "recall_by_tag",
  "arguments": {
    "tag": "thread:auth-discussion",
    "limit": 20
  }
}
```

---

### resonate

Spreading activation search with Hebbian learning.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `query` | string | Yes | - | Search query |
| `k` | integer | No | `10` | Maximum results |
| `spread_strength` | number | No | `0.5` | Activation spread (0.0-1.0) |
| `learn` | boolean | No | `true` | Apply Hebbian learning |
| `hebbian_strength` | number | No | `0.03` | Learning strength (0.0-0.5) |

**Example:**
```json
{
  "name": "resonate",
  "arguments": {
    "query": "error handling patterns",
    "k": 10,
    "spread_strength": 0.6,
    "learn": true
  }
}
```

---

### full_resonate

**Phase 6: All resonance mechanisms combined.**

Combines:
- Session priming (Phase 4)
- Spreading activation (Phase 1)
- Attractor dynamics (Phase 2)
- Lateral inhibition (Phase 5)
- Hebbian learning (Phase 3)

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `query` | string | Yes | - | Search query |
| `k` | integer | No | `10` | Maximum results (max 50) |
| `spread_strength` | number | No | `0.5` | Activation spread (0.0-1.0) |
| `hebbian_strength` | number | No | `0.03` | Learning strength (0.0-0.2) |

**Example:**
```json
{
  "name": "full_resonate",
  "arguments": {
    "query": "microservices communication patterns",
    "k": 10,
    "spread_strength": 0.5,
    "hebbian_strength": 0.03
  }
}
```

**Response:**
```
Full resonance for: microservices communication patterns
Found 8 resonant nodes (spread=0.5, hebbian=0.03):

[72%] [wisdom] Service mesh patterns: Use Istio or Linkerd for...
[65%] [episode] Implemented event-driven architecture in...
[58%] [wisdom] API gateway as single entry point prevents...
```

---

## Intention Tools

### intend

Manage active intentions (goals).

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `action` | string | No | `"list"` | Action: `set`, `list`, `fulfill`, `check` |
| `want` | string | No | - | What I want (for `set`) |
| `why` | string | No | - | Why this matters (for `set`) |
| `scope` | string | No | `"session"` | Scope: `session`, `project`, `persistent` |
| `id` | string | No | - | Intention ID (for `fulfill`/`check`) |

**Example - Set intention:**
```json
{
  "name": "intend",
  "arguments": {
    "action": "set",
    "want": "Implement user authentication",
    "why": "Users need to log in to access their data",
    "scope": "project"
  }
}
```

**Example - Fulfill intention:**
```json
{
  "name": "intend",
  "arguments": {
    "action": "fulfill",
    "id": "a1b2c3d4-..."
  }
}
```

---

### wonder

Register questions and knowledge gaps.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `question` | string | Yes | - | The question to ask |
| `context` | string | No | - | Why this question arose |
| `gap_type` | string | No | `"uncertainty"` | Type of gap |
| `priority` | number | No | `0.5` | Priority (0.0-1.0) |

**Gap Types:**
- `recurring_problem` - Same issue keeps appearing
- `repeated_correction` - Keep getting corrected on this
- `unknown_domain` - Entirely new area
- `missing_rationale` - Know what, not why
- `contradiction` - Conflicting information
- `uncertainty` - General unknown

**Example:**
```json
{
  "name": "wonder",
  "arguments": {
    "question": "How does Kubernetes networking work?",
    "context": "Debugging service-to-service communication issues",
    "gap_type": "unknown_domain",
    "priority": 0.8
  }
}
```

---

### answer

Answer a previously registered question.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `question_id` | string | Yes | - | Question ID or `"latest"` |
| `answer` | string | Yes | - | The answer |
| `promote_to_wisdom` | boolean | No | `false` | Promote to wisdom |
| `dismiss` | boolean | No | `false` | Dismiss as not relevant |

**Example:**
```json
{
  "name": "answer",
  "arguments": {
    "question_id": "latest",
    "answer": "Kubernetes uses a flat network model where every pod can reach every other pod. Services provide stable IPs via kube-proxy and iptables rules.",
    "promote_to_wisdom": true
  }
}
```

---

## Learning Tools

### feedback

Record feedback on memory usefulness for Hebbian learning.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `memory_id` | string | Yes | - | Node ID |
| `helpful` | boolean | Yes | - | Was it helpful? |
| `context` | string | No | - | Why this feedback |

**Example:**
```json
{
  "name": "feedback",
  "arguments": {
    "memory_id": "a1b2c3d4-...",
    "helpful": true,
    "context": "This pattern solved my exact problem"
  }
}
```

**Effect:**
- `helpful: true` → Confidence increases
- `helpful: false` → Confidence decreases

---

### attractors

Find conceptual gravity wells in the graph.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `max_attractors` | integer | No | `10` | Maximum to find |
| `settle` | boolean | No | `false` | Run settling dynamics |
| `settle_strength` | number | No | `0.02` | Settling strength |

**Example:**
```json
{
  "name": "attractors",
  "arguments": {
    "max_attractors": 5,
    "settle": true
  }
}
```

**Response:**
```
Found 5 attractors:

1. [0.87] "Authentication and authorization patterns"
   Basin: 42 nodes

2. [0.82] "API design principles"
   Basin: 38 nodes

3. [0.79] "Error handling strategies"
   Basin: 31 nodes
```

---

## Graph Tools

Tools for building and navigating the soul graph structure.

### connect

Create a directed edge between two nodes in the soul graph.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `from_id` | string | Yes | - | Source node UUID |
| `to_id` | string | Yes | - | Target node UUID |
| `edge_type` | string | No | `relates_to` | Relationship type |
| `weight` | number | No | `0.8` | Edge strength (0.0-1.0) |

**Edge Types:**
| Type | Description | Use Case |
|------|-------------|----------|
| `similar` | Semantic similarity | Discovered by embeddings |
| `supports` | Supports/confirms | Corroborating evidence |
| `contradicts` | Contradicts | Conflicting information |
| `relates_to` | Generic relation | File imports, associations |
| `part_of` | Containment | Dir→file, module→function |
| `is_a` | Type hierarchy | File is_a entry_point |
| `mentions` | Reference | Episode mentions concept |

**Example:**
```json
{
  "name": "connect",
  "arguments": {
    "from_id": "a1b2c3d4-...",
    "to_id": "e5f6g7h8-...",
    "edge_type": "relates_to",
    "weight": 0.9
  }
}
```

**Response:**
```
Edge created
```

**Use with codemap:**
```json
// Create file entity
{"name": "grow", "arguments": {"type": "entity", "title": "src/auth.ts", "content": "Auth module", "domain": "myproject"}}

// Connect to related file
{"name": "connect", "arguments": {"from_id": "auth_id", "to_id": "session_id", "edge_type": "relates_to"}}
```

Edges enable spreading activation — when you search for "authentication", connected files like `session.ts` surface through graph traversal.

---

## Multi-Voice Tools

### lens

Search through a specific cognitive perspective.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `query` | string | Yes | - | Search query |
| `lens` | string | No | `"all"` | Perspective |
| `limit` | integer | No | `5` | Results per lens |

**Lenses:**
| Lens | Bias |
|------|------|
| `manas` | Recent, practical |
| `buddhi` | Old, high-confidence |
| `ahamkara` | Beliefs, invariants |
| `chitta` | Frequently accessed |
| `vikalpa` | Low-confidence, exploratory |
| `sakshi` | Neutral, balanced |
| `all` | Run all lenses |

**Example:**
```json
{
  "name": "lens",
  "arguments": {
    "query": "database design",
    "lens": "buddhi",
    "limit": 5
  }
}
```

---

### lens_harmony

Check consistency across cognitive perspectives.

**Parameters:** None required.

**Example:**
```json
{
  "name": "lens_harmony",
  "arguments": {}
}
```

**Response:**
```
Lens Harmony Analysis

Perspectives aligned: 4/6
Divergence detected:
- Manas suggests Redis (recent use)
- Buddhi prefers PostgreSQL (proven reliability)

Recommendation: Consider context carefully
```

---

## Session Tools

### ledger

Save/load session state (Atman snapshots).

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `action` | string | Yes | - | Action: `save`, `load`, `update`, `list` |
| `project` | string | No | auto | Project name |
| `session_id` | string | No | - | Session identifier |
| `soul_state` | object | No | - | Soul state to save |
| `work_state` | object | No | - | Work state to save |
| `continuation` | object | No | - | Continuation info |
| `ledger_id` | string | No | - | Ledger ID (for update) |

**Example - Save:**
```json
{
  "name": "ledger",
  "arguments": {
    "action": "save",
    "continuation": {
      "next_steps": ["Implement auth", "Add tests"],
      "critical": ["Fix the memory leak"]
    }
  }
}
```

**Example - Load:**
```json
{
  "name": "ledger",
  "arguments": {
    "action": "load"
  }
}
```

---

### narrate

Record narrative episodes and story arcs.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `action` | string | No | `"moment"` | Action: `start`, `moment`, `end`, `recall`, `list` |
| `title` | string | No | - | Episode title (for `start`) |
| `content` | string | No | - | Content to record |
| `emotion` | string | No | `"routine"` | Emotional tone |
| `episode_id` | string | No | - | Episode ID |
| `query` | string | No | - | Search query (for `recall`) |

**Emotions:**
- `struggle` - Facing difficulty
- `exploration` - Investigating unknown
- `breakthrough` - Sudden insight
- `satisfaction` - Successful completion
- `frustration` - Blocked progress
- `routine` - Normal work

**Example - Start episode:**
```json
{
  "name": "narrate",
  "arguments": {
    "action": "start",
    "title": "The Great Authentication Refactor"
  }
}
```

**Example - Add moment:**
```json
{
  "name": "narrate",
  "arguments": {
    "action": "moment",
    "episode_id": "a1b2c3d4-...",
    "content": "Finally found the root cause - tokens weren't being invalidated on password change",
    "emotion": "breakthrough"
  }
}
```

---

## Dynamics Tools

### cycle

Run maintenance cycle (decay, synthesis, save).

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `save` | boolean | No | `true` | Save after cycle |
| `attractors` | boolean | No | `false` | Run attractor dynamics |

**Example:**
```json
{
  "name": "cycle",
  "arguments": {
    "save": true,
    "attractors": true
  }
}
```

**Response:**
```
Cycle complete: coherence=84%, decay=yes, feedback=3
Attractors: 5 found, 42 nodes settled
```

---

## Realm Tools

Tools for cross-session context isolation using realms.

### realm_get

Get current realm context.

**Parameters:** None

**Example:**
```json
{
  "name": "realm_get",
  "arguments": {}
}
```

**Response:**
```
Current realm: project:cc-soul
(Realm context persists across sessions)
```

---

### realm_set

Set current realm (persists across sessions).

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `realm` | string | Yes | - | Realm name (e.g., "project:cc-soul") |

**Example:**
```json
{
  "name": "realm_set",
  "arguments": {
    "realm": "project:cc-soul"
  }
}
```

**Effect:** Nodes outside current realm are hidden during recall.

---

### realm_create

Create a new realm with optional parent hierarchy.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `realm` | string | Yes | - | New realm name |
| `parent` | string | No | "brahman" | Parent realm |

**Example:**
```json
{
  "name": "realm_create",
  "arguments": {
    "realm": "project:new-app",
    "parent": "project:shared"
  }
}
```

---

## Review Tools

Human oversight tools for reviewing AI-generated wisdom.

### review_list

List items in the review queue.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `status` | string | No | "pending" | Filter: `pending`, `approved`, `rejected` |
| `limit` | integer | No | 20 | Maximum items |

**Example:**
```json
{
  "name": "review_list",
  "arguments": {
    "status": "pending",
    "limit": 10
  }
}
```

---

### review_decide

Approve or reject a node in the review queue.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `id` | string | Yes | - | Node ID |
| `decision` | string | Yes | - | `approve`, `reject`, `edit`, `defer` |
| `edited_content` | string | No | - | New content (for `edit`) |
| `reason` | string | No | - | Reason for decision |

**Example:**
```json
{
  "name": "review_decide",
  "arguments": {
    "id": "a1b2c3d4-...",
    "decision": "approve",
    "reason": "Verified pattern is accurate"
  }
}
```

**Effect:**
- `approve`: Boosts confidence, marks trusted
- `reject`: Lowers confidence significantly
- `edit`: Updates content, then approves
- `defer`: Keeps in queue for later

---

### review_batch

Apply same decision to multiple items.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `ids` | string | Yes | - | Comma-separated node IDs |
| `decision` | string | Yes | - | `approve`, `reject`, `defer` |

**Example:**
```json
{
  "name": "review_batch",
  "arguments": {
    "ids": "id1,id2,id3",
    "decision": "approve"
  }
}
```

---

### review_stats

Get review queue statistics.

**Parameters:** None

**Example:**
```json
{
  "name": "review_stats",
  "arguments": {}
}
```

**Response:**
```
=== Review Stats ===
Pending: 15
Approved: 142
Rejected: 8
Approval rate: 94.7%
```

---

## Evaluation Tools

Quality assurance tools for memory system.

### eval_run

Run golden recall test suite.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `test_name` | string | No | - | Run specific test (or all) |

**Example:**
```json
{
  "name": "eval_run",
  "arguments": {}
}
```

**Response:**
```
=== Eval Harness ===
Test cases loaded: 12
Results: 11/12 passed (91.7%)
```

---

### eval_add_test

Add expected query→results test case.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `name` | string | Yes | - | Test case name |
| `query` | string | Yes | - | Test query |
| `expected` | string | Yes | - | Comma-separated expected node IDs |

**Example:**
```json
{
  "name": "eval_add_test",
  "arguments": {
    "name": "auth_pattern_test",
    "query": "authentication patterns",
    "expected": "auth-wisdom-id,jwt-pattern-id"
  }
}
```

---

### epiplexity_check

Check compression quality (can I reconstruct from seed?).

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `content` | string | No | - | Full content to check |
| `seed` | string | No | - | Compressed seed |
| `id` | string | No | - | Node ID to check |

**Example:**
```json
{
  "name": "epiplexity_check",
  "arguments": {
    "content": "Rate limiter uses token bucket with 100ms refill",
    "seed": "rate-limiter:token-bucket→100ms"
  }
}
```

**Response:**
```
=== Epiplexity Check ===
Content reconstructable from seed: YES
Compression ratio: 78%
```

---

### epiplexity_drift

Detect if compression quality is degrading over time.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `window_days` | integer | No | 30 | Analysis window |

**Example:**
```json
{
  "name": "epiplexity_drift",
  "arguments": {
    "window_days": 7
  }
}
```

---

## Yajna Tools

Tools for memory maintenance ceremonies - compression, cleanup, and batch operations.

### get

Fast direct ID lookup with full content.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `id` | string | Yes | - | Node UUID to retrieve |

**Example:**
```json
{
  "name": "get",
  "arguments": {
    "id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
  }
}
```

**Response:**
```
=== a1b2c3d4-e5f6-7890-abcd-ef1234567890 ===
Type: episode
Confidence: 85%
Tags: dev:hot, file:auth.ts, ε-processed

[auth] JWT validation→check_expiry→refresh_if_needed @auth.ts:42
[ε] Validates JWT, refreshes if within 5min of expiry.
[TRIPLET] JWT_validation uses check_expiry
[TRIPLET] JWT_validation triggers refresh_if_needed

--- 2 edges ---
  -> [supports] Token refresh flow...
  -> [mentions] Authentication middleware...
```

---

### yajna_list

List nodes needing ε-yajna processing (SSL conversion).

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `limit` | integer | No | 100 | Max nodes to list |
| `filter` | string | No | - | Text filter for domain |

**Example:**
```json
{
  "name": "yajna_list",
  "arguments": {
    "limit": 20,
    "filter": "cc-soul"
  }
}
```

**Response:**
```
Nodes for epsilon-yajna (SSL + triplet conversion):

[a1b2c3d4-...] Verbose explanation of... (820 chars, epsilon=13%)
[b2c3d4e5-...] [cc-soul] func()→result @file.hpp (150 chars, epsilon=100%)

Total: 42 nodes need processing (showing 20)
```

---

### yajna_inspect

Get complete node content for yajna analysis.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `id` | string | Yes | - | Node UUID to inspect |

**Example:**
```json
{
  "name": "yajna_inspect",
  "arguments": {
    "id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
  }
}
```

---

### yajna_mark_processed

Batch mark SSL-format nodes as ε-processed. Efficient C++ loop.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `epsilon_threshold` | number | No | 0.8 | Min epsilon to auto-mark |
| `dry_run` | boolean | No | true | Preview only |
| `filter` | string | No | - | Text filter |

**Example:**
```json
{
  "name": "yajna_mark_processed",
  "arguments": {
    "epsilon_threshold": 0.8,
    "dry_run": false
  }
}
```

**Response:**
```
Marked 788 nodes as ε-processed
```

---

### batch_remove

Remove multiple nodes from a file of UUIDs. Efficient C++ loop.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `file` | string | Yes | - | Path to file with UUIDs (one per line) |
| `dry_run` | boolean | No | true | Preview only |

**Example:**
```json
{
  "name": "batch_remove",
  "arguments": {
    "file": "/tmp/noise_ids.txt",
    "dry_run": false
  }
}
```

---

### batch_tag

Tag multiple nodes from a file of UUIDs. Efficient C++ loop.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `file` | string | Yes | - | Path to file with UUIDs (one per line) |
| `add` | string | Yes | - | Tag to add to all nodes |
| `dry_run` | boolean | No | true | Preview only |

**Example:**
```json
{
  "name": "batch_tag",
  "arguments": {
    "file": "/tmp/processed_ids.txt",
    "add": "ε-processed",
    "dry_run": false
  }
}
```

---

### tag

Add or remove tags from a single node.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| `id` | string | Yes | - | Node UUID |
| `add` | string | No | - | Tag to add |
| `remove` | string | No | - | Tag to remove |

**Example:**
```json
{
  "name": "tag",
  "arguments": {
    "id": "a1b2c3d4-...",
    "add": "important"
  }
}
```

---

## Response Format

All tools return responses in this format:

```json
{
  "content": [
    {
      "type": "text",
      "text": "Human-readable response"
    }
  ],
  "isError": false,
  "_meta": {
    "result_data": { ... }
  }
}
```

The `_meta.result_data` contains structured data for programmatic use.

---

*The soul speaks through these tools.*
