#!/usr/bin/env bash
# Deploy chitta-hint-tuned: merge LoRA adapter into llama3.2:3b and register with Ollama.
# Requirements: Ollama running, llama3.2:3b pulled, Python with transformers + peft.
# Run once per install (or after an adapter update).
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADAPTER_DIR="${SCRIPT_DIR}/chitta-hint-adapter"
SCRATCH="${TMPDIR:-/tmp}/chitta-hint-merge-$$"
MODELFILE="${SCRIPT_DIR}/Modelfile.chitta-hint-tuned"

# Discover Ollama endpoint
OLLAMA_URL=""
for f in /tmp/ollama-server-*.url; do
    [[ -f "$f" ]] && OLLAMA_URL=$(cat "$f") && break
done
[[ -z "$OLLAMA_URL" ]] && OLLAMA_URL="http://localhost:11434"
export OLLAMA_HOST="$OLLAMA_URL"

if ! curl -s "${OLLAMA_URL}/v1/models" >/dev/null 2>&1; then
    echo "[setup_hint_model] Ollama not reachable at ${OLLAMA_URL}" >&2
    exit 1
fi

# Check llama3.2:3b is pulled
if ! curl -s "${OLLAMA_URL}/api/tags" | python3 -c \
    "import sys,json; d=json.load(sys.stdin); assert any('llama3.2:3b' in m['name'] for m in d['models'])" 2>/dev/null; then
    echo "[setup_hint_model] llama3.2:3b not pulled — run: ollama pull llama3.2:3b" >&2
    exit 1
fi

echo "[setup_hint_model] Merging LoRA adapter into llama3.2:3b..."
mkdir -p "$SCRATCH"

# Find the llama3.2:3b GGUF blob via Ollama manifest
BLOB=$(python3 - <<'EOF'
import json, os, pathlib
manifest_dir = pathlib.Path.home() / ".ollama/models/manifests/registry.ollama.ai/library/llama3.2/3b"
if not manifest_dir.exists():
    manifest_dir = pathlib.Path.home() / ".ollama/models/manifests/registry.ollama.ai/library/llama3.2/latest"
manifest = json.loads(next(manifest_dir.iterdir()).read_text())
for layer in manifest["layers"]:
    if layer["mediaType"] == "application/vnd.ollama.image.model":
        digest = layer["digest"].replace("sha256:", "sha256-")
        blob = pathlib.Path.home() / f".ollama/models/blobs/{digest}"
        if blob.exists():
            print(blob)
            break
EOF
)

if [[ -z "$BLOB" || ! -f "$BLOB" ]]; then
    echo "[setup_hint_model] could not locate llama3.2:3b blob" >&2
    exit 1
fi
echo "[setup_hint_model] base GGUF: $BLOB"

# Merge LoRA adapter into base model and save as safetensors
python3 - <<PYEOF
import os, sys, torch
from pathlib import Path
from transformers import AutoModelForCausalLM, AutoTokenizer
from peft import PeftModel

SCRATCH    = "$SCRATCH"
ADAPTER    = "$ADAPTER_DIR"
BLOB       = "$BLOB"
BASE_GGUF  = Path(SCRATCH) / "base.gguf"

# Symlink the blob so transformers can find it by extension
BASE_GGUF.symlink_to(BLOB)

print("[setup_hint_model] loading base model from GGUF (bf16)...")
model = AutoModelForCausalLM.from_pretrained(
    SCRATCH, gguf_file="base.gguf",
    torch_dtype=torch.bfloat16, device_map="cpu",
)
tokenizer = AutoTokenizer.from_pretrained(SCRATCH, gguf_file="base.gguf")
if tokenizer.pad_token is None:
    tokenizer.pad_token = tokenizer.eos_token

print("[setup_hint_model] applying LoRA adapter...")
model = PeftModel.from_pretrained(model, ADAPTER, torch_dtype=torch.bfloat16)
model = model.merge_and_unload()

out = Path(SCRATCH) / "merged"
out.mkdir(exist_ok=True)
print(f"[setup_hint_model] saving merged model to {out}...")
model.save_pretrained(out)
tokenizer.save_pretrained(out)

# Remove the stray GGUF that save_pretrained may have written as tokenizer.model
for f in out.glob("tokenizer.model"):
    if f.stat().st_size > 1_000_000:
        f.rename(str(f) + ".bak")
        print("[setup_hint_model] moved stray GGUF tokenizer.model aside")

print("[setup_hint_model] merge done")
PYEOF

echo "[setup_hint_model] creating Ollama model..."
cat > "$MODELFILE" <<MODELFILE
FROM ${SCRATCH}/merged

TEMPLATE """{{ if .System }}<|begin_of_text|><|start_header_id|>system<|end_header_id|>
{{ .System }}<|eot_id|>
{{ end }}<|start_header_id|>user<|end_header_id|>
{{ .Prompt }}<|eot_id|>
<|start_header_id|>assistant<|end_header_id|>
"""

SYSTEM """You are a memory indexer. Given a user statement, output a single short third-person retrieval fact (8-15 words). Output only the fact. If there is no personal fact, output: -"""

PARAMETER temperature 0.1
PARAMETER num_predict 48
PARAMETER stop "<|eot_id|>"
PARAMETER stop "<|end_of_text|>"
MODELFILE

ollama create chitta-hint-tuned -f "$MODELFILE"
echo "[setup_hint_model] Done — chitta-hint-tuned registered."
echo "  Test: ollama run chitta-hint-tuned 'I moved to Copenhagen last year for work'"

rm -rf "$SCRATCH" "$MODELFILE"
