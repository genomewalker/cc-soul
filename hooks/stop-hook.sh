#!/bin/bash
# Claude adapter: stop hook -> shared core
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$SCRIPT_DIR/stop-core.sh"
