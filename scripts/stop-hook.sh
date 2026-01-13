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

    # Extract recently edited files from transcript for context tagging
    # Look for Edit tool results and file paths in recent messages
    RECENT_FILES=$(grep -oP '"file_path"\s*:\s*"\K[^"]+' "$TRANSCRIPT" 2>/dev/null | \
        tail -10 | sort -u | head -5 | tr '\n' ',' | sed 's/,$//')

    # Build file tags from recent context
    FILE_TAGS=""
    if [[ -n "$RECENT_FILES" ]]; then
        # Convert absolute paths to relative for cleaner tags
        for fpath in $(echo "$RECENT_FILES" | tr ',' '\n'); do
            fname=$(basename "$fpath")
            FILE_TAGS="${FILE_TAGS},file:${fname}"
        done
        FILE_TAGS="${FILE_TAGS#,}"  # Remove leading comma
    fi

    # Hot update: Extract key patterns from recently edited files
    # These are stored with dev:hot tag (ephemeral, fast decay)
    for fpath in $(echo "$RECENT_FILES" | tr ',' '\n'); do
        [[ ! -f "$fpath" ]] && continue
        fname=$(basename "$fpath")
        fext="${fname##*.}"

        # Extract patterns based on file type
        case "$fext" in
            hpp|h|cpp|c|cc)
                # C/C++: Extract class/struct definitions and key function signatures
                # Class/struct definitions
                grep -oP '(class|struct)\s+\K[A-Z][a-zA-Z0-9_]+' "$fpath" 2>/dev/null | head -10 | while read -r name; do
                    "$CHITTA" observe \
                        --category signal \
                        --title "$name defined in $fname" \
                        --content "[$PROJECT] $name :: $fext type @$fname" \
                        --project "$PROJECT" \
                        --tags "dev:hot,file:$fname,type:$fext" \
                        >/dev/null 2>&1 || true
                done

                # Key function signatures (public methods, main functions)
                grep -oP '^\s*(inline\s+)?(static\s+)?\w+\s+\K[a-z][a-zA-Z0-9_]+(?=\s*\()' "$fpath" 2>/dev/null | \
                    grep -v '^if$\|^for$\|^while$\|^switch$' | head -10 | while read -r func; do
                    "$CHITTA" observe \
                        --category signal \
                        --title "$func() in $fname" \
                        --content "[$PROJECT] function $func @$fname" \
                        --project "$PROJECT" \
                        --tags "dev:hot,file:$fname,func:$func" \
                        >/dev/null 2>&1 || true
                done
                ;;

            py)
                # Python: Extract class and function definitions
                grep -oP '^class\s+\K[A-Z][a-zA-Z0-9_]+' "$fpath" 2>/dev/null | head -10 | while read -r name; do
                    "$CHITTA" observe \
                        --category signal \
                        --title "$name defined in $fname" \
                        --content "[$PROJECT] $name :: python class @$fname" \
                        --project "$PROJECT" \
                        --tags "dev:hot,file:$fname,type:py" \
                        >/dev/null 2>&1 || true
                done

                grep -oP '^def\s+\K[a-z_][a-zA-Z0-9_]+' "$fpath" 2>/dev/null | head -10 | while read -r func; do
                    "$CHITTA" observe \
                        --category signal \
                        --title "$func() in $fname" \
                        --content "[$PROJECT] function $func @$fname" \
                        --project "$PROJECT" \
                        --tags "dev:hot,file:$fname,func:$func" \
                        >/dev/null 2>&1 || true
                done
                ;;

            sh|bash)
                # Shell: Extract function definitions
                grep -oP '^[a-z_][a-zA-Z0-9_]+(?=\s*\(\))' "$fpath" 2>/dev/null | head -10 | while read -r func; do
                    "$CHITTA" observe \
                        --category signal \
                        --title "$func() in $fname" \
                        --content "[$PROJECT] shell function $func @$fname" \
                        --project "$PROJECT" \
                        --tags "dev:hot,file:$fname,func:$func" \
                        >/dev/null 2>&1 || true
                done
                ;;
        esac
    done

    # Extract [LEARN ε=XX] markers and store as wisdom
    echo "$LAST_MSG" | grep -oP '\[LEARN[^\]]*\]\s*\K[^\n]+' | while read -r learning; do
        [[ -z "$learning" ]] && continue

        # Extract epsilon if present
        EPSILON=$(echo "$LAST_MSG" | grep -oP '\[LEARN ε=\K[0-9]+' | head -1)
        EPSILON=${EPSILON:-50}
        EPSILON_FLOAT=$(echo "scale=2; $EPSILON / 100" | bc 2>/dev/null || echo "0.5")

        # Parse title and content
        if [[ "$learning" == *": "* ]]; then
            TITLE="${learning%%: *}"
            CONTENT="${learning#*: }"
        else
            TITLE="${learning:0:60}"
            CONTENT="$learning"
        fi

        # Add file tags if we have file context
        local tags_arg=""
        [[ -n "$FILE_TAGS" ]] && tags_arg="--tags $FILE_TAGS"

        "$CHITTA" grow --type wisdom --title "$TITLE" --content "$CONTENT" --epsilon "$EPSILON_FLOAT" $tags_arg >/dev/null 2>&1 || true
    done

    # Extract [USED:uuid] markers and strengthen those memories
    echo "$LAST_MSG" | grep -oP '\[USED:[^\]]+\]' | while read -r marker; do
        UUID="${marker#\[USED:}"
        UUID="${UUID%\]}"
        [[ -z "$UUID" || ${#UUID} -lt 30 ]] && continue

        "$CHITTA" feedback --memory_id "$UUID" --helpful true --context "Used in response" >/dev/null 2>&1 || true
    done

    # Extract [REMEMBER] markers and store as observations
    echo "$LAST_MSG" | grep -oP '\[REMEMBER\]\s*\K[^\n]+' | while read -r memory; do
        [[ -z "$memory" ]] && continue

        if [[ "$memory" == *": "* ]]; then
            TITLE="${memory%%: *}"
            CONTENT="${memory#*: }"
        else
            TITLE="${memory:0:60}"
            CONTENT="$memory"
        fi

        # Add file tags if we have file context
        local tags_arg=""
        [[ -n "$FILE_TAGS" ]] && tags_arg="--tags $FILE_TAGS"

        "$CHITTA" observe --category decision --title "$TITLE" --content "$CONTENT" --project "$PROJECT" $tags_arg >/dev/null 2>&1 || true
    done

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
