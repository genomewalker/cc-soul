#!/usr/bin/env bash
# Fine-tune Qwen3-0.6B on hint corpus with unsloth (4-bit QLoRA), then export GGUF.
#
# Usage:
#   bash finetune_hint_qwen.sh [--data PATH] [--out-dir PATH] [--steps N] [--dry-run]
#
# Requirements:
#   pip install unsloth[colab-new] xformers trl peft accelerate bitsandbytes
#   llama.cpp (for quantisation) — expects llama-quantize in PATH or LLAMA_QUANTIZE env
#
# Outputs:
#   $OUT_DIR/          — merged safetensors (fp16)
#   $GGUF_DIR/chitta-hint-qwen-f16.gguf
#   $GGUF_DIR/chitta-hint-qwen-q4_k_m.gguf

set -euo pipefail

# ---------------------------------------------------------------------------
# Defaults (override via env or flags)
# ---------------------------------------------------------------------------
DATA="${CHITTA_HINT_DATA:-/maps/projects/caeg/scratch/kbd606/tmp/hint_corpus_chatml.jsonl}"
OUT_DIR="${CHITTA_HINT_MODEL_DIR:-/maps/projects/caeg/scratch/kbd606/tmp/hint_qwen_out}"
GGUF_DIR="${CHITTA_HINT_GGUF_DIR:-/maps/projects/caeg/scratch/kbd606/tmp/hint_qwen_gguf}"
STEPS="${CHITTA_HINT_STEPS:-200}"
BATCH="${CHITTA_HINT_BATCH:-4}"
BASE_MODEL="Qwen/Qwen3-0.6B"
DRY_RUN=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --data)    DATA="$2";    shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --steps)   STEPS="$2";  shift 2 ;;
        --dry-run) DRY_RUN=1;   shift   ;;
        *) echo "Unknown arg: $1" >&2; exit 1 ;;
    esac
done

echo "[finetune] base=$BASE_MODEL  data=$DATA  out=$OUT_DIR  steps=$STEPS"

if [[ $DRY_RUN -eq 1 ]]; then
    echo "[finetune] --dry-run: skipping actual training"
    exit 0
fi

mkdir -p "$OUT_DIR" "$GGUF_DIR"

# ---------------------------------------------------------------------------
# Step 1: fine-tune with unsloth
# ---------------------------------------------------------------------------
python3 - <<PYEOF
import os, json, torch
from datasets import Dataset
from unsloth import FastLanguageModel
from unsloth.chat_templates import get_chat_template
from trl import SFTTrainer
from transformers import TrainingArguments

BASE_MODEL = "$BASE_MODEL"
DATA_PATH  = "$DATA"
OUT_DIR    = "$OUT_DIR"
MAX_SEQ    = 512
STEPS      = int("$STEPS")
BATCH      = int("$BATCH")

print(f"[finetune] loading {BASE_MODEL}...")
model, tokenizer = FastLanguageModel.from_pretrained(
    model_name=BASE_MODEL,
    max_seq_length=MAX_SEQ,
    load_in_4bit=True,
    dtype=None,
)

model = FastLanguageModel.get_peft_model(
    model,
    r=16, lora_alpha=32, lora_dropout=0.05,
    target_modules=["q_proj", "k_proj", "v_proj", "o_proj",
                    "gate_proj", "up_proj", "down_proj"],
    bias="none", use_gradient_checkpointing="unsloth",
)

tokenizer = get_chat_template(tokenizer, chat_template="qwen-2.5")

# Load ShareGPT corpus
rows = [json.loads(l) for l in open(DATA_PATH) if l.strip()]

def format_row(row):
    if "text" in row:
        return row["text"]
    return tokenizer.apply_chat_template(
        row["conversations"], tokenize=False, add_generation_prompt=False
    )

texts = [format_row(r) for r in rows]
dataset = Dataset.from_dict({"text": texts})
print(f"[finetune] {len(dataset)} training examples")

trainer = SFTTrainer(
    model=model,
    tokenizer=tokenizer,
    train_dataset=dataset,
    dataset_text_field="text",
    max_seq_length=MAX_SEQ,
    args=TrainingArguments(
        per_device_train_batch_size=BATCH,
        gradient_accumulation_steps=4,
        num_train_epochs=1,
        max_steps=STEPS,
        warmup_steps=max(10, STEPS // 20),
        learning_rate=2e-4,
        fp16=not torch.cuda.is_bf16_supported(),
        bf16=torch.cuda.is_bf16_supported(),
        logging_steps=10,
        output_dir=OUT_DIR,
        save_strategy="no",
        report_to="none",
    ),
    dataset_num_proc=2,
    packing=False,
)

print("[finetune] training...")
trainer.train()

print(f"[finetune] saving merged model to {OUT_DIR}...")
model.save_pretrained_merged(OUT_DIR, tokenizer, save_method="merged_16bit")
print("[finetune] done")
PYEOF

# ---------------------------------------------------------------------------
# Step 2: convert merged safetensors → GGUF (F16), then quantise Q4_K_M
# ---------------------------------------------------------------------------
LLAMA_QUANTIZE="${LLAMA_QUANTIZE:-$(command -v llama-quantize 2>/dev/null || echo '')}"
LLAMA_CONVERT="${LLAMA_CONVERT:-$(command -v llama-convert-hf-to-gguf 2>/dev/null || python3 -c "import llama_cpp; print(llama_cpp.__file__.replace('__init__.py','../convert_hf_to_gguf.py'))" 2>/dev/null || echo '')}"

F16_GGUF="${GGUF_DIR}/chitta-hint-qwen-f16.gguf"
Q4_GGUF="${GGUF_DIR}/chitta-hint-qwen-q4_k_m.gguf"

if [[ -n "$LLAMA_CONVERT" && -f "$LLAMA_CONVERT" ]]; then
    echo "[finetune] converting to F16 GGUF..."
    python3 "$LLAMA_CONVERT" "$OUT_DIR" --outfile "$F16_GGUF" --outtype f16
    echo "[finetune] F16 GGUF: $F16_GGUF"

    if [[ -n "$LLAMA_QUANTIZE" ]]; then
        echo "[finetune] quantising Q4_K_M..."
        "$LLAMA_QUANTIZE" "$F16_GGUF" "$Q4_GGUF" Q4_K_M
        echo "[finetune] Q4_K_M GGUF: $Q4_GGUF"
        ls -lh "$Q4_GGUF"
    else
        echo "[finetune] llama-quantize not found — skipping Q4_K_M (set LLAMA_QUANTIZE)"
    fi
else
    echo "[finetune] llama-convert not found — skipping GGUF export"
    echo "  Set LLAMA_CONVERT to path of convert_hf_to_gguf.py from llama.cpp"
    echo "  Merged safetensors are in: $OUT_DIR"
fi

echo "[finetune] complete"
echo "  merged : $OUT_DIR"
echo "  f16    : $F16_GGUF"
echo "  q4_k_m : $Q4_GGUF"
