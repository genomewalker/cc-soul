#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
export CHITTA_RPC_PORT="${CHITTA_RPC_PORT:-7432}"

exec "${CONDA_EXE:-conda}" run --no-capture-output \
    -n "${CHITTA_MCP_CONDA_ENV:-bioinfo}" python3 "$repo_root/server.py" "$@"
