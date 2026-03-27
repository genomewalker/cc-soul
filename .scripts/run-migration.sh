#!/usr/bin/env bash
# run-migration.sh — DuckDB → chitta-field one-shot bulk import
#
# Steps:
#   1. Pre-flight validation (validate-migration.sh)
#   2. Export from DuckDB via migrate_from_duckdb.py (reads file directly)
#   3. Ingest JSONL into chitta-field via migrate binary
#   4. Post-import validation
#   5. Print FieldOnly activation instructions
#
# Usage:
#   .scripts/run-migration.sh [--db-path PATH] [--field-dir PATH] [--dry-run]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

DB_PATH="${HOME}/.claude/mind/chitta.duckdb"
FIELD_DIR="${HOME}/.claude/mind/chitta-field"
DRY_RUN=""
OUTPUT_DIR="/tmp/chitta_migration_$$"

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --db-path)    DB_PATH="$2";  shift 2 ;;
        --field-dir)  FIELD_DIR="$2"; shift 2 ;;
        --dry-run)    DRY_RUN="--dry-run"; shift ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
done

IMPORT_BIN="${REPO_ROOT}/bin/chitta_import"
VALIDATE_SCRIPT="${SCRIPT_DIR}/validate-migration.sh"

EXPORT_SCRIPT="${REPO_ROOT}/chitta-field/scripts/migrate_from_duckdb.py"
MIGRATE_BIN="${REPO_ROOT}/chitta-field/target/release/migrate"
PYTHON="${PYTHON:-python3}"

echo "=== chitta-field migration ==="
echo "  db-path:   ${DB_PATH}"
echo "  field-dir: ${FIELD_DIR}"
echo "  output:    ${OUTPUT_DIR}"
[[ -n "$DRY_RUN" ]] && echo "  mode:      DRY-RUN"
echo ""

# ── Check prerequisites ───────────────────────────────────────────────────────
if [[ ! -f "${EXPORT_SCRIPT}" ]]; then
    echo "ERROR: export script not found: ${EXPORT_SCRIPT}" >&2; exit 1
fi
if [[ ! -f "${DB_PATH}" ]]; then
    echo "ERROR: DuckDB file not found: ${DB_PATH}" >&2
    echo "Try: --db-path ~/.claude/mind/chitta.db  (or .duckdb)" >&2; exit 1
fi
if [[ -z "$DRY_RUN" && ! -x "${MIGRATE_BIN}" ]]; then
    echo "ERROR: migrate binary not found: ${MIGRATE_BIN}" >&2
    echo "Build it: cd ${REPO_ROOT}/chitta-field && ./build.sh build --release" >&2; exit 1
fi

# ── Step 1: Pre-flight validation ─────────────────────────────────────────────
VALIDATE_SCRIPT="${SCRIPT_DIR}/validate-migration.sh"
if [[ -x "${VALIDATE_SCRIPT}" ]]; then
    echo "--- Step 1: Pre-flight validation ---"
    set +e; bash "${VALIDATE_SCRIPT}"; PREFLIGHT_RC=$?; set -e
    echo ""
fi

# ── Step 2: Export DuckDB → JSONL ─────────────────────────────────────────────
echo "--- Step 2: Export DuckDB → JSONL ---"
mkdir -p "${OUTPUT_DIR}"
"${PYTHON}" "${EXPORT_SCRIPT}" \
    --db-path "${DB_PATH}" \
    --output-dir "${OUTPUT_DIR}" \
    ${DRY_RUN}
echo ""

if [[ -n "$DRY_RUN" ]]; then
    echo "DRY-RUN finished — no data written."
    exit 0
fi

# ── Step 3: Ingest JSONL → chitta-field ──────────────────────────────────────
echo "--- Step 3: Ingest into chitta-field ---"
"${MIGRATE_BIN}" \
    --memories "${OUTPUT_DIR}/memories.jsonl" \
    --triplets "${OUTPUT_DIR}/triplets.jsonl" \
    --field-dir "${FIELD_DIR}"
echo ""

# ── Step 4: Post-import validation ───────────────────────────────────────────
if [[ -x "${VALIDATE_SCRIPT}" ]]; then
    echo "--- Step 4: Post-import validation ---"
    set +e; bash "${VALIDATE_SCRIPT}"; POSTCHECK_RC=$?; set -e
    echo ""
    if [[ "${POSTCHECK_RC}" -ne 0 ]]; then
        echo "WARNING: Post-import validation reported failures."
        echo "Review the output above before activating FieldOnly mode."
        exit 1
    fi
fi

# ── Step 4: Activation instructions ──────────────────────────────────────────
echo "=== Migration complete ==="
echo ""
if [[ -n "${DRY_RUN}" ]]; then
    echo "DRY-RUN finished — no data was written."
    echo "Remove --dry-run and re-run to perform the actual import."
else
    echo "To activate FieldOnly mode (stop writing to DuckDB, read from chitta-field only):"
    echo ""
    echo "  1. Set environment variable:"
    echo "       export CHITTA_FIELD_ONLY=1"
    echo ""
    echo "  2. Or edit the daemon config to set field_only = true"
    echo ""
    echo "  3. Restart the daemon:"
    echo "       systemctl --user restart chittad"
    echo ""
    echo "  4. Verify with:"
    echo "       ~/.claude/bin/chitta health_check"
fi
