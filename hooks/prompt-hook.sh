#!/bin/bash
# Claude adapter: prompt hook -> shared core
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$SCRIPT_DIR/prompt-core.sh"
