#!/usr/bin/env bash
# G0 wrapper: run the golden-set recall grader, then record the baseline
# score into chitta as a provenance signal.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Eval quiesce: freeze the daemon's periodic store mutation (distill,
# consolidation, ...) for the duration of the run — mid-eval distillation
# swings golden nDCG by ±0.027, swamping any small effect. The daemon
# ignores flags older than 30 min, so a crashed run can't freeze it.
quiesce="${MIND:-$HOME/.claude/mind}/.quiesce"
touch "$quiesce"
trap 'rm -f "$quiesce"' EXIT

out="$(python3 "$here/grade-recall.py" "$@")" || rc=$? || true
rc="${rc:-0}"
printf '%s\n' "$out"

score="$(printf '%s\n' "$out" | sed -n 's/^SCORE=//p' | tail -1)"
date="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

if [ -n "$score" ]; then
    chitta remember \
        --content "[done] grade-recall baseline score:$score date:$date" \
        --kind signal --realm cc-soul --tags "grader,baseline,provenance" || true
fi

exit "$rc"
