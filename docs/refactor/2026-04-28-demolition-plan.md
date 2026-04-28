# Demolition pass 1 — reducing surface before refactoring

Date: 2026-04-28
Branch: `refactor/demolition-pass-1`
Worktree: `/projects/caeg/scratch/kbd606/tmp/cc-soul-demolition`

## Why this exists

The user pain is "code is hard to change." Analysis of `main` showed the
root cause is breadth, not depth: 14 top-level plan markdowns, a vendored
`rtk/` (53k LOC, 297 MB) we no longer use, 41 `field_*.hpp` headers all
funnelling into one 3,156-line `field_handler.hpp`, 212 FFI functions in
one 7,500-line `ffi.rs`, and a 9:1 source-to-test imbalance.

Refactoring against that surface is a rewrite with extra steps. This
pass cuts surface — no behaviour changes — so a real refactor (pass 2)
has somewhere to land.

## Scope of this pass

In:
- Archive or remove duplicated plan markdowns at the repo root.
- Remove `rtk/` (vendored Rust submodule) and its build/hook wiring.
- Replace `rtk_rewrite()` in `hooks/pre-tool-hook.sh` (no-op or leave a
  passthrough — to decide during step).
- Decide what to do with the registered `worktree-agent-aaaffe7d`
  worktree.

Out:
- Any C++ ↔ Rust boundary changes. That's pass 2 and requires a separate
  decision (collapse C++ → Rust, or split state ownership).
- Any change to FFI surface, MCP tools, or hook behaviour.
- Test scaffolding. Comes after the boundary call.

## Steps (each step = one commit)

1. **Archive plan markdowns.** Move the redundant top-level `*.md`
   plan/proposal documents to `docs/archive/`. Keep `README.md`,
   `CHANGELOG.md`, `CLAUDE.md`, `CONTRACTS.md`, `POLICY.md`. The rest go.

2. **Pick one canonical contract.** Rename or symlink the chosen file
   so future work has a single place to anchor. (Likely `CONTRACTS.md`
   or `README.md`; decide on inspection.)

3. **Remove `rtk/` build wiring.** Strip `chitta/CMakeLists.txt:410-448`
   so a missing `rtk/` is the new normal, not a "skipping" warning.

4. **Strip `rtk_rewrite()` from `hooks/pre-tool-hook.sh`.** The user
   replaced the user-global rtk hook with sqz; this in-repo function
   is dead weight. Remove the function and its invocation site;
   leave the rest of the hook untouched.

5. **Delete `rtk/`.** Now it's safe.

6. **Verify.** `cmake --build build --parallel` (or whatever the
   project's smoke command is) must still succeed. `git status` clean.

7. **Decision point — `worktree-agent-aaaffe7d`.** Stop here. Ask the
   user whether the registered worktree is in flight. If yes — leave.
   If no — `git worktree remove` properly.

8. **Decision point — boundary call.** Stop. Pass 2 needs the user
   to decide: collapse C++ daemon into Rust core, or formalise the
   split. Don't proceed without that.

## Reversibility

Every step is one commit on a branch. `git reset --hard main` from the
worktree reverts the entire pass. Any single step is `git revert
<sha>`. The worktree itself is removable with `git worktree remove`.

## Out-of-scope follow-ups (pass 2 candidates)

- Collapse the C++/Rust boundary (whichever direction).
- Replace 41 `field_*.hpp` with one boundary header per concern (recall,
  storage, code-intel) — destination decided after the boundary call.
- Add 5–10 end-to-end tests through the MCP boundary as a refactor
  harness.
- Audit the 212 FFI functions for 1:1 pass-throughs that exist only to
  bridge layers; collapse them with the boundary.
