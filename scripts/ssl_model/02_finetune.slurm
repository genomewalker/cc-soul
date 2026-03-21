#!/bin/bash
#SBATCH --job-name=ssl-distiller
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=40G
#SBATCH --gres=gpu:1
#SBATCH --time=04:00:00
#SBATCH --output=/maps/projects/fernandezguerra/apps/repos/cc-soul/scripts/ssl_model/ssl_train_%j.log
#SBATCH --error=/maps/projects/fernandezguerra/apps/repos/cc-soul/scripts/ssl_model/ssl_train_%j.err

# Adjust partition/account as needed:
# #SBATCH --partition=gpu
# #SBATCH --account=your-account

set -e
export PYTHONUNBUFFERED=1
export HF_HUB_OFFLINE=1
export HF_HOME="${HF_HOME:-$HOME/.cache/huggingface}"

SCRIPT_DIR="/maps/projects/fernandezguerra/apps/repos/cc-soul/scripts/ssl_model"
cd "$SCRIPT_DIR"

echo "=== SSL Distiller Fine-tuning ==="
echo "Job: $SLURM_JOB_ID on $(hostname)"
echo "GPU: $(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null || echo 'unknown')"
echo "Start: $(date)"

# Activate conda env if specified (CONDA_ENV=myenv sbatch ...)
# Falls back to bioinfo env, then bare python3
CONDA_BASE="/maps/projects/fernandezguerra/apps/opt/conda"
if [[ -n "${CONDA_ENV:-}" ]]; then
    source "$CONDA_BASE/etc/profile.d/conda.sh"
    conda activate "$CONDA_ENV"
elif [[ -d "$CONDA_BASE/envs/bioinfo" ]]; then
    source "$CONDA_BASE/etc/profile.d/conda.sh"
    conda activate bioinfo
fi

# Install dependencies if not present
python3 -c "import peft" 2>/dev/null || pip install peft datasets accelerate bitsandbytes sentencepiece -q
python3 -c "import sentencepiece" 2>/dev/null || pip install sentencepiece -q

# Extract training data if not already done
if [[ ! -f "training_data.jsonl" ]]; then
    echo "Extracting training data..."
    python3 01_extract_data.py --output training_data.jsonl --strategy existing
fi

LINES=$(wc -l < training_data.jsonl)
echo "Training on $LINES examples"

# Run fine-tuning (skip if adapter already trained)
if [[ -d "ssl_lora_adapter" ]]; then
    echo "Adapter already exists, skipping training."
else
python3 02_finetune.py \
    --data training_data.jsonl \
    --model "$HOME/.cache/huggingface/hub/models--Qwen--Qwen2.5-1.5B-Instruct/snapshots/989aa7980e4cf806f80c7fef2b1adb7bc71aa306" \
    --output-dir ssl_lora_adapter \
    --epochs 3 \
    --batch-size 4 \
    --grad-accum 4 \
    --lr 2e-4 \
    --lora-r 16 \
    --lora-alpha 32 \
    --no-4bit
fi

echo "Training done: $(date)"

# Export to GGUF for llama.cpp inference
echo "Exporting to GGUF..."
bash 03_export_gguf.sh ssl_lora_adapter ssl_distiller.gguf

echo "=== All done: $(date) ==="
echo "Model: ssl_distiller.gguf"
echo "Copy to chitta bin dir with: cp ssl_distiller.gguf ~/.claude/bin/"
