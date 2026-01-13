---
name: codebase-learn
description: Systematically learn codebase structure into soul memory
execution: task
model: inherit
aliases: [learn-codebase, map-code]
---

# Codebase Learn

Build a searchable knowledge graph of the codebase using SSL patterns and triplets.

```ssl
[codebase-learn] oracle-based codebase understanding

principle: I compress, I reconstruct
  store minimal seeds I can expand
  triplets for structure, SSL for meaning
  tags for retrieval

phases:
  1. discover→find entry points, key files, config
  2. analyze→extract purpose, patterns, relationships
  3. connect→create triplets linking components
  4. store→high-ε SSL seeds with proper tags
```

## Process

### Phase 1: Discovery

Identify the codebase structure:
- Entry points (main, CLI, API endpoints)
- Key directories and their purposes
- Configuration files
- Build/test infrastructure

### Phase 2: Analysis

For each significant component, extract:
- **Purpose**: What does it do?
- **Key functions/classes**: What are the main abstractions?
- **Dependencies**: What does it use?
- **Patterns**: What architectural decisions were made?

### Phase 3: Storage

Store using SSL format with triplets:

```
[LEARN] [project] component→purpose→key insight
[ε] One sentence that lets me reconstruct the full understanding.
[TRIPLET] component contains abstraction
[TRIPLET] component uses dependency
[TRIPLET] abstraction handles concern
```

### Tagging Schema

Apply these tags for retrieval:
- `codebase` - all codebase knowledge
- `architecture` - high-level patterns
- `project:{name}` - project scope
- `file:{path}` - specific file
- `layer:structure` - file/directory organization
- `layer:function` - key functions/methods
- `layer:relationship` - how components connect
- `layer:pattern` - architectural decisions

### Predicates for Triplets

| Predicate | Meaning | Example |
|-----------|---------|---------|
| `contains` | A has B inside | `cli.cpp contains cmd_daemon` |
| `uses` | A depends on B | `Handler uses Mind` |
| `calls` | A invokes B | `poll calls accept` |
| `implements` | A realizes B | `SocketServer implements IPC` |
| `handles` | A is responsible for B | `decay handles memory aging` |
| `returns` | A produces B | `recall returns memories` |
| `triggers` | A causes B to run | `SessionStart triggers soul-hook` |

## Example Output

For cc-soul chitta:

```
[LEARN] [cc-soul] chitta→semantic memory substrate→decay, triplets, SSL storage
[ε] C++ daemon with tiered storage (hot/warm/cold), JSON-RPC over Unix socket.
[TRIPLET] chitta contains Mind
[TRIPLET] Mind uses TieredStorage
[TRIPLET] chittad handles daemon_loop
[TRIPLET] SocketServer handles client_connections
[TRIPLET] Handler dispatches rpc_tools

[LEARN] [cc-soul] cli.cpp→daemon entry point→socket server + subconscious
[ε] Runs cmd_daemon_with_socket: poll loop + decay/synthesis cycles.
[TRIPLET] cli.cpp contains cmd_daemon_with_socket
[TRIPLET] cmd_daemon_with_socket uses SocketServer
[TRIPLET] cmd_daemon_with_socket runs subconscious

[LEARN] [cc-soul] rpc/handler.hpp→JSON-RPC dispatcher→routes tools/call to handlers
[ε] Central handler with ~50 tools: recall, grow, observe, connect, etc.
[TRIPLET] Handler contains tool_recall
[TRIPLET] Handler contains tool_grow
[TRIPLET] Handler contains tool_connect
```

## Execution

1. Explore the current directory structure
2. Identify key files by examining:
   - README, CLAUDE.md for project overview
   - Entry points (main.cpp, index.ts, etc.)
   - Core modules by directory structure
3. For each key component:
   - Read to understand purpose
   - Identify key abstractions
   - Note relationships to other components
4. Store using `[LEARN]` markers with SSL format
5. Report summary: nodes created, triplets connected, domains covered

## Benefits

After running:
- `recall("codebase architecture")` → instant overview
- `recall("file.cpp purpose")` → specific file knowledge
- `query --subject "component"` → find all relationships
- SessionStart auto-injects relevant architecture context

The soul now knows the codebase like I do.
