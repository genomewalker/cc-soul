#!/usr/bin/env python3
"""
DPO fine-tuning for SSL distiller.

Input:  contrastive JSONL: {"prompt": ..., "chosen": ..., "rejected": ...}
Output: LoRA adapter at ./ssl_lora_dpo_adapter/

Usage:
    python 02b_dpo_finetune.py --data training_data_dpo.jsonl
    sbatch 02b_dpo_finetune.slurm

Requirements:
    pip install transformers peft trl datasets accelerate
"""

import argparse
import json
import os
from pathlib import Path

import torch
from datasets import Dataset
from peft import LoraConfig, TaskType
from transformers import AutoModelForCausalLM, AutoTokenizer
from trl import DPOConfig, DPOTrainer


MODEL_ID = "Qwen/Qwen2.5-1.5B-Instruct"

SYSTEM_PROMPT = (
    "You are a memory distiller. Given a conversation, extract the key learnings "
    "in SSL v0.2 format: [TYPE] [domain] subject→action→result @location\n"
    "Types: SOLUTION, GOTCHA, DECISION, PATTERN, PREFERENCE, FAILURE\n"
    "Be concise. Use SSL arrows (→) and location refs (@file:line).\n"
    "Output ONLY SSL lines, one per line. No preamble, no explanation."
)


def load_dataset(data_path: Path, tokenizer, val_split: float = 0.1):
    pairs = []
    with open(data_path) as f:
        for line in f:
            obj = json.loads(line)
            if not obj.get("prompt") or not obj.get("chosen") or not obj.get("rejected"):
                continue
            pairs.append({
                "prompt": obj["prompt"],
                "chosen": obj["chosen"],
                "rejected": obj["rejected"],
            })

    print(f"Loaded {len(pairs)} DPO pairs")

    def apply_template(example):
        prompt_msgs = [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user",   "content": example["prompt"]},
        ]
        chosen_msgs = prompt_msgs + [{"role": "assistant", "content": example["chosen"]}]
        rejected_msgs = prompt_msgs + [{"role": "assistant", "content": example["rejected"]}]

        return {
            "prompt":   tokenizer.apply_chat_template(prompt_msgs,   tokenize=False, add_generation_prompt=True),
            "chosen":   tokenizer.apply_chat_template(chosen_msgs,   tokenize=False, add_generation_prompt=False),
            "rejected": tokenizer.apply_chat_template(rejected_msgs, tokenize=False, add_generation_prompt=False),
        }

    dataset = Dataset.from_list(pairs).map(apply_template)

    n_val = max(1, int(len(dataset) * val_split))
    split = dataset.train_test_split(test_size=n_val, seed=42)
    return split["train"], split["test"]


def train(args):
    print(f"Loading model: {args.model}")
    model = AutoModelForCausalLM.from_pretrained(
        args.model,
        torch_dtype=torch.bfloat16,
        device_map="auto",
        trust_remote_code=True,
    )
    tokenizer = AutoTokenizer.from_pretrained(args.model, trust_remote_code=True)
    if tokenizer.pad_token is None:
        tokenizer.pad_token = tokenizer.eos_token

    lora_config = LoraConfig(
        r=args.lora_r,
        lora_alpha=args.lora_alpha,
        lora_dropout=0.05,
        bias="none",
        task_type=TaskType.CAUSAL_LM,
        target_modules=["q_proj", "k_proj", "v_proj", "o_proj",
                        "gate_proj", "up_proj", "down_proj"],
    )

    train_dataset, val_dataset = load_dataset(Path(args.data), tokenizer)
    print(f"Train: {len(train_dataset)}, Val: {len(val_dataset)}")

    training_args = DPOConfig(
        output_dir=args.output_dir,
        num_train_epochs=args.epochs,
        per_device_train_batch_size=args.batch_size,
        per_device_eval_batch_size=args.batch_size,
        gradient_accumulation_steps=args.grad_accum,
        learning_rate=args.lr,
        lr_scheduler_type="cosine",
        warmup_ratio=0.05,
        bf16=torch.cuda.is_available() and torch.cuda.get_device_capability()[0] >= 8,
        gradient_checkpointing=True,
        logging_steps=10,
        eval_strategy="steps",
        eval_steps=50,
        save_strategy="steps",
        save_steps=100,
        save_total_limit=2,
        load_best_model_at_end=True,
        metric_for_best_model="eval_loss",
        report_to="none",
        beta=args.beta,
        max_length=1024,
        remove_unused_columns=False,
    )

    trainer = DPOTrainer(
        model=model,
        ref_model=None,   # implicit reference (frozen copy) — saves VRAM
        args=training_args,
        train_dataset=train_dataset,
        eval_dataset=val_dataset,
        processing_class=tokenizer,
        peft_config=lora_config,
    )

    print("\nStarting DPO training...")
    trainer.train()

    print(f"\nSaving adapter to {args.output_dir}")
    trainer.save_model(args.output_dir)
    tokenizer.save_pretrained(args.output_dir)
    print("Done.")


def main():
    parser = argparse.ArgumentParser(description="DPO fine-tune SSL distiller")
    parser.add_argument("--data",       default="training_data_dpo.jsonl")
    parser.add_argument("--model",      default=MODEL_ID)
    parser.add_argument("--output-dir", default="ssl_lora_dpo_adapter")
    parser.add_argument("--epochs",     type=int,   default=3)
    parser.add_argument("--batch-size", type=int,   default=2)
    parser.add_argument("--grad-accum", type=int,   default=8)
    parser.add_argument("--lr",         type=float, default=5e-5)
    parser.add_argument("--lora-r",     type=int,   default=16)
    parser.add_argument("--lora-alpha", type=int,   default=32)
    parser.add_argument("--beta",       type=float, default=0.1,
                        help="DPO beta — KL penalty weight (0.1=standard)")
    args = parser.parse_args()

    if not torch.cuda.is_available():
        print("WARNING: No CUDA GPU detected. DPO training will be very slow on CPU.")

    train(args)


if __name__ == "__main__":
    main()
