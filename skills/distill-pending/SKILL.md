---
name: distill-pending
description: Triggers and monitors distillation of raw session transcripts into concise, structured memory notes using the chitta daemon. Extracts key insights from conversation transcripts, stores them as tagged learnings, and reports distillation status. Use when the user asks to summarize transcripts, distill session notes, process pending transcriptions, check distillation progress, extract insights from meeting or conversation logs, or troubleshoot why automatic transcript processing is not running.
execution: inline
---

# Distill Pending Sessions

Trigger distillation of pending transcripts and check distillation status.

## Check Distillation Status

View registered transcripts and their distillation state:

```bash
chitta transcript_list
```

This shows:
- `session_id`: Transcript identifier
- `last_processed_line`: How many lines have been distilled
- `distilled`: Whether any distillation has occurred
- `realm`: Project context

## Manual Trigger

To manually trigger distillation, first obtain the current session ID, then queue it for processing:

```bash
# Get the current session ID
SESSION_ID=$(chitta transcript_list | grep -m1 'session_id' | awk '{print $2}')

# Add to queue for daemon processing
echo '{"tool":"distill_trigger","args":{"session_id":"'"$SESSION_ID"'"},"ts":'$(date +%s)'}' >> /tmp/chitta-queue.jsonl
```

### Verify the trigger was queued

After queuing, confirm distillation was picked up by re-checking the transcript list:

```bash
chitta transcript_list
```

Look for an updated `last_processed_line` value or `distilled: true` compared to before. If the values are unchanged after ~30 seconds, proceed to troubleshooting.

## Configuration

Daemon distillation settings (in `chittad --help`):
- `--distill-interval MINS`: Check interval (default: 5)
- `--distill-min-turns N`: Min turns before distilling (default: 4)
- `--distill-model MODEL`: LLM model for extraction
- `--no-distill`: Disable automatic distillation

Run `chittad --help` for the full list of available options and defaults.

## Troubleshooting

If distillation isn't running, work through these steps in order:

1. **Check daemon is running**: `chitta health_check`
   - Expected: status OK with uptime reported. If not, restart the daemon.

2. **Check GPU/LLM endpoint is reachable**: `curl -s http://localhost:11434/v1/models`
   - Expected: JSON list of available models. If empty or connection refused, the LLM backend is down.

3. **Check transcript is registered**: `chitta transcript_list`
   - Expected: at least one entry with a valid `session_id`. If missing, the transcript was not registered on session start.

4. **Check queue is being processed**: `ls -la /tmp/chitta-queue.jsonl*`
   - Expected: the main `.jsonl` file exists and a `.lock` file is absent (lock file means processing is in progress). A stale `.lock` file may indicate a crashed daemon run.

If all steps pass but distillation still does not occur, consult the daemon logs for detailed error output.
