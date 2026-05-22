#!/usr/bin/env python3
"""
Convert hint corpus JSONL ({"input": ..., "output": ...}) to ShareGPT ChatML format
for unsloth fine-tuning with Qwen3.

Input row:
    {"input": "<text>", "output": "User <fact>." | "-"}

Output row (ShareGPT):
    {"conversations": [
        {"from": "system",    "value": "<SYSTEM_PROMPT>"},
        {"from": "human",     "value": "<text>"},
        {"from": "gpt",       "value": "User <fact>." | "-"}
    ]}

Usage:
    python3 convert_to_chatml.py --in corpus.jsonl --out corpus_chatml.jsonl
    python3 convert_to_chatml.py --in corpus.jsonl --out corpus_chatml.jsonl --split 0.1
"""

import argparse
import json
import os
import random
import sys

SYSTEM_PROMPT = (
    "You extract personal facts from conversation excerpts. "
    "Given a message or conversation, output a single concise third-person sentence "
    "about the user (e.g. \"User lives in Copenhagen.\", \"User has two cats.\"). "
    "If no stable personal fact is present, output exactly: -"
)


def convert(row: dict) -> dict:
    return {
        "conversations": [
            {"from": "system", "value": SYSTEM_PROMPT},
            {"from": "human",  "value": row["input"].strip()},
            {"from": "gpt",    "value": row["output"].strip()},
        ]
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--in",   dest="inp", required=True, help="Input JSONL path")
    parser.add_argument("--out",  required=True, help="Output JSONL path (train set)")
    parser.add_argument("--split", type=float, default=0.0,
                        help="Fraction to hold out as eval set (0 = no split)")
    parser.add_argument("--seed",  type=int, default=42)
    args = parser.parse_args()

    random.seed(args.seed)

    rows = []
    with open(args.inp) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError as e:
                sys.stderr.write(f"[convert] skip bad line: {e}\n")
                continue
            if "input" not in row or "output" not in row:
                sys.stderr.write(f"[convert] skip row missing keys: {line[:80]}\n")
                continue
            rows.append(convert(row))

    random.shuffle(rows)

    if args.split > 0:
        n_eval = max(1, int(len(rows) * args.split))
        eval_rows = rows[:n_eval]
        train_rows = rows[n_eval:]
        eval_path = args.out.replace(".jsonl", "_eval.jsonl")
        _write(eval_rows, eval_path)
        sys.stderr.write(f"[convert] eval  → {eval_path} ({len(eval_rows)} rows)\n")
    else:
        train_rows = rows

    _write(train_rows, args.out)
    sys.stderr.write(f"[convert] train → {args.out} ({len(train_rows)} rows)\n")
    print(json.dumps({"train": len(train_rows), "out": args.out}))


def _write(rows: list, path: str):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w") as f:
        for row in rows:
            f.write(json.dumps(row, ensure_ascii=False) + "\n")


if __name__ == "__main__":
    main()
