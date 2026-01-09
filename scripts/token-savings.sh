#!/bin/bash
# Token savings estimator for cc-soul
#
# Estimates tokens saved by:
# 1. Recall hits (vs re-reading files)
# 2. Codemap cache hits (vs re-scanning)
# 3. Transparent memory injection (vs exploration)
#
# Usage: token-savings.sh [report|reset]

SAVINGS_FILE="${HOME}/.claude/mind/.token_savings"
CHARS_PER_TOKEN=4  # Rough estimate

# Initialize if missing
if [[ ! -f "$SAVINGS_FILE" ]]; then
    echo '{"recall_hits":0,"recall_chars":0,"cache_hits":0,"cache_chars":0,"transparent_hits":0,"transparent_chars":0}' > "$SAVINGS_FILE"
fi

case "$1" in
    add-recall)
        # $2 = chars saved by recall (vs re-reading file)
        chars="${2:-0}"
        jq ".recall_hits += 1 | .recall_chars += $chars" "$SAVINGS_FILE" > "${SAVINGS_FILE}.tmp" && mv "${SAVINGS_FILE}.tmp" "$SAVINGS_FILE"
        ;;

    add-cache)
        # $2 = chars saved by cache hit
        chars="${2:-0}"
        jq ".cache_hits += 1 | .cache_chars += $chars" "$SAVINGS_FILE" > "${SAVINGS_FILE}.tmp" && mv "${SAVINGS_FILE}.tmp" "$SAVINGS_FILE"
        ;;

    add-transparent)
        # $2 = chars injected via transparent memory
        chars="${2:-0}"
        jq ".transparent_hits += 1 | .transparent_chars += $chars" "$SAVINGS_FILE" > "${SAVINGS_FILE}.tmp" && mv "${SAVINGS_FILE}.tmp" "$SAVINGS_FILE"
        ;;

    report)
        if [[ ! -f "$SAVINGS_FILE" ]]; then
            echo "No token savings data collected yet"
            exit 0
        fi

        data=$(cat "$SAVINGS_FILE")
        recall_hits=$(echo "$data" | jq -r '.recall_hits')
        recall_chars=$(echo "$data" | jq -r '.recall_chars')
        cache_hits=$(echo "$data" | jq -r '.cache_hits')
        cache_chars=$(echo "$data" | jq -r '.cache_chars')
        transparent_hits=$(echo "$data" | jq -r '.transparent_hits')
        transparent_chars=$(echo "$data" | jq -r '.transparent_chars')

        total_chars=$((recall_chars + cache_chars + transparent_chars))
        total_tokens=$((total_chars / CHARS_PER_TOKEN))

        echo "=== CC-Soul Token Savings Report ==="
        echo ""
        echo "Recall (avoided file re-reads):"
        echo "  Hits: $recall_hits"
        echo "  Chars saved: $recall_chars (~$((recall_chars / CHARS_PER_TOKEN)) tokens)"
        echo ""
        echo "Codemap cache (avoided re-scans):"
        echo "  Hits: $cache_hits"
        echo "  Chars saved: $cache_chars (~$((cache_chars / CHARS_PER_TOKEN)) tokens)"
        echo ""
        echo "Transparent memory (injected context):"
        echo "  Injections: $transparent_hits"
        echo "  Chars injected: $transparent_chars (~$((transparent_chars / CHARS_PER_TOKEN)) tokens)"
        echo ""
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo "TOTAL ESTIMATED SAVINGS: ~$total_tokens tokens"
        echo ""
        echo "Note: Actual savings depend on what would have been"
        echo "loaded without memory (files, exploration, etc.)"
        ;;

    reset)
        echo '{"recall_hits":0,"recall_chars":0,"cache_hits":0,"cache_chars":0,"transparent_hits":0,"transparent_chars":0}' > "$SAVINGS_FILE"
        echo "Token savings reset"
        ;;

    json)
        cat "$SAVINGS_FILE"
        ;;

    *)
        echo "Usage: token-savings.sh {add-recall|add-cache|add-transparent|report|reset|json} [chars]"
        exit 1
        ;;
esac
