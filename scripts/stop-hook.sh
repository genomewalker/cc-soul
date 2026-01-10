#!/bin/bash
# Stop hook: Parse Claude's graph notation into relational storage
#
# Notation → Graph Structure:
#   A → B    (A)--[causes]→(B)
#   A::B     (A)--[is_a]→(B)
#   A~B      (A)--[related]→(B)
#   A?X      (A)--[uncertain:X]
#   !A       (A {important})
#   +A/-A    feedback signal
#
# Storage is relational: nodes + edges, not flat text.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

INPUT=$(cat)
TRANSCRIPT=$(echo "$INPUT" | jq -r '.transcript_path // empty')
CWD=$(echo "$INPUT" | jq -r '.cwd // empty')
STOP_ACTIVE=$(echo "$INPUT" | jq -r '.stop_hook_active // false')

[[ "$STOP_ACTIVE" == "true" ]] && echo '{}' && exit 0
[[ -z "$TRANSCRIPT" || ! -f "$TRANSCRIPT" ]] && echo '{}' && exit 0

PROJECT=$(basename "$CWD" 2>/dev/null || echo "unknown")

(
    source "$SCRIPT_DIR/lib/chitta-lib.sh"
    CHITTA="$CHITTA_PLUGIN_DIR/bin/chitta"

    LAST_MSG=$(tac "$TRANSCRIPT" | grep -m1 '"role":"assistant"' | \
        jq -r '.message.content[] | select(.type=="text") | .text' 2>/dev/null | head -c 8000)

    [[ -z "$LAST_MSG" || ${#LAST_MSG} -lt 10 ]] && exit 0

    # Parse each line for graph patterns
    echo "$LAST_MSG" | while IFS= read -r line; do
        [[ ${#line} -lt 3 ]] && continue
        [[ "$line" =~ ^[[:space:]]*$ ]] && continue
        [[ "$line" =~ ^\| ]] && continue  # Skip table rows
        [[ "$line" =~ ^\`\`\` ]] && continue  # Skip code blocks

        # Pattern: A → B (causal/implies)
        if [[ "$line" =~ ([a-zA-Z0-9._-]+)[[:space:]]*→[[:space:]]*([a-zA-Z0-9._-]+) ]]; then
            subj="${BASH_REMATCH[1]}"
            obj="${BASH_REMATCH[2]}"

            # Store as insight with relationship encoded
            "$CHITTA" observe \
                --category insight \
                --title "$subj → $obj" \
                --content "$line" \
                --project "$PROJECT" \
                --tags "relation:causes,$subj,$obj" \
                >/dev/null 2>&1 || true

            # Create edge between concepts
            "$CHITTA" connect \
                --from "$subj" \
                --to "$obj" \
                --relation "causes" \
                --weight 0.8 \
                >/dev/null 2>&1 || true
            continue
        fi

        # Pattern: A::B (is-a/definition)
        if [[ "$line" =~ ([a-zA-Z0-9._-]+)::[[:space:]]*([a-zA-Z0-9._-]+) ]]; then
            subj="${BASH_REMATCH[1]}"
            obj="${BASH_REMATCH[2]}"

            "$CHITTA" observe \
                --category insight \
                --title "$subj :: $obj" \
                --content "$line" \
                --project "$PROJECT" \
                --tags "relation:is_a,$subj,$obj" \
                >/dev/null 2>&1 || true

            "$CHITTA" connect \
                --from "$subj" \
                --to "$obj" \
                --relation "is_a" \
                --weight 0.9 \
                >/dev/null 2>&1 || true
            continue
        fi

        # Pattern: A~B (related)
        if [[ "$line" =~ ([a-zA-Z0-9._-]+)~([a-zA-Z0-9._-]+) ]]; then
            subj="${BASH_REMATCH[1]}"
            obj="${BASH_REMATCH[2]}"

            "$CHITTA" connect \
                --from "$subj" \
                --to "$obj" \
                --relation "related" \
                --weight 0.5 \
                >/dev/null 2>&1 || true
            continue
        fi

        # Pattern: !statement (important/decision)
        if [[ "$line" =~ ^[[:space:]]*!([a-zA-Z0-9._-]+) ]]; then
            stmt="${BASH_REMATCH[1]}"

            "$CHITTA" observe \
                --category decision \
                --title "!$stmt" \
                --content "$line" \
                --project "$PROJECT" \
                --tags "important" \
                >/dev/null 2>&1 || true
            continue
        fi

        # Pattern: +A (positive signal)
        if [[ "$line" =~ ^[[:space:]]*\+([a-zA-Z0-9._-]+) ]]; then
            target="${BASH_REMATCH[1]}"
            "$CHITTA" feedback --signal positive --context "$target" >/dev/null 2>&1 || true
            continue
        fi

        # Pattern: -A (negative signal)
        if [[ "$line" =~ ^[[:space:]]*-([a-zA-Z0-9._-]+) ]]; then
            target="${BASH_REMATCH[1]}"
            "$CHITTA" feedback --signal negative --context "$target" >/dev/null 2>&1 || true
            continue
        fi

        # Pattern: A?X (uncertainty/question)
        if [[ "$line" =~ ([a-zA-Z0-9._-]+)\?([a-zA-Z0-9._-]*) ]]; then
            question="${BASH_REMATCH[1]}"
            hint="${BASH_REMATCH[2]}"

            "$CHITTA" wonder --question "$question${hint:+ ($hint)}" >/dev/null 2>&1 || true
            continue
        fi

    done
) &

echo '{}'
