#!/bin/bash
# migrate-ssl-v03.sh — Migrate existing memories from SSL v0.2 (or legacy) to SSL v0.3
#
# What it does:
#   1. Scans all memories for format version (v0.3, v0.2, legacy)
#   2. For v0.2 memories: adds default affect values based on category
#   3. Detects structurally significant memories and adds F: flags
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

# ── Helper: send JSON-RPC to daemon via pipe ──────────────────────────────────

rpc_call() {
    local tool="$1" args="$2"
    echo "{\"method\":\"tools/call\",\"params\":{\"name\":\"$tool\",\"arguments\":$args},\"id\":1}" \
        | "$CHITTA_BIN" 2>/dev/null
}

# ── Default affect values by category ─────────────────────────────────────────

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

# ── Detect structural flags from content ──────────────────────────────────────

detect_flags() {
    local content="$1"
    local flags=""

    if echo "$content" | grep -qiE '(first|birth|started|created|invented|initial|began|introduced)'; then
        flags="ORIGIN"
    fi

    if echo "$content" | grep -qiE '(changed|switched|migrated|replaced|moved from|pivoted|reversed|abandoned)'; then
        [[ -n "$flags" ]] && flags="$flags,"
        flags="${flags}PIVOT"
    fi

    if echo "$content" | grep -qiE '(always|fundamental|essential|critical|must|foundation|core|cornerstone)'; then
        [[ -n "$flags" ]] && flags="$flags,"
        flags="${flags}CORE"
    fi

    if echo "$content" | grep -qiE '(breakthrough|finally|eureka|realized|key insight|turning point|game.changer)'; then
        [[ -n "$flags" ]] && flags="$flags,"
        flags="${flags}TURNING"
    fi

    echo "$flags"
}

# ── Detect format version of a memory ─────────────────────────────────────────

detect_version() {
    local content="$1"
    if echo "$content" | grep -qP 'A:[+-]?[0-9.]+,[0-9.]+'; then
        echo "v0.3"
    elif echo "$content" | grep -qF '→'; then
        echo "v0.2"
    else
        echo "legacy"
    fi
}

# ── Main ──────────────────────────────────────────────────────────────────────

echo "=== SSL v0.3 Migration ==="
echo "Mode: $([ "$DRY_RUN" = true ] && echo 'DRY RUN' || echo 'LIVE')"
[[ -n "$REALM_FILTER" ]] && echo "Realm: $REALM_FILTER"
[[ "$LIMIT" -gt 0 ]] && echo "Limit: $LIMIT"
echo ""

STATS_V03=0
STATS_V02=0
STATS_LEGACY=0
STATS_MIGRATED=0
STATS_FLAGGED=0
STATS_SKIPPED=0
STATS_ERRORS=0
PROCESSED=0

# Memory kinds to process (from hygiene_stats)
KINDS=("wisdom" "insight" "episode" "signal" "milestone" "habit" "claim" "goal" "research_event")
BATCH_SIZE=500

