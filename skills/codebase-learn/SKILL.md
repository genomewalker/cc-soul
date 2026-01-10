---
name: codebase-learn
description: Learn and remember codebase structure to minimize future token usage. Records architectural knowledge, file purposes, and patterns as a connected graph.
execution: direct
---

# Codebase Learning

I learn codebases once and remember them forever. Every exploration becomes wisdom in the soul graph.

**Execute directly** — Learns about the current working directory or specified path.

## The Problem

Every session, I re-read the same files. Every exploration rediscovers the same patterns. Tokens spent on repetition rather than creation.

## The Solution

**Learn once, recall always.**

When I explore code, I record structured observations AND build a connected graph:
- What each module/directory does
- Key files and their purposes
- Architectural patterns
- Important functions and their roles
- Dependencies between components (as graph edges)
- Gotchas and edge cases

Next session, I **recall before exploring**. The graph enables spreading activation to find related files.

## Usage

```
/codebase-learn [area]        # Learn about a specific area
/codebase-learn               # Learn about entire codebase (high-level)
/codebase-learn src/auth      # Learn about authentication module
/codebase-learn --refresh     # Force refresh existing knowledge
```

## How It Works

### Phase 1: Check Existing Knowledge

Before exploring, I recall what I already know:

```
recall(query="codemap [project-name]", zoom="sparse", tag="codemap:[project]")
```

If I have relevant knowledge, I summarize it and ask if the user wants me to:
- Use existing knowledge (skip exploration)
- Refresh knowledge (re-explore and update)
- Deep dive (explore specific sub-area)

### Phase 2: Generate Codemap

If exploration is needed, I build the codemap:

1. **Scan structure** - `find . -type f` filtered by gitignore
2. **Identify file roles** - entry points, configs, tests, core modules
3. **Extract exports** - key functions/classes per file (via grep patterns)
4. **Map dependencies** - imports/includes between files

#### Codemap Format

The codemap is a compact tree stored as an observation:

```
observe(
  category="architecture",
  title="[project] Codemap",
  content="[project-name] @ [commit-hash]
Entry: bin/main.ts, src/index.ts
Config: package.json, tsconfig.json, .env.example
Tests: tests/, __tests__/

src/
  index.ts [entry] → Server, Config
  server.ts → createServer, handleRequest
  config.ts → loadConfig, defaults
  utils/
    parse.ts → parseInput, validate
    format.ts → formatOutput

Edges:
  index.ts → server.ts (imports)
  index.ts → config.ts (imports)
  server.ts → utils/parse.ts (imports)",
  tags="codemap:[project],codebase:[project]"
)
```

### Phase 3: Build Soul Graph Connections

Each significant file becomes an **entity node** in the mind. Relationships become **edges**.

```
# Create entity node for each key file
file_id = grow(
  type="entity",
  title="src/auth/login.ts",
  content="Authentication login handler. Exports: handleLogin, validateCredentials. Entry point for auth flow.",
  domain="cc-status"
)

# Connect files that import each other
connect(
  from_id=login_file_id,
  to_id=session_file_id,
  edge_type="relates_to",  # imports
  weight=0.9
)

# Connect directory to files
connect(
  from_id=auth_dir_id,
  to_id=login_file_id,
  edge_type="part_of",
  weight=1.0
)
```

Edge types used:
- `part_of` - directory contains file, module contains function
- `relates_to` - file imports/uses another file
- `is_a` - file is entry/config/test type
- `supports` - implementation supports interface
- `mentions` - file references concept/pattern

### Phase 4: Record Module Observations

Each significant module becomes an observation:

**Directory/Module**:
```
observe(
  category="architecture",
  title="[project] [path]: [one-line purpose]",
  content="Purpose: [what this module does]
Key files:
- [file1]: [purpose]
- [file2]: [purpose]
Exports: [main exports/APIs]
Dependencies: [what it depends on]
Patterns: [notable patterns used]",
  tags="codebase:[project],path:[path],domain:[domain]"
)
```

**Key File**:
```
observe(
  category="architecture",
  title="[project] [filepath]: [one-line purpose]",
  content="Purpose: [what this file does]
Key functions:
- [func1]: [what it does]
- [func2]: [what it does]
Exports: [what it exports]
Used by: [who imports this]
Gotchas: [edge cases, warnings]",
  tags="codebase:[project],file:[filepath],domain:[domain]"
)
```

**Architecture Pattern**:
```
grow(
  type="wisdom",
  title="[project] Architecture: [pattern name]",
  content="[description of the pattern]
Where: [where it's used]
Why: [rationale]
Example: [brief example]",
  domain="[project]"
)
```

