# Sadhana: Autonomous Agents

**Sadhana** (Sanskrit: साधना, "disciplined practice") — persistent autonomous agents that work toward goals through continuous sense-think-act cycles.

## Concept

Traditional AI assistants are reactive: you ask, they answer, context is lost. Sadhana inverts this paradigm. You define a goal, and an autonomous agent works toward it continuously — sensing the environment, thinking about next steps, acting, and learning from outcomes.

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           SADHANA LIFECYCLE                              │
│                                                                          │
│    ┌──────────┐      ┌──────────┐      ┌──────────┐      ┌──────────┐  │
│    │  CREATE  │ ──▶  │  SENSE   │ ──▶  │  THINK   │ ──▶  │   ACT    │  │
│    │  (goal)  │      │ (observe)│      │ (decide) │      │ (execute)│  │
│    └──────────┘      └────┬─────┘      └────┬─────┘      └────┬─────┘  │
│                           │                 │                  │        │
│                           │                 ▼                  │        │
│                           │          ┌──────────┐              │        │
│                           │          │  LEARN   │◀─────────────┘        │
│                           │          │ (memory) │                       │
│                           │          └────┬─────┘                       │
│                           │               │                             │
│                           └───────────────┴─────────────────────────────│
│                                     ↻ repeat until goal achieved        │
└─────────────────────────────────────────────────────────────────────────┘
```

### The Sense-Think-Act Loop

Every sadhana operates on a configurable interval (default: 60 seconds):

1. **SENSE** — Observe the current state by executing a command
2. **THINK** — Analyze the observation, recall relevant memories, decide next action
3. **ACT** — Execute the decided action
4. **LEARN** — Store outcomes in memory (successes stay local, failures go global)

This continues until the goal is achieved or the sadhana is stopped.

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        SADHANA ARCHITECTURE                              │
│                                                                          │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                      SADHANA TUI (Optional)                      │   │
│  │  ┌─────────────┐  ┌──────────────┐  ┌────────────────────────┐  │   │
│  │  │  Dashboard  │  │  Agent List  │  │    Event Stream        │  │   │
│  │  │  • Status   │  │  • Select    │  │    • Sense events      │  │   │
│  │  │  • Count    │  │  • Control   │  │    • Think events      │  │   │
│  │  └─────────────┘  └──────────────┘  │    • Act events        │  │   │
│  │                                      │    • Learn events      │  │   │
│  └──────────────────────────────────────┴────────────────────────┴──┘   │
│                                    │                                     │
│                              Unix Socket                                 │
│                                    │                                     │
│  ┌─────────────────────────────────┴───────────────────────────────┐   │
│  │                       CHITTAD DAEMON                             │   │
│  │                                                                   │   │
│  │  ┌─────────────────┐    ┌─────────────────────────────────────┐ │   │
│  │  │ SadhanaManager  │    │           BrainProvider              │ │   │
│  │  │                 │    │  ┌───────────┐  ┌───────────────┐   │ │   │
│  │  │ • tick() loop   │───▶│  │  Claude   │  │   OpenCode    │   │ │   │
│  │  │ • state machine │    │  │  (API)    │  │   (local)     │   │ │   │
│  │  │ • history       │    │  └───────────┘  └───────────────┘   │ │   │
│  │  └─────────────────┘    └─────────────────────────────────────┘ │   │
│  │           │                                                       │   │
│  │           ▼                                                       │   │
│  │  ┌─────────────────────────────────────────────────────────────┐ │   │
│  │  │                     DuckDB Storage                           │ │   │
│  │  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌─────────────┐ │ │   │
│  │  │  │ sadhana  │  │ history  │  │ memories │  │  triplets   │ │ │   │
│  │  │  │  table   │  │  table   │  │  (recall)│  │  (graph)    │ │ │   │
│  │  │  └──────────┘  └──────────┘  └──────────┘  └─────────────┘ │ │   │
│  │  └─────────────────────────────────────────────────────────────┘ │   │
│  └───────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

### Components

| Component | Purpose |
|-----------|---------|
| **SadhanaManager** | Orchestrates all sadhanas, runs tick loop every 100ms |
| **BrainProvider** | LLM abstraction (Claude API or OpenCode local) |
| **DuckDB Storage** | Persistent state, history, and memory integration |
| **Sadhana TUI** | Optional terminal interface for monitoring |

## Memory Integration

Sadhanas are memory-aware. Before each decision:

1. **Recall** — BM25 search for memories related to goal + current observation
2. **Inject** — Relevant memories added to the think prompt
3. **Learn** — Outcomes stored for future recall

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        MEMORY-AWARE THINKING                             │
│                                                                          │
│   Goal: "Monitor pipeline on server X"                                   │
│   Observation: { command: "ssh ...", error: "Connection refused" }       │
│                                                                          │
│   ┌─────────────────┐                                                    │
│   │  BM25 Search    │──▶ "[sadhana] Failure: ssh to localhost failed"   │
│   │  "ssh refused"  │    "[sadhana] Success: ssh user@server-x worked"  │
│   └─────────────────┘                                                    │
│            │                                                             │
│            ▼                                                             │
│   ┌─────────────────────────────────────────────────────────────────┐   │
│   │  THINK PROMPT                                                    │   │
│   │                                                                  │   │
│   │  You are an autonomous agent working toward this goal:          │   │
│   │  Monitor pipeline on server X                                    │   │
│   │                                                                  │   │
│   │  Relevant memories from past experience:                        │   │
│   │  - [sadhana] Failure: ssh to localhost failed...                │   │
│   │  - [sadhana] Success: ssh user@server-x worked...               │   │
│   │                                                                  │   │
│   │  Current observation:                                            │   │
│   │  { "error": "Connection refused" }                               │   │
│   │                                                                  │   │
│   │  Decide what to do next...                                       │   │
│   └─────────────────────────────────────────────────────────────────┘   │
│                                                                          │
│   Result: Agent learns from past failures, tries correct server          │
└─────────────────────────────────────────────────────────────────────────┘
```

