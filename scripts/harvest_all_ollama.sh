#!/usr/bin/env bash
# Harvest vocab_geometry from every Ollama model and rebuild the constellation.
#
# Usage:
#   ./harvest_all_ollama.sh [--soul-anchor] [--gpu-url URL] [--out DIR]
#
# Defaults:
#   --gpu-url    http://dandygpun01fl:11434   (Ollama on GPU node)
#   --out        /projects/caeg/scratch/kbd606/tmp/harvest_ow
#   --embed-model qwen2.5:32b                (model used for /api/embed calls)
#
# What it does:
#   1. ollama list → all available models
#   2. For each model: check if harvest JSON already up-to-date; skip if so
#   3. Find GGUF via `ollama show --modelfile`
#   4. Run harvest_ow.py --mode vocab_geometry (optionally --soul-anchor)
#   5. build_constellation.py on all outputs
#   6. chitta seed_hdc_geometry --json_path constellation.json
#
# Add to cron or run after `ollama pull <new-model>` to keep the constellation current.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYBIN="${PYBIN:-/maps/projects/fernandezguerra/apps/opt/conda/envs/bioinfo/bin/python3}"
CHITTA="${CHITTA:-$HOME/.claude/bin/chitta}"
HARVEST="$SCRIPT_DIR/harvest_ow.py"
BUILD_CONST="$SCRIPT_DIR/build_constellation.py"

OUT_DIR="${OUT_DIR:-/projects/caeg/scratch/kbd606/tmp/harvest_ow}"
GPU_URL="${GPU_URL:-http://dandygpun01fl:11434}"
EMBED_MODEL="${EMBED_MODEL:-qwen2.5:32b}"
SOUL_ANCHOR="${SOUL_ANCHOR:-0}"
SCOPE="${SCOPE:-$OUT_DIR/harvest_targets.json}"
MODEL_STRIDE="${MODEL_STRIDE:-1}"   # process every Nth model
MODEL_OFFSET="${MODEL_OFFSET:-0}"   # starting index (0-based)

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --soul-anchor)    SOUL_ANCHOR=1; shift ;;
        --gpu-url)        GPU_URL="$2"; shift 2 ;;
        --out)            OUT_DIR="$2"; shift 2 ;;
        --embed-model)    EMBED_MODEL="$2"; shift 2 ;;
        --stride)         MODEL_STRIDE="$2"; shift 2 ;;
        --offset)         MODEL_OFFSET="$2"; shift 2 ;;
        *) echo "Unknown arg: $1" >&2; exit 1 ;;
    esac
done

mkdir -p "$OUT_DIR"
echo "[harvest-all] GPU Ollama: $GPU_URL | embed-model: $EMBED_MODEL | soul-anchor: $SOUL_ANCHOR"

# Skip models that don't have a local GGUF (e.g. HuggingFace hub refs without blob)
# or are known embedding-only models
SKIP_PATTERNS=("embed" "nomic" "all-minilm" "bge-")

harvest_one() {
    local model="$1"
    local safe_name="${model//[:\/ ]/-}"
    local out_json="$OUT_DIR/${safe_name}_vocab_geometry.json"
    local log="$OUT_DIR/${safe_name}_vocab_geometry.log"

    # Skip if already done (JSON exists and is newer than 7 days)
    if [[ -f "$out_json" ]] && [[ $(find "$out_json" -mtime -7 2>/dev/null | wc -l) -gt 0 ]]; then
        echo "[harvest-all] skip $model (harvest fresh, <7d)"
        return 0
    fi

    # Check skip patterns
    for pat in "${SKIP_PATTERNS[@]}"; do
        if [[ "$model" == *"$pat"* ]]; then
            echo "[harvest-all] skip $model (embedding model)"
            return 0
        fi
    done

    # Find GGUF
    local gguf
    gguf=$(ollama show --modelfile "$model" 2>/dev/null | awk '/^FROM/{print $2; exit}')
    if [[ -z "$gguf" || ! -f "$gguf" ]]; then
        echo "[harvest-all] skip $model (GGUF not found: $gguf)"
        return 0
    fi

    echo "[harvest-all] harvesting $model (GGUF: $(basename "$gguf")) ..."
    local soul_flag=""
    [[ "$SOUL_ANCHOR" == "1" ]] && soul_flag="--soul-anchor --chitta-bin $CHITTA"

    "$PYBIN" "$HARVEST" \
        --gguf "$gguf" \
        --ollama-model "$EMBED_MODEL" \
        --ollama-base-url "$GPU_URL" \
        --mode vocab_geometry \
        --provenance ow_distilled \
        --scope "$SCOPE" \
        --output "$out_json" \
        $soul_flag \
        > "$log" 2>&1
    local rc=$?
    if [[ $rc -eq 0 ]]; then
        echo "[harvest-all] done $model → $out_json"
    else
        echo "[harvest-all] FAILED $model (rc=$rc) — see $log" >&2
    fi
    return $rc
}

# Get all models from Ollama
mapfile -t MODELS < <(ollama list 2>/dev/null | tail -n +2 | awk '{print $1}')
echo "[harvest-all] found ${#MODELS[@]} Ollama models"

# Harvest sequentially (each uses GPU embed — parallelising would OOM the GPU)
# With --stride N --offset K: process models at indices K, K+N, K+2N, ...
DONE=0
FAILED=0
for idx in "${!MODELS[@]}"; do
    [[ $(( (idx - MODEL_OFFSET) % MODEL_STRIDE )) -ne 0 ]] && continue
    [[ $idx -lt $MODEL_OFFSET ]] && continue
    harvest_one "${MODELS[$idx]}" && DONE=$((DONE+1)) || FAILED=$((FAILED+1))
done

echo "[harvest-all] harvest complete: $DONE done, $FAILED failed"

# Build constellation from all successful outputs
GEOM_FILES=("$OUT_DIR"/*_vocab_geometry.json)
if [[ ${#GEOM_FILES[@]} -lt 2 ]]; then
    echo "[harvest-all] not enough geometry files for constellation (need ≥2)" >&2
    exit 1
fi

CONST_JSON="$OUT_DIR/constellation.json"
echo "[harvest-all] building constellation from ${#GEOM_FILES[@]} geometry files ..."
"$PYBIN" "$BUILD_CONST" \
    --inputs "${GEOM_FILES[@]}" \
    --output "$CONST_JSON" \
    --n-directions 512 \
    --min-models 2 \
    --overlap-threshold 0.2

echo "[harvest-all] seeding HDC codebook from constellation ..."
"$CHITTA" seed_hdc_geometry --json_path "$CONST_JSON"

echo "[harvest-all] constellation seeded. $(date)"
