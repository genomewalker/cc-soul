# CC-Soul CLI Reference

The `chittad` command-line tool provides direct access to soul operations without going through Claude Code.

---

## Table of Contents

- [Installation](#installation)
- [Global Options](#global-options)
- [Commands](#commands)
- [Yajna Commands](#yajna-commands)
- [Realm Commands](#realm-commands)
- [Review Commands](#review-commands)
- [Evaluation Commands](#evaluation-commands)
- [Environment Variables](#environment-variables)
- [Examples](#examples)

---

## Installation

The CLI is built as part of the standard setup:

```bash
cd cc-soul
./setup.sh

# CLI is now at:
./bin/chittad
```

Or build manually:

```bash
cd chitta
mkdir build && cd build
cmake ..
make chittad
```

---

## Global Options

These options apply to all commands:

| Option | Description | Default |
|--------|-------------|---------|
| `--path PATH` | Mind storage path | `~/.claude/mind/chitta` |
| `--model PATH` | ONNX model path | Auto-detected |
| `--vocab PATH` | Vocabulary file path | Auto-detected |
| `--json` | Output as JSON | `false` |
| `--fast` | Skip BM25 loading (faster startup) | `false` |
| `-v, --version` | Show version | - |
| `-h, --help` | Show help | - |

### Path Resolution

If `--model` and `--vocab` are not specified, the CLI looks for them in:

1. `$CLAUDE_PLUGIN_ROOT/chitta/models/` (when running as plugin)
2. `~/.claude/mind/model.onnx` and `~/.claude/mind/vocab.txt`

---

## Commands

### stats

Show soul statistics and health metrics.

```bash
chittad stats [--json] [--fast]
```

**Output:**
```
Soul Statistics
═══════════════════════════════
Nodes:
  Hot:    1963
  Warm:   0
  Cold:   0
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

**JSON Output:**
```json
{
  "version": "2.25.0",
  "hot": 1963,
  "warm": 0,
  "cold": 0,
  "total": 1963,
  "coherence": {
    "global": 1.0,
    "local": 1.0,
    "structural": 1.0,
    "temporal": 0.5,
    "tau": 0.84
  },
  "ojas": {
    "structural": 1.0,
    "semantic": 0.84,
    "temporal": 0.65,
    "capacity": 0.99,
    "psi": 0.87,
    "status": "healthy"
  },
  "yantra": true
}
```

---

### recall

Semantic search across all memory.

```bash
chittad recall <query> [--limit N]
```

**Arguments:**
| Argument | Description |
|----------|-------------|
| `query` | The search query |

**Options:**
| Option | Description | Default |
|--------|-------------|---------|
| `--limit N` | Maximum results | `5` |

**Example:**
```bash
chittad recall "error handling patterns" --limit 10
```

**Output:**
```
Results for: error handling patterns
═══════════════════════════════

[1] (score: 0.82)
[wisdom] Error Boundary Pattern: Wrap components in error boundaries to
prevent cascading failures. Log errors, show fallback UI, report to
monitoring.

[2] (score: 0.76)
[episode] Implemented retry with exponential backoff in Project X for
transient API failures. Max 3 retries, 1s/2s/4s delays.

[3] (score: 0.71)
[failure] Silent failure anti-pattern: Swallowing exceptions without
logging caused hours of debugging. Always log or rethrow.
```

---

### resonate

Full resonance search (all 6 phases).

```bash
chittad resonate <query> [--limit N] [--json]
```

**Arguments:**
| Argument | Description |
|----------|-------------|
| `query` | The search query |

**Options:**
| Option | Description | Default |
|--------|-------------|---------|
| `--limit N` | Maximum results | `5` |
| `--json` | Output as JSON | `false` |

**Example:**
```bash
chittad resonate "caching strategies" --limit 5
```

**Output:**
```
[72%] [wisdom] LRU Caching: Time-to-live of 5 minutes works well for API
responses. Invalidate on write, not on read. Consider cache stampede...

[65%] [episode] Implemented Redis caching in Project X. Used cache-aside
pattern with write-through for critical data...

[58%] [failure] Cache inconsistency bug: Race condition when multiple
processes updated cache simultaneously. Solution: distributed locks...
```

**JSON Output:**
```json
{
  "query": "caching strategies",
  "results": [
    {
      "relevance": 0.72,
      "similarity": 0.68,
      "text": "[wisdom] LRU Caching: Time-to-live..."
    }
  ]
}
```

---

### cycle

Run maintenance cycle (decay, synthesis, save).

```bash
chittad cycle
```

**Output:**
```
Running maintenance cycle...
Cycle complete.
  Before: 1963 nodes
  After:  1963 nodes
  Decay applied: yes
```

---

## Yajna Commands

Memory maintenance and batch operations.

### get

Fast direct ID lookup with full content.

```bash
chitta get --id "UUID"
```

**Output:**
```
=== a1b2c3d4-... ===
Type: episode
Confidence: 85%
Tags: dev:hot, file:auth.ts, ε-processed

[auth] JWT validation→check_expiry→refresh_if_needed @auth.ts:42
[ε] Validates JWT, refreshes if within 5min of expiry.
```

---

### yajna_list

List nodes needing ε-yajna processing.

```bash
chitta yajna_list --limit 20 [--filter "domain"]
```

**Output:**
```
Nodes for epsilon-yajna (SSL + triplet conversion):

[a1b2c3d4-...] Verbose explanation... (820 chars, epsilon=13%)
[b2c3d4e5-...] [cc-soul] func()→result @file.hpp (150 chars, epsilon=100%)

Total: 42 nodes need processing (showing 20)
```

---

### yajna_inspect

Inspect a node for yajna analysis.

```bash
chitta yajna_inspect --id "UUID"
```

---

### yajna_mark_processed

Batch mark SSL-format nodes as ε-processed. Efficient C++ loop.

```bash
chitta yajna_mark_processed [--epsilon_threshold 0.8] [--dry_run true|false]
```

**Options:**
| Option | Default | Description |
|--------|---------|-------------|
| `--epsilon_threshold` | 0.8 | Min epsilon to auto-mark |
| `--dry_run` | true | Preview only |
| `--filter` | - | Text filter |

**Output:**
```
Marked 788 nodes as ε-processed
```

---

### batch_remove

Remove multiple nodes from a file of UUIDs.

```bash
chitta batch_remove --file /tmp/ids.txt [--dry_run false]
```

File format: one UUID per line, `#` for comments.

---

### batch_tag

Tag multiple nodes from a file of UUIDs.

```bash
chitta batch_tag --file /tmp/ids.txt --add "ε-processed" [--dry_run false]
```

---

### tag

Add or remove tags from a node.

```bash
chitta tag --id "UUID" --add "important"
chitta tag --id "UUID" --remove "old-tag"
```

---

### daemon

Run the daemon for background processing and RPC communication.

```bash
chittad daemon --socket [--interval SECS] [--foreground]
```

**Options:**
| Option | Description | Default |
|--------|-------------|---------|
| `--socket` | Enable RPC socket server (required for hooks/MCP) | Off |
| `--interval SECS` | Maintenance cycle interval in seconds | `60` |
| `--foreground` | Run in foreground (don't daemonize) | Off |

**Example:**
```bash
# Standard daemon (daemonizes, creates socket)
chittad daemon --socket

# Run in foreground for debugging
chittad daemon --socket --foreground

# Custom interval
chittad daemon --socket --interval 30
```

**Output:**
```
[socket_server] Listening on /tmp/chitta-HASH.sock
[daemon] Started (socket=/tmp/chitta-HASH.sock, interval=60s, pid=12345)
...
```

**Daemon Processing:**

Each cycle performs:
1. Apply decay to all nodes
2. Synthesize wisdom from episode clusters
3. Apply pending feedback (Hebbian learning)
4. Run attractor dynamics (settle nodes)
5. Save snapshot

**Stopping:**
```bash
# Graceful shutdown (preferred)
chittad shutdown

# Or send SIGTERM
pkill -f "chittad daemon"
```

---

### upgrade

Upgrade database to current schema version.

```bash
chittad upgrade
```

**Output:**
```
Database: /home/user/.claude/mind/chitta.hot
Current version: 4
Target version: 5

Upgrading...
Upgrade complete: v4 → v5
Backup saved: /home/user/.claude/mind/chitta.hot.v4.backup
```

---

### convert

Convert storage format between unified and segment-based.

```bash
chittad convert <format>
```

**Arguments:**
| Argument | Description |
|----------|-------------|
| `format` | Target format: `unified` or `segments` |

**Example:**
```bash
chittad convert unified
```

**Output:**
```
Converting /home/user/.claude/mind/chitta to unified format...

Conversion complete!
  Nodes converted: 1963
  Backup saved: /home/user/.claude/mind/chitta.hot.backup

The database will now use unified format on next open.
```

---

## Realm Commands

### realm_get

Get current realm context.

```bash
chitta realm_get
```

**Output:**
```
Current realm: project:cc-soul
(Realm context persists across sessions)
```

---

### realm_set

Set current realm (persists across sessions).

```bash
chitta realm_set --realm "project:cc-soul"
```

**Options:**
| Option | Description | Required |
|--------|-------------|----------|
| `--realm NAME` | Realm name | Yes |

---

### realm_create

Create a new realm with optional parent hierarchy.

```bash
chitta realm_create --realm "project:new-app" --parent "project:shared"
```

**Options:**
| Option | Description | Required |
|--------|-------------|----------|
| `--realm NAME` | New realm name | Yes |
| `--parent NAME` | Parent realm | No (default: brahman) |

---

## Review Commands

### review_list

List items in the review queue.

```bash
chitta review_list --status pending --limit 10
```

**Options:**
| Option | Description | Default |
|--------|-------------|---------|
| `--status STATUS` | Filter: pending, approved, rejected | pending |
| `--limit N` | Maximum items | 20 |

---

### review_decide

Approve or reject a node.

```bash
chitta review_decide --id "a1b2c3d4-..." --decision approve --reason "Verified"
```

**Options:**
| Option | Description | Required |
|--------|-------------|----------|
| `--id ID` | Node ID | Yes |
| `--decision DECISION` | approve, reject, edit, defer | Yes |
| `--edited_content TEXT` | New content (for edit) | No |
| `--reason TEXT` | Reason for decision | No |

---

### review_batch

Batch decision on multiple items.

```bash
chitta review_batch --ids "id1,id2,id3" --decision approve
```

**Options:**
| Option | Description | Required |
|--------|-------------|----------|
| `--ids IDS` | Comma-separated node IDs | Yes |
| `--decision DECISION` | approve, reject, defer | Yes |

---

### review_stats

Get review queue statistics.

```bash
chitta review_stats
```

**Output:**
```
=== Review Stats ===
Pending: 15
Approved: 142
Rejected: 8
Approval rate: 94.7%
```

---

## Evaluation Commands

### eval_run

Run golden recall test suite.

```bash
chitta eval_run [--test_name NAME]
```

**Options:**
| Option | Description | Default |
|--------|-------------|---------|
| `--test_name NAME` | Run specific test | All tests |

---

### eval_add_test

Add a golden test case.

```bash
chitta eval_add_test --name "auth_test" --query "authentication" --expected "id1,id2"
```

**Options:**
| Option | Description | Required |
|--------|-------------|----------|
| `--name NAME` | Test case name | Yes |
| `--query QUERY` | Test query | Yes |
| `--expected IDS` | Expected node IDs (comma-separated) | Yes |

---

### epiplexity_check

Check compression quality.

```bash
chitta epiplexity_check --content "Full text" --seed "compressed→seed"
```

**Options:**
| Option | Description |
|--------|-------------|
| `--content TEXT` | Full content to check |
| `--seed TEXT` | Compressed seed |
| `--id ID` | Node ID to check |

---

### epiplexity_drift

Detect compression quality degradation.

```bash
chitta epiplexity_drift --window_days 7
```

**Options:**
| Option | Description | Default |
|--------|-------------|---------|
| `--window_days N` | Analysis window | 30 |

---

## Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `CHITTA_DB_PATH` | Database path | `~/.claude/mind/chitta` |
| `CLAUDE_PLUGIN_ROOT` | Plugin root for model paths | None |
| `SUBCONSCIOUS_INTERVAL` | Daemon cycle interval | `60` |

---

## Examples

### Basic Usage

```bash
# Check soul health
chittad stats

# Quick search
chittad recall "authentication" --limit 3

# Deep resonance search
chittad resonate "microservices patterns"

# Run maintenance
chittad cycle
```

### Daemon Management

```bash
# Start daemon (self-daemonizes)
chittad daemon --socket

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
chittad stats --json | jq '.coherence.tau'

# Search and extract
chittad resonate "error handling" --json --limit 5 | \
  jq -r '.results[].text'
```

### Fast Startup

```bash
# Skip BM25 for quick stats
chittad stats --fast

# Useful for health checks
chittad stats --fast --json | jq '.ojas.status'
```

### Custom Paths

```bash
# Use different database
chittad stats --path /custom/path/chitta

# Specify model explicitly
chittad recall "query" \
  --model /path/to/model.onnx \
  --vocab /path/to/vocab.txt
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
ls -la ~/.claude/mind/model.onnx
ls -la ~/.claude/mind/vocab.txt

# Or specify paths explicitly
chittad stats --model /path/to/model.onnx --vocab /path/to/vocab.txt
```

### "Failed to open mind"

Database path issue.

```bash
# Check path exists
ls -la ~/.claude/mind/

# Create if needed
mkdir -p ~/.claude/mind

# Check permissions
chmod 755 ~/.claude/mind
```

### Slow Startup

BM25 index loading takes time for large databases.

```bash
# Use --fast for quick operations
chittad stats --fast

# Or disable BM25 permanently (hybrid search won't work)
# Note: Not recommended for regular use
```

---

*The command line is another window into the soul.*