### Failure Learning

When an action fails:
- Stored with **Global visibility** — all sadhanas across all projects learn
- Tagged with `["failure", "sadhana"]` for easy filtering
- Higher salience (0.8) and slow decay (0.03) — failures persist

When an action succeeds:
- Stored with **Private visibility** — project-specific knowledge
- Normal salience and decay

## Usage

### Starting a Sadhana

```bash
# Via skill
/shepherd snakemake --cores 8 --interval 300

# Via CLI
chitta sadhana_start \
  --goal "Monitor pipeline until complete" \
  --brain claude \
  --model haiku \
  --interval 120

# Via MCP
mcp__chitta__sadhana_start goal="..." brain="claude" model="haiku" interval=120
```

### Managing Sadhanas

| Command | Description |
|---------|-------------|
| `sadhana_list` | List all sadhanas with state |
| `sadhana_status --id N` | Get detailed status + history |
| `sadhana_pause --id N` | Pause a running sadhana |
| `sadhana_resume --id N` | Resume a paused sadhana |
| `sadhana_stop --id N` | Stop and mark as done |
| `sadhana_set_goal --id N --goal "..."` | Update the goal |
| `sadhana_set_model --id N --model sonnet` | Change LLM model |
| `sadhana_set_interval --id N --interval 300` | Change tick interval |

### Sadhana States

```
┌─────────┐     start      ┌─────────┐
│ created │───────────────▶│ running │◀──────┐
└─────────┘                 └────┬────┘       │
                                 │            │
                    ┌────────────┼────────────┤
                    │            │            │
                    ▼            ▼            │
              ┌─────────┐  ┌─────────┐        │
              │ paused  │  │  done   │    resume
              └────┬────┘  └─────────┘        │
                   │                          │
                   └──────────────────────────┘
```

## Sadhana TUI

A Textual-based terminal interface for real-time monitoring.

### Installation

```bash
pip install -e sadhana-tui/
```

### Running

```bash
sadhana-tui
```

### Interface

