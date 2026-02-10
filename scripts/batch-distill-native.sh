#!/bin/bash
# batch-distill-native.sh - Process pending transcripts using native C++ distiller
#
# Usage: ./scripts/batch-distill-native.sh [--dry-run] [--limit N] [--min-size BYTES]

set -e

CHITTAD="${CHITTAD:-$HOME/.claude/bin/chittad}"
DRY_RUN=false
LIMIT=0
MIN_SIZE=10000  # 10KB minimum

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dry-run) DRY_RUN=true; shift ;;
        --limit) LIMIT="$2"; shift 2 ;;
        --min-size) MIN_SIZE="$2"; shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

echo "[batch-distill] Stopping daemon..."
"$CHITTAD" shutdown 2>/dev/null || true
sleep 2

# Find transcripts with user content
echo "[batch-distill] Scanning for transcripts..."
TRANSCRIPTS=()
for dir in ~/.claude/projects/*/; do
    realm=$(basename "$dir" | sed 's/^-//' | tr '-' '/')
    for f in "$dir"*.jsonl; do
        [[ -f "$f" ]] || continue
        size=$(stat -c%s "$f" 2>/dev/null || stat -f%z "$f" 2>/dev/null)
        [[ $size -lt $MIN_SIZE ]] && continue
        grep -q '"type":"user"' "$f" 2>/dev/null || continue
        TRANSCRIPTS+=("$f|$realm")
    done
done

echo "[batch-distill] Found ${#TRANSCRIPTS[@]} transcripts >= ${MIN_SIZE} bytes"

# Process each
COUNT=0
SUCCESS=0
FAILED=0

for entry in "${TRANSCRIPTS[@]}"; do
    IFS='|' read -r path realm <<< "$entry"
    session_id=$(basename "$path" .jsonl)

    ((COUNT++))
    [[ $LIMIT -gt 0 && $COUNT -gt $LIMIT ]] && break

    size=$(stat -c%s "$path" 2>/dev/null || stat -f%z "$path" 2>/dev/null)
    size_mb=$(echo "scale=1; $size/1048576" | bc)

    echo ""
    echo "[batch-distill] [$COUNT/${#TRANSCRIPTS[@]}] $session_id (${size_mb}MB)"
    echo "  Path: $path"
    echo "  Realm: $realm"

    if $DRY_RUN; then
        echo "  [DRY-RUN] Would distill"
        continue
    fi

    if "$CHITTAD" distill \
        --transcript-path "$path" \
        --session-id "$session_id" \
        --realm "$realm" \
        --distill-min-turns 2 2>&1 | grep -E '^\[distill\]'; then
        ((SUCCESS++))
    else
        ((FAILED++))
        echo "  [FAILED]"
    fi
done

echo ""
echo "[batch-distill] Complete: $SUCCESS success, $FAILED failed"

# Restart daemon
echo "[batch-distill] Restarting daemon..."
"$CHITTAD" daemon
