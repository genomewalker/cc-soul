#!/usr/bin/env bash
# Fails if the PREFILTER block in prefilter_keep_test.cpp drifts from the one
# in field_memory_recall.cpp (the test compiles a copy, not the source).
set -e; d="$(cd "$(dirname "$0")" && pwd)"
x() { awk '/^\/\/ PREFILTER_BEGIN/{f=1} f{print} /^\/\/ PREFILTER_END/{f=0}' "$1"; }
diff <(x "$d/handlers/field_memory_recall.cpp") <(x "$d/prefilter_keep_test.cpp") && echo "prefilter block in sync"
