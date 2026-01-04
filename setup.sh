#!/bin/bash
# cc-soul plugin setup
#
# Builds synapse (C++ backend), downloads ONNX models, installs Python CLI.
#
# Usage:
#   ./setup.sh           # Full setup
#   ./setup.sh --quick   # Skip tests

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SYNAPSE_DIR="$SCRIPT_DIR/synapse"
BUILD_DIR="$SYNAPSE_DIR/build"
MODELS_DIR="$SYNAPSE_DIR/models"

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
echo -e "${YELLOW}[1/4] Checking dependencies...${NC}"

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
check_cmd python3 || DEPS_OK=false
check_cmd pip || DEPS_OK=false

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
echo -e "${YELLOW}[2/4] Downloading ONNX models...${NC}"

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

# Step 3: Build synapse
echo -e "${YELLOW}[3/4] Building synapse (C++)...${NC}"

if [ ! -d "$SYNAPSE_DIR/src" ]; then
    echo -e "${RED}  Synapse source not found at $SYNAPSE_DIR${NC}"
    echo "  Copy synapse C++ source from cc-synapse repo"
    exit 1
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Use system gcc to avoid GLIBC version issues from conda gcc
if [ -f /usr/bin/gcc ] && [ -f /usr/bin/g++ ]; then
    export CC=/usr/bin/gcc
    export CXX=/usr/bin/g++
    echo "  Using system compiler: $(${CXX} --version | head -1)"
fi

echo "  Running cmake..."
cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null

echo "  Building..."
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) > /dev/null

if [ -f "$BUILD_DIR/synapse_mcp" ]; then
    echo -e "  ${GREEN}✓ synapse_mcp built${NC}"
else
    echo -e "${RED}  ✗ Build failed${NC}"
    exit 1
fi

cd "$SCRIPT_DIR"
echo ""

# Step 4: Install Python CLI
echo -e "${YELLOW}[4/4] Installing Python CLI...${NC}"

pip install -e "$SCRIPT_DIR" --quiet

if command -v cc-soul &> /dev/null; then
    echo -e "  ${GREEN}✓ cc-soul CLI installed${NC}"
else
    echo -e "${YELLOW}  Warning: cc-soul not in PATH. Add ~/.local/bin to PATH.${NC}"
fi

echo ""

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
echo "Available commands:"
echo "  /soul:grow      Add wisdom, beliefs, failures"
echo "  /soul:recall    Search the soul"
echo "  /soul:observe   Record observations"
echo "  /soul:context   View current state"
echo "  /soul:cycle     Run maintenance"
