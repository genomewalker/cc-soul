#!/bin/bash
#SBATCH --job-name=ssl-public
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=40G
#SBATCH --gres=gpu:1
#SBATCH --time=01:30:00
#SBATCH --output=/maps/projects/fernandezguerra/apps/repos/cc-soul/scripts/ssl_model/ssl_public_%j.log
#SBATCH --error=/maps/projects/fernandezguerra/apps/repos/cc-soul/scripts/ssl_model/ssl_public_%j.err

# Train the PUBLIC ssl_distiller embedding model on the synthetic (no-personal-data)
# corpus produced by generate_synthetic_corpus.py. Distinct inputs + outputs from the
# personal pipeline so the personal ssl_distiller_dpo is never touched/clobbered.
#   in:  training_data_sft_synth.jsonl   (+ _dpo_ when STAGE=dpo)
#   out: ssl_lora_public_adapter/ , ssl_distiller_public.gguf
# Usage: STAGE=sft|dpo sbatch train_public.slurm   (default sft)

set -e
export PYTHONUNBUFFERED=1
export HF_HUB_OFFLINE=1
export HF_HOME="${HF_HOME:-$HOME/.cache/huggingface}"
export QUANT="${QUANT:-Q8_0}"          # Q8_0 for embedding-quality; Q4_K_M to shrink
STAGE="${STAGE:-sft}"

SCRIPT_DIR="/maps/projects/fernandezguerra/apps/repos/cc-soul/scripts/ssl_model"
cd "$SCRIPT_DIR"
# Load the base from its local cache snapshot dir (NOT the repo id) so transformers never
# calls the HF API — compute nodes are offline and a repo-id load triggers model_info.
BASE_MODEL=$(ls -d "$HF_HOME"/hub/models--Qwen--Qwen2.5-1.5B-Instruct/snapshots/*/ 2>/dev/null | head -1)
BASE_MODEL="${BASE_MODEL%/}"
[[ -d "$BASE_MODEL" ]] || { echo "base model not cached under $HF_HOME/hub"; exit 1; }
echo "Base model (local): $BASE_MODEL"

echo "=== Public SSL distiller — STAGE=$STAGE ==="
echo "Job: ${SLURM_JOB_ID:-local} on $(hostname)"
echo "GPU: $(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null || echo unknown)"
echo "Start: $(date)"

CONDA_BASE="/maps/projects/fernandezguerra/apps/opt/conda"
if [[ -d "$CONDA_BASE/envs/bioinfo" ]]; then
    source "$CONDA_BASE/etc/profile.d/conda.sh"
    conda activate bioinfo
fi

if [[ "$STAGE" == "dpo" ]]; then
    DATA="training_data_dpo_synth.jsonl"
    ADAPTER="ssl_lora_public_dpo_adapter"
    GGUF="ssl_distiller_public_dpo.gguf"
    [[ -f "$DATA" ]] || { echo "missing $DATA"; exit 1; }
    echo "DPO on $(wc -l < "$DATA") pairs"
    [[ -d "$ADAPTER" ]] || python3 02b_dpo_finetune.py \
        --data "$DATA" --model "$BASE_MODEL" --output-dir "$ADAPTER" \
        --epochs 3 --batch-size 2 --grad-accum 8 --lr 5e-5 \
        --lora-r 16 --lora-alpha 32 --beta 0.1
else
    DATA="training_data_sft_synth.jsonl"
    ADAPTER="ssl_lora_public_adapter"
    GGUF="ssl_distiller_public.gguf"
    [[ -f "$DATA" ]] || { echo "missing $DATA"; exit 1; }
    echo "SFT on $(wc -l < "$DATA") examples"
    [[ -d "$ADAPTER" ]] || python3 02_finetune.py \
        --data "$DATA" --model "$BASE_MODEL" --output-dir "$ADAPTER" \
        --epochs 3 --batch-size 4 --grad-accum 4 --lr 2e-4 \
        --lora-r 16 --lora-alpha 32 --no-4bit
fi

echo "Train done: $(date). Exporting GGUF ($QUANT)..."
bash 03_export_gguf.sh "$ADAPTER" "$GGUF"

echo "=== Done: $(date) ==="
echo "Model: $SCRIPT_DIR/$GGUF"
