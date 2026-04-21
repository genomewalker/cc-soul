#!/usr/bin/env python3
"""
Classify a user prompt into: correction|preference|belief|milestone|neutral
Usage: echo "text" | classify-intent.py <model.bin> [threshold]
Prints: "<label> <score>" or nothing if neutral/below threshold.
"""
import sys

def main() -> None:
    if len(sys.argv) < 2:
        sys.exit(1)
    model_path = sys.argv[1]
    threshold = float(sys.argv[2]) if len(sys.argv) > 2 else 0.55

    text = sys.stdin.read().strip().replace("\n", " ")
    if not text:
        sys.exit(0)

    try:
        import fasttext
        m = fasttext.load_model(model_path)
    except Exception:
        sys.exit(1)

    labels, scores = m.predict(text, k=1)
    label = labels[0].replace("__label__", "")
    score = float(scores[0])

    if label != "neutral" and score >= threshold:
        print(f"{label} {score:.3f}")

if __name__ == "__main__":
    main()
