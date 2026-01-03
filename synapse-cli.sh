#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SYNAPSE_BIN="$SCRIPT_DIR/synapse/build/synapse_mcp"
MIGRATE_BIN="$SCRIPT_DIR/synapse/build/synapse_migrate"
MIND_PATH="${HOME}/.claude/mind/synapse"
SOUL_DB="${HOME}/.claude/mind/soul.db"

usage() {
    echo "Usage: synapse-cli <command> [options]"
    echo ""
    echo "Commands:"
    echo "  import     Import data from soul.db to synapse"
    echo "  test       Test MCP server connectivity"
    echo "  stats      Show soul statistics"
    echo "  recall     Semantic search"
    echo "  help       Show this help"
    echo ""
}

cmd_import() {
    local dry_run=""
    local verbose=""
    local source="$SOUL_DB"

    while [[ $# -gt 0 ]]; do
        case $1 in
            --dry-run) dry_run="--dry-run"; shift ;;
            -v|--verbose) verbose="-v"; shift ;;
            --source) source="$2"; shift 2 ;;
            *) echo "Unknown option: $1"; exit 1 ;;
        esac
    done

    if [[ ! -f "$MIGRATE_BIN" ]]; then
        echo "Error: synapse_migrate not found. Run ./setup.sh first"
        exit 1
    fi

    if [[ ! -f "$source" ]]; then
        echo "Error: Source database not found: $source"
        exit 1
    fi

    echo "Importing from: $source"
    echo "Output: $MIND_PATH"

    "$MIGRATE_BIN" --soul-db "$source" --output "$MIND_PATH" $dry_run $verbose
}

cmd_test() {
    if [[ ! -f "$SYNAPSE_BIN" ]]; then
        echo "Error: synapse_mcp not found. Run ./setup.sh first"
        exit 1
    fi

    echo "Testing MCP server..."
    echo '{"jsonrpc":"2.0","method":"tools/list","id":1}' | \
        "$SYNAPSE_BIN" --path "$MIND_PATH" 2>&1 | grep -v '^\[synapse'
}

cmd_stats() {
    if [[ ! -f "$SYNAPSE_BIN" ]]; then
        echo "Error: synapse_mcp not found. Run ./setup.sh first"
        exit 1
    fi

    echo '{"jsonrpc":"2.0","method":"tools/call","params":{"name":"soul_context","arguments":{"format":"json"}},"id":1}' | \
        "$SYNAPSE_BIN" --path "$MIND_PATH" 2>&1 | grep -v '^\[synapse'
}

cmd_recall() {
    local query="$1"
    local limit="${2:-5}"

    if [[ -z "$query" ]]; then
        echo "Usage: synapse-cli recall <query> [limit]"
        exit 1
    fi

    echo "{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"recall\",\"arguments\":{\"query\":\"$query\",\"limit\":$limit}},\"id\":1}" | \
        "$SYNAPSE_BIN" --path "$MIND_PATH" 2>&1 | grep -v '^\[synapse'
}

case "${1:-help}" in
    import) shift; cmd_import "$@" ;;
    test) cmd_test ;;
    stats) cmd_stats ;;
    recall) shift; cmd_recall "$@" ;;
    help|--help|-h) usage ;;
    *) echo "Unknown command: $1"; usage; exit 1 ;;
esac
