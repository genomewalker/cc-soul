#!/bin/bash
# cc-soul plugin setup
#
# Builds chitta (C++ backend), downloads ONNX models.
#
# Usage:
#   ./setup.sh           # Full setup
#   ./setup.sh --quick   # Skip tests

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHITTA_DIR="$SCRIPT_DIR/chitta"
BUILD_DIR="$CHITTA_DIR/build"
BIN_DIR="$SCRIPT_DIR/bin"
MODELS_DIR="$CHITTA_DIR/models"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}╔══════════════════════════════════════╗${NC}"
echo -e "${BLUE}║        cc-soul Plugin Setup          ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════════╝${NC}"
echo ""

# Parse args
QUICK=false
while [[ $# -gt 0 ]]; do
    case $1 in
        --quick) QUICK=true; shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# Step 1: Check dependencies
echo -e "${YELLOW}[1/3] Checking dependencies...${NC}"

check_cmd() {
    if ! command -v "$1" &> /dev/null; then
        echo -e "${RED}  ✗ $1 not found${NC}"
        return 1
    fi
    echo -e "  ✓ $1"
    return 0
}

DEPS_OK=true
check_cmd cmake || DEPS_OK=false
check_cmd make || DEPS_OK=false

if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
    echo -e "${RED}  ✗ C++ compiler not found${NC}"
    DEPS_OK=false
fi

if [ "$DEPS_OK" = false ]; then
    echo ""
    echo -e "${RED}Missing dependencies. Install them and try again.${NC}"
    exit 1
fi

echo ""

# Step 2: Download ONNX models
echo -e "${YELLOW}[2/3] Downloading ONNX models...${NC}"

mkdir -p "$MODELS_DIR"

MODEL_URL="https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/main/onnx/model.onnx"
VOCAB_URL="https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/main/vocab.txt"

if [ -f "$MODELS_DIR/model.onnx" ] && [ -f "$MODELS_DIR/vocab.txt" ]; then
    echo -e "  Models already exist ($(du -h "$MODELS_DIR/model.onnx" | cut -f1))"
else
    echo "  Downloading all-MiniLM-L6-v2..."
    if command -v curl &> /dev/null; then
        curl -L -o "$MODELS_DIR/model.onnx" "$MODEL_URL" --progress-bar
        curl -L -o "$MODELS_DIR/vocab.txt" "$VOCAB_URL" --silent
    else
        wget -O "$MODELS_DIR/model.onnx" "$MODEL_URL" --show-progress
        wget -O "$MODELS_DIR/vocab.txt" "$VOCAB_URL" --quiet
    fi
    echo -e "  ${GREEN}✓ Models downloaded${NC}"
fi

echo ""

# Step 3: Build chitta
echo -e "${YELLOW}[3/3] Building chitta (C++)...${NC}"

if [ ! -d "$CHITTA_DIR/src" ]; then
    echo -e "${RED}  Chitta source not found at $CHITTA_DIR${NC}"
    exit 1
fi

mkdir -p "$BUILD_DIR" "$BIN_DIR"
cd "$BUILD_DIR"

echo "  Running cmake..."
cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null

echo "  Building..."
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) > /dev/null

if [ -f "$BIN_DIR/chitta_mcp" ]; then
    echo -e "  ${GREEN}✓ chitta_mcp built${NC}"
else
    echo -e "${RED}  ✗ Build failed${NC}"
    exit 1
fi

cd "$SCRIPT_DIR"
echo ""

# Create mind directory and symlinks
# The database lives at ~/.claude/mind/chitta.{hot,cold}
# We symlink into plugin root so ${CLAUDE_PLUGIN_ROOT} can be used in plugin.json
# (workaround for Claude Code not expanding ${HOME} in env vars)
mkdir -p "${HOME}/.claude/mind"
mkdir -p "$SCRIPT_DIR/mind"

# Create symlinks (or update if they exist)
for ext in hot warm cold; do
    ln -sfn "${HOME}/.claude/mind/chitta.$ext" "$SCRIPT_DIR/mind/chitta.$ext"
done
echo -e "  ${GREEN}✓ Database symlinks created${NC}"

# Done
echo -e "${GREEN}╔══════════════════════════════════════╗${NC}"
echo -e "${GREEN}║          Setup Complete!             ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════╝${NC}"
echo ""
echo "To use the plugin:"
echo ""
echo "  claude --plugin-dir $SCRIPT_DIR"
echo ""
echo "Or add to ~/.claude/settings.json permanently."
echo ""
