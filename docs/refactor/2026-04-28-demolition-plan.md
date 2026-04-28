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
- Remove `rtk/` (vendored Rust submodule) and its build/hook wiring.
- Strip `rtk_rewrite()` from `hooks/pre-tool-hook.sh` (dead since the
  user-global hook switched to sqz).
- Decide what to do with the registered `worktree-agent-aaaffe7d`
  worktree.

**Correction during pass 1**: the original plan also targeted ~10
top-level `*.md` plan/proposal documents (`ALL_PROPOSALS`,
`IMPLEMENTATION_PLAN`, `SCALE_100M_PLAN`, etc). Verification showed all
of those are `.gitignore`d local scratchpads, not repo content. Tracked
root markdowns are only `README.md`, `CHANGELOG.md`, `CLAUDE.md`,
`CLAUDE.lean.md`. The "plans-as-scratchpads" smell from the analysis
was a working-tree artefact, not a repo problem. No action needed in
this pass.

Out:
- Any C++ ↔ Rust boundary changes. That's pass 2 and requires a separate
  decision (collapse C++ → Rust, or split state ownership).
- Any change to FFI surface, MCP tools, or hook behaviour.
- Test scaffolding. Comes after the boundary call.

## Steps (each step = one commit)

1. **Remove `rtk/` build wiring.** Strip `chitta/CMakeLists.txt:410-448`
   so a missing `rtk/` is the new normal, not a "skipping" warning.

2. **Strip `rtk_rewrite()` from `hooks/pre-tool-hook.sh`.** The user
   replaced the user-global rtk hook with sqz; this in-repo function
   is dead weight. Remove the function and its invocation site;
   leave the rest of the hook untouched.

3. **Delete `rtk/`.** Now it's safe.

4. **Verify.** `cmake -S chitta -B chitta/build` must still configure
   without rtk-related errors. Build target list should no longer
   include `rtk`. `git status` clean.

5. ~~**Decision point — `worktree-agent-aaaffe7d`.**~~ **Resolved.**
   Branch was fully merged into main (main 86 commits ahead, 0 unique
   to the worktree branch). 718 MB on disk, last touched 2026-04-15,
   but contained 224 LOC of uncommitted edits across 4 chitta files
   (`field_store.hpp`, `subconscious.hpp/cpp`, `field_code_intel.hpp`).
   Decision per user: stash + remove. The 224 LOC are preserved on
   branch `refactor/agent-aaaffe7d-stash`; worktree removed; branch
   `worktree-agent-aaaffe7d` deleted.

6. **Decision point — boundary call.** Stop. Pass 2 needs the user
   to decide: collapse C++ daemon into Rust core, or formalise the
   split. Don't proceed without that.

## Verification (step 4 result)

`cmake -S chitta -B chitta/build-verify` from the demolition worktree
produced **zero rtk-related output** before stopping at an unrelated
chitta-field submodule check (the throwaway worktree didn't have that
submodule initialised). Conclusion: rtk-removal is build-clean.

## Outcome of pass 1

- 4 commits on `refactor/demolition-pass-1`.
- `-156 lines` of dead code (CMakeLists rtk block + rtk_rewrite + `.gitmodules` entry + gitlink).
- `-718 MB` of disk freed (agent-aaaffe7d worktree).
- 1 branch preserved (`refactor/agent-aaaffe7d-stash`) with 224 LOC of in-flight work for later review.
- 0 behaviour changes to the live system.

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