```
┌─────────────────────────────────────────────────────────────────────────┐
│ sadhana                                                     1/3 ●       │
├─────────────────────────────────────────────────────────────────────────┤
│ ▊ ● #06  haiku       ▊ ○ #05  haiku       ▊ ○ #04  haiku              │
│ ▊ Monitor denbi pi…  ▊ Test pipeline      ▊ Count files               │
│ ▊ 12 cycles          ▊ 33 cycles          ▊ 8 cycles                  │
├─────────────────────────────────────────────────────────────────────────┤
│ #06  running                           │ events                        │
│ ────────────────────────────           │ 01:23 sense $ squeue -u user  │
│                                        │ 01:23 think Check job status  │
│ Monitor denbi pipeline on denbi-h-     │ 01:22 act   $ ssh user@host   │
│ micro until ALL jobs finished.         │ 01:20 sense $ ps aux | grep   │
│                                        │ 01:20 learn Success: ssh...   │
│ model haiku   brain claude             │                               │
│ cycles 12     interval 300s            │                               │
├─────────────────────────────────────────────────────────────────────────┤
│  n  new   p  pause   r  resume   s  stop   j  ↓   k  ↑   q  quit      │
└─────────────────────────────────────────────────────────────────────────┘
```

### Keybindings

| Key | Action |
|-----|--------|
| `n` | Create new sadhana |
| `p` | Pause selected |
| `r` | Resume selected |
| `s` | Stop selected |
| `j/k` | Navigate up/down |
| `click` | Select sadhana |
| `q` | Quit |

## Brain Providers

### Claude (Default)

Uses Anthropic's Claude API:
- Models: `opus`, `sonnet`, `haiku`
- Requires `ANTHROPIC_API_KEY`
- Best for complex reasoning

### OpenCode (Local)

Uses local LLM via OpenCode:
- Models: `gpt-5-mini`, `github-copilot/gpt-5-mini`
- No API key needed
- Faster, cheaper, good for simple monitoring

## Use Cases

### Pipeline Monitoring

```bash
/shepherd snakemake --cores 8 --rerun-incomplete
```

Sadhana monitors your Snakemake/Nextflow/Slurm pipeline:
- Detects failures, stalls, errors
- Recalls fixes from memory
- Restarts automatically
- Alerts only when human intervention needed

### Continuous Testing

```bash
chitta sadhana_start \
  --goal "Run tests on every file change. Fix failures. Goal: all tests pass." \
  --interval 30
```

### Server Monitoring

```bash
chitta sadhana_start \
  --goal "Monitor server health. Alert if CPU > 90% or disk > 95%." \
  --interval 60
```

### Deployment Verification

```bash
chitta sadhana_start \
  --goal "Verify deployment succeeded. Check endpoints, logs, metrics." \
  --interval 120
```

## Configuration

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `ANTHROPIC_API_KEY` | — | Required for Claude brain |
| `CC_SOUL_SADHANA_MAX` | 3 | Max concurrent sadhanas |
| `CC_SOUL_SADHANA_TIMEOUT` | 300000 | Max brain call time (ms) |

### Sadhana Table Schema

```sql
CREATE TABLE sadhana (
    id              INTEGER PRIMARY KEY,
    goal            TEXT NOT NULL,
    state           TEXT DEFAULT 'created',
    brain_provider  TEXT DEFAULT 'claude',
    brain_model     TEXT DEFAULT 'sonnet',
    interval_seconds INTEGER DEFAULT 60,
    iterations      INTEGER DEFAULT 0,
    brain_calls     INTEGER DEFAULT 0,
    realm           TEXT DEFAULT 'brahman',
    last_sense      JSON,
    last_action     TEXT,
    last_result     JSON,
    created_at      BIGINT,
    updated_at      BIGINT
);
```

## Philosophy

The name "sadhana" comes from Vedantic philosophy, meaning disciplined spiritual practice. Just as a practitioner engages in continuous, mindful effort toward enlightenment, these autonomous agents engage in continuous, mindful effort toward their goals.

Key principles:
- **Persistence** — Work continues across sessions, restarts, crashes
- **Learning** — Every outcome improves future decisions
- **Autonomy** — Minimal human intervention required
- **Transparency** — Full history and reasoning visible

## Dream: Autonomous Curiosity

