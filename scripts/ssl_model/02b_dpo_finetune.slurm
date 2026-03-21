#!/bin/bash
#SBATCH --job-name=ssl-dpo
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=40G
#SBATCH --gres=gpu:1
#SBATCH --time=04:00:00
#SBATCH --output=/maps/projects/fernandezguerra/apps/repos/cc-soul/scripts/ssl_model/ssl_dpo_%j.log
#SBATCH --error=/maps/projects/fernandezguerra/apps/repos/cc-soul/scripts/ssl_model/ssl_dpo_%j.err

set -e
export PYTHONUNBUFFERED=1
export HF_HUB_OFFLINE=1
export HF_HOME="${HF_HOME:-$HOME/.cache/huggingface}"

SCRIPT_DIR="/maps/projects/fernandezguerra/apps/repos/cc-soul/scripts/ssl_model"
cd "$SCRIPT_DIR"

echo "=== SSL DPO Fine-tuning ==="
echo "Job: $SLURM_JOB_ID on $(hostname)"
echo "GPU: $(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null || echo 'unknown')"
echo "Start: $(date)"

CONDA_BASE="/maps/projects/fernandezguerra/apps/opt/conda"
if [[ -d "$CONDA_BASE/envs/bioinfo" ]]; then
    source "$CONDA_BASE/etc/profile.d/conda.sh"
    conda activate bioinfo
fi

python3 -c "import trl" 2>/dev/null || pip install trl -q
python3 -c "import sentencepiece" 2>/dev/null || pip install sentencepiece -q

if [[ ! -f "training_data_dpo.jsonl" ]]; then
    echo "ERROR: training_data_dpo.jsonl not found. Run on login node first:"
    echo "  python 01_extract_data.py --strategy contrastive --output training_data_dpo.jsonl"
    exit 1
fi

LINES=$(wc -l < training_data_dpo.jsonl)
echo "DPO training on $LINES pairs"

if [[ -d "ssl_lora_dpo_adapter" ]]; then
    echo "Adapter already exists, skipping training."
else
    python3 02b_dpo_finetune.py \
        --data training_data_dpo.jsonl \
        --model "$HOME/.cache/huggingface/hub/models--Qwen--Qwen2.5-1.5B-Instruct/snapshots/989aa7980e4cf806f80c7fef2b1adb7bc71aa306" \
        --output-dir ssl_lora_dpo_adapter \
        --epochs 3 \
        --batch-size 2 \
        --grad-accum 8 \
        --lr 5e-5 \
        --lora-r 16 \
        --lora-alpha 32 \
        --beta 0.1
fi

echo "Training done: $(date)"

echo "Exporting to GGUF..."
bash 03_export_gguf.sh ssl_lora_dpo_adapter ssl_distiller_dpo.gguf

echo "=== All done: $(date) ==="
echo "Model: ssl_distiller_dpo.gguf"
echo "Deploy: cp ssl_distiller_dpo.gguf ~/.claude/bin/"
