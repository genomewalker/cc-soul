---
name: resume
description: Resume a thread by loading its ~800-token context capsule
---

# Resume a thread safely across Claude and Codex

Use the shared `chitta-mcp/resume_selector.py`; do not read the global newest
transcript and do not use `.current_thread_id`. Claude and Codex may be live in
the same project on different threads, but only one live session may own a
given thread.

## Select

Resolve this plugin's `chitta-mcp` directory as described by the recap skill.
Determine the invoking client from its `CODEX_*` or `CLAUDE_*` session
environment. Run:

```bash
python3 "$_MCP_DIR/resume_selector.py" --project-dir "$PWD" --client "$CLIENT"
```

For `/resume <thread id prefix or title>`, add `--thread <argument>`. Always
show the returned live Claude/Codex sessions so the user can see parallel work
and ownership.

- On `locked`, report the owner session/client/model/thread and stop.
- On `registry_unavailable`, stop and retry after Chitta responds; do not infer
  that no session is live.
- On `ambiguous`, show the candidates and ask which thread/session to use.
- On `none`, say no resumable project-exact thread was found and offer `/tasks`.
- On `selected`, rerun the identical command with `--claim`.

Continue only if the atomic claim returns `claimed: true`. Never pass `--force`
unless the user explicitly requests takeover of a known live owner.

## Load

Use the claimed `selected.thread_id`:

```bash
python3 "$_MCP_DIR/resume_capsule.py" build --thread-id <thread_id>
```

Display the capsule's `summary` verbatim. It includes active tasks, job IDs,
branches, environments, blockers, completed work, failures, artifacts, and
next actions. Then orient to its realm, branch, environment, and active files.

If capsule construction fails, fall back to the selected exact transcript via
the recap procedure. Do not fall back to another session by recency.

The successful lease and session-to-thread binding are the durable current
context. No global marker file is required. Prompt and Stop heartbeats from
either frontend keep the lease alive; SessionEnd releases it.
