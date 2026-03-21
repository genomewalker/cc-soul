#!/bin/bash
# Build gradmemd on a node with CUDA 12.x (GPU node or login node with cuda/12 module).
# Requires: CUDA 12.x, conda bioinfo env (PyTorch 2.9.1+cu128), cmake 3.14+
#
# Usage: bash build_gradmemd.sh
# Output: ~/.claude/bin/gradmemd
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CC_SOUL_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
CHITTA_DIR="$CC_SOUL_DIR/chitta"
LLAMA_DIR="$CC_SOUL_DIR/scripts/ssl_model/llama.cpp"

CONDA_BASE="/maps/projects/fernandezguerra/apps/opt/conda"
source "$CONDA_BASE/etc/profile.d/conda.sh"
conda activate bioinfo

TORCH_PATH="$CONDA_PREFIX/lib/python3.12/site-packages/torch"
echo "libtorch: $TORCH_PATH"
echo "CUDA: $(nvcc --version 2>/dev/null | head -1 || echo 'not found')"

cmake -S "$CHITTA_DIR" -B "$CHITTA_DIR/build" \
    -DCHITTA_WITH_TORCH=ON \
    -DCMAKE_PREFIX_PATH="$TORCH_PATH" \
    -DLLAMA_CPP_DIR="$LLAMA_DIR" \
    2>&1 | grep -E "torch|gradmem|llama|Error|error|CUDA" | grep -v Warning

cmake --build "$CHITTA_DIR/build" --target gradmemd --parallel 8 2>&1 | tail -5

BINARY="$CC_SOUL_DIR/bin/gradmemd"
if [[ -f "$BINARY" ]]; then
    cp "$BINARY" ~/.claude/bin/gradmemd
    echo "Deployed: ~/.claude/bin/gradmemd ($(du -sh ~/.claude/bin/gradmemd | cut -f1))"
else
    echo "ERROR: $BINARY not found after build"
    exit 1
fi
