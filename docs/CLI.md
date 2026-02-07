# CC-Soul CLI Reference

CC-Soul provides two command-line binaries: `chittad` (daemon/server) and `chitta` (client/tool invoker).

---

## Table of Contents

- [Binaries](#binaries)
- [Installation](#installation)
- [chittad — Daemon](#chittad--daemon)
- [chitta — Client](#chitta--client)
- [CLI Tool Invocation](#cli-tool-invocation)
- [Thin Client Mode](#thin-client-mode)
- [TOON Output Format](#toon-output-format)
- [Environment Variables](#environment-variables)
- [Examples](#examples)

---

## Binaries

| Binary | Purpose | Location |
|--------|---------|----------|
| `chittad` | Daemon server (background processing, socket listener, RPC) | `~/.claude/bin/chittad` |
| `chitta` | CLI client (direct tool invocation or JSON-RPC forwarding) | `~/.claude/bin/chitta` |

Both are compiled from the same C++ codebase (`chitta/src/`).

---

## Installation

The CLI is built automatically during installation:

```bash
cd cc-soul
./scripts/smart-install.sh

# Binaries are now at:
~/.claude/bin/chitta
~/.claude/bin/chittad
```

Or build manually:

```bash
cd chitta
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Binaries are output to `cc-soul/bin/` by CMakeLists.txt, then copied to `~/.claude/bin/` by the install script.

---

## chittad — Daemon

The daemon runs as a background process, providing the soul's memory services over a Unix domain socket.

### Commands

#### daemon

Run the background daemon. Self-daemonizes by default (double-fork).

```bash
chittad daemon [options]
```

**Options:**

| Option | Description | Default |
|--------|-------------|---------|
| `--path PATH` | Mind storage directory | `~/.claude/mind` |
| `--interval SECS` | Maintenance cycle interval | `60` |
| `--foreground` | Run in foreground (don't daemonize) | Off |

**Example:**
```bash
# Standard daemon (daemonizes, creates socket)
chittad daemon

# Run in foreground for debugging
chittad daemon --foreground

# Custom path and interval
chittad daemon --path /custom/mind --interval 30
```

**What the daemon does:**
- Opens DuckDB database at `{path}/chitta.duckdb`
- Loads ONNX embedding model (bge-base-en-v1.5, 768 dimensions)
- Creates Unix domain socket at `/tmp/chitta-{hash}.sock`
- Writes PID to `/tmp/chitta-{hash}.pid`
- Starts thread pool (auto-scaling 2-16 workers)
- Runs subconscious background processing:
  - Every 1s: Check for work
  - Every 30s: Process embedding queue (batch of 20)
  - Every 30min: Hygiene cycle (decay, prune, consolidate)
  - Every 60min: Theme maintenance
- Runs transcript distillation (configurable interval)
- Runs code enrichment (symbol descriptions)

**Output:**
```
[socket_server] Listening on /tmp/chitta-1234567890.sock
[daemon] Started (socket=/tmp/chitta-1234567890.sock, interval=60s, pid=12345)
```

#### shutdown

Gracefully stop the running daemon.

```bash
chittad shutdown
```

Sends a shutdown command via the Unix socket. The daemon saves state and exits cleanly.

#### status

Check if daemon is running and responsive.

```bash
chittad status
```

#### stats

Show soul statistics.

```bash
chittad stats [--json] [--fast]
```

**Options:**

| Option | Description |
|--------|-------------|
| `--json` | Output as JSON |
| `--fast` | Skip BM25 loading (faster startup) |

**Output:**
```
Soul Statistics
═══════════════════════════════
Nodes:
  Total:  1963

Sāmarasya (Coherence):
  Global:     1.0
  Local:      1.0
  Structural: 1.0
  Temporal:   0.5
  τ (tau):    0.84

Ojas (Vitality):
  Structural: 1.0
  Semantic:   0.84
  Temporal:   0.65
  Capacity:   0.99
  ψ (psi):    0.87 [healthy]

Yantra: ready
```

---

## chitta — Client

The `chitta` binary operates in two modes:

1. **CLI mode**: Direct tool invocation with named arguments
2. **Thin client mode**: Forward raw JSON-RPC to daemon via socket

### Global Options

| Option | Description | Default |
|--------|-------------|---------|
| `--socket-path PATH` | Daemon socket path | Auto-detected |
| `--json` | Output raw JSON | Text output |
| `--toon` | Output TOON format (compact, LLM-friendly) | Text output |
| `-v, --version` | Show version | - |
| `-h, --help` | Show help | - |

---

## CLI Tool Invocation

The `chitta` binary can invoke any registered RPC tool directly from the command line:

```bash
chitta <tool-name> [--param value ...]
```

### Core Memory Tools

```bash
# Store a memory
chitta remember --content "[domain] pattern→insight" --tags "important" --type wisdom

# Semantic search
chitta recall --query "error handling patterns" --limit 10

# Add wisdom/belief/failure
chitta grow --type wisdom --title "Caching Strategy" --content "LRU with TTL for APIs"

# Get node by ID
chitta get --id "a1b2c3d4-..."

# Update content
chitta update --id "a1b2c3d4-..." --content "Updated text"

# Remove a memory
chitta forget --id "a1b2c3d4-..."

# Adjust confidence
chitta strengthen --id "a1b2c3d4-..." --amount 0.1
chitta weaken --id "a1b2c3d4-..." --amount 0.1

# Manage tags
chitta tag --id "a1b2c3d4-..." --add "important"
chitta tag --id "a1b2c3d4-..." --remove "old-tag"
```

### Search and Resonance

```bash
# Full resonance (8-phase retrieval)
chitta full_resonate --query "caching strategies" --k 5

# Full resonance with realm filter
chitta full_resonate --query "auth patterns" --realm "project:web-app" --k 3

# Partnership-only resonance (exclude code intel)
chitta full_resonate --query "user preferences" --partnership-only true
```

### Graph/Triplets

```bash
# Create relationship
chitta connect --subject "Mind" --predicate "contains" --object "remember()"

# Query triplets
chitta query --subject "Mind"
chitta query --predicate "calls"
chitta query_graph --object "DuckDBStore"
```

### Code Intelligence

```bash
# Index a codebase
chitta learn_codebase --path /path/to/project --project "my-app"

# Find symbols by name
chitta find_symbol --name "Mind" --kind class

# Read symbol source code
chitta read_symbol --name "DuckDBStore"
chitta read_function --name "full_resonate"

# Find callers/callees
chitta symbol_callers --name "remember"
chitta symbol_callees --name "full_resonate"

# Semantic code search
chitta search_symbols --query "memory storage class" --limit 5

# Codebase overview
chitta codebase_overview --format tree
```

### Realm Management

```bash
# Auto-detect current realm
chitta realm_detect

# List all realms
chitta realm_list

# Set realm for a memory
chitta realm_set --id "..." --realm "project:my-app"
```

### Session/Ledger

```bash
# Save checkpoint
chitta ledger_save --session_id "work-session-1" --project "project:cc-soul" \
  --mood "confident" --snapshot "Completed API documentation rewrite"

# Load latest checkpoint
chitta ledger_load --project "project:cc-soul"

# List checkpoints
chitta ledger_list --project "project:cc-soul" --limit 5
```

### State and Health

```bash
# Soul context
chitta soul_context

# Health check
chitta health_check

# Version
chitta version_check

# Memory hygiene stats
chitta hygiene_stats

# Run hygiene
chitta hygiene_run
```

### Observation (Hook Integration)

```bash
# Store observation (used by hooks)
chitta observe --title "Edit: main.cpp" --content "[signal] Edit: main.cpp" \
  --category signal --tags "auto:edit"
```

### Import/Export

```bash
# Import .soul file
chitta import_soul --file /path/to/data.soul

# Export memories
chitta export_soul --file /path/to/backup.soul --limit 100
```

---

## Thin Client Mode

When stdin is not a terminal, `chitta` acts as a **thin client** — forwarding raw JSON-RPC to the daemon:

```bash
# Forward JSON-RPC to daemon
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"recall","arguments":{"query":"test","limit":3}}}' \
  | chitta

# With explicit socket path
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"stats"}}' \
  | chitta --socket-path /tmp/chitta-12345.sock
```

This is how the hook scripts communicate with the daemon — they pipe JSON-RPC requests through `chitta` as a fallback when direct socket access via `nc` fails.

---

## TOON Output Format

The `--toon` flag outputs in TOON (Token-Oriented Object Notation) format — ~40% fewer tokens than JSON, optimized for LLM consumption:

```bash
chitta soul_context --toon
```

**Output:**
```
total_nodes: 1963
triplet_count: 340
avg_confidence: 0.84
status: healthy
coherence:
 global: 1.0
 local: 1.0
 structural: 1.0
 temporal: 0.5
 tau_k: 0.84
```

Compare with JSON (same data, more tokens):
```json
{"total_nodes":1963,"triplet_count":340,"avg_confidence":0.84,"status":"healthy","coherence":{"global":1.0,"local":1.0,"structural":1.0,"temporal":0.5,"tau_k":0.84}}
```

---

## Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `CHITTA_DB_PATH` | Mind storage directory | `~/.claude/mind` |
| `CLAUDE_PLUGIN_ROOT` | Plugin root for model paths | None |
| `SUBCONSCIOUS_INTERVAL` | Daemon cycle interval (seconds) | `60` |
| `CHITTAD_BIN` | Path to chittad binary | `~/.claude/bin/chittad` |
| `CHITTA_BIN` | Path to chitta binary | `~/.claude/bin/chitta` |

### Model Path Resolution

If `--model` and `--vocab` are not specified, the binaries look for them in order:

1. `$CLAUDE_PLUGIN_ROOT/chitta/models/` (when running as plugin)
2. `~/.claude/models/model.onnx` and `~/.claude/models/vocab.txt` (smart-install location)
3. `~/.claude/mind/model.onnx` and `~/.claude/mind/vocab.txt` (legacy)

---

## Examples

### Basic Usage

```bash
# Check soul health
chitta soul_context

# Quick search
chitta recall --query "authentication" --limit 3

# Deep resonance search
chitta full_resonate --query "microservices patterns" --k 5

# Run maintenance
chitta cycle
```

### Daemon Management

```bash
# Start daemon (self-daemonizes)
chittad daemon

# Check status
chittad status

# View logs
tail -f ~/.claude/mind/.subconscious.log

# Stop daemon
chittad shutdown
```

### JSON Integration

```bash
# Get stats as JSON for scripts
chitta soul_context --json | jq '.total_nodes'

# Search and extract text
chitta full_resonate --query "error handling" --json --k 5 | \
  jq -r '.results[].text'
```

### Code Intelligence Workflow

```bash
# Index project
chitta learn_codebase --path . --project "my-project"

# Find a class
chitta find_symbol --name "UserService" --kind class

# Read its code
chitta read_symbol --name "UserService"

# Find what calls it
chitta symbol_callers --name "UserService"

# Semantic search
chitta search_symbols --query "database connection pooling"
```

---

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Error (see stderr for details) |

---

## Troubleshooting

### "Yantra not attached"

The embedding model couldn't be loaded.

```bash
# Check if model files exist
ls -la ~/.claude/models/model.onnx
ls -la ~/.claude/models/vocab.txt

# Or at legacy location
ls -la ~/.claude/mind/model.onnx
```

### "Failed to open mind"

Database path issue.

```bash
# Check path exists
ls -la ~/.claude/mind/

# Create if needed
mkdir -p ~/.claude/mind

# Check for DuckDB files
ls -la ~/.claude/mind/chitta.duckdb
```

### Socket Connection Failed

```bash
# Check socket exists
ls /tmp/chitta-*.sock

# Check daemon is running
pgrep -f "chittad daemon"

# Clean up and restart
rm -f /tmp/chitta-*.sock /tmp/chitta-*.pid /tmp/chitta-*.lock
chittad daemon
```

---

*The command line is another window into the soul.*
