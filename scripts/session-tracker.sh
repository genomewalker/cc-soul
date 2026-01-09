#!/bin/bash
# Session learning tracker - tracks what was learned during a session
#
# Usage:
#   session-tracker.sh start   - Initialize new session
#   session-tracker.sh track <node_id> <type> <title>  - Track a new node
#   session-tracker.sh summary - Show session summary
#   session-tracker.sh end     - End session and show summary

SESSION_FILE="${HOME}/.claude/mind/.session_learned"
SESSION_START="${HOME}/.claude/mind/.session_start"

case "$1" in
    start)
        # Initialize new session
        rm -f "$SESSION_FILE"
        date +%s > "$SESSION_START"
        echo "Session started at $(date)"
        ;;

    track)
        # Track a learned node: track <id> <type> <title>
        if [[ -n "$2" ]]; then
            echo "$2|$3|$4|$(date +%s)" >> "$SESSION_FILE"
        fi
        ;;

    summary)
        if [[ ! -f "$SESSION_FILE" ]]; then
            echo "No learning tracked this session"
            exit 0
        fi

        total=$(wc -l < "$SESSION_FILE")
        wisdom=$(grep -c "|wisdom|" "$SESSION_FILE" 2>/dev/null || echo 0)
        episodes=$(grep -c "|episode|" "$SESSION_FILE" 2>/dev/null || echo 0)
        entities=$(grep -c "|entity|" "$SESSION_FILE" 2>/dev/null || echo 0)

        echo "=== Session Learning Summary ==="
        echo "Total nodes created: $total"
        echo "  Wisdom:   $wisdom"
        echo "  Episodes: $episodes"
        echo "  Entities: $entities"
        echo ""
        echo "Recent learnings:"
        tail -5 "$SESSION_FILE" | while IFS='|' read -r id type title ts; do
            echo "  [$type] $title"
        done
        ;;

    end)
        if [[ -f "$SESSION_START" ]]; then
            start_time=$(cat "$SESSION_START")
            end_time=$(date +%s)
            duration=$((end_time - start_time))
            mins=$((duration / 60))

            echo "=== Session Complete ==="
            echo "Duration: ${mins}m"
            $0 summary

            rm -f "$SESSION_START"
        else
            $0 summary
        fi
        ;;

    *)
        echo "Usage: session-tracker.sh {start|track|summary|end}"
        exit 1
        ;;
esac