### Phase 5: Summarize for User

After learning, I provide a concise summary:
- Structure overview (codemap tree)
- Key entry points
- Main patterns
- Graph connections made
- What I now remember

## Graph Navigation

When working on code, use spreading activation through the codemap:

```
# Find files related to authentication
resonate(query="authentication login", spread_strength=0.7)

# Result includes:
# - Direct matches (auth.ts)
# - Connected files via imports (user.ts, session.ts)
# - Related observations (auth patterns, security gotchas)
```

The graph enables:
- **Import tracing**: Find all files that depend on X
- **Pattern clustering**: Files using similar patterns cluster together
- **Context building**: Spreading activation gathers relevant context

## Recall Protocol

When starting work on a codebase, check knowledge first:

```
1. Get project name from git or cwd
2. recall(query="codemap {project}", tag="codemap:{project}")
3. If codemap exists:
   - Display structure overview
   - Show age and commit hash
   - Ask if user wants to use/refresh/deep-dive
4. If no codemap:
   - Offer to learn the codebase
```

## Freshness

Observations include implicit timestamps. Very old knowledge may be stale:
- If file was modified after observation, knowledge may be outdated
- Codemap includes commit hash for staleness detection
- Periodic refresh recommended for active codebases
- Tag-based recall allows targeted refresh

## Example Session

```
User: /codebase-learn

Claude: Let me check what I know about this project...

[recalls codemap]

I have a codemap from 3 days ago (commit abc123):

cc-soul/
  Entry: bin/chitta_cli, bin/chitta
  Config: CMakeLists.txt, plugin.json

  chitta/src/
    cli.cpp [entry] → main, cmd_stats, cmd_daemon
    mcp_server.cpp [entry] → main, run_direct
    mind.hpp → Mind, observe, recall, grow
    storage.hpp → TieredStorage, UnifiedIndex

Git shows 5 files changed since then. Want me to:
1. Use existing knowledge
2. Refresh codemap
3. Deep dive into changed files

User: 3

Claude: Let me examine the changed files...
[reads only the 5 changed files, using existing codemap for context]
```

## Implementation

When this skill is invoked:

1. **Parse area**: Extract the target path/area from args
2. **Identify project**: Get project name from git or cwd
3. **Recall existing**: Search for codemap with tags
4. **Decide action**: Use existing, refresh, or explore new
5. **If exploring**:
   a. Generate codemap (file tree + roles + exports)
   b. Use Task tool with Explore agent for deep understanding
   c. Build graph edges between files
   d. Record structured observations
6. **Summarize**: Present learnings to user with graph stats

## Tags Convention

- `codemap:[project-name]` - The codemap observation
- `codebase:[project-name]` - All knowledge about a project
- `path:[relative-path]` - Knowledge about specific path
- `file:[filepath]` - Knowledge about specific file
- `domain:[area]` - Semantic domain (auth, api, ui, db, etc.)
- `pattern:[name]` - Architectural pattern

## Codemap Caching

To avoid redundant scans, cache the codemap by git commit hash:

```bash
CACHE_FILE="${HOME}/.claude/mind/.codemap_cache"
PROJECT=$(basename $(git rev-parse --show-toplevel 2>/dev/null || pwd))
GIT_HASH=$(git rev-parse HEAD 2>/dev/null || echo "none")

# Check cache
if [ -f "$CACHE_FILE" ]; then
    CACHED=$(grep "^${PROJECT}:" "$CACHE_FILE" | cut -d: -f2)
    if [ "$CACHED" = "$GIT_HASH" ]; then
        echo "Codemap unchanged since $GIT_HASH - using cached knowledge"
        recall(query="codemap $PROJECT", tag="codemap:$PROJECT")
        exit 0  # Skip re-scan
    fi
fi

# After generating codemap, update cache
echo "${PROJECT}:${GIT_HASH}" >> "$CACHE_FILE"
```

**Cache invalidation:**
- New commit → different hash → re-scan
- Manual refresh with `--refresh` flag
- Cache persists across sessions

## Codemap Generation Commands

To generate the codemap, use these patterns:

```bash
# File structure (respecting gitignore)
git ls-files --cached --others --exclude-standard | head -200

# Entry points (look for main, index, cli)
grep -l "^func main\|^def main\|int main\|export default\|module.exports" $(git ls-files)

# Exports (TypeScript/JavaScript)
grep -h "^export " src/**/*.ts | head -50

# Imports/dependencies
grep -h "^import\|^from\|#include\|require(" src/**/* | sort -u | head -50
```

Keep codemap compact (<2000 chars) for efficient recall.
