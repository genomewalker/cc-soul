#!/bin/bash
# Export LoRA adapter to GGUF for llama.cpp inference.
#
# Usage:
#   bash 03_export_gguf.sh <adapter_dir> <output_gguf>
#
# Example:
#   bash 03_export_gguf.sh ssl_lora_adapter ssl_distiller.gguf
#
# Requirements:
#   pip install transformers peft
#   git clone https://github.com/ggerganov/llama.cpp (for convert_hf_to_gguf.py)
#   make -C llama.cpp quantize  (optional, for quantization)

set -e

ADAPTER_DIR="${1:-ssl_lora_adapter}"
OUTPUT_GGUF="${2:-ssl_distiller.gguf}"
MERGED_DIR="${ADAPTER_DIR}_merged"
QUANT="${QUANT:-Q4_K_M}"  # quantization type
export ADAPTER_DIR MERGED_DIR OUTPUT_GGUF QUANT

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Export LoRA → GGUF ==="
echo "Adapter:  $ADAPTER_DIR"
echo "Output:   $OUTPUT_GGUF"
echo "Quant:    $QUANT"

# ── Step 1: Merge LoRA into base model ────────────────────────────────────────

if [[ -d "$MERGED_DIR" ]]; then
    echo "Merged model already exists at $MERGED_DIR — skipping merge."
else
    echo ""
    echo "Step 1: Merging LoRA adapter into base model..."
    python3 - <<'PYEOF'
import sys, os
sys.path.insert(0, os.path.dirname(__file__))

from pathlib import Path
import torch
from peft import PeftModel
from transformers import AutoModelForCausalLM, AutoTokenizer

adapter_dir = os.environ.get("ADAPTER_DIR", "ssl_lora_adapter")
merged_dir = os.environ.get("MERGED_DIR", "ssl_lora_adapter_merged")

# Read base model ID from adapter config
import json
config_path = Path(adapter_dir) / "adapter_config.json"
with open(config_path) as f:
    config = json.load(f)
base_model_id = config["base_model_name_or_path"]
print(f"  Base model: {base_model_id}")

print("  Loading base model...")
model = AutoModelForCausalLM.from_pretrained(
    base_model_id,
    torch_dtype=torch.float16,
    device_map="cpu",
    trust_remote_code=True,
)

print("  Loading LoRA adapter...")
model = PeftModel.from_pretrained(model, adapter_dir)

print("  Merging weights...")
model = model.merge_and_unload()

print(f"  Saving merged model → {merged_dir}")
model.save_pretrained(merged_dir)

tokenizer = AutoTokenizer.from_pretrained(adapter_dir, trust_remote_code=True)
tokenizer.save_pretrained(merged_dir)

print("  Merge complete.")
PYEOF
fi

# ── Step 2: Find llama.cpp convert script ─────────────────────────────────────

echo ""
echo "Step 2: Locating llama.cpp converter..."

CONVERT_SCRIPT=""
LLAMA_CPP_SEARCH_DIRS=(
    "$HOME/llama.cpp"
    "$HOME/src/llama.cpp"
    "/opt/llama.cpp"
    "$(pwd)/llama.cpp"
)

for d in "${LLAMA_CPP_SEARCH_DIRS[@]}"; do
    if [[ -f "$d/convert_hf_to_gguf.py" ]]; then
        CONVERT_SCRIPT="$d/convert_hf_to_gguf.py"
        LLAMA_CPP_DIR="$d"
        break
    fi
done

if [[ -z "$CONVERT_SCRIPT" ]]; then
    echo "  llama.cpp not found. Cloning..."
    git clone --depth 1 https://github.com/ggerganov/llama.cpp llama.cpp
    CONVERT_SCRIPT="$(pwd)/llama.cpp/convert_hf_to_gguf.py"
    LLAMA_CPP_DIR="$(pwd)/llama.cpp"
    # Build quantizer
    echo "  Building llama.cpp quantize tool..."
    make -C "$LLAMA_CPP_DIR" quantize -j4 2>/dev/null || \
        cmake -S "$LLAMA_CPP_DIR" -B "$LLAMA_CPP_DIR/build" -DLLAMA_CURL=OFF && \
        cmake --build "$LLAMA_CPP_DIR/build" --target llama-quantize --parallel
fi

echo "  Using: $CONVERT_SCRIPT"

# ── Step 3: Convert to GGUF (f16) ─────────────────────────────────────────────

F16_GGUF="${OUTPUT_GGUF%.gguf}_f16.gguf"

echo ""
echo "Step 3: Converting to GGUF (f16)..."
python3 "$CONVERT_SCRIPT" \
    "$MERGED_DIR" \
    --outfile "$F16_GGUF" \
    --outtype f16

echo "  f16 GGUF: $F16_GGUF ($(du -sh "$F16_GGUF" | cut -f1))"

# ── Step 4: Quantize to Q4_K_M ────────────────────────────────────────────────

echo ""
echo "Step 4: Quantizing to $QUANT..."

# Find the quantize binary
QUANTIZE_BIN=""
for candidate in \
    "$LLAMA_CPP_DIR/build/bin/llama-quantize" \
    "$LLAMA_CPP_DIR/llama-quantize" \
    "$LLAMA_CPP_DIR/quantize"; do
    if [[ -x "$candidate" ]]; then
        QUANTIZE_BIN="$candidate"
        break
    fi
done

if [[ -z "$QUANTIZE_BIN" ]]; then
    echo "  WARNING: quantize binary not found — keeping f16 GGUF as final output."
    mv "$F16_GGUF" "$OUTPUT_GGUF"
else
    "$QUANTIZE_BIN" "$F16_GGUF" "$OUTPUT_GGUF" "$QUANT"
    rm -f "$F16_GGUF"
    echo "  Quantized: $OUTPUT_GGUF ($(du -sh "$OUTPUT_GGUF" | cut -f1))"
fi

echo ""
echo "=== Export complete ==="
echo "Model: $OUTPUT_GGUF"
echo ""
echo "Test with:"
echo "  llama-cli -m $OUTPUT_GGUF -p 'USER: summarize this\n\nASSISTANT:' -n 200"
echo ""
echo "Deploy with:"
echo "  cp $OUTPUT_GGUF ~/.claude/bin/"
