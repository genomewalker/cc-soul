---
name: reregister-sessions
description: Re-register every live claude CLI session with chittad's session registry (e.g. after a chittad restart wipes it)
execution: direct
---

# Reregister Sessions

```ssl
[reregister-sessions] restore cross-session messaging targets

why: session_register isn't part of the durable snapshot, so a chittad
     restart drops all registrations; each session only re-registers on
     its own next prompt

run: .scripts/reregister-sessions.sh

for each live `claude` process:
  cwd → transcript(~/.claude/projects/<encoded-cwd>/*.jsonl, newest) → session_id
  realm ← realm_detect (cwd) | brahman
  chitta session_register --session_id --realm --pid --transcript_path --project_dir

report: registered vs skipped counts, one line per session
```

## Known limitation

Two live sessions sharing the same cwd both resolve to the same
(most-recently-modified) transcript — one gets registered under the
other's session_id. Rare in practice (distinct project dirs per window).
