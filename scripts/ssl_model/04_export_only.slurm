#!/bin/bash
#SBATCH --job-name=ssl-export
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=32G
#SBATCH --gres=gpu:1
#SBATCH --time=08:00:00
#SBATCH --output=/maps/projects/fernandezguerra/apps/repos/cc-soul/scripts/ssl_model/ssl_export_%j.log
#SBATCH --error=/maps/projects/fernandezguerra/apps/repos/cc-soul/scripts/ssl_model/ssl_export_%j.err

# Export merged model to GGUF — no GPU needed, pure CPU job.
# Merged model already at ssl_lora_adapter_merged/ (2.9G safetensors).

set -e
export PYTHONUNBUFFERED=1
export HF_HUB_OFFLINE=1

SCRIPT_DIR="/maps/projects/fernandezguerra/apps/repos/cc-soul/scripts/ssl_model"
cd "$SCRIPT_DIR"

echo "=== SSL GGUF Export ==="
echo "Job: $SLURM_JOB_ID on $(hostname)"
echo "CPUs: $SLURM_CPUS_PER_TASK"
echo "Start: $(date)"

CONDA_BASE="/maps/projects/fernandezguerra/apps/opt/conda"
if [[ -d "$CONDA_BASE/envs/bioinfo" ]]; then
    source "$CONDA_BASE/etc/profile.d/conda.sh"
    conda activate bioinfo
fi

python3 -c "import sentencepiece" 2>/dev/null || pip install sentencepiece -q

MERGED_DIR="ssl_lora_dpo_adapter_merged"
OUTPUT_GGUF="ssl_distiller_dpo.gguf"
F16_GGUF="ssl_distiller_dpo_f16.gguf"
LLAMA_DIR="llama.cpp"
CONVERT_SCRIPT="$LLAMA_DIR/convert_hf_to_gguf.py"
QUANTIZE_BIN="$LLAMA_DIR/build/bin/llama-quantize"

echo ""
echo "Step 1: Converting $MERGED_DIR → $F16_GGUF (f16)..."
echo "Start: $(date)"
python3 "$CONVERT_SCRIPT" "$MERGED_DIR" --outfile "$F16_GGUF" --outtype f16
echo "f16 done: $(date) — $(du -sh "$F16_GGUF" | cut -f1)"

echo ""
echo "Step 2: Quantizing to Q4_K_M..."
"$QUANTIZE_BIN" "$F16_GGUF" "$OUTPUT_GGUF" Q4_K_M
rm -f "$F16_GGUF"
echo "Quantized: $(du -sh "$OUTPUT_GGUF" | cut -f1)"

echo ""
echo "=== Export complete: $(date) ==="
echo "Model: $OUTPUT_GGUF"
echo "Deploy: cp $SCRIPT_DIR/$OUTPUT_GGUF ~/.claude/bin/"
