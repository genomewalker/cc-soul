---
name: codebase-learn
description: Learn and remember codebase structure to minimize future token usage. Records architectural knowledge, file purposes, and patterns.
execution: direct
---

# Codebase Learning

I learn codebases once and remember them forever. Every exploration becomes wisdom.

**Execute directly** — Learns about the current working directory or specified path.

## The Problem

Every session, I re-read the same files. Every exploration rediscovers the same patterns. Tokens spent on repetition rather than creation.

## The Solution

**Learn once, recall always.**

When I explore code, I record structured observations:
- What each module/directory does
- Key files and their purposes
- Architectural patterns
- Important functions and their roles
- Dependencies between components
- Gotchas and edge cases

Next session, I **recall before exploring**. Existing knowledge reduces the need to re-read files.

## Usage

```
/codebase-learn [area]        # Learn about a specific area
/codebase-learn               # Learn about entire codebase (high-level)
/codebase-learn src/auth      # Learn about authentication module
```

## How It Works

### Phase 1: Check Existing Knowledge

Before exploring, I recall what I already know:

```
recall(query="codebase architecture [project-name]", zoom="sparse", primed=true)
```

If I have relevant knowledge, I summarize it and ask if the user wants me to:
- Use existing knowledge (skip exploration)
- Refresh knowledge (re-explore and update)
- Deep dive (explore specific sub-area)

### Phase 2: Systematic Exploration

If exploration is needed, I:

1. **Map the territory** - List directories, identify structure
2. **Identify key files** - README, configs, entry points
3. **Understand architecture** - How components connect
4. **Note patterns** - Conventions, idioms, style

### Phase 3: Record as Structured Observations

Each discovery becomes an observation with:
- **Category**: `architecture` (slow decay) or `discovery` (medium decay)
- **Tags**: `codebase:[project]`, `path:[relative-path]`, `domain:[area]`
- **Structured content**: Purpose, key exports, dependencies, patterns

#### Observation Templates

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

### Phase 4: Summarize for User

After learning, I provide a concise summary:
- Structure overview
- Key entry points
- Main patterns
- What I now remember

## Recall Protocol

When starting work on a codebase, check knowledge first:

```
1. Get project name from cwd
2. recall(query="codebase architecture {project}", zoom="sparse")
3. If results:
   - Display summary of known structure
   - Ask if user wants to use/refresh/deep-dive
4. If no results:
   - Offer to learn the codebase
```

## Freshness

Observations include implicit timestamps. Very old knowledge may be stale:
- If file was modified after observation, knowledge may be outdated
- Periodic refresh recommended for active codebases
- Tag-based recall allows targeted refresh

## Example Session

```
User: /codebase-learn src/auth

Claude: Let me check what I know about authentication in this project...

[recalls existing knowledge]

I remember this from a previous session:
- src/auth/ handles JWT-based authentication
- Key files: login.ts, middleware.ts, tokens.ts
- Uses refresh token rotation
- Gotcha: tokens stored in httpOnly cookies, not localStorage

This knowledge is 3 days old. Want me to:
1. Use this knowledge (skip exploration)
2. Refresh (re-explore and update)
3. Deep dive into a specific file

User: 1

Claude: Great, using existing knowledge. The auth flow works like this...
[continues without reading files]
```

## Implementation

When this skill is invoked:

1. **Parse area**: Extract the target path/area from args
2. **Identify project**: Get project name from git or cwd
3. **Recall existing**: Search for codebase knowledge with tags
4. **Decide action**: Use existing, refresh, or explore new
5. **If exploring**: Use Task tool with Explore agent
6. **Record findings**: Create structured observations
7. **Summarize**: Present learnings to user

## Tags Convention

- `codebase:[project-name]` - All knowledge about a project
- `path:[relative-path]` - Knowledge about specific path
- `file:[filepath]` - Knowledge about specific file
- `domain:[area]` - Semantic domain (auth, api, ui, db, etc.)
- `pattern:[name]` - Architectural pattern
