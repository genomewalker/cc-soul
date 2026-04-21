#!/usr/bin/env bash
# Train fasttext intent classifier for prompt-hook.sh
# Usage: train-hook-classifier.sh [--model-out PATH] [--no-synthetic] [--from-logs]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MIND_PATH="${MIND_PATH:-${HOME}/.claude/mind}"
TMP_DIR="${TMPDIR:-/maps/projects/caeg/scratch/kbd606/tmp}/hook-classifier-$$"
MODEL_OUT="${MIND_PATH}/hook-classifier"
SYNTHETIC_FLAG=""
LOGS_FLAG=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --model-out) MODEL_OUT="$2"; shift 2 ;;
        --no-synthetic) SYNTHETIC_FLAG="--no-synthetic"; shift ;;
        --from-logs) LOGS_FLAG="--from-logs"; shift ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

mkdir -p "$TMP_DIR" "$(dirname "$MODEL_OUT")"
trap 'rm -rf "$TMP_DIR"' EXIT

CORPUS="$TMP_DIR/corpus.txt"
TRAIN="$TMP_DIR/train.txt"
TEST="$TMP_DIR/test.txt"
BUNDLED_CORPUS_GZ="$SCRIPT_DIR/../data/hook-classifier-corpus.txt.gz"
BUNDLED_CORPUS="$SCRIPT_DIR/../data/hook-classifier-corpus.txt"

if [[ -z "$SYNTHETIC_FLAG" && -f "$BUNDLED_CORPUS_GZ" ]]; then
    echo "[train] Using bundled corpus: $BUNDLED_CORPUS_GZ"
    zcat "$BUNDLED_CORPUS_GZ" > "$CORPUS"
    python3 "$SCRIPT_DIR/gen-hook-corpus.py" \
        --out "$TMP_DIR/chitta-extra.txt" \
        --no-synthetic \
        --from-chitta \
        $LOGS_FLAG 2>&1 || true
    [[ -s "$TMP_DIR/chitta-extra.txt" ]] && zcat "$TMP_DIR/chitta-extra.txt" >> "$CORPUS" 2>/dev/null || cat "$TMP_DIR/chitta-extra.txt" >> "$CORPUS" || true
elif [[ -z "$SYNTHETIC_FLAG" && -f "$BUNDLED_CORPUS" ]]; then
    echo "[train] Using bundled corpus: $BUNDLED_CORPUS"
    cp "$BUNDLED_CORPUS" "$CORPUS"
    python3 "$SCRIPT_DIR/gen-hook-corpus.py" \
        --out "$TMP_DIR/chitta-extra.txt" \
        --no-synthetic \
        --from-chitta \
        $LOGS_FLAG 2>&1 || true
    [[ -s "$TMP_DIR/chitta-extra.txt" ]] && cat "$TMP_DIR/chitta-extra.txt" >> "$CORPUS" || true
else
    echo "[train] Generating corpus (this calls haiku, ~10 min ~\$0.10)..."
    python3 "$SCRIPT_DIR/gen-hook-corpus.py" \
        --out "$CORPUS" \
        $SYNTHETIC_FLAG \
        $LOGS_FLAG \
        --from-chitta 2>&1
fi

TOTAL=$(wc -l < "$CORPUS")
if [[ "$TOTAL" -lt 10 ]]; then
    echo "[train] ERROR: corpus too small ($TOTAL lines) — run without --no-synthetic"
    exit 1
fi

SPLIT=$(( TOTAL * 80 / 100 ))
head -n "$SPLIT" "$CORPUS" > "$TRAIN"
tail -n "+$((SPLIT + 1))" "$CORPUS" > "$TEST"
echo "[train] corpus=$TOTAL train=$SPLIT test=$((TOTAL - SPLIT))"

echo "[train] Training fasttext..."
python3 - "$TRAIN" "$TEST" "$MODEL_OUT" <<'PYEOF'
import fasttext, sys

model = fasttext.train_supervised(
    input=sys.argv[1],
    epoch=30,
    lr=0.5,
    wordNgrams=2,
    dim=32,
    loss="softmax",
    minCount=2,
    verbose=0,
)

n, precision, recall = model.test(sys.argv[2])
print(f"[train] test: n={n} precision={precision:.3f} recall={recall:.3f}")

result = model.test_label(sys.argv[2])
for label in ["correction", "preference", "belief", "milestone", "neutral"]:
    if label in result:
        r = result[label]
        print(f"  {label:12s}: P={r['precision']:.3f} R={r['recall']:.3f} F1={r['f1score']:.3f}")

model.quantize(input=sys.argv[1], qnorm=True, retrain=True, cutoff=50000)
model.save_model(sys.argv[3] + ".bin")

import os
size_mb = os.path.getsize(sys.argv[3] + ".bin") / 1e6
print(f"[train] model saved → {sys.argv[3]}.bin ({size_mb:.1f} MB)")
PYEOF

echo "[train] Done. Quick test:"
echo "  I prefer you always use file_patch" | python3 "$SCRIPT_DIR/classify-intent.py" "${MODEL_OUT}.bin"
echo "  you got that wrong, should be file_patch" | python3 "$SCRIPT_DIR/classify-intent.py" "${MODEL_OUT}.bin"
echo "  we always use cmake in this project" | python3 "$SCRIPT_DIR/classify-intent.py" "${MODEL_OUT}.bin"
echo "  it finally works!" | python3 "$SCRIPT_DIR/classify-intent.py" "${MODEL_OUT}.bin"
echo "  how do I debug a segfault in C++?" | python3 "$SCRIPT_DIR/classify-intent.py" "${MODEL_OUT}.bin"
