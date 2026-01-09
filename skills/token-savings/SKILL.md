---
name: token-savings
description: Show estimated token savings from soul memory
execution: direct
aliases: [savings, tokens]
---

# Token Savings

Show how many tokens cc-soul has saved by avoiding redundant operations.

**Execute directly** — Runs the token-savings.sh script and shows report.

## How It Works

The soul tracks three types of token savings:

### 1. Recall Hits
When you ask about something and the soul already knows it, we avoid re-reading files:
- File re-read avoided: ~500-5000 tokens per file
- Exploration avoided: ~2000-10000 tokens per search

### 2. Codemap Cache Hits
When the codemap is cached and git commit unchanged:
- Full codebase scan avoided: ~1000-5000 tokens
- File tree generation avoided: ~500-2000 tokens

### 3. Transparent Memory Injection
Context injected from memory instead of exploration:
- Pre-answered questions: ~500-2000 tokens per injection
- Pattern recall: ~200-1000 tokens per pattern

## Usage

```
/token-savings          # Show report
/token-savings reset    # Reset counters
```

## Implementation

When this skill is invoked:

```bash
${CLAUDE_PLUGIN_ROOT}/scripts/token-savings.sh report
```

## Example Output

```
=== CC-Soul Token Savings Report ===

Recall (avoided file re-reads):
  Hits: 12
  Chars saved: 45000 (~11250 tokens)

Codemap cache (avoided re-scans):
  Hits: 3
  Chars saved: 8000 (~2000 tokens)

Transparent memory (injected context):
  Injections: 45
  Chars injected: 22500 (~5625 tokens)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
TOTAL ESTIMATED SAVINGS: ~18875 tokens
```

## Accuracy Notes

Token savings are estimates based on:
- 4 characters ≈ 1 token (rough average)
- Assumes alternative would be file read or exploration
- Actual savings vary by use case

The real benefit is qualitative: faster responses, less context churn, preserved working memory across sessions.
