#!/usr/bin/env bash
# Deploy chitta-hint-tuned: register fine-tuned Qwen2.5-0.5B with Ollama.
# Requirements: Ollama running, model weights at MODEL_DIR.
# Run once per install (or after retraining).
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Fine-tuned merged weights (safetensors from finetune_hint_qwen.sh)
MODEL_DIR="${CHITTA_HINT_MODEL_DIR:-/maps/projects/caeg/scratch/kbd606/tmp/hint_qwen_out}"
GGUF_DIR="${CHITTA_HINT_GGUF_DIR:-/maps/projects/caeg/scratch/kbd606/tmp/hint_qwen_gguf}"
MODELFILE="${TMPDIR:-/tmp}/Modelfile.chitta-hint-tuned-$$"

if [[ ! -f "${MODEL_DIR}/config.json" ]]; then
    echo "[setup_hint_model] model weights not found at ${MODEL_DIR}" >&2
    echo "  Set CHITTA_HINT_MODEL_DIR or run finetune_hint_qwen.sh first" >&2
    exit 1
fi

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

# Prefer pre-quantized Q4_K_M GGUF if available, else import safetensors (F16)
FROM_PATH="${MODEL_DIR}"
if [[ -f "${GGUF_DIR}/chitta-hint-qwen-q4_k_m.gguf" ]]; then
    FROM_PATH="${GGUF_DIR}/chitta-hint-qwen-q4_k_m.gguf"
    echo "[setup_hint_model] Using Q4_K_M GGUF: ${FROM_PATH}"
elif [[ -f "${GGUF_DIR}/chitta-hint-qwen-f16.gguf" ]]; then
    FROM_PATH="${GGUF_DIR}/chitta-hint-qwen-f16.gguf"
    echo "[setup_hint_model] Using F16 GGUF: ${FROM_PATH}"
else
    echo "[setup_hint_model] Using safetensors dir (Ollama will convert to F16): ${FROM_PATH}"
fi

echo "[setup_hint_model] Creating Ollama model chitta-hint-tuned..."
cat > "$MODELFILE" <<MODELFILE
FROM ${FROM_PATH}

TEMPLATE """<|im_start|>system
{{ .System }}<|im_end|>
<|im_start|>user
{{ .Prompt }}<|im_end|>
<|im_start|>assistant
"""

SYSTEM """You are a memory indexer. Given a user statement, output a single short third-person retrieval fact (8-15 words) starting with 'User'. Output only the fact. If there is no personal fact, output exactly: -"""

PARAMETER temperature 0.1
PARAMETER num_predict 48
PARAMETER stop "<|im_end|>"
MODELFILE

ollama create chitta-hint-tuned -f "$MODELFILE"
rm -f "$MODELFILE"

echo "[setup_hint_model] Done — chitta-hint-tuned registered."
echo "  Test: ollama run chitta-hint-tuned 'I moved to Copenhagen last year for work'"
