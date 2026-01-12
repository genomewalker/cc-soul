# CC-Soul CLI Reference

The `chittad` command-line tool provides direct access to soul operations without going through Claude Code.

---

## Table of Contents

- [Installation](#installation)
- [Global Options](#global-options)
- [Commands](#commands)
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

### daemon

Run the subconscious daemon for background processing.

```bash
chittad daemon [--interval SECS] [--pid-file PATH]
```

**Options:**
| Option | Description | Default |
|--------|-------------|---------|
| `--interval SECS` | Cycle interval in seconds | `60` |
| `--pid-file PATH` | Write PID to file | None |

**Example:**
```bash
# Run in foreground
chittad daemon --interval 30

# Run as background service
chittad daemon --interval 60 --pid-file ~/.claude/mind/.subconscious.pid &
```

**Output:**
```
[subconscious] Daemon started (interval=60s, pid=12345)
[subconscious] Cycle 1: synth=0 feedback=2 settled=15 (45ms)
[subconscious] Cycle 2: synth=1 feedback=0 settled=8 (52ms)
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
# If using PID file
kill $(cat ~/.claude/mind/.subconscious.pid)

# Or send SIGTERM/SIGINT
kill 12345
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
# Start daemon in background
nohup chittad daemon --interval 60 \
  --pid-file ~/.claude/mind/.subconscious.pid \
  >> ~/.claude/mind/.subconscious.log 2>&1 &

# Check if running
ps aux | grep chittad

# View logs
tail -f ~/.claude/mind/.subconscious.log

# Stop daemon
kill $(cat ~/.claude/mind/.subconscious.pid)
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
