#!/bin/bash
#SBATCH --job-name=llama-cli-build
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=16G
#SBATCH --gres=gpu:1
#SBATCH --time=01:00:00
#SBATCH --output=/maps/projects/fernandezguerra/apps/repos/cc-soul/scripts/ssl_model/llama_build_%j.log
#SBATCH --error=/maps/projects/fernandezguerra/apps/repos/cc-soul/scripts/ssl_model/llama_build_%j.err

set -e
SCRIPT_DIR="/maps/projects/fernandezguerra/apps/repos/cc-soul/scripts/ssl_model"
LLAMA_DIR="$SCRIPT_DIR/llama.cpp"

echo "=== Build llama-cli ==="
echo "Job: $SLURM_JOB_ID on $(hostname)"
echo "Start: $(date)"

cmake -S "$LLAMA_DIR" -B "$LLAMA_DIR/build" \
    -DLLAMA_CURL=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DGGML_CUDA=OFF

cmake --build "$LLAMA_DIR/build" --target llama-cli --parallel 16

echo "Built: $(ls -lh $LLAMA_DIR/build/bin/llama-cli)"

# Deploy
cp "$LLAMA_DIR/build/bin/llama-cli" ~/.claude/bin/
cp "$LLAMA_DIR/build/bin/libggml"*.so ~/.claude/bin/ 2>/dev/null || true
cp "$LLAMA_DIR/build/bin/libllama"*.so ~/.claude/bin/ 2>/dev/null || true
echo "Deployed to ~/.claude/bin/llama-cli"

echo "=== Done: $(date) ==="
