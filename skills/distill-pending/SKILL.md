---
name: distill-pending
description: Process pending transcript distillation
execution: inline
---

# Distill Pending Sessions

Process staged conversation transcripts and extract learnings.

## Instructions

You have pending session transcripts that need distillation. For each staged file:

1. Read the conversation content
2. Extract learnings in SSL format:
   - Key decisions made and why
   - Problems solved and how
   - Patterns discovered
   - Failures and lessons learned
   - Important facts worth remembering

3. Store each learning using this format:

```
[LEARN] [domain] subject→action→result @location
[ε] Key details and context.
[TRIPLET] subject predicate object
```

4. After processing, mark the staging file as complete.

## Execution

Read the staging directory and process pending files:

```bash
ls ~/.claude/mind/.distill_staging/*.json 2>/dev/null | head -5
```

For each pending file, read it and extract the conversation:

```bash
cat ~/.claude/mind/.distill_staging/<file>.json | jq -r '.conversation' | head -100
```

After extracting learnings, mark as processed:

```bash
# Update status in staging file
jq '.status = "processed"' <file>.json > <file>.json.tmp && mv <file>.json.tmp <file>.json
```

Or archive if no longer needed:

```bash
mv <file>.json ~/.claude/mind/.distill_staging/archive/
```

## Quality Guidelines

- Only extract genuinely useful learnings
- Skip trivial operations (simple file reads, routine commands)
- Focus on decisions, solutions, failures, and patterns
- Each learning should help in future similar situations
- Use specific domains: [cc-soul], [project-name], [domain], etc.
