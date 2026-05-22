#!/usr/bin/env bash
# Re-embed all chitta-field memories via Ollama nomic-embed-text:v1.5 (768-d).
# Runs on a GPU node where Ollama can serve the model efficiently.
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
OLLAMA_BIN="${OLLAMA_BIN:-$HOME/.local/bin/ollama}"
CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chittad}"
MODEL="nomic-embed-text:v1.5"
LOG_DIR="/maps/projects/caeg/scratch/kbd606/tmp"

echo "[reembed] Starting on $(hostname) at $(date)"
echo "[reembed] Mind path: $MIND_PATH"
echo "[reembed] Model: $MODEL"

# ── Start Ollama ─────────────────────────────────────────────────────────────
export OLLAMA_HOST="0.0.0.0"
export OLLAMA_MODELS="${OLLAMA_MODELS:-$HOME/.ollama/models}"
export CUDA_VISIBLE_DEVICES="${CUDA_VISIBLE_DEVICES:-0}"

echo "[reembed] Starting Ollama..."
"$OLLAMA_BIN" serve > "$LOG_DIR/ollama-reembed-$SLURM_JOB_ID.log" 2>&1 &
OLLAMA_PID=$!

# Wait for Ollama to be ready
for i in $(seq 1 30); do
    if curl -sf http://localhost:11434/api/tags > /dev/null 2>&1; then
        echo "[reembed] Ollama ready after ${i}s"
        break
    fi
    sleep 1
done

if ! curl -sf http://localhost:11434/api/tags > /dev/null 2>&1; then
    echo "[reembed] ERROR: Ollama did not start within 30s"
    exit 1
fi

# ── Pull model if not present ────────────────────────────────────────────────
echo "[reembed] Pulling $MODEL..."
"$OLLAMA_BIN" pull "$MODEL"
echo "[reembed] Model ready"

# ── Stop login-node daemon to avoid store lock conflict ──────────────────────
# The daemon runs on the login node; we need exclusive store access.
# It will auto-restart via systemd when we're done.
echo "[reembed] Note: ensure chittad is stopped on login node before this runs"
# (handled by submit script or manually: systemctl --user stop chittad)

# ── Run re-embed ─────────────────────────────────────────────────────────────
echo "[reembed] Running chitta re_embed..."
OLLAMA_HOST=localhost:11434 "$CHITTA_BIN" re_embed \
    --path "$MIND_PATH" \
    2>&1 | tee "$LOG_DIR/reembed-output-$SLURM_JOB_ID.log"

echo "[reembed] Done at $(date)"

# ── Cleanup ───────────────────────────────────────────────────────────────────
kill "$OLLAMA_PID" 2>/dev/null || true
echo "[reembed] Ollama stopped. Restart chittad on login node: systemctl --user start chittad"
