#!/bin/bash
# Batch distill all large transcripts that haven't been processed
#
# Usage: ./scripts/batch-distill.sh [--dry-run] [--min-size 10M]

set -euo pipefail

MIN_SIZE="${MIN_SIZE:-10M}"
DRY_RUN=false
DISTILL_SCRIPT="${DISTILL_SCRIPT:-$(dirname "$0")/../hooks/distill.sh}"
MODEL="${MODEL:-github-copilot/gpt-5-mini}"
CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"

# Parse args
while [[ $# -gt 0 ]]; do
    case $1 in
        --dry-run) DRY_RUN=true; shift ;;
        --min-size) MIN_SIZE="$2"; shift 2 ;;
        --model) MODEL="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [--dry-run] [--min-size SIZE] [--model MODEL]"
            echo ""
            echo "Options:"
            echo "  --dry-run     Show what would be distilled without running"
            echo "  --min-size    Minimum file size (default: 10M)"
            echo "  --model       OpenCode model (default: github-copilot/gpt-5-mini)"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

echo "=== Batch Transcript Distillation ==="
echo "Min size: $MIN_SIZE | Model: $MODEL | Dry run: $DRY_RUN"
echo ""

# Find large transcripts (excluding subagents)
TRANSCRIPTS=$(find ~/.claude/projects -name "*.jsonl" ! -path "*/subagents/*" -size +${MIN_SIZE} 2>/dev/null | sort -r)

if [[ -z "$TRANSCRIPTS" ]]; then
    echo "No transcripts larger than $MIN_SIZE found."
    exit 0
fi

COUNT=0
SUCCESS=0
FAILED=0
TOTAL=$(echo "$TRANSCRIPTS" | wc -l)

for transcript in $TRANSCRIPTS; do
    ((++COUNT))
    session_id=$(basename "$transcript" .jsonl)
    project=$(dirname "$transcript" | xargs basename)
    size=$(du -h "$transcript" | cut -f1)

    # Detect realm from project path
    if [[ "$project" == -* ]]; then
        # Convert -maps-projects-foo-bar to project:foo/bar
        realm_path=$(echo "$project" | sed 's/^-//' | tr '-' '/')
        realm="project:$realm_path"
    else
        realm="brahman"
    fi

    echo "[$COUNT/$TOTAL] $size $project"
    echo "  Session: $session_id"
    echo "  Realm: $realm"

    if $DRY_RUN; then
        echo "  [DRY RUN] Would distill..."
        echo ""
        continue
    fi

    # Create temp file for distill.sh
    TEMP_FILE=$(mktemp /tmp/distill-batch-XXXXXX)

    # Write header
    cat > "$TEMP_FILE" << EOF
SESSION_ID=$session_id
REALM=$realm
MODEL=$MODEL
---
EOF

    # Extract conversation (user and assistant messages, text only)
    jq -r '
        select(.type == "user" or .type == "assistant") |
        "[" + .type + "]\n" + (
            if .message.content | type == "string" then
                .message.content
            elif .message.content | type == "array" then
                [.message.content[] | select(.type == "text") | .text] | join("\n")
            else
                ""
            end
        )
    ' "$transcript" 2>/dev/null >> "$TEMP_FILE" || {
        echo "  [ERROR] Failed to parse transcript"
        rm -f "$TEMP_FILE"
        ((++FAILED))
        continue
    }

    # Check content size
    CONTENT_SIZE=$(wc -c < "$TEMP_FILE")
    if [[ $CONTENT_SIZE -lt 1000 ]]; then
        echo "  [SKIP] Too little content ($CONTENT_SIZE bytes)"
        rm -f "$TEMP_FILE"
        continue
    fi

    echo "  Content: $(numfmt --to=iec $CONTENT_SIZE 2>/dev/null || echo "$CONTENT_SIZE bytes")"

    # Run distillation
    echo "  Distilling..."
    if OUTPUT=$(bash "$DISTILL_SCRIPT" "$TEMP_FILE" 2>&1); then
        # Count memories created (distill.sh outputs +solution:, +gotcha:, etc.)
        MEM_COUNT=$(echo "$OUTPUT" | grep -cE '^\[distill\]\s+\+' || echo "0")
        echo "$OUTPUT" | grep -E '^\[distill\]' | tail -5
        echo "  [OK] Created $MEM_COUNT memories"
        ((++SUCCESS))
    else
        echo "  [WARN] Distillation may have failed"
        echo "$OUTPUT" | head -5
        ((++FAILED))
    fi

    rm -f "$TEMP_FILE"
    echo ""

    # Delay between sessions to avoid rate limits
    sleep 3
done

echo "=== Summary ==="
echo "Total: $COUNT | Success: $SUCCESS | Failed: $FAILED"
