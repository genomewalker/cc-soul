#!/usr/bin/env python3
"""
Fine-tune a small model for SSL distillation using LoRA.

Model: Qwen/Qwen2.5-1.5B-Instruct (or 0.5B for speed)
Method: LoRA (r=16, alpha=32) on seq2seq-style instruction tuning
Output: LoRA adapter saved to ./ssl_lora_adapter/

GPU node usage:
    sbatch 02_finetune.slurm     # if using SLURM
    python 02_finetune.py        # direct GPU run

Requirements:
    pip install transformers peft datasets accelerate bitsandbytes
"""

import argparse
import json
import os
import sys
from pathlib import Path

import torch
from datasets import Dataset
from peft import LoraConfig, TaskType, get_peft_model
from transformers import (
    AutoModelForCausalLM,
    AutoTokenizer,
    Trainer,
    TrainingArguments,
)


# ── Config ────────────────────────────────────────────────────────────────────

MODEL_ID = "Qwen/Qwen2.5-1.5B-Instruct"
MAX_INPUT_LEN = 768
MAX_OUTPUT_LEN = 256
MAX_SEQ_LEN = MAX_INPUT_LEN + MAX_OUTPUT_LEN

SYSTEM_PROMPT = (
    "You are a memory distiller. Given a conversation, extract the key learnings "
    "in SSL v0.2 format: [TYPE] [domain] subject→action→result @location\n"
    "Types: SOLUTION, GOTCHA, DECISION, PATTERN, PREFERENCE, FAILURE\n"
    "Be concise. Use SSL arrows (→) and location refs (@file:line)."
)


# ── Data loading ──────────────────────────────────────────────────────────────

def load_dataset(data_path: Path, tokenizer, val_split: float = 0.1):
    pairs = []
    with open(data_path) as f:
        for line in f:
            obj = json.loads(line)
            if not obj.get("input") or not obj.get("output"):
                continue
            pairs.append({"input": obj["input"], "output": obj["output"]})

    print(f"Loaded {len(pairs)} training pairs")

    # Format as chat template
    def format_example(example):
        messages = [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": example["input"]},
            {"role": "assistant", "content": example["output"]},
        ]
        text = tokenizer.apply_chat_template(
            messages, tokenize=False, add_generation_prompt=False)
        return {"text": text}

    dataset = Dataset.from_list(pairs).map(format_example)

    # Tokenize
    def tokenize(example):
        enc = tokenizer(
            example["text"],
            truncation=True,
            max_length=MAX_SEQ_LEN,
            padding=False,
        )
        enc["labels"] = enc["input_ids"].copy()
        # Mask the system+user portion so loss only applies to assistant output
        # Find the assistant turn start by looking for generation prompt
        try:
            gen_prompt = tokenizer.apply_chat_template(
                [{"role": "system", "content": SYSTEM_PROMPT},
                 {"role": "user", "content": example["input"]}],
                tokenize=False, add_generation_prompt=True)
            n_prompt_tokens = min(
                len(tokenizer(gen_prompt, add_special_tokens=False)["input_ids"]),
                len(enc["labels"])
            )
            enc["labels"][:n_prompt_tokens] = [-100] * n_prompt_tokens
        except Exception:
            pass
        return enc

    tokenized = dataset.map(tokenize, remove_columns=dataset.column_names)

    n_val = max(1, int(len(tokenized) * val_split))
    split = tokenized.train_test_split(test_size=n_val, seed=42)
    return split["train"], split["test"]


# ── Model setup ──────────────────────────────────────────────────────────────

def load_model(model_id: str, use_4bit: bool = True):
    print(f"Loading model: {model_id}")

    if use_4bit:
        from transformers import BitsAndBytesConfig
        bnb_config = BitsAndBytesConfig(
            load_in_4bit=True,
            bnb_4bit_use_double_quant=True,
            bnb_4bit_quant_type="nf4",
            bnb_4bit_compute_dtype=torch.bfloat16,
        )
        model = AutoModelForCausalLM.from_pretrained(
            model_id, quantization_config=bnb_config,
            device_map="auto", trust_remote_code=True)
    else:
        model = AutoModelForCausalLM.from_pretrained(
            model_id, torch_dtype=torch.bfloat16,
            device_map="auto", trust_remote_code=True)

    tokenizer = AutoTokenizer.from_pretrained(model_id, trust_remote_code=True)
    if tokenizer.pad_token is None:
        tokenizer.pad_token = tokenizer.eos_token

    return model, tokenizer


