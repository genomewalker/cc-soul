#!/usr/bin/env bash
# Re-embed all chitta-field memories with nomic-embed-text-v1.5 (768-d) via
# llama.cpp in-process GGUF on a GPU node.
#
# Usage:
#   sbatch scripts/reembed_ollama.sh
#   # or with custom mind path:
#   MIND_PATH=/path/to/mind sbatch scripts/reembed_ollama.sh

#SBATCH --job-name=chitta-reembed
#SBATCH --partition=compregular
#SBATCH --account=fernandezguerra
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --gres=gpu:a100:1
#SBATCH --time=02:00:00
#SBATCH --output=/maps/projects/caeg/scratch/kbd606/tmp/reembed-%j.log
#SBATCH --error=/maps/projects/caeg/scratch/kbd606/tmp/reembed-%j.log

set -euo pipefail

MIND_PATH="${MIND_PATH:-$HOME/.claude/mind}"
CHITTA_BIN="${CHITTA_BIN:-/maps/projects/caeg/scratch/kbd606/tmp/chittad-768-cuda}"
EMBED_MODEL="${EMBED_MODEL:-$HOME/.claude/models/nomic-embed-text-v1.5.gguf}"
LOG_DIR="/maps/projects/caeg/scratch/kbd606/tmp"

echo "[reembed] Starting on $(hostname) at $(date)"
echo "[reembed] Mind path:   $MIND_PATH"
echo "[reembed] Embed model: $EMBED_MODEL"
echo "[reembed] Binary:      $CHITTA_BIN"

# CUDA runtime — required for llama.cpp GPU offload
module load cuda/12.8 2>/dev/null || true
export CUDA_VISIBLE_DEVICES="${CUDA_VISIBLE_DEVICES:-0}"
export LD_LIBRARY_PATH="/opt/software/cuda/12.8/lib64:${LD_LIBRARY_PATH:-}"
echo "[reembed] CUDA_VISIBLE_DEVICES=$CUDA_VISIBLE_DEVICES"
nvidia-smi -L 2>/dev/null || echo "[reembed] WARNING: no GPU visible"

# ── Stop login-node daemon to avoid store lock conflict ──────────────────────
# The daemon runs on the login node; SLURM nodes can't run systemctl --user.
# Stop it before submitting this job:
#   systemctl --user stop chittad
#   sbatch scripts/reembed_ollama.sh
#   systemctl --user start chittad   # after job completes
echo "[reembed] Note: ensure chittad is stopped on login node before this job runs"

# ── Run re-embed (in-process GGUF, GPU-accelerated via llama.cpp CUDA) ──────
echo "[reembed] Running chittad re_embed..."
"$CHITTA_BIN" re_embed \
    --path "$MIND_PATH" \
    --embed-model "$EMBED_MODEL" \
    2>&1 | tee "$LOG_DIR/reembed-output-$SLURM_JOB_ID.log"

echo "[reembed] Done at $(date)"
echo "[reembed] Restart chittad on login node: systemctl --user start chittad"
