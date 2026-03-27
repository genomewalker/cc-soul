#!/usr/bin/env bash
# run-migration.sh — DuckDB → chitta-field one-shot bulk import via chitta_import
#
# Usage:
#   .scripts/run-migration.sh [--db-path PATH] [--field-dir PATH] [--force] [--dry-run]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

DB_PATH="${HOME}/.claude/mind/chitta.duckdb"
FIELD_DIR="${HOME}/.claude/mind/chitta-field"
FORCE=""
DRY_RUN=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --db-path)    DB_PATH="$2";  shift 2 ;;
        --field-dir)  FIELD_DIR="$2"; shift 2 ;;
        --force)      FORCE="--force"; shift ;;
        --dry-run)    DRY_RUN="--dry-run"; shift ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
done

IMPORT_BIN="${REPO_ROOT}/bin/chitta_import"
VALIDATE_SCRIPT="${SCRIPT_DIR}/validate-migration.sh"

echo "=== chitta-field migration ==="
echo "  db-path:   ${DB_PATH}"
echo "  field-dir: ${FIELD_DIR}"
[[ -n "$DRY_RUN" ]] && echo "  mode:      DRY-RUN"
[[ -n "$FORCE" ]]   && echo "  mode:      FORCE"
echo ""

if [[ ! -x "${IMPORT_BIN}" ]]; then
    echo "ERROR: chitta_import not found at ${IMPORT_BIN}" >&2
    echo "Build it: cd ${REPO_ROOT}/chitta && cmake --build build --parallel" >&2
    exit 1
fi

if [[ -x "${VALIDATE_SCRIPT}" ]]; then
    echo "--- Step 1: Pre-flight validation ---"
    set +e; bash "${VALIDATE_SCRIPT}"; PREFLIGHT_RC=$?; set -e
    if [[ "${PREFLIGHT_RC}" -ne 0 && -z "${FORCE}" ]]; then
        echo "Pre-flight validation failed. Use --force to proceed anyway."
        exit 1
    fi
    echo ""
fi

echo "--- Step 2: Import ---"
"${IMPORT_BIN}" \
    --db-path "${DB_PATH}" \
    --field-dir "${FIELD_DIR}" \
    ${FORCE} \
    ${DRY_RUN}
echo ""

if [[ -z "${DRY_RUN}" && -x "${VALIDATE_SCRIPT}" ]]; then
    echo "--- Step 3: Post-import validation ---"
    set +e; bash "${VALIDATE_SCRIPT}"; POSTCHECK_RC=$?; set -e
    echo ""
    if [[ "${POSTCHECK_RC}" -ne 0 ]]; then
        echo "WARNING: Post-import validation reported failures."
        exit 1
    fi
fi

echo "=== Migration complete ==="
if [[ -n "${DRY_RUN}" ]]; then
    echo "DRY-RUN finished — no data written. Remove --dry-run to import."
else
    echo ""
    echo "Restart the daemon:  systemctl --user restart chittad"
    echo "Verify:              ~/.claude/bin/chitta health_check"
fi