def apply_lora(model, r: int = 16, alpha: int = 32, dropout: float = 0.05):
    lora_config = LoraConfig(
        r=r,
        lora_alpha=alpha,
        lora_dropout=dropout,
        bias="none",
        task_type=TaskType.CAUSAL_LM,
        target_modules=["q_proj", "k_proj", "v_proj", "o_proj",
                        "gate_proj", "up_proj", "down_proj"],
    )
    model = get_peft_model(model, lora_config)
    model.enable_input_require_grads()  # Required for gradient checkpointing + LoRA
    model.print_trainable_parameters()
    return model


# ── Training ──────────────────────────────────────────────────────────────────

def train(args):
    model, tokenizer = load_model(args.model, use_4bit=not args.no_4bit)
    model = apply_lora(model, r=args.lora_r, alpha=args.lora_alpha)

    train_dataset, val_dataset = load_dataset(
        Path(args.data), tokenizer, val_split=0.1)
    print(f"Train: {len(train_dataset)}, Val: {len(val_dataset)}")

    training_args = TrainingArguments(
        output_dir=args.output_dir,
        num_train_epochs=args.epochs,
        per_device_train_batch_size=args.batch_size,
        per_device_eval_batch_size=args.batch_size,
        gradient_accumulation_steps=args.grad_accum,
        learning_rate=args.lr,
        lr_scheduler_type="cosine",
        warmup_ratio=0.05,
        fp16=False,
        bf16=torch.cuda.is_available() and torch.cuda.get_device_capability()[0] >= 8,
        gradient_checkpointing=True,
        logging_steps=10,
        eval_strategy="steps",
        eval_steps=100,
        save_strategy="steps",
        save_steps=200,
        save_total_limit=3,
        load_best_model_at_end=True,
        metric_for_best_model="eval_loss",
        report_to="none",
        dataloader_num_workers=2,
        remove_unused_columns=False,
    )

    pad_id = tokenizer.pad_token_id

    def collate_fn(examples):
        # Pad input_ids and labels to the same length within the batch
        max_len = max(len(e["input_ids"]) for e in examples)
        input_ids, attention_mask, labels = [], [], []
        for e in examples:
            pad = max_len - len(e["input_ids"])
            input_ids.append(e["input_ids"] + [pad_id] * pad)
            attention_mask.append(e["attention_mask"] + [0] * pad)
            labels.append(e["labels"] + [-100] * pad)
        return {
            "input_ids": torch.tensor(input_ids),
            "attention_mask": torch.tensor(attention_mask),
            "labels": torch.tensor(labels),
        }

    trainer = Trainer(
        model=model,
        args=training_args,
        train_dataset=train_dataset,
        eval_dataset=val_dataset,
        data_collator=collate_fn,
    )

    print("\nStarting training...")
    trainer.train()

    print(f"\nSaving adapter to {args.output_dir}")
    model.save_pretrained(args.output_dir)
    tokenizer.save_pretrained(args.output_dir)
    print("Done.")


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Fine-tune SSL distiller")
    parser.add_argument("--data", default="training_data.jsonl")
    parser.add_argument("--model", default=MODEL_ID)
    parser.add_argument("--output-dir", default="ssl_lora_adapter")
    parser.add_argument("--epochs", type=int, default=3)
    parser.add_argument("--batch-size", type=int, default=2)
    parser.add_argument("--grad-accum", type=int, default=8)
    parser.add_argument("--lr", type=float, default=2e-4)
    parser.add_argument("--lora-r", type=int, default=16)
    parser.add_argument("--lora-alpha", type=int, default=32)
    parser.add_argument("--no-4bit", action="store_true",
                        help="Disable 4-bit quantization (for full precision training)")

    args = parser.parse_args()

    if not torch.cuda.is_available():
        print("WARNING: No CUDA GPU detected. Training will be very slow on CPU.")
        print("Use the SLURM script to submit to a GPU node.")

    train(args)


if __name__ == "__main__":
    main()
