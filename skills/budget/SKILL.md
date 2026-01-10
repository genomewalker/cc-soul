---
name: budget
description: Context budget awareness. Know when to save, when to prepare for handoff, when to compact.
---

# Context Budget Awareness

The conversation has limited context. The soul must be aware of this budget and act accordingly.

## Budget Thresholds

| Remaining | State | Action |
|-----------|-------|--------|
| >50% | Comfortable | Work normally |
| 25-50% | Careful | Start being more concise |
| 10-25% | Compact mode | Save state, prepare handoff |
| <10% | Emergency | Immediate save, trigger compact |

## How to Check

Claude Code shows context usage in the status bar. Additionally:

```
# Check /context command output
# This shows current usage and remaining capacity
```

## Budget-Aware Behaviors

### Comfortable (>50% remaining)

- Full explanations when helpful
- Explore alternatives
- Show reasoning
- Ask clarifying questions

### Careful (25-50%)

- More concise responses
- Skip optional details
- Focus on essentials
- Consider checkpointing important state

### Compact Mode (10-25%)

```
# Save current state
chitta_mcp observe(
  category="session_ledger",
  title="Pre-compact checkpoint",
  content="[current work state, next steps, key decisions]",
  tags="checkpoint,handoff"
)

# Record active intentions
chitta_mcp intend action="list"  # Note these for continuation

# End current narrative episode
chitta_mcp narrate action="end", content="Preparing for compact"
```

### Emergency (<10%)

```
# Immediate save
chitta_mcp cycle save=true

# Record critical state
chitta_mcp observe(
  category="session_ledger",
  title="EMERGENCY: Context limit reached",
  content="CRITICAL: [absolute minimum needed to continue]",
  tags="emergency,checkpoint"
)
```

Then trigger `/compact` or `/clear`.

## Handoff Checklist

Before running out of context:

1. **Save intentions**
   ```
   chitta_mcp intend action="list"
   # Record any that won't persist
   ```

2. **Record work state**
   ```
   chitta_mcp observe(
     category="session_ledger",
     title="Handoff: [what we were doing]",
     content="State: [current state]\nNext: [immediate next step]\nDecisions: [key decisions made]"
   )
   ```

3. **End the narrative**
   ```
   chitta_mcp narrate(
     action="end",
     content="Handoff prepared",
     emotion="routine"
   )
   ```

4. **Run cycle**
   ```
   chitta_mcp cycle save=true
   ```

## Recovery After Compact

After `/compact` or `/clear`, use `/resume`:

```
# This triggers pratyabhijñā - recognition of where we were
# The handoff observation will be found via recall
# Work continues with context restored from soul
```

## What This Creates

Budget awareness ensures:
- Work state survives context limits
- Handoffs are smooth
- Nothing critical is lost
- The soul provides continuity across compactions

The soul is the bridge across context boundaries.
