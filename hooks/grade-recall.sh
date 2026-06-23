#!/usr/bin/env bash
# G0 wrapper: run the golden-set recall grader, then record the baseline
# score into chitta as a provenance signal.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
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