for kind in "${KINDS[@]}"; do
    echo "--- Scanning kind: $kind ---"
    offset=0
    kind_total=0

    while true; do
        # List memory IDs via JSON-RPC with pagination
        raw=$(rpc_call "list_memories_brief" "{\"kind\":\"$kind\",\"limit\":$BATCH_SIZE,\"offset\":$offset}")
        if [[ -z "$raw" ]]; then
            break
        fi

        # Extract memory IDs from structured response
        ids=$(echo "$raw" | python3 -c "
import json, sys
try:
    d = json.load(sys.stdin)
    mems = d.get('result',{}).get('structured',{}).get('memories',[]) or []
    for m in mems:
        print(m['id'])
except Exception:
    pass
" 2>/dev/null)

        if [[ -z "$ids" ]]; then
            break
        fi

        batch_count=$(echo "$ids" | wc -l)
        kind_total=$((kind_total + batch_count))
        echo "  Batch at offset $offset: $batch_count memories (total $kind_total $kind)"

    while IFS= read -r mem_id; do
        [[ -z "$mem_id" ]] && continue

        # Check limit
        if [[ "$LIMIT" -gt 0 && "$PROCESSED" -ge "$LIMIT" ]]; then
            echo "  Hit limit ($LIMIT), stopping."
            break 2
        fi

        # Get memory content (default text mode; --text-only strips too aggressively)
        content=$("$CHITTA_BIN" get --id "$mem_id" 2>/dev/null || echo "")
        if [[ -z "$content" ]]; then
            ((STATS_SKIPPED++)) || true
            continue
        fi

        # Realm filter
        if [[ -n "$REALM_FILTER" ]]; then
            mem_realm=$("$CHITTA_BIN" get --id "$mem_id" --json 2>/dev/null | python3 -c "import json,sys; print(json.load(sys.stdin).get('result',{}).get('structured',{}).get('realm',''))" 2>/dev/null || echo "")
            if [[ "$mem_realm" != "$REALM_FILTER" ]]; then
                ((STATS_SKIPPED++)) || true
                continue
            fi
        fi

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

        # Determine affect values — detect category from content if possible
        category="$kind"
        if echo "$content" | grep -qiP '^\[SOLUTION\]'; then category="solution"
        elif echo "$content" | grep -qiP '^\[GOTCHA\]'; then category="gotcha"
        elif echo "$content" | grep -qiP '^\[DECISION\]'; then category="decision"
        elif echo "$content" | grep -qiP '^\[PATTERN\]'; then category="pattern"
        elif echo "$content" | grep -qiP '^\[PREFERENCE\]'; then category="preference"
        elif echo "$content" | grep -qiP '^\[FAILURE\]'; then category="failure"
        elif echo "$content" | grep -qiP '^\[CORRECTION\]'; then category="correction"
        fi

        affect=$(default_affect "$category")
        valence="${affect%,*}"
        arousal="${affect#*,}"

        # Detect structural flags
        flags=$(detect_flags "$content")

        if [[ "$DRY_RUN" == "true" ]]; then
            echo "  #$mem_id [$kind→$category] ($version) → A:$affect${flags:+ F:$flags}"
        else
            # Set affect via JSON-RPC
            rpc_call "set_affect" "{\"id\":\"$mem_id\",\"valence\":$valence,\"arousal\":$arousal}" >/dev/null 2>&1
            ((STATS_MIGRATED++)) || true

            # Add structural flags as triplets
            if [[ -n "$flags" ]]; then
                IFS=',' read -ra flag_arr <<< "$flags"
                for flag in "${flag_arr[@]}"; do
                    "$CHITTA_BIN" connect --subject "$mem_id" --predicate "has_flag" --object "$flag" >/dev/null 2>&1 || true
                done
                ((STATS_FLAGGED++)) || true
            fi

            echo "  + #$mem_id [$kind→$category] A:$affect${flags:+ F:$flags}"
        fi

        ((PROCESSED++)) || true

        # Progress every 100
        if [[ $((PROCESSED % 100)) -eq 0 ]]; then
            echo "  ... processed $PROCESSED memories so far"
        fi

    done <<< "$ids"

        # Next page
        offset=$((offset + BATCH_SIZE))
        if [[ "$batch_count" -lt "$BATCH_SIZE" ]]; then
            break  # last page
        fi

        # Check limit
        if [[ "$LIMIT" -gt 0 && "$PROCESSED" -ge "$LIMIT" ]]; then
            break
        fi
    done  # pagination loop
done

echo ""
echo "=== Migration Summary ==="
echo "Already v0.3:  $STATS_V03"
echo "Was v0.2:      $STATS_V02"
echo "Was legacy:    $STATS_LEGACY"
echo "Migrated:      $STATS_MIGRATED"
echo "Flagged:       $STATS_FLAGGED"
echo "Skipped:       $STATS_SKIPPED"
echo "Errors:        $STATS_ERRORS"
echo "Total scanned: $PROCESSED"

if [[ "$DRY_RUN" == "true" ]]; then
    echo ""
    echo "Re-run without --dry-run to apply changes."
fi
