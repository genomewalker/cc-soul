#!/bin/bash
# migrate-ssl-v03.sh — Migrate existing memories from SSL v0.2 (or legacy) to SSL v0.3
#
# What it does:
#   1. Scans all memories for format version (v0.3, v0.2, legacy)
#   2. For v0.2 memories: adds default affect values based on category
#   3. Detects structurally significant memories and adds F: flags
#   4. Tags migrated memories as ssl-v0.3
#
# Usage:
#   ./scripts/migrate-ssl-v03.sh [--dry-run] [--limit N] [--realm REALM]

set -euo pipefail

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
DRY_RUN=false
LIMIT=0
REALM_FILTER=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dry-run) DRY_RUN=true; shift ;;
        --limit) LIMIT="$2"; shift 2 ;;
        --realm) REALM_FILTER="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [--dry-run] [--limit N] [--realm REALM]"
            echo ""
            echo "Migrates SSL v0.2 memories to v0.3 format by adding:"
            echo "  - A:v,a affect annotations (default values based on category)"
            echo "  - F:FLAG structural flags (detected from content patterns)"
            echo "  - ssl-v0.3 tag"
            echo ""
            echo "Options:"
            echo "  --dry-run   Show what would be changed without modifying anything"
            echo "  --limit N   Process at most N memories (0 = unlimited)"
            echo "  --realm R   Only process memories in realm R"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

if ! command -v "$CHITTA_BIN" &>/dev/null; then
    echo "Error: chitta binary not found at $CHITTA_BIN" >&2
    exit 1
fi

if ! "$CHITTA_BIN" health_check &>/dev/null; then
    echo "Error: chitta daemon not running" >&2
    exit 1
fi

# Default affect values by category
default_affect() {
    local category="$1"
    case "$category" in
        solution)    echo "+0.5,0.4" ;;
        gotcha)      echo "-0.3,0.5" ;;
        decision)    echo "+0.3,0.3" ;;
        pattern)     echo "+0.3,0.2" ;;
        preference)  echo "+0.2,0.1" ;;
        failure)     echo "-0.4,0.5" ;;
        correction)  echo "+0.1,0.3" ;;
        wisdom)      echo "+0.2,0.2" ;;
        episode)     echo "0.0,0.2" ;;
        *)           echo "0.0,0.1" ;;
    esac
}

# Detect structural flags from content
detect_flags() {
    local content="$1"
    local flags=""

    # ORIGIN: first mention, birth, started, created, invented
    if echo "$content" | grep -qiE '(first|birth|started|created|invented|initial|began|introduced)'; then
        flags="ORIGIN"
    fi

    # PIVOT: changed, switched, migrated, replaced, moved from/to
    if echo "$content" | grep -qiE '(changed|switched|migrated|replaced|moved from|pivoted|reversed|abandoned)'; then
        [[ -n "$flags" ]] && flags="$flags,"
        flags="${flags}PIVOT"
    fi

    # CORE: always, fundamental, essential, critical, must, foundation
    if echo "$content" | grep -qiE '(always|fundamental|essential|critical|must|foundation|core|cornerstone)'; then
        [[ -n "$flags" ]] && flags="$flags,"
        flags="${flags}CORE"
    fi

    # TURNING: breakthrough, finally, eureka, realized, key insight
    if echo "$content" | grep -qiE '(breakthrough|finally|eureka|realized|key insight|turning point|game.changer)'; then
        [[ -n "$flags" ]] && flags="$flags,"
        flags="${flags}TURNING"
    fi

    echo "$flags"
}

# Detect format version of a memory
detect_version() {
    local content="$1"
    # v0.3: has A:v,a annotation
    if echo "$content" | grep -qP 'A:[+-]?[0-9.]+,[0-9.]+'; then
        echo "v0.3"
    # v0.2: has → arrows but no A: annotation
    elif echo "$content" | grep -qF '→'; then
        echo "v0.2"
    # legacy: prose
    else
        echo "legacy"
    fi
}

