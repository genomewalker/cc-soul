#!/bin/bash
# Realm retag: Find memories with project prefixes and add them to correct realms
#
# Memories often get stored in brahman (default realm) but contain project-specific
# content like "[chitta] ..." or "[cc-soul] ...". This script detects these patterns
# and adds the memories to the correct project realms.
#
# Usage: realm-retag.sh
# Rate limited to once per day.

set -e

MIND_PATH="${CHITTA_DB_PATH:-${HOME}/.claude/mind/chitta}"
CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
LAST_RETAG_FILE="$MIND_PATH/.last_realm_retag"
RETAG_INTERVAL="${CHITTA_RETAG_INTERVAL:-${CC_SOUL_RETAG_INTERVAL:-86400}}"  # 24 hours default
LOCK_FILE="/tmp/chitta-realm-retag.lock"

# Check chitta CLI exists
[[ ! -x "$CHITTA_BIN" ]] && exit 0

# Rate limiting: check last retag time
if [[ -f "$LAST_RETAG_FILE" ]]; then
    last_retag=$(cat "$LAST_RETAG_FILE" 2>/dev/null || echo 0)
    now=$(date +%s)
    elapsed=$((now - last_retag))

    if [[ $elapsed -lt $RETAG_INTERVAL ]]; then
        exit 0
    fi
fi

# Prevent concurrent retagging
if ! mkdir "$LOCK_FILE" 2>/dev/null; then
    exit 0
fi

trap 'rmdir "$LOCK_FILE" 2>/dev/null' EXIT

# Get list of known realms (project names)
REALMS=$("$CHITTA_BIN" realm_list 2>/dev/null | grep -oE 'name=[^ ]+' | sed 's/name=//' || true)
if [[ -z "$REALMS" ]]; then
    exit 0
fi

# Query for memories that are only in brahman but have project prefixes in content
# Pattern: memories starting with [project-name] that aren't tagged to that realm
retagged=0

for realm in $REALMS; do
    # Skip brahman
    [[ "$realm" == "brahman" ]] && continue

    # Find memories with [realm] prefix in content but realm not in their realm list
    # Query returns: id|content
    results=$("$CHITTA_BIN" sql_query --query "
        SELECT n.id, n.content
        FROM nodes n
        WHERE n.content LIKE '[${realm}]%'
        AND n.realm = 'brahman'
        AND n.id NOT IN (
            SELECT nr.node_id FROM node_realms nr WHERE nr.realm = '${realm}'
        )
        LIMIT 50
    " 2>/dev/null || true)

    if [[ -n "$results" && "$results" != *"0 rows"* ]]; then
        # Parse each row and add to realm
        while IFS='|' read -r id content; do
            [[ -z "$id" ]] && continue
            "$CHITTA_BIN" realm_add --id "$id" --realm "$realm" 2>/dev/null && ((retagged++)) || true
        done <<< "$results"
    fi
done

# Update last retag time
mkdir -p "$MIND_PATH"
date +%s > "$LAST_RETAG_FILE"

if [[ $retagged -gt 0 ]]; then
    echo "[realm-retag] Added $retagged memories to their project realms"
fi

exit 0