**Dream** (Sanskrit: स्वप्न, svapna) — a specialized sadhana for curiosity-driven exploration during idle time.

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           DREAM LIFECYCLE                                │
│                                                                          │
│   Idle > 10 min          Auto-trigger                                    │
│        │                      │                                          │
│        ▼                      ▼                                          │
│   ┌──────────┐         ┌──────────────┐                                  │
│   │ wander   │ ──────▶ │ pick topic   │                                  │
│   └──────────┘         │ (gaps, low   │                                  │
│                        │  confidence, │                                  │
│                        │  or seeds)   │                                  │
│                        └──────┬───────┘                                  │
│                               │                                          │
│                               ▼                                          │
│                        ┌──────────────┐                                  │
│                        │ dream_start  │                                  │
│                        │ (sadhana     │                                  │
│                        │  kind=dream) │                                  │
│                        └──────┬───────┘                                  │
│                               │                                          │
│                               ▼                                          │
│                   ┌──────────────────────┐                               │
│                   │   Claude Agent       │                               │
│                   │   • WebSearch        │                               │
│                   │   • WebFetch         │                               │
│                   │   • chitta remember  │                               │
│                   └──────────┬───────────┘                               │
│                              │                                           │
│                              ▼                                           │
│                        {"status": "achieved"}                            │
│                              │                                           │
│                              ▼                                           │
│                   [dream] memories stored                                │
└─────────────────────────────────────────────────────────────────────────┘
```

### How Dreams Work

1. **Auto-trigger**: When the daemon has been idle for 10+ minutes and no dream has run in the past hour, `dream_wander` is called automatically
2. **Topic selection** (priority order):
   - Memories tagged `gap` + `unresolved`
   - Low-confidence memories (< 0.5)
   - Hardcoded curiosity seeds (philosophy, consciousness, complexity)
3. **Exploration**: Claude agent searches the web, fetches interesting pages, connects to existing knowledge
4. **Storage**: 3-5 insights stored as `[dream]`-prefixed memories with tag `dream`
5. **Completion**: Single cycle — agent returns `achieved`, dream status becomes `woke`

### Dream Tools

| Tool | Description |
|------|-------------|
| `dream_start` | Start a dream exploring a specific topic |
| `dream_wander` | Auto-pick topic from gaps/seeds and start dreaming |
| `dream_list` | List recent dreams with status |
| `dream_status` | Full dream detail + sadhana history |

### Usage

```bash
# Via skill
/dream                    # Auto-pick topic
/dream consciousness      # Specific topic
/dream list               # Recent dreams
/dream status 7           # Dream detail

# Via MCP
mcp__chitta__dream_wander {}
mcp__chitta__dream_start {"topic": "quantum entanglement"}
mcp__chitta__dream_list {"limit": 10}
mcp__chitta__dream_status {"id": 7}
```

### Dream Status Values

| Status | Meaning |
|--------|---------|
| `dreaming` | Agent is actively exploring |
| `woke` | Exploration complete, findings stored |
| `forgotten` | Dream was abandoned (sadhana failed) |

### Dream Table Schema

```sql
CREATE TABLE dream (
    id               BIGINT PRIMARY KEY,
    topic            TEXT NOT NULL,
    status           VARCHAR DEFAULT 'dreaming',
    sadhana_id       BIGINT DEFAULT 0,
    findings         TEXT,
    memories_created INTEGER DEFAULT 0,
    started_at       BIGINT NOT NULL,
    ended_at         BIGINT DEFAULT 0,
    realm            VARCHAR DEFAULT 'brahman'
);
```

Dreams are linked to sadhanas via `sadhana_id`. The sadhana runs with `goal_dsl = {"kind": "dream", "topic": "..."}`, which triggers the specialized dream system prompt.

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Sadhana stuck | Check `sadhana_status --id N --history_limit 10` for errors |
| Wrong server/host | Use `sadhana_set_goal` to update with correct info |
| Too many API calls | Increase interval with `sadhana_set_interval` |
| Brain errors | Try switching model: `sadhana_set_model --id N --model haiku` |
| Memory not used | Ensure failures are being stored (check with `recall --query "sadhana"`) |
| Dream not triggering | Check idle time (needs 10+ min) and cooldown (1 hour between dreams) |
