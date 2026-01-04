#!/bin/bash
# Smart Install Script for cc-soul
#
# Ensures chitta binary is compiled and ONNX models are downloaded.
# Runs as first hook on SessionStart.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(dirname "$SCRIPT_DIR")"
CHITTA_DIR="$PLUGIN_DIR/chitta"
BUILD_DIR="$CHITTA_DIR/build"
MODELS_DIR="$CHITTA_DIR/models"
MARKER="$PLUGIN_DIR/.install-complete"

# Check if already installed
current_version=$(cat "$PLUGIN_DIR/.claude-plugin/plugin.json" 2>/dev/null | grep '"version"' | cut -d'"' -f4 || echo "0.0.0")
installed_version=$(cat "$MARKER" 2>/dev/null || echo "")

if [[ "$current_version" == "$installed_version" && -x "$BUILD_DIR/chitta_mcp" && -f "$MODELS_DIR/model.onnx" ]]; then
    exit 0  # Already installed
fi

echo "[cc-soul] Installing dependencies..."

# Check for cmake
if ! command -v cmake &> /dev/null; then
    echo "[cc-soul] ERROR: cmake not found. Please install cmake." >&2
    exit 1
fi

# Download ONNX models if missing
if [[ ! -f "$MODELS_DIR/model.onnx" || ! -f "$MODELS_DIR/vocab.txt" ]]; then
    echo "[cc-soul] Downloading embedding model..."
    mkdir -p "$MODELS_DIR"

    # Download all-MiniLM-L6-v2 ONNX model
    MODEL_URL="https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/main/onnx/model.onnx"
    VOCAB_URL="https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/main/vocab.txt"

    if command -v curl &> /dev/null; then
        curl -L -o "$MODELS_DIR/model.onnx" "$MODEL_URL" 2>/dev/null || {
            echo "[cc-soul] WARNING: Could not download model.onnx" >&2
        }
        curl -L -o "$MODELS_DIR/vocab.txt" "$VOCAB_URL" 2>/dev/null || {
            echo "[cc-soul] WARNING: Could not download vocab.txt" >&2
        }
    elif command -v wget &> /dev/null; then
        wget -q -O "$MODELS_DIR/model.onnx" "$MODEL_URL" || {
            echo "[cc-soul] WARNING: Could not download model.onnx" >&2
        }
        wget -q -O "$MODELS_DIR/vocab.txt" "$VOCAB_URL" || {
            echo "[cc-soul] WARNING: Could not download vocab.txt" >&2
        }
    else
        echo "[cc-soul] WARNING: No curl or wget. Models not downloaded." >&2
    fi
fi

# Build chitta if missing or version changed
if [[ ! -x "$BUILD_DIR/chitta_mcp" || "$current_version" != "$installed_version" ]]; then
    echo "[cc-soul] Building chitta..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    cmake .. -DCMAKE_BUILD_TYPE=Release 2>/dev/null || {
        echo "[cc-soul] ERROR: cmake configuration failed" >&2
        exit 1
    }

    make -j$(nproc 2>/dev/null || echo 4) chitta_mcp 2>/dev/null || {
        echo "[cc-soul] ERROR: build failed" >&2
        exit 1
    }

    echo "[cc-soul] Build complete"
fi

# Create mind directory
mkdir -p "${HOME}/.claude/mind/chitta"

# Mark as installed
echo "$current_version" > "$MARKER"
echo "[cc-soul] Installation complete"