echo "=== SSL v0.3 Migration ==="
echo "Mode: $([ "$DRY_RUN" = true ] && echo 'DRY RUN' || echo 'LIVE')"
[[ -n "$REALM_FILTER" ]] && echo "Realm: $REALM_FILTER"
[[ "$LIMIT" -gt 0 ]] && echo "Limit: $LIMIT"
echo ""

# Get all memory IDs with their categories
QUERY="SELECT id, kind, realm FROM memory_state WHERE deleted = 0"
[[ -n "$REALM_FILTER" ]] && QUERY="$QUERY AND realm = '$REALM_FILTER'"
QUERY="$QUERY ORDER BY id"
[[ "$LIMIT" -gt 0 ]] && QUERY="$QUERY LIMIT $LIMIT"

STATS_V03=0
STATS_V02=0
STATS_LEGACY=0
STATS_MIGRATED=0
STATS_FLAGGED=0
STATS_SKIPPED=0
STATS_ERRORS=0

# Process each memory
while IFS='|' read -r mem_id kind realm; do
    [[ -z "$mem_id" ]] && continue
    [[ "$mem_id" =~ ^[^0-9] ]] && continue  # skip header rows

    # Get memory content
    content=$("$CHITTA_BIN" get --id "$mem_id" --text-only 2>/dev/null || echo "")
    [[ -z "$content" ]] && { ((STATS_SKIPPED++)) || true; continue; }

    version=$(detect_version "$content")

    case "$version" in
        v0.3)
            ((STATS_V03++)) || true
            continue
            ;;
        v0.2)
            ((STATS_V02++)) || true
            ;;
        legacy)
            ((STATS_LEGACY++)) || true
            ;;
    esac

    # Already tagged as ssl-v0.3? Skip
    if "$CHITTA_BIN" query --subject "$mem_id" --predicate "tagged" --object "ssl-v0.3" 2>/dev/null | grep -q "ssl-v0.3"; then
        ((STATS_SKIPPED++)) || true
        continue
    fi

    # Determine affect values
    affect=$(default_affect "$kind")
    valence="${affect%,*}"
    arousal="${affect#*,}"

    # Detect structural flags
    flags=$(detect_flags "$content")

    if [[ "$DRY_RUN" == "true" ]]; then
        echo "  #$mem_id [$kind] ($version) → A:$affect${flags:+ F:$flags}"
    else
        # Set affect via queue (daemon routes to tool_set_affect)
        QUEUE_FILE="${CHITTA_QUEUE:-/tmp/chitta-queue.jsonl}"
        echo "{\"tool\":\"set_affect\",\"args\":{\"id\":\"$mem_id\",\"valence\":$valence,\"arousal\":$arousal},\"ts\":$(date +%s)}" >> "$QUEUE_FILE"
        ((STATS_MIGRATED++)) || true

        # Add structural flags as triplets
        if [[ -n "$flags" ]]; then
            IFS=',' read -ra flag_arr <<< "$flags"
            for flag in "${flag_arr[@]}"; do
                echo "{\"tool\":\"connect\",\"args\":{\"subject\":\"$mem_id\",\"predicate\":\"has_flag\",\"object\":\"$flag\"},\"ts\":$(date +%s)}" >> "$QUEUE_FILE"
            done
            ((STATS_FLAGGED++)) || true
        fi

        # Tag as migrated
        echo "{\"tool\":\"tag\",\"args\":{\"id\":\"$mem_id\",\"add\":\"ssl-v0.3\"},\"ts\":$(date +%s)}" >> "$QUEUE_FILE"

        echo "  + #$mem_id [$kind] A:$affect${flags:+ F:$flags}"
    fi

done < <("$CHITTA_BIN" sql_query --query "$QUERY" --text-only 2>/dev/null || echo "")

echo ""
echo "=== Migration Summary ==="
echo "Already v0.3:  $STATS_V03"
echo "Was v0.2:      $STATS_V02"
echo "Was legacy:    $STATS_LEGACY"
echo "Migrated:      $STATS_MIGRATED"
echo "Flagged:       $STATS_FLAGGED"
echo "Skipped:       $STATS_SKIPPED"
echo "Errors:        $STATS_ERRORS"

if [[ "$DRY_RUN" == "true" ]]; then
    echo ""
    echo "Re-run without --dry-run to apply changes."
fi
